#pragma once

#include "common.h"
#include "scoped_locks.h"

struct WinFutexRecData
{
    long volatile initialized;
    CRITICAL_SECTION cs;
};

#define WIN_FUTEX_REC_DATA_INITIALIZER {0}

class WinFutexRec
{
    WinFutexRecData& m_data;

private:
    void LazyInit()
    {
        if (!m_data.initialized)
        {
            long const old = _InterlockedExchange(&m_data.initialized, 1);
            if (old == 0)
            {
                InitializeCriticalSection(&m_data.cs);
                m_data.initialized = 2;
            }
            else
            {
                while (m_data.initialized != 2)
                {
                    Sleep(0);
                }
            }
        }
    }

public:
    // Since this constructor only assigns a single pointer, it is threadsafe
    // if called concurrently from multiple threads on the same instance.  This
    // allows it to be used as a function-static variable.
    WinFutexRec(WinFutexRecData& data) : m_data(data)
    {
    }
    ~WinFutexRec()
    {
        if (m_data.initialized == 2)
        {
            DeleteCriticalSection(&m_data.cs);
        }
    }

    void WriteLock()
    {
        LazyInit();
        EnterCriticalSection(&m_data.cs);
        _ReadWriteBarrier();
    }
    void WriteUnlock()
    {
        _ReadWriteBarrier();
        LeaveCriticalSection(&m_data.cs);
    }

    void ReadLock()
    {
        WriteLock();
    }
    void ReadUnlock()
    {
        WriteUnlock();
    }

    typedef ScopedWriteLock<WinFutexRec> ScopedWriteLock;
    typedef ScopedReadLock<WinFutexRec> ScopedReadLock;
};

class WinFutexRecC
{
    WinFutexRec m_mutex;
    WinFutexRecData m_data;

public:
    WinFutexRecC() : m_mutex(m_data)
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

    typedef ScopedWriteLock<WinFutexRecC> ScopedWriteLock;
    typedef ScopedReadLock<WinFutexRecC> ScopedReadLock;
};
