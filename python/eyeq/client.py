import zmq, time

import eyeq.samples_pb2 as samples
from eyeq import block
import numpy as np

class TransactionError(Exception):
    pass

class Client:
    ''"Client is a client connection to the eyeq server."""
    
    def __init__(self, endpoint = 'tcp://localhost:13450', context=None):
        self.endpoint = endpoint
        self.context = context or zmq.Context.instance()
        self.socket = self.context.socket(zmq.DEALER)
        self.socket.connect(self.endpoint)
        self.poll = zmq.Poller()
        self.poll.register(self.socket, zmq.POLLIN)
        self.sequence_id = 0

    def __del__(self):
        self.socket.close()
        self.context.term()

    def receive_responses(self, number=1, timeout=2000):
        end_time = time.time() + timeout / 1000

        while number > 0:
            if end_time < time.time():
                break
            sockets = dict(self.poll.poll((end_time - time.time())*1000))
            if self.socket in sockets:
                _, message = self.socket.recv_multipart()
                response = samples.ServerResponse()
                response.ParseFromString(message)
                if response.req_id != self.sequence_id:
                    continue
                if response.error != '':
                    raise TransactionError("Server error: " + str(response.error))
                yield response
                number -= 1
            else:
                break

    def transaction(self, request):
        self.sequence_id += 1
        request.req_id = self.sequence_id
        self.socket.send_multipart([b'', request.SerializeToString()])
        for response in self.receive_responses():
            return response
        raise TransactionError("No response")

    def multi_transaction(self, requests, expected_responses=1, timeout=10000):
        self.sequence_id += 1
        for request in requests:
            request.req_id = self.sequence_id
            self.socket.send_multipart([b'', request.SerializeToString()])

        for response in self.receive_responses(number=expected_responses, timeout=timeout):
            yield response

    def create_store(self, name, block_count, path='', store_type = samples.StoreType.MEMORY_STORE, file_path=''):
        request = samples.ServerRequest()
        cs = samples.CreateStore()
        cs.store.name = name
        cs.store.path = path
        cs.store.block_count = block_count
        cs.store.store_type = store_type
        cs.store.file_path = file_path
        request.create_store.CopyFrom(cs)
        self.transaction(request).create_store_response
        return Store(self, name, path)

    def store(self, name, path):
        return Store(self, name, path)

    def stream(self, name, path):
        return Stream(self, name, path)

    def list_stores(self, path=''):
        request = samples.ServerRequest()
        ls = samples.ListStores()
        ls.path = path
        request.list_stores.CopyFrom(ls)
        return self.transaction(request).list_stores_response

    def delete_store(self, name, path=''):
        request = samples.ServerRequest()
        ds = samples.DeleteStore()
        ds.name = name
        ds.path = path
        request.delete_store.CopyFrom(ds)
        return self.transaction(request).delete_store_response

    def flush_stores(self):
        request = samples.ServerRequest()
        fs = samples.FlushStores()
        request.flush_stores.CopyFrom(fs)
        return self.transaction(request).flush_stores_response

    def write_block(self, name, bl, path=''):
        request = samples.ServerRequest()
        wb = samples.WriteBlock()
        wb.name = name
        wb.path = path
        wb.offset = bl.header.block_id
        wb.block.data = bl.tobytes()
        request.write_block.CopyFrom(wb)
        return self.transaction(request).write_block_response

    def write_blocks(self, name, blocks, path=''):
        requests = []
        for bl in blocks:
            request = samples.ServerRequest()
            wb = samples.WriteBlock()
            wb.name = name
            wb.path = path
            wb.offset = bl.header.block_id
            wb.block.data = bl.tobytes()
            request.write_block.CopyFrom(wb)
            requests.append(request)
        return [r for r in self.multi_transaction(requests, expected_responses=len(requests))]

    def read_block(self, name, offset, path=''):
        request = samples.ServerRequest()
        rb = samples.ReadBlocks()
        rb.name = name
        rb.path = path
        rb.offset = offset
        rb.count = 1
        request.read_blocks.CopyFrom(rb)
        response = self.transaction(request).read_blocks_response
        return block.parse_block(response.block.data)

    def read_blocks(self, name, offset, path='', count=1, timeout=10000):
        request = samples.ServerRequest()
        rb = samples.ReadBlocks()
        rb.name = name
        rb.path = path
        rb.offset = offset
        rb.count = count
        request.read_blocks.CopyFrom(rb)
        number_of_blocks = 0
        for response in self.multi_transaction([request], expected_responses=count, timeout=timeout):
            block_data = block.parse_block(response.read_blocks_response.block.data)
            number_of_blocks += 1
            yield block_data
        if number_of_blocks == 0:
            raise TransactionError("No response")

    def create_stream(self, name, layers=[], path=''):
        request = samples.ServerRequest()
        cs = samples.CreateStream()
        cs.stream.name = name
        cs.stream.path = path
        for layer in layers:
            l = cs.layers.add()
            if isinstance(layer, samples.StoreReaderStream):
                l.store_reader.CopyFrom(layer)
            elif isinstance(layer, samples.FirFilterStream):
                l.fir_filter.CopyFrom(layer)
            elif isinstance(layer, samples.FrequencyTranslateStream):
                l.frequency_translate.CopyFrom(layer)
            else:
                raise Exception(f"don't know about layer: {layer}")
        request.create_stream.CopyFrom(cs)
        self.transaction(request).create_stream_response
        return Stream(self, name, path)

    def seek_stream(self, name, block_id=0, path=''):
        request = samples.ServerRequest()
        ss = samples.SeekStream()
        ss.name = name
        ss.path = path
        ss.block_id = block_id
        request.seek_stream.CopyFrom(ss)
        response = self.transaction(request).seek_stream_response
        return response

    def read_stream(self, name, sample_count, path='', timeout=10000):
        request = samples.ServerRequest()
        rs = samples.ReadStream()
        rs.name = name
        rs.path = path
        rs.sample_count = sample_count
        request.read_stream.CopyFrom(rs)

        expected_count = (sample_count + 4095) // 4096
        result = np.zeros(sample_count, dtype=np.float32)
        actually_read = 0
        for response in self.multi_transaction([request], expected_responses=expected_count, timeout=timeout):
            rs_response = response.read_stream_response
            data = rs_response.samples
            result[actually_read:actually_read+len(data)] = data
            actually_read += len(data)
            if rs_response.eos:
                break
        return result

    def list_streams(self, path=''):
        request = samples.ServerRequest()
        ls = samples.ListStreams()
        ls.path = path
        request.list_streams.CopyFrom(ls)
        return self.transaction(request).list_streams_response

    def close_stream(self, name, path=''):
        request = samples.ServerRequest()
        ls = samples.CloseStream()
        ls.name = name
        ls.path = path
        request.close_stream.CopyFrom(ls)
        return self.transaction(request).close_stream_response
    
    def read_samples(self, name, start_block, Nsamples, path='', end_block=-1):
        # TODO: Replace with stream read
        result = np.zeros(Nsamples, dtype=np.float32)
        length = 0
        i = start_block
        while length < Nsamples and (end_block == -1 or i < end_block):
            try:
                bl = self.read_block(name, i, path=path)
                needed = min(Nsamples-length, len(bl.data))
                result[length:length+needed] = bl.samples()[:needed]
                length += len(bl.data)
                i += 1
            except TransactionError:
                return result[:length]
        return result[:min(length, Nsamples)]

    def read_complex_samples(self, name, start_block, Nsamples, path='', end_block=-1):
        return self.read_samples(name, start_block, Nsamples * 2, path=path, end_block=end_block).view(np.complex64)


class Store:
    """Store represents the client's reference to a server-side store."""
    
    def __init__(self, client, name, path=''):
        self.client = client
        self.name = name
        self.path = path

    def write(self, bl):
        self.client.write_block(self.name, bl, path=self.path)

    def read(self, offset):
        self.client.read_block(self.name, offset, path=self.path)

class Stream:
    """Stream represents the client's reference to a server-side stream."""

    def __init__(self, client, name, path=''):
        self.client = client
        self.name = name
        self.path = path

    def read(self, count):
        self.client.read_stream(self.name, count, path=self.path)