typedef struct
{
	uint8_t *buf;
	size_t as, len;		// len for strings excludes the nul end character
	size_t read_pos;	// only used by fread_buffer()
	char *write_dest;	// only used by fopen_buffer() and fclose_buffer()
} buffer_t;

extern char *vbufprintf(buffer_t *s, const char *format, va_list args);
extern char *bufprintf(buffer_t *s, const char *format, ...);
extern void buf_alloc_enough(buffer_t *s, size_t req_size);
extern void free_buf(buffer_t *s);
extern void clear_buf(buffer_t *s);
extern buffer_t buf_load_raw_file(const char *path);
