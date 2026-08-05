typedef struct
{
	uint64_t hash;
	char *name;
} wahe_symbol_name_t;

typedef struct
{
	wahe_symbol_name_t *symbol;
	size_t symbol_count, symbol_as;
} wahe_symbol_table_t;

int wahe_add_symbol_to_table(wahe_symbol_table_t *table, char *name)
{
	int is;

	is = table->symbol_count;
	alloc_enough(&table->symbol, table->symbol_count+=1, &table->symbol_as, sizeof(wahe_symbol_name_t), 1.5);
	table->symbol[is].name = name;
	table->symbol[is].hash = get_string_hash(name);

	return is;
}

int wahe_find_symbol_in_table(wahe_symbol_table_t *table, char *name)
{
	int is;
	uint64_t hash = get_string_hash(name);

	for (is=0; is < table->symbol_count; is++)
		if (hash == table->symbol[is].hash)
			if (strcmp(name, table->symbol[is].name) == 0)
				return is;

	return -1;
}

void wahe_symbol_table_free(wahe_symbol_table_t *table)
{
	for (int is=0; is < table->symbol_count; is++)
		free(table->symbol[is].name);
	free(table->symbol);
	memset(table, 0, sizeof(wahe_symbol_table_t));
}

#define WAHE_MODULE_RESERVE_COUNT ((size_t) 4096)

static int wahe_alloc_stable_module_slots(wahe_group_t *group, size_t needed_count)
{
	// Reject module counts outside the fixed address reservation
	if (needed_count > WAHE_MODULE_RESERVE_COUNT)
	{
		fprintf_rl(stderr, "Cannot allocate %zu modules beyond the %zu-module reservation\n", needed_count, WAHE_MODULE_RESERVE_COUNT);
		return 0;
	}

	// Keep spare committed slots while staying inside the reservation
	size_t new_alloc_count = needed_count + needed_count / 2 + 1;
	new_alloc_count = MINN(new_alloc_count, WAHE_MODULE_RESERVE_COUNT);

	// Reserve the module array once so initialized module addresses never move
	if (group->module == NULL)
	{
		group->module = wahe_virtual_memory_alloc(new_alloc_count * sizeof(*group->module), WAHE_MODULE_RESERVE_COUNT * sizeof(*group->module));
		if (group->module == NULL)
			return 0;
		group->module_as = new_alloc_count;
		return 1;
	}

	// Commit a larger prefix without changing the reserved base address
	if (needed_count > group->module_as)
	{
		if (!wahe_virtual_memory_commit(group->module, new_alloc_count * sizeof(*group->module)))
			return 0;
		group->module_as = new_alloc_count;
	}
	return 1;
}

void wahe_file_parse(wahe_group_t *group, char *filepath, buffer_t *err_log)
{
	// group has to be a pointer with a fixed location so that pointers to it in the struct wouldn't be dereferenced
	int i, n[4], is, il, linecount;
	#ifdef H_ROUZICLIB
	int image_offset = group->image_count;
	#endif
	char *line, **line_array = arrayise_text(load_raw_file_dos_conv(filepath, NULL), &linecount);
	wahe_symbol_table_t symb_module={0}, symb_display={0}, symb_order={0};
	wahe_chain_t *chain = NULL;
	int init = 0;

	// Register host commands before modules so module registrations keep precedence
	wahe_register_host_commands(group);

	// Re-add already loaded modules to the module symbol list
	for (is=0; is < group->module_count; is++)
		wahe_add_symbol_to_table(&symb_module, make_string_copy(group->module[is].wahe_name));

	// Check if group needs to be initialised
	if (group->chain_count == 0)
		init = 1;

	// Get path that the .wahe file is in to access modules from there
	char *dir_path = remove_name_from_path(NULL, filepath);

	// Start by allocating or pointing to the default chain used during module initialisation
	if (init)
		alloc_enough(&group->chain, group->chain_count+=1, &group->chain_as, sizeof(wahe_chain_t), 1.5);
	chain = &group->chain[0];
	chain->parent_group = group;
	wahe_cur_chain = chain;

	// Go through each line
	for (il=0; il < linecount; il++)
	{
		line = line_array[il];

		// Load module
		memset(n, 0, sizeof(n));
		sscanf(line, "Module %n%*[^:]%n: \"%n%*[^\"]%n", &n[0], &n[1], &n[2], &n[3]);
		if (n[3])
		{
			char *module_path = make_string_copy_len(&line[n[2]], n[3]-n[2]);
			char *module_name = make_string_copy_len(&line[n[0]], n[1]-n[0]);

			// Reject declarations without the closing path quote
			if (line[n[3]] != '"')
			{
				bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Module path is missing its closing quote in \"%s\".\n", filepath, il, line);
				free(module_path);
				free(module_name);
				continue;
			}

			// Parse the optional Wasmtime runner count
			size_t runner_count = 1;
			int runner_count_end = 0;
			int runner_count_valid = 1;
			const char *module_suffix = &line[n[3]+1];
			if (strstr(module_suffix, ", runners") == module_suffix)
			{
				sscanf(module_suffix, ", runners %zu%n", &runner_count, &runner_count_end);
				const char *runner_text = &module_suffix[sizeof(", runners")-1];
				while (*runner_text == ' ' || *runner_text == '\t')
					runner_text++;
				if (*runner_text < '0' || *runner_text > '9' || runner_count_end == 0)
					runner_count_valid = 0;
				for (const char *end = &module_suffix[runner_count_end]; *end; end++)
					if (*end != ' ' && *end != '\t' && *end != '\r')
						runner_count_valid = 0;
			}
			if (runner_count == 0 || !runner_count_valid)
			{
				bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Invalid module runner count in \"%s\".\n", filepath, il, line);
				free(module_path);
				free(module_name);
				continue;
			}

			// Add symbol to table
			if (wahe_find_symbol_in_table(&symb_module, module_name) != -1)
				bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Module symbol name \"%s\" already taken.\n", filepath, il, module_name);

			is = wahe_add_symbol_to_table(&symb_module, module_name);

			// Grow the module array without invalidating initialized module addresses
			if (!wahe_alloc_stable_module_slots(group, is + 1))
			{
				bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Cannot allocate storage for module \"%s\".\n", filepath, il, module_name);
				free(module_path);
				goto end;
			}
			group->module_count = is + 1;

			// Load module
			if (check_file_is_readable(module_path))
				wahe_module_init(group, is, &group->module[is], module_path, runner_count);
			else
			{
				char *actual_path = append_name_to_path(NULL, dir_path, module_path);
				wahe_module_init(group, is, &group->module[is], actual_path, runner_count);
			}
			free(module_path);

			// Store instance name
			group->module[is].wahe_name = make_string_copy(module_name);
		}

		// Set display
		#ifdef H_ROUZICLIB
		memset(n, 0, sizeof(n));
		xy_t pos, size, offset;
		if (sscanf(line, "Display %n%*[^:]%n: pos %lg %lg, size %lg %lg, offset %lg %lg", &n[0], &n[1], &pos.x, &pos.y, &size.x, &size.y, &offset.x, &offset.y) == 6)
		{
			char *display_name = make_string_copy_len(&line[n[0]], n[1]-n[0]);

			// Add symbol to table
			if (wahe_find_symbol_in_table(&symb_display, display_name) != -1)
				bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Display symbol name \"%s\" already taken.\n", filepath, il, display_name);

			is = wahe_add_symbol_to_table(&symb_display, display_name) + image_offset;

			// Add display
			alloc_enough(&group->image, group->image_count = is+1, &group->image_as, sizeof(wahe_image_display_t), 1.5);
			group->image[is].fb_area = make_rect_off(pos, size, offset);
		}
		#endif

		// Send to
		memset(n, 0, sizeof(n));
		sscanf(line, "Send to %n%*[^:]%n: %n", &n[0], &n[1], &n[2]);
		int send_lines = 0;
		if (n[2] == 0)
			sscanf(line, "Send %d lines to %n%*[^:]%n:%n", &send_lines, &n[0], &n[1], &n[2]);
		if (n[2])
		{
			// Find module
			char *module_name = make_string_copy_len(&line[n[0]], n[1]-n[0]);
			is = wahe_find_symbol_in_table(&symb_module, module_name);
			if (is == -1)
			{
				bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Module symbol name \"%s\" not previously defined.\n", filepath, il, module_name);
				free(module_name);
				goto end;
			}
			free(module_name);

			// Send lines to the module
			if (send_lines == 0)
				wahe_send_input(&group->module[is], "%s", &line[n[2]]);
			else
			{
				buffer_t buf = {0};

				for (i=il+1; i < linecount && i-(il+1) < send_lines; i++)
					bufprintf(&buf, "%s\n", line_array[i]);

				wahe_send_input(&group->module[is], "%s", buf.buf);
				free_buf(&buf);

				il += send_lines;
			}
		}

		// Chain
		memset(n, 0, sizeof(n));
		sscanf(line, "Chain %n%*[^\n]%n", &n[0], &n[1]);
		if (n[1])
		{
			// Free EO symbol table
			wahe_symbol_table_free(&symb_order);

			// Alloc chain
			alloc_enough(&group->chain, group->chain_count+=1, &group->chain_as, sizeof(wahe_chain_t), 1.5);
			wahe_cur_chain = &group->chain[0];
			chain = &group->chain[group->chain_count-1];
			chain->chain_name = make_string_copy_len(&line[n[0]], n[1]-n[0]);
			chain->parent_group = group;

			// Add chain_input_msg EO
			is = wahe_add_symbol_to_table(&symb_order, make_string_copy("chain_input_msg"));
			alloc_enough(&chain->exec_order, chain->exec_order_count = is+1, &chain->exec_order_as, sizeof(wahe_exec_order_t), 1.5);
			chain->exec_order[is].type = WAHE_EO_CHAIN_INPUT_MSG;	// the type is the only thing needed since such EOs don't do anything
		}

		// Execution orders
		memset(n, 0, sizeof(n));
		sscanf(line, "Order %n%*[^:]%n: %n", &n[0], &n[1], &n[2]);
		if (n[2])
		{
			char *order_name = make_string_copy_len(&line[n[0]], n[1]-n[0]);

			// Add symbol to table
			if (wahe_find_symbol_in_table(&symb_order, order_name) != -1)
				bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Order symbol name \"%s\" already taken.\n", filepath, il, order_name);

			is = wahe_add_symbol_to_table(&symb_order, order_name);

			// Add execution order
			alloc_enough(&chain->exec_order, chain->exec_order_count = is+1, &chain->exec_order_as, sizeof(wahe_exec_order_t), 1.5);

			// Go through the order's arguments
			char *p = &line[n[2]];
			while (p)
			{
				memset(n, 0, sizeof(n));
				char attribute[16];
				sscanf(p, "%15s %n%*[^,]%n, %n", attribute, &n[0], &n[1], &n[2]);
				char *arg_name = make_string_copy_len(&p[n[0]], n[1]-n[0]);

				if (n[1] == 0)
					break;

				// Set order type
				if (strcmp(attribute, "type") == 0)
				{
					chain->exec_order[is].type = find_string_in_string_array(arg_name, wahe_eo_name, sizeof(wahe_eo_name)/sizeof(*wahe_eo_name));
					if (chain->exec_order[is].type == -1)
						bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Order type attribute \"%s\" not previously defined.\n", filepath, il, arg_name);
				}

				// Set module
				if (strcmp(attribute, "module") == 0)
				{
					chain->exec_order[is].module_id = wahe_find_symbol_in_table(&symb_module, arg_name);
					if (chain->exec_order[is].module_id == -1)
						bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Order module attribute \"%s\" not previously defined.\n", filepath, il, arg_name);
				}

				// Set module function to call
				if (strcmp(attribute, "func") == 0)
				{
					chain->exec_order[is].func_id = find_string_in_string_array(arg_name, wahe_func_name, sizeof(wahe_func_name)/sizeof(*wahe_func_name));
					if (chain->exec_order[is].func_id == -1)
						bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Order function attribute \"%s\" not previously defined.\n", filepath, il, arg_name);
				}

				// Set the zero-indexed module runner
				if (strcmp(attribute, "runner") == 0)
				{
					int runner_end = 0;
					sscanf(arg_name, "%zu%n", &chain->exec_order[is].runner_id, &runner_end);
					if (arg_name[0] < '0' || arg_name[0] > '9' || runner_end == 0 || arg_name[runner_end] != '\0')
						bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Order runner attribute \"%s\" is not a zero-indexed integer.\n", filepath, il, arg_name);
				}

				// Set image display
				if (strcmp(attribute, "display") == 0)
				{
					chain->exec_order[is].display_id = wahe_find_symbol_in_table(&symb_display, arg_name);
					if (chain->exec_order[is].display_id == -1)
						bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Order display attribute \"%s\" not previously defined.\n", filepath, il, arg_name);
				}

				free_null(&arg_name);
				p = &p[n[2]];

				if (n[2] == 0)
					break;
			}

			// Validate the selected runner after all order attributes are known
			wahe_exec_order_t *eo = &chain->exec_order[is];
			if (eo->type == WAHE_EO_MODULE_FUNC && eo->module_id >= 0 && (size_t) eo->module_id < group->module_count &&
				eo->runner_id >= group->module[eo->module_id].runner_count)
				bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Runner %zu is outside module %s's %zu runners.\n",
					filepath, il, eo->runner_id, group->module[eo->module_id].wahe_name, group->module[eo->module_id].runner_count);
		}

		// Connections between orders
		memset(n, 0, sizeof(n));
		sscanf(line, "Connection %n%*s%n - %n%*s%n", &n[0], &n[1], &n[2], &n[3]);
		if (n[3])
		{
			is = chain->conn_count;

			// Add connection
			alloc_enough(&chain->connection, chain->conn_count+=1, &chain->conn_as, sizeof(wahe_connection_t), 1.5);

			char *src_name = make_string_copy_len(&line[n[0]], n[1]-n[0]);
			char *dst_name = make_string_copy_len(&line[n[2]], n[3]-n[2]);

			// Set source and destination execution orders
			chain->connection[is].src_eo = wahe_find_symbol_in_table(&symb_order, src_name);
			chain->connection[is].dst_eo = wahe_find_symbol_in_table(&symb_order, dst_name);

			if (chain->connection[is].src_eo == -1)
				bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Connection source order \"%s\" not previously defined.\n", filepath, il, src_name);

			if (chain->connection[is].dst_eo == -1)
				bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Connection destination order \"%s\" not previously defined.\n", filepath, il, dst_name);

			free_null(&src_name);
			free_null(&dst_name);
		}

		// Command processors
		memset(n, 0, sizeof(n));
		sscanf(line, "Command processor in module %n%*s%n for order %n%*s%n", &n[0], &n[1], &n[2], &n[3]);
		if (n[3])
		{
			// Find execution order
			char *order_name = make_string_copy_len(&line[n[2]], n[3]-n[2]);
			int ie = wahe_find_symbol_in_table(&symb_order, order_name);
			if (ie == -1)
			{
				bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Order symbol name \"%s\" not previously defined.\n", filepath, il, order_name);
				free(order_name);
				goto end;
			}
			free(order_name);
			wahe_exec_order_t *eo = &chain->exec_order[ie];

			// Add exec order command processor
			int ip = eo->cmd_proc_count;
			alloc_enough(&eo->cmd_proc_id, eo->cmd_proc_count+=1, &eo->cmd_proc_as, sizeof(int), 1.5);

			// Find command processing module
			char *proc_module_name = make_string_copy_len(&line[n[0]], n[1]-n[0]);
			eo->cmd_proc_id[ip] = wahe_find_symbol_in_table(&symb_module, proc_module_name);
			if (eo->cmd_proc_id[ip] == -1)
			{
				bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Module symbol name \"%s\" not previously defined.\n", filepath, il, proc_module_name);
				free(proc_module_name);
				goto end;
			}
			free(proc_module_name);
		}
	}

end:
	wahe_symbol_table_free(&symb_module);
	wahe_symbol_table_free(&symb_display);
	wahe_symbol_table_free(&symb_order);
	free_2d(line_array, 1);
}

void wahe_group_create_cita_index(wahe_group_t *group)
{
	static const char cita_index_magic[] = "WAHE CITA Index";
	enum { cita_index_header_size = 32, cita_index_entry_field_count = 8 };
	size_t linear_memory_count = 0;

	// Count only modules that expose a linear-memory base address
	for (size_t i=0; i < group->module_count; i++)
		if (group->module[i].memory_ptr)
			linear_memory_count++;

	// Reject index sizes that cannot fit in the host address space
	if (linear_memory_count > (SIZE_MAX - cita_index_header_size) / (cita_index_entry_field_count * sizeof(uint64_t)))
	{
		fprintf_rl(stderr, "Cannot create CITA index for %zu linear-memory modules because its size would overflow\n", linear_memory_count);
		return;
	}
	size_t index_size = cita_index_header_size + linear_memory_count * cita_index_entry_field_count * sizeof(uint64_t);

	// Release the previous index before replacing it during group reinitialisation
	if (group->cita_index)
		wahe_virtual_memory_free(group->cita_index, group->cita_index_size);
	group->cita_index = NULL;
	group->cita_index_size = 0;

	// Reserve and commit the complete process-scannable index range
	uint8_t *index = wahe_virtual_memory_alloc(index_size, index_size);
	if (index == NULL)
		return;
	memset(index, 0, index_size);
	group->cita_index = index;
	group->cita_index_size = index_size;

	// Write the fixed header and number of indexed modules
	memcpy(index, cita_index_magic, sizeof(cita_index_magic));
	uint64_t linear_memory_count_u64 = linear_memory_count;
	memcpy(&index[16], &linear_memory_count_u64, sizeof(linear_memory_count_u64));

	// Write each linear-memory module's fixed metadata and live-value sources
	uint64_t *entry = (uint64_t *) &index[cita_index_header_size];
	for (size_t i=0; i < group->module_count; i++)
	{
		wahe_module_t *ctx = &group->module[i];
		if (ctx->memory_ptr == NULL)
			continue;

		entry[0] = (uint64_t) (uintptr_t) ctx->module_name;
		entry[1] = (uint64_t) (uintptr_t) ctx->wahe_name;
		entry[2] = (uint64_t) (uintptr_t) ctx->memory_ptr;
		entry[3] = (uint64_t) ctx->heap_base;
		entry[4] = (uint64_t) ctx->stack_base;
		entry[5] = (uint64_t) ctx->data_end;
		entry[6] = (uint64_t) (uintptr_t) (ctx->memory_size_addr ? ctx->memory_size_addr : &ctx->memory_size);
		entry[7] = (uint64_t) (uintptr_t) ctx->stack_ptr_addr;
		entry += cita_index_entry_field_count;
	}
}

void wahe_group_init(wahe_group_t *group)
{
	// Send an Init message to all modules
	for (int i = 0; i < group->module_count; i++)
		wahe_send_input(&group->module[i], "Init");

	// Create the CITA index after module initialization established linear memory
	wahe_group_create_cita_index(group);
}
