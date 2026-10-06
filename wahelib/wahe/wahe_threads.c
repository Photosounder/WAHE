#ifdef WAHE_WASMTIME
static void wahe_delete_thread_runner(wahe_module_t *ctx, wahe_wasmtime_runner_t *runner)
{
	// Free guest allocations on the calling runner after the worker has stopped
	call_module_free(ctx, runner->input_addr);
	call_module_free(ctx, runner->tls_alloc);
	call_module_free(ctx, runner->stack_alloc);

	// Remove the stable object before destroying its store and host resources
	rl_mutex_lock(&ctx->mutex);
	ctx->runner[runner->runner_id] = NULL;
	rl_mutex_unlock(&ctx->mutex);
	if (runner->linker)
		wasmtime_linker_delete(runner->linker);
	if (runner->store)
		wasmtime_store_delete(runner->store);
	rl_mutex_destroy(&runner->mutex);
	free(runner->thread_reply);
	free(runner);
}

static int wahe_init_thread_exports(wahe_module_t *ctx, wahe_wasmtime_runner_t *runner)
{
	// Resolve only the new instance's handles without touching running stores
	for (int i = WAHE_FUNC_MALLOC; i < WAHE_FUNC_COUNT; i++)
	{
		// Match the standard exported callback names to their store-specific handles
		char name[64];
		snprintf(name, sizeof(name), "module_%s", i == WAHE_FUNC_INPUT ? "message_input" : wahe_func_name[i]);
		wasmtime_extern_t item;
		if (wasmtime_linker_get(runner->linker, runner->context, "", 0, name, strlen(name), &item) &&
			item.kind == WASMTIME_EXTERN_FUNC)
			runner->func[i] = item.of.func;
	}

	// Require the normal address-to-address text callback ABI
	if (!runner->func[WAHE_FUNC_THREAD_ENTRY].store_id)
		return 0;
	wasm_functype_t *type = wasmtime_func_type(runner->context, &runner->func[WAHE_FUNC_THREAD_ENTRY]);
	const wasm_valtype_vec_t *params = wasm_functype_params(type), *results = wasm_functype_results(type);
	wasm_valkind_t address_kind = ctx->address_type == WASMTIME_I32 ? WASM_I32 : WASM_I64;
	int valid = params->size == 1 && results->size == 1 &&
		wasm_valtype_kind(params->data[0]) == address_kind && wasm_valtype_kind(results->data[0]) == address_kind;
	wasm_functype_delete(type);
	return valid;
}

static int wahe_init_thread_tls(wahe_module_t *ctx, wahe_wasmtime_runner_t *runner)
{
	// Require LLVM's TLS metadata so hidden thread-local state cannot go undetected
	wasmtime_extern_t size_export, align_export, init_export;
	if (!wasmtime_linker_get(runner->linker, runner->context, "", 0, "__tls_size", 10, &size_export))
		return 0;
	if (size_export.kind != WASMTIME_EXTERN_GLOBAL)
		return 0;
	wasmtime_val_t value;
	wasmtime_global_get(runner->context, &size_export.of.global, &value);
	size_t size = wasmtime_val_get_address(value);
	if (!size)
		return 1;

	// Allocate an independently aligned TLS block on the module heap
	if (!wasmtime_linker_get(runner->linker, runner->context, "", 0, "__tls_align", 11, &align_export) ||
		align_export.kind != WASMTIME_EXTERN_GLOBAL ||
		!wasmtime_linker_get(runner->linker, runner->context, "", 0, "__wasm_init_tls", 15, &init_export) ||
		init_export.kind != WASMTIME_EXTERN_FUNC)
		return 0;
	wasmtime_global_get(runner->context, &align_export.of.global, &value);
	size_t align = wasmtime_val_get_address(value);
	size_t address_max = ctx->address_type == WASMTIME_I32 ? UINT32_MAX : SIZE_MAX;
	if (!align || (align & (align-1)) || size > address_max - (align-1))
		return 0;
	runner->tls_alloc = call_module_malloc(ctx, size + align-1);
	if (!runner->tls_alloc)
		return 0;
	size_t tls_addr = (runner->tls_alloc + align-1) & ~(align-1);

	// Initialize the private TLS base before entering any C callback on this runner
	wasmtime_val_t argument = wasmtime_val_set_address(ctx, tls_addr);
	wasm_trap_t *trap = NULL;
	wasmtime_error_t *error = wasmtime_func_call(runner->context, &init_export.of.func, &argument, 1, NULL, 0, &trap);
	if (error || trap)
	{
		// Preserve diagnostics for an invalid TLS initializer
		fprint_wasmtime_error(ctx, runner, error, trap);
		return 0;
	}
	return 1;
}

#ifdef _WIN32
static unsigned __stdcall wahe_thread_main(void *argument)
#else
static void *wahe_thread_main(void *argument)
#endif
{
	// Enter the dedicated runner without borrowing a chain's mutable execution state
	wahe_wasmtime_runner_t *runner = argument;
	wahe_cur_wasmtime_runner = runner;
	wahe_cur_chain = NULL;
	char *reply = call_module_func_on_runner(runner->module, runner->runner_id, runner->input_addr, WAHE_FUNC_THREAD_ENTRY, 0);
	if (reply)
		runner->thread_reply = make_string_copy(reply);
	wahe_cur_wasmtime_runner = NULL;
	return 0;
}

static size_t wahe_create_module_thread(wahe_module_t *ctx, size_t stack_size, const char *message, const char **failure)
{
	// Accept only instances whose memory can safely be shared by additional runners
	*failure = "requires imported shared memory with passive data segments";
	if (!ctx->valid || ctx->type != WAHE_MODULE_WASMTIME || !ctx->thread_memory_safe)
		return 0;
	*failure = "stack size must be positive and representable";
	size_t address_max = ctx->address_type == WASMTIME_I32 ? UINT32_MAX : SIZE_MAX;
	if (!stack_size || stack_size > address_max - 30)
		return 0;
	stack_size = (stack_size + 15) & ~(size_t) 15;

	// Serialize runner publication and creation against joins and shutdown
	rl_mutex_lock(&ctx->mutex);
	*failure = "module is shutting down";
	if (ctx->threads_stopping)
	{
		// Leave the registry unchanged once shutdown has started
		rl_mutex_unlock(&ctx->mutex);
		return 0;
	}
	*failure = "thread identifiers exhausted";
	if (!ctx->next_thread_id)
	{
		// Never recycle a public thread ID into a stale handle
		rl_mutex_unlock(&ctx->mutex);
		return 0;
	}

	// Reuse empty runner slots or extend the pointer array without moving live runners
	size_t slot = ctx->initial_runner_count;
	while (slot < ctx->runner_count && ctx->runner[slot])
		slot++;
	*failure = "host allocation failed";
	if (slot == ctx->runner_count)
	{
		// Grow only the registry of pointers while every store keeps its original identity
		if (slot == SIZE_MAX / sizeof(*ctx->runner))
		{
			rl_mutex_unlock(&ctx->mutex);
			return 0;
		}
		wahe_wasmtime_runner_t **array = realloc(ctx->runner, (slot+1) * sizeof(*array));
		if (!array)
		{
			rl_mutex_unlock(&ctx->mutex);
			return 0;
		}
		ctx->runner = array;
		ctx->runner[slot] = NULL;
		ctx->runner_count++;
	}
	wahe_wasmtime_runner_t *runner = calloc(1, sizeof(*runner));
	if (!runner)
	{
		rl_mutex_unlock(&ctx->mutex);
		return 0;
	}
	ctx->runner[slot] = runner;
	runner->runner_id = slot;
	runner->thread_id = ctx->next_thread_id++;
	runner->creating = 1;
	rl_mutex_init(&runner->mutex);
	rl_mutex_unlock(&ctx->mutex);

	// Allocate only the usable stack size when the module allocator provides alignment
	*failure = "module stack allocation failed";
	size_t stack_alloc = call_module_malloc(ctx, stack_size);
	if (stack_alloc && (stack_alloc & 15))
	{
		// Retry with padding only when the first allocation cannot be used directly
		call_module_free(ctx, stack_alloc);
		stack_alloc = call_module_malloc(ctx, stack_size + 15);
	}
	runner->stack_alloc = stack_alloc;
	runner->stack_size = stack_size;
	if (stack_alloc)
	{
		// Keep the allocation base for freeing while aligning the stack's usable start
		runner->stack_start = (stack_alloc + 15) & ~(size_t) 15;
		size_t memory_size = wahe_get_module_memory_size(ctx);
		if (runner->stack_start > memory_size || stack_size > memory_size - runner->stack_start ||
			runner->stack_start > address_max || stack_size > address_max - runner->stack_start)
			stack_alloc = 0;
		else
			memset(ctx->memory_ptr + runner->stack_start, WAHE_STACK_BLANK_PATTERN, stack_size);
	}

	// Initialize the independent store, stack and TLS without rerunning reactor constructors
	int initialized = 0;
	if (stack_alloc)
	{
		// Initialize the runner lock even when later instantiation fails
		*failure = "runner initialization failed";
		initialized = wahe_init_wasmtime_runner(ctx, runner);
	}
	if (initialized)
	{
		// Resolve the worker callback and LLVM TLS before copying the entry message
		*failure = "module_thread_entry must have the usual text callback signature";
		initialized = wahe_init_thread_exports(ctx, runner);
		if (initialized)
		{
			*failure = "TLS initialization failed; export __tls_size, __tls_align and __wasm_init_tls";
			initialized = wahe_init_thread_tls(ctx, runner);
		}
		if (initialized)
		{
			// Copy the opaque input into shared module memory owned until join
			*failure = "entry message allocation failed";
			size_t length = strlen(message) + 1;
			runner->input_addr = call_module_malloc(ctx, length);
			initialized = runner->input_addr != 0;
			if (initialized)
				memcpy(ctx->memory_ptr + runner->input_addr, message, length);
		}
	}

	// Start one OS thread after its full guest context is ready
	if (initialized)
	{
		*failure = "OS thread creation failed";
		#ifdef _WIN32
		runner->thread_handle = (void *) _beginthreadex(NULL, 0, wahe_thread_main, runner, 0, NULL);
		initialized = runner->thread_handle != NULL;
		#else
		pthread_t *handle = malloc(sizeof(*handle));
		initialized = handle && pthread_create(handle, NULL, wahe_thread_main, runner) == 0;
		if (initialized)
			runner->thread_handle = handle;
		else
			free(handle);
		#endif
	}
	size_t id = initialized ? runner->thread_id : 0;
	if (!initialized)
		wahe_delete_thread_runner(ctx, runner);
	else
	{
		// Publish the join handle only after the OS thread has been created successfully
		rl_mutex_lock(&ctx->mutex);
		runner->creating = 0;
		rl_mutex_unlock(&ctx->mutex);
	}
	return id;
}

static int wahe_join_module_thread(wahe_module_t *ctx, size_t id, char **reply, const char **failure)
{
	// Claim the thread once without holding a module lock during its execution
	*failure = "unknown thread ID or thread already being joined";
	*reply = NULL;
	rl_mutex_lock(&ctx->mutex);
	wahe_wasmtime_runner_t *runner = NULL;
	for (size_t i = ctx->initial_runner_count; i < ctx->runner_count; i++)
		if (ctx->runner[i] && ctx->runner[i]->thread_id == id && !ctx->runner[i]->creating && !ctx->runner[i]->joining)
			runner = ctx->runner[i];
	if (runner == wahe_cur_wasmtime_runner && runner)
	{
		// Reject self-joins rather than waiting forever
		*failure = "a thread cannot join itself";
		runner = NULL;
	}
	if (runner)
		runner->joining = 1;
	rl_mutex_unlock(&ctx->mutex);
	if (!runner)
		return 0;

	// Wait for completion and release the platform's joinable thread handle
	#ifdef _WIN32
	int joined = WaitForSingleObject(runner->thread_handle, INFINITE) == WAIT_OBJECT_0;
	if (joined)
		CloseHandle(runner->thread_handle);
	#else
	int joined = pthread_join(*(pthread_t *) runner->thread_handle, NULL) == 0;
	if (joined)
		free(runner->thread_handle);
	#endif
	if (!joined)
	{
		// Leave the handle joinable after an OS-level wait failure
		rl_mutex_lock(&ctx->mutex);
		runner->joining = 0;
		rl_mutex_unlock(&ctx->mutex);
		*failure = "OS thread join failed";
		return 0;
	}

	// Transfer the copied result before freeing the worker's stack on a different runner
	*reply = runner->thread_reply;
	runner->thread_reply = NULL;
	wahe_delete_thread_runner(ctx, runner);
	return 1;
}
#endif

void wahe_module_join_threads(wahe_module_t *ctx)
{
	#ifdef WAHE_WASMTIME
	// Prevent new workers before waiting for all existing workers to finish cooperatively
	if (!ctx || ctx->type != WAHE_MODULE_WASMTIME)
		return;
	if (wahe_cur_wasmtime_runner && wahe_cur_wasmtime_runner->module == ctx && wahe_cur_wasmtime_runner->thread_id)
	{
		// Keep worker code from using the host shutdown path to wait on itself
		fprintf_rl(stderr, "Cannot shut down threads of module %s from one of its workers\n", ctx->module_name);
		return;
	}
	rl_mutex_lock(&ctx->mutex);
	ctx->threads_stopping = 1;
	int notify = 0;
	for (size_t i = ctx->initial_runner_count; i < ctx->runner_count; i++)
		if (ctx->runner[i])
			notify = 1;
	notify = notify && ctx->runner[0]->func[WAHE_FUNC_INPUT].store_id != 0;
	rl_mutex_unlock(&ctx->mutex);

	// Let continuous workers receive a stop request before waiting for their exit
	if (notify)
		wahe_send_input(ctx, "Stop threads");

	for (;;)
	{
		// Snapshot an unclaimed worker while respecting concurrent joins
		size_t id = 0, remaining = 0;
		rl_mutex_lock(&ctx->mutex);
		for (size_t i = ctx->initial_runner_count; i < ctx->runner_count; i++)
			if (ctx->runner[i])
			{
				remaining++;
				if (!ctx->runner[i]->creating && !ctx->runner[i]->joining)
					id = ctx->runner[i]->thread_id;
			}
		rl_mutex_unlock(&ctx->mutex);
		if (!remaining)
			return;
		if (id)
		{
			// Release each completed worker and discard its optional result during shutdown
			char *reply = NULL;
			const char *failure;
			if (wahe_join_module_thread(ctx, id, &reply, &failure))
				free(reply);
		}
		else
		{
			// Let in-flight joins finish without spinning on the module registry
			#ifdef _WIN32
			Sleep(1);
			#else
			usleep(1000);
			#endif
		}
	}
	#else
	(void) ctx;
	#endif
}
