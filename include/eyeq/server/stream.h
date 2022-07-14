#pragma once

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <eyeq/server.h>
#include <sys/types.h>

#define STREAM_OK 0
#define STREAM_SEEK_ERROR 1
#define STREAM_READ_ERROR 2
#define STREAM_ALREADY_EXISTS 3
#define STREAM_OUT_OF_MEMORY 4
#define STREAM_NOT_FOUND 5

struct stream_base {
    int (*read)(struct stream_base *stream, float *output, int count);
    void (*seek)(struct stream_base *stream, uint32_t offset);
    void (*cleanup)(struct stream_base *stream);

    int64_t offset;

    // end of stream
    bool eos;
};

typedef struct stream_base stream_t;

typedef bool (*iterate_streams_callback)(void *context, const char *name, const char *path, stream_t *stream);

int add_stream(const char *name, const char *path, stream_list_t *list, stream_t *stream);
int remove_stream(const char *name, const char *path, stream_list_t *list);
stream_t* find_stream(const char *name, const char *path, stream_list_t *list);
void iterate_stream_list(const char *path, stream_list_t *list, iterate_streams_callback cb, void *context);
void free_stream_list(stream_list_t *list);

void read_samples_from_stream(stream_t *stream, float *output, int count);
void free_stream(stream_t *stream);
