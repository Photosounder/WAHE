// https://webassembly.github.io/spec/core/binary/modules.html

int check_if_file_is_wasm(const char *path)
{
	char buffer[4] = {0};
	const char sig[4] = {'\0', 'a', 's', 'm'};

	FILE *file = fopen_utf8(path, "rb");
	if (file == NULL)
		return 0;

	fread(buffer, 1, sizeof(buffer), file);

	return memcmp(sig, buffer, sizeof(buffer)) == 0;
}

size_t wasmbin_jump_to_section(FILE *file, uint8_t section_id)
{
	// Get file size then rewind
	fseek_override(file, 0, SEEK_END);
	size_t file_size = ftell_override(file);
	fseek_override(file, 8, SEEK_SET);

	// Go through sections
	do
	{
		// Read the section ID and its length
		uint8_t id = fread_byte8(file);
		size_t section_len = (uint64_t) fread_LEB128(file, 0);

		if (id == section_id)
			return section_len;

		// Skip to the next section
		fseek_override(file, section_len, SEEK_CUR);
	}
	while (ftell_override(file) < file_size);

	return 0;
}

size_t wasmbin_read_stack_pointer(FILE *file)
{
	size_t stack = 0;

	// Jump to Global section
	wasmbin_jump_to_section(file, 6);

	fread_LEB128(file, 0);	// global variables count
	fread_byte8(file);	// type opcode, should be 0x7F for i32
	fread_byte8(file);	// mutability, 1 for stack, 0 for the others
	fread_byte8(file);	// 'i32 const' opcode (0x41)

	stack = fread_LEB128(file, 1);

	fread_byte8(file);	// 'end' opcode (0x0B)

	return stack;
}

static void wasmbin_skip_name(FILE *file)
{
	// Skip a length-prefixed UTF-8 name
	size_t length = (size_t) fread_LEB128(file, 0);
	fseek_override(file, length, SEEK_CUR);
}

static void wasmbin_read_memory_type(FILE *file, wasmbin_memory_info_t *info)
{
	// Decode the memory limits flags and page counts
	uint32_t flags = (uint32_t) fread_LEB128(file, 0);
	info->maximum_present = !!(flags & 1);
	info->shared = !!(flags & 2);
	info->memory64 = !!(flags & 4);
	info->base_pages = (uint32_t) fread_LEB128(file, 0);
	if (info->maximum_present)
		info->max_pages = (uint32_t) fread_LEB128(file, 0);
}

static void wasmbin_skip_table_type(FILE *file)
{
	// Skip the reference type used by the table
	fread_byte8(file);

	// Skip the table limits
	uint32_t flags = (uint32_t) fread_LEB128(file, 0);
	fread_LEB128(file, 0);
	if (flags & 1)
		fread_LEB128(file, 0);
}

int wasmbin_read_memory_info(FILE *file, wasmbin_memory_info_t *info)
{
	// Reject missing output storage
	if (info == NULL)
		return 0;

	// Start with a deterministic empty result
	memset(info, 0, sizeof(*info));

	// Look for an imported memory first
	if (wasmbin_jump_to_section(file, 2))
	{
		size_t import_count = (size_t) fread_LEB128(file, 0);
		for (size_t i = 0; i < import_count; i++)
		{
			// Skip the import module and field names
			wasmbin_skip_name(file);
			wasmbin_skip_name(file);

			// Decode or skip the imported external type
			uint8_t kind = fread_byte8(file);
			switch (kind)
			{
				case 0:
					fread_LEB128(file, 0);
					break;

				case 1:
					wasmbin_skip_table_type(file);
					break;

				case 2:
					wasmbin_read_memory_type(file, info);
					info->imported = 1;
					return 1;

				case 3:
					fread_byte8(file);
					fread_byte8(file);
					break;

				case 4:
					fread_byte8(file);
					fread_LEB128(file, 0);
					break;

				default:
					return 0;
			}
		}
	}

	// Fall back to a memory defined by the module
	if (!wasmbin_jump_to_section(file, 5))
		return 0;

	// Read the first defined memory type
	if (fread_LEB128(file, 0) == 0)
		return 0;
	wasmbin_read_memory_type(file, info);
	return 1;
}

void wasmbin_read_memory_size(FILE *file, uint32_t *base_pages, uint32_t *max_pages)
{
	// Preserve the original page-count API on top of the full memory parser
	wasmbin_memory_info_t info;
	if (!wasmbin_read_memory_info(file, &info))
	{
		*base_pages = 0;
		*max_pages = 0;
		return;
	}

	// Return the decoded memory limits
	*base_pages = info.base_pages;
	*max_pages = info.max_pages;
}
