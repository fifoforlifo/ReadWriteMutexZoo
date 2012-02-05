#pragma once

#include <intrin.h>
#include "common.h"
#include "scoped_locks.h"
#include "critical_section.h"

/// This mutex is OK in terms of reader speed, but writer speed is still lacking.
class TicketedReadWriteMutex
{
private: // types
    struct TlsData
    {
        volatile DWORD isReading;
        HANDLE readerDoneEvent;
        bool isLockedReader;
        bool isFirstReader;

        TlsData()
            : isReading(false)
            , readerDoneEvent(NULL)
            , isLockedReader(false)
            , isFirstReader(false)
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
    volatile long m_ticket;
    volatile char pad0[CACHE_LINE_SIZE - 4];
    volatile long m_lastReaderTicket;
    volatile char pad1[CACHE_LINE_SIZE - 4];
    volatile long m_writeRequested;
    volatile char pad2[CACHE_LINE_SIZE - 4];
    volatile long m_readerCount;
    volatile char pad3[CACHE_LINE_SIZE - 4];

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
        pTlsData->isReading = true;
        const long lastReaderTicket = m_lastReaderTicket;
        const long ticket = m_ticket;
        if (ticket - lastReaderTicket <= 1)
        {
            return;
        }
        else
        {
            pTlsData->isReading = false;
            SetEvent(pTlsData->readerDoneEvent);
            {
                m_csQueue.WriteLock();
                long readerCount = _InterlockedIncrement(&m_readerCount);
                if (readerCount == 1)
                {
                    m_csWriter.WriteLock();
                    const long newTicket = _InterlockedIncrement(&m_ticket);
                    m_lastReaderTicket = newTicket;
                    pTlsData->isLockedReader = true;
                    pTlsData->isFirstReader = true;
                }
                else
                {
                    pTlsData->isLockedReader = true;
                }
                pTlsData->isReading = true;
                m_csQueue.WriteUnlock();
            }
        }
    }

    void readUnlock(TlsData* pTlsData)
    {
        pTlsData->isReading = false;
        if (pTlsData->isLockedReader)
        {
            pTlsData->isLockedReader = false;
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
                    pTlsData->isFirstReader = false;
                    m_csWriter.WriteUnlock();
                }
            }
        }
        if (m_writeRequested)
        {
            SetEvent(pTlsData->readerDoneEvent);
        }
    }

public: // interface
    TicketedReadWriteMutex()
        : m_tlsIndex(TLS_OUT_OF_INDEXES)
        , m_ticket(0)
        , m_lastReaderTicket(0)
        , m_readerCount(0)
        , m_lastLockedReaderEvent(NULL)
    {
        m_tlsIndex = TlsAlloc();
        assert(m_tlsIndex != TLS_OUT_OF_INDEXES);
        m_lastLockedReaderEvent = CreateEventA(NULL, /* bManualReset */ FALSE, /* bInitialState */ FALSE, NULL);
        assert(m_lastLockedReaderEvent != NULL);
    }
    ~TicketedReadWriteMutex()
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
        _InterlockedExchangeAdd(&m_ticket, 2);
        m_csWriter.WriteLock();
        m_writeRequested = true;
        m_csQueue.WriteUnlock();
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

    typedef ScopedWriteLock<TicketedReadWriteMutex> ScopedWriteLock;

    class ScopedReadLock
    {
        TicketedReadWriteMutex& m_mutex;
        TlsData* m_pTlsData;

    public:
        ScopedReadLock(TicketedReadWriteMutex& mutex)
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
