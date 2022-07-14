#include <stdint.h>
#include <malloc.h>
#include <math.h>
#include <string.h>
#include <stdbool.h>
#include <eyeq/server.h>
#include <eyeq/server/store.h>
#include <eyeq/server/stream.h>
#include "../util.h"

#include <complex.h>
typedef float complex complex_t;

#define BUFFER_SIZE 2048
#define READER_BUFFER_SIZE 32768

void read_samples_from_stream(stream_t *stream, float *output, int count) {
    int read = 0;

    if (stream->eos) {
        bzero(output, count * sizeof(float));
        return;
    }

    while (count > 0) {
        int r = stream->read(stream, output, count);
        if (r <= 0) {
            return;
        }
        count -= r;
        read += r;
    }
}

struct store_reader_stream {
    struct stream_base base;

    uint32_t start_block;
    uint32_t end_block;

    uint32_t current_block;

    int buffer_offset;
    int buffer_count;

    float buffer[READER_BUFFER_SIZE];

    store_t *store;
};


struct sine_stream {
    struct stream_base base;

    float phase;
    float frequency;
    float scale;
};

struct stream_combiner {
    struct stream_base base;

    stream_t *parent1;
    stream_t *parent2;
};

struct array_stream {
    struct stream_base base;

    int64_t offset;
    float *input;
    int input_length;
};

struct fir_stream {
    struct stream_base base;

    stream_t *parent;
    bool is_complex;
    int64_t offset;
    int ntaps;
    int overlap;
    int data_offset;
    float buffer[BUFFER_SIZE];
    float taps[];
};

static int generate_complex_sine(stream_t *stream, float *output, int count) {
    struct sine_stream *s = (struct sine_stream *)stream;
    for (int i = 0; i < count; i += 2) {
        float phase = (fmodf(s->frequency * s->base.offset, 1) * 2 * M_PI) + s->phase;
        output[i] = cosf(phase) * s->scale;
        output[i+1] = sinf(phase) * s->scale;
        s->base.offset++;
    }
    return count;
}

static void seek_complex_sine(stream_t *stream, uint32_t offset) {
    struct sine_stream *s = (struct sine_stream *)stream;
    s->base.offset = offset;
}

stream_t* new_complex_sine_stream(double phase, double frequency, double scale) {
    struct sine_stream *ss = (struct sine_stream *)calloc(1, sizeof(struct sine_stream));
    if (!ss) {
        return NULL;
    }
    ss->phase = phase;
    ss->frequency = frequency;
    ss->scale = scale;
    ss->base.read = generate_complex_sine;
    ss->base.seek = seek_complex_sine;

    return (stream_t *)ss;
}

static int cc_mul(stream_t *stream, float *output, int count) {
    struct stream_combiner *s = (struct stream_combiner *)stream;

    complex_t v1[BUFFER_SIZE / 2];
    complex_t v2[BUFFER_SIZE / 2];

    complex_t *out = (complex_t *)output;

    int read = 0;
    while (!s->base.eos && count > 0) {
        int to_read = min(BUFFER_SIZE, count);
        read_samples_from_stream(s->parent1, (float_t *)v1, to_read);
        read_samples_from_stream(s->parent2, (float_t *)v2, to_read);

        for (int i = 0; i < to_read / 2; i++) {
            out[i + read / 2] = v1[i] * v2[i];
        }

        read += to_read;
        count -= to_read;

        s->base.eos = (s->parent1->eos || s->parent1->eos);
    }

    s->base.eos = (s->parent1->eos || s->parent1->eos);

    return read;
}

static void complex_multiply_stream_cleanup(stream_t *stream) {
    struct stream_combiner *ss = (struct stream_combiner *)stream;
    if (ss->parent1) {
        free_stream(ss->parent1);
        ss->parent1 = NULL;
    }
    if (ss->parent2) {
        free_stream(ss->parent2);
        ss->parent2 = NULL;
    }
}

static void complex_multiply_stream_seek(stream_t *stream, uint32_t offset) {
    struct stream_combiner *ss = (struct stream_combiner *)stream;
    if (ss->parent1 && ss->parent1->seek) {
        ss->parent1->seek(ss->parent1, offset);

        if (ss->parent2 && ss->parent2->seek) {
            ss->parent2->seek(ss->parent2, ss->parent2->offset);
        }
    }

    ss->base.eos = (!ss->parent1 || ss->parent1->eos || !ss->parent2 || ss->parent2->eos);
}

stream_t* new_complex_multiply_stream(stream_t *parent1, stream_t *parent2) {
    struct stream_combiner *ss = (struct stream_combiner *)calloc(1, sizeof(struct stream_combiner));
    if (!ss) {
        return NULL;
    }
    ss->parent1 = parent1;
    ss->parent2 = parent2;
    ss->base.read = cc_mul;
    ss->base.seek = complex_multiply_stream_seek;
    ss->base.cleanup = complex_multiply_stream_cleanup;

    return (stream_t *)ss;
}

stream_t *new_frequency_translate_stream(stream_t *parent, double frequency) {
    stream_t *sine_stream = new_complex_sine_stream(0.0, frequency, 1.0);
    if (!sine_stream) {
        return NULL;
    }

    stream_t *ss = new_complex_multiply_stream(parent, sine_stream);
    if (!ss) {
        free_stream(sine_stream);
        return NULL;
    }

    return ss;
}

void free_stream(stream_t *stream) {
    if (stream != NULL) {
        if (stream->cleanup) {
            stream->cleanup(stream);
        }
        free(stream);
    }
}

/**
 * array_stream_read
 *
 * Reads floats from a float array (mostly for testing).
 * Can also be used for complex input/output - in that case the values are interleaved I/Q.
 */
static int array_stream_read(stream_t *stream, float *output, int count) {
    struct array_stream *as = (struct array_stream *)stream;

    int within_stream = min(count, as->input_length - as->base.offset);
    int rest = count - within_stream;

    memcpy(output, &as->input[as->base.offset], within_stream * sizeof(float));
    if (rest > 0) {
        bzero(&output[within_stream], rest * sizeof(float));
    }
    as->base.offset += within_stream;

    return count;
}

static void array_stream_seek(stream_t *stream, uint32_t offset) {
    struct array_stream *as = (struct array_stream *)stream;

    as->base.offset = offset;
}


stream_t* new_array_stream(float *input, int length) {
    struct array_stream *as = (struct array_stream *)calloc(1, sizeof(struct array_stream));

    as->input = input;
    as->input_length = length;
    as->base.read = array_stream_read;
    as->base.seek = array_stream_seek;

    return (stream_t *)as;
}

/*

def fill(self):
    while self.data_offset < len(self.buffer):
        to_read = len(self.buffer) - self.data_offset
        data = self.backend.read(to_read)
        self.buffer[self.data_offset:self.data_offset+len(data)] = data
        self.data_offset += len(data)

*/
void fir_stream_fill(struct fir_stream *fs) {
    while (!fs->base.eos && fs->data_offset < BUFFER_SIZE) {
        int to_read = BUFFER_SIZE - fs->data_offset;
        int read = fs->parent->read(fs->parent, &fs->buffer[fs->data_offset], to_read);
        if (read <= 0) {
            // No more data ...
            fs->base.eos = true;
            break;
        }
        fs->data_offset += read;
    }
}

/*
def shift_and_read(self):
    """Shift tail end to start of buffer and fill with new data"""
    self.buffer[:self.overlap] = self.buffer[-self.overlap:]
    self.data_offset = self.overlap
    self.fill()
    self.offset = 0
*/
void fir_stream_shift_and_read(struct fir_stream *fs) {
    memmove(fs->buffer, &fs->buffer[BUFFER_SIZE - fs->overlap], fs->overlap * sizeof(float));
    fs->data_offset = fs->overlap;
    fir_stream_fill(fs);
    fs->offset = 0;
}

/*

def fir_filter(self):
    """Produce one output sample by filtering the current position in the input stream."""
    if self.offset + len(self.taps) * 2 > len(self.buffer):
        self.shift_and_read()
    input_data = self.buffer[self.offset:self.offset+len(self.taps)*2].view(np.complex64)
    self.offset += 2
    return np.sum(np.multiply(input_data, self.taps))

*/
void fir_stream_fir_filter(struct fir_stream *fs, float *output) {
    if (fs->offset + fs->ntaps > BUFFER_SIZE) {
        fir_stream_shift_and_read(fs);
    }

    *output = 0;
    for (int i = 0; i < fs->ntaps; i++) {
        *output += fs->buffer[fs->offset+i] * fs->taps[fs->ntaps - i - 1];
    }
    fs->offset++;
}

void fir_stream_fir_filter_complex(struct fir_stream *fs, float *output) {
    if (fs->offset + fs->ntaps > BUFFER_SIZE) {
        fir_stream_shift_and_read(fs);
    }

    complex_t c_out = 0;
    complex_t *d_in = ((complex_t *)&fs->buffer[fs->offset]);
    complex_t *taps = ((complex_t *)fs->taps);
    int last_tap = fs->ntaps / 2 - 1;
    for (int i = 0; i < fs->ntaps / 2; i++) {
        c_out += d_in[i] * taps[last_tap-i];
    }
    fs->offset += 2;
    *((complex_t *)output) = c_out;
}


/*
def read(self, n):
    """Read filtered data"""
    output = np.zeros(n // 2, dtype=np.complex64)
    for i in range(len(output)):
        output[i] = self.fir_filter()
    return output.view(np.float32)
*/
static int fir_stream_read(stream_t *stream, float *output, int count) {
    struct fir_stream *fs = (struct fir_stream *)stream;

    if (fs->base.eos) {
        return 0;
    }

    if (fs->is_complex) {
        for (int i = 0; i < count; i += 2) {
            fir_stream_fir_filter_complex(fs, &output[i]);
        }
    } else {
        for (int i = 0; i < count; i++) {
            fir_stream_fir_filter(fs, &output[i]);
        }
    }

    fs->base.offset += count;

    return count;
}

static void fir_stream_seek(stream_t *stream, uint32_t offset) {
    struct fir_stream *fs = (struct fir_stream *)stream;
    if (fs->parent && fs->parent->seek) {
        fs->parent->seek(fs->parent, offset);
        fs->base.offset = fs->parent->offset - fs->overlap;
    }
    fs->offset = 0;
    fs->data_offset = fs->overlap;
    bzero(fs->buffer, sizeof(float) * BUFFER_SIZE);
    fir_stream_shift_and_read(fs);
    fs->base.eos = (fs->parent && fs->parent->eos);
}

static void fir_stream_cleanup(stream_t *stream) {
    struct fir_stream *fs = (struct fir_stream *)stream;
    if (fs->parent) {
        free_stream(fs->parent);
    }
}

stream_t* new_fir_stream(stream_t *data, float *taps, int tap_count, bool is_complex) {
    struct fir_stream *fs = (struct fir_stream *)calloc(1, sizeof(struct fir_stream) + sizeof(float) * tap_count);
    if (!fs) {
        return NULL;
    }

    fs->offset = BUFFER_SIZE;
    fs->parent = data;
    fs->is_complex = is_complex;
    fs->ntaps = tap_count;
    fs->overlap = is_complex ? tap_count - 2 : tap_count - 1;
    fs->base.read = fir_stream_read;
    fs->base.seek = fir_stream_seek;
    fs->base.cleanup = fir_stream_cleanup;
    memcpy(&fs->taps[0], taps, tap_count * sizeof(float));

    return (stream_t *)fs;
}


static struct stream_dir_entry_s* find_stream_entry(const char *name, const char *path, stream_list_t *list) {
    struct stream_dir_entry_s *entry = SLIST_FIRST(list);

    while (entry) {
        if (!strncmp(entry->name, name, STORE_MAX_NAME) && !strncmp(entry->path, path, STORE_MAX_PATH))
            return entry;
        entry = SLIST_NEXT(entry, next);
    }

    return NULL;
}

stream_t* find_stream(const char *name, const char *path, stream_list_t *list) {
    struct stream_dir_entry_s *entry = find_stream_entry(name, path, list);
    if (!entry) {
        return NULL;
    }

    return entry->stream;
}




int add_stream(const char *name, const char *path, stream_list_t *list, stream_t *stream) {
    if (find_stream(name, path, list)) {
        return STREAM_ALREADY_EXISTS;
    }

    struct stream_dir_entry_s *entry = calloc(1, sizeof(struct stream_dir_entry_s));
    if (!entry) {
        return STREAM_OUT_OF_MEMORY;
    }

    strncpy(entry->name, name, STORE_MAX_NAME);
    strncpy(entry->path, path, STORE_MAX_PATH);
    entry->stream = stream;

    fprintf(stderr, "Creating stream '%s/%s'\n", path, name);

    SLIST_INSERT_HEAD(list, entry, next);

    return STREAM_OK;
}

int remove_stream(const char *name, const char *path, stream_list_t *list) {
    struct stream_dir_entry_s *entry = find_stream_entry(name, path, list);
    if (!entry) {
        return STREAM_NOT_FOUND;
    }

    SLIST_REMOVE(list, entry, stream_dir_entry_s, next);

    free_stream(entry->stream);
    free(entry);

    return STREAM_OK;
}

void iterate_stream_list(const char *path, stream_list_t *list, iterate_streams_callback callback, void *context) {
    struct stream_dir_entry_s *entry = SLIST_FIRST(list);

    while (entry) {
        // fprintf(stderr, "Iterate: %s/%s\n", entry->path, entry->name);
        if (!path[0] || !strncmp(entry->path, path, STORE_MAX_PATH)) {
            if (callback(context, entry->name, entry->path, entry->stream))
                break;
        }
        entry = SLIST_NEXT(entry, next);
    }
}

static void store_reader_clear_buffer(struct store_reader_stream *sr) {
    sr->base.eos = true;
    bzero(sr->buffer, sizeof(float) * BUFFER_SIZE);
    sr->buffer_count = 0;
    sr->buffer_offset = 0;
}

#define BLOCK_TYPE_I8_SAMPLES  1
#define BLOCK_TYPE_I16_SAMPLES 2
#define BLOCK_TYPE_I32_SAMPLES 3
#define BLOCK_TYPE_F32_SAMPLES 4
#define BLOCK_TYPE_F64_SAMPLES 5

static void store_reader_fill_block(struct store_reader_stream *sr) {
    block_t block;

    if (sr->current_block < sr->start_block || sr->current_block >= sr->end_block) {
        // Reading before / after stream will trigger an EOS
        store_reader_clear_buffer(sr);
        return;
    }

    int res = store_read_block(sr->store, &block, sr->current_block);
    if (res != STORE_OK) {
        fprintf(stderr, "Store reader: Error reading from store: %d\n", res);
        store_reader_clear_buffer(sr);
        return;
    }

    if (block.hdr.block_id != sr->current_block || block.hdr.block_length < BLOCK_HEADER_LENGTH) {
        store_reader_clear_buffer(sr);
        return;
    }

    sr->buffer_offset = 0;
    sr->current_block++;

    float scale = block.hdr.sample_block_header.scale;
    if (scale == 0) {
        scale = 1.0f;
    }

    int data_item_size = 1;
    switch (block.hdr.block_type) {
    case BLOCK_TYPE_I8_SAMPLES:
        data_item_size = 1;
        break;
    case BLOCK_TYPE_I16_SAMPLES:
        data_item_size = 2;
        break;
    case BLOCK_TYPE_I32_SAMPLES:
        data_item_size = 4;
        break;
    case BLOCK_TYPE_F32_SAMPLES:
        data_item_size = 4;
        break;
    case BLOCK_TYPE_F64_SAMPLES:
        data_item_size = 8;
        break;
    }

    sr->buffer_count = (block.hdr.block_length - BLOCK_HEADER_LENGTH) / data_item_size;

    for (int i = 0; i < sr->buffer_count; i++) {
        switch (block.hdr.block_type) {
        case BLOCK_TYPE_I8_SAMPLES:
            sr->buffer[i] = block.data.i8_samples[i] * scale;
            break;
        case BLOCK_TYPE_I16_SAMPLES:
            sr->buffer[i] = block.data.i16_samples[i] * scale;
            break;
        case BLOCK_TYPE_I32_SAMPLES:
            sr->buffer[i] = block.data.i32_samples[i] * scale;
            break;
        case BLOCK_TYPE_F32_SAMPLES:
            sr->buffer[i] = block.data.f32_samples[i] * scale;
            break;
        case BLOCK_TYPE_F64_SAMPLES:
            sr->buffer[i] = block.data.f64_samples[i] * scale;
            break;
        }
    }
}

static int store_reader_read(stream_t *stream, float *output, int count) {
    struct store_reader_stream *sr = (struct store_reader_stream *)stream;

    int r = 0;

    while ((!sr->base.eos) && (count > 0)) {
        int to_read = min(sr->buffer_count - sr->buffer_offset, count);
        if (to_read > 0) {
            memcpy(&output[r], &sr->buffer[sr->buffer_offset], to_read * sizeof(float));
            r += to_read;
            count -= to_read;
            sr->buffer_offset += to_read;
        }

        if (sr->buffer_offset == sr->buffer_count) {
            store_reader_fill_block(sr);
        }
    }

    return r;
}

static void store_reader_seek(stream_t *stream, uint32_t offset) {
    struct store_reader_stream *sr = (struct store_reader_stream *)stream;

    sr->current_block = sr->start_block + offset;
    sr->base.eos = false;
    store_reader_fill_block(sr);
}

static void store_reader_cleanup(stream_t *stream) {
    struct store_reader_stream *sr = (struct store_reader_stream *)stream;

    store_release(sr->store);
    sr->store = NULL;
}

stream_t* new_store_reader_stream(store_t *store, uint32_t start_block, uint32_t end_block) {
    struct store_reader_stream *sr = (struct store_reader_stream *)calloc(1, sizeof(struct store_reader_stream));
    if (!sr) {
        return NULL;
    }

    sr->store = store;
    sr->start_block = start_block;
    sr->end_block = end_block;
    sr->base.cleanup = store_reader_cleanup;
    sr->base.seek = store_reader_seek;
    sr->base.read = store_reader_read;

    store_use(sr->store);
    store_reader_seek(&sr->base, 0);

    return (struct stream_base *)sr;
}
