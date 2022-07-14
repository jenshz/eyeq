#include <unistd.h>

#include "unity.h"
#include "eyeq/shared.h"
#include "eyeq/server.h"
#include "eyeq/server/store.h"

const char *test_store_filename = "/tmp/eyeq_sample_store.dat";
const char *test_store_list_filename = "/tmp/eyeq_sample_list.txt";

void setUp(void) {
}

void tearDown(void) {
}

void test_crc32_array(void) {
    uint8_t test_array[11] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10 };

    /*
        In python3:
        import zlib
        "%08x" % zlib.crc32(b'\x00\x01\x02\x03\x04\x05\x06\x07\x08\x09\x0A')
        => 'ad2d8ee1'
    */
    uint32_t output = eyeq_crc32(EYEQ_CRC_INITIAL, test_array, 11) ^ EYEQ_CRC_INITIAL;
    TEST_ASSERT_EQUAL_UINT32(0xad2d8ee1, output);
}

void test_block_size(void) {
    block_t block;
    TEST_ASSERT_EQUAL(BLOCK_LENGTH, sizeof(block));
    TEST_ASSERT_EQUAL(SAMPLE_DATA_PER_BLOCK, sizeof(block.data));
}

static void verify_store_metadata(store_t *store) {
    TEST_ASSERT_EQUAL_UINT32(128, store->block_count);
}

void test_new_memory_store(void) {
    store_t *store = new_memory_store(128);
    TEST_ASSERT_NOT_NULL(store);
    verify_store_metadata(store);
    free_store(store);
}


void test_new_file_store(void) {
    store_t *store = new_file_store(test_store_filename, 128, true);
    TEST_ASSERT_NOT_NULL(store);
    verify_store_metadata(store);
    free_store(store);
    unlink(test_store_filename);
}

static void store_read_write_test(store_t *store) {
    const block_t all_zero = { 0 };

    // Test reading from store (before it's initialized)
    block_t block;
    int res = store_read_block(store, &block, 0);

    if (store->store_type == MEMORY_STORE) {
        TEST_ASSERT_EQUAL_INT(STORE_OK, res);
        TEST_ASSERT_EQUAL_INT16_ARRAY(block.data.i16_samples, all_zero.data.i16_samples, BLOCK_I16_SAMPLES);
    } else if (store->store_type == FILE_STORE) {
        TEST_ASSERT_EQUAL_INT(STORE_READ_ERROR, res);
    }

    // Test writing to store
    for (int i = 0; i < BLOCK_I16_SAMPLES; i++) {
        block.data.i16_samples[i] = i;
    }

    res = store_write_block(store, &block, -1);
    TEST_ASSERT_EQUAL_INT(STORE_OK, res);
    TEST_ASSERT_EQUAL_UINT32(1, store->write_offset);

    for (int i = 0; i < BLOCK_I16_SAMPLES; i++) {
        block.data.i16_samples[i] = i;
    }

    block_t read_block = { 0 };
    res = store_read_block(store, &read_block, 0);
    TEST_ASSERT_EQUAL_INT(STORE_OK, res);
    TEST_ASSERT_EQUAL_INT16_ARRAY(block.data.i16_samples, read_block.data.i16_samples, BLOCK_I16_SAMPLES);
}

void test_memory_store_io(void) {
    store_t *store = new_memory_store(128);
    TEST_ASSERT_NOT_NULL(store);

    store_read_write_test(store);

    free_store(store);
}

// Read and write from a file backed store
void test_file_store_io(void) {
    store_t *store = new_file_store(test_store_filename, 128, true);
    TEST_ASSERT_NOT_NULL(store);

    store_read_write_test(store);

    free_store(store);
    unlink(test_store_filename);
}

static bool iteration_test_callback(void *context, const char *name, const char *path, store_t *store) {
    TEST_ASSERT_EQUAL_STRING("samples", name);
    return false;
}

// Add, find, iterate and remove
void test_store_list(void) {
    store_t *store = new_memory_store(128);
    TEST_ASSERT_NOT_NULL(store);

    store_list_t list = { 0 };

    // Add
    TEST_ASSERT_EQUAL_INT(STORE_OK, add_store("samples", "", &list, store));

    store_t *store2 = new_memory_store(128);
    TEST_ASSERT_EQUAL_INT(STORE_OK, add_store("samples", "testing", &list, store2));
    store_t *found_store = find_store("samples", "", &list);
    TEST_ASSERT_NOT_NULL(found_store);

    // Iterate
    iterate_store_list("testing", &list, iteration_test_callback, NULL);

    // Save
    int res = save_store_list_to_file(test_store_list_filename, &list);
    TEST_ASSERT_EQUAL_INT(STORE_OK, res);

    // Load
    store_list_t other_list = { 0 };
    res = load_store_list_from_file(test_store_list_filename, &other_list);
    TEST_ASSERT_EQUAL_INT(STORE_OK, res);

    // Remove
    res = remove_store("samples", "", &list);
    TEST_ASSERT_EQUAL_INT(STORE_OK, res);

    found_store = find_store("samples", "", &list);
    TEST_ASSERT_NULL(found_store);
    found_store = find_store("samples", "testing", &list);
    TEST_ASSERT_NOT_NULL(found_store);

    free_store_list(&list);
    free_store_list(&other_list);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_crc32_array);
    RUN_TEST(test_block_size);
    RUN_TEST(test_new_memory_store);
    RUN_TEST(test_memory_store_io);
    RUN_TEST(test_new_file_store);
    RUN_TEST(test_file_store_io);

    // Store list
    RUN_TEST(test_store_list);

    return UNITY_END();
}
