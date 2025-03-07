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

	// Re-add already loaded modules to the module symbol list
	for (is=0; is < group->module_count; is++)
		wahe_add_symbol_to_table(&symb_module, make_string_copy(group->module[is].module_name));

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

	// Initialise the shared buffer mutex
	if (init)
		rl_mutex_init(&group->shared_buffer_mutex);

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

			// Add symbol to table
			if (wahe_find_symbol_in_table(&symb_module, module_name) != -1)
				bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Module symbol name \"%s\" already taken.\n", filepath, il, module_name);

			is = wahe_add_symbol_to_table(&symb_module, module_name);

			// Load module
			alloc_enough(&group->module, group->module_count = is+1, &group->module_as, sizeof(wahe_module_t), 1.5);
			if (check_file_is_readable(module_path))
				wahe_module_init(group, is, &group->module[is], module_path);
			else
			{
				char *actual_path = append_name_to_path(NULL, dir_path, module_path);
				wahe_module_init(group, is, &group->module[is], actual_path);
			}
			free(module_path);

			// Store instance name
			group->module[is].wahe_name = make_string_copy(module_name);
		}

		// Sync group
		memset(n, 0, sizeof(n));
		sscanf(line, "Sync group %n%*[^:]%n: %n%*s%n", &n[0], &n[1], &n[2], &n[3]);
		if (n[3])
		{
			char *p = &line[n[2]];
			while (p)
			{
				// Get module name
				n[2] = n[3] = 0;
				sscanf(p, " %n%*s%n", &n[2], &n[3]);
				if (n[3] == 0)
					break;

				char *module_name = make_string_copy_len(&p[n[2]], n[3]-n[2]);
				p = &p[n[3]];

				// Find module
				is = wahe_find_symbol_in_table(&symb_module, module_name);
				if (is == -1)
				{
					bufprintf(err_log, "WAHE file parsing error. In file %s line %d: Module symbol name \"%s\" not previously defined.\n", filepath, il, module_name);
					free(module_name);
					goto end;
				}
				else
					group->module[is].sync_group_name = make_string_copy_len(&line[n[0]], n[1]-n[0]);
				free(module_name);
			}
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

void wahe_group_init(wahe_group_t *group)
{
	// Send an Init message to all modules
	for (int i = 0; i < group->module_count; i++)
		wahe_send_input(&group->module[i], "Init");
}
