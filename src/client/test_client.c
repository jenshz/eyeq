#include <eyeq/client.h>

#include "unity.h"

static eyeq_client_t *client;

void setUp(void) {
}

void tearDown(void) {
}

void preclean() {
	eyeq_delete_store(client, "TEST", "client-test");
}

void test_create_store() {
	eyeq_Store store;
	int resp = eyeq_find_store(client, "TEST", "client-test", &store);
	TEST_ASSERT_EQUAL_INT(EYEQ_NOT_FOUND, resp);

	strcpy(store.name, "TEST");
	strcpy(store.path, "client-test");
	store.block_count = 128;
	store.store_type = MEMORY_STORE;
	resp = eyeq_create_store(client, &store);
	TEST_ASSERT_EQUAL_INT(EYEQ_OK, resp);

	bzero(&store, sizeof(eyeq_Store));

	resp = eyeq_find_store(client, "TEST", "client-test", &store);
	TEST_ASSERT_EQUAL_INT(EYEQ_OK, resp);
	TEST_ASSERT_EQUAL_INT(128, store.block_count);
}

void test_write_blocks() {
	for (int j = 0; j < 100; j++) {
		eyeq_Block block = { 0 };
		block_t *bt = (block_t *)&block.data.bytes;
		block.data.size = BLOCK_LENGTH;

		bt->hdr.block_length = BLOCK_LENGTH;

		for (int i = 0; i < BLOCK_I16_SAMPLES; i++) {
			bt->data.i16_samples[i] = i + j;
		}

		uint32_t offset = 0;
		int resp = eyeq_write_block(client, "TEST", "client-test", &block, -1, &offset);
		TEST_ASSERT_EQUAL_INT(EYEQ_OK, resp);
		TEST_ASSERT_EQUAL_INT(j, offset);
	}
}

void test_read_blocks() {
	int j = 0;

	bool read_block_callback(eyeq_Block *block, void *context) {
		block_t *bt = (block_t *)&block->data.bytes;
		TEST_ASSERT_EQUAL_INT(j, bt->hdr.block_id);

		int16_t reference[BLOCK_I16_SAMPLES];

		for (int i = 0; i < BLOCK_I16_SAMPLES; i++) {
			reference[i] = i + j;
		}

		TEST_ASSERT_EQUAL_INT16_ARRAY(reference, bt->data.i16_samples, BLOCK_I16_SAMPLES);

		j++;

		return true;
	}

	int resp = eyeq_read_blocks(client, "TEST", "client-test", 0, 100, read_block_callback, 2000, NULL);
	TEST_ASSERT_EQUAL_INT(EYEQ_OK, resp);
	TEST_ASSERT_EQUAL_INT(100, j);
}

int main(int argc, char *argv[]) {
    UNITY_BEGIN();

    void *eyeq_ctx = eyeq_create_context();
	client = eyeq_connect("tcp://localhost:13450", eyeq_ctx, -1);
	TEST_ASSERT_NOT_NULL(client);

	preclean();

    RUN_TEST(test_create_store);
    RUN_TEST(test_write_blocks);
    RUN_TEST(test_read_blocks);

    eyeq_close(client);
    eyeq_destroy_context(eyeq_ctx);

    return UNITY_END();
}
