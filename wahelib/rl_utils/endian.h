#ifndef RL_DONT_ASSUME_LITTLE_ENDIAN
#define ASS_LE
#endif

// Read and write to a file
extern uint8_t fread_byte8(FILE *file);
extern int64_t fread_LEB128(FILE *file, int signed_leb);
extern void fwrite_byte8(FILE *file, uint8_t s);
extern void fwrite_ULEB128(FILE *file, uint64_t v);
extern void fwrite_SLEB128(FILE *file, int64_t v);

// Read from a buffer
extern uint8_t read_byte8(const void *ptr, size_t *index);
extern uint32_t read_LE32(const void *ptr, size_t *index);
extern uint64_t read_LE64(const void *ptr, size_t *index);
