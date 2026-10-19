#ifdef WAHE_WAMR

#ifndef WAHE_WAMR_MAX_THREADS
	#define WAHE_WAMR_MAX_THREADS 64
#endif

#ifndef WAHE_WAMR_NATIVE_STACK_SIZE
	#define WAHE_WAMR_NATIVE_STACK_SIZE (64 * 1024)
#endif

#ifndef WAHE_WAMR_APP_HEAP_SIZE
	#define WAHE_WAMR_APP_HEAP_SIZE 0
#endif

#ifndef WAHE_WAMR_MEMORY64_THREADS
	#define WAHE_WAMR_MEMORY64_THREADS 0
#endif

static _Thread_local wahe_wamr_runner_t *wahe_cur_wamr_runner = NULL;

static int wahe_wamr_runtime_init(void)
{
	static int attempted = 0, initialized = 0;

	// Return the process-wide runtime result after the first attempt
	if (attempted)
		return initialized;
	attempted = 1;

	// Initialize WAMR with system allocation and room for host-managed runners
	RuntimeInitArgs init_args = {0};
	init_args.mem_alloc_type = Alloc_With_System_Allocator;
	init_args.max_thread_num = WAHE_WAMR_MAX_THREADS;
	if (!wasm_runtime_full_init(&init_args))
	{
		fprintf_rl(stderr, "Initializing the WAMR runtime failed\n");
		return 0;
	}

	// Register the untyped raw callback so both wasm32 and wasm64 imports resolve
	static NativeSymbol native_symbols[] =
	{
		{"wahe_run_command", (void *) wahe_run_command_wamr, NULL, NULL}
	};
	if (!wasm_runtime_register_natives_raw("env", native_symbols,
			(uint32_t) (sizeof(native_symbols) / sizeof(*native_symbols))))
	{
		fprintf_rl(stderr, "Registering the WAMR wahe_run_command import failed\n");
		return 0;
	}

	initialized = 1;
	return 1;
}

static wahe_wamr_runner_t *wahe_get_wamr_runner(wahe_module_t *ctx, size_t runner_id, const char *operation)
{
	// Reject runner access outside a WAMR module
	if (ctx == NULL || ctx->type != WAHE_MODULE_WAMR)
	{
		fprintf_rl(stderr, "Cannot %s with a non-WAMR module\n", operation);
		return NULL;
	}

	// Avoid registry locks when a worker accesses its own stable runner object
	if (wahe_cur_wamr_runner && wahe_cur_wamr_runner->module == ctx &&
		wahe_cur_wamr_runner->runner_id == runner_id)
		return wahe_cur_wamr_runner;

	// Read stable runner objects under the lock protecting the growable pointer array
	rl_mutex_lock(&ctx->mutex);
	wahe_wamr_runner_t *runner = runner_id < ctx->runner_count && ctx->wamr_runner ? ctx->wamr_runner[runner_id] : NULL;
	if (runner && runner->thread_id && runner != wahe_cur_wamr_runner)
		runner = NULL;
	if (runner == NULL)
		fprintf_rl(stderr, "Cannot %s with runner %zu in module %s, which has %zu runners\n",
			operation, runner_id, ctx->module_name, ctx->runner_count);
	rl_mutex_unlock(&ctx->mutex);
	return runner;
}

static size_t wahe_wamr_active_runner_id(wahe_module_t *ctx)
{
	// Reuse the runner currently executing a callback into this module
	if (ctx && ctx->type == WAHE_MODULE_WAMR && wahe_cur_wamr_runner && wahe_cur_wamr_runner->module == ctx)
		return wahe_cur_wamr_runner->runner_id;

	// Use runner zero for ordinary calls
	return 0;
}

static int wahe_wamr_refresh_memory(wahe_module_t *ctx, int report_growth)
{
	// Reject missing WAMR memory metadata
	if (ctx == NULL || ctx->type != WAHE_MODULE_WAMR || ctx->wamr_memory == NULL)
		return 0;

	// Read the runtime-owned base and active page extent
	uint8_t *memory_ptr = wasm_memory_get_base_address(ctx->wamr_memory);
	uint64_t pages = wasm_memory_get_cur_page_count(ctx->wamr_memory);
	uint64_t bytes_per_page = wasm_memory_get_bytes_per_page(ctx->wamr_memory);
	if (memory_ptr == NULL || bytes_per_page == 0 || pages > SIZE_MAX / bytes_per_page)
	{
		fprintf_rl(stderr, "WAMR returned invalid linear-memory metadata for module %s\n", ctx->module_name);
		return 0;
	}
	size_t memory_size = (size_t) (pages * bytes_per_page);

	// Enforce WAHE's fixed linear-memory address contract
	if (ctx->memory_ptr && ctx->memory_ptr != memory_ptr)
	{
		fprintf_rl(stderr, "WAMR moved module %s memory from %p to %p; use a WAMR build with stable linear-memory reservations\n",
			ctx->module_name, (void *) ctx->memory_ptr, (void *) memory_ptr);
		ctx->valid = 0;
		return 0;
	}

	// Publish and optionally report an enlarged active extent
	rl_mutex_lock(&ctx->mutex);
	if (report_growth && memory_size > ctx->memory_size)
		fprintf_rl(stdout, "Memory of module #%d %s grew from %zu kB to %zu kB\n",
			ctx->module_id, ctx->module_name, ctx->memory_size >> 10, memory_size >> 10);
	ctx->memory_ptr = memory_ptr;
	ctx->memory_size = memory_size;
	rl_mutex_unlock(&ctx->mutex);
	return 1;
}

static size_t wahe_wamr_get_symbol_address(wahe_module_t *ctx, size_t runner_id, const char *symbol_name, int verbosity)
{
	// Select the child instance whose private globals are being queried
	wahe_wamr_runner_t *runner = wahe_get_wamr_runner(ctx, runner_id, "find a module symbol");
	if (runner == NULL)
		return 0;

	// Resolve and validate the exported global
	wasm_global_inst_t global = {0};
	if (!wasm_runtime_get_export_global_inst(runner->module_inst, symbol_name, &global) || global.global_data == NULL)
	{
		if (verbosity == -1)
			fprintf_rl(stderr, "Error in module %s runner %zu: symbol %s was not found as a WAMR global\n",
				ctx->module_name, runner_id, symbol_name);
		return 0;
	}

	// Convert the exported address-width global to a host size
	size_t address = 0;
	if (global.kind == WASM_I32)
		address = *(uint32_t *) global.global_data;
	else if (global.kind == WASM_I64)
		address = (size_t) *(uint64_t *) global.global_data;
	else
	{
		fprintf_rl(stderr, "Error in module %s runner %zu: symbol %s is not an integer global\n",
			ctx->module_name, runner_id, symbol_name);
		return 0;
	}

	// Report successful verbose lookups consistently with other runtimes
	if (verbosity == 1)
		fprintf_rl(stdout, "Module #%d %s runner %zu: symbol %s found\n",
			ctx->module_id, ctx->module_name, runner_id, symbol_name);
	return address;
}

static void *wahe_wamr_get_symbol_data(wahe_module_t *ctx, size_t runner_id, const char *symbol_name)
{
	// Select the WAMR child instance that owns the mutable global storage
	wahe_wamr_runner_t *runner = wahe_get_wamr_runner(ctx, runner_id, "find mutable global storage");
	if (runner == NULL)
		return NULL;

	// Return the runtime-owned storage backing the exported global
	wasm_global_inst_t global = {0};
	if (!wasm_runtime_get_export_global_inst(runner->module_inst, symbol_name, &global))
		return NULL;
	return global.global_data;
}

static int wahe_wamr_get_function(wahe_module_t *ctx, size_t runner_id, const char *func_name,
		enum wahe_func_id func_id, int verbosity)
{
	// Select the runner instance that owns the function handle
	wahe_wamr_runner_t *runner = wahe_get_wamr_runner(ctx, runner_id, "find a module function");
	if (runner == NULL)
		return 0;

	// Resolve the exported function from this child instance
	runner->func[func_id] = wasm_runtime_lookup_function(runner->module_inst, func_name);
	if (runner->func[func_id] == NULL)
	{
		if (verbosity == -1)
			fprintf_rl(stderr, "Error in module %s runner %zu: function %s() was not found by WAMR\n",
				ctx->module_name, runner_id, func_name);
		return 0;
	}

	// Report successful verbose lookups consistently with other runtimes
	if (verbosity == 1)
		fprintf_rl(stdout, "Module #%d %s runner %zu: %s() found\n",
			ctx->module_id, ctx->module_name, runner_id, func_name);
	return 1;
}

static int wahe_wamr_validate_address_function(wahe_module_t *ctx, wahe_wamr_runner_t *runner,
		enum wahe_func_id func_id, uint32_t parameter_count, uint32_t result_count)
{
	// Require the expected number of parameters and results
	wasm_function_inst_t function = runner->func[func_id];
	if (function == NULL || wasm_func_get_param_count(function, runner->module_inst) != parameter_count ||
		wasm_func_get_result_count(function, runner->module_inst) != result_count)
		return 0;

	// Require every value to use the module's address width
	wasm_valkind_t address_kind = ctx->memory_bits == 64 ? WASM_I64 : WASM_I32;
	wasm_valkind_t kinds[2] = {0};
	if (parameter_count)
	{
		wasm_func_get_param_types(function, runner->module_inst, kinds);
		for (uint32_t i = 0; i < parameter_count; i++)
			if (kinds[i] != address_kind)
				return 0;
	}
	if (result_count)
	{
		wasm_func_get_result_types(function, runner->module_inst, kinds);
		if (kinds[0] != address_kind)
			return 0;
	}
	return 1;
}

static int wahe_wamr_call(wahe_module_t *ctx, wahe_wamr_runner_t *runner, enum wahe_func_id func_id,
		const size_t *arguments, int argument_count, size_t *result)
{
	// Initialize WAMR state for host-created threads only for the duration of this call
	int destroy_thread_env = 0;
	if (!wasm_runtime_thread_env_inited())
	{
		if (!wasm_runtime_init_thread_env())
		{
			fprintf_rl(stderr, "Initializing WAMR thread state for module %s runner %zu failed\n",
				ctx->module_name, runner->runner_id);
			return 0;
		}
		destroy_thread_env = 1;
	}

	// Prepare typed arguments and the optional address result
	wasm_val_t params[2] = {0}, results[1] = {0};
	wasm_valkind_t address_kind = ctx->memory_bits == 64 ? WASM_I64 : WASM_I32;
	for (int i = 0; i < argument_count; i++)
	{
		params[i].kind = address_kind;
		if (address_kind == WASM_I32)
			params[i].of.i32 = (int32_t) arguments[i];
		else
			params[i].of.i64 = (int64_t) arguments[i];
	}
	if (func_id != WAHE_FUNC_FREE)
		results[0].kind = address_kind;

	// Serialize calls on one execution environment while leaving other runners concurrent
	wahe_wamr_runner_t *previous_runner = wahe_cur_wamr_runner;
	rl_mutex_lock(&runner->mutex);
	wahe_cur_wamr_runner = runner;
	int called = wasm_runtime_call_wasm_a(runner->exec_env, runner->func[func_id],
		func_id != WAHE_FUNC_FREE, results, (uint32_t) argument_count, params);
	wahe_cur_wamr_runner = previous_runner;
	rl_mutex_unlock(&runner->mutex);
	if (destroy_thread_env)
		wasm_runtime_destroy_thread_env();

	// Report the WAMR exception at the point of failure
	if (!called)
	{
		const char *exception = wasm_runtime_get_exception(runner->module_inst);
		fprintf_rl(stderr, "Calling %s runner %zu:%s() failed in WAMR%s%s\n",
			ctx->module_name, runner->runner_id, wahe_func_name[func_id],
			exception ? ": " : "", exception ? exception : "");
		return 0;
	}

	// Convert the typed result to the common module-address representation
	if (result)
		*result = address_kind == WASM_I32 ? (uint32_t) results[0].of.i32 : (size_t) results[0].of.i64;
	return 1;
}

static int wahe_wamr_init_thread_tls(wahe_module_t *ctx, wahe_wamr_runner_t *runner, int require_metadata)
{
	// Find LLVM's TLS size metadata in this child instance
	wasm_global_inst_t size_global = {0};
	if (!wasm_runtime_get_export_global_inst(runner->module_inst, "__tls_size", &size_global))
		return !require_metadata;
	if (size_global.global_data == NULL || (size_global.kind != WASM_I32 && size_global.kind != WASM_I64))
		return 0;
	size_t size = size_global.kind == WASM_I32 ? *(uint32_t *) size_global.global_data :
		(size_t) *(uint64_t *) size_global.global_data;
	if (!size)
		return 1;

	// Resolve the alignment global and initializer required by nonempty TLS
	wasm_global_inst_t align_global = {0};
	wasm_function_inst_t initializer = wasm_runtime_lookup_function(runner->module_inst, "__wasm_init_tls");
	if (!wasm_runtime_get_export_global_inst(runner->module_inst, "__tls_align", &align_global) ||
		align_global.global_data == NULL || (align_global.kind != WASM_I32 && align_global.kind != WASM_I64) ||
		initializer == NULL)
		return 0;
	size_t align = align_global.kind == WASM_I32 ? *(uint32_t *) align_global.global_data :
		(size_t) *(uint64_t *) align_global.global_data;
	size_t address_max = ctx->memory_bits == 32 ? UINT32_MAX : SIZE_MAX;
	if (!align || (align & (align - 1)) || size > address_max - (align - 1))
		return 0;

	// Require the standard one-address TLS initializer signature
	wasm_valkind_t parameter_type = 0;
	if (wasm_func_get_param_count(initializer, runner->module_inst) != 1 ||
		wasm_func_get_result_count(initializer, runner->module_inst) != 0)
		return 0;
	wasm_func_get_param_types(initializer, runner->module_inst, &parameter_type);
	if (parameter_type != (ctx->memory_bits == 64 ? WASM_I64 : WASM_I32))
		return 0;

	// Allocate an independently aligned TLS block from the module allocator
	runner->tls_alloc = call_module_malloc(ctx, size + align - 1);
	if (!runner->tls_alloc)
		return 0;
	size_t tls_address = (runner->tls_alloc + align - 1) & ~(align - 1);

	// Initialize WAMR state when setup happens on a host-created thread
	int destroy_thread_env = 0;
	if (!wasm_runtime_thread_env_inited())
	{
		if (!wasm_runtime_init_thread_env())
			return 0;
		destroy_thread_env = 1;
	}

	// Initialize the private TLS base before entering a C callback on this runner
	wasm_val_t argument = {0};
	argument.kind = ctx->memory_bits == 64 ? WASM_I64 : WASM_I32;
	if (ctx->memory_bits == 64)
		argument.of.i64 = (int64_t) tls_address;
	else
		argument.of.i32 = (int32_t) tls_address;
	wahe_wamr_runner_t *previous_runner = wahe_cur_wamr_runner;
	rl_mutex_lock(&runner->mutex);
	wahe_cur_wamr_runner = runner;
	int initialized = wasm_runtime_call_wasm_a(runner->exec_env, initializer, 0, NULL, 1, &argument);
	wahe_cur_wamr_runner = previous_runner;
	rl_mutex_unlock(&runner->mutex);
	if (destroy_thread_env)
		wasm_runtime_destroy_thread_env();
	if (!initialized)
	{
		const char *exception = wasm_runtime_get_exception(runner->module_inst);
		fprintf_rl(stderr, "Initializing TLS for WAMR module %s runner %zu failed%s%s\n",
			ctx->module_name, runner->runner_id, exception ? ": " : "", exception ? exception : "");
	}
	return initialized;
}

static uint32_t wahe_wamr_app_heap_size(wahe_module_t *ctx)
{
	// Skip WAMR's private heap for modules that cannot create shared-memory runners
	if (!ctx->thread_memory_safe || !ctx->stack_base ||
		(ctx->memory_bits == 64 && !WAHE_WAMR_MEMORY64_THREADS))
		return 0;

	// Honor an explicit embedding override when one was configured
	if (WAHE_WAMR_APP_HEAP_SIZE)
		return WAHE_WAMR_APP_HEAP_SIZE > UINT32_MAX ? UINT32_MAX : (uint32_t) WAHE_WAMR_APP_HEAP_SIZE;

	// Reserve one linked auxiliary stack per possible child execution environment
	uint64_t desired = (uint64_t) ctx->stack_base * WAHE_WAMR_MAX_THREADS;
	if (desired > UINT32_MAX)
		desired = UINT32_MAX;
	return (uint32_t) desired;
}

static int wahe_wamr_blank_aux_stack(wahe_module_t *ctx, wahe_wamr_runner_t *runner)
{
	// Leave the original runner stack to the common stack-blanking path
	if (runner == NULL || runner->runner_id == 0 || ctx->stack_base == 0)
		return 1;

	// Read the private stack top WAMR assigned to this child instance
	wasm_global_inst_t stack_pointer = {0};
	if (!wasm_runtime_get_export_global_inst(runner->module_inst, "__stack_pointer", &stack_pointer) ||
		stack_pointer.global_data == NULL)
	{
		fprintf_rl(stderr, "Finding the stack pointer for WAMR module %s runner %zu failed\n",
			ctx->module_name, runner->runner_id);
		return 0;
	}
	size_t stack_top = stack_pointer.kind == WASM_I32 ? *(uint32_t *) stack_pointer.global_data :
		stack_pointer.kind == WASM_I64 ? (size_t) *(uint64_t *) stack_pointer.global_data : 0;

	// Validate and fill the auxiliary stack without touching WAMR's heap metadata
	if (!wahe_wamr_refresh_memory(ctx, 0) || stack_top < ctx->stack_base || stack_top > ctx->memory_size)
	{
		fprintf_rl(stderr, "Cannot blank stack of WAMR module %s runner %zu: stack top %#zx is outside its %#zx-byte memory\n",
			ctx->module_name, runner->runner_id, stack_top, ctx->memory_size);
		return 0;
	}
	memset(ctx->memory_ptr + stack_top - ctx->stack_base, WAHE_STACK_BLANK_PATTERN, ctx->stack_base);
	return 1;
}

static void wahe_wamr_cleanup_failed_init(wahe_module_t *ctx, wasm_module_inst_t main_instance)
{
	// Destroy child execution environments before their shared parent instance
	if (ctx->wamr_runner)
		for (size_t i = 1; i < ctx->runner_count; i++)
			if (ctx->wamr_runner[i] && ctx->wamr_runner[i]->exec_env)
				wasm_runtime_destroy_spawned_exec_env(ctx->wamr_runner[i]->exec_env);

	// Destroy the main execution environment before deinstantiating its module
	if (ctx->wamr_runner && ctx->wamr_runner[0] && ctx->wamr_runner[0]->exec_env)
		wasm_runtime_destroy_exec_env(ctx->wamr_runner[0]->exec_env);
	if (main_instance)
		wasm_runtime_deinstantiate(main_instance);

	// Release every runner object whose mutex was initialized
	if (ctx->wamr_runner)
	{
		for (size_t i = 0; i < ctx->runner_count; i++)
			if (ctx->wamr_runner[i])
			{
				rl_mutex_destroy(&ctx->wamr_runner[i]->mutex);
				free(ctx->wamr_runner[i]);
			}
		free(ctx->wamr_runner);
	}
	ctx->wamr_runner = NULL;

	// Unload the module before its caller releases the retained writable image
	if (ctx->wamr_module)
		wasm_runtime_unload(ctx->wamr_module);
	ctx->wamr_module = NULL;
	ctx->wamr_memory = NULL;
	ctx->wamr_module_buffer = NULL;
	ctx->wamr_module_buffer_size = 0;
}

static int wahe_wamr_init_module(wahe_module_t *ctx, uint8_t *wasm_buffer, size_t wasm_size)
{
	// Reject buffers that cannot be represented by WAMR's loader API
	if (wasm_buffer == NULL || wasm_size == 0 || wasm_size > UINT32_MAX)
	{
		fprintf_rl(stderr, "Module %s has a WASM image size WAMR cannot load\n", ctx->module_name);
		return 0;
	}
	if (!wahe_wamr_runtime_init())
		return 0;

	// Load the writable module image and retain it for the WAMR module lifetime
	char error[256] = {0};
	ctx->wamr_module = wasm_runtime_load(wasm_buffer, (uint32_t) wasm_size, error, sizeof(error));
	if (ctx->wamr_module == NULL)
	{
		fprintf_rl(stderr, "Loading WAMR module %s failed: %s\n", ctx->module_name, error);
		return 0;
	}
	ctx->wamr_module_buffer = wasm_buffer;
	ctx->wamr_module_buffer_size = wasm_size;
	wasm_module_inst_t main_instance = NULL;

	// Configure an empty WASI environment with inherited standard streams
	char *arguments[] = {ctx->module_name};
	wasm_runtime_set_wasi_args(ctx->wamr_module, NULL, 0, NULL, 0, NULL, 0, arguments, 1);

	// Size the private space WAMR uses for runtime-owned child stacks
	uint32_t app_heap_size = wahe_wamr_app_heap_size(ctx);
	if (ctx->runner_count > 1 && app_heap_size < ctx->stack_base)
	{
		fprintf_rl(stderr, "Module %s does not provide enough WAMR application heap for a child stack\n", ctx->module_name);
		goto fail;
	}

	// Make the module identity available to imports invoked by startup functions
	struct InstantiationArgs2 *instantiation_args = NULL;
	if (!wasm_runtime_instantiation_args_create(&instantiation_args))
	{
		fprintf_rl(stderr, "Allocating WAMR instantiation arguments for module %s failed\n", ctx->module_name);
		goto fail;
	}
	wasm_runtime_instantiation_args_set_default_stack_size(instantiation_args, WAHE_WAMR_NATIVE_STACK_SIZE);
	wasm_runtime_instantiation_args_set_host_managed_heap_size(instantiation_args, app_heap_size);
	wasm_runtime_instantiation_args_set_custom_data(instantiation_args, ctx);
	main_instance = wasm_runtime_instantiate_ex2(ctx->wamr_module,
		instantiation_args, error, sizeof(error));
	wasm_runtime_instantiation_args_destroy(instantiation_args);
	if (main_instance == NULL)
	{
		fprintf_rl(stderr, "Instantiating WAMR module %s failed: %s\n", ctx->module_name, error);
		goto fail;
	}
	wasm_runtime_set_custom_data(main_instance, ctx);

	// Allocate stable runner objects before any module callback can observe them
	ctx->wamr_runner = calloc(ctx->runner_count, sizeof(*ctx->wamr_runner));
	if (ctx->wamr_runner == NULL)
	{
		fprintf_rl(stderr, "Allocating %zu WAMR runners for module %s failed\n", ctx->runner_count, ctx->module_name);
		goto fail;
	}
	for (size_t i = 0; i < ctx->runner_count; i++)
	{
		ctx->wamr_runner[i] = calloc(1, sizeof(*ctx->wamr_runner[i]));
		if (ctx->wamr_runner[i] == NULL)
		{
			fprintf_rl(stderr, "Allocating WAMR runner %zu for module %s failed\n", i, ctx->module_name);
			goto fail;
		}
		ctx->wamr_runner[i]->module = ctx;
		ctx->wamr_runner[i]->runner_id = i;
		rl_mutex_init(&ctx->wamr_runner[i]->mutex);
	}

	// Create runner zero and let WAMR discover the module's auxiliary stack
	wahe_wamr_runner_t *main_runner = ctx->wamr_runner[0];
	main_runner->module_inst = main_instance;
	main_runner->exec_env = wasm_runtime_create_exec_env(main_instance, WAHE_WAMR_NATIVE_STACK_SIZE);
	if (main_runner->exec_env == NULL)
	{
		fprintf_rl(stderr, "Creating WAMR execution environment for module %s runner 0 failed\n", ctx->module_name);
		goto fail;
	}
	wasm_runtime_set_user_data(main_runner->exec_env, main_runner);

	// Spawn child instances that share memory and receive independent globals and auxiliary stacks
	for (size_t i = 1; i < ctx->runner_count; i++)
	{
		wahe_wamr_runner_t *runner = ctx->wamr_runner[i];
		runner->exec_env = wasm_runtime_spawn_exec_env(main_runner->exec_env);
		if (runner->exec_env == NULL)
		{
			fprintf_rl(stderr, "Spawning WAMR execution environment for module %s runner %zu failed\n", ctx->module_name, i);
			goto fail;
		}
		runner->module_inst = wasm_runtime_get_module_inst(runner->exec_env);
		wasm_runtime_set_custom_data(runner->module_inst, ctx);
		wasm_runtime_set_user_data(runner->exec_env, runner);
	}

	// Cache the shared default memory and its fixed host address
	ctx->wamr_memory = wasm_runtime_get_default_memory(main_instance);
	if (ctx->wamr_memory == NULL || !wahe_wamr_refresh_memory(ctx, 0))
	{
		fprintf_rl(stderr, "Finding WAMR linear memory for module %s failed\n", ctx->module_name);
		goto fail;
	}
	return 1;

fail:
	// Release all WAMR objects before the caller frees the module image
	wahe_wamr_cleanup_failed_init(ctx, main_instance);
	return 0;
}

void wahe_run_command_wamr(wasm_exec_env_t exec_env, uint64_t *args)
{
	// Recover the calling module and any fully initialized runner from WAMR-owned state
	wasm_module_inst_t module_inst = wasm_runtime_get_module_inst(exec_env);
	wahe_module_t *ctx = wasm_runtime_get_custom_data(module_inst);
	wahe_wamr_runner_t *runner = wasm_runtime_get_user_data(exec_env);
	if (ctx == NULL || args == NULL)
	{
		fprintf_rl(stderr, "Malformed WAMR wahe_run_command callback\n");
		if (args)
			args[0] = 0;
		return;
	}

	// Reject a runner identity that belongs to a different module
	if (runner && runner->module != ctx)
	{
		fprintf_rl(stderr, "WAMR callback for module %s has an invalid runner context\n", ctx->module_name);
		args[0] = 0;
		return;
	}

	// Decode and validate the module-relative input string address
	uint64_t address = ctx->memory_bits == 64 ? args[0] : (uint32_t) args[0];
	char *message = NULL;
	if (address)
	{
		if (!wasm_runtime_validate_app_str_addr(module_inst, address))
		{
			fprintf_rl(stderr, "WAMR module %s passed an invalid command string address %#" PRIx64 "\n",
				ctx->module_name, address);
			args[0] = 0;
			return;
		}
		message = wasm_runtime_addr_app_to_native(module_inst, address);
	}

	// Make nested calls use the callback runner once normal initialization has completed
	wahe_wamr_runner_t *previous_runner = wahe_cur_wamr_runner;
	wahe_cur_wamr_runner = runner;
	if (runner)
		wahe_wamr_refresh_memory(ctx, 1);
	size_t return_address = message ? wahe_run_command_core(ctx, message) : 0;
	wahe_cur_wamr_runner = previous_runner;

	// Return the address using the importing module's address width
	args[0] = ctx->memory_bits == 64 ? return_address : (uint32_t) return_address;
}

#endif // WAHE_WAMR
