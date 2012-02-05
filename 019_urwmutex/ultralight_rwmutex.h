#pragma once

#include "common.h"
#include "scoped_locks.h"
#include "critical_section.h"

/// This class is similar to UltraFastReadWriteMutex -- fully synchronized,
/// writers take precedence over readers.  This version is lighter since
/// it uses a single CriticalSection whenever readers and writers need
/// to be arbitrated, but there's a performance cost since readers get
/// temporarily serialized through it when a writer owns the lock.
class UltraLightReadWriteMutex
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

    // This critical section enforces the following
    // - mutual exclusion of writers from each other
    // - mutual exclusion of new readers from existing writers
    // - mutual exclusion and fair ordering of readers that arrive after previous writers
    // - protects the m_threadStates vector
    Mutex_t m_cs;
    ThreadStates m_threadStates;

private: // methods
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
            {
                Mutex_t::ScopedWriteLock lk(m_cs);
                pTlsData->isReading = true;
            }
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

public: // interface
    UltraLightReadWriteMutex()
        : m_tlsIndex(TLS_OUT_OF_INDEXES)
        , m_writeRequested(false)
    {
        m_tlsIndex = TlsAlloc();
        assert(m_tlsIndex != TLS_OUT_OF_INDEXES);
    }
    ~UltraLightReadWriteMutex()
    {
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

    typedef ScopedWriteLock<UltraLightReadWriteMutex> ScopedWriteLock;

    class ScopedReadLock
    {
        UltraLightReadWriteMutex& m_mutex;
        TlsData* m_pTlsData;

    public:
        ScopedReadLock(UltraLightReadWriteMutex& mutex)
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
