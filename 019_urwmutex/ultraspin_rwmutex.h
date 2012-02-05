#pragma once

#include "common.h"
#include "scoped_locks.h"
#include "critical_section.h"

/// This class implements a read-write mutex that is
/// heavily in favor of readers.
/// When no writer is contending for a lock, readers
/// perform no syscall nor atomics.
/// In contrast, writers pay a heavier penalty as compared
/// to other synchronization primitives.
///
/// Note that this version allocates its own TLS slot; this is
/// just for demonstration purposes.  A real implementation would
/// allow the client to control that policy (allowing the mutex
/// to use, say, a portion of some already-setup TLS system that
/// the client has available).
/// Note also that when a thread exits, its TLS data must be removed.
/// That issue is not accounted for in this code either.
class UltraSpinReadWriteMutex
{
private: // types
    struct TlsData
    {
        volatile DWORD isReading;

        TlsData() : isReading(false)
        {
        }
    };
    typedef std::vector<TlsData*> ThreadStates;
    typedef CriticalSection Mutex_t;

private: // members
    // treated as an atomic bool
    volatile DWORD m_writeRequested;
    volatile char pad0[CACHE_LINE_SIZE - 4];

    DWORD m_tlsIndex;
    HANDLE m_writerDoneEvent;

    // This critical section enforces the following
    // - mutual exclusion of writers from each other
    // - mutual exclusion of new readers from existing writers
    Mutex_t m_cs;
    ThreadStates m_threadStates;

private:
    TlsData* initTlsData()
    {
        TlsData* pTlsData = new TlsData();
        TlsSetValue(m_tlsIndex, (void*)pTlsData);
        DWORD threadId = GetCurrentThreadId();

        {
            Mutex_t::ScopedWriteLock lk(m_cs);
            m_threadStates.push_back(pTlsData);
        }
        return pTlsData;
    }

    TlsData* getTlsData()
    {
        TlsData* pTlsData = (TlsData*)InlineTlsGetValue(m_tlsIndex);
        if (pTlsData == NULL)
        {
            pTlsData = initTlsData();
        }
        return pTlsData;
    }

    void readLock(TlsData* pTlsData)
    {
        pTlsData->isReading = true;
        while (m_writeRequested)
        {
            pTlsData->isReading = false;
            // wait until writer finishes
            WaitForSingleObject(m_writerDoneEvent, INFINITE);
            pTlsData->isReading = true;
        }
    }
    void readUnlock(TlsData* pTlsData)
    {
        pTlsData->isReading = false;
    }

public:
    UltraSpinReadWriteMutex()
        : m_tlsIndex(TLS_OUT_OF_INDEXES)
        , m_writeRequested(false)
        , m_writerDoneEvent(NULL)
    {
        m_tlsIndex = TlsAlloc();
        assert(m_tlsIndex != TLS_OUT_OF_INDEXES);
        m_writerDoneEvent = CreateEvent(NULL, /* bManualReset */ TRUE, /* bInitialState */ TRUE, NULL);
        assert(m_writerDoneEvent != NULL);
    }
    ~UltraSpinReadWriteMutex()
    {
        CloseHandle(m_writerDoneEvent);
        for (ThreadStates::iterator iter = m_threadStates.begin(),
                                     end = m_threadStates.end();
             iter != end;
             ++iter)
        {
            TlsData* pTlsData = *iter;
            delete pTlsData;
        }
        TlsFree(m_tlsIndex);
    }

    void WriteLock()
    {
        m_cs.WriteLock();
        ResetEvent(m_writerDoneEvent);
        m_writeRequested = true;
        for (ThreadStates::iterator iter = m_threadStates.begin(),
                                     end = m_threadStates.end();
             iter != end;
             ++iter)
        {
            TlsData* pTlsData = *iter;
            while (pTlsData->isReading)
            {
                Sleep(1);
            }
        }
    }
    void WriteUnlock()
    {
        m_writeRequested = false;
        SetEvent(m_writerDoneEvent);
        m_cs.WriteUnlock();
    }
    void ReadLock()
    {
        TlsData* pTlsData = getTlsData();
        readLock(pTlsData);
    }
    void ReadUnlock()
    {
        TlsData* pTlsData = getTlsData();
        readUnlock(pTlsData);
    }

    typedef ScopedWriteLock<UltraSpinReadWriteMutex> ScopedWriteLock;

    class ScopedReadLock
    {
        UltraSpinReadWriteMutex& m_mutex;
        TlsData* m_pTlsData;

    public:
        ScopedReadLock(UltraSpinReadWriteMutex& mutex)
            : m_mutex(mutex)
        {
            m_pTlsData = m_mutex.getTlsData();
            m_mutex.readLock(m_pTlsData);
        }
        ~ScopedReadLock()
        {
            m_mutex.readUnlock(m_pTlsData);
        }
    };
};
