#pragma once

#include "common.h"
#include "scoped_locks.h"
#include "critical_section.h"

class UltraSyncSingleReadWriteMutex
{
    // treated as an atomic bool
    volatile DWORD m_writeRequested;
    volatile char m_pad0[CACHE_LINE_SIZE - 4];
    volatile DWORD m_isReading;
    volatile char m_pad1[CACHE_LINE_SIZE - 4];

    HANDLE m_readerDoneEvent;
    HANDLE m_writerDoneEvent;
    // this critical section excludes writers from each other
    CriticalSection m_csWriters;

public:
    UltraSyncSingleReadWriteMutex()
        : m_writeRequested(false)
        , m_isReading(false)
        , m_writerDoneEvent(NULL)
    {
        m_readerDoneEvent = CreateEvent(NULL, /* bManualReset */ TRUE, /* bInitialState */ TRUE, NULL);
        assert(m_readerDoneEvent != NULL);
        m_writerDoneEvent = CreateEvent(NULL, /* bManualReset */ TRUE, /* bInitialState */ TRUE, NULL);
        assert(m_writerDoneEvent != NULL);
    }
    ~UltraSyncSingleReadWriteMutex()
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
            WaitForSingleObject(m_readerDoneEvent, INFINITE);
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
            SetEvent(m_readerDoneEvent);
            // wait until writer finishes
            WaitForSingleObject(m_writerDoneEvent, INFINITE);
            m_isReading = true;
        }
    }
    void ReadUnlock()
    {
        m_isReading = false;
        if (m_writeRequested)
        {
            SetEvent(m_readerDoneEvent);
        }
    }

    typedef ScopedWriteLock<UltraSyncSingleReadWriteMutex> ScopedWriteLock;
    typedef ScopedReadLock<UltraSyncSingleReadWriteMutex> ScopedReadLock;
};

// NOTE: UltraSyncSingleReadWriteMutex completely dominates UltraSpinSingleReadWriteMutex,
//       but it's unclear why since the reader actually does less work in UltraSpin.
// 32-bit:
//      UltraSpin:  {000R, 001W} :     1839569.5
//                  {001R, 000W} :   430849671.5
//                  {001R, 001W} :      223494.5
//      UltraSync:  {000R, 001W} :     1837250.5
//                  {001R, 000W} :   634840555.0    <-- WIN
//                  {001R, 001W} :   224776973.0
// 64-bit:
//      UltraSpin:  {000R, 001W} :     2440596.5
//                  {001R, 000W} :   706208152.0
//                  {001R, 001W} :     1079657.0
//      UltraSync:  {000R, 001W} :     2327532.0
//                  {001R, 000W} :   846614680.5    <-- WIN
//                  {001R, 001W} :   440648676.5
