#pragma once

#include <stdint.h>
#include <eyeq/server/store.h>
#include <eyeq/samples.pb.h>

#define MAX_NAME 32
#define MAX_PATH 128

#define EYEQ_OK 0
#define EYEQ_ENCODING_ERROR 1
#define EYEQ_INVALID_RESPONSE 2
#define EYEQ_NETWORK_TIMEOUT 3
#define EYEQ_NETWORK_ERROR 4
#define EYEQ_REQUEST_ABORTED 5
#define EYEQ_ERROR 6
#define EYEQ_NOT_FOUND 7

typedef struct {
    // ZMQ socket
    void *context;
    void *socket;

    // Current request id (used to distinguish whether
    // multiple responses belong to the same request)
    int req_id;

    long timeout_ms;
} eyeq_client_t;

void *eyeq_create_context();
void eyeq_destroy_context(void *eyeq_context);

eyeq_client_t *eyeq_connect(const char *endpoint, void *eyeq_context, long timeout_ms);
void eyeq_close(eyeq_client_t *client);

int eyeq_create_store(eyeq_client_t *client, eyeq_Store *store);
int eyeq_list_stores(eyeq_client_t *client, const char *path, bool (*list_stores_callback)(eyeq_Store *store), void *context);
int eyeq_delete_store(eyeq_client_t *client, const char *name, const char *path);
int eyeq_write_block(eyeq_client_t *client, const char *name, const char *path, eyeq_Block *block, int32_t offset, uint32_t *written_offset);

int eyeq_read_blocks(
    eyeq_client_t *client,
    const char *name,
    const char *path,
    uint32_t offset,
    uint32_t count,
    bool (*read_block_callback)(eyeq_Block *block, void *context),
    long timeout_ms,
    void *context);

int eyeq_flush_stores(eyeq_client_t *client);
int eyeq_find_store(eyeq_client_t *client, const char *name, const char *path, eyeq_Store *response);

int eyeq_close_stream(eyeq_client_t *client, const char *name, const char *path);
int eyeq_list_streams(eyeq_client_t *client, const char *path, bool (*list_streams_callback)(eyeq_Stream *stream), void *context);
int eyeq_seek_stream(eyeq_client_t *client, const char *name, const char *path, uint32_t block_id);
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
    int ntaps);

int eyeq_read_stream(
    eyeq_client_t *client,
    const char *name,
    const char *path,
    float *output,
    uint32_t count,
    long timeout_ms,
    uint32_t *written,
    uint32_t *block);

void init_eyeq_slash_client(eyeq_client_t *eyeq_client);
