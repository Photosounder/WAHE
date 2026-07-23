extern int wahe_virtual_memory_commit(void *memory, size_t commit_size);
extern int wahe_virtual_memory_decommit(void *memory, size_t commit_size, size_t previous_commit_size);
extern void *wahe_virtual_memory_alloc(size_t commit_size, size_t reserve_size);
extern int wahe_virtual_memory_free(void *memory, size_t reserve_size);
