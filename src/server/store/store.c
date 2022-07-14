#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <inttypes.h>

#include <eyeq/shared.h>
#include <eyeq/server/store.h>

typedef struct {
    FILE *file;
    char filepath[STORE_MAX_PATH];
} file_store_t;

store_t* new_memory_store(uint32_t number_of_blocks) {
    store_t *store = calloc(1, sizeof(store_t));
    if (store == NULL) {
        return NULL;
    }

    store->block_count = number_of_blocks;
    store->store_type = MEMORY_STORE;

    store->internal = calloc(number_of_blocks, sizeof(block_t));
    if (!store->internal) {
        free(store);
        return NULL;
    }

    return store;
}

/*
    struct stat sb;
    int ret = stat(filepath, &sb);
    bool file_exists = !ret;
    if (file_exists && (sb.st_mode & S_IFMT) != S_IFBLK && (sb.st_mode & S_IFMT) != S_IFREG)) {
        // File is neither a regular file or a block device.

        return NULL;
    }

 */
// TODO: Metadata block?
store_t* new_file_store(const char *filepath, uint32_t number_of_blocks, bool initialize) {
    // If it exists, open it for reading and writing, otherwise create a new file.
    FILE *file = fopen(filepath, initialize ? "w+" : "r+");
    if (!file) {
        perror("Error opening store output file");
        return NULL;
    }

    store_t *store = calloc(1, sizeof(store_t));
    if (store == NULL) {
        fprintf(stderr, "Error while allocating memory for store.\n");
        fclose(file);
        return NULL;
    }

    store->block_count = number_of_blocks;
    store->store_type = FILE_STORE;

    file_store_t *fs = calloc(1, sizeof(file_store_t));
    if (!fs) {
        fprintf(stderr, "Error while allocating memory for file_store.\n");
        fclose(file);
        free(store);
        return NULL;
    }

    fs->file = file;
    strncpy(fs->filepath, filepath, STORE_MAX_PATH);
    store->internal = fs;

    return store;
}

static int memory_store_read_block(store_t *store, block_t *output, uint32_t offset) {
    block_t *ptr = (block_t *)store->internal;
    memcpy(output, &ptr[offset], sizeof(block_t));
    return STORE_OK;
}

static int file_store_read_block(store_t *store, block_t *output, uint32_t offset) {
    file_store_t *fs = (file_store_t *)store->internal;
    int ret = fseek(fs->file, (size_t)offset * sizeof(block_t), SEEK_SET);
    if (ret != 0) {
        return STORE_SEEK_ERROR;
    }

    return fread(output, sizeof(block_t), 1, fs->file) == 1 ? STORE_OK : STORE_READ_ERROR;
}

int store_read_block(store_t *store, block_t *output, uint32_t block_offset) {
    block_offset %= store->block_count;

    if (store->store_type == MEMORY_STORE) {
        return memory_store_read_block(store, output, block_offset);
    } else if (store->store_type == FILE_STORE) {
        return file_store_read_block(store, output, block_offset);
    }

    return STORE_UNKNOWN_STORE_TYPE;
}

static void memory_store_write_block(store_t *store, block_t *output, uint32_t offset) {
    offset %= store->block_count;

    block_t *ptr = (block_t *)store->internal;
    memcpy(&ptr[offset], output, sizeof(block_t));
}

static int file_store_write_block(store_t *store, block_t *output, uint32_t offset) {
    offset %= store->block_count;

    file_store_t *fs = (file_store_t *)store->internal;
    int ret = fseek(fs->file, (size_t)offset * sizeof(block_t), SEEK_SET);
    if (ret != 0) {
        return STORE_SEEK_ERROR;
    }

    return fwrite(output, sizeof(block_t), 1, fs->file) == 1 ? STORE_OK : STORE_WRITE_ERROR;
}

int store_write_block(store_t *store, block_t *block, int32_t block_offset) {
    bool appending = false;

    if (block_offset < 0) {
        block_offset = store->write_offset;
        appending = true;
    }

    // Update the block
    block->hdr.block_id = (uint32_t)block_offset;
    block->hdr.block_magic = BLOCK_MAGIC;

    block->hdr.crc32 = 0;
    block->hdr.crc32 = eyeq_crc32(EYEQ_CRC_INITIAL, (uint8_t *)block, block->hdr.block_length) ^ EYEQ_CRC_INITIAL;

    int ret = 0;

    if (store->store_type == MEMORY_STORE) {
        memory_store_write_block(store, block, block_offset);
    } else if (store->store_type == FILE_STORE) {
        ret = file_store_write_block(store, block, block_offset);
    }

    // If appending, increment write offset (wrapping around if necessary).
    if (!ret && appending) {
        store->write_offset++;
    }

    return ret;
}

void free_store(store_t *store) {
    if (!store) {
        return;
    }

    // Check which type it is ...
    if (store->store_type == MEMORY_STORE) {
        free(store->internal);
    } else if (store->store_type == FILE_STORE) {
        file_store_t *fs = (file_store_t *)store->internal;
        fclose(fs->file);
        free(store->internal);
    }

    free(store);
}

int add_store(const char *name, const char *path, store_list_t *list, store_t *store) {
    if (find_store(name, path, list)) {
        return STORE_ALREADY_EXISTS;
    }

    struct store_dir_entry_s *entry = calloc(1, sizeof(struct store_dir_entry_s));
    if (!entry) {
        return STORE_OUT_OF_MEMORY;
    }

    strncpy(entry->name, name, STORE_MAX_NAME);
    strncpy(entry->path, path, STORE_MAX_PATH);
    entry->store = store;

    fprintf(stderr, "Creating store '%s/%s', type: %d of length %"PRIu32" (%"PRIu64" bytes)\n", path, name, store->store_type, store->block_count, (uint64_t)store->block_count * sizeof(block_t));

    SLIST_INSERT_HEAD(list, entry, next);

    return STORE_OK;
}

static struct store_dir_entry_s* find_store_entry(const char *name, const char *path, store_list_t *list) {
    struct store_dir_entry_s *entry = SLIST_FIRST(list);

    while (entry) {
        if (!strncmp(entry->name, name, STORE_MAX_NAME) && !strncmp(entry->path, path, STORE_MAX_PATH))
            return entry;
        entry = SLIST_NEXT(entry, next);
    }

    return NULL;
}

int remove_store(const char *name, const char *path, store_list_t *list) {
    struct store_dir_entry_s *entry = find_store_entry(name, path, list);
    if (!entry) {
        return STORE_NOT_FOUND;
    }

    if (entry->store->ref_count > 0) {
        return STORE_STILL_IN_USE;
    }

    SLIST_REMOVE(list, entry, store_dir_entry_s, next);

    free_store(entry->store);
    free(entry);

    return STORE_OK;
}

store_t* find_store(const char *name, const char *path, store_list_t *list) {
    struct store_dir_entry_s *entry = find_store_entry(name, path, list);
    if (!entry) {
        return NULL;
    }

    return entry->store;
}

void iterate_store_list(const char *path, store_list_t *list, iterate_stores_callback callback, void *context) {
    struct store_dir_entry_s *entry = SLIST_FIRST(list);

    while (entry) {
        // fprintf(stderr, "Iterate: %s/%s\n", entry->path, entry->name);
        if (!path[0] || !strncmp(entry->path, path, STORE_MAX_PATH)) {
            if (callback(context, entry->name, entry->path, entry->store))
                break;
        }
        entry = SLIST_NEXT(entry, next);
    }
}

/*
 * store file definition:
 *
 * "name;path;store_type;number_of_blocks;current_offset;store_path"
 */
int load_store_list_from_file(const char *filepath, store_list_t *list) {
    FILE *f = fopen(filepath, "r");
    if (!f) {
        return STORE_FILE_NOT_FOUND;
    }

    char line[256];
    int lineno = 0;

    while (fgets(line, sizeof line, f)) {
        lineno++;
        char *fields[6];
        char *p = line;

        for (int n = 0; n < 6; n++) {
            fields[n] = p;
            p += strcspn(p, ";\n");
            if (*p != '\0') {
                *p++ = '\0';
            }
        }

        char *store_name = fields[0];
        char *store_path = fields[1];
        int store_type = strtol(fields[2], NULL, 10);
        uint32_t store_blocks = strtoul(fields[3], NULL, 10);
        uint32_t store_write_offset = strtoul(fields[4], NULL, 10);
        char *store_filepath = fields[5];

//        printf("Ret: %s %s\n", store_name, store_path);

        store_t *store;
        if (store_type == MEMORY_STORE) {
            store = new_memory_store(store_blocks);
        } else if (store_type == FILE_STORE) {
            store = new_file_store(store_filepath, store_blocks, !store_write_offset);
        } else {
            fprintf(stderr, "Wrong store definition %s:%d\n", filepath, lineno);
            continue;
        }
        if (!store) {
            fprintf(stderr, "Error creating store %s:%d\n", filepath, lineno);
            continue;
        }
        store->write_offset = store_write_offset;

        if (add_store(store_name, store_path, list, store) != STORE_OK) {
            fprintf(stderr, "Error adding store %s:%d - store already exists!\n", filepath, lineno);
            free_store(store);
        }
    }

    fclose(f);

    return STORE_OK;
}

struct store_write_ctx {
    FILE *f;
};

static bool __save_callback(void *context, const char *name, const char *path, store_t *store) {
    struct store_write_ctx *ctx = (struct store_write_ctx *)context;
    if (store->store_type == MEMORY_STORE) {
        fprintf(ctx->f, "%s;%s;%d;%"PRIu32";%"PRIu32";\n", name, path, store->store_type, store->block_count, store->write_offset);
    } else if (store->store_type == FILE_STORE) {
        fprintf(ctx->f, "%s;%s;%d;%"PRIu32";%"PRIu32";%s\n", name, path, store->store_type, store->block_count,
                store->write_offset, ((file_store_t *) store->internal)->filepath);
    } else {
        return true;
    }
    return false;
}

int save_store_list_to_file(const char *filepath, store_list_t *list) {
    FILE *f = fopen(filepath, "w");
    if (!f) {
        return STORE_COULD_NOT_WRITE_LIST;
    }

    struct store_write_ctx ctx = { .f = f };
    iterate_store_list("", list, __save_callback, &ctx);

    fclose(f);

    return STORE_OK;
}

void free_store_list(store_list_t *list) {
    struct store_dir_entry_s *entry;
    while ((entry = SLIST_FIRST(list)) != NULL) {
        SLIST_REMOVE(list, entry, store_dir_entry_s, next);
        fprintf(stderr, "Removing entry: %s/%s\n", entry->path, entry->name);
        free_store(entry->store);
        free(entry);
    }
}

void store_use(store_t *store) {
    if (!store) {
        return;
    }

    store->ref_count++;
}

void store_release(store_t *store) {
    if (!store) {
        return;
    }

    int ref_count = store->ref_count;
    if (ref_count > 0) {
        ref_count--;
    }
    store->ref_count = ref_count;
}
