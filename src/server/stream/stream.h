#pragma once

#include <eyeq/server/store.h>
#include <eyeq/server/stream.h>

stream_t* new_store_reader_stream(store_t *store, uint32_t start_block, uint32_t end_block);
stream_t* new_complex_sine_stream(double phase, double frequency, double scale);
stream_t* new_multiply_stream(stream_t *parent1, stream_t *parent2);
stream_t* new_complex_multiply_stream(stream_t *parent1, stream_t *parent2);
stream_t* new_array_stream(float *input, int length);
stream_t* new_fir_stream(stream_t *data, float *taps, int tap_count, bool is_complex);
stream_t* new_frequency_translate_stream(stream_t *parent, double frequency);
