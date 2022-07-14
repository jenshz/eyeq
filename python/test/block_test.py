#!/usr/bin/env python3

import time
import numpy as np
import eyeq
from pytest import fixture
from eyeq.samples_pb2 import StoreType

@fixture(scope="session")
def client():
	client = eyeq.Client()
	resp = client.list_stores("pytest")
	for store in resp.stores:
		client.delete_store(store.name, "pytest")
	return client

def timeit(l, description=""):
	t0 = time.time()
	res = l()
	t1 = time.time()
	print(f"{description}: {int((t1-t0) * 1000)}ms")
	return res

def write_100(client, store, data):
	client.write_blocks(store, [data for i in range(100)], path="pytest")

def test_create_store(client):
	client.create_store("samples", 128, path="pytest")

def test_read_write(client):
	stores = client.list_stores().stores
	assert len(stores) > 0

def test_write_block(client):
	data = eyeq.Block.create(eyeq.BLOCK_TYPE_F32_SAMPLES, time.time())
	assert client.write_block("samples", data, path="pytest").offset == 0
	assert client.write_block("samples", data, path="pytest").offset == 1

def test_write_performance(client):
	data = eyeq.Block.create(eyeq.BLOCK_TYPE_F32_SAMPLES, time.time())
	timeit(lambda: write_100(client, "samples", data), "writing 100 blocks to memory store")

def test_file_write_performance(client):
	client.create_store("file", 128, store_type=StoreType.FILE_STORE, file_path="/tmp/pytest_samples.dat", path="pytest")
	data = eyeq.Block.create(eyeq.BLOCK_TYPE_F32_SAMPLES, time.time())
	timeit(lambda: write_100(client, "file", data), "writing 100 blocks to file store")
	client.delete_store("file", path="pytest")

def test_multi_read(client):
	client.create_store("file", 128, store_type=StoreType.FILE_STORE, file_path="/tmp/pytest_samples.dat", path="pytest")
	data = eyeq.Block.create(eyeq.BLOCK_TYPE_F32_SAMPLES, time.time())
	timeit(lambda: write_100(client, "file", data), "writing 100 blocks to file store")
	def read_blocks_body():
		counter = 0
		for block in client.read_blocks("file", offset=0, count=100, path="pytest"):
			assert block.header.block_id == counter
			counter += 1
		assert counter == 100
	timeit(lambda: read_blocks_body(), "reading 100 blocks")
	client.delete_store("file", path="pytest")


def test_delete_store(client):
	client.delete_store("samples", path="pytest")
