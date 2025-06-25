extern int check_if_file_is_wasm(const char *path);
extern size_t wasmbin_read_stack_pointer(FILE *file);
extern void wasmbin_read_memory_size(FILE *file, uint32_t *base_pages, uint32_t *max_pages);
