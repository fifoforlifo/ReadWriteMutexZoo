#pragma once

#include "common.h"
#include "scoped_locks.h"
#include "critical_section.h"

/// A fully synchronized read-write mutex with the following properties.
/// - Heavily biased towards high performance of large numbers of concurrent
///   readers, with infrequent access by writers.
/// - Writers take priority over readers; that is, all new readers yield
///   access to writers.  This means writers can starve readers.
/// - When no writers are contending for a lock, readers only incur
///   1 volatile write + 1 volatile read on enter, and
///   1 volatile write + 1 volatile read on exit.
/// - Use cases include:
///   - For API interception, normal API calls get read-locked; then a background
///     thread can acquire a write-lock to "boot everyone out of the API".
///   - Garbage Collector where normal threads read-lock the heap, and the
///     collection routine write-locks the heap.
class UltraFastReadWriteMutex
{
private: // types
    struct TlsData
    {
        volatile DWORD isReading;
        HANDLE readerDoneEvent;

        TlsData()
            : isReading(false)
            , readerDoneEvent(NULL)
        {
            readerDoneEvent = CreateEvent(NULL, /* bManualReset */ FALSE, /* bInitialState */ FALSE, NULL);
            assert(readerDoneEvent != NULL);
        }
        ~TlsData()
        {
            CloseHandle(readerDoneEvent);
        }
    };
    typedef std::vector<TlsData*> ThreadStates;
    typedef CriticalSection Mutex_t;

private: // members
    // treated as an atomic bool
    volatile DWORD m_writeRequested;
    volatile char pad0[CACHE_LINE_SIZE - 4];

    DWORD m_tlsIndex;

    // A manual-reset even that is kept signaled except when
    // writer is attempting or has acquisition.
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
            SetEvent(pTlsData->readerDoneEvent);
            // wait until writer finishes
            WaitForSingleObject(m_writerDoneEvent, INFINITE);
            pTlsData->isReading = true;
        }
    }

    void readUnlock(TlsData* pTlsData)
    {
        pTlsData->isReading = false;
        if (m_writeRequested)
        {
            SetEvent(pTlsData->readerDoneEvent);
        }
    }

public:
    UltraFastReadWriteMutex()
        : m_tlsIndex(TLS_OUT_OF_INDEXES)
        , m_writeRequested(false)
        , m_writerDoneEvent(NULL)
    {
        m_tlsIndex = TlsAlloc();
        assert(m_tlsIndex != TLS_OUT_OF_INDEXES);
        m_writerDoneEvent = CreateEvent(NULL, /* bManualReset */ TRUE, /* bInitialState */ TRUE, NULL);
        assert(m_writerDoneEvent != NULL);
    }
    ~UltraFastReadWriteMutex()
    {
        CloseHandle(m_writerDoneEvent);
        for (ThreadStates::iterator iter = m_threadStates.begin(),
                                     end = m_threadStates.end();
             iter != end; ++iter)
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
                WaitForSingleObject(pTlsData->readerDoneEvent, INFINITE);
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

    typedef ScopedWriteLock<UltraFastReadWriteMutex> ScopedWriteLock;

    class ScopedReadLock
    {
        UltraFastReadWriteMutex& m_mutex;
        TlsData* m_pTlsData;

    public:
        ScopedReadLock(UltraFastReadWriteMutex& mutex)
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
