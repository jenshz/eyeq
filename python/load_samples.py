#!/usr/bin/env python3

import numpy as np
from eyeq import Client, Block, block
client = Client("tcp://localhost:13450")

import os

# Clean-up
def tryit(l):
    try:
        l()
    except:
        pass

tryit(lambda: client.delete_store("jupyter"))
tryit(lambda: client.create_store("jupyter", 1000))

offset = 0
data = np.fromfile(os.sys.argv[1], dtype=np.float32)
while len(data) > 0:
    bl = block.Block.create(block.BLOCK_TYPE_F32_SAMPLES, data=data[:block.BLOCK_F32_SAMPLES])
    bl.header.block_id = offset
    client.write_block("jupyter", bl)
    print(f'Wrote block {offset}')
    offset += 1
    data = data[block.BLOCK_F32_SAMPLES:]
