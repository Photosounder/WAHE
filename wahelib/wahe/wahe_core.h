enum wahe_eo_type
{
	WAHE_EO_MODULE_FUNC,
	WAHE_EO_IMAGE_DISPLAY,
	WAHE_EO_KB_MOUSE,
	WAHE_EO_CHAIN_INPUT_MSG
};

enum wahe_func_id
{
	WAHE_FUNC_NONE,
	WAHE_FUNC_MALLOC,
	WAHE_FUNC_REALLOC,
	WAHE_FUNC_FREE,

	WAHE_FUNC_INPUT,
	WAHE_FUNC_PROC_CMD,
	WAHE_FUNC_DRAW,
	WAHE_FUNC_PROC_IMAGE,
	WAHE_FUNC_PROC_SOUND,

	WAHE_FUNC_COUNT
};

// The two enums above must match the names in those arrays (in wahe_core.c)
extern const char *wahe_eo_name[];
extern const char *wahe_func_name[];

enum wahe_module_type
{
	WAHE_MODULE_NONE,
	WAHE_MODULE_WASMTIME,
	WAHE_MODULE_WASM_TO_NATIVE,
	WAHE_MODULE_NATIVE
};

typedef struct wahe_module_t wahe_module_t;

#ifdef WAHE_WASMTIME
typedef struct
{
	wahe_module_t *module;
	size_t runner_id;
	rl_mutex_t mutex;
	wasmtime_store_t *store;
	wasmtime_context_t *context;
	wasmtime_linker_t *linker;
	wasmtime_memory_t memory;
	wasmtime_global_t stack_pointer;
	wasmtime_func_t func[WAHE_FUNC_COUNT];
} wahe_wasmtime_runner_t;
#endif // WAHE_WASMTIME

struct wahe_module_t
{
	int valid;
	char *module_name;
	char *wahe_name;
	int module_id;
	enum wahe_module_type type;
	rl_mutex_t mutex;
	void *parent_group;	// wahe_group_t *
	size_t runner_count;
	#ifdef H_ROUZICLIB
	textedit_t input_te;
	#endif

	uint8_t *memory_ptr;
	size_t stack_base, data_end, heap_base, memory_size, memory_reserve_size, *memory_size_addr, *stack_ptr_addr, cita_time_addr;
	int8_t memory_bits;
	uint32_t page_count_initial, page_count_max;

	// Specific to WASM modules
	#ifdef WAHE_WASMTIME
	wasm_engine_t *engine;
	wasmtime_module_t *module;
	wasmtime_sharedmemory_t *shared_memory;
	wahe_wasmtime_runner_t *runner;
	int memory_is_shared;
	wasmtime_valkind_t address_type;
	#endif // WAHE_WASMTIME

	// Specific to native modules
	void *native, *dl_func[WAHE_FUNC_COUNT];
	uint8_t **native_memory;	// specific to wasm-to-native
};

#ifdef H_ROUZICLIB
typedef struct
{
	raster_t fb;
	rect_t fb_area, fb_rect;
	int mouse_active, kb_active;
} wahe_image_display_t;
#endif

typedef struct
{
	int src_eo, dst_eo;
} wahe_connection_t;

enum wahe_cmd_target_type
{
	WAHE_CMD_TARGET_MODULE,
	WAHE_CMD_TARGET_HOST
};

enum wahe_host_cmd_result
{
	WAHE_HOST_CMD_NOT_HANDLED,
	WAHE_HOST_CMD_HANDLED,
	WAHE_HOST_CMD_RETURN
};

typedef enum wahe_host_cmd_result (*wahe_host_cmd_func_t)(wahe_module_t *ctx, const char **line, size_t *return_msg_addr);

typedef struct
{
	uint64_t hash;
	int word_count;
	enum wahe_cmd_target_type target_type;
	union
	{
		int module_id;
		wahe_host_cmd_func_t host_func;
	};
} wahe_cmd_reg_t;

typedef struct
{
	enum wahe_eo_type type;
	int module_id, display_id;
	enum wahe_func_id func_id;
	size_t runner_id;
	size_t dst_msg_addr, ret_msg_addr;

	// Command processors
	// If type is WAHE_EO_MODULE_FUNC the function can call wahe_run_command()
	// A cascade of module_proc_cmd() functions can filter the commands and their return messages
	int *cmd_proc_id;	// Module index for the command processor
	size_t cmd_proc_count, cmd_proc_as;
} wahe_exec_order_t;

typedef struct
{
	char *chain_name;
	void *parent_group;	// wahe_group_t *

	wahe_connection_t *connection;
	size_t conn_count, conn_as;

	wahe_exec_order_t *exec_order;
	size_t exec_order_count, exec_order_as;
	
	int current_eo, current_cmd_proc_id, current_func;
} wahe_chain_t;

typedef struct
{
	wahe_module_t *module;
	size_t module_count, module_as;

	wahe_chain_t *chain;
	size_t chain_count, chain_as;

	#ifdef H_ROUZICLIB
	wahe_image_display_t *image;
	size_t image_count, image_as;
	#endif

	wahe_cmd_reg_t *cmd_reg;
	size_t cmd_reg_count, cmd_reg_as;
	int max_cmd_word_count, host_commands_registered;

	uint8_t *cita_index;
	size_t cita_index_size;
} wahe_group_t;

extern _Thread_local wahe_chain_t *wahe_cur_chain;

extern void *wahe_virtual_memory_alloc(size_t commit_size, size_t reserve_size);
extern int wahe_virtual_memory_commit(void *memory, size_t commit_size);
extern int wahe_virtual_memory_decommit(void *memory, size_t commit_size, size_t previous_commit_size);
extern int wahe_virtual_memory_free(void *memory, size_t reserve_size);

extern size_t wahe_get_module_symbol_address(wahe_module_t *ctx, const char *symbol_name, int verbosity);
extern void wahe_get_module_func(wahe_module_t *ctx, const char *func_name, enum wahe_func_id func_id, int verbosity);
extern void wahe_init_all_module_symbols(wahe_module_t *ctx);
extern size_t call_module_malloc_on_runner(wahe_module_t *ctx, size_t runner_id, size_t size);
extern size_t call_module_realloc_on_runner(wahe_module_t *ctx, size_t runner_id, size_t address, size_t size);
extern void call_module_free_on_runner(wahe_module_t *ctx, size_t runner_id, size_t address);
extern char *call_module_func_on_runner(wahe_module_t *ctx, size_t runner_id, size_t message_addr, enum wahe_func_id func_id, int call_from_eo);
extern size_t call_module_malloc(wahe_module_t *ctx, size_t size);
extern size_t call_module_realloc(wahe_module_t *ctx, size_t address, size_t size);
extern void call_module_free(wahe_module_t *ctx, size_t address);
extern char *call_module_func(wahe_module_t *ctx, size_t message_addr, enum wahe_func_id func_id, int call_from_eo);

#ifdef H_ROUZICLIB
extern int wahe_pixel_format_to_raster_mode(const char *name);
extern int wahe_message_to_raster(wahe_module_t *ctx, size_t msg_addr, raster_t *r);
#endif

extern size_t module_vsprintf_alloc(wahe_module_t *ctx, const char *format, va_list args);
extern size_t module_sprintf_alloc(wahe_module_t *ctx, const char* format, ...);
extern char *wahe_send_input(wahe_module_t *ctx, const char *format, ...);
extern void wahe_register_host_commands(wahe_group_t *group);
extern void wahe_module_init(wahe_group_t *parent_group, int module_index, wahe_module_t *ctx, const char *path, size_t runner_count);
extern void wahe_copy_between_memories(wahe_module_t *src_module, size_t src_addr, size_t copy_size, wahe_module_t *dst_module, size_t dst_addr);
extern size_t wahe_copy_message_between_modules_on_runner(wahe_module_t *src_module, const char *src_message, wahe_module_t *dst_module, size_t dst_runner_id);
extern size_t wahe_copy_message_between_modules(wahe_module_t *src_module, const char *src_message, wahe_module_t *dst_module);
#ifdef H_ROUZICLIB
extern void wahe_make_keyboard_mouse_messages(wahe_chain_t *chain, int module_id, int display_id, int conn_id);
#endif

extern char *wahe_run_command_with_id_native(wahe_module_t *ctx, char *message);
#ifdef WAHE_WASMTIME
extern wasm_trap_t *wahe_run_command(void *env, wasmtime_caller_t *caller, const wasmtime_val_t *arg, size_t arg_count, wasmtime_val_t *result, size_t result_count);
#endif // WAHE_WASMTIME
