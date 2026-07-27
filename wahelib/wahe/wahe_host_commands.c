static int wahe_hcmd_ends_at(const char *line, size_t end)
{
	// Accept commands ending at either the message or the current line
	return line[end] == '\0' || line[end] == '\n';
}

static enum wahe_host_cmd_result wahe_hcmd_init_memory(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	// Extract the initial commit and reservation sizes
	size_t initial_commit_size = 0, reserve_size = 0;
	int end = 0;
	sscanf(*line, "Init memory to %zu - %zu%n", &initial_commit_size, &reserve_size, &end);
	if (end == 0)
		return WAHE_HOST_CMD_NOT_HANDLED;

	// Reject attempts to replace an existing fixed memory reservation
	if (ctx->memory_ptr || ctx->memory_reserve_size)
	{
		fprintf_rl(stderr, "Module %s attempted to initialise virtual memory more than once\n", ctx->module_name);
		return WAHE_HOST_CMD_HANDLED;
	}

	// Allocate the module's first and only memory reservation
	uint8_t *memory = wahe_virtual_memory_alloc(initial_commit_size, reserve_size);

	// Track the reservation for later commit and decommit commands
	if (memory)
	{
		ctx->memory_ptr = memory;
		ctx->memory_size = initial_commit_size;
		ctx->memory_reserve_size = reserve_size;
		*return_msg_addr = (size_t) memory;
		return WAHE_HOST_CMD_RETURN;
	}

	// Report allocation failure
	fprintf_rl(stderr, "Initialising virtual memory of module %s with %zu committed bytes and %zu reserved bytes failed\n", ctx->module_name, initial_commit_size, reserve_size);
	return WAHE_HOST_CMD_HANDLED;
}

static enum wahe_host_cmd_result wahe_hcmd_enlarge_memory(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	(void) return_msg_addr;

	// Extract the requested committed size
	size_t enlarged_commit_size = 0;
	int end = 0;
	sscanf(*line, "Enlarge memory to %zu bytes%n", &enlarged_commit_size, &end);
	if (end == 0)
		return WAHE_HOST_CMD_NOT_HANDLED;

	// Clamp oversized requests to the end of the reservation
	if (ctx->memory_reserve_size && enlarged_commit_size > ctx->memory_reserve_size)
	{
		fprintf_rl(stderr, "Cannot enlarge virtual memory of module %s beyond its %zu-byte reservation\n", ctx->module_name, ctx->memory_reserve_size);
		enlarged_commit_size = ctx->memory_reserve_size;
	}

	// Commit only valid growth within the existing reservation
	if (ctx->memory_reserve_size == 0)
		fprintf_rl(stderr, "Virtual memory of module %s has not been initialised\n", ctx->module_name);
	else if (enlarged_commit_size < ctx->memory_size)
		fprintf_rl(stderr, "Enlarging virtual memory of module %s to %zu bytes would shrink its committed area\n", ctx->module_name, enlarged_commit_size);
	else if (wahe_virtual_memory_commit(ctx->memory_ptr, enlarged_commit_size))
	{
		ctx->memory_size = enlarged_commit_size;
		if (ctx->memory_size_addr)
			*ctx->memory_size_addr = enlarged_commit_size;
	}
	else
		fprintf_rl(stderr, "Enlarging virtual memory of module %s to %zu bytes failed\n", ctx->module_name, enlarged_commit_size);

	return WAHE_HOST_CMD_HANDLED;
}

static enum wahe_host_cmd_result wahe_hcmd_shrink_memory(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	(void) return_msg_addr;

	// Extract the requested committed size
	size_t shrink_size = 0;
	int end = 0;
	sscanf(*line, "Shrink memory to %zu bytes%n", &shrink_size, &end);
	if (end == 0)
		return WAHE_HOST_CMD_NOT_HANDLED;

	// Decommit only valid shrinkage within an existing reservation
	if (ctx->memory_reserve_size == 0)
		fprintf_rl(stderr, "Virtual memory of module %s has not been initialised\n", ctx->module_name);
	else if (shrink_size > ctx->memory_size)
		fprintf_rl(stderr, "Shrinking virtual memory of module %s to %zu bytes would enlarge its committed area\n", ctx->module_name, shrink_size);
	else if (wahe_virtual_memory_decommit(ctx->memory_ptr, shrink_size, ctx->memory_size))
	{
		ctx->memory_size = shrink_size;
		if (ctx->memory_size_addr)
			*ctx->memory_size_addr = shrink_size;
	}
	else
		fprintf_rl(stderr, "Shrinking virtual memory of module %s to %zu bytes failed\n", ctx->module_name, shrink_size);

	return WAHE_HOST_CMD_HANDLED;
}

static enum wahe_host_cmd_result wahe_hcmd_run_chain(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	// Extract the chain name and optional following input
	int input_start = 0, name_start = 0, name_end = 0;
	sscanf(*line, "Run chain %n%*[^\n]%n\n%n", &name_start, &name_end, &input_start);
	if (name_end == 0)
		return WAHE_HOST_CMD_NOT_HANDLED;

	// Find the requested chain
	wahe_group_t *group = ctx->parent_group;
	wahe_chain_t *chain_to_run = NULL;
	char *name = make_string_copy_len(&(*line)[name_start], name_end-name_start);
	for (size_t i=0; i < group->chain_count; i++)
		if (group->chain[i].chain_name && strcmp(group->chain[i].chain_name, name) == 0)
			chain_to_run = &group->chain[i];

	// Execute a chain that was found
	if (chain_to_run)
	{
		// Prefix optional input with the calling module's memory address
		const char *input_msg = NULL;
		char *prefixed_input_msg = NULL;
		if (input_start)
		{
			if (ctx->memory_ptr)
				prefixed_input_msg = sprintf_alloc("From memory %#zx\n%s", (size_t) ctx->memory_ptr, &(*line)[input_start]);
			else
				prefixed_input_msg = sprintf_alloc("%s", &(*line)[input_start]);
			input_msg = prefixed_input_msg;
		}

		// Execute the chain and release its temporary input
		char *end_msg = wahe_execute_chain(chain_to_run, input_msg);
		free(prefixed_input_msg);

		// Find the module whose memory contains the last message
		wahe_module_t *end_module = NULL;
		for (size_t i = chain_to_run->exec_order_count; i > 0; i--)
		{
			if (chain_to_run->exec_order[i-1].type == WAHE_EO_MODULE_FUNC)
			{
				end_module = &group->module[chain_to_run->exec_order[i-1].module_id];
				break;
			}
		}

		// Copy the last message with its source memory address
		if (end_msg)
			*return_msg_addr = wahe_copy_message_between_modules(end_module, end_msg, ctx);
	}
	else
	{
		// Report requests for chains that do not exist
		fprintf_rl(stderr, "Module %s attempted to run unknown chain '%s'\n", ctx->module_name, name);
	}

	// Release the parsed name and return the chain result
	free(name);
	return WAHE_HOST_CMD_RETURN;
}

static enum wahe_host_cmd_result wahe_hcmd_copy(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	(void) return_msg_addr;

	// Extract the copy range and destination
	size_t src_addr = 0, copy_size = 0, dst_addr = 0;
	int end = 0;
	sscanf(*line, "Copy %zi bytes at %zi to %zi%n", &copy_size, &src_addr, &dst_addr, &end);
	if (end == 0)
		return WAHE_HOST_CMD_NOT_HANDLED;

	// Treat an unsuffixed destination as a direct host address
	if (wahe_hcmd_ends_at(*line, end))
	{
		wahe_copy_between_memories(NULL, src_addr, copy_size, NULL, dst_addr);
		return WAHE_HOST_CMD_HANDLED;
	}

	// Recognise a destination relative to the calling module
	int module_suffix_len = 0;
	sscanf(&(*line)[end], " in this module%n", &module_suffix_len);
	if (module_suffix_len && wahe_hcmd_ends_at(*line, end + module_suffix_len))
	{
		wahe_copy_between_memories(NULL, src_addr, copy_size, ctx, dst_addr);
		return WAHE_HOST_CMD_HANDLED;
	}

	return WAHE_HOST_CMD_NOT_HANDLED;
}

static enum wahe_host_cmd_result wahe_hcmd_get_memory_address(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	// Reject text following the hash-matched command
	if (!wahe_hcmd_ends_at(*line, sizeof("Get memory address")-1))
		return WAHE_HOST_CMD_NOT_HANDLED;

	// Return the host address when memory exists
	if (ctx->memory_ptr)
		*return_msg_addr = module_sprintf_alloc(ctx, "%#zx", (size_t) ctx->memory_ptr);
	// Report requests made before memory initialization
	else
		fprintf_rl(stderr, "Module %s requested its memory address before memory was initialised\n", ctx->module_name);
	return WAHE_HOST_CMD_HANDLED;
}

static enum wahe_host_cmd_result wahe_hcmd_get_stack_pointer_address(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	// Reject text following the hash-matched command
	if (!wahe_hcmd_ends_at(*line, sizeof("Get stack pointer address")-1))
		return WAHE_HOST_CMD_NOT_HANDLED;

	// Return the host address when the exported pointer exists
	if (ctx->stack_ptr_addr)
		*return_msg_addr = module_sprintf_alloc(ctx, "%#zx", (size_t) ctx->stack_ptr_addr);
	// Report requests for metadata the module does not provide
	else
		fprintf_rl(stderr, "Module %s requested a stack pointer address that it does not export\n", ctx->module_name);
	return WAHE_HOST_CMD_HANDLED;
}

static enum wahe_host_cmd_result wahe_hcmd_get_heap_base(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	// Reject text following the hash-matched command
	if (!wahe_hcmd_ends_at(*line, sizeof("Get heap base")-1))
		return WAHE_HOST_CMD_NOT_HANDLED;

	// Return the heap base when it exists
	if (ctx->heap_base)
		*return_msg_addr = module_sprintf_alloc(ctx, "%#zx", ctx->heap_base);
	// Report requests for unavailable module metadata
	else
		fprintf_rl(stderr, "Module %s requested a heap base that is unavailable\n", ctx->module_name);
	return WAHE_HOST_CMD_HANDLED;
}

static enum wahe_host_cmd_result wahe_hcmd_get_data_end(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	// Reject text following the hash-matched command
	if (!wahe_hcmd_ends_at(*line, sizeof("Get data end")-1))
		return WAHE_HOST_CMD_NOT_HANDLED;

	// Return the data end when it exists
	if (ctx->data_end)
		*return_msg_addr = module_sprintf_alloc(ctx, "%#zx", ctx->data_end);
	// Report requests for unavailable module metadata
	else
		fprintf_rl(stderr, "Module %s requested a data end that is unavailable\n", ctx->module_name);
	return WAHE_HOST_CMD_HANDLED;
}

static enum wahe_host_cmd_result wahe_hcmd_get_stack_base(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	// Reject text following the hash-matched command
	if (!wahe_hcmd_ends_at(*line, sizeof("Get stack base")-1))
		return WAHE_HOST_CMD_NOT_HANDLED;

	// Return the stack base when it exists
	if (ctx->stack_base)
		*return_msg_addr = module_sprintf_alloc(ctx, "%#zx", ctx->stack_base);
	// Report requests for unavailable module metadata
	else
		fprintf_rl(stderr, "Module %s requested a stack base that is unavailable\n", ctx->module_name);
	return WAHE_HOST_CMD_HANDLED;
}

static enum wahe_host_cmd_result wahe_hcmd_get_stack_pointer(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	// Reject text following the hash-matched command
	if (!wahe_hcmd_ends_at(*line, sizeof("Get stack pointer")-1))
		return WAHE_HOST_CMD_NOT_HANDLED;

	// Read the stack pointer exported by a WASM module
	if (ctx->type == WAHE_MODULE_WASMTIME)
	{
		size_t ptr = wahe_get_module_symbol_address(ctx, "__stack_pointer", 0);
		*return_msg_addr = module_sprintf_alloc(ctx, "%#zx", ptr);
	}
	else
	{
		// Report use of a Wasmtime-only metadata command
		fprintf_rl(stderr, "Module %s requested its Wasmtime stack pointer with module type %d\n", ctx->module_name, ctx->type);
	}
	return WAHE_HOST_CMD_HANDLED;
}

static enum wahe_host_cmd_result wahe_hcmd_get_memory_size(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	// Reject text following the hash-matched command
	if (!wahe_hcmd_ends_at(*line, sizeof("Get memory size")-1))
		return WAHE_HOST_CMD_NOT_HANDLED;

	// Return the active module memory size
	*return_msg_addr = module_sprintf_alloc(ctx, "%#zx", ctx->memory_size);
	return WAHE_HOST_CMD_HANDLED;
}

static enum wahe_host_cmd_result wahe_hcmd_get_memory_size_address(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	// Reject text following the hash-matched command
	if (!wahe_hcmd_ends_at(*line, sizeof("Get memory size address")-1))
		return WAHE_HOST_CMD_NOT_HANDLED;

	// Return either the exported size address or the host-side fallback
	size_t *memory_size_addr = ctx->memory_size_addr ? ctx->memory_size_addr : &ctx->memory_size;
	*return_msg_addr = module_sprintf_alloc(ctx, "%#zx", (size_t) memory_size_addr);
	return WAHE_HOST_CMD_HANDLED;
}

static enum wahe_host_cmd_result wahe_hcmd_load_raw_file(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	// Extract the requested path
	int path_start = 0, path_end = 0;
	sscanf(*line, "Load raw file at path %n%*[^\n]%n", &path_start, &path_end);
	if (path_end == 0)
		return WAHE_HOST_CMD_NOT_HANDLED;

	// Load the file into the calling module
	size_t data_size = 0;
	char *path = make_string_copy_len(&(*line)[path_start], path_end-path_start);
	size_t data_addr = wahe_load_raw_file(ctx, path, &data_size);
	free(path);
	*return_msg_addr = module_sprintf_alloc(ctx, "Data location: %zu bytes at %#zx", data_size, (void *) data_addr);
	return WAHE_HOST_CMD_HANDLED;
}

static enum wahe_host_cmd_result wahe_hcmd_save_raw_file(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	// Extract the requested path and module data range
	int path_start = 0, path_end = 0, end = 0;
	size_t data_addr = 0, data_size = 0;
	sscanf(*line, "Save raw file to path %n%*[^\n]%n\nData location: %zi bytes at %zi%n", &path_start, &path_end, &data_size, &data_addr, &end);
	if (data_addr == 0 || end == 0 || !wahe_hcmd_ends_at(*line, end))
		return WAHE_HOST_CMD_NOT_HANDLED;

	// Save the module data and prepare a result message
	char *path = make_string_copy_len(&(*line)[path_start], path_end-path_start);
	int ret = save_raw_file(path, "wb", &ctx->memory_ptr[data_addr], data_size);
	if (ret == 0)
	{
		// Report file writing failures to both stderr and the module
		fprintf_rl(stderr, "Module %s could not save %zu bytes at offset %#zx to '%s'\n", ctx->module_name, data_size, data_addr, path);
		*return_msg_addr = module_sprintf_alloc(ctx, "Error: Couldn't write to file %s", path);
	}
	else
		*return_msg_addr = module_sprintf_alloc(ctx, "Done.");
	free(path);

	// Skip the consumed data-location line
	*line = strstr_after(*line, "\n");
	return WAHE_HOST_CMD_HANDLED;
}

static enum wahe_host_cmd_result wahe_hcmd_get_raw_time(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	(void) line;

	// Return the current high-resolution time
	*return_msg_addr = module_sprintf_alloc(ctx, "Raw time %.17g seconds", get_time_hr());
	return WAHE_HOST_CMD_HANDLED;
}

#ifdef H_ROUZICLIB
static enum wahe_host_cmd_result wahe_hcmd_mouse_capture(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	(void) ctx;
	(void) line;
	(void) return_msg_addr;

	// Capture and warp the mouse
	mouse.b.orig = zc.offset_u;
	mouse.warp_if_move = 1;
	return WAHE_HOST_CMD_HANDLED;
}

static enum wahe_host_cmd_result wahe_hcmd_mouse_release(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	(void) ctx;
	(void) line;
	(void) return_msg_addr;

	// Release mouse warping
	mouse.b.orig = zc.offset_u;
	mouse.warp_if_move = 0;
	return WAHE_HOST_CMD_HANDLED;
}
#endif

static enum wahe_host_cmd_result wahe_hcmd_benchmark(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	(void) ctx;
	(void) line;
	(void) return_msg_addr;

	// Record the benchmark point
	wahe_bench_point("-Benchmark command-", 0);
	return WAHE_HOST_CMD_HANDLED;
}

static enum wahe_host_cmd_result wahe_hcmd_print(wahe_module_t *ctx, const char **line, size_t *return_msg_addr)
{
	(void) return_msg_addr;

	// Locate the text after the command word
	size_t end = sizeof("Print")-1;
	if ((*line)[end] != ' ' && (*line)[end] != '\n')
		return WAHE_HOST_CMD_NOT_HANDLED;

	// Print the message with its calling module and function
	wahe_chain_t *chain = wahe_cur_chain;
	if (get_string_linecount(&(*line)[end+1], 0) > 1)
		fprintf_rl(stdout, "\n=== from %s:%s ===\n%s\n    ===    ===    \n\n",
				ctx->module_name, chain ? wahe_func_name[chain->current_func] : "(?)", &(*line)[end+1]);
	else
		fprintf_rl(stdout, "(from %s:%s)   %s\n",
				ctx->module_name, chain ? wahe_func_name[chain->current_func] : "(?)", &(*line)[end+1]);
	return WAHE_HOST_CMD_RETURN;
}

void wahe_register_host_commands(wahe_group_t *group)
{
	// Avoid duplicating host registrations when several WAHE files extend one group
	if (group->host_commands_registered)
		return;
	group->host_commands_registered = 1;

	// Register fixed host commands before modules so later module registrations retain precedence
	static const struct
	{
		const char *command;
		wahe_host_cmd_func_t func;
	} command[] =
	{
		{"Init memory to", wahe_hcmd_init_memory},
		{"Enlarge memory to", wahe_hcmd_enlarge_memory},
		{"Shrink memory to", wahe_hcmd_shrink_memory},
		{"Run chain", wahe_hcmd_run_chain},
		{"Copy", wahe_hcmd_copy},
		{"Get memory address", wahe_hcmd_get_memory_address},
		{"Get heap base", wahe_hcmd_get_heap_base},
		{"Get data end", wahe_hcmd_get_data_end},
		{"Get stack base", wahe_hcmd_get_stack_base},
		{"Get stack pointer", wahe_hcmd_get_stack_pointer},
		{"Get stack pointer address", wahe_hcmd_get_stack_pointer_address},
		{"Get memory size", wahe_hcmd_get_memory_size},
		{"Get memory size address", wahe_hcmd_get_memory_size_address},
		{"Load raw file at path", wahe_hcmd_load_raw_file},
		{"Save raw file to path", wahe_hcmd_save_raw_file},
		{"Get raw time", wahe_hcmd_get_raw_time},
		#ifdef H_ROUZICLIB
		{"Mouse capture", wahe_hcmd_mouse_capture},
		{"Mouse release", wahe_hcmd_mouse_release},
		#endif
		{"Benchmark", wahe_hcmd_benchmark},
		{"Print", wahe_hcmd_print}
	};

	// Add each host handler to the shared command registry
	for (size_t i=0; i < sizeof(command)/sizeof(*command); i++)
	{
		wahe_cmd_reg_t *reg = wahe_add_command_registration(group, command[i].command);
		reg->target_type = WAHE_CMD_TARGET_HOST;
		reg->host_func = command[i].func;
	}
}
