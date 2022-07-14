#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <eyeq/server.h>
#include <eyeq/block.h>

// Error codes
#define STORE_OK 0
#define STORE_UNKNOWN_STORE_TYPE 1
#define STORE_SEEK_ERROR 2
#define STORE_READ_ERROR 3
#define STORE_WRITE_ERROR 4
#define STORE_ALREADY_EXISTS 5
#define STORE_OUT_OF_MEMORY 6
#define STORE_NOT_FOUND 7
#define STORE_FILE_NOT_FOUND 8
#define STORE_COULD_NOT_WRITE_LIST 9
#define STORE_STILL_IN_USE 10

#define MEMORY_STORE 0
#define FILE_STORE   1

typedef struct store_s {
    uint32_t block_count;
    uint32_t write_offset;
    int store_type;

    void *internal;

    int ref_count;
} store_t;

store_t* new_memory_store(uint32_t number_of_blocks);
store_t* new_file_store(const char *filepath, uint32_t number_of_blocks, bool initialize);

int store_read_block(store_t *store, block_t *output, uint32_t block_offset);
int store_write_block(store_t *store, block_t *input, int32_t block_offset);

void free_store(store_t *store);

typedef bool (*iterate_stores_callback)(void *context, const char *name, const char *path, store_t *store);

int add_store(const char *name, const char *path, store_list_t *list, store_t *store);
int remove_store(const char *name, const char *path, store_list_t *list);
store_t* find_store(const char *name, const char *path, store_list_t *list);
void iterate_store_list(const char *path, store_list_t *list, iterate_stores_callback cb, void *context);
void free_store_list(store_list_t *list);

void store_use(store_t *store);
void store_release(store_t *store);
