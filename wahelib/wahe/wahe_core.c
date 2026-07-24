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
void fprint_wasmtime_error(wahe_module_t *ctx, wasmtime_error_t *error, wasm_trap_t *trap)
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
		fprintf_rl(stderr, "Module stack pointer %#zx\n", wahe_get_module_symbol_address(ctx, "__stack_pointer", 0));
		wasm_byte_vec_delete(&error_message);
	}
}

wasmtime_val_t wasmtime_val_set_address(wahe_module_t *ctx, size_t address)
{
	wasmtime_val_t val;

	val.kind = ctx->address_type;

	if (ctx->address_type == WASMTIME_I32)
		val.of.i32 = address;
	else
		val.of.i64 = address;

	return val;
}

size_t wasmtime_val_get_address(wasmtime_val_t val)
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
	wasmtime_extern_t item;

	// Look for memory
	if (!wasmtime_linker_get(ctx->linker, ctx->context, "", 0, "memory", strlen("memory"), &item))
	{
		fprintf_rl(stderr, "Error: memory not found in wasmtime_linker_get()\n");
		return 0;
	}

	if (item.kind != WASMTIME_EXTERN_MEMORY)
	{
		fprintf_rl(stderr, "Error: memory found in wasmtime_linker_get() is not a memory\n");
		return 0;
	}

	ctx->memory = item.of.memory;

	// Store the fixed memory address and initial size
	ctx->memory_ptr = wasmtime_memory_data(ctx->context, &ctx->memory);
	ctx->memory_size = wasmtime_memory_data_size(ctx->context, &ctx->memory);

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

	// Report and store changes without updating the fixed memory pointer
	size_t current_size = wasmtime_memory_data_size(ctx->context, &ctx->memory);
	if (current_size != ctx->memory_size)
	{
		fprintf_rl(stdout, "Memory of module #%d %s grew from %zu kB to %zu kB\n", ctx->module_id, ctx->module_name, ctx->memory_size >> 10, current_size >> 10);
		ctx->memory_size = current_size;
	}
	#else
	(void) ctx;
	#endif
}

size_t wahe_get_module_symbol_address(wahe_module_t *ctx, const char *symbol_name, int verbosity)
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
		wasmtime_extern_t symb_ext;

		if (wasmtime_linker_get(ctx->linker, ctx->context, "", 0, symbol_name, strlen(symbol_name), &symb_ext))
		{
			wasmtime_val_t offset;
			wasmtime_global_get(ctx->context, &symb_ext.of.global, &offset);
			addr = wasmtime_val_get_address(offset);

			if (verbosity == 1)
				fprintf_rl(stdout, "Module #%d %s: symbol %s found\n", ctx->module_id, ctx->module_name, symbol_name);
		}
		else if (verbosity == -1)
			fprintf_rl(stderr, "Error in module %s: symbol %s not found in wasmtime_linker_get()\n", ctx->module_name, symbol_name);

		#endif // WAHE_WASMTIME
	}
	else
	{
		// Report symbol lookup with an uninitialized module type
		fprintf_rl(stderr, "Cannot find symbol %s in module %s with invalid module type %d\n", symbol_name, ctx->module_name, ctx->type);
	}

	return addr;
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
		wasmtime_extern_t func_ext;

		// Get the symbol
		if (!wasmtime_linker_get(ctx->linker, ctx->context, "", 0, func_name, strlen(func_name), &func_ext))
		{
			if (verbosity == -1)
				fprintf_rl(stderr, "Error in module %s: function %s() not found in wasmtime_linker_get()\n", ctx->module_name, func_name);
			return;
		}

		// Check the symbol is a function
		if (func_ext.kind != WASMTIME_EXTERN_FUNC)
		{
			if (verbosity == -1)
				fprintf_rl(stderr, "Error in module %s: symbol %s found in wasmtime_linker_get() is not a function\n", ctx->module_name, func_name);
			return;
		}

		// Store function symbol data
		ctx->func[func_id] = func_ext.of.func;

		if (verbosity == 1)
			fprintf_rl(stdout, "Module #%d %s: %s() found\n", ctx->module_id, ctx->module_name, func_name);
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
			void (*wasm_decomp_init)(int32_t, void *) = dynlib_find_symbol(ctx->native, "wasm_decomp_init");
			if (wasm_decomp_init)
				wasm_decomp_init(ctx->module_id, wahe_run_command_with_id_native);

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

size_t call_module_func_core(wahe_module_t *ctx, size_t *arg, int arg_count, enum wahe_func_id func_id)
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
	wahe_chain_t *chain = wahe_cur_chain;
	wasmtime_error_t *error;
	wasm_trap_t *trap = NULL;
	wasmtime_val_t ret[1], param[2];

	if (ctx->func[func_id].store_id == 0)
	{
		fprintf_rl(stderr, "Cannot call %s:%s() because the Wasmtime function is not exported\n", ctx->module_name, wahe_func_name[func_id]);
		return 0;
	}

	// Set params
	for (int i=0; i < arg_count; i++)
		param[i] = wasmtime_val_set_address(ctx, arg[i]);

	// Call the function
	wahe_bench_point("calling function", 1);
	int prev_func = WAHE_FUNC_NONE;
	if (chain)
	{
		prev_func = chain->current_func;
		chain->current_func = func_id;
	}
	rl_mutex_lock(&ctx->mutex);
	error = wasmtime_func_call(ctx->context, &ctx->func[func_id], param, arg_count, ret, func_id != WAHE_FUNC_FREE, &trap);
	rl_mutex_unlock(&ctx->mutex);
	if (chain)
		chain->current_func = prev_func;
	wahe_bench_point("function returned", -1);

	// Synchronize memory growth performed during the module call
	wahe_refresh_module_memory_size(ctx);

	if (error || trap)
	{
		fprintf_rl(stderr, "Calling %s:%s() failed\n", ctx->module_name, wahe_func_name[func_id]);
		fprint_wasmtime_error(ctx, error, trap);
		ctx->valid = 0;
		return 0;
	}

	// Check result type
	if (func_id != WAHE_FUNC_FREE)
		if (ret[0].kind != ctx->address_type)
		{
			fprintf_rl(stderr, "call_module_func_core() expected a type %s result from %s:%s()\n", ctx->address_type==WASMTIME_I32 ? "int32_t" : "int64_t", ctx->module_name, wahe_func_name[func_id]);
			return 0;
		}

	// Return the raw return value
	return wasmtime_val_get_address(ret[0]);

	#else
	fprintf_rl(stderr, "Cannot call Wasmtime function %s:%s() because WAHE was built without Wasmtime support\n", ctx->module_name, wahe_func_name[func_id]);
	return 0;
	#endif // WAHE_WASMTIME
}

size_t call_module_malloc(wahe_module_t *ctx, size_t size)
{
	// Report allocation failure with its module and requested size
	size_t address = call_module_func_core(ctx, &size, 1, WAHE_FUNC_MALLOC);
	if (address == 0)
		fprintf_rl(stderr, "module_malloc() in module %s failed to allocate %zu bytes\n", ctx ? ctx->module_name : "(?)", size);
	return address;
}

size_t call_module_realloc(wahe_module_t *ctx, size_t address, size_t size)
{
	size_t ret, arg[2];

	// Call realloc
	arg[0] = address;
	arg[1] = size;
	ret = call_module_func_core(ctx, arg, 2, WAHE_FUNC_REALLOC);

	// Check NULL result
	if (ret == 0)
		fprintf_rl(stderr, "call_module_realloc(%s, %#zx, %zu) returned NULL\n", ctx->module_name, address, size);

	return ret;
}

void call_module_free(wahe_module_t *ctx, size_t address)
{
	call_module_func_core(ctx, &address, 1, WAHE_FUNC_FREE);
}

char *call_module_func(wahe_module_t *ctx, size_t message_addr, enum wahe_func_id func_id, int call_from_eo)
{
	size_t ret_msg_addr_s = 0, *ret_msg_addr = &ret_msg_addr_s;

	if (call_from_eo)
	{
		// Find where to store the return message address
		wahe_exec_order_t *eo = &wahe_cur_chain->exec_order[wahe_cur_chain->current_eo];
		ret_msg_addr = &eo->ret_msg_addr;
	}

	// Call function and store the return message address
	*ret_msg_addr = (size_t) call_module_func_core(ctx, &message_addr, 1, func_id);

	// Return pointer to return message
	if (ctx->type == WAHE_MODULE_NATIVE)
		return (char *) *ret_msg_addr;
	if (*ret_msg_addr)
		return (char *) &ctx->memory_ptr[*ret_msg_addr];
	return NULL;
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

size_t module_vsprintf_alloc(wahe_module_t *ctx, const char *format, va_list args)
{
	int len;

	// Get length of string to print
	len = vstrlenf(format, args);

	// Alloc message in the module's linear memory
	size_t addr = call_module_malloc(ctx, len+1);

	// Print
	if (addr)
		vsnprintf(&ctx->memory_ptr[addr], len+1, format, args);

	return addr;
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

void wahe_module_init(wahe_group_t *parent_group, int module_index, wahe_module_t *ctx, const char *path)
{
	memset(ctx, 0, sizeof(wahe_module_t));
	rl_mutex_init(&ctx->mutex);
	ctx->module_name = sprintf_alloc("%s", get_filename_from_path(path));

	fprintf_rl(stdout, "\n\342\234\247 Initialising module %s\n", ctx->module_name);

	// Store module index so we can know which index a given module has
	ctx->parent_group = parent_group;
	ctx->module_id = module_index;

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
		io_override_set_buffer();
		ctx->stack_base = wasmbin_read_stack_pointer((FILE *) &wasm_buf);
		wasmbin_read_memory_size((FILE *) &wasm_buf, &ctx->page_count_initial, &ctx->page_count_max);
		io_override_set_FILE();

		wasmtime_error_t *error;
		wasm_functype_t *func_type;

		// WASM initialisation
		wasm_config_t *config = wasm_config_new();
		//wasmtime_config_debug_info_set(config, true);
		ctx->engine = wasm_engine_new_with_config(config);
		ctx->store = wasmtime_store_new(ctx->engine, NULL, NULL);
		ctx->context = wasmtime_store_context(ctx->store);

		// Create a linker with WASI functions defined
		ctx->linker = wasmtime_linker_new(ctx->engine);
		error = wasmtime_linker_define_wasi(ctx->linker);
		if (error)
		{
			fprintf_rl(stderr, "Error linking WASI in wasmtime_linker_define_wasi()\n");
			fprint_wasmtime_error(ctx, error, NULL);
			return;
		}

		// Compile WASM
		error = wasmtime_module_new(ctx->engine, wasm_buf.buf, wasm_buf.len, &ctx->module);
		if (error)
		{
			fprintf_rl(stderr, "Error compiling the module in wasmtime_module_new()\n");
			fprint_wasmtime_error(ctx, error, NULL);
			return;
		}

		free_buf(&wasm_buf);

		// WASI initialisation
		ctx->wasi_config = wasi_config_new();
		wasi_config_inherit_stdout(ctx->wasi_config);
		wasi_config_inherit_stderr(ctx->wasi_config);
		error = wasmtime_context_set_wasi(ctx->context, ctx->wasi_config);
		if (error)
		{
			fprintf_rl(stderr, "Error initialising WASI in wasmtime_context_set_wasi()\n");
			fprint_wasmtime_error(ctx, error, NULL);
			return;
		}

		// Initialise callbacks (host functions called from WASM module)
		func_type = wasm_functype_new_1_1(wasm_valtype_new_i32(), wasm_valtype_new_i32());

		// Pass the calling module context directly to the host callback
		error = wasmtime_linker_define_func(ctx->linker, "env", strlen("env"), "wahe_run_command", strlen("wahe_run_command"), func_type, wahe_run_command, ctx, NULL);
		if (error)
		{
			fprintf_rl(stderr, "Error defining callback in wasmtime_linker_define_func()\n");
			wasm_functype_delete(func_type);
			fprint_wasmtime_error(ctx, error, NULL);
			return;
		}
		wasm_functype_delete(func_type);

		// Instantiate the module
		error = wasmtime_linker_module(ctx->linker, ctx->context, "", 0, ctx->module);
		if (error)
		{
			fprintf_rl(stderr, "Error instantiating module %s in wasmtime_linker_module()\n", ctx->module_name);
			fprint_wasmtime_error(ctx, error, NULL);
			return;
		}

		// Find functions from the WASM module
		wahe_init_all_module_symbols(ctx);
		ctx->valid = 1;

		// Store the module's fixed memory address
		if (!wahe_init_module_memory(ctx))
		{
			ctx->valid = 0;
			return;
		}

		// Set the type of module addresses (currently always 32-bit)
		ctx->address_type = WASMTIME_I32;

		// Print details
		fprintf_rl(stdout, "Stack base %#zx, heap base %#zx, data end %#zx\n", ctx->stack_base, ctx->heap_base, ctx->data_end);
		fprintf_rl(stdout, "Initial memory %" PRIu64 " kB, max memory %" PRIu64 " kB\n", (uint64_t) (ctx->page_count_initial*64ULL), (uint64_t) (ctx->page_count_max*64ULL));

		#else
		ctx->valid = 0;
		fprintf_rl(stderr, "Cannot initiate WASM module %s when WAHE is compiled without WASM support\n", ctx->module_name);
		return;
		#endif // WAHE_WASMTIME
	}
	// Native module
	else if ((ctx->native = dynlib_open(path)))
	{
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

size_t wahe_copy_message_between_modules(wahe_module_t *src_module, const char *src_message, wahe_module_t *dst_module)
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

	// Prefix the message with the source module's actual host memory address
	return module_sprintf_alloc(dst_module, "From memory %#zx\n%s", (size_t) src_module->memory_ptr, src_message);
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
	size_t *addr = &chain->exec_order[ chain->connection[conn_id].dst_eo ].dst_msg_addr;
	call_module_free(&group->module[module_id], *addr);
	*addr = 0;

	if (buf.buf)
	{
		*addr = call_module_malloc(&group->module[module_id], buf.len + 1);
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
	if (ctx == NULL || arg == NULL || result == NULL || arg_count < 1 || result_count < 1)
	{
		fprintf_rl(stderr, "Malformed Wasmtime wahe_run_command() callback with module %p, %zu arguments and %zu results\n", (void *) ctx, arg_count, result_count);
		return NULL;
	}

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

	// Return message
	result[0] = wasmtime_val_set_address(ctx, return_msg_addr);

	return NULL;
}
#endif // WAHE_WASMTIME
