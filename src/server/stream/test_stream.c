#include <unistd.h>
#include <math.h>

#include "unity.h"
#include "stream.h"

void setUp(void) {
}

void tearDown(void) {
}

void test_read_int16_block(void) {
    float output[32];
    float reference[BLOCK_I16_SAMPLES + 128] = { 0 };

    block_t block = { 0 };
    store_t *store = new_memory_store(128);
    TEST_ASSERT_NOT_NULL(store);

    // Test writing to store
    for (int i = 0; i < BLOCK_I16_SAMPLES; i++) {
        block.data.i16_samples[i] = i;
        reference[i] = i;
    }

    block.hdr.block_length = BLOCK_LENGTH;
    block.hdr.block_type = BLOCK_TYPE_I16_SAMPLES;

    int res = store_write_block(store, &block, -1);
    TEST_ASSERT_EQUAL_INT(STORE_OK, res);

    stream_t *s = new_store_reader_stream(store, 0, 1);
    TEST_ASSERT_NOT_NULL(s);

    for (int i = 0; i < BLOCK_I16_SAMPLES + 128; i += 32) {
        read_samples_from_stream(s, output, 32);
        TEST_ASSERT_EQUAL_FLOAT_ARRAY(&reference[i], output, 32);
    }

    free_stream(s);
    free_store(store);
}

void test_complex_sine_stream(void) {
    float output[32];
    const float reference[32] = { 1.000000e+00,0.000000e+00,8.090170e-01,5.877852e-01,3.090170e-01,9.510565e-01,-3.090170e-01,9.510565e-01,-8.090171e-01,5.877852e-01,-1.000000e+00,-8.742278e-08,-8.090169e-01,-5.877854e-01,-3.090171e-01,-9.510565e-01,3.090171e-01,-9.510565e-01,8.090172e-01,-5.877849e-01,1.000000e+00,0.000000e+00,8.090169e-01,5.877854e-01,3.090167e-01,9.510566e-01,-3.090174e-01,9.510564e-01,-8.090169e-01,5.877854e-01,-1.000000e+00,-8.742278e-08 };

    stream_t *s = new_complex_sine_stream(0, 0.1, 1.0);
    TEST_ASSERT_NOT_NULL(s);

    read_samples_from_stream(s, output, 32);
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(reference, output, 32);

    free_stream(s);
}

void test_multiply(void) {
    float output[32];
    const float reference[32] = { 1.000000e+00,0.000000e+00,1.000000e+00,0.000000e+00, 1.000000e+00,0.000000e+00,1.000000e+00,0.000000e+00, 1.000000e+00,0.000000e+00,1.000000e+00,0.000000e+00, 1.000000e+00,0.000000e+00,1.000000e+00,0.000000e+00, 1.000000e+00,0.000000e+00,1.000000e+00,0.000000e+00, 1.000000e+00,0.000000e+00,1.000000e+00,0.000000e+00, 1.000000e+00,0.000000e+00,1.000000e+00,0.000000e+00, 1.000000e+00,0.000000e+00,1.000000e+00,0.000000e+00 };

    stream_t *s1 = new_complex_sine_stream(0, 0.1, 1.0);
    stream_t *s2 = new_complex_sine_stream(0, -0.1, 1.0);
    TEST_ASSERT_NOT_NULL(s1);
    TEST_ASSERT_NOT_NULL(s2);

    stream_t *s = new_complex_multiply_stream(s1, s2);
    TEST_ASSERT_NOT_NULL(s);

    read_samples_from_stream(s, output, 32);
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(reference, output, 32);

    // Free top-most stream frees the underlying streams too
    free_stream(s);
}

void test_array_stream(void) {
    float zeros[32] = { 0 };
    float reference[32];
    for (int i = 0; i < 32; i++) {
        reference[i] = i + 1.0f;
    }

    float output[64];

    stream_t *s = new_array_stream(reference, 32);
    TEST_ASSERT_NOT_NULL(s);

    read_samples_from_stream(s, output, 64);
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(reference, output, 32);
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(zeros, &output[32], 32);

    free_stream(s);
}

void test_fir_stream(void) {
    float test_data[4] = { 1, 2, 3, 4 };
    float taps[3] = { 1, 2, 1 };
    float reference[8] = { 1, 4, 8, 12, 11, 4, 0, 0 };
    float output[8];

    stream_t *data = new_array_stream(test_data, 4);
    TEST_ASSERT_NOT_NULL(data);

    stream_t *fir = new_fir_stream(data, taps, 3, false);

    read_samples_from_stream(fir, output, 8);
    TEST_ASSERT_EQUAL_FLOAT_ARRAY(reference, output, 8);

    free_stream(fir);
}

void test_fir_complex_stream(void) {
    float input_data[4500];
    float taps[100];
    float reference[4500];
    float output[4500];

    FILE *fin = fopen("../test_data/data.bin", "rb");
    TEST_ASSERT_NOT_NULL(fin);
    int input_len = fread(input_data, sizeof(float), 4500, fin);
    fclose(fin);

    fin = fopen("../test_data/taps.bin", "rb");
    TEST_ASSERT_NOT_NULL(fin);
    int ntaps = fread(taps, sizeof(float), 100, fin);
    fclose(fin);

    fin = fopen("../test_data/complex_convolved.bin", "rb");
    TEST_ASSERT_NOT_NULL(fin);
    int noutput = fread(reference, sizeof(float), 4500, fin);
    fclose(fin);

    stream_t *data = new_array_stream((float*)input_data, input_len);
    TEST_ASSERT_NOT_NULL(data);

    stream_t *fir = new_fir_stream(data, (float *)taps, ntaps, true);

    read_samples_from_stream(fir, output, noutput);

    double atol = 1e-08;
    double rtol = 3e-05;
    for (int i = 0; i < noutput; i++) {
        double a = reference[i];
        double b = output[i];
        TEST_ASSERT_TRUE(fabs(a - b) <= (atol + rtol * fabs(b)));
    }

    free_stream(fir);
}

int main(void) {
    UNITY_BEGIN();

    RUN_TEST(test_complex_sine_stream);
    RUN_TEST(test_multiply);
    RUN_TEST(test_array_stream);
    RUN_TEST(test_fir_stream);
    RUN_TEST(test_fir_complex_stream);
    RUN_TEST(test_read_int16_block);

    return UNITY_END();
}
