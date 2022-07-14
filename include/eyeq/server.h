#pragma once

#include <sys/queue.h>

struct store_s;
struct stream_base;

#define STORE_MAX_NAME 31
#define STORE_MAX_PATH 127

typedef struct store_dir_entry_s {
    char name[STORE_MAX_NAME+1];
    char path[STORE_MAX_PATH+1];

    SLIST_ENTRY(store_dir_entry_s) next;

    struct store_s *store;
} store_list_entry_t;

typedef SLIST_HEAD(store_list_head_s, store_dir_entry_s) store_list_t;

typedef struct stream_dir_entry_s {
    char name[STORE_MAX_NAME+1];
    char path[STORE_MAX_PATH+1];

    SLIST_ENTRY(stream_dir_entry_s) next;

    struct stream_base *stream;
} stream_list_entry_t;

typedef SLIST_HEAD(stream_list_head_s, stream_dir_entry_s) stream_list_t;

int load_store_list_from_file(const char *filepath, store_list_t *list);
int save_store_list_to_file(const char *filepath, store_list_t *list);

void eyeq_server(const char *endpoint, store_list_t *stores, stream_list_t *streams);

void save_store_list(void);
