char *wahe_execute_chain(wahe_chain_t *chain, const char *input_msg)
{
	wahe_group_t *group = chain->parent_group;
	char *last_msg = NULL;

	wahe_cur_chain = chain;

	// Execute all orders
	for (int ie=0; ie < chain->exec_order_count; ie++)
	{
		wahe_exec_order_t *eo = &chain->exec_order[ie];
		wahe_connection_t *conn = NULL;
		int ic;

		// Find the one connection to this execution order
		if (eo->type == WAHE_EO_KB_MOUSE)				// if this EO takes no inputs we look for the outgoing connection
		{
			for (ic=0; ic < chain->conn_count; ic++)
				if (chain->connection[ic].src_eo == ie)
				{
					conn = &chain->connection[ic];
					break;
				}
		}
		else
		{
			for (int ic=0; ic < chain->conn_count; ic++)
				if (chain->connection[ic].dst_eo == ie)
				{
					conn = &chain->connection[ic];
					break;
				}
		}

		// If the execution order concerns a module function
		if (eo->type == WAHE_EO_MODULE_FUNC)
		{
			wahe_module_t *exec_module = &group->module[eo->module_id];
			wahe_module_t *src_module = NULL, *dst_module = NULL;

			// Handle the connection that feeds into this EO's function
			if (conn)
			{
				// Copy message to module memory
				switch (chain->exec_order[conn->src_eo].type)
				{
					case WAHE_EO_MODULE_FUNC:
						src_module = &group->module[chain->exec_order[conn->src_eo].module_id];
						dst_module = &group->module[chain->exec_order[conn->dst_eo].module_id];

						call_module_free(dst_module, eo->dst_msg_addr);
						eo->dst_msg_addr = 0;

						size_t src_addr = chain->exec_order[conn->src_eo].ret_msg_addr;

						if (src_addr)
						{
							size_t copy_size = strlen(&src_module->memory_ptr[src_addr]) + 1;

							eo->dst_msg_addr = call_module_malloc(dst_module, copy_size);
							wahe_copy_between_memories(src_module, src_addr, copy_size, dst_module, eo->dst_msg_addr);
						}
						break;

					case WAHE_EO_CHAIN_INPUT_MSG:
						dst_module = &group->module[chain->exec_order[conn->dst_eo].module_id];
						call_module_free(dst_module, eo->dst_msg_addr);
						eo->dst_msg_addr = 0;

						if (input_msg)
						{
							size_t copy_size = strlen(input_msg) + 1;
							eo->dst_msg_addr = call_module_malloc(dst_module, copy_size);
							wahe_copy_between_memories(NULL, (size_t) input_msg, copy_size, dst_module, eo->dst_msg_addr);
						}
						break;
					default:
						break;
				}
			}

			// Call function
			chain->current_eo = ie;
			chain->current_cmd_proc_id = 0;
			last_msg = call_module_func(exec_module, eo->dst_msg_addr, eo->func_id, 1);
			chain->current_eo = -1;
		}

		// If the execution order is to put keyboard-mouse messages in a module's memory
		if (eo->type == WAHE_EO_KB_MOUSE && conn)
		{
			#ifdef H_ROUZICLIB
			wahe_make_keyboard_mouse_messages(chain, eo->module_id, eo->display_id, ic);
			#endif
		}

		// If the execution order is to display the image found in a return message
		if (eo->type == WAHE_EO_IMAGE_DISPLAY && conn)
		{
			// Make display image from message in the source module memory
			if (chain->exec_order[conn->src_eo].type == WAHE_EO_MODULE_FUNC)
			{
				wahe_module_t *src_module = &group->module[chain->exec_order[conn->src_eo].module_id];
				size_t src_addr = chain->exec_order[conn->src_eo].ret_msg_addr;

				#ifdef H_ROUZICLIB
				// Make raster from message
				raster_t *r = &group->image[eo->display_id].fb;
				wahe_message_to_raster(src_module, src_addr, r);

				// Calculate actual image rectangle inside the display area
				group->image[eo->display_id].fb_rect = fit_rect_in_area(xyi_to_xy(r->dim), group->image[eo->display_id].fb_area, xy(0.5, 0.5));
				#endif
			}
		}
	}

	return last_msg;
}

void wahe_blit_group_displays(wahe_group_t *group)
{
	#ifdef H_ROUZICLIB
	for (int i=0; i < group->image_count; i++)
		blit_in_rect(&group->image[i].fb, sc_rect(group->image[i].fb_rect), 1, AA_NEAREST_INTERP);
	#endif
}
