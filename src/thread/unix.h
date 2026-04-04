#pragma once

#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>

typedef pthread_mutex_t Mutex;
typedef pthread_cond_t  Cond;
// typedef pthread_mutex_t static_mutex;
typedef pthread_t       Thread;

#define STATIC_MUTEX_INITIALIZER PTHREAD_MUTEX_INITIALIZER

static inline int mutex_init_type_internal(Mutex *mutex, enum MutexType mtype)
{
    int mutex_type;
    switch (mtype) {
        case MUTEX_RECURSIVE:
            mutex_type = PTHREAD_MUTEX_RECURSIVE;
            break;
        case MUTEX_NORMAL:
        default:
        #ifndef NDEBUG
            mutex_type = PTHREAD_MUTEX_ERRORCHECK;
        #else
            mutex_type = PTHREAD_MUTEX_DEFAULT;
        #endif
            break;
    }

    int ret = 0;
    pthread_mutexattr_t attr;
    ret = pthread_mutexattr_init(&attr);
    if (ret != 0)
        return ret;

    pthread_mutexattr_settype(&attr, mutex_type);
    ret = pthread_mutex_init(mutex, &attr);
    pthread_mutexattr_destroy(&attr);
    return ret;
}

// static inline int mutex_init_type(Mutex *mutex, enum MutexType mtype)
// {
//     return mutex_init_type_internal(mutex, mtype);
// }

#define mutex_init_type(mutex, mtype) \
    mutex_init_type_internal(mutex, mtype)

// static inline int mutex_init(Mutex *mutex)
// {
//     return mutex_init_type(mutex, MUTEX_NORMAL);
// }

#define mutex_destroy    pthread_mutex_destroy
#define mutex_lock       pthread_mutex_lock
#define mutex_unlock     pthread_mutex_unlock

static inline int cond_init(Cond *cond)
{ 
    int ret = 0;
    pthread_condattr_t attr;
    ret = pthread_condattr_init(&attr);
    if (ret != 0)
        return ret;

#ifdef PTHREAD_HAS_SETCLOCK
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
#endif
    ret = pthread_cond_init(cond, &attr);
    pthread_condattr_destroy(&attr);
    return ret;
}

#define cond_destroy     pthread_cond_destroy
#define cond_broadcast   pthread_cond_broadcast
#define cond_signal      pthread_cond_signal
#define cond_wait        pthread_cond_wait

static inline int cond_timedwait(Cond *cond, Mutex *mutex, uint64_t timeout)
{
    if (timeout == UINT64_MAX)
        return pthread_cond_wait(cond, mutex);

    struct timespec ts;
#ifdef PTHREAD_HAS_SETCLOCK
    if (clock_gettime(CLOCK_MONOTONIC, &ts) < 0)
        return errno;
#else
    struct timeval tv;
    if (gettimeofday(&tv, NULL) < 0) // equivalent to CLOCK_REALTIME
        return errno;
    ts.tv_sec = tv.tv_sec;
    ts.tv_nsec = tv.tv_usec * 1000;
#endif

    ts.tv_sec  += timeout / 1000000000LLU;
    ts.tv_nsec += timeout % 1000000000LLU;

    if (ts.tv_nsec > 1000000000L) {
        ts.tv_nsec -= 1000000000L;
        ts.tv_sec++;
    }

    return pthread_cond_timedwait(cond, mutex, &ts);
}

// #define static_mutex_lock    pthread_mutex_lock
// #define static_mutex_unlock  pthread_mutex_unlock

#define THREAD_VOID void *
#define THREAD_RETURN() return NULL

#define thread_create(t, f, a) pthread_create(t, NULL, f, a)
#define thread_join(t)         pthread_join(t, NULL)

static inline int thread_sleep(double t)
{
    if (t <= 0.0)
        return 1;

    struct timespec ts;
    ts.tv_sec = (time_t) t;
    ts.tv_nsec = (t - ts.tv_sec) * 1e9;

    return nanosleep(&ts, NULL);
}
