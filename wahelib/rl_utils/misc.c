void free_2d(void **ptr, const size_t count)
{
	size_t i;

	if (ptr==NULL)
		return ;

	for (i=0; i < count; i++)
		free(ptr[i]);

	free(ptr);
}

void swap_ptr(void **a, void **b)
{
	void *c = *a;

	*a = *b;
	*b = c;
}

// IO

int check_file_is_readable(const char *path)
{
	FILE *file;
	int ret=0;

	file = fopen_utf8(path, "rb");
	if (file)
	{
		ret = 1;
		fclose(file);
	}

	return ret;
}

uint8_t *load_raw_file(const char *path, size_t *size)
{
	FILE *in_file;
	uint8_t *data;
	size_t fsize;

	if (size)
		*size = 0;

	// Open file handle
	in_file = fopen_utf8(path, "rb");
	if (in_file==NULL)
	{
		fprintf_rl(stderr, "File '%s' not found.\n", path);
		return NULL;
	}

	// Get file size
	fseek(in_file, 0, SEEK_END);
	fsize = ftell(in_file);
	rewind(in_file);

	// Alloc data buffer
	data = calloc(fsize+1, sizeof(uint8_t));

	// Read all the data at once
	int ret = fread(data, 1, fsize, in_file);
	fclose(in_file);

	if (size)
		*size = fsize;

	return data;
}

uint8_t *load_raw_file_dos_conv(const char *path, size_t *size)	// loads raw file but converts "\r\n" to "\n"
{
	FILE *in_file;
	size_t i, offset=0, fsize;
	uint8_t *data, byte0, byte1=0;

	in_file = fopen_utf8(path, "rb");

	if (in_file==NULL)
	{
		fprintf_rl(stderr, "File '%s' not found.\n", path);
		return NULL;
	}

	fseek(in_file, 0, SEEK_END);
	fsize = ftell(in_file);
	rewind(in_file);

	data = calloc(fsize+1, sizeof(uint8_t));

	for (i=0; i < fsize; i++)
	{
		byte0 = byte1;
		int ret = fread(&byte1, 1, 1, in_file);

		if (byte0=='\r' && byte1=='\n')
			offset++;

		data[i-offset] = byte1;
	}

	fclose(in_file);

	uint8_t *ra = realloc(data, fsize-offset+1);
	if (ra)
		data = ra;

	if (size)
		*size = fsize - offset;

	return data;
}

size_t alloc_enough_pattern(void **buffer, size_t needed_count, size_t alloc_count, size_t size_elem, double inc_ratio, uint8_t pattern)
{
	size_t newsize;
	void *p;

	if (needed_count > alloc_count)
	{
		newsize = ceil((double) needed_count * inc_ratio);

		// Try realloc to the new larger size
		p = realloc(*buffer, newsize * size_elem);
		if (p == NULL)
		{
			fprintf_rl(stderr, "realloc(*buffer=%p, size=%zu) failed.\n", (void *) *buffer, newsize * size_elem);
			return alloc_count;
		}
		else
			*buffer = p;

		// Set the new bytes
		memset(&((uint8_t *)(*buffer))[alloc_count * size_elem], pattern, (newsize-alloc_count) * size_elem);

		alloc_count = newsize;
	}

	return alloc_count;
}

void free_null(void **ptr)
{
	if (ptr)
	{
		free(*ptr);
		*ptr = NULL;
	}
}

// String
char *make_string_copy(const char *orig)
{
	char *copy;

	if (orig==NULL)
		return NULL;

	copy = calloc(strlen(orig)+1, sizeof(char));
	strcpy(copy, orig);

	return copy;
}

char *make_string_copy_len(const char *orig, size_t len)
{
	char *copy;

	if (orig==NULL)
		return NULL;

	len = MINN(len, strlen(orig));

	copy = calloc(len+1, sizeof(char));
	strncpy(copy, orig, len);

	return copy;
}

size_t get_string_linecount(const char *text, size_t len)
{
	size_t i, linecount=0;

	if (text==NULL)
		return linecount;

	if (text[0]=='\0')
		return linecount;

	if (len == 0)
		len = strlen(text);

	linecount = 1;
	for (i=0; i < len-1; i++)
		if (text[i] == '\n')
			linecount++;

	return linecount;
}

char **arrayise_text(char *text, int *linecount)	// turns line breaks into null chars, makes an array of pointers to the beginning of each lines
{
	size_t i, ia, len;
	char **array;

	*linecount = 0;
	if (text==NULL)
		return NULL;

	len = strlen(text);
	if (len==0)		// if the text is empty
	{
		array = calloc(1, sizeof(char *));
		array[0] = text;

		return array;
	}

	*linecount = get_string_linecount(text, len);

	array = calloc(*linecount, sizeof(char *));
	array[0] = text;

	// Set the pointers to the start of each line and replace all line breaks with \0
	for (ia=1, i=0; i < len-1; i++)
		if (text[i] == '\n')
		{
			array[ia] = &text[i+1];
			text[i] = '\0';
			ia++;
		}

	if (text[len-1] == '\n')
		text[len-1] = '\0';

	return array;
}

const char *strstr_after(const char *fullstr, const char *substr)		// points to after the substring
{
	const char *p;

	if (fullstr==NULL || substr==NULL)
		return NULL;

	p = strstr(fullstr, substr);
	if (p)
		return &p[strlen(substr)];

	return NULL;
}

int string_count_fields(const char *string, const char *delim)
{
	int count = 0;
	const char *p = string;

	// Count fields
	while (p)
	{
		count++;
		p = strstr_after(p, delim);
	}

	return count;
}

int vstrlenf(const char *format, va_list args)
{
	int len;
	va_list args_copy;

	va_copy(args_copy, args);
	len = vsnprintf(NULL, 0, format, args_copy);	// gets the printed length without actually printing
	va_end(args_copy);

	return len;
}

char *vsprintf_realloc(char **string, size_t *alloc_count, const int append, const char *format, va_list args)
{
	int len0=0, len1;
	size_t zero=0;
	char *p=NULL;

	if (string==NULL)				// if there's no string then we create one
		string = &p;				// so that ultimately it's p that will be returned

	if (alloc_count==NULL)				// if alloc_count isn't provided
		alloc_count = &zero;			// use 0 which will realloc string to an adequate size

	len1 = vstrlenf(format, args);

	if (string)
		if (append && *string)
			len0 = strlen(*string);

	alloc_enough(string, len0+len1+1, alloc_count, sizeof(char), (*alloc_count)==0 ? 1. : 1.5);

	vsnprintf(&(*string)[len0], *alloc_count - len0, format, args);

	return *string;
}

char *sprintf_realloc(char **string, size_t *alloc_count, const int append, const char *format, ...)	// like sprintf but expands the string alloc if needed
{
	va_list args;
	char *p=NULL;

	if (string==NULL)				// if there's no string then we create one
		string = &p;

	va_start(args, format);
	vsprintf_realloc(string, alloc_count, append, format, args);
	va_end(args);

	return *string;
}

int find_string_in_string_array(const char *string, const char **array, const int count)
{
	int i;

	for (i=0; i < count; i++)
		if (strcmp(string, array[i]) == 0)
			return i;

	return -1;
}

// Path
const char *find_last_dirchar(const char *path, int ignore_trailing)
{
	int i, len;

	if (path==NULL)
		return NULL;

	len = strlen(path);
	if (ignore_trailing)
		len -= 1;
	if (len <= 0)
		return path;

	for (i = len-1; i >= 0; i--)
		if (path[i] == '/' || path[i] == '\\')
			return &path[i];

	return NULL;
}

const char *get_filename_from_path(const char *fullpath)	// returns a pointer to the filename in the path
{
	const char *p;

	if (fullpath==NULL)
	{
		fprintf_rl(stderr, "fullpath is NULL in get_filename_from_path()\n");
		return NULL;
	}

	p = find_last_dirchar(fullpath, 0);
	if (p)
		return &p[1];

	return fullpath;
}

char *remove_name_from_path(char *dirpath, const char *fullpath)	// removes the file or dir name after DIR_CHAR and DIR_CHAR itself
{
	const char *p;
	int len;

	if (dirpath)
		dirpath[0] = '\0';

	if (fullpath==NULL)
	{
		fprintf_rl(stderr, "fullpath is NULL in remove_name_from_path()\n");
		return NULL;
	}

	p = find_last_dirchar(fullpath, 1);	// look for the last '\' or '/', ignoring trailing ones
	if (p == NULL)
		return NULL;

	len = p - fullpath;

	if (dirpath==NULL)
		dirpath = calloc(len + 1, sizeof(char));

	memcpy(dirpath, fullpath, len);
	dirpath[len] = 0;

	return dirpath;
}

char *append_name_to_path(char *dest, const char *path, const char *name)	// appends name to path properly regardless of how path is ended
{
	int path_len, name_len, path_has_dirchar=0;

	if (path==NULL && name==NULL)
	{
		if (dest)
			dest[0] = '\0';
		return dest;
	}

	if (name==NULL)
	{
		if (dest)
			sprintf(dest, "%s", path);
		return dest;
	}

	if (path==NULL)
	{
		if (dest)
			sprintf(dest, "%s", name);
		return dest;
	}

	path_len = strlen(path);
	name_len = strlen(name);

	if (path_len > 0)
		if (path[path_len-1] == DIR_CHAR)
			path_has_dirchar = 1;

	if (dest==NULL)
		dest = calloc(path_len + name_len + 2, sizeof(char));

	if (dest==path)		// in-place appending
	{
		if (path_has_dirchar)
			sprintf(&dest[path_len], "%s", name);
		else
			sprintf(&dest[path_len], "%c%s", DIR_CHAR, name);
	}
	else
	{
		if (path_has_dirchar)
			sprintf(dest, "%s%s", path, name);
		else
			sprintf(dest, "%s%c%s", path, DIR_CHAR, name);
	}

	return dest;
}

uint8_t *sprint_unicode(uint8_t *str, uint32_t c)	// str must be able to hold 1 to 5 bytes and will be null-terminated by this function
{
	const uint8_t m6 = 63;
	const uint8_t	c10x	= 0x80,
	      		c110x	= 0xC0,
	      		c1110x	= 0xE0,
	      		c11110x	= 0xF0;

	if (c < 0x0080)
	{
		str[0] = c;
		if (c > 0)
			str[1] = '\0';
	}
	else if (c < 0x0800)
	{
		str[1] = (c & m6) | c10x;
		c >>= 6;
		str[0] = c | c110x;
		str[2] = '\0';
	}
	else if (c < 0x10000)
	{
		str[2] = (c & m6) | c10x;
		c >>= 6;
		str[1] = (c & m6) | c10x;
		c >>= 6;
		str[0] = c | c1110x;
		str[3] = '\0';
	}
	else if (c < 0x200000)
	{
		str[3] = (c & m6) | c10x;
		c >>= 6;
		str[2] = (c & m6) | c10x;
		c >>= 6;
		str[1] = (c & m6) | c10x;
		c >>= 6;
		str[0] = c | c11110x;
		str[4] = '\0';
	}
	else
		str[0] = '\0';		// Unicode character doesn't map to UTF-8

	return str;
}

int codepoint_utf8_size(const uint32_t c)
{
	if (c < 0x0080) return 1;
	if (c < 0x0800) return 2;
	if (c < 0x10000) return 3;
	if (c < 0x110000) return 4;

	return 0;
}

int utf16_char_size(const uint16_t *c)
{
	if (c[0] <= 0xD7FF || c[0] >= 0xE000)
		return 1;
	else if (c[1]==0)			// if there's an abrupt mid-character stream end
		return 1;
	else
		return 2;
}

uint32_t utf16_to_unicode32(const uint16_t *c, size_t *index)
{
	uint32_t v;
	size_t size;

	size = utf16_char_size(c);

	if (size > 0 && index)
		*index += size-1;

	switch (size)
	{
		case 1:
			v = c[0];
			break;
		case 2:
			v = c[0] & 0x3FF;
			v = v << 10 | (c[1] & 0x3FF);
			v += 0x10000;
			break;
	}

	return v;
}

size_t strlen_utf16_to_utf8(const uint16_t *str)
{
	size_t i, count;
	uint32_t c;

	for (i=0, count=0; ; i++)
	{
		if (str[i]==0)
			return count;

		c = utf16_to_unicode32(&str[i], &i);
		count += codepoint_utf8_size(c);
	}
}

#define wchar_to_utf8 utf16_to_utf8

uint8_t *utf16_to_utf8(const uint16_t *utf16, uint8_t *utf8)
{
	size_t i, j;
	uint32_t c;

	if (utf16==NULL)
		return NULL;

	if (utf8==NULL)
		utf8 = calloc(strlen_utf16_to_utf8(utf16) + 1, sizeof(uint8_t));

	for (i=0, j=0, c=1; c; i++)
	{
		c = utf16_to_unicode32(&utf16[i], &i);
		sprint_unicode(&utf8[j], c);
		j += codepoint_utf8_size(c);
	}

	return utf8;
}

#ifdef _WIN32
#ifndef SHFOLDERAPI
#if defined(_SHFOLDER_) || defined(_SHELL32_)
#define SHFOLDERAPI           STDAPI
#else
#define SHFOLDERAPI           EXTERN_C DECLSPEC_IMPORT HRESULT STDAPICALLTYPE
#endif
#endif
SHFOLDERAPI SHGetFolderPathW(_Reserved_ HWND hwnd, _In_ int csidl, _In_opt_ HANDLE hToken, _In_ DWORD dwFlags, _Out_writes_(MAX_PATH) LPWSTR pszPath);
#endif

char *win_get_system_folder_path(int csidl)
{
	char *path = NULL;

	#ifdef _WIN32
	wchar_t path_w[MAX_PATH];

	SHGetFolderPathW(NULL, csidl, NULL, 0, path_w);
	//SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, NULL, path_w);
	path = wchar_to_utf8(path_w, NULL);
	#endif

	return path;
}

#ifdef __APPLE__
#include <glob.h>
#endif

char *make_appdata_path(const char *dirname, const char *filename, const int make_subdir)
{
	char *path=NULL, *dirpath=NULL;

	#ifdef _WIN32

	char *origpath = win_get_system_folder_path(0x001a /*CSIDL_APPDATA*/);

	#elif defined(__APPLE__)

	glob_t globbuf;
	char *origpath=NULL;

	if (glob("~/Library/Application Support/", GLOB_TILDE | GLOB_MARK, NULL, &globbuf)==0)	// globbuf.gl_pathv[0] is the path
		origpath = globbuf.gl_pathv[0];
	else
	{
		globfree(&globbuf);
		return NULL;
	}

	#else
	char origpath[] = "~/.config/";
	#endif

	dirpath = append_name_to_path(NULL, origpath, dirname);

	if (make_subdir)
		create_dir(dirpath);

	if (filename)
	{
		path = append_name_to_path(NULL, dirpath, filename);
		free(dirpath);
	}
	else
		path = dirpath;

	#ifdef _WIN32
	free(origpath);
	#endif

	#ifdef __APPLE__
	globfree(&globbuf);
	#endif

	return path;
}

int create_dir(const char *path)	// returns 0 if successful
{
	#ifdef _WIN32
	wchar_t wpath[MAX_PATH*2];

	utf8_to_wchar(path, wpath);
	return CreateDirectoryW(wpath, NULL) == 0;

	#else

	return mkdir(path, 0755);
	#endif
}

// Hashing
#include "xxh64.c"

uint64_t get_buffer_hash(const void *ptr, size_t size)
{
	return XXH64(ptr, size, 0);
}

uint64_t get_string_hash(const char *string)
{
	return get_buffer_hash(string, strlen(string));
}
