typedef struct
{
	uint32_t base_pages;
	uint32_t max_pages;
	int maximum_present;
	int shared;
	int imported;
	int memory64;
} wasmbin_memory_info_t;

extern int check_if_file_is_wasm(const char *path);
extern size_t wasmbin_read_stack_pointer(FILE *file);
extern int wasmbin_read_memory_info(FILE *file, wasmbin_memory_info_t *info);
extern void wasmbin_read_memory_size(FILE *file, uint32_t *base_pages, uint32_t *max_pages);
