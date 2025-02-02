#ifndef INPUT_STREAM_H
#define INPUT_STREAM_H

#include "wasm.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#define assert(x) { if (x==0) panic(self, "%s():%d, %s not true", __func__, __LINE__, #x); }	// WAHE edit

static void InputStream_open(struct InputStream *self, const char *path) {
    self->stream = fopen(path, "rb");
    if (self->stream == NULL) panic(self, "unable to open input file");
}

static void InputStream_close(struct InputStream *self) {
    fclose(self->stream);
}

static bool InputStream_atEnd(struct InputStream *self) {
    return feof(self->stream) != 0;
}

static uint8_t InputStream_readByte(struct InputStream *self) {
    int value;
    value = fgetc(self->stream);
    //+ WAHE edit
    if (value == EOF)
    {
	    fprintf(stderr, "At file pos %lx: ", ftell(self->stream));
	    panic(self, "unexpected end of input stream");
    }
    //- WAHE edit
    return value;
}

static uint32_t InputStream_readLittle_u32(struct InputStream *self) {
    uint32_t value = 0;
    value |= (uint32_t)InputStream_readByte(self) << 0;
    value |= (uint32_t)InputStream_readByte(self) << 8;
    value |= (uint32_t)InputStream_readByte(self) << 16;
    value |= (uint32_t)InputStream_readByte(self) << 24;
    return value;
}

static uint64_t InputStream_readLittle_u64(struct InputStream *self) {
    uint64_t value = 0;
    value |= (uint64_t)InputStream_readByte(self) << 0;
    value |= (uint64_t)InputStream_readByte(self) << 8;
    value |= (uint64_t)InputStream_readByte(self) << 16;
    value |= (uint64_t)InputStream_readByte(self) << 24;
    value |= (uint64_t)InputStream_readByte(self) << 32;
    value |= (uint64_t)InputStream_readByte(self) << 40;
    value |= (uint64_t)InputStream_readByte(self) << 48;
    value |= (uint64_t)InputStream_readByte(self) << 56;
    return value;
}

static float InputStream_readLittle_f32(struct InputStream *self) {
    uint32_t value = InputStream_readLittle_u32(self);
    float result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static double InputStream_readLittle_f64(struct InputStream *self) {
    uint64_t value = InputStream_readLittle_u64(self);
    double result;
    memcpy(&result, &value, sizeof(result));
    return result;
}

static uint32_t InputStream_readLeb128_u32(struct InputStream *self) {
    uint32_t value = 0;
    uint8_t shift = 0;
    uint8_t byte;
    do {
        byte = InputStream_readByte(self);
        assert(shift < 64);	// WAHE edit
        value |= (uint32_t)(byte & 0x7F) << shift;
        shift += 7;
    } while (byte & 0x80);
    return value;
}

static int32_t InputStream_readLeb128_i32(struct InputStream *self) {
    uint32_t value = 0;
    uint8_t shift = 0;
    uint8_t byte;
    do {
        byte = InputStream_readByte(self);
        assert(shift < 64);
        value |= (uint32_t)(byte & 0x7F) << shift;
        shift += 7;
    } while (byte & 0x80);
    if (shift < 32) {
        uint32_t mask = -((uint32_t)1 << shift);
        if (byte & 0x40) value |= mask; else value &= ~mask;
    }
    return (int32_t)value;
}

static int64_t InputStream_readLeb128_u64(struct InputStream *self) {
    uint64_t value = 0;
    uint8_t shift = 0;
    uint8_t byte;
    do {
        byte = InputStream_readByte(self);
        assert(shift < 64);
        value |= (uint64_t)(byte & 0x7F) << shift;
        shift += 7;
    } while (byte & 0x80);
    return value;
}

static int64_t InputStream_readLeb128_i64(struct InputStream *self) {
    uint64_t value = 0;
    uint8_t shift = 0;
    uint8_t byte;
    do {
        byte = InputStream_readByte(self);
        assert(shift < 64);
        value |= (uint64_t)(byte & 0x7F) << shift;
        shift += 7;
    } while (byte & 0x80);
    if (shift < 64) {
        uint64_t mask = -((uint64_t)1 << shift);
        if (byte & 0x40) value |= mask; else value &= ~mask;
    }
    return (int64_t)value;
}

static char *InputStream_readName(struct InputStream *self) {
    uint32_t len = InputStream_readLeb128_u32(self);
    char *name = malloc(len + 1);
    if (name == NULL) panic(self, "out of memory");
    if (fread(name, 1, len, self->stream) != len) panic(self, "unexpected end of input stream");
    name[len] = 0;
    return name;
}

static void InputStream_skipBytes(struct InputStream *self, size_t len) {
    if (fseek(self->stream, len, SEEK_CUR) == -1) panic(self, "unexpected end of input stream");
}

static uint32_t InputStream_skipToSection(struct InputStream *self, uint8_t expected_id) {
    fseek(self->stream, 0, SEEK_END);		// WAHE edit
    long int file_size = ftell(self->stream);	// WAHE edit
    fseek(self->stream, 8, SEEK_SET);		// WAHE edit, return to byte 8
    while (true) {
        uint8_t id = InputStream_readByte(self);
        uint32_t size = InputStream_readLeb128_u32(self);
        if (id == expected_id) return size;
        InputStream_skipBytes(self, size);
	if (ftell(self->stream) == file_size)	return 0;	// WAHE edit
    }
}

//+ WAHE
static uint32_t InputStream_skipToCustomSection(struct InputStream *self, const char *section_name)
{
	// Get size and rewind
	fseek(self->stream, 0, SEEK_END);
	long int file_size = ftell(self->stream);
	fseek(self->stream, 8, SEEK_SET);

	while (1)
	{
		int i;
		uint8_t id = InputStream_readByte(self);
		uint32_t size = InputStream_readLeb128_u32(self);
		long int section_start = ftell(self->stream);
		if (id == WasmSectionId_custom)
		{
			// Check name
			uint32_t len = InputStream_readLeb128_u32(self); // name length
			uint32_t len2 = strlen(section_name);
			if (len == len2)
			{
				for (i=0; i < len; i++)
					if (section_name[i] != InputStream_readByte(self))
						break;

				fseek(self->stream, section_start, SEEK_SET);
				if (i == len)
					return size;
			}

			fseek(self->stream, section_start, SEEK_SET);
		}

		InputStream_skipBytes(self, size);

		if (ftell(self->stream) == file_size)
			return 0;
	}
}
//- WAHE

struct ResultType {
    uint32_t len;
    int8_t types[1];
};
static struct ResultType *InputStream_readResultType(struct InputStream *self) {
    uint32_t len = InputStream_readLeb128_u32(self);
    struct ResultType *result_type = malloc(offsetof(struct ResultType, types) + sizeof(int8_t) * len);
    if (result_type == NULL) panic(self, "out of memory");
    result_type->len = len;
    for (uint32_t i = 0; i < len; i += 1) {
        int64_t val_type = InputStream_readLeb128_i64(self);
        switch (val_type) {
            case WasmValType_i32: case WasmValType_i64:
            case WasmValType_f32: case WasmValType_f64:
                break;

            default: panic(self, "unsupported val_type %lld", val_type);	// WAHE edit
        }
        result_type->types[i] = val_type;
    }
    return result_type;
}

struct Limits {
    uint64_t min, max;	// WAHE edit
    int bits;	// WAHE edit
};
static struct Limits InputStream_readLimits(struct InputStream *self) {
    struct Limits limits;
    uint8_t kind = InputStream_readByte(self);
    limits.min = InputStream_readLeb128_u64(self);	// WAHE edit
    limits.bits = kind < 0x04 ? 32 : 64;		// WAHE edit
    switch (kind) {
        case 0x00: limits.max = UINT32_MAX; break;
        case 0x01: limits.max = InputStream_readLeb128_u32(self); break;
	//+ WAHE edit
        case 0x04: limits.max = UINT64_MAX; break;
        case 0x05: limits.max = InputStream_readLeb128_u64(self); break;
        default: panic(self, "unsupported limit kind %d", kind);
	//- WAHE edit
    }
    return limits;
}

#endif /* INPUT_STREAM_H */
