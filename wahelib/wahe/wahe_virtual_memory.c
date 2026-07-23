static size_t wahe_virtual_memory_page_size(void)
{
	#ifdef _WIN32
	// Read the operating system's commitment granularity
	SYSTEM_INFO system_info;
	GetSystemInfo(&system_info);
	return system_info.dwPageSize;
	#else
	// Read the operating system's commitment granularity
	long page_size = sysconf(_SC_PAGESIZE);
	return page_size > 0 ? (size_t) page_size : 0;
	#endif
}

static int wahe_align_size_to_page(size_t size, size_t page_size, size_t *aligned_size)
{
	// Reject invalid page sizes and alignment overflow
	if (page_size == 0 || size > SIZE_MAX - (page_size - 1))
		return 0;

	// Round the size up to the next page boundary
	*aligned_size = ((size + page_size - 1) / page_size) * page_size;
	return 1;
}

int wahe_virtual_memory_commit(void *memory, size_t commit_size)
{
	// Treat an empty commit as successful
	if (commit_size == 0)
		return 1;

	// Reject commits without a reserved base address
	if (memory == NULL)
		return 0;

	#ifdef _WIN32
	// Commit the requested prefix with read and write access
	return VirtualAlloc(memory, commit_size, MEM_COMMIT, PAGE_READWRITE) == memory;
	#else
	// Commit the requested prefix by making its reserved pages accessible
	return mprotect(memory, commit_size, PROT_READ | PROT_WRITE) == 0;
	#endif
}

int wahe_virtual_memory_decommit(void *memory, size_t commit_size, size_t previous_commit_size)
{
	// Reject invalid shrinking ranges
	if (commit_size > previous_commit_size || memory == NULL)
		return 0;
	if (commit_size == previous_commit_size)
		return 1;

	// Find the complete trailing pages that are no longer needed
	size_t page_size = wahe_virtual_memory_page_size();
	size_t decommit_start, decommit_end;
	if (!wahe_align_size_to_page(commit_size, page_size, &decommit_start) || !wahe_align_size_to_page(previous_commit_size, page_size, &decommit_end))
		return 0;
	if (decommit_start == decommit_end)
		return 1;

	uint8_t *decommit_address = &((uint8_t *) memory)[decommit_start];
	size_t decommit_size = decommit_end - decommit_start;

	#ifdef _WIN32
	// Decommit the unused Windows pages while retaining their address range
	return VirtualFree(decommit_address, decommit_size, MEM_DECOMMIT) != 0;
	#else
	// Discard the unused POSIX pages before making them inaccessible
	if (madvise(decommit_address, decommit_size, MADV_DONTNEED) != 0)
		return 0;
	return mprotect(decommit_address, decommit_size, PROT_NONE) == 0;
	#endif
}

void *wahe_virtual_memory_alloc(size_t commit_size, size_t reserve_size)
{
	// Reject empty or inconsistent ranges
	if (reserve_size == 0 || commit_size > reserve_size)
		return NULL;

	#ifdef _WIN32
	// Reserve a fixed address range without committing its pages
	void *memory = VirtualAlloc(NULL, reserve_size, MEM_RESERVE, PAGE_NOACCESS);
	#else
	// Reserve an inaccessible fixed address range
	void *memory = mmap(NULL, reserve_size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (memory == MAP_FAILED)
		memory = NULL;
	#endif

	// Commit the initially requested prefix
	if (memory && !wahe_virtual_memory_commit(memory, commit_size))
	{
		wahe_virtual_memory_free(memory, reserve_size);
		return NULL;
	}

	return memory;
}

int wahe_virtual_memory_free(void *memory, size_t reserve_size)
{
	// Treat an empty allocation as already released
	if (memory == NULL)
		return 1;

	#ifdef _WIN32
	// Release the complete Windows reservation
	(void) reserve_size;
	return VirtualFree(memory, 0, MEM_RELEASE) != 0;
	#else
	// Release the complete POSIX reservation
	if (reserve_size == 0)
		return 0;
	return munmap(memory, reserve_size) == 0;
	#endif
}
