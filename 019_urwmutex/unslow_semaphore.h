#pragma once

#include "common.h"

/// This semaphore class can by no means be considered fast.
/// BUT it is still not as slow as the Win32 semaphore.
class UnslowSemaphore
{
private:
    HANDLE m_event;
    volatile long m_signalCount;

public:
    UnslowSemaphore(long initialCount = 0, long maxCount = 0x7fffffff)
        : m_event(NULL)
        , m_signalCount(0)
    {
        m_event = CreateEventA(NULL, /* bManualReset */ FALSE, /* bInitialState */ FALSE, NULL);
        assert(m_event != NULL);
        if (initialCount > 0)
        {
            V(initialCount);
        }
    }

    ~UnslowSemaphore()
    {
        CloseHandle(m_event);
    }

    void P()
    {
        WaitForSingleObject(m_event, INFINITE);
        const long oldSignalCount = _InterlockedExchangeAdd(&m_signalCount, -1);
        if (oldSignalCount != 0)
        {
            SetEvent(m_event);
        }
    }

    void V(long delta)
    {
        assert(delta);

        long oldSignalCount = _InterlockedExchangeAdd(&m_signalCount, delta);
        if (oldSignalCount == 0)
        {
            SetEvent(m_event);
        }
    }

    void V()
    {
        V(1);
    }
};
