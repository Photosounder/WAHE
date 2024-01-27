extern size_t alloc_enough_pattern(void **buffer, size_t needed_count, size_t alloc_count, size_t size_elem, double inc_ratio, uint8_t pattern);
#define alloc_enough(b, nc, acp, se, ir)	(*acp) = alloc_enough_pattern(b, nc, (*acp), se, ir, 0)

#define sprintf_alloc(format, ...) sprintf_realloc(NULL, NULL, 0, format, ##__VA_ARGS__)

#define MINN(x, y)   (((x) < (y)) ? (x) : (y))
#define MAXN(x, y)   (((x) > (y)) ? (x) : (y))

#ifdef _WIN32
  #define DIR_CHAR '\\'
#else
  #define DIR_CHAR '/'
#endif

extern char *make_appdata_path(const char *dirname, const char *filename, const int make_subdir);
