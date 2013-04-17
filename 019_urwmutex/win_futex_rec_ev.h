#pragma once

#include "common.h"
#include "scoped_locks.h"

struct WinFutexRecEvData
{
    long volatile threadCount;
    DWORD volatile threadId;
    long recursionCount;
    HANDLE hEvent;
};

#define WIN_FUTEX_REC_EV_DATA_INITIALIZER {0}

class WinFutexRecEv
{
    WinFutexRecEvData& m_data;

private:
    void LazyInitEvent()
    {
        if (!m_data.hEvent)
        {
            // create an auto-reset event
            HANDLE hEvent = CreateEventA(NULL, FALSE, FALSE, NULL);
            if (!hEvent)
            {
                // Do this to prevent hanging in WriteUnlock.
                hEvent = INVALID_HANDLE_VALUE;
            }
            HANDLE old = InterlockedCompareExchangePointer(&m_data.hEvent, hEvent, 0);
            if (old != 0)
            {
                // we lost the race
                CloseHandle(hEvent);
            }
        }
    }

    void WaitForEventCreation()
    {
        while (!m_data.hEvent)
        {
            Sleep(0);
        }
    }

    void WaitOnEvent()
    {
        if (m_data.hEvent && m_data.hEvent != INVALID_HANDLE_VALUE)
        {
            WaitForSingleObject(m_data.hEvent, INFINITE);
        }
        else
        {
            // TODO: fallback plan
        }
    }

    void SignalEvent()
    {
        if (m_data.hEvent != INVALID_HANDLE_VALUE)
        {
            SetEvent(m_data.hEvent);
        }
        else
        {
            // TODO: fallback plan
        }
    }

public:
    // Since this constructor only assigns a single pointer, it is threadsafe
    // if called concurrently from multiple threads on the same instance.  This
    // allows it to be used as a function-static variable.
    WinFutexRecEv(WinFutexRecEvData& data) : m_data(data)
    {
    }
    ~WinFutexRecEv()
    {
        //CloseHandle(m_data.hEvent);
    }

    void WriteLock()
    {
        // test if this is a recursive lock
        if (m_data.threadCount != 0)
        {
            if (m_data.threadId == GetCurrentThreadId())
            {
                m_data.recursionCount += 1;
                _ReadWriteBarrier();
                return;
            }
        }

        // acquire an arrival order
        long const oldThreadCount = _InterlockedExchangeAdd(&m_data.threadCount, 1);
        if (oldThreadCount == 0)
        {
            // we acquired the lock
            m_data.recursionCount = 1;
            m_data.threadId = GetCurrentThreadId();
            _ReadWriteBarrier();
            return;
        }

        // if we reached here, we must wait
        LazyInitEvent();
        WaitOnEvent();
        m_data.recursionCount = 1;
        m_data.threadId = GetCurrentThreadId();
        _ReadWriteBarrier();
    }
    void WriteUnlock()
    {
        _ReadWriteBarrier();
        m_data.recursionCount -= 1;
        if (m_data.recursionCount == 0)
        {
            // give up the thread's lock
            m_data.threadId = 0;
            long const oldThreadCount = _InterlockedExchangeAdd(&m_data.threadCount, -1);
            if (oldThreadCount > 1)
            {
                // some other thread is waiting
                WaitForEventCreation();
                SignalEvent();
            }
        }
    }

    void ReadLock()
    {
        WriteLock();
    }
    void ReadUnlock()
    {
        WriteUnlock();
    }

    typedef ScopedWriteLock<WinFutexRecEv> ScopedWriteLock;
    typedef ScopedReadLock<WinFutexRecEv> ScopedReadLock;
};

// This class adapts WinFutexRecEv to the interface required by the tests.
class WinFutexRecEvC
{
    WinFutexRecEv m_mutex;
    WinFutexRecEvData m_data;

public:
    WinFutexRecEvC() : m_mutex(m_data)
    {
        memset(&m_data, 0, sizeof(m_data));
    }

    void WriteLock()
    {
        m_mutex.WriteLock();
    }
    void WriteUnlock()
    {
        m_mutex.WriteUnlock();
    }

    void ReadLock()
    {
        m_mutex.ReadLock();
    }
    void ReadUnlock()
    {
        m_mutex.ReadUnlock();
    }

    typedef ScopedWriteLock<WinFutexRecEvC> ScopedWriteLock;
    typedef ScopedReadLock<WinFutexRecEvC> ScopedReadLock;
};
