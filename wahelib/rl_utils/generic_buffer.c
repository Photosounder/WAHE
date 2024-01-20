char *vbufprintf(buffer_t *s, const char *format, va_list args)		// like vfprintf except for a buffer_t
{
	if (s==NULL)
		return NULL;

	vsprintf_realloc(&s->buf, &s->as, 1, format, args);

	s->len = strlen(s->buf);

	return s->buf;
}

char *bufprintf(buffer_t *s, const char *format, ...)			// like fprintf except for a buffer_t
{
	va_list args;
	int len1;

	if (s==NULL)
		return NULL;

	va_start(args, format);
	vbufprintf(s, format, args);
	va_end(args);

	return s->buf;
}

void buf_alloc_enough(buffer_t *s, size_t req_size)
{
	alloc_enough(&s->buf, req_size, &s->as, sizeof(char), 1.);
}

void clear_buf(buffer_t *s)
{
	if (s==NULL)
		return ;

	s->len = 0;
	if (s->buf && s->as > 0)
		s->buf[0] = '\0';
}

void free_buf(buffer_t *s)
{
	if (s==NULL)
		return ;

	free(s->buf);
	memset(s, 0, sizeof(buffer_t));
}

buffer_t buf_load_raw_file(const char *path)
{
	buffer_t s={0};

	s.buf = load_raw_file(path, &s.len);
	s.as = s.len + 1;

	return s;
}
