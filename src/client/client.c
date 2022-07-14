#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <strings.h>

#include <zmq.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include <eyeq/client.h>
#include <eyeq/samples.pb.h>

// Default timeout
#define DEFAULT_TIMEOUT_MS 2000

void *eyeq_create_context() {
    return zmq_ctx_new();
}

void eyeq_destroy_context(void *eyeq_context) {
    zmq_ctx_destroy(eyeq_context);
}

eyeq_client_t *eyeq_connect(const char *endpoint, void *zmq_context, long timeout_ms) {
    eyeq_client_t *client = calloc(sizeof(eyeq_client_t), 1);
    if (!client) {
        return NULL;
    }

    client->timeout_ms = timeout_ms > 0 ? timeout_ms : DEFAULT_TIMEOUT_MS;
    client->socket = zmq_socket(zmq_context, ZMQ_DEALER);

    int rc = zmq_connect(client->socket, endpoint);
    if (rc) {
        fprintf(stderr, "Error connecting with ZMQ socket: %d\n", rc);
        zmq_close(client->socket);
        free(client);

        return NULL;
    }

    return client;
}

// Performs a transaction with the EyeQ server.
static int eyeq_transaction(
    eyeq_client_t *client,
    eyeq_ServerRequest *request,
    eyeq_ServerResponse *response,
    int expected_response_tag,
    bool (*response_callback)(eyeq_ServerResponse *response, void *context),
    int expected_responses, // Wait until there are no more responses
    int timeout_ms,
    void *context) {

    uint8_t request_buffer[eyeq_ServerRequest_size];
    uint8_t response_buffer[eyeq_ServerResponse_size];

    request->req_id = client->req_id;
    client->req_id++;

    pb_ostream_t ostream = pb_ostream_from_buffer(request_buffer, eyeq_ServerRequest_size);

    if (!pb_encode(&ostream, eyeq_ServerRequest_fields, request)) {
        return EYEQ_ENCODING_ERROR;
    }

    zmq_send(client->socket, 0, 0, ZMQ_SNDMORE);
    zmq_send(client->socket, request_buffer, ostream.bytes_written, 0);

    // Wait for response
    while (true) {
        zmq_pollitem_t items[1];
        /* First item refers to ØMQ socket 'socket' */
        items[0].socket = client->socket;
        items[0].events = ZMQ_POLLIN;
        /* Poll for events indefinitely */
        int rc = zmq_poll(items, 1, (long)timeout_ms);
        if (!rc) {
            return EYEQ_NETWORK_TIMEOUT;
        } else if (rc < 0) {
            return EYEQ_NETWORK_ERROR;
        }

        // Receive empty frame
        int nbytes = zmq_recv(client->socket, response_buffer, eyeq_ServerRequest_size, 0);
        if (nbytes) {
            continue;
        }

        int value;
        size_t option_len = 4;
        if (zmq_getsockopt(client->socket, ZMQ_RCVMORE, &value, &option_len)) {
            fprintf(stderr, "Error while running zmq_getsockopt: %s\n", strerror(errno));
            return EYEQ_INVALID_RESPONSE;
        }
        if (!value) {
            continue;
        }

        // Receive contents
        nbytes = zmq_recv(client->socket, response_buffer, eyeq_ServerResponse_size, 0);
        if (nbytes == -1) {
            return EYEQ_NETWORK_ERROR;
        }

        pb_istream_t stream = pb_istream_from_buffer(response_buffer, nbytes);
        if (pb_decode(&stream, eyeq_ServerResponse_fields, response)) {
            if (response->req_id != request->req_id) {
                continue;
            }

            if (!response->which_resp) {
                fprintf(stderr, "Error while performing EyeQ request: %s\n", response->error);
                return EYEQ_ERROR;
            }

            if (response->which_resp != expected_response_tag) {
                return EYEQ_INVALID_RESPONSE;
            }

            // We use the user-supplied callback function if defined.
            if (!response_callback) {
                // The result will be contained in the response object.
                return EYEQ_OK;
            }

            if (!response_callback(response, context)) {
                // If response_callback returns false, stop the iteration
                return EYEQ_REQUEST_ABORTED;
            } else if (--expected_responses <= 0) {
                // Return immediately if we don't expect more responses
                return EYEQ_OK;
            } else {
                // Continue to receive the next response
                continue;
            }
        } else {
            fprintf(stderr, "Could not decode EyeQ packet of length %d", nbytes);
            return EYEQ_INVALID_RESPONSE;
        }
    }
}

// Create store
int eyeq_create_store(eyeq_client_t *client, eyeq_Store *store) {
    eyeq_ServerResponse response;

    eyeq_ServerRequest request;
    request.which_req = eyeq_ServerRequest_create_store_tag;
    memcpy(&request.req.create_store.store, store, sizeof(eyeq_Store));

    return eyeq_transaction(client, &request, &response, eyeq_ServerResponse_create_store_response_tag, NULL, 1, client->timeout_ms, NULL);
}

int eyeq_list_stores(eyeq_client_t *client, const char *path, bool (*list_stores_callback)(eyeq_Store *store), void *context) {
    eyeq_ServerResponse response;

    eyeq_ServerRequest request;
    request.which_req = eyeq_ServerRequest_list_stores_tag;
    strncpy(request.req.list_stores.path, path, STORE_MAX_PATH);
    request.req.list_stores.path[STORE_MAX_PATH] = '\0';

    int resp = eyeq_transaction(client, &request, &response, eyeq_ServerResponse_list_stores_response_tag, NULL, 1, client->timeout_ms, NULL);
    if (resp) {
        return resp;
    }

    for (int i = 0; i < response.resp.list_stores_response.stores_count; i++) {
        eyeq_Store *store = &response.resp.list_stores_response.stores[i];
        if (!list_stores_callback(store)) {
            break;
        }
    }

    return EYEQ_OK;
}

int eyeq_delete_store(eyeq_client_t *client, const char *name, const char *path) {
    eyeq_ServerResponse response;
    eyeq_ServerRequest request;

    request.which_req = eyeq_ServerRequest_delete_store_tag;
    strncpy(request.req.delete_store.name, name, STORE_MAX_NAME);
    request.req.delete_store.name[STORE_MAX_NAME] = '\0';
    strncpy(request.req.delete_store.path, path, STORE_MAX_PATH);
    request.req.delete_store.path[STORE_MAX_PATH] = '\0';

    return eyeq_transaction(client, &request, &response, eyeq_ServerResponse_delete_store_response_tag, NULL, 1, client->timeout_ms, NULL);
}

int eyeq_write_block(eyeq_client_t *client, const char *name, const char *path, eyeq_Block *block, int32_t offset, uint32_t *written_offset) {
    eyeq_ServerResponse response;
    eyeq_ServerRequest request;

    request.which_req = eyeq_ServerRequest_write_block_tag;
    strncpy(request.req.write_block.name, name, STORE_MAX_NAME);
    request.req.write_block.name[STORE_MAX_NAME] = '\0';
    strncpy(request.req.write_block.path, path, STORE_MAX_PATH);
    request.req.write_block.path[STORE_MAX_PATH] = '\0';
    request.req.write_block.offset = offset;
    memcpy(&request.req.write_block.block, block, sizeof(eyeq_Block));

    int resp = eyeq_transaction(client, &request, &response, eyeq_ServerResponse_write_block_response_tag, NULL, 1, client->timeout_ms, NULL);
    if (resp) {
        return resp;
    }

    if (written_offset) {
        *written_offset = response.resp.write_block_response.offset;
    }

    return EYEQ_OK;
}

struct read_block_callback_context_t {
    bool (*read_block_callback)(eyeq_Block *block, void *context);
    void *inner_context;
};

// Unpack the response and just forward the block itself.
static bool read_blocks_callback(eyeq_ServerResponse *response, void *context) {
    struct read_block_callback_context_t *ctx = context;
    if (ctx->read_block_callback) {
        return ctx->read_block_callback(&response->resp.read_blocks_response.block, ctx->inner_context);
    }

    // If no callback is provided, return early
    return false;
}

int eyeq_read_blocks(
    eyeq_client_t *client,
    const char *name,
    const char *path,
    uint32_t offset,
    uint32_t count,
    bool (*read_block_callback)(eyeq_Block *block, void *context),
    long timeout_ms,
    void *context) {

    eyeq_ServerResponse response;
    eyeq_ServerRequest request;

    struct read_block_callback_context_t read_block_ctx = {
        .read_block_callback = read_block_callback,
        .inner_context = context,
    };

    request.which_req = eyeq_ServerRequest_read_blocks_tag;
    strncpy(request.req.read_blocks.name, name, STORE_MAX_NAME);
    request.req.read_blocks.name[STORE_MAX_NAME] = '\0';
    strncpy(request.req.read_blocks.path, path, STORE_MAX_PATH);
    request.req.read_blocks.path[STORE_MAX_PATH] = '\0';
    request.req.read_blocks.offset = offset;
    request.req.read_blocks.count = count;

    return eyeq_transaction(client, &request, &response, eyeq_ServerResponse_read_blocks_response_tag, read_blocks_callback, count, timeout_ms, &read_block_ctx);
}

int eyeq_flush_stores(eyeq_client_t *client) {
    eyeq_ServerResponse response;
    eyeq_ServerRequest request;

    request.which_req = eyeq_ServerRequest_flush_stores_tag;

    return eyeq_transaction(client, &request, &response, eyeq_ServerResponse_flush_stores_response_tag, NULL, 1, client->timeout_ms, NULL);
}

int eyeq_find_store(eyeq_client_t *client, const char *name, const char *path, eyeq_Store *store) {
    eyeq_ServerResponse response;

    eyeq_ServerRequest request;
    request.which_req = eyeq_ServerRequest_list_stores_tag;
    strncpy(request.req.list_stores.path, path, STORE_MAX_PATH);
    request.req.list_stores.path[STORE_MAX_PATH] = '\0';

    int resp = eyeq_transaction(client, &request, &response, eyeq_ServerResponse_list_stores_response_tag, NULL, 1, client->timeout_ms, NULL);
    if (resp) {
        return resp;
    }

    for (int i = 0; i < response.resp.list_stores_response.stores_count; i++) {
        eyeq_Store *_store = &response.resp.list_stores_response.stores[i];
        if (!strncmp(_store->name, name, STORE_MAX_NAME) && 
            !strncmp(_store->path, path, STORE_MAX_PATH)) {
            memcpy(store, _store, sizeof(eyeq_Store));
            return EYEQ_OK;
        }
    }

    return EYEQ_NOT_FOUND;
}


struct read_stream_ctx {
    float *output;
    uint32_t written;
};

static bool read_stream_callback(eyeq_ServerResponse *response, void *context) {
    struct read_stream_ctx *ctx = context;

    memcpy(&ctx->output[ctx->written], &response->resp.read_stream_response.samples[0], response->resp.read_stream_response.samples_count * sizeof(float));
    ctx->written += response->resp.read_stream_response.samples_count;

    return !response->resp.read_stream_response.eos;
}

int eyeq_read_stream(
    eyeq_client_t *client,
    const char *name,
    const char *path,
    float *output,
    uint32_t count,
    long timeout_ms,
    uint32_t *written,
    uint32_t *block_id) {

    eyeq_ServerResponse response;
    eyeq_ServerRequest request;

    request.which_req = eyeq_ServerRequest_read_stream_tag;
    strncpy(request.req.read_stream.name, name, STORE_MAX_NAME);
    strncpy(request.req.read_stream.path, path, STORE_MAX_PATH);
    request.req.read_stream.name[STORE_MAX_NAME] = '\0';
    request.req.read_stream.path[STORE_MAX_PATH] = '\0';
    request.req.read_stream.sample_count = count;

    struct read_stream_ctx read_block_ctx = {
        .output = output,
        .written = 0,
    };

    int res = eyeq_transaction(client, &request, &response, eyeq_ServerResponse_read_stream_response_tag, read_stream_callback, (count + 4095) / 4096, timeout_ms, &read_block_ctx);
    if (written) {
        *written = read_block_ctx.written;
    }

    if (block_id) {
        *block_id = response.resp.read_stream_response.block;
    }

    return res;
}

int eyeq_list_streams(eyeq_client_t *client, const char *path, bool (*list_streams_callback)(eyeq_Stream *stream), void *context) {
    eyeq_ServerResponse response;
    eyeq_ServerRequest request;

    request.which_req = eyeq_ServerRequest_list_streams_tag;
    strncpy(request.req.list_streams.path, path, STORE_MAX_PATH);
    request.req.list_streams.path[STORE_MAX_PATH] = '\0';

    int resp = eyeq_transaction(client, &request, &response, eyeq_ServerResponse_list_streams_response_tag, NULL, 1, client->timeout_ms, NULL);
    if (resp) {
        return resp;
    }

    for (int i = 0; i < response.resp.list_streams_response.streams_count; i++) {
        eyeq_Stream *stream = &response.resp.list_streams_response.streams[i];
        if (!list_streams_callback(stream)) {
            break;
        }
    }

    return EYEQ_OK;
}

int eyeq_seek_stream(
    eyeq_client_t *client,
    const char *name,
    const char *path,
    uint32_t block_id) {

    eyeq_ServerResponse response;
    eyeq_ServerRequest request;

    request.which_req = eyeq_ServerRequest_seek_stream_tag;
    strncpy(request.req.seek_stream.name, name, STORE_MAX_NAME);
    strncpy(request.req.seek_stream.path, path, STORE_MAX_PATH);
    request.req.seek_stream.name[STORE_MAX_NAME] = '\0';
    request.req.seek_stream.path[STORE_MAX_PATH] = '\0';
    request.req.seek_stream.block_id = block_id;

    return eyeq_transaction(client, &request, &response, eyeq_ServerResponse_seek_stream_response_tag, NULL, 1, client->timeout_ms, NULL);
}

int eyeq_create_frequency_filter_stream(
    eyeq_client_t *client,
    const char *name,
    const char *path,
    const char *store_name,
    const char *store_path,
    uint32_t start_block,
    uint32_t block_count,
    float relative_frequency,
    const float *filter_taps,
    int ntaps)
{

    eyeq_ServerResponse response;
    eyeq_ServerRequest request = { 0 };

    eyeq_CreateStream *cs = &request.req.create_stream;

    request.which_req = eyeq_ServerRequest_create_stream_tag;
    strncpy(cs->stream.name, name, STORE_MAX_NAME);
    strncpy(cs->stream.path, path, STORE_MAX_PATH);
    cs->stream.name[STORE_MAX_NAME] = '\0';
    cs->stream.path[STORE_MAX_PATH] = '\0';

    cs->layers_count = 1;
    eyeq_StreamLayer *l = &cs->layers[0];
    l->which_layer = eyeq_StreamLayer_store_reader_tag;
    eyeq_StoreReaderStream *sr = &l->layer.store_reader;

    strncpy(sr->name, store_name, STORE_MAX_NAME);
    strncpy(sr->path, store_path, STORE_MAX_PATH);
    sr->name[STORE_MAX_NAME] = '\0';
    sr->path[STORE_MAX_PATH] = '\0';
    sr->start_block = start_block;
    sr->end_block = start_block + block_count;

    if (relative_frequency != 0) {
        eyeq_StreamLayer *ftl = &cs->layers[cs->layers_count];
        ftl->which_layer = eyeq_StreamLayer_frequency_translate_tag;
        ftl->layer.frequency_translate.relative_frequency = relative_frequency;
        cs->layers_count++;
    }

    if (ntaps > 0) {
        eyeq_StreamLayer *ffl = &cs->layers[cs->layers_count];
        ffl->which_layer = eyeq_StreamLayer_fir_filter_tag;
        ffl->layer.fir_filter.is_complex = true;
        for (int i = 0; i < ntaps; i++) {
            ffl->layer.fir_filter.filter_taps[i*2] = filter_taps[i];
        }
        ffl->layer.fir_filter.filter_taps_count = ntaps * 2;
        cs->layers_count++;
    }

    return eyeq_transaction(client, &request, &response, eyeq_ServerResponse_create_stream_response_tag, NULL, 1, client->timeout_ms, NULL);
}

int eyeq_close_stream(eyeq_client_t *client, const char *name, const char *path) {
    eyeq_ServerResponse response;
    eyeq_ServerRequest request;

    request.which_req = eyeq_ServerRequest_close_stream_tag;
    strncpy(request.req.close_stream.name, name, STORE_MAX_NAME);
    request.req.close_stream.name[STORE_MAX_NAME] = '\0';
    strncpy(request.req.close_stream.path, path, STORE_MAX_PATH);
    request.req.close_stream.path[STORE_MAX_PATH] = '\0';

    return eyeq_transaction(client, &request, &response, eyeq_ServerResponse_delete_store_response_tag, NULL, 1, client->timeout_ms, NULL);
}


void eyeq_close(eyeq_client_t *client) {
    zmq_close(client->socket);
    free(client);
}
