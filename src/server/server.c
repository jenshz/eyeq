#include <stdio.h>
#include <unistd.h>
#include <assert.h>
#include <strings.h>
#include <errno.h>

#include <zmq.h>
#include <eyeq/server.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include <eyeq/server/store.h>
#include "stream/stream.h"
#include "../proto/samples.pb.h"
#include "util.h"

typedef struct {
    uint8_t ident[100];
    int ident_length;
    uint32_t req_id;
    void *responder;
    uint8_t output_buffer[eyeq_ServerResponse_size];
    store_list_t *stores;
    stream_list_t *streams;
    eyeq_ServerResponse response;
} server_context_t;

static void send_response(server_context_t *ctx) {
    // TODO: Set ident
    zmq_send(ctx->responder, ctx->ident, ctx->ident_length, ZMQ_SNDMORE);
    zmq_send(ctx->responder, 0, 0, ZMQ_SNDMORE);
    ctx->response.req_id = ctx->req_id;
    pb_ostream_t ostream = pb_ostream_from_buffer(ctx->output_buffer, eyeq_ServerResponse_size);
    if (pb_encode(&ostream, eyeq_ServerResponse_fields, &ctx->response)) {
        zmq_send(ctx->responder, ctx->output_buffer, ostream.bytes_written, 0);
    } else {
        zmq_send(ctx->responder, "", 0, 0);
    }
}

static void handle_create_store(server_context_t *ctx, eyeq_Store *request) {
    if (find_store(request->name, request->path, ctx->stores)) {
        sprintf(ctx->response.error, "Store already exists");
        send_response(ctx);
        return;
    }

    store_t *store;

    if (request->store_type == eyeq_StoreType_MEMORY_STORE) {
        store = new_memory_store(request->block_count);
    } else if (request->store_type == eyeq_StoreType_FILE_STORE) {
        store = new_file_store(request->file_path, request->block_count, true);
    } else {
        sprintf(ctx->response.error, "Unknown store type %d!", request->store_type);
        send_response(ctx);
        return;
    }

    if (!store) {
        sprintf(ctx->response.error, "Could not create store");
        send_response(ctx);
        return;
    }

    int res = add_store(request->name, request->path, ctx->stores, store);
    if (res != STORE_OK) {
        free_store(store);
        sprintf(ctx->response.error, "Could not add store to store list: %d", res);
        send_response(ctx);
        return;
    }

    ctx->response.which_resp = eyeq_ServerResponse_create_store_response_tag;
    memcpy(&ctx->response.resp.create_store_response.store, request, sizeof(eyeq_Store));
    send_response(ctx);
}

bool store_list_iterator(void *context, const char *name, const char *path, store_t *store) {
    eyeq_ServerResponse *sr = ((eyeq_ServerResponse *)context);
    eyeq_Store *s = &sr->resp.list_stores_response.stores[sr->resp.list_stores_response.stores_count++];
    sprintf(s->name, name, STORE_MAX_NAME);
    sprintf(s->path, path, STORE_MAX_PATH);
    s->block_count = store->block_count;
    s->block_offset = store->write_offset;
    s->store_type = store->store_type;

    return sr->resp.list_stores_response.stores_count >= 64;
}

// iterate_store_list(const char *path, store_list_t *list, iterate_stores_callback cb, void *context);
static void handle_list_stores(server_context_t *ctx, eyeq_ListStores *request) {
    iterate_store_list(request->path, ctx->stores, store_list_iterator, &ctx->response);
    ctx->response.which_resp = eyeq_ServerResponse_list_stores_response_tag;
    send_response(ctx);
}

static void handle_delete_store(server_context_t *ctx, eyeq_DeleteStore *request) {
    int res = remove_store(request->name, request->path, ctx->stores);
    if (res != STORE_OK) {
        sprintf(ctx->response.error, "Error while removing store");
        send_response(ctx);
        return;
    }

    ctx->response.which_resp = eyeq_ServerResponse_delete_store_response_tag;
    send_response(ctx);
}

static void handle_write_block(server_context_t *ctx, eyeq_WriteBlock *request) {
    store_t *store = find_store(request->name, request->path, ctx->stores);
    if (!store) {
        sprintf(ctx->response.error, "Store does not exist");
        send_response(ctx);
        return;
    }

    block_t block;
    memcpy(&block, request->block.data.bytes, request->block.data.size);
    block.hdr.block_length = request->block.data.size;

    int res = store_write_block(store, &block, request->offset);
    if (res != STORE_OK) {
        sprintf(ctx->response.error, "Error while writing block.");
        send_response(ctx);
        return;
    }

    ctx->response.which_resp = eyeq_ServerResponse_write_block_response_tag;
    ctx->response.resp.write_block_response.offset = block.hdr.block_id;
    send_response(ctx);
}

static void handle_read_blocks(server_context_t *ctx, eyeq_ReadBlocks *request) {
    store_t *store = find_store(request->name, request->path, ctx->stores);
    if (!store) {
        sprintf(ctx->response.error, "Store does not exist");
        send_response(ctx);
        return;
    }

    if (!request->count) {
        sprintf(ctx->response.error, "read_blocks count should be > 0");
        send_response(ctx);
        return;
    }

    for (int i = 0; i < request->count; i++) {
        block_t block;
        int res = store_read_block(store, &block, request->offset+i);
        if (res != STORE_OK) {
            sprintf(ctx->response.error, "Error while writing block.");
            return;
        }

        ctx->response.which_resp = eyeq_ServerResponse_read_blocks_response_tag;

        eyeq_ReadBlocks_Response *resp = &ctx->response.resp.read_blocks_response;
        memcpy(resp->block.data.bytes, &block, block.hdr.block_length);
        resp->block.data.size = block.hdr.block_length;
        send_response(ctx);
    }
}

static void handle_flush_stores(server_context_t *ctx, eyeq_FlushStores *requests) {
    save_store_list();
}

static void handle_create_stream(server_context_t *ctx, eyeq_CreateStream *request) {
    eyeq_Stream *s = &request->stream;
    if (find_stream(s->name, s->path, ctx->streams)) {
        sprintf(ctx->response.error, "Stream already exists");
        send_response(ctx);
        return;
    }

    if (!request->layers_count) {
        sprintf(ctx->response.error, "Cannot create stream without any layers.");
        send_response(ctx);
        return;
    }

    stream_t *stream = NULL;

    for (int i = 0; i < request->layers_count; i++) {
        // printf("Creating layer %d!\n", i);
        eyeq_StreamLayer *layer = &request->layers[i];
        // printf("Layer type: %d\n", layer->which_layer);
        switch (layer->which_layer) {
        case eyeq_StreamLayer_store_reader_tag:
            if (stream) {
                free_stream(stream);
                sprintf(ctx->response.error, "Store reader should be the first layer.");
                send_response(ctx);
                return;                
            }

            eyeq_StoreReaderStream *srs = &layer->layer.store_reader;

            store_t *store = find_store(srs->name, srs->path, ctx->stores);
            if (!store) {
                sprintf(ctx->response.error, "Could not find store '%s'", srs->name);
                send_response(ctx);
                return;
            }

            stream = new_store_reader_stream(store, srs->start_block, srs->end_block);
            break;
        case eyeq_StreamLayer_frequency_translate_tag: {
            eyeq_FrequencyTranslateStream *fts = &layer->layer.frequency_translate;

            stream_t *sine = new_complex_sine_stream(fts->phase, fts->relative_frequency, 1.0);
            stream = new_complex_multiply_stream(stream, sine);

            break;
        }
        case eyeq_StreamLayer_const_multiply_tag:
            // TODO
            break;
        case eyeq_StreamLayer_fir_filter_tag: {
            eyeq_FirFilterStream *ffs = &layer->layer.fir_filter;

            stream = new_fir_stream(stream, ffs->filter_taps, ffs->filter_taps_count, ffs->is_complex);

            break;
        }
        case eyeq_StreamLayer_abs_stream_tag:
            // TODO
            break;
        case eyeq_StreamLayer_log_stream_tag:
            // TODO
            break;
        // TODO: Welch power spectrum?
        // TODO: FFT
        default:
            if (stream) {
                free_stream(stream);
                sprintf(ctx->response.error, "Unknown layer type: %d", layer->which_layer);
                send_response(ctx);
                return;
            }
        }
    }

    if (!stream) {
        sprintf(ctx->response.error, "Could not create stream");
        send_response(ctx);
        return;
    }

    stream->seek(stream, 0);

    int res = add_stream(s->name, s->path, ctx->streams, stream);
    if (res != STORE_OK) {
        free_stream(stream);
        sprintf(ctx->response.error, "Could not add stream to stream list: %d", res);
        send_response(ctx);
        return;
    }

    ctx->response.which_resp = eyeq_ServerResponse_create_stream_response_tag;
    memcpy(&ctx->response.resp.create_stream_response.stream, request, sizeof(eyeq_Stream));
    send_response(ctx);
}

static void handle_read_stream(server_context_t *ctx, eyeq_ReadStream *request) {
    stream_t *stream = find_stream(request->name, request->path, ctx->streams);
    if (!stream) {
        sprintf(ctx->response.error, "Stream does not exist");
        send_response(ctx);
        return;
    }

    int cnt = request->sample_count;
    while (cnt > 0) {
        eyeq_ReadStream_Response *resp = &ctx->response.resp.read_stream_response;
        int to_read = min(cnt, 4096);
        int r = stream->read(stream, resp->samples, to_read);
        if (stream->eos) {
            resp->eos = true;
        }

        if (r >= 0) {
            resp->samples_count = r;
            cnt -= r;
        } else {
            resp->samples_count = 0;
        }

        ctx->response.which_resp = eyeq_ServerResponse_read_stream_response_tag;
        send_response(ctx);

        if (stream->eos || r < 0) {
            break;
        }
    }
}

static void handle_seek_stream(server_context_t *ctx, eyeq_SeekStream *request) {
    stream_t *stream = find_stream(request->name, request->path, ctx->streams);
    if (!stream) {
        sprintf(ctx->response.error, "Stream does not exist");
        send_response(ctx);
        return;
    }

    if (stream->seek) {
        stream->seek(stream, request->block_id);
    }

    ctx->response.which_resp = eyeq_ServerResponse_seek_stream_response_tag;
    send_response(ctx);
}

static void handle_close_stream(server_context_t *ctx, eyeq_CloseStream *request) {
    int res = remove_stream(request->name, request->path, ctx->streams);
    if (res != STREAM_OK) {
        sprintf(ctx->response.error, "Error while removing stream");
        send_response(ctx);
        return;
    }

    ctx->response.which_resp = eyeq_ServerResponse_close_stream_response_tag;
    send_response(ctx);
}

static void handle_stream_info(server_context_t *ctx, eyeq_StreamInfo *request) {
    stream_t *stream = find_stream(request->name, request->path, ctx->streams);
    if (!stream) {
        sprintf(ctx->response.error, "Stream does not exist");
        send_response(ctx);
        return;
    }

    sprintf(ctx->response.error, "Not implemented yet.");
    send_response(ctx);
}

bool stream_list_iterator(void *context, const char *name, const char *path, stream_t *store) {
    eyeq_ServerResponse *sr = ((eyeq_ServerResponse *)context);
    eyeq_Stream *s = &sr->resp.list_streams_response.streams[sr->resp.list_streams_response.streams_count++];
    sprintf(s->name, name, STORE_MAX_NAME);
    sprintf(s->path, path, STORE_MAX_PATH);

    return sr->resp.list_streams_response.streams_count >= 64;
}

static void handle_list_streams(server_context_t *ctx, eyeq_ListStreams *request) {
    iterate_stream_list(request->path, ctx->streams, stream_list_iterator, &ctx->response);
    ctx->response.which_resp = eyeq_ServerResponse_list_streams_response_tag;
    send_response(ctx);
}



void eyeq_server(const char *endpoint, store_list_t *stores, stream_list_t *streams) {
    void *context = zmq_ctx_new();
    void *responder = zmq_socket(context, ZMQ_ROUTER);
    int rc = zmq_bind(responder, endpoint);
    assert(rc == 0);

    server_context_t ctx = {
        .responder = responder,
        .stores = stores,
        .streams = streams,
    };

    uint8_t input_buffer[eyeq_ServerRequest_size];
    eyeq_ServerRequest request;

    while (1) {
        ctx.ident_length = zmq_recv(ctx.responder, ctx.ident, 100, 0);
        if (ctx.ident_length == -1) {
            fprintf(stderr, "Error while getting identity: %s\n", strerror(errno));
            break;
        }

        int value;
        size_t option_len = 4;
        if (zmq_getsockopt(ctx.responder, ZMQ_RCVMORE, &value, &option_len)) {
            fprintf(stderr, "Error while running zmq_getsockopt: %s\n", strerror(errno));
            break;
        }

        if (!value) {
            continue;
        }

        // Receive empty frame
        int nbytes = zmq_recv(ctx.responder, input_buffer, eyeq_ServerRequest_size, 0);
        if (nbytes) {
            continue;
        }

        if (zmq_getsockopt(ctx.responder, ZMQ_RCVMORE, &value, &option_len)) {
            fprintf(stderr, "Error while running zmq_getsockopt: %s\n", strerror(errno));
            break;
        }
        if (!value) {
            continue;
        }

        // Receive contents
        nbytes = zmq_recv(ctx.responder, input_buffer, eyeq_ServerRequest_size, 0);
        if (nbytes == -1) {
            break;
        }

        bzero(&ctx.response, sizeof(eyeq_ServerResponse));

        pb_istream_t stream = pb_istream_from_buffer(input_buffer, nbytes);
        if (pb_decode(&stream, eyeq_ServerRequest_fields, &request)) {
            ctx.req_id = request.req_id;
            // printf("Handling request: %d\n", request.which_req);
            // Handle incoming message
            switch (request.which_req) {
            case eyeq_ServerRequest_create_store_tag:
                handle_create_store(&ctx, &request.req.create_store.store);
                break;
            case eyeq_ServerRequest_list_stores_tag:
                handle_list_stores(&ctx, &request.req.list_stores);
                break;
            case eyeq_ServerRequest_delete_store_tag:
                handle_delete_store(&ctx, &request.req.delete_store);
                break;
            case eyeq_ServerRequest_write_block_tag:
                handle_write_block(&ctx, &request.req.write_block);
                break;
            case eyeq_ServerRequest_read_blocks_tag:
                handle_read_blocks(&ctx, &request.req.read_blocks);
                break;
            case eyeq_ServerRequest_flush_stores_tag:
                handle_flush_stores(&ctx, &request.req.flush_stores);
                break;
            case eyeq_ServerRequest_create_stream_tag:
                handle_create_stream(&ctx, &request.req.create_stream);
                break;
            case eyeq_ServerRequest_read_stream_tag:
                handle_read_stream(&ctx, &request.req.read_stream);
                break;
            case eyeq_ServerRequest_seek_stream_tag:
                handle_seek_stream(&ctx, &request.req.seek_stream);
                break;
            case eyeq_ServerRequest_close_stream_tag:
                handle_close_stream(&ctx, &request.req.close_stream);
                break;
            case eyeq_ServerRequest_stream_info_tag:
                handle_stream_info(&ctx, &request.req.stream_info);
                break;
            case eyeq_ServerRequest_list_streams_tag:
                handle_list_streams(&ctx, &request.req.list_streams);
                break;
            }
        } else {
            // Set error message
            sprintf(ctx.response.error, "Could not decode packet of length %d", nbytes);
            send_response(&ctx);
        }
    }

    zmq_close(responder);
    zmq_ctx_destroy(context);
}

__attribute__((weak)) void save_store_list(void);
