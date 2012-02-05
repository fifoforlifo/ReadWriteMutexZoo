#pragma once

#include "common.h"
#include "scoped_locks.h"
#include "critical_section.h"

class UltraSpinSingleReadWriteMutex
{
    // treated as an atomic bool
    volatile DWORD m_writeRequested;
    volatile char m_pad0[CACHE_LINE_SIZE - 4];
    volatile DWORD m_isReading;
    volatile char m_pad1[CACHE_LINE_SIZE - 4];

    HANDLE m_writerDoneEvent;
    // this critical section excludes writers from each other
    CriticalSection m_csWriters;

public:
    UltraSpinSingleReadWriteMutex()
        : m_writeRequested(false)
        , m_isReading(false)
        , m_writerDoneEvent(NULL)
    {
        m_writerDoneEvent = CreateEvent(NULL, /* bManualReset */ TRUE, /* bInitialState */ TRUE, NULL);
        assert(m_writerDoneEvent != NULL);
    }
    ~UltraSpinSingleReadWriteMutex()
    {
        CloseHandle(m_writerDoneEvent);
    }

    void WriteLock()
    {
        m_csWriters.WriteLock();
        ResetEvent(m_writerDoneEvent);
        m_writeRequested = true;
        while (m_isReading)
        {
            Sleep(1);
        }
    }
    void WriteUnlock()
    {
        m_writeRequested = false;
        SetEvent(m_writerDoneEvent);
        m_csWriters.WriteUnlock();
    }
    void ReadLock()
    {
        m_isReading = true;
        while (m_writeRequested)
        {
            m_isReading = false;
            // wait until writer finishes
            WaitForSingleObject(m_writerDoneEvent, INFINITE);
            m_isReading = true;
        }
    }
    void ReadUnlock()
    {
        m_isReading = false;
    }

    typedef ScopedWriteLock<UltraSpinSingleReadWriteMutex> ScopedWriteLock;
    typedef ScopedReadLock<UltraSpinSingleReadWriteMutex> ScopedReadLock;
};
