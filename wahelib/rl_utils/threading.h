// _Thread_local definition
#if defined(_MSC_VER) && !defined(_Thread_local)
    #define _Thread_local __declspec(thread)
#endif

#if !(defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201102L)) && !defined(_Thread_local)
    #if defined(__GNUC__) || defined(__INTEL_COMPILER) || defined(__SUNPRO_CC) || defined(__IBMCPP__)
        #define _Thread_local __thread
    #endif
#elif defined(__GNUC__) && defined(__GNUC_MINOR__) && (((__GNUC__ << 8) | __GNUC_MINOR__) < ((4 << 8) | 9))
    #define _Thread_local __thread
#endif

// Mutexes
typedef union 
{
	void *align; 
	char data[64];
} rl_mutex_t;

extern void rl_mutex_init(rl_mutex_t *mutex);
extern void rl_mutex_destroy(rl_mutex_t *mutex);
extern void rl_mutex_lock(rl_mutex_t *mutex);
extern void rl_mutex_unlock(rl_mutex_t *mutex);
