// Translating Wasm modules to native DLLs with wasm2native.dll, enabled by defining WAHE_WASM2NATIVE

#ifdef WAHE_WASM2NATIVE
  #ifdef _WIN32
    #include <process.h>
  #else
    #include <pthread.h>
  #endif
#endif

static const char *wahe_wasm2native_mode_name[] = { "off", "cached", "auto", "always" };

static struct
{
	int mode_set;
	enum wahe_wasm2native_mode mode;
	char *cache_dir, *lib_path;
} wahe_wasm2native_settings;

int wahe_wasm2native_parse_mode(const char *name, enum wahe_wasm2native_mode *mode)
{
	// Match one of the mode names case-sensitively
	for (int i = 0; i < sizeof(wahe_wasm2native_mode_name) / sizeof(*wahe_wasm2native_mode_name); i++)
		if (name && strcmp(name, wahe_wasm2native_mode_name[i]) == 0)
		{
			*mode = (enum wahe_wasm2native_mode) i;
			return 1;
		}
	return 0;
}

void wahe_wasm2native_set_mode(enum wahe_wasm2native_mode mode, const char *cache_dir, const char *lib_path)
{
	// Replace the host-wide settings, NULL keeping the defaults
	wahe_wasm2native_settings.mode_set = 1;
	wahe_wasm2native_settings.mode = mode;
	free(wahe_wasm2native_settings.cache_dir);
	free(wahe_wasm2native_settings.lib_path);
	wahe_wasm2native_settings.cache_dir = cache_dir ? make_string_copy(cache_dir) : NULL;
	wahe_wasm2native_settings.lib_path = lib_path ? make_string_copy(lib_path) : NULL;
}

#ifdef WAHE_WASM2NATIVE

// Must match wasm2native.h
typedef struct
{
	const char *cache_dir;
	int opt_level;
	const char *cpu;
	int keep_c;
	int force;
	int cache_only;
	const char *name;
	int64_t mtime;
} wahe_wasm2native_options_t;

static struct
{
	int load_attempted;
	void *lib;
	char *(*compile_file)(const char *wasm_path, const wahe_wasm2native_options_t *options, char **log);
	void (*free)(void *ptr);
} wahe_wasm2native_lib;

typedef struct
{
	char *module_name, *path;
	wahe_wasm2native_options_t options;
} wahe_wasm2native_job_t;

static enum wahe_wasm2native_mode wahe_wasm2native_mode(void)
{
	// Use the host's choice, else the WAHE_WASM2NATIVE environment variable, else auto
	if (!wahe_wasm2native_settings.mode_set)
	{
		wahe_wasm2native_settings.mode_set = 1;
		wahe_wasm2native_settings.mode = WAHE_WASM2NATIVE_AUTO;
		const char *env = getenv("WAHE_WASM2NATIVE");
		if (env && !wahe_wasm2native_parse_mode(env, &wahe_wasm2native_settings.mode))
			fprintf_rl(stderr, "Unknown WAHE_WASM2NATIVE mode '%s', use off, cached, auto or always\n", env);
	}
	return wahe_wasm2native_settings.mode;
}

static int wahe_wasm2native_load(void)
{
	// Load wasm2native.dll on first use only, since it's large and optional
	if (wahe_wasm2native_lib.load_attempted)
		return wahe_wasm2native_lib.lib != NULL;
	wahe_wasm2native_lib.load_attempted = 1;
	const char *lib_path = wahe_wasm2native_settings.lib_path ? wahe_wasm2native_settings.lib_path : "wasm2native.dll";
	wahe_wasm2native_lib.lib = dynlib_open(lib_path);
	if (wahe_wasm2native_lib.lib == NULL)
	{
		fprintf_rl(stderr, "Native translation is unavailable: %s could not be loaded, Wasm modules will use the WebAssembly runtime\n", lib_path);
		return 0;
	}

	// Find the entry points
	wahe_wasm2native_lib.compile_file = dynlib_find_symbol(wahe_wasm2native_lib.lib, "wasm2native_compile_file");
	wahe_wasm2native_lib.free = dynlib_find_symbol(wahe_wasm2native_lib.lib, "wasm2native_free");
	if (wahe_wasm2native_lib.compile_file == NULL || wahe_wasm2native_lib.free == NULL)
	{
		fprintf_rl(stderr, "Native translation is unavailable: %s lacks the wasm2native functions\n", lib_path);
		dynlib_close(&wahe_wasm2native_lib.lib);
		return 0;
	}
	return 1;
}

static char *wahe_wasm2native_compile(wahe_wasm2native_job_t *job)
{
	// Return a malloc'd copy of the DLL path, printing the diagnostics of a failed build
	char *log = NULL;
	char *dll_path = wahe_wasm2native_lib.compile_file(job->path, &job->options, &log);
	char *copy = dll_path ? make_string_copy(dll_path) : NULL;
	if (dll_path == NULL && log)
		fprintf_rl(stderr, "Native translation of module %s failed:\n%s", job->module_name, log);
	wahe_wasm2native_lib.free(dll_path);
	wahe_wasm2native_lib.free(log);
	return copy;
}

#ifdef _WIN32
static unsigned __stdcall wahe_wasm2native_thread(void *arg)
#else
static void *wahe_wasm2native_thread(void *arg)
#endif
{
	// Build at low priority so that the running modules keep the CPU
	wahe_wasm2native_job_t *job = arg;
	#ifdef _WIN32
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_LOWEST);
	#endif
	char *dll_path = wahe_wasm2native_compile(job);
	if (dll_path)
		fprintf_rl(stdout, "Native translation of module %s is ready for its next launch\n", job->module_name);
	free(dll_path);
	free(job->module_name);
	free(job->path);
	free(job);
	return 0;
}

static void wahe_wasm2native_build_in_background(wahe_wasm2native_job_t *job)
{
	// Start a detached thread that owns a copy of the job
	wahe_wasm2native_job_t *copy = malloc(sizeof(*copy));
	*copy = *job;
	copy->module_name = make_string_copy(job->module_name);
	copy->path = make_string_copy(job->path);
	copy->options.cache_only = 0;
	int started;
	#ifdef _WIN32
	HANDLE thread = (HANDLE) _beginthreadex(NULL, 0, wahe_wasm2native_thread, copy, 0, NULL);
	started = thread != NULL;
	if (started)
		CloseHandle(thread);
	#else
	pthread_t thread;
	started = pthread_create(&thread, NULL, wahe_wasm2native_thread, copy) == 0;
	if (started)
		pthread_detach(thread);
	#endif
	if (!started)
	{
		fprintf_rl(stderr, "Could not start the native translation of module %s\n", job->module_name);
		free(copy->module_name);
		free(copy->path);
		free(copy);
	}
}

static int wahe_wasm_file_is_memory64(const char *path)
{
	// Read the memory declaration of the Wasm binary
	buffer_t wasm_buf = buf_load_raw_file(path);
	if (wasm_buf.buf == NULL)
		return 0;
	wasmbin_memory_info_t memory_info = {0};
	io_override_set_buffer();
	int memory_found = wasmbin_read_memory_info((FILE *) &wasm_buf, &memory_info);
	io_override_set_FILE();
	free_buf(&wasm_buf);
	return memory_found && memory_info.memory64;
}

#endif // WAHE_WASM2NATIVE

static char *wahe_wasm2native_resolve(wahe_module_t *ctx, const char *path, enum wahe_module_backend backend)
{
	// Returns the path of the native translation to load instead of the Wasm file, or NULL to use the WebAssembly runtime
	#if defined(WAHE_WASMTIME) || defined(WAHE_WAMR)
	const int has_runtime = 1;
	#else
	const int has_runtime = 0;
	#endif

	#ifdef WAHE_WASM2NATIVE
	// A sandboxed module always runs in the WebAssembly runtime
	if (backend == WAHE_BACKEND_SANDBOXED)
	{
		if (!has_runtime)
			fprintf_rl(stderr, "Module %s must be sandboxed but this host has no WebAssembly runtime\n", ctx->module_name);
		return NULL;
	}

	// Without a runtime the translation is the only way to run the module, forcing native also waits for the build
	enum wahe_wasm2native_mode mode = wahe_wasm2native_mode();
	if (backend == WAHE_BACKEND_NATIVE || (!has_runtime && mode != WAHE_WASM2NATIVE_OFF))
		mode = WAHE_WASM2NATIVE_ALWAYS;
	if (mode == WAHE_WASM2NATIVE_OFF)
		return NULL;

	// Translated memory64 modules have access to host memory, so they stay sandboxed unless the .wahe file asks for native
	if (backend != WAHE_BACKEND_NATIVE && has_runtime && wahe_wasm_file_is_memory64(path))
	{
		fprintf_rl(stdout, "Module %s uses 64-bit memory so it stays in the WebAssembly runtime, add \", native\" to its Module line to translate it\n", ctx->module_name);
		return NULL;
	}
	if (!wahe_wasm2native_load())
		return NULL;

	// Look for a cached translation, building it now only when waiting for it is required
	wahe_wasm2native_job_t job = { .module_name = ctx->module_name, .path = (char *) path };
	if (wahe_wasm2native_settings.cache_dir)
		job.options.cache_dir = wahe_wasm2native_settings.cache_dir;
	else
	{
		static char *default_cache_dir = NULL;
		if (default_cache_dir == NULL)
			default_cache_dir = make_appdata_path("WAHE", "native_cache", 1);
		job.options.cache_dir = default_cache_dir;
	}
	job.options.cache_only = 1;
	char *dll_path = wahe_wasm2native_compile(&job);
	if (dll_path == NULL && mode == WAHE_WASM2NATIVE_ALWAYS)
	{
		fprintf_rl(stdout, "Translating module %s to native code, this can take a while\n", ctx->module_name);
		job.options.cache_only = 0;
		dll_path = wahe_wasm2native_compile(&job);
	}

	// Run in the WebAssembly runtime this time while the translation is built for the next launch
	if (dll_path == NULL && mode == WAHE_WASM2NATIVE_AUTO)
	{
		fprintf_rl(stdout, "Module %s runs in the WebAssembly runtime while its native translation is built in the background\n", ctx->module_name);
		wahe_wasm2native_build_in_background(&job);
	}
	if (dll_path)
		fprintf_rl(stdout, "Module %s uses its native translation %s\n", ctx->module_name, dll_path);
	else if (!has_runtime)
		fprintf_rl(stderr, "Module %s could not be translated and this host has no WebAssembly runtime\n", ctx->module_name);
	return dll_path;

	#else
	// Native translation isn't built into this host
	(void) has_runtime;
	if (backend == WAHE_BACKEND_NATIVE)
		fprintf_rl(stderr, "Module %s asks for native translation but this host was built without WAHE_WASM2NATIVE\n", ctx->module_name);
	return NULL;
	#endif
}
