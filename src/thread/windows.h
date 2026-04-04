#pragma once

#include <assert.h>
#include <windows.h>
#include <process.h>
#include <stdint.h>
#include <errno.h>

typedef CRITICAL_SECTION   Mutex;
typedef CONDITION_VARIABLE Cond;

static inline int mutex_init_type_internal(Mutex *mutex, enum MutexType mtype)
{
    (void) mtype;
    return !InitializeCriticalSectionEx(mutex, 0, 0);
}

#define mutex_init_type(mutex, mtype) \
    assert(!mutex_init_type_internal(mutex, mtype))

static inline int mutex_destroy(Mutex *mutex)
{
    DeleteCriticalSection(mutex);
    return 0;
}

static inline int mutex_lock(Mutex *mutex)
{
    EnterCriticalSection(mutex);
    return 0;
}

static inline int mutex_unlock(Mutex *mutex)
{
    LeaveCriticalSection(mutex);
    return 0;
}

static inline int cond_init(Cond *cond)
{
    InitializeConditionVariable(cond);
    return 0;
}

static inline int cond_destroy(Cond *cond)
{
    // condition variables are not destroyed
    (void) cond;
    return 0;
}

static inline int cond_broadcast(Cond *cond)
{
    WakeAllConditionVariable(cond);
    return 0;
}

static inline int cond_signal(Cond *cond)
{
    WakeConditionVariable(cond);
    return 0;
}

static inline int cond_wait(Cond *cond, Mutex *mutex)
{
    return !SleepConditionVariableCS(cond, mutex, INFINITE);
}

static inline int cond_timedwait(Cond *cond, Mutex *mutex, uint64_t timeout)
{
    if (timeout == UINT64_MAX)
        return cond_wait(cond, mutex);

    timeout /= UINT64_C(1000000);
    if (timeout > INFINITE - 1)
        timeout = INFINITE - 1;

    BOOL bRet = SleepConditionVariableCS(cond, mutex, timeout);
    if (bRet == FALSE)
    {
        if (GetLastError() == ERROR_TIMEOUT)
            return ETIMEDOUT;
        else
            return EINVAL;
    }
    return 0;
}

// typedef SRWLOCK static_mutex;
// #define STATIC_MUTEX_INITIALIZER SRWLOCK_INIT

// static inline int static_mutex_lock(static_mutex *mutex)
// {
//     AcquireSRWLockExclusive(mutex);
//     return 0;
// }

// static inline int static_mutex_unlock(static_mutex *mutex)
// {
//     ReleaseSRWLockExclusive(mutex);
//     return 0;
// }

typedef HANDLE Thread;

#define THREAD_VOID unsigned __stdcall
#define THREAD_RETURN() return 0

static inline int thread_create(Thread *thread,
                                   THREAD_VOID (*fun)(void *),
                                   void *__restrict arg)
{
    *thread = (HANDLE) _beginthreadex(NULL, 0, fun, arg, 0, NULL);
    return *thread ? 0 : -1;
}

static inline int thread_join(Thread thread)
{
    DWORD ret = WaitForSingleObject(thread, INFINITE);
    if (ret != WAIT_OBJECT_0)
        return ret == WAIT_ABANDONED ? EINVAL : EDEADLK;
    CloseHandle(thread);
    return 0;
}

static inline bool thread_sleep(double t)
{
    // Time is expected in 100 nanosecond intervals.
    // Negative values indicate relative time.
    LARGE_INTEGER time = { .QuadPart = -(LONGLONG) (t * 1e7) };

    if (time.QuadPart >= 0)
        return true;

    bool ret = false;

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
# define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x2
#endif

    HANDLE timer = CreateWaitableTimerEx(NULL, NULL,
                                         CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                         TIMER_ALL_ACCESS);

    // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION is supported in Windows 10 1803+,
    // retry without it.
    if (!timer)
        timer = CreateWaitableTimerEx(NULL, NULL, 0, TIMER_ALL_ACCESS);

    if (!timer)
        goto end;

    if (!SetWaitableTimer(timer, &time, 0, NULL, NULL, 0))
        goto end;

    if (WaitForSingleObject(timer, INFINITE) != WAIT_OBJECT_0)
        goto end;

    ret = true;

end:
    if (timer)
        CloseHandle(timer);
    return ret;
}
