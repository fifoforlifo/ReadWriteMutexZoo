#pragma once

#include "common.h"
#include "scoped_locks.h"
#include "critical_section.h"

/// This mutex is OK in terms of reader speed, but writer speed is still lacking.
template <class TSema>
class CohortReadWriteMutex
{
private: // types
    struct TlsData
    {
        volatile DWORD isReading;
        long readerOrder;
        HANDLE readerDoneEvent;

        TlsData()
            : isReading(false)
            , readerOrder(0)
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
    volatile long m_writeRequested;
    volatile char pad0[CACHE_LINE_SIZE - 4];
    volatile long m_readerCount;
    volatile char pad1[CACHE_LINE_SIZE - 4];
    volatile long m_cohortCount;
    volatile char pad2[CACHE_LINE_SIZE - 4];

    DWORD m_tlsIndex;

    // Writer mutex, to mutually exclude writers from each other and from all-consecutive-readers.
    // Also protects m_threadStates when new readers arrive.
    Mutex_t m_csWriter;
    ThreadStates m_threadStates;

    // ManualRE Signaled when the current cohort of readers gets signaled.
    TSema m_cohortReadySema;

    // AutoRE gets signaled when the last reader in the current cohort
    // exits the mutex, so that the first locked reader may release m_csWriter.
    HANDLE m_cohortDoneEvent;

private: // methods
    TlsData* initTlsData()
    {
        TlsData* pTlsData = new TlsData();
        TlsSetValue(m_tlsIndex, (void*)pTlsData);
        DWORD threadId = GetCurrentThreadId();

        {
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
        pTlsData->isReading = true;
        if (m_writeRequested)
        {
            pTlsData->isReading = false;
            SetEvent(pTlsData->readerDoneEvent);
            pTlsData->readerOrder = _InterlockedIncrement(&m_readerCount);

            if (pTlsData->readerOrder != 1)
            {
                m_cohortReadySema.P();
                pTlsData->isReading = true;
            }
            else // pTlsData->readerOrder == 1
            {
                m_csWriter.WriteLock();
                long cohortCount = _InterlockedExchange(&m_readerCount, 0);
                m_cohortCount = cohortCount;
                m_cohortReadySema.V(cohortCount - 1);
                pTlsData->isReading = true;
            }
        }
    }

    void readUnlock(TlsData* pTlsData)
    {
        pTlsData->isReading = false;
        if (pTlsData->readerOrder)
        {
            long cohortExitOrder = _InterlockedExchangeAdd(&m_cohortCount, -1);
            if (cohortExitOrder == 1)
            {
                if (pTlsData->readerOrder == 1)
                {
                    m_csWriter.WriteUnlock();
                }
                else
                {
                    SetEvent(m_cohortDoneEvent);
                }
            }
            else
            {
                if (pTlsData->readerOrder == 1)
                {
                    WaitForSingleObject(m_cohortDoneEvent, INFINITE);
                    m_csWriter.WriteUnlock();
                }
            }
            pTlsData->readerOrder = 0;
        }
        if (m_writeRequested)
        {
            SetEvent(pTlsData->readerDoneEvent);
        }
    }

public: // interface
    CohortReadWriteMutex()
        : m_tlsIndex(TLS_OUT_OF_INDEXES)
        , m_writeRequested(false)
        , m_readerCount(0)
        , m_cohortCount(0)
        , m_cohortReadySema(/* initialCount */ 0, /* maxCount */ 0x7fffffff)
        , m_cohortDoneEvent(NULL)
    {
        memset((void*)pad0, 0, sizeof(pad0));
        memset((void*)pad1, 0, sizeof(pad0));
        memset((void*)pad2, 0, sizeof(pad0));

        m_tlsIndex = TlsAlloc();
        assert(m_tlsIndex != TLS_OUT_OF_INDEXES);
        m_cohortDoneEvent = CreateEventA(NULL, /* bManualReset */ FALSE, /* bInitialState */ FALSE, NULL);
        assert(m_cohortDoneEvent != NULL);
    }
    ~CohortReadWriteMutex()
    {
        CloseHandle(m_cohortDoneEvent);
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
        m_csWriter.WriteLock();
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

    typedef ScopedWriteLock<CohortReadWriteMutex> ScopedWriteLock;

    class ScopedReadLock
    {
        CohortReadWriteMutex& m_mutex;
        TlsData* m_pTlsData;

    public:
        ScopedReadLock(CohortReadWriteMutex& mutex)
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
