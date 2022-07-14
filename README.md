# EyeQ

IQ storage and visualization


## EyeQ CLI

If built with 'slash' support, the eyeq client application is also built.

This tool contains the following operations:

```
create_store <name> <path> <count> [store_type] [filename]

  Create a new block store with the specified name, path and block count.
  store_type defaults to 0 (RAM), but can be specified as 1 (file / block store) if needed.
  In this case, a filename should be provided.

list_stores [path]

  List all stores (optionally only search the given path for stores).

export_blocks <name> <start_block> <block_count> <filename> [path]

  Export block_count blocks from the specified store (name + path), starting from start_block.
  The blocks will be written in full (even if the block length per block varies).
  This allows for easy import later using import_blocks.
  If filename is "-", the data will be written to stdout.

import_blocks <name> <filename> [path]

  Import blocks that have been previously exported using "export_blocks".
  This functionality allows for creating backups from one block store to another.
  If filename is "-", the data will be read from stdin.

delete_store <name> [path]

  Deletes the store with the specified name and path

create_stream <name> <path> <store_name> <store_path> <start_block> <block_count> [relative_freq] [filter_type]

  Create stream with given name and path, based on the samples in the specified store.
  Sample reading will start from the block id specified as the start_block and
  up to block_count blocks will be read.

  If relative_freq is specified (e.g. not 0.0), the samples will be shifted in
  frequency by this value. NB: The value is *not* in Hz but specified relative to the sampling
  frequency. E.g. in the case a frequency shift of 10 KHz is desired at a sample rate of 200 KHz,
  the value should be 10 / 200, e.g. 0.05

  If filter_type is set, the stream will be filtered with a low-pass filter.
  The following filter shapes are supported:
    * 0: No filtering is applied.
    * 1: 100 Kchip/s (150 KHz pass-band)
    * 2: 50 Kchip/s (86 KHz pass-band)
    * 3: 25 Kchip/s (56 KHz pass-band)

  NB: Streams consist of floating point samples. In the case that relative_freq or filter_type
  is specified, the underlying samples will be treated as streams of complex numbers.
  This means that 2 floating point values will be combined into a single sample.

  The underlying stream subsystem supports more operations than are supported by this command
  line interface - in case any of this more advanced functionality is desired, please use the
  Python API (python/eyeq), since this API allows full access to the underlying functionality.

list_streams [path]

  List all streams (optionally only search the given path for streams).

seek_stream <name> <block_id> [path]

  Reset the stream to read from the specified block offset (relative to the start_block value
  in the create_stream call).

read_stream <name> <sample_count> <filename> [path] 

  Read stream samples from the specified stream (name + path).
  sample_count samples will be read from the stream and written to "filename".
  If filename is "-", the output will be written to stdout (useful for piping).

close_stream <name> [path]

  Close the stream, removing it from the EyeQ daemon (and releasing the lock on the underlying
  store).

```

### Example session:

```
# Previously added some data using Example Notebook.ipynb sample. In addition the server is running in another tab.
jens@laptop:builddir$ ./eyeq
Using 'tcp://localhost:13450' as EyeQ endpoint.
eyeq % list_stores
Listing stores:
* /jupyter (type: 0, offset: 100, count: 1000)
eyeq % create_store test 100
usage: create_store <name> <path> <count> [store_type] [filename]
eyeq ! create_store test '' 100
eyeq % list_stores
Listing stores:
* /test (type: 0, offset: 0, count: 100)
* /jupyter (type: 0, offset: 100, count: 1000)
eyeq % export_blocks jupyter 0 100 test.bin
Exporting blocks from '/jupyter' to 'test.bin', offset: 0, count: 100
eyeq % import_blocks test test.bin
eyeq % list_stores
Listing stores:
* /test (type: 0, offset: 0, count: 100)
* /jupyter (type: 0, offset: 100, count: 1000)
eyeq % create_stream 
usage: create_stream <name> <path> <store_name> <store_path> <start_block> <block_count> [relative_freq] [filter_type]
eyeq ! create_stream t '' test '' 0 100
eyeq % list_streams
Listing streams:
* /t
eyeq % read_stream
usage: read_stream <name> <sample_count> <filename> [path]
eyeq ! read_stream t 1000000 test.bin
Exporting samples from stream '/t' to 'test.bin', count: 1000000
Transfer was aborted
Wrote 812800 samples to 'test.bin'
eyeq ! close_stream t
eyeq ! delete_store test
eyeq % list_stores
Listing stores:
* /jupyter (type: 0, offset: 100, count: 1000)
eyeq % list_streams
Listing streams:
# Plot samples
../tools/plot_samples.py test.bin

```

### Script for importing test samples

```
create_store test '' 1000
import_samples test ../demod/python/pydemod/tests/input_samples_11_signals.dat float int16 4096
```

## TODO

* Import samples from raw sample files (e.g. np.complex64)
