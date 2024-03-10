extern void wasmbin_read_global_addresses(FILE *file, size_t *stack, size_t *heap, size_t *data_end);
extern void wasmbin_read_memory_size(FILE *file, uint32_t *base_pages, uint32_t *max_pages);
