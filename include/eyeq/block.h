#pragma once

#include <stdint.h>

#define BLOCK_LENGTH 16384
#define BLOCK_HEADER_LENGTH 128

#define SAMPLE_DATA_PER_BLOCK (BLOCK_LENGTH - BLOCK_HEADER_LENGTH)
#define BLOCK_I16_SAMPLES (SAMPLE_DATA_PER_BLOCK / sizeof(int16_t))
#define BLOCK_I8_SAMPLES (SAMPLE_DATA_PER_BLOCK / sizeof(int8_t))
#define BLOCK_F32_SAMPLES (SAMPLE_DATA_PER_BLOCK / sizeof(float))
#define BLOCK_F64_SAMPLES (SAMPLE_DATA_PER_BLOCK / sizeof(double))

#define BLOCK_CU32_SAMPLES BLOCK_F32_SAMPLES
#define BLOCK_CU64_SAMPLES BLOCK_F64_SAMPLES

#define BLOCK_TYPE_BYTES       0
#define BLOCK_TYPE_I8_SAMPLES  1
#define BLOCK_TYPE_I16_SAMPLES 2
#define BLOCK_TYPE_I32_SAMPLES 3
#define BLOCK_TYPE_F32_SAMPLES 4
#define BLOCK_TYPE_F64_SAMPLES 5

// struct.pack("<I", 0x51657945) => b'EyeQ'
#define BLOCK_MAGIC 0x51657945

struct cu32_sample {
    uint16_t i;
    uint16_t q;
};

struct cu64_sample {
    uint32_t i;
    uint32_t q;
};

typedef struct block_s {
    union {
    	// Headers can be read out as a byte array or as a structured header.
        uint8_t hdr_bytes[BLOCK_HEADER_LENGTH];
        struct {
            // Block magic is 0x51657945; struct.pack("<I", 0x51657945) => b'EyeQ'
            uint32_t block_magic;

        	// Block ID is the ID of the current block
            uint32_t block_id;

            // Block type (any of the above BLOCK_TYPE_x defines)
            uint16_t block_type;

            // Block length in bytes
            uint16_t block_length;

            // Every block has an embedded CRC32 for integrity checking
            uint32_t crc32;

            // Unix epoch, second part, for the first data entry in the block
            uint32_t timestamp_sec;
            // Nanosecond part
            uint32_t timestamp_nsec;

            // What was the source id of this block? API consumer defined
            uint16_t source_id;

            // This union is different per block type. All sample block types use the sample_block_header.
            union {
                struct {
                	// Sample rate is the number of samples per second.
                	// Together with the timestamp, each sample can be accurately timestamped.
                    uint32_t sample_rate;

                    // 1 = I only, 2 = IQ, 6 = Surround sound?
                    uint8_t num_channels;

                    // Which scaling factor should be applied to this data?
                    float scale;

                    // For IQ data, this marks the (approximate) center frequency in Hz.
                    float center_frequency;
                } sample_block_header;
            };
        } hdr;
    };
    union {
    	// Raw bytes of the block
        uint8_t bytes[SAMPLE_DATA_PER_BLOCK];

        // Values as 8 bit signed integers
        int8_t  i8_samples[BLOCK_I8_SAMPLES];

        // Values as 16 bit signed integers
        int16_t i16_samples[BLOCK_I16_SAMPLES];

        // Values as 16 bit signed integers
        int32_t i32_samples[BLOCK_F32_SAMPLES];

        // Values as 32-bit floating-point values (single precision)
        float   f32_samples[BLOCK_F32_SAMPLES];

        // Values as 64-bit floating-point values (double precision)
        double  f64_samples[BLOCK_F64_SAMPLES];

        // Values as 16-bit interleaved complex samples (I/Q)
        struct cu32_sample cu32_samples[BLOCK_F32_SAMPLES];

        // Values as 32-bit interleaved complex samples (I/Q)
        struct cu64_sample cu64_samples[BLOCK_F64_SAMPLES];
    } data;
} __attribute__((packed)) block_t;
