import numpy as np
import struct
from collections import namedtuple

BLOCK_LENGTH = 16384
BLOCK_HEADER_LENGTH = 128

SAMPLE_DATA_PER_BLOCK = BLOCK_LENGTH - BLOCK_HEADER_LENGTH
BLOCK_I16_SAMPLES = SAMPLE_DATA_PER_BLOCK // 2
BLOCK_I8_SAMPLES =  SAMPLE_DATA_PER_BLOCK
BLOCK_F32_SAMPLES = SAMPLE_DATA_PER_BLOCK // 4
BLOCK_F64_SAMPLES = SAMPLE_DATA_PER_BLOCK // 8

BLOCK_TYPE_BYTES       = 0
BLOCK_TYPE_I8_SAMPLES  = 1
BLOCK_TYPE_I16_SAMPLES = 2
BLOCK_TYPE_I32_SAMPLES = 3
BLOCK_TYPE_F32_SAMPLES = 4
BLOCK_TYPE_F64_SAMPLES = 5

BLOCK_MAGIC = 0x51657945

class BlockHeader:
    HEADER_PACK_STRING = "<IiHHIIIH"
    HEADER_LENGTH = 4 + 4 + 2 + 2 + 4 + 4 + 4 + 2

    def __init__(self, block_type, source_id=0):
        self.block_id = -1
        self.block_type = block_type
        self.block_length = 0
        self.crc32 = 0
        self.source_id = source_id
        self.timestamp_sec = 0
        self.timestamp_nsec = 0

    def set_time(self, timestamp):
        self.timestamp_sec = int(timestamp)
        self.timestamp_nsec = int((timestamp % 1) * 1e9)

    def pack(self):
        # CRC is written by backend - should be set to 0 here.
        hdr = struct.pack(BlockHeader.HEADER_PACK_STRING, BLOCK_MAGIC, self.block_id, self.block_type, self.block_length, self.crc32, self.timestamp_sec, self.timestamp_nsec, self.source_id)
        hdr += b'\0' * (BLOCK_HEADER_LENGTH - len(hdr))
        return hdr

    def parse(data):
        t = struct.unpack(BlockHeader.HEADER_PACK_STRING, data)
        assert t[0] == BLOCK_MAGIC
        hdr = BlockHeader(t[2])
        hdr.block_id = t[1]
        hdr.block_length = t[3]
        hdr.crc32 = t[4]
        hdr.timestamp_sec = t[5]
        hdr.timestamp_nsec = t[6]
        hdr.source_id = t[7]
        return hdr

    def __str__(self):
        return f"block_id: {self.block_id}, block_type: {self.block_type}, block_length: {self.block_length}, crc32: {self.crc32}, source_id: {self.source_id}, timestamp_sec: {self.timestamp_sec}, timestamp_nsec: {self.timestamp_nsec}"

def parse_block(data):
    block = Block()
    block.header = BlockHeader.parse(data[:BlockHeader.HEADER_LENGTH])
    block.init_block_data(data[BLOCK_HEADER_LENGTH:block.header.block_length])
    return block

class Block:
    """Contains a data block"""
    def __init__(self):
        self.count = 0
        self.max_count = 0

    def create(block_type, timestamp=None, data=None, source_id=0):
        block = Block()
        block.header = BlockHeader(block_type, source_id)
        if timestamp:
            block.header.set_time(timestamp)
        block.init_block_data(data)
        return block

    def append(self, value):
        if self.count == self.max_count:
            raise RuntimeException("No more space in sample buffer")
        self.data[self.count] = value
        self.count += 1

    def init_block_data(self, data=None):
        if self.header.block_type == BLOCK_TYPE_BYTES:
            self.dtype = np.int8
            self.max_count = BLOCK_I8_SAMPLES
        elif self.header.block_type == BLOCK_TYPE_I16_SAMPLES:
            self.dtype = np.int16
            self.max_count = BLOCK_I16_SAMPLES
        elif self.header.block_type == BLOCK_TYPE_I8_SAMPLES:
            self.dtype = np.int8
            self.max_count = BLOCK_I8_SAMPLES
        elif self.header.block_type == BLOCK_TYPE_F32_SAMPLES:
            self.dtype = np.float32
            self.max_count = BLOCK_F32_SAMPLES
        elif self.header.block_type == BLOCK_TYPE_F64_SAMPLES:
            self.dtype = np.float64
            self.max_count = BLOCK_F64_SAMPLES
        self.data = np.zeros(self.max_count, dtype=self.dtype)
        if not data is None:
            if isinstance(data, bytes):
                data = np.frombuffer(data, dtype=self.dtype)
            self.count = len(data)
            self.data[:self.count] = data

    def samples(self):
        return self.data[:self.count].astype(np.float32)

    def tobytes(self):
        return self.header.pack() + self.data.tobytes()
