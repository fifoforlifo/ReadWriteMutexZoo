#pragma once

#include "common.h"
#include "critical_section.h"

/// This semaphore class is built on a atomics + events.
/// It has fast single-threaded performance, but perf falls sharply under contention.
/// NOTE: the perf numbers are so similar to Win32 Semaphore that I think I may have stumbled on that algorithm
class FastStSemaphore
{
private:
    HANDLE m_waitEvent;
    HANDLE m_semaEvent;
    volatile long m_waitCount;
    volatile long m_semaCount;

public:
    FastStSemaphore(long initialCount = 0, long maxCount = 0x7fffffff)
        : m_waitEvent(NULL)
        , m_semaEvent(NULL)
        , m_waitCount(0)
        , m_semaCount(0)
    {
        assert(initialCount <= maxCount);
        m_waitEvent = CreateEventA(NULL, /* bManualReset */ FALSE, /* bInitialState */ FALSE, NULL);
        assert(m_waitEvent != NULL);
        m_semaEvent = CreateEventA(NULL, /* bManualReset */ FALSE, /* bInitialState */ FALSE, NULL);
        assert(m_semaEvent != NULL);
        if (initialCount > 0)
        {
            V(initialCount);
        }
    }

    ~FastStSemaphore()
    {
        CloseHandle(m_semaEvent);
        CloseHandle(m_waitEvent);
    }

    void P()
    {
        bool acquired = false;

        long waiterId = _InterlockedExchangeAdd(&m_waitCount, 1);
        if (waiterId > 0)
        {
            WaitForSingleObject(m_waitEvent, INFINITE);
        }

        long newSemaCount = _InterlockedExchangeAdd(&m_semaCount, -1) - 1;
        if (newSemaCount < 0)
        {
            // will be woken up when m_semaCount transitions from negative to non-negative
            WaitForSingleObject(m_semaEvent, INFINITE);
        }

        long newWaitCount = _InterlockedExchangeAdd(&m_waitCount, -1) - 1;
        if (newWaitCount > 0)
        {
            SetEvent(m_waitEvent);
        }
    }

    void V(long delta)
    {
        assert(delta > 0);

        long prevCount = _InterlockedExchangeAdd(&m_semaCount, delta);
        if (prevCount < 0)
        {
            SetEvent(m_semaEvent);
        }
    }

    void V()
    {
        V(1);
    }
};
