#ifdef _WIN32
	#include <memoryapi.h>
	#include <sysinfoapi.h>
#else
	#include <sys/mman.h>
	#include <unistd.h>
	#ifndef MAP_ANONYMOUS
		#define MAP_ANONYMOUS MAP_ANON
	#endif
#endif

const char *wahe_eo_name[] =
{
	"module_func",
	"image_display",
	"kb_mouse",
	"chain_input_msg"
};

const char *wahe_func_name[] =
{
	"(none)",
	"malloc",
	"realloc",
	"free",

	"input",
	"proc_cmd",
	"draw",
	"proc_image",
	"proc_sound"
};

_Thread_local wahe_chain_t *wahe_cur_chain = NULL;

#ifdef WAHE_WASMTIME
static _Thread_local wahe_wasmtime_runner_t *wahe_cur_wasmtime_runner = NULL;
static wasmtime_val_t wasmtime_val_set_address(wahe_module_t *ctx, size_t address);
static size_t wasmtime_val_get_address(wasmtime_val_t val);
#endif

void wahe_bench_point(const char *label, int depth)
{
	return;
	static double ref_time = 0.;
	static int bench_depth = 0;
	static buffer_t buf = {0};

	// Set ref time if we're starting at the bottom level
	if (bench_depth == 0)
		ref_time = get_time_hr();

	// Decrement depth
	if (depth < 0)
		bench_depth--;

	// Indent printing according to depth
	for (int i=0; i < bench_depth; i++)
		bufprintf(&buf, "\t");
	bufprintf(&buf, "Bench %s: %.3f ms\n", label, 1e3*(get_time_hr() - ref_time));

	// Increment depth
	if (depth > 0)
		bench_depth++;

	// Print buffer to output if it's the end
	if (bench_depth == 0)
	{
		if (get_time_hr() - ref_time > 0.6/60.)
			fprintf_rl(stdout, "%s\n", buf.buf);
		clear_buf(&buf);
	}
}

#ifdef WAHE_WASMTIME
static wahe_wasmtime_runner_t *wahe_get_wasmtime_runner(wahe_module_t *ctx, size_t runner_id, const char *operation)
{
	// Reject runner access outside a Wasmtime module
	if (ctx == NULL || ctx->type != WAHE_MODULE_WASMTIME)
	{
		fprintf_rl(stderr, "Cannot %s with a non-Wasmtime module\n", operation);
		return NULL;
	}

	// Reject runner indexes outside the module's runner array
	if (ctx->runner == NULL || runner_id >= ctx->runner_count)
	{
		fprintf_rl(stderr, "Cannot %s with runner %zu in module %s, which has %zu runners\n", operation, runner_id, ctx->module_name, ctx->runner_count);
		return NULL;
	}

	return &ctx->runner[runner_id];
}

static size_t wahe_active_runner_id(wahe_module_t *ctx)
{
	// Reuse the runner currently executing a callback into this module
	if (ctx && ctx->type == WAHE_MODULE_WASMTIME && wahe_cur_wasmtime_runner && wahe_cur_wasmtime_runner->module == ctx)
		return wahe_cur_wasmtime_runner->runner_id;

	// Use runner zero for ordinary and native calls
	return 0;
}

static void fprint_wasmtime_error(wahe_module_t *ctx, wahe_wasmtime_runner_t *runner, wasmtime_error_t *error, wasm_trap_t *trap)
{
	wasm_byte_vec_t error_message;

	if (error)
	{
		wasmtime_error_message(error, &error_message);
		wasmtime_error_delete(error);
		fprintf_rl(stderr, "wasmtime_error_message(): \"%.*s\"\n", (int) error_message.size, error_message.data);
		wasm_byte_vec_delete(&error_message);
	}

	if (trap)
	{
		wasm_trap_message(trap, &error_message);
		wasm_trap_delete(trap);
		fprintf_rl(stderr, "wasm_trap_message(): \"%.*s\"\n", (int) error_message.size, error_message.data);
		if (runner && runner->stack_pointer.store_id)
		{
			// Report the stack pointer belonging to the runner that trapped
			wasmtime_val_t stack_pointer;
			wasmtime_global_get(runner->context, &runner->stack_pointer, &stack_pointer);
			fprintf_rl(stderr, "Module runner %zu stack pointer %#zx\n", runner->runner_id, wasmtime_val_get_address(stack_pointer));
		}
		wasm_byte_vec_delete(&error_message);
	}
}

static wasmtime_val_t wasmtime_val_set_address(wahe_module_t *ctx, size_t address)
{
	wasmtime_val_t val;

	val.kind = ctx->address_type;

	if (ctx->address_type == WASMTIME_I32)
		val.of.i32 = address;
	else
		val.of.i64 = address;

	return val;
}

static size_t wasmtime_val_get_address(wasmtime_val_t val)
{
	if (val.kind == WASMTIME_I32)
		return val.of.i32;
	else
		return val.of.i64;
}
#endif // WAHE_WASMTIME

static int wahe_init_module_memory(wahe_module_t *ctx)
{
	// Accept host memory as already initialized
	if (ctx == NULL)
		return 1;

	// Read the fixed memory address exported by a wasm-to-native module
	if (ctx->type == WAHE_MODULE_WASM_TO_NATIVE)
	{
		ctx->memory_ptr = *ctx->native_memory;
		if (ctx->memory_ptr == NULL)
		{
			fprintf_rl(stderr, "Wasm-to-native module %s did not initialise its exported memory pointer\n", ctx->module_name);
			return 0;
		}
		if (ctx->memory_size_addr)
			ctx->memory_size = *ctx->memory_size_addr;
		return 1;
	}

	// Leave directly compiled native memory for the module to initialize
	if (ctx->type == WAHE_MODULE_NATIVE)
		return 1;

	// Reject module types without Wasmtime memory
	if (ctx->type != WAHE_MODULE_WASMTIME)
	{
		fprintf_rl(stderr, "Cannot initialise memory for module %s with invalid module type %d\n", ctx->module_name, ctx->type);
		return 0;
	}

	#ifdef WAHE_WASMTIME
	// Use the store-independent shared memory handle when present
	if (ctx->shared_memory)
	{
		ctx->memory_ptr = wasmtime_sharedmemory_data(ctx->shared_memory);
		ctx->memory_size = wasmtime_sharedmemory_data_size(ctx->shared_memory);
		return 1;
	}

	// Look for the ordinary memory exported by runner zero
	wahe_wasmtime_runner_t *runner = wahe_get_wasmtime_runner(ctx, 0, "initialise module memory");
	wasmtime_extern_t item;
	if (runner == NULL || !wasmtime_linker_get(runner->linker, runner->context, "", 0, "memory", strlen("memory"), &item))
	{
		fprintf_rl(stderr, "Error: memory not found in wasmtime_linker_get()\n");
		return 0;
	}

	if (item.kind != WASMTIME_EXTERN_MEMORY)
	{
		// Accept an internally defined shared memory for a single runner
		if (item.kind == WASMTIME_EXTERN_SHAREDMEMORY)
		{
			ctx->shared_memory = wasmtime_sharedmemory_clone(item.of.sharedmemory);
			ctx->memory_ptr = wasmtime_sharedmemory_data(ctx->shared_memory);
			ctx->memory_size = wasmtime_sharedmemory_data_size(ctx->shared_memory);
			return 1;
		}

		fprintf_rl(stderr, "Error: memory found in wasmtime_linker_get() is not a memory\n");
		return 0;
	}

	runner->memory = item.of.memory;

	// Store the fixed memory address and initial size
	ctx->memory_ptr = wasmtime_memory_data(runner->context, &runner->memory);
	ctx->memory_size = wasmtime_memory_data_size(runner->context, &runner->memory);

	return 1;

	#else
	fprintf_rl(stderr, "Cannot initialise Wasmtime memory for module %s because WAHE was built without Wasmtime support\n", ctx->module_name);
	return 0;
	#endif // WAHE_WASMTIME

}

static void wahe_refresh_module_memory_size(wahe_module_t *ctx)
{
	#ifdef WAHE_WASMTIME
	// Refresh only Wasmtime memories whose committed size can grow
	if (ctx == NULL || ctx->type != WAHE_MODULE_WASMTIME)
		return;

	// Read the active size from the module's shared or ordinary memory
	size_t current_size;
	if (ctx->shared_memory)
		current_size = wasmtime_sharedmemory_data_size(ctx->shared_memory);
	else
	{
		wahe_wasmtime_runner_t *runner = wahe_get_wasmtime_runner(ctx, 0, "refresh module memory size");
		if (runner == NULL)
			return;
		current_size = wasmtime_memory_data_size(runner->context, &runner->memory);
	}

	// Serialize reporting and updating the common module size
	rl_mutex_lock(&ctx->mutex);
	if (current_size != ctx->memory_size)
	{
		fprintf_rl(stdout, "Memory of module #%d %s grew from %zu kB to %zu kB\n", ctx->module_id, ctx->module_name, ctx->memory_size >> 10, current_size >> 10);
		ctx->memory_size = current_size;
	}
	rl_mutex_unlock(&ctx->mutex);
	#else
	(void) ctx;
	#endif
}

static size_t wahe_get_module_symbol_address_on_runner(wahe_module_t *ctx, size_t runner_id, const char *symbol_name, int verbosity)
{
	size_t addr = 0;

	if (ctx->type == WAHE_MODULE_WASM_TO_NATIVE || ctx->type == WAHE_MODULE_NATIVE)
	{
		addr = (size_t) dynlib_find_symbol(ctx->native, symbol_name);

		if (addr == 0 && verbosity == -1)
			fprintf_rl(stderr, "Error in module %s: symbol %s not found\n", ctx->module_name, symbol_name);

		if (addr && verbosity == 1)
			fprintf_rl(stdout, "Module #%d %s: symbol %s found\n", ctx->module_id, ctx->module_name, symbol_name);
	}
	else if (ctx->type == WAHE_MODULE_WASMTIME)
	{
		#ifdef WAHE_WASMTIME
		// Select the store-specific instance that owns the global
		wahe_wasmtime_runner_t *runner = wahe_get_wasmtime_runner(ctx, runner_id, "find a module symbol");
		if (runner == NULL)
			return 0;

		// Look up and read the exported global
		wasmtime_extern_t symb_ext;
		if (wasmtime_linker_get(runner->linker, runner->context, "", 0, symbol_name, strlen(symbol_name), &symb_ext))
		{
			if (symb_ext.kind != WASMTIME_EXTERN_GLOBAL)
			{
				fprintf_rl(stderr, "Error in module %s runner %zu: symbol %s is not a global\n", ctx->module_name, runner_id, symbol_name);
				return 0;
			}

			// Convert the Wasm global value to a module address
			wasmtime_val_t offset;
			wasmtime_global_get(runner->context, &symb_ext.of.global, &offset);
			addr = wasmtime_val_get_address(offset);

			if (verbosity == 1)
				fprintf_rl(stdout, "Module #%d %s runner %zu: symbol %s found\n", ctx->module_id, ctx->module_name, runner_id, symbol_name);
		}
		else if (verbosity == -1)
			fprintf_rl(stderr, "Error in module %s runner %zu: symbol %s not found in wasmtime_linker_get()\n", ctx->module_name, runner_id, symbol_name);

		#endif // WAHE_WASMTIME
	}
	else
	{
		// Report symbol lookup with an uninitialized module type
		fprintf_rl(stderr, "Cannot find symbol %s in module %s with invalid module type %d\n", symbol_name, ctx->module_name, ctx->type);
	}

	return addr;
}

size_t wahe_get_module_symbol_address(wahe_module_t *ctx, const char *symbol_name, int verbosity)
{
	// Resolve Wasmtime globals in the active runner and all other symbols normally
	#ifdef WAHE_WASMTIME
	size_t runner_id = wahe_active_runner_id(ctx);
	#else
	size_t runner_id = 0;
	#endif
	return wahe_get_module_symbol_address_on_runner(ctx, runner_id, symbol_name, verbosity);
}

void wahe_get_module_func(wahe_module_t *ctx, const char *func_name, enum wahe_func_id func_id, int verbosity)
{
	if (ctx->type == WAHE_MODULE_WASM_TO_NATIVE || ctx->type == WAHE_MODULE_NATIVE)
	{
		ctx->dl_func[func_id] = dynlib_find_symbol(ctx->native, func_name);

		if (ctx->dl_func[func_id] == NULL && verbosity == -1)
			fprintf_rl(stderr, "Error in module %s: function %s() not found\n", ctx->module_name, func_name);

		if (ctx->dl_func[func_id] && verbosity == 1)
			fprintf_rl(stdout, "Module #%d %s: %s() found\n", ctx->module_id, ctx->module_name, func_name);
	}
	else if (ctx->type == WAHE_MODULE_WASMTIME)
	{
		#ifdef WAHE_WASMTIME
		// Resolve the store-specific function handle in every runner
		for (size_t runner_id = 0; runner_id < ctx->runner_count; runner_id++)
		{
			wahe_wasmtime_runner_t *runner = &ctx->runner[runner_id];
			wasmtime_extern_t func_ext;

			// Get the function export from this runner's instance
			if (!wasmtime_linker_get(runner->linker, runner->context, "", 0, func_name, strlen(func_name), &func_ext))
			{
				if (verbosity == -1)
					fprintf_rl(stderr, "Error in module %s runner %zu: function %s() not found in wasmtime_linker_get()\n", ctx->module_name, runner_id, func_name);
				continue;
			}

			// Check the export kind before storing the function handle
			if (func_ext.kind != WASMTIME_EXTERN_FUNC)
			{
				if (verbosity == -1)
					fprintf_rl(stderr, "Error in module %s runner %zu: symbol %s is not a function\n", ctx->module_name, runner_id, func_name);
				continue;
			}

			// Store the handle that belongs to this runner's store
			runner->func[func_id] = func_ext.of.func;

			if (verbosity == 1)
				fprintf_rl(stdout, "Module #%d %s runner %zu: %s() found\n", ctx->module_id, ctx->module_name, runner_id, func_name);
		}
		#endif // WAHE_WASMTIME
	}
	else
	{
		// Report function lookup with an uninitialized module type
		fprintf_rl(stderr, "Cannot find function %s() in module %s with invalid module type %d\n", func_name, ctx->module_name, ctx->type);
	}
}

void wahe_init_all_module_symbols(wahe_module_t *ctx)
{
	wahe_get_module_func(ctx, "module_malloc",        WAHE_FUNC_MALLOC, -1);
	wahe_get_module_func(ctx, "module_realloc",       WAHE_FUNC_REALLOC, -1);
	wahe_get_module_func(ctx, "module_free",          WAHE_FUNC_FREE, -1);
	wahe_get_module_func(ctx, "module_message_input", WAHE_FUNC_INPUT, 1);
	wahe_get_module_func(ctx, "module_proc_cmd",      WAHE_FUNC_PROC_CMD, 1);
	wahe_get_module_func(ctx, "module_draw",          WAHE_FUNC_DRAW, 1);
	wahe_get_module_func(ctx, "module_proc_image",    WAHE_FUNC_PROC_IMAGE, 1);
	wahe_get_module_func(ctx, "module_proc_sound",    WAHE_FUNC_PROC_SOUND, 1);

	if (ctx->type == WAHE_MODULE_WASM_TO_NATIVE || ctx->type == WAHE_MODULE_NATIVE)
	{
		// Identify a wasm-to-native module by its exported memory pointer
		ctx->native_memory = (uint8_t **) wahe_get_module_symbol_address(ctx, "mem0", 0);
		ctx->type = ctx->native_memory ? WAHE_MODULE_WASM_TO_NATIVE : WAHE_MODULE_NATIVE;

		if (ctx->type == WAHE_MODULE_WASM_TO_NATIVE)
		{
			// Send the host callback to the translated Wasm runtime
			void (*wasm_decomp_init)(void *, void *) = dynlib_find_symbol(ctx->native, "wasm_decomp_init");
			if (wasm_decomp_init)
				wasm_decomp_init(ctx, wahe_run_command_with_id_native);

			// Initialise the translated module's memory buffer
			call_module_free(ctx, 0);

			// Find the translated Wasm globals
			size_t addr = wahe_get_module_symbol_address(ctx, "memory_bits", 0);
			if (addr)
			{
				ctx->memory_bits = *(int8_t *) addr;

				addr = wahe_get_module_symbol_address(ctx, "__heap_base", 0);
				if (addr)
					ctx->heap_base = ctx->memory_bits == 32 ? *(uint32_t *) addr : *(uint64_t *) addr;

				addr = wahe_get_module_symbol_address(ctx, "__stack_pointer", 0);
				ctx->stack_ptr_addr = (size_t *) addr;
				if (addr)
					ctx->stack_base = ctx->memory_bits == 32 ? *(uint32_t *) addr : *(uint64_t *) addr;

				addr = wahe_get_module_symbol_address(ctx, "__data_end", 0);
				if (addr)
					ctx->data_end = ctx->memory_bits == 32 ? *(uint32_t *) addr : *(uint64_t *) addr;

				ctx->memory_size_addr = (size_t *) wahe_get_module_symbol_address(ctx, "mem0_size", 0);
			}
		}
	}
	else if (ctx->type == WAHE_MODULE_WASMTIME)
	{
		ctx->heap_base = wahe_get_module_symbol_address(ctx, "__heap_base", 0);
		ctx->data_end = wahe_get_module_symbol_address(ctx, "__data_end", 0);
	}
}

static size_t call_module_func_core_on_runner(wahe_module_t *ctx, size_t runner_id, size_t *arg, int arg_count, enum wahe_func_id func_id)
{
	size_t ret_val;

	// Reject invalid call metadata
	if (ctx == NULL)
	{
		fprintf_rl(stderr, "Cannot call module function %d without a module context\n", func_id);
		return 0;
	}
	if (func_id <= WAHE_FUNC_NONE || func_id >= WAHE_FUNC_COUNT)
	{
		fprintf_rl(stderr, "Cannot call invalid function ID %d in module %s\n", func_id, ctx->module_name);
		return 0;
	}
	if (arg_count < 0 || arg_count > 2 || (arg_count && arg == NULL))
	{
		fprintf_rl(stderr, "Cannot call %s:%s() with %d arguments at %p\n", ctx->module_name, wahe_func_name[func_id], arg_count, (void *) arg);
		return 0;
	}

	// Reject calls into modules that did not initialise successfully
	if (!ctx->valid)
	{
		fprintf_rl(stderr, "Cannot call %s:%s() because the module is invalid\n", ctx->module_name, wahe_func_name[func_id]);
		return 0;
	}

	// Native call
	if (ctx->type == WAHE_MODULE_WASM_TO_NATIVE || ctx->type == WAHE_MODULE_NATIVE)
	{
		// Native modules currently expose one directly callable instance
		if (runner_id != 0)
		{
			fprintf_rl(stderr, "Cannot call native module %s with runner %zu\n", ctx->module_name, runner_id);
			return 0;
		}

		// Reject calls to functions the dynamic library did not export
		if (ctx->dl_func[func_id] == NULL)
		{
			fprintf_rl(stderr, "Cannot call %s:%s() because the function is not exported\n", ctx->module_name, wahe_func_name[func_id]);
			return 0;
		}

		switch (func_id)
		{
			case WAHE_FUNC_MALLOC:
			{
				void *(*func)(size_t) = ctx->dl_func[WAHE_FUNC_MALLOC];
				ret_val = (size_t) func(arg[0]);
				break;
			}

			case WAHE_FUNC_REALLOC:
			{
				void *(*func)(size_t,size_t) = ctx->dl_func[WAHE_FUNC_REALLOC];
				ret_val = (size_t) func(arg[0], arg[1]);
				break;
			}

			case WAHE_FUNC_FREE:
			{
				void (*func)(void *) = ctx->dl_func[WAHE_FUNC_FREE];
				func((void *) arg[0]);
				ret_val = 0;
				break;
			}

			default:
			{
				char *(*func)(char *) = ctx->dl_func[func_id];
				ret_val = (size_t) func((char *) arg[0]);
			}
		}

		return ret_val;
	}

	// Update CIT Alloc timestamp if present
	if (ctx->cita_time_addr)
		*(int32_t*) &ctx->memory_ptr[ctx->cita_time_addr] = get_time_hr() * 100.;

	// Reject module types that cannot use Wasmtime dispatch
	if (ctx->type != WAHE_MODULE_WASMTIME)
	{
		fprintf_rl(stderr, "Cannot call %s:%s() with invalid module type %d\n", ctx->module_name, wahe_func_name[func_id], ctx->type);
		return 0;
	}

	#ifdef WAHE_WASMTIME
	// Select the runner whose store and function handle will execute the call
	wahe_wasmtime_runner_t *runner = wahe_get_wasmtime_runner(ctx, runner_id, "call a module function");
	if (runner == NULL)
		return 0;

	wahe_chain_t *chain = wahe_cur_chain;
	wasmtime_error_t *error;
	wasm_trap_t *trap = NULL;
	wasmtime_val_t ret[1], param[2];

	// Reject functions not exported by this runner's instance
	if (runner->func[func_id].store_id == 0)
	{
		fprintf_rl(stderr, "Cannot call %s runner %zu:%s() because the Wasmtime function is not exported\n", ctx->module_name, runner_id, wahe_func_name[func_id]);
		return 0;
	}

	// Set params
	for (int i=0; i < arg_count; i++)
		param[i] = wasmtime_val_set_address(ctx, arg[i]);

	// Record the function selected by the current execution chain
	wahe_bench_point("calling function", 1);
	int prev_func = WAHE_FUNC_NONE;
	if (chain)
	{
		prev_func = chain->current_func;
		chain->current_func = func_id;
	}

	// Enter only this runner while allowing other runners to execute in parallel
	wahe_wasmtime_runner_t *prev_runner = wahe_cur_wasmtime_runner;
	rl_mutex_lock(&runner->mutex);
	wahe_cur_wasmtime_runner = runner;
	error = wasmtime_func_call(runner->context, &runner->func[func_id], param, arg_count, ret, func_id != WAHE_FUNC_FREE, &trap);
	wahe_cur_wasmtime_runner = prev_runner;
	rl_mutex_unlock(&runner->mutex);

	// Restore chain diagnostics after the call returns
	if (chain)
		chain->current_func = prev_func;
	wahe_bench_point("function returned", -1);

	// Synchronize memory growth performed during the module call
	wahe_refresh_module_memory_size(ctx);

	if (error || trap)
	{
		fprintf_rl(stderr, "Calling %s runner %zu:%s() failed\n", ctx->module_name, runner_id, wahe_func_name[func_id]);
		fprint_wasmtime_error(ctx, runner, error, trap);
		ctx->valid = 0;
		return 0;
	}

	// Check result type
	if (func_id != WAHE_FUNC_FREE)
		if (ret[0].kind != ctx->address_type)
		{
			fprintf_rl(stderr, "call_module_func_core_on_runner() expected a type %s result from %s runner %zu:%s()\n", ctx->address_type==WASMTIME_I32 ? "int32_t" : "int64_t", ctx->module_name, runner_id, wahe_func_name[func_id]);
			return 0;
		}

	// Return the raw return value
	return wasmtime_val_get_address(ret[0]);

	#else
	fprintf_rl(stderr, "Cannot call Wasmtime function %s:%s() because WAHE was built without Wasmtime support\n", ctx->module_name, wahe_func_name[func_id]);
	return 0;
	#endif // WAHE_WASMTIME
}

size_t call_module_malloc_on_runner(wahe_module_t *ctx, size_t runner_id, size_t size)
{
	// Report allocation failure with its module and requested size
	size_t address = call_module_func_core_on_runner(ctx, runner_id, &size, 1, WAHE_FUNC_MALLOC);
	if (address == 0)
		fprintf_rl(stderr, "module_malloc() in module %s runner %zu failed to allocate %zu bytes\n", ctx ? ctx->module_name : "(?)", runner_id, size);
	return address;
}

size_t call_module_realloc_on_runner(wahe_module_t *ctx, size_t runner_id, size_t address, size_t size)
{
	size_t ret, arg[2];

	// Call realloc
	arg[0] = address;
	arg[1] = size;
	ret = call_module_func_core_on_runner(ctx, runner_id, arg, 2, WAHE_FUNC_REALLOC);

	// Check NULL result
	if (ret == 0)
		fprintf_rl(stderr, "call_module_realloc_on_runner(%s, %zu, %#zx, %zu) returned NULL\n", ctx->module_name, runner_id, address, size);

	return ret;
}

void call_module_free_on_runner(wahe_module_t *ctx, size_t runner_id, size_t address)
{
	// Call the deallocator through the selected runner
	call_module_func_core_on_runner(ctx, runner_id, &address, 1, WAHE_FUNC_FREE);
}

char *call_module_func_on_runner(wahe_module_t *ctx, size_t runner_id, size_t message_addr, enum wahe_func_id func_id, int call_from_eo)
{
	size_t ret_msg_addr_s = 0, *ret_msg_addr = &ret_msg_addr_s;

	if (call_from_eo)
	{
		// Find where to store the return message address
		wahe_exec_order_t *eo = &wahe_cur_chain->exec_order[wahe_cur_chain->current_eo];
		ret_msg_addr = &eo->ret_msg_addr;
	}

	// Call function and store the return message address
	*ret_msg_addr = (size_t) call_module_func_core_on_runner(ctx, runner_id, &message_addr, 1, func_id);

	// Return pointer to return message
	if (ctx->type == WAHE_MODULE_NATIVE)
		return (char *) *ret_msg_addr;
	if (*ret_msg_addr)
		return (char *) &ctx->memory_ptr[*ret_msg_addr];
	return NULL;
}

size_t call_module_malloc(wahe_module_t *ctx, size_t size)
{
	// Use the active callback runner or runner zero
	#ifdef WAHE_WASMTIME
	size_t runner_id = wahe_active_runner_id(ctx);
	#else
	size_t runner_id = 0;
	#endif
	return call_module_malloc_on_runner(ctx, runner_id, size);
}

size_t call_module_realloc(wahe_module_t *ctx, size_t address, size_t size)
{
	// Use the active callback runner or runner zero
	#ifdef WAHE_WASMTIME
	size_t runner_id = wahe_active_runner_id(ctx);
	#else
	size_t runner_id = 0;
	#endif
	return call_module_realloc_on_runner(ctx, runner_id, address, size);
}

void call_module_free(wahe_module_t *ctx, size_t address)
{
	// Use the active callback runner or runner zero
	#ifdef WAHE_WASMTIME
	size_t runner_id = wahe_active_runner_id(ctx);
	#else
	size_t runner_id = 0;
	#endif
	call_module_free_on_runner(ctx, runner_id, address);
}

char *call_module_func(wahe_module_t *ctx, size_t message_addr, enum wahe_func_id func_id, int call_from_eo)
{
	// Use the active callback runner or runner zero
	#ifdef WAHE_WASMTIME
	size_t runner_id = wahe_active_runner_id(ctx);
	#else
	size_t runner_id = 0;
	#endif
	return call_module_func_on_runner(ctx, runner_id, message_addr, func_id, call_from_eo);
}

#ifdef H_ROUZICLIB
int wahe_pixel_format_to_raster_mode(const char *name)
{
	if (strcmp(name, "RGBA UQ1.15 linear") == 0)
		return IMAGE_USE_LRGB;

	if (strcmp(name, "RGBA float linear") == 0)
		return IMAGE_USE_FRGB;

	if (strcmp(name, "RGBA 8 sRGB") == 0)
		return IMAGE_USE_SRGB;

	if (strcmp(name, "RGB 10-12-10 sqrt") == 0)
		return IMAGE_USE_SQRGB;

	return IMAGE_USE_BUF;
}

int wahe_message_to_raster(wahe_module_t *ctx, size_t msg_addr, raster_t *r)
{
	size_t raster_size = 0, raster_address = 0;

	// Report missing display messages
	if (msg_addr == 0)
	{
		fprintf_rl(stderr, "Module %s did not return a framebuffer message for display\n", ctx->module_name);
		return 0;
	}

	int ret_mode = get_raster_mode(*r);

	// Pointer to the message
	char *message = &ctx->memory_ptr[msg_addr];

	// Parse each line of the message
	for (const char *line = message; line; line = strstr_after(line, "\n"))
	{
		char a[32];

		if (sscanf(line, "Pixel format: %31[^\n]", a) == 1)
			ret_mode = wahe_pixel_format_to_raster_mode(a);

		sscanf(line, "Framebuffer location: %zi bytes at %zi", &raster_size, &raster_address);
		sscanf(line, "Framebuffer resolution %dx%d", &r->dim.x, &r->dim.y);
	}

	if (raster_address == 0)
	{
		fprintf_rl(stderr, "Framebuffer message from module %s does not contain a valid framebuffer location\n", ctx->module_name);
		return 0;
	}

	// Reject framebuffer ranges outside active module memory
	if (ctx->memory_ptr == NULL || raster_address > ctx->memory_size || raster_size > ctx->memory_size - raster_address)
	{
		fprintf_rl(stderr, "Framebuffer from module %s uses %zu bytes at offset %#zx outside its %zu-byte active memory\n", ctx->module_name, raster_size, raster_address, ctx->memory_size);
		return 0;
	}

	// Update the host-side raster for the module framebuffer
	*r = make_raster(&ctx->memory_ptr[raster_address], r->dim, r->dim, ret_mode);
	cl_unref_raster(r);

	if (ret_mode == IMAGE_USE_BUF)
		r->buf_size = raster_size;

	return 1;
}
#endif

static size_t module_vsprintf_alloc_on_runner(wahe_module_t *ctx, size_t runner_id, const char *format, va_list args)
{
	int len;

	// Get length of string to print
	len = vstrlenf(format, args);

	// Allocate the message through the selected runner
	size_t addr = call_module_malloc_on_runner(ctx, runner_id, len+1);

	// Print
	if (addr)
		vsnprintf(&ctx->memory_ptr[addr], len+1, format, args);

	return addr;
}

static size_t module_sprintf_alloc_on_runner(wahe_module_t *ctx, size_t runner_id, const char *format, ...)
{
	// Format a message allocated through the selected runner
	va_list args;
	va_start(args, format);
	size_t addr = module_vsprintf_alloc_on_runner(ctx, runner_id, format, args);
	va_end(args);
	return addr;
}

size_t module_vsprintf_alloc(wahe_module_t *ctx, const char *format, va_list args)
{
	// Select the active callback runner or runner zero
	#ifdef WAHE_WASMTIME
	size_t runner_id = wahe_active_runner_id(ctx);
	#else
	size_t runner_id = 0;
	#endif
	return module_vsprintf_alloc_on_runner(ctx, runner_id, format, args);
}

size_t module_sprintf_alloc(wahe_module_t *ctx, const char *format, ...)
{
	va_list args;

	va_start(args, format);
	size_t addr = module_vsprintf_alloc(ctx, format, args);
	va_end(args);

	return addr;
}

char *wahe_send_input(wahe_module_t *ctx, const char *format, ...)
{
	va_list args;

	// Print to message allocated in the module's memory
	va_start(args, format);
	size_t message_addr = module_vsprintf_alloc(ctx, format, args);
	va_end(args);

	// Send the message then free it
	char *return_msg = call_module_func(ctx, message_addr, WAHE_FUNC_INPUT, 0);
	call_module_free(ctx, message_addr);

	return return_msg;
}

#ifdef WAHE_WASMTIME
int is_wasmtime_func_found(wasmtime_func_t func)
{
	return func.store_id != 0;
}
#endif // WAHE_WASMTIME

static wahe_cmd_reg_t *wahe_add_command_registration(wahe_group_t *group, const char *command)
{
	// Allocate and initialise the common registration fields
	alloc_enough(&group->cmd_reg, group->cmd_reg_count+=1, &group->cmd_reg_as, sizeof(wahe_cmd_reg_t), 1.2);
	wahe_cmd_reg_t *reg = &group->cmd_reg[group->cmd_reg_count-1];
	reg->hash = get_string_hash(command);
	reg->word_count = string_count_fields(command, " ");
	group->max_cmd_word_count = MAXN(group->max_cmd_word_count, reg->word_count);
	return reg;
}

void wahe_register_commands(wahe_module_t *ctx, char *list)
{
	wahe_group_t *group = ctx->parent_group;
	int il, linecount;
	char **array = arrayise_text(make_string_copy(list), &linecount);

	fprintf_rl(stdout, "Registering commands for module %s:\n%s\n", ctx->module_name, list);

	for (il = 0; il < linecount; il++)
	{
		// Blindly add command to register, even if it's already been registered
		wahe_cmd_reg_t *reg = wahe_add_command_registration(group, array[il]);
		reg->target_type = WAHE_CMD_TARGET_MODULE;
		reg->module_id = ctx->module_id;
	}

	free_2d(array, 1);
}

#ifdef WAHE_WASMTIME
static int wahe_set_wasmtime_runner_stack(wahe_module_t *ctx, wahe_wasmtime_runner_t *runner)
{
	// Find the private stack-pointer global in this runner's instance
	wasmtime_extern_t stack_export;
	if (!wasmtime_linker_get(runner->linker, runner->context, "", 0, "__stack_pointer", sizeof("__stack_pointer")-1, &stack_export) ||
		stack_export.kind != WASMTIME_EXTERN_GLOBAL)
	{
		// Keep legacy single-runner modules that do not export their stack pointer
		if (ctx->runner_count == 1)
			return 1;

		fprintf_rl(stderr, "Module %s runner %zu does not export a mutable __stack_pointer global\n", ctx->module_name, runner->runner_id);
		return 0;
	}
	runner->stack_pointer = stack_export.of.global;

	// Read the linked stack top from this instance before assigning its slice
	wasmtime_val_t linked_stack_pointer;
	wasmtime_global_get(runner->context, &runner->stack_pointer, &linked_stack_pointer);
	size_t linked_stack_top = wasmtime_val_get_address(linked_stack_pointer);
	if (runner->runner_id == 0)
		ctx->stack_base = linked_stack_top;
	else if (linked_stack_top != ctx->stack_base)
	{
		fprintf_rl(stderr, "Module %s runner %zu has stack top %#zx instead of runner zero's %#zx\n", ctx->module_name, runner->runner_id, linked_stack_top, ctx->stack_base);
		return 0;
	}

	// Preserve the linked stack pointer for a single runner
	if (ctx->runner_count == 1)
		return 1;

	// Require stack-first layout so slicing does not overlap module data
	size_t data_end = wahe_get_module_symbol_address_on_runner(ctx, runner->runner_id, "__data_end", 0);
	if (data_end < ctx->stack_base)
	{
		fprintf_rl(stderr, "Module %s must export __data_end and be linked with --stack-first before its stack can be split between runners\n", ctx->module_name);
		return 0;
	}

	// Validate that the linker-reserved stack can be split equally and aligned
	if (ctx->stack_base == 0 || ctx->stack_base % ctx->runner_count != 0)
	{
		fprintf_rl(stderr, "Module %s stack region %#zx cannot be split between %zu runners\n", ctx->module_name, ctx->stack_base, ctx->runner_count);
		return 0;
	}
	size_t stack_size = ctx->stack_base / ctx->runner_count;
	if (stack_size < 16 || stack_size % 16 != 0)
	{
		fprintf_rl(stderr, "Module %s runner stack size %#zx is not a positive multiple of 16 bytes\n", ctx->module_name, stack_size);
		return 0;
	}

	// Assign this runner the top of its zero-indexed stack slice
	size_t stack_top = stack_size * (runner->runner_id + 1);
	wasmtime_val_t value = wasmtime_val_set_address(ctx, stack_top);
	wasmtime_error_t *error = wasmtime_global_set(runner->context, &runner->stack_pointer, &value);
	if (error)
	{
		fprintf_rl(stderr, "Error setting stack pointer %#zx for module %s runner %zu\n", stack_top, ctx->module_name, runner->runner_id);
		fprint_wasmtime_error(ctx, runner, error, NULL);
		return 0;
	}

	fprintf_rl(stdout, "Module #%d %s runner %zu stack range %#zx - %#zx\n", ctx->module_id, ctx->module_name, runner->runner_id, stack_top-stack_size, stack_top);
	return 1;
}

static int wahe_init_wasmtime_runner(wahe_module_t *ctx, size_t runner_id)
{
	// Initialize the runner identity and its recursive store lock
	wahe_wasmtime_runner_t *runner = &ctx->runner[runner_id];
	runner->module = ctx;
	runner->runner_id = runner_id;
	rl_mutex_init(&runner->mutex);

	// Create a separate store so this runner can execute concurrently
	runner->store = wasmtime_store_new(ctx->engine, runner, NULL);
	if (runner->store == NULL)
	{
		fprintf_rl(stderr, "Error creating Wasmtime store for module %s runner %zu\n", ctx->module_name, runner_id);
		return 0;
	}
	runner->context = wasmtime_store_context(runner->store);

	// Create this runner's linker and install the WASI imports
	runner->linker = wasmtime_linker_new(ctx->engine);
	wasmtime_error_t *error = wasmtime_linker_define_wasi(runner->linker);
	if (error)
	{
		fprintf_rl(stderr, "Error linking WASI for module %s runner %zu\n", ctx->module_name, runner_id);
		fprint_wasmtime_error(ctx, runner, error, NULL);
		return 0;
	}

	// Give every store an independent WASI configuration
	wasi_config_t *wasi_config = wasi_config_new();
	wasi_config_inherit_stdout(wasi_config);
	wasi_config_inherit_stderr(wasi_config);
	error = wasmtime_context_set_wasi(runner->context, wasi_config);
	if (error)
	{
		fprintf_rl(stderr, "Error initialising WASI for module %s runner %zu\n", ctx->module_name, runner_id);
		fprint_wasmtime_error(ctx, runner, error, NULL);
		return 0;
	}

	// Define the host command callback with the module as its stable identity
	wasm_valtype_t *address_type = ctx->address_type == WASMTIME_I32 ? wasm_valtype_new_i32() : wasm_valtype_new_i64();
	wasm_functype_t *func_type = wasm_functype_new_1_1(address_type, ctx->address_type == WASMTIME_I32 ? wasm_valtype_new_i32() : wasm_valtype_new_i64());
	error = wasmtime_linker_define_func(runner->linker, "env", strlen("env"), "wahe_run_command", strlen("wahe_run_command"), func_type, wahe_run_command, ctx, NULL);
	wasm_functype_delete(func_type);
	if (error)
	{
		fprintf_rl(stderr, "Error defining callback for module %s runner %zu\n", ctx->module_name, runner_id);
		fprint_wasmtime_error(ctx, runner, error, NULL);
		return 0;
	}

	// Import the same shared memory handle into every runner
	if (ctx->shared_memory)
	{
		wasmtime_extern_t memory_import = {0};
		memory_import.kind = WASMTIME_EXTERN_SHAREDMEMORY;
		memory_import.of.sharedmemory = ctx->shared_memory;
		error = wasmtime_linker_define(runner->linker, runner->context, "env", sizeof("env")-1, "memory", sizeof("memory")-1, &memory_import);
		if (error)
		{
			fprintf_rl(stderr, "Error defining shared memory for module %s runner %zu\n", ctx->module_name, runner_id);
			fprint_wasmtime_error(ctx, runner, error, NULL);
			return 0;
		}
	}

	// Instantiate the compiled module in this runner's store
	error = wasmtime_linker_module(runner->linker, runner->context, "", 0, ctx->module);
	if (error)
	{
		fprintf_rl(stderr, "Error instantiating module %s runner %zu\n", ctx->module_name, runner_id);
		fprint_wasmtime_error(ctx, runner, error, NULL);
		return 0;
	}

	// Give the instance its non-overlapping shadow-stack slice
	return wahe_set_wasmtime_runner_stack(ctx, runner);
}
#endif

void wahe_module_init(wahe_group_t *parent_group, int module_index, wahe_module_t *ctx, const char *path, size_t runner_count)
{
	memset(ctx, 0, sizeof(wahe_module_t));
	rl_mutex_init(&ctx->mutex);
	ctx->module_name = sprintf_alloc("%s", get_filename_from_path(path));

	fprintf_rl(stdout, "\n\342\234\247 Initialising module %s\n", ctx->module_name);

	// Store module index so we can know which index a given module has
	ctx->parent_group = parent_group;
	ctx->module_id = module_index;
	ctx->runner_count = runner_count ? runner_count : 1;

	// WASM module
	if (check_if_file_is_wasm(path))
	{
		// Record the Wasmtime module type
		ctx->type = WAHE_MODULE_WASMTIME;

		#ifdef WAHE_WASMTIME
		// Load WASM file
		buffer_t wasm_buf = buf_load_raw_file(path);
		if (wasm_buf.buf == NULL)
		{
			fprintf_rl(stderr, "Module not found at '%s'\n", path);
			return;
		}

		// Parse sections of the WASM binary
		wasmbin_memory_info_t memory_info;
		io_override_set_buffer();
		ctx->stack_base = wasmbin_read_stack_pointer((FILE *) &wasm_buf);
		int memory_found = wasmbin_read_memory_info((FILE *) &wasm_buf, &memory_info);
		io_override_set_FILE();
		if (!memory_found)
		{
			fprintf_rl(stderr, "Module %s does not declare or import a linear memory\n", ctx->module_name);
			free_buf(&wasm_buf);
			return;
		}
		ctx->page_count_initial = memory_info.base_pages;
		ctx->page_count_max = memory_info.max_pages;
		ctx->memory_is_shared = memory_info.shared;
		ctx->memory_bits = memory_info.memory64 ? 64 : 32;
		ctx->address_type = memory_info.memory64 ? WASMTIME_I64 : WASMTIME_I32;

		// Require an imported shared memory when multiple instances must share it
		if (ctx->runner_count > 1 && (!memory_info.imported || !memory_info.shared || !memory_info.maximum_present))
		{
			fprintf_rl(stderr, "Module %s requests %zu runners but its memory is not an imported shared memory with a maximum\n", ctx->module_name, ctx->runner_count);
			free_buf(&wasm_buf);
			return;
		}

		// Reject unsupported imported ordinary memories explicitly
		if (memory_info.imported && !memory_info.shared)
		{
			fprintf_rl(stderr, "Module %s imports an ordinary memory, which WAHE does not provide\n", ctx->module_name);
			free_buf(&wasm_buf);
			return;
		}

		// Configure the engine for the Wasm threads proposal when needed
		wasm_config_t *config = wasm_config_new();
		//wasmtime_config_debug_info_set(config, true);
		#ifdef WASMTIME_FEATURE_THREADS
		if (memory_info.shared)
			wasmtime_config_wasm_threads_set(config, true);
		#else
		if (memory_info.shared)
		{
			fprintf_rl(stderr, "Module %s requires Wasmtime thread support that is absent from this build\n", ctx->module_name);
			wasm_config_delete(config);
			free_buf(&wasm_buf);
			return;
		}
		#endif
		ctx->engine = wasm_engine_new_with_config(config);
		if (ctx->engine == NULL)
		{
			fprintf_rl(stderr, "Error creating Wasmtime engine for module %s\n", ctx->module_name);
			free_buf(&wasm_buf);
			return;
		}

		// Compile WASM
		wasmtime_error_t *error = wasmtime_module_new(ctx->engine, wasm_buf.buf, wasm_buf.len, &ctx->module);
		if (error)
		{
			fprintf_rl(stderr, "Error compiling the module in wasmtime_module_new()\n");
			fprint_wasmtime_error(ctx, NULL, error, NULL);
			free_buf(&wasm_buf);
			return;
		}

		free_buf(&wasm_buf);

		// Create the store-independent shared memory imported by every runner
		if (memory_info.shared)
		{
			wasm_memorytype_t *memory_type = wasmtime_memorytype_new(memory_info.base_pages, memory_info.maximum_present, memory_info.max_pages, memory_info.memory64, true);
			error = wasmtime_sharedmemory_new(ctx->engine, memory_type, &ctx->shared_memory);
			wasm_memorytype_delete(memory_type);
			if (error)
			{
				fprintf_rl(stderr, "Error creating shared memory for module %s\n", ctx->module_name);
				fprint_wasmtime_error(ctx, NULL, error, NULL);
				return;
			}
		}

		// Allocate and initialize all zero-indexed runners before executing the module
		ctx->runner = calloc(ctx->runner_count, sizeof(*ctx->runner));
		if (ctx->runner == NULL)
		{
			fprintf_rl(stderr, "Error allocating %zu Wasmtime runners for module %s\n", ctx->runner_count, ctx->module_name);
			return;
		}
		for (size_t runner_id = 0; runner_id < ctx->runner_count; runner_id++)
			if (!wahe_init_wasmtime_runner(ctx, runner_id))
				return;

		// Find every instance's store-specific function handles
		wahe_init_all_module_symbols(ctx);
		ctx->valid = 1;

		// Store the module's fixed memory address
		if (!wahe_init_module_memory(ctx))
		{
			ctx->valid = 0;
			return;
		}

		// Print details
		fprintf_rl(stdout, "%zu runners, total stack region %#zx, heap base %#zx, data end %#zx\n", ctx->runner_count, ctx->stack_base, ctx->heap_base, ctx->data_end);
		fprintf_rl(stdout, "Initial memory %" PRIu64 " kB, max memory %" PRIu64 " kB%s\n", (uint64_t) (ctx->page_count_initial*64ULL), (uint64_t) (ctx->page_count_max*64ULL), ctx->memory_is_shared ? ", shared" : "");

		#else
		ctx->valid = 0;
		fprintf_rl(stderr, "Cannot initiate WASM module %s when WAHE is compiled without WASM support\n", ctx->module_name);
		return;
		#endif // WAHE_WASMTIME
	}
	// Native module
	else if ((ctx->native = dynlib_open(path)))
	{
		// Reject multiple runners for module types without separate Wasmtime stores
		if (ctx->runner_count != 1)
		{
			fprintf_rl(stderr, "Native module %s cannot be initialized with %zu runners\n", ctx->module_name, ctx->runner_count);
			return;
		}

		// Use the native ABI while discovering the dynamic library type
		ctx->type = WAHE_MODULE_NATIVE;
		ctx->valid = 1;

		// Find functions from the native module
		wahe_init_all_module_symbols(ctx);

		// Store the module's fixed memory address
		if (!wahe_init_module_memory(ctx))
		{
			ctx->valid = 0;
			return;
		}
	}
	else
	{
		// Report dynamic libraries that could not be loaded
		fprintf_rl(stderr, "Could not load module %s from '%s' as Wasm or a dynamic library\n", ctx->module_name, path);
		return;
	}

	// Send pointer to wahe_run_command() if the module is not WASM
	if (ctx->type == WAHE_MODULE_WASM_TO_NATIVE || ctx->type == WAHE_MODULE_NATIVE)
		wahe_send_input(ctx, "wahe_run_command_with_id() = %#zx", wahe_run_command_with_id_native);

	// Register commands
	char *cmd_reg_msg = wahe_send_input(ctx, "Command registration");
	if (cmd_reg_msg)
		wahe_register_commands(ctx, cmd_reg_msg);

	// Find CIT Alloc timestamp address
	if (ctx->heap_base && memcmp(&ctx->memory_ptr[ctx->heap_base], "CITA", 4) == 0)
	{
		int version_pos = *(int32_t *)&ctx->memory_ptr[ctx->heap_base+4];
		char *version = &ctx->memory_ptr[ctx->heap_base + version_pos];
		if (strncmp(version, "CITA 1.0\nAddress 4", 18))
			fprintf_rl(stderr, "Module %s has an unknown CIT Alloc version: \"%s\"\n", ctx->module_name, version);
		else
			ctx->cita_time_addr = ctx->heap_base + 12;
	}

	#ifdef H_ROUZICLIB
	// Init module's textedit used for transmitting text input
	textedit_init(&ctx->input_te, 1);
	ctx->input_te.edit_mode = te_mode_full;
	#endif
}

void wahe_copy_between_memories(wahe_module_t *src_module, size_t src_addr, size_t copy_size, wahe_module_t *dst_module, size_t dst_addr)
{
	// Reject invalid source modules
	if (src_module && src_module->valid == 0)
	{
		fprintf_rl(stderr, "Cannot copy %zu bytes from invalid module %s\n", copy_size, src_module->module_name);
		return;
	}

	// Reject invalid destination modules
	if (dst_module && dst_module->valid == 0)
	{
		fprintf_rl(stderr, "Cannot copy %zu bytes to invalid module %s\n", copy_size, dst_module->module_name);
		return;
	}

	// Synchronize Wasmtime sizes before validating module-relative ranges
	wahe_refresh_module_memory_size(src_module);
	wahe_refresh_module_memory_size(dst_module);

	// Reject source ranges outside the module's active memory
	if (src_module && (src_module->memory_ptr == NULL || src_addr > src_module->memory_size || copy_size > src_module->memory_size - src_addr))
	{
		fprintf_rl(stderr, "Cannot copy %zu bytes from offset %#zx in module %s with a %zu-byte active memory\n", copy_size, src_addr, src_module->module_name, src_module->memory_size);
		return;
	}

	// Reject destination ranges outside the module's active memory
	if (dst_module && (dst_module->memory_ptr == NULL || dst_addr > dst_module->memory_size || copy_size > dst_module->memory_size - dst_addr))
	{
		fprintf_rl(stderr, "Cannot copy %zu bytes to offset %#zx in module %s with a %zu-byte active memory\n", copy_size, dst_addr, dst_module->module_name, dst_module->memory_size);
		return;
	}

	// Reject null direct host addresses
	if ((src_module == NULL && src_addr == 0 && copy_size) || (dst_module == NULL && dst_addr == 0 && copy_size))
	{
		fprintf_rl(stderr, "Cannot copy %zu bytes using direct host addresses %#zx to %#zx\n", copy_size, src_addr, dst_addr);
		return;
	}

	// Copy
	memcpy(dst_module ? &dst_module->memory_ptr[dst_addr] : (void *) dst_addr, src_module ? &src_module->memory_ptr[src_addr] : (void *) src_addr, copy_size);
}

size_t wahe_copy_message_between_modules_on_runner(wahe_module_t *src_module, const char *src_message, wahe_module_t *dst_module, size_t dst_runner_id)
{
	// Reject invalid module-to-module message transfers
	if (src_module == NULL || dst_module == NULL || src_message == NULL || src_module->valid == 0 || dst_module->valid == 0)
	{
		fprintf_rl(stderr, "Cannot copy a message between modules %s and %s using source pointer %p\n",
				src_module ? src_module->module_name : "(null)",
				dst_module ? dst_module->module_name : "(null)",
				(const void *) src_message);
		return 0;
	}

	// Prefix the message in memory allocated through the selected destination runner
	return module_sprintf_alloc_on_runner(dst_module, dst_runner_id, "From memory %#zx\n%s", (size_t) src_module->memory_ptr, src_message);
}

size_t wahe_copy_message_between_modules(wahe_module_t *src_module, const char *src_message, wahe_module_t *dst_module)
{
	// Select the active destination runner or runner zero
	#ifdef WAHE_WASMTIME
	size_t runner_id = wahe_active_runner_id(dst_module);
	#else
	size_t runner_id = 0;
	#endif
	return wahe_copy_message_between_modules_on_runner(src_module, src_message, dst_module, runner_id);
}

size_t wahe_load_raw_file(wahe_module_t *ctx, const char *path, size_t *size)
{
	FILE *in_file;
	uint8_t *data;
	size_t fsize, data_addr;

	if (size)
		*size = 0;

	// Open file handle
	in_file = fopen_utf8(path, "rb");
	if (in_file == NULL)
	{
		fprintf_rl(stderr, "File '%s' not found.\n", path);
		return 0;
	}

	// Get file size
	if (fseek(in_file, 0, SEEK_END) != 0)
	{
		fprintf_rl(stderr, "Could not seek to the end of file '%s'\n", path);
		fclose(in_file);
		return 0;
	}
	long file_size = ftell(in_file);
	if (file_size < 0 || (uintmax_t) file_size >= SIZE_MAX)
	{
		fprintf_rl(stderr, "Could not determine an allocatable size for file '%s'\n", path);
		fclose(in_file);
		return 0;
	}
	fsize = (size_t) file_size;
	rewind(in_file);

	// Alloc data buffer
	data_addr = call_module_malloc(ctx, fsize+1);
	if (data_addr == 0)
	{
		fprintf_rl(stderr, "Cannot load file '%s' because module %s could not allocate %zu bytes\n", path, ctx->module_name, fsize+1);
		fclose(in_file);
		return 0;
	}
	data = &ctx->memory_ptr[data_addr];

	// Read all the data at once
	size_t read_size = fread(data, 1, fsize, in_file);
	fclose(in_file);
	if (read_size != fsize)
	{
		fprintf_rl(stderr, "Reading file '%s' stopped after %zu of %zu bytes\n", path, read_size, fsize);
		call_module_free(ctx, data_addr);
		return 0;
	}

	if (size)
		*size = fsize;

	return data_addr;
}

#ifdef H_ROUZICLIB
void wahe_make_keyboard_mouse_messages(wahe_chain_t *chain, int module_id, int display_id, int conn_id)
{
	int i;
	buffer_t buf = {0};
	const char *state_name[] = { "up", "", "", "", "down", "repeat" };
	wahe_group_t *group = chain->parent_group;
	wahe_module_t *ctx = &group->module[module_id];

	// Set textedit if framebuffer is clicked which indicates that the module is active in the interface
	ctrl_button_state_t *butt_state = proc_mouse_rect_ctrl_lrmb(group->image[display_id].fb_rect);
	if (butt_state[0].down || butt_state[1].down)
		cur_textedit = &ctx->input_te;

	// Determine if the control that represents the display is active
	int mouse_active = butt_state[0].orig || butt_state[0].over || butt_state[1].orig || butt_state[1].over;
	int kb_active = (cur_textedit == &ctx->input_te);

	// Send text input
	if (ctx->input_te.string && ctx->input_te.string[0])
	{
		bufprintf(&buf, "Text input (0@) ");

		// Convert to 0@ format
		size_t len = strlen(ctx->input_te.string);
		for (int i=0; i < len; i++)
		{
			uint8_t c = ctx->input_te.string[i];
			bufprintf(&buf, "%c%c", '0' + (c >> 5), '@' + (c & 0x1F));
		}
		bufprintf(&buf, "\n");

		// Clear textedit
		textedit_clear_then_set_new_text(&ctx->input_te, NULL);
	}

	// Go through all keys looking for newly pressed or released keys
	if (kb_active)
	for (i = RL_SCANCODE_A; i < RL_NUM_SCANCODES; i++)
	{
		if (abs(mouse.key_state[i]) >= 2)
		{
			bufprintf(&buf, "Key %s: %d", state_name[2 + mouse.key_state[i]], i);

		#ifdef RL_SDL
			bufprintf(&buf, " / \"%s\" / \"%s\"", SDL_GetScancodeName(i), SDL_GetKeyName(SDL_GetKeyFromScancode(i)));
		#endif
			bufprintf(&buf, "\n");
		}
	}

	// Make mouse messages depending on the target display
	if (mouse_active)
	{
		xy_t r_scale, r_offset;
		rect_range_and_dim_to_scale_offset_inv(group->image[display_id].fb_rect, group->image[display_id].fb.dim, &r_scale, &r_offset, 0);
		xy_t pix_pos = mad_xy(mouse.u, r_scale, r_offset);

//if (mouse.b.lmb != -1 || mouse.b.rmb != -1)	// use this to simulate a touchscreen
		bufprintf(&buf, "Mouse position (pixels) %.16g %.16g\n", pix_pos.x, pix_pos.y);

		// Mouse delta
		bufprintf(&buf, "Mouse delta %.16g %.16g\n", mouse.d.x, mouse.d.y);
	}
	else if (group->image[display_id].mouse_active)
		bufprintf(&buf, "Mouse position (pixels) NAN NAN\n");

	// Mouse buttons
	for (i=0; i < 3; i++)
	{
		int b;
		const char *b_name[] = { "left", "middle", "right" };

		switch (i)
		{
				case 0: b = mouse.b.lmb;
			break;	case 1: b = mouse.b.mmb;
			break;	case 2: b = mouse.b.rmb;
		}

		if (abs(b) == 2)
			bufprintf(&buf, "Mouse %s button %s\n", b_name[i], state_name[2 + b]);
	}

	// Mouse wheel
	if (mouse.b.wheel)
		bufprintf(&buf, "Mouse scroll %s %d\n", mouse.b.wheel < 0 ? "down" : "up", abs(mouse.b.wheel));

	// Copy message from host memory to module memory
	wahe_exec_order_t *dst_eo = &chain->exec_order[chain->connection[conn_id].dst_eo];
	size_t *addr = &dst_eo->dst_msg_addr;
	call_module_free_on_runner(&group->module[module_id], dst_eo->runner_id, *addr);
	*addr = 0;

	if (buf.buf)
	{
		// Allocate the input through the runner that will consume it
		*addr = call_module_malloc_on_runner(&group->module[module_id], dst_eo->runner_id, buf.len + 1);
		if (*addr)
			memcpy(&group->module[module_id].memory_ptr[*addr], buf.buf, buf.len + 1);
		free_buf(&buf);
	}

	// Remember the active statuses
	group->image[display_id].mouse_active = mouse_active;
	group->image[display_id].kb_active = kb_active;
}
#endif

// Get called from the module
size_t wahe_run_command_core(wahe_module_t *ctx, char *message)
{
	wahe_group_t *group = NULL;
	size_t return_msg_addr = 0;

	// Report callbacks made without a message
	if (message == NULL)
	{
		fprintf_rl(stderr, "Module %s called wahe_run_command() without a message\n", ctx ? ctx->module_name : "(unidentified)");
		return 0;
	}

	wahe_chain_t *chain = wahe_cur_chain;
	if (ctx)
		group = ctx->parent_group;

	// Execute command processors for this execution order
	if (chain && chain->current_eo >= 0 && chain->exec_order)
	{
		wahe_exec_order_t *eo = &chain->exec_order[chain->current_eo];
		if (eo && eo->cmd_proc_id && chain->current_cmd_proc_id < eo->cmd_proc_count && eo->module_id == ctx->module_id)
		{
			int dst_module_index = eo->cmd_proc_id[chain->current_cmd_proc_id];
			wahe_module_t *dst_module = &group->module[dst_module_index];

			// Copy message to cmd processing module
			size_t dst_addr = wahe_copy_message_between_modules(ctx, message, dst_module);

			// Call cmd processing function
			chain->current_cmd_proc_id++;
			size_t return_msg_addr_dst = (size_t) call_module_func(dst_module, dst_addr, WAHE_FUNC_PROC_CMD, 0);
			if (return_msg_addr_dst)
				return_msg_addr_dst -= (size_t) dst_module->memory_ptr;
			call_module_free(dst_module, dst_addr);

			if (chain->current_cmd_proc_id == eo->cmd_proc_count)
				chain->current_cmd_proc_id = 0;

			// Copy and return the return message
			if (return_msg_addr_dst)
			{
				// Copy the processed message with its source memory address
				return_msg_addr = wahe_copy_message_between_modules(dst_module, &dst_module->memory_ptr[return_msg_addr_dst], ctx);
				call_module_free(dst_module, return_msg_addr_dst);
				return return_msg_addr;
			}
			else
				return 0;
		}
	}

	// Parse each line of the message
	for (const char *line = message; line; line = strstr_after(line, "\n"))
	{
		int done = 0;

		// Skip parsing if the callback did not identify its calling module
		if (group == NULL || ctx == NULL)
		{
			fprintf_rl(stderr, "Module calling wahe_run_command() is unidentified.\n");
			goto loop_end;
		}

		//** Run registered commands **
		uint64_t msg_hash[16] = {0};
		const char *p = line, *line_end = strstr(line, "\n");
		if (line_end == NULL)
			line_end = &line[strlen(line)];

		// Make hashes for all word counts
		for (int i=0; i < group->max_cmd_word_count && p < line_end; i++)
		{
			p = strstr(p, " ");
			if (p == NULL || p > line_end)
				p = line_end;

			msg_hash[i] = get_buffer_hash(line, p - line);
			p = &p[1];
		}

		// Go through every registered command to find a match
		for (int i = group->cmd_reg_count-1; i >= 0; i--)
			if (group->cmd_reg[i].hash == msg_hash[group->cmd_reg[i].word_count-1])
			{
				wahe_cmd_reg_t *reg = &group->cmd_reg[i];

				// Forward dynamically registered commands to their owning module
				if (reg->target_type == WAHE_CMD_TARGET_MODULE)
				{
					if (reg->module_id == ctx->module_id)
						continue;

					wahe_module_t *registered_module = &group->module[reg->module_id];
					char *ret_msg = wahe_send_input(registered_module, "From memory %#zx\n%s", (size_t) ctx->memory_ptr, line);
					if (ret_msg)
						return_msg_addr = wahe_copy_message_between_modules(registered_module, ret_msg, ctx);
					return return_msg_addr;
				}

				// Run host commands with the issuing module as their caller context
				enum wahe_host_cmd_result result = reg->host_func(ctx, &line, &return_msg_addr);
				if (result == WAHE_HOST_CMD_RETURN)
					return return_msg_addr;
				if (result == WAHE_HOST_CMD_HANDLED)
				{
					done = 1;
					break;
				}
			}
		//**                         **

loop_end:
		// Print line if it wasn't interpreted
		if (done == 0)
		{
			int line_len = strlen(line);
			if (strstr(line, "\n"))
				line_len = strstr(line, "\n") - line;
			fprintf_rl(stderr, "Command from %s:%s not interpreted: %.*s\n",
					ctx ? ctx->module_name : "(?)", 
					chain ? wahe_func_name[chain->current_func] : "(?)",
					line_len, line);
		}
	}

	return return_msg_addr;
}

char *wahe_run_command_with_id_native(wahe_module_t *ctx, char *message)
{
	return (char *) wahe_run_command_core(ctx, message);
}

#ifdef WAHE_WASMTIME
wasm_trap_t *wahe_run_command(void *env, wasmtime_caller_t *caller, const wasmtime_val_t *arg, size_t arg_count, wasmtime_val_t *result, size_t result_count)
{
	wahe_module_t *ctx = env;
	size_t return_msg_addr = 0;

	// Reject malformed callback invocations
	if (ctx == NULL || caller == NULL || arg == NULL || result == NULL || arg_count < 1 || result_count < 1)
	{
		fprintf_rl(stderr, "Malformed Wasmtime wahe_run_command() callback with module %p, %zu arguments and %zu results\n", (void *) ctx, arg_count, result_count);
		return NULL;
	}

	// Recover the runner identity stored in the calling Wasmtime store
	wasmtime_context_t *caller_context = wasmtime_caller_context(caller);
	wahe_wasmtime_runner_t *runner = wasmtime_context_get_data(caller_context);
	if (runner == NULL || runner->module != ctx)
	{
		fprintf_rl(stderr, "Wasmtime callback for module %s has an invalid runner context\n", ctx->module_name);
		return NULL;
	}

	// Make callback-triggered allocations and nested calls use this runner
	wahe_wasmtime_runner_t *prev_runner = wahe_cur_wasmtime_runner;
	wahe_cur_wasmtime_runner = runner;

	// Synchronize memory growth before processing commands from the module
	wahe_refresh_module_memory_size(ctx);

	// Parse message
	if (wasmtime_val_get_address(arg[0]))
	{
		// Get the message address and make pointer to the message
		char *message = &ctx->memory_ptr[wasmtime_val_get_address(arg[0])];
		if (wasmtime_val_get_address(arg[0]) == 0)
			message = NULL;

		// Run the command
		return_msg_addr = wahe_run_command_core(ctx, message);
	}

	// Restore the previous runner before returning to Wasmtime
	wahe_cur_wasmtime_runner = prev_runner;

	// Return message
	result[0] = wasmtime_val_set_address(ctx, return_msg_addr);

	return NULL;
}
#endif // WAHE_WASMTIME
