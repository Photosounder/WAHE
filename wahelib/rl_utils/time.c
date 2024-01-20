#ifdef _WIN32
  #include <profileapi.h>
  #pragma comment(lib, "winmm.lib")
#else
  #include <sys/types.h> 
  #include <sys/time.h>
#endif

#ifdef __APPLE__
  #include <mach/mach_time.h>
#endif

double get_time_hr()	// High-resolution timing
{
	static double tick_dur = 0.;

	// Find the tick duration in seconds only once
	if (tick_dur==0.)
	{
		#ifdef _WIN32
		LARGE_INTEGER rate;
		QueryPerformanceFrequency(&rate);
		tick_dur = 1. / (double) rate.QuadPart;

		#elif defined(__APPLE__)
		mach_timebase_info_data_t rate_nsec;
		mach_timebase_info(&rate_nsec);
		tick_dur = 1e-9 * (double) rate_nsec.numer / (double) rate_nsec.denom;

		#else
		struct timespec rate;
		#ifdef __wasi__
		clock_getres(CLOCK_MONOTONIC, &rate);
		#else
		clock_getres(CLOCK_MONOTONIC_RAW, &rate);
		#endif
		tick_dur = 1e-9 * (double) rate.tv_nsec;

		#endif
	}

	#ifdef _WIN32
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	return (double) now.QuadPart * tick_dur;

	#elif defined(__APPLE__)
	return (double) mach_absolute_time() * tick_dur;

	#elif defined (__EMSCRIPTEN__)
	return emscripten_get_now() * 1e-3;
	//return (double) get_time_ms() * 1e-3;		// Emscripten doesn't work well with my clock_gettime() code below

	#else
	struct timespec now;
	#ifdef __wasi__
	clock_gettime(CLOCK_MONOTONIC, &now);
	#else
	clock_gettime(CLOCK_MONOTONIC_RAW, &now);
	#endif
	return (double) ((uint64_t) now.tv_sec*1000000000LL + now.tv_nsec) * tick_dur;
	#endif
}
