#pragma once

#include "../os.h"

enum MutexType {
    MUTEX_NORMAL = 0,
    MUTEX_RECURSIVE,
};

#define mutex_init(mutex) \
    mutex_init_type(mutex, MUTEX_NORMAL)

// Note: This is never compiled, and only documents the API. The actual
// implementations of these prototypes may be macros.
#ifdef MKP_API_REFERENCE

typedef void Mutex;
void mutex_init_type(Mutex *mutex, enum MutexType mtype);
int mutex_destroy(Mutex *mutex);
int mutex_lock(Mutex *mutex);
int mutex_unlock(Mutex *mutex);

typedef void Cond;
int cond_init(Cond *cond);
int cond_destroy(Cond *cond);
int cond_broadcast(Cond *cond);
int cond_signal(Cond *cond);

// `timeout` is in nanoseconds, or UINT64_MAX to block forever
int cond_timedwait(Cond *cond, Mutex *mutex, uint64_t timeout);
int cond_wait(Cond *cond, Mutex *mutex);

// typedef void static_mutex;
// #define STATIC_MUTEX_INITIALIZER
// int static_mutex_lock(static_mutex *mutex);
// int static_mutex_unlock(static_mutex *mutex);

typedef void Thread;
#define THREAD_VOID void
#define THREAD_RETURN() return
int thread_create(Thread *thread, THREAD_VOID (*fun)(void *), void *arg);
int thread_join(Thread thread);

// Returns true if slept the full time, false otherwise
bool thread_sleep(double t);

#endif

// Actual platform-specific implementation
#ifdef HAVE_WIN32
#include "windows.h"
#elif defined(HAVE_PTHREAD)
#include "unix.h"
#else
#error No threading implementation available!
#endif
