// https://webassembly.github.io/spec/core/binary/modules.html

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

void wasmbin_read_memory_size(FILE *file, uint32_t *base_pages, uint32_t *max_pages)
{
	// Jump to Global section
	wasmbin_jump_to_section(file, 5);

	fread_LEB128(file, 0);	// memory limit count (1 expected)
	fread_byte8(file);	// limit type (type 1 'min-max' expected)
	*base_pages = (uint64_t) fread_LEB128(file, 0);
	*max_pages = (uint64_t) fread_LEB128(file, 0);
}
