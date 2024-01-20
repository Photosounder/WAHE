#ifndef _WIN32
  #include <pthread.h>
#endif

// Mutexes

void rl_mutex_init(rl_mutex_t *mutex)
{
#if defined(_WIN32)

	// Compile-time size check
	#pragma warning(push)
	#pragma warning(disable: 4214) // nonstandard extension used: bit field types other than int
	struct x { char thread_mutex_type_too_small : (sizeof(rl_mutex_t) < sizeof(CRITICAL_SECTION) ? 0 : 1); }; 
	#pragma warning(pop)

	InitializeCriticalSectionAndSpinCount((CRITICAL_SECTION *) mutex, 32);

#elif defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__)

	// Compile-time size check
	struct x { char thread_mutex_type_too_small : (sizeof(rl_mutex_t) < sizeof(pthread_mutex_t) ? 0 : 1); };

	pthread_mutexattr_t ma;
	pthread_mutexattr_init(&ma);
	pthread_mutexattr_settype(&ma, PTHREAD_MUTEX_RECURSIVE);
	pthread_mutex_init((pthread_mutex_t *) mutex, &ma);
	pthread_mutexattr_destroy(&ma);

#elif defined (__wasi__)

#else 
  #error Unknown platform.
#endif
}

void rl_mutex_destroy(rl_mutex_t *mutex)
{
#if defined(_WIN32)

	DeleteCriticalSection((CRITICAL_SECTION *) mutex);

#elif defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__)

	pthread_mutex_destroy((pthread_mutex_t *) mutex);

#elif defined (__wasi__)

#else 
  #error Unknown platform.
#endif
}

void rl_mutex_lock(rl_mutex_t *mutex)
{
#if defined(_WIN32)

	EnterCriticalSection((CRITICAL_SECTION *) mutex);

#elif defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__)

	pthread_mutex_lock((pthread_mutex_t *) mutex);

#elif defined (__wasi__)

#else 
  #error Unknown platform.
#endif
}

void rl_mutex_unlock(rl_mutex_t *mutex)
{
#if defined(_WIN32)

	LeaveCriticalSection((CRITICAL_SECTION *) mutex);

#elif defined(__linux__) || defined(__APPLE__) || defined(__ANDROID__)

	pthread_mutex_unlock((pthread_mutex_t *) mutex);

#elif defined (__wasi__)

#else 
  #error Unknown platform.
#endif
}
