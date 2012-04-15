#pragma once

#include "common.h"
#include "critical_section.h"

/// This semaphore class is built on a critical section + event.
class CsevSemaphore
{
private:
    CriticalSection m_cs;
    HANDLE m_event;
    volatile long m_count;

public:
    CsevSemaphore(long initialCount = 0, long maxCount = 0x7fffffff)
        : m_event(NULL)
        , m_count(0)
    {
        assert(initialCount <= maxCount);
        m_event = CreateEventA(NULL, /* bManualReset */ FALSE, /* bInitialState */ FALSE, NULL);
        assert(m_event != NULL);
        if (initialCount > 0)
        {
            V(initialCount);
        }
    }

    ~CsevSemaphore()
    {
        CloseHandle(m_event);
    }

    void P()
    {
        long count;
        for (;;)
        {
            {
                CriticalSection::ScopedWriteLock lk(m_cs);
                long oldCount = m_count;
                if (oldCount > 0)
                {
                    count = oldCount - 1;
                    m_count = count;
                    break;
                }
            }
            WaitForSingleObject(m_event, INFINITE);
        }
        if (count > 0)
        {
            SetEvent(m_event);
        }
    }

    void V(long delta)
    {
        assert(delta > 0);

        long oldCount;
        long count;
        {
            CriticalSection::ScopedWriteLock lk(m_cs);
            oldCount = m_count;
            count = m_count + delta;
            m_count = count;
        }
        if (oldCount <= 0)
        {
            SetEvent(m_event);
        }
    }

    void V()
    {
        V(1);
    }
};
