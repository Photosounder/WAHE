// File read

uint8_t fread_byte8(FILE *file)
{
	uint8_t	b;
	fread_override(&b, 1, 1, file);
	return b;
}

int64_t fread_LEB128(FILE *file, int signed_leb)
{
	uint64_t v = 0;
	int shift = 0;
	uint8_t b;

	// Add up the 7 bits of each byte
	do
	{
		b = fread_byte8(file);
		v |= (uint64_t) (b & 0x7F) << shift;
		shift += 7;
	}
	while (b & 0x80);

	// If we're decoding a signed number the sign is given by the bit at 0x40
	if (signed_leb && b & 0x40)
		v |= (~0ULL << shift);		// this extends the sign

	return (int64_t) v;
}

// File write
void fwrite_byte8(FILE *file, uint8_t s)
{
	fwrite_override(&s, 1, 1, file);
}

void fwrite_ULEB128(FILE *file, uint64_t v)
{
	do
	{
		uint8_t b = v & 0x7F;

		v >>= 7;
		if (v)
			b |= 0x80;

		fwrite_byte8(file, b);
	}
	while (v);
}

void fwrite_SLEB128(FILE *file, int64_t v)
{
	uint8_t b;

	do
	{
		b = v & 0x7F;

		v >>= 7;
		if ((v != 0 || b & 0x40) && (v != -1 || (b & 0x40) == 0))
			b |= 0x80;

		fwrite_byte8(file, b);
	}
	while (b & 0x80);
}

// Buffer read
uint8_t read_byte8(const void *ptr, size_t *index)	// used when function pointers are needed
{
	const uint8_t *buf = ptr;
	if (index)
		*index += sizeof(uint8_t);

	return buf[0];
}

uint32_t read_LE32(const void *ptr, size_t *index)
{
	const uint8_t *buf = ptr;
	if (index)
		*index += sizeof(uint32_t);

#ifdef ASS_LE
	return *((uint32_t *) buf);
#else
	return (uint32_t) (buf[3] << 24) | (buf[2] << 16) | (buf[1] << 8) | buf[0];
#endif
}

uint64_t read_LE64(const void *ptr, size_t *index)
{
	const uint8_t *buf = ptr;
	if (index)
		*index += sizeof(uint64_t);

#ifdef ASS_LE
	return *((uint64_t *) buf);
#else
	return ((uint64_t) read_LE32(&buf[4], NULL) << 32) | read_LE32(buf, NULL);
#endif
}
