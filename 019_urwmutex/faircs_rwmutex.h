#pragma once

#include <intrin.h>
#include "common.h"
#include "scoped_locks.h"
#include "critical_section.h"

/// This mutex is OK in terms of reader speed, but writer speed is still lacking.
class FairCsReadWriteMutex
{
private: // types
    struct TlsData
    {
        bool isFirstReader;

        TlsData()
            : isFirstReader(false)
        {
        }
    };
    typedef std::vector<TlsData*> ThreadStates;
    typedef CriticalSection Mutex_t;

private: // members
    volatile long m_readerCount;
    volatile char pad0[CACHE_LINE_SIZE - 4];

    DWORD m_tlsIndex;

    // Queue mutex, to enforce fair ordering between readers and writers.
    Mutex_t m_csQueue;
    // Writer mutex, to mutually exclude writers from each other and from all-consecutive-readers.
    // Also protects m_threadStates when new readers arrive.
    Mutex_t m_csWriter;
    ThreadStates m_threadStates;

    // This gets signaled when the last locked reader exits the mutex,
    // so that the first locked reader may release m_csWriter.
    HANDLE m_lastLockedReaderEvent;

private: // methods
    TlsData* initTlsData()
    {
        TlsData* pTlsData = new TlsData();
        TlsSetValue(m_tlsIndex, (void*)pTlsData);
        DWORD threadId = GetCurrentThreadId();

        {
            Mutex_t::ScopedWriteLock queueLk(m_csQueue);
            Mutex_t::ScopedWriteLock lk(m_csWriter);
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
        m_csQueue.WriteLock();
        long readerCount = _InterlockedIncrement(&m_readerCount);
        if (readerCount == 1)
        {
            m_csWriter.WriteLock();
            pTlsData->isFirstReader = true;
        }
        m_csQueue.WriteUnlock();
    }

    void readUnlock(TlsData* pTlsData)
    {
        long readerCount = _InterlockedDecrement(&m_readerCount);
        if (readerCount == 0)
        {
            if (pTlsData->isFirstReader)
            {
                m_csWriter.WriteUnlock();
                pTlsData->isFirstReader = false;
            }
            else
            {
                SetEvent(m_lastLockedReaderEvent);
            }
        }
        else
        {
            if (pTlsData->isFirstReader)
            {
                WaitForSingleObject(m_lastLockedReaderEvent, INFINITE);
                m_csWriter.WriteUnlock();
                pTlsData->isFirstReader = false;
            }
        }
    }

public: // interface
    FairCsReadWriteMutex()
        : m_tlsIndex(TLS_OUT_OF_INDEXES)
        , m_readerCount(0)
        , m_lastLockedReaderEvent(NULL)
    {
        m_tlsIndex = TlsAlloc();
        assert(m_tlsIndex != TLS_OUT_OF_INDEXES);
        m_lastLockedReaderEvent = CreateEventA(NULL, /* bManualReset */ FALSE, /* bInitialState */ FALSE, NULL);
        assert(m_lastLockedReaderEvent != NULL);
    }
    ~FairCsReadWriteMutex()
    {
        CloseHandle(m_lastLockedReaderEvent);
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
        m_csQueue.WriteLock();
        m_csWriter.WriteLock();
        m_csQueue.WriteUnlock();
    }
    void WriteUnlock()
    {
        m_csWriter.WriteUnlock();
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

    typedef ScopedWriteLock<FairCsReadWriteMutex> ScopedWriteLock;

    class ScopedReadLock
    {
        FairCsReadWriteMutex& m_mutex;
        TlsData* m_pTlsData;

    public:
        ScopedReadLock(FairCsReadWriteMutex& mutex)
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
