#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <slash/slash.h>
#include <eyeq/client.h>

static eyeq_client_t *client;

static int list_stores(struct slash *slash)
{
    char *path = "";
    if (slash->argc > 1) {
        path = slash->argv[1];
    }

	bool list_stores_cb(eyeq_Store *store) {
		printf("* %s/%s (type: %d, offset: %"PRIu32", count: %"PRIu32")\n", store->path, store->name, store->store_type, store->block_offset, store->block_count);
		return true;
	}

	printf("Listing stores:\n");
	return eyeq_list_stores(client, path, list_stores_cb, NULL);
}
slash_command(list_stores, list_stores, "[path]", "List stores");

static int list_streams(struct slash *slash)
{
    char *path = "";
    if (slash->argc > 1) {
        path = slash->argv[1];
    }

	bool list_streams_cb(eyeq_Stream *stream) {
		printf("* %s/%s\n", stream->path, stream->name);
		return true;
	}

	printf("Listing streams:\n");
	return eyeq_list_streams(client, path, list_streams_cb, NULL);
}
slash_command(list_streams, list_streams, "[path]", "List streams");


static int export_blocks(struct slash *slash)
{
    char *path = "";
    char *name, *filename;

    if (slash->argc < 5) {
    	return SLASH_EUSAGE;
    }

    name = slash->argv[1];
    uint32_t offset = strtoul(slash->argv[2], NULL, 10);
    uint32_t count = strtoul(slash->argv[3], NULL, 10);
    filename = slash->argv[4];

    if (slash->argc > 5) {
	    path = slash->argv[5];
    }

    bool read_block_cb(eyeq_Block *block, void *context) {
    	FILE *f = (FILE *)context;
    	return fwrite(block->data.bytes, BLOCK_LENGTH, 1, f) == 1;
    }

    FILE *f;
    if (!strcmp("-", filename)) {
    	f = stdout;
    } else {
    	f = fopen(filename, "wb");
    	if (!f) {
    		fprintf(stderr, "Could not open %s for writing\n", filename);
    		return SLASH_EINVAL;
    	}
    }

	fprintf(stderr, "Exporting blocks from '%s/%s' to '%s', offset: %"PRIu32", count: %"PRIu32"\n", path, name, filename, offset, count);

    while (count > 0) {
        int to_transfer = count > 100 ? 100 : count;
        int res = eyeq_read_blocks(client, name, path, offset, to_transfer, read_block_cb, client->timeout_ms, f);
        if (res) {
            return res;
        }
        offset += to_transfer;
        count -= to_transfer;
    }
	fclose(f);

	return 0;
}
slash_command(export_blocks, export_blocks, "<name> <start> <count> <filename> [path]", "Export blocks");

static int import_blocks(struct slash *slash)
{
    char *path = "";
    char *name, *filename;

    if (slash->argc < 2) {
    	return SLASH_EUSAGE;
    }

    name = slash->argv[1];
    filename = slash->argv[2];

    if (slash->argc > 3) {
	    path = slash->argv[3];
    }

    FILE *f;
    if (!strcmp("-", filename)) {
    	f = stdout;
    } else {
    	f = fopen(filename, "rb");
    	if (!f) {
    		fprintf(stderr, "Could not open %s for reading\n", filename);
    		return SLASH_EINVAL;
    	}
    }

    int r = 0;
    eyeq_Block block;
	// block_t *bl = (block_t *)block.data.bytes;
    while ((r = fread(&block.data.bytes, BLOCK_LENGTH, 1, f)) > 0) {
    	block.data.size = BLOCK_LENGTH;
		int res = eyeq_write_block(client, name, path, &block, -1, NULL);
		if (res) {
			fclose(f);
			return res;
		}
    }

	fclose(f);

	return SLASH_SUCCESS;
}
slash_command(import_blocks, import_blocks, "<name> <filename> [path]", "Import blocks");


static int read_stream(struct slash *slash)
{
    char *path = "";
    char *name, *filename;

    if (slash->argc < 4) {
    	return SLASH_EUSAGE;
    }

    name = slash->argv[1];
    uint32_t count = strtoul(slash->argv[2], NULL, 10);
    filename = slash->argv[3];

    if (slash->argc > 4) {
	    path = slash->argv[4];
    }

    float *data = calloc(count, sizeof(float));
    if (!data) {
		fprintf(stderr, "Failed to allocate buffer for output data\n");
		return SLASH_EINVAL;
    }

    FILE *f;
    if (!strcmp("-", filename)) {
    	f = stdout;
    	filename = "<stdout>";
    } else {
    	f = fopen(filename, "wb");
    	if (!f) {
    		fprintf(stderr, "Could not open '%s' for writing\n", filename);
    		free(data);
    		return SLASH_EINVAL;
    	}
    }

	fprintf(stderr, "Exporting samples from stream '%s/%s' to '%s', count: %"PRIu32"\n", path, name, filename, count);

	uint32_t written = 0;

	int res = eyeq_read_stream(client, name, path, data, count, client->timeout_ms, &written, NULL);

	if (res == EYEQ_REQUEST_ABORTED) {
		fprintf(stderr, "Transfer was aborted\n");
	}

	if (written > 0) {
		if (fwrite(data, sizeof(float), written, f) != written) {
			fprintf(stderr, "Failed to write all output data\n");
		} else {
			fprintf(stderr, "Wrote %"PRIu32" samples to '%s'\n", written, filename);
		}
	}

	fclose(f);
	free(data);

	return res;
}
slash_command(read_stream, read_stream, "<name> <sample_count> <filename> [path]", "Export samples");


static int seek_stream(struct slash *slash)
{
    char *path = "";
    char *name;

    if (slash->argc < 3) {
    	return SLASH_EUSAGE;
    }

    name = slash->argv[1];
    uint32_t block_id = strtoul(slash->argv[2], NULL, 10);

    if (slash->argc > 3) {
	    path = slash->argv[3];
    }

	return eyeq_seek_stream(client, name, path, block_id);
}
slash_command(seek_stream, seek_stream, "<name> <block_id> [path]", "Seek in stream");


static int create_store(struct slash *slash)
{
    char *path, *name;
    char *filename = "";
    int store_type = 0;

    if (slash->argc < 4) {
    	return SLASH_EUSAGE;
    }

    name = slash->argv[1];
    path = slash->argv[2];
    uint32_t count = strtoul(slash->argv[3], NULL, 10);

    if (slash->argc > 4) {
	    store_type = strtoul(slash->argv[4], NULL, 10);
    }

    if (slash->argc > 5) {
	    filename = slash->argv[5];
    }

    eyeq_Store store = {
	    .store_type = store_type,
	    .block_count = count,
	    .block_offset = 0,
    };

    strncpy(store.name, name, MAX_NAME-1);
    strncpy(store.path, path, MAX_PATH-1);
    strncpy(store.file_path, filename, MAX_PATH-1);
    store.name[MAX_NAME-1] = 0;
	store.path[MAX_PATH-1] = 0;
	store.file_path[MAX_PATH-1] = 0;

	return eyeq_create_store(client, &store);
}
slash_command(create_store, create_store, "<name> <path> <count> [store_type] [filename]", "Create store");


#include "filters.h"

static int create_stream(struct slash *slash)
{
    if (slash->argc < 7) {
    	return SLASH_EUSAGE;
    }

    char *name = slash->argv[1];
    char *path = slash->argv[2];
    char *store_name = slash->argv[3];
    char *store_path = slash->argv[4];
    uint32_t start_block = strtoul(slash->argv[5], NULL, 10);
    uint32_t block_count = strtoul(slash->argv[6], NULL, 10);

    // filter_type:
    //   0: No filter
    //   1: lowpass100
    //   2: lowpass50
    //   3: lowpass25
    int filter_type = 0;

    float relative_freq = 0;

    if (slash->argc > 7) {
    	relative_freq = atof(slash->argv[7]);
    }

    if (slash->argc > 8) {
	    filter_type = strtoul(slash->argv[8], NULL, 10);
    }

    const float *filter = NULL;
    int ntaps = 0;

    switch (filter_type) {
   	case 1:
   		ntaps = LP_FILTER_TAPS;
   		filter = g_lowpass_filters[LP_FILTER_100];
   		break;
   	case 2:
   		ntaps = LP_FILTER_TAPS;
   		filter = g_lowpass_filters[LP_FILTER_50];
   		break;
   	case 3:
   		ntaps = LP_FILTER_TAPS;
   		filter = g_lowpass_filters[LP_FILTER_25];
   		break;
   	default:
   		break;
    }

	return eyeq_create_frequency_filter_stream(client, name, path, store_name, store_path, start_block, block_count, relative_freq, filter, ntaps);
}
slash_command(create_stream, create_stream, "<name> <path> <store_name> <store_path> <start_block> <block_count> [relative_freq] [filter_type]", "Create filtered stream");

static int delete_store(struct slash *slash)
{
    if (slash->argc < 2) {
    	return SLASH_EUSAGE;
    }

    char *name = slash->argv[1];
    char *path = "";

    if (slash->argc > 2) {
    	path = slash->argv[2];
    }

	return eyeq_delete_store(client, name, path);
}
slash_command(delete_store, delete_store, "<name> [path]", "Delete store");


static int close_stream(struct slash *slash)
{
    if (slash->argc < 2) {
    	return SLASH_EUSAGE;
    }

    char *name = slash->argv[1];
    char *path = "";

    if (slash->argc > 2) {
    	path = slash->argv[2];
    }

	return eyeq_close_stream(client, name, path);
}
slash_command(close_stream, close_stream, "<name> [path]", "Close stream");

void init_eyeq_slash_client(eyeq_client_t *eyeq_client) {
    client = eyeq_client;
}

#define BLOCK_TYPE_I8_SAMPLES  1
#define BLOCK_TYPE_I16_SAMPLES 2
#define BLOCK_TYPE_I32_SAMPLES 3
#define BLOCK_TYPE_F32_SAMPLES 4
#define BLOCK_TYPE_F64_SAMPLES 5

static int import_samples(struct slash *slash)
{
    char *path = "";
    char *name, *filename;

    if (slash->argc < 5) {
        return SLASH_EUSAGE;
    }

    name = slash->argv[1];
    filename = slash->argv[2];

    int input_type;
    int output_type;
    int input_item_size;
    int output_item_size;
    int block_item_count;
    double scale = 1.0;

    if (!strcmp("float",  slash->argv[3])) {
        input_type = BLOCK_TYPE_F32_SAMPLES;
        input_item_size = 4;
    } else if (!strcmp("int16",  slash->argv[3])) {
        input_type = BLOCK_TYPE_I16_SAMPLES;
        input_item_size = 2;
    } else if (!strcmp("int8",  slash->argv[3])) {
        input_type = BLOCK_TYPE_I8_SAMPLES;
        input_item_size = 1;
    } else {
        fprintf(stderr, "Unknown input type '%s'\n",  slash->argv[3]);
        return SLASH_EINVAL;
    }

    if (!strcmp("float",  slash->argv[4])) {
        output_type = BLOCK_TYPE_F32_SAMPLES;
        block_item_count = BLOCK_F32_SAMPLES;
        output_item_size = 4;
    } else if (!strcmp("int16",  slash->argv[4])) {
        output_type = BLOCK_TYPE_I16_SAMPLES;
        block_item_count = BLOCK_I16_SAMPLES;
        output_item_size = 2;
    } else if (!strcmp("int8",  slash->argv[4])) {
        output_type = BLOCK_TYPE_I8_SAMPLES;
        block_item_count = BLOCK_I8_SAMPLES;
        output_item_size = 1;
    } else {
        fprintf(stderr, "Unknown output type '%s'\n",  slash->argv[4]);
        return SLASH_EINVAL;
    }

    if (slash->argc > 5) {
        scale = atof(slash->argv[5]);
    }

    if (slash->argc > 6) {
        path = slash->argv[6];
    }

    FILE *f;
    if (!strcmp("-", filename)) {
        f = stdout;
    } else {
        f = fopen(filename, "rb");
        if (!f) {
            fprintf(stderr, "Could not open %s for reading\n", filename);
            return SLASH_EINVAL;
        }
    }

    uint8_t *input_buffer = calloc(block_item_count, input_item_size);

    int r = 0;
    eyeq_Block block;

    block_t *bl = (block_t *)block.data.bytes;
    while ((r = fread(input_buffer, input_item_size, block_item_count, f)) > 0) {
        for (int i = 0; i < r; i++) {
            double item = 0;
            switch (input_type) {
            case BLOCK_TYPE_F32_SAMPLES:
                item = ((float *)input_buffer)[i];
                break;
            case BLOCK_TYPE_I16_SAMPLES:
                item = ((int16_t *)input_buffer)[i];
                break;
            case BLOCK_TYPE_I8_SAMPLES:
                item = ((int8_t *)input_buffer)[i];
                break;
            }

            switch (output_type) {
            case BLOCK_TYPE_F32_SAMPLES:
                bl->data.f32_samples[i] = item * scale;
                break;
            case BLOCK_TYPE_I16_SAMPLES:
                bl->data.i16_samples[i] = item * scale;
                break;
            case BLOCK_TYPE_I8_SAMPLES:
                bl->data.i8_samples[i] = item * scale;
                break;
            }
        }
        bl->hdr.block_type = output_type;
        bl->hdr.block_length = BLOCK_HEADER_LENGTH + block_item_count * output_item_size;
        block.data.size = bl->hdr.block_length;

        int res = eyeq_write_block(client, name, path, &block, -1, NULL);
        if (res) {
            free(input_buffer);
            fclose(f);
            return res;
        }
    }

    free(input_buffer);
    fclose(f);

    return SLASH_SUCCESS;
}
slash_command(import_samples, import_samples, "<name> <filename> <input_type> <destination_type> [scale] [path]", "Import blocks");
