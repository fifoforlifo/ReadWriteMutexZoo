#pragma once

#include "common.h"
#include "critical_section.h"

/// This semaphore class is built on a atomics + events + critical section.
/// It's the same fundamental design as FastStSemaphore, just using CS instead of event for waiter arbitration.
class Csev2Semaphore
{
private:
    CriticalSection m_cs;
    HANDLE m_semaEvent;
    volatile long m_semaCount;

public:
    Csev2Semaphore(long initialCount = 0, long maxCount = 0x7fffffff)
        : m_semaEvent(NULL)
        , m_semaCount(0)
    {
        assert(initialCount <= maxCount);
        m_semaEvent = CreateEventA(NULL, /* bManualReset */ FALSE, /* bInitialState */ FALSE, NULL);
        assert(m_semaEvent != NULL);
        if (initialCount > 0)
        {
            V(initialCount);
        }
    }

    ~Csev2Semaphore()
    {
        CloseHandle(m_semaEvent);
    }

    void P()
    {
        bool acquired = false;

        CriticalSection::ScopedWriteLock lk(m_cs);

        long newSemaCount = _InterlockedExchangeAdd(&m_semaCount, -1) - 1;
        if (newSemaCount < 0)
        {
            // will be woken up when m_semaCount transitions from negative to non-negative
            WaitForSingleObject(m_semaEvent, INFINITE);
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
