#pragma once

#include <assert.h>
#include <map>
#include <Windows.h>


template <typename TMutex>
class ScopedWriteLock
{
    TMutex& m_mutex;

public:
    ScopedWriteLock(TMutex& mutex) : m_mutex(mutex)
    {
        m_mutex.WriteLock();
    }
    ~ScopedWriteLock()
    {
        m_mutex.WriteUnlock();
    }
};

template <typename TMutex>
class ScopedReadLock
{
    TMutex& m_mutex;

public:
    ScopedReadLock(TMutex& mutex) : m_mutex(mutex)
    {
        m_mutex.ReadLock();
    }
    ~ScopedReadLock()
    {
        m_mutex.ReadUnlock();
    }
};


class Mutex
{
    HANDLE m_hMutex;

public:
    Mutex()
    {
        m_hMutex = CreateMutexA(NULL, FALSE, NULL);
    }
    ~Mutex()
    {
        CloseHandle(m_hMutex);
    }

    void WriteLock()
    {
        DWORD waitResult = WaitForSingleObject(m_hMutex, INFINITE);
    }
    void WriteUnlock()
    {
        ReleaseMutex(m_hMutex);
    }

    void ReadLock()
    {
        WriteLock();
    }
    void ReadUnlock()
    {
        WriteUnlock();
    }

    typedef ScopedWriteLock<Mutex> ScopedWriteLock;
    typedef ScopedReadLock<Mutex> ScopedReadLock;
};

class CriticalSection
{
    CRITICAL_SECTION m_cs;

public:
    CriticalSection()
    {
        InitializeCriticalSection(&m_cs);
    }
    ~CriticalSection()
    {
        DeleteCriticalSection(&m_cs);
    }

    void WriteLock()
    {
        EnterCriticalSection(&m_cs);
    }
    void WriteUnlock()
    {
        LeaveCriticalSection(&m_cs);
    }
    void ReadLock()
    {
        WriteLock();
    }
    void ReadUnlock()
    {
        WriteUnlock();
    }

    typedef ScopedWriteLock<CriticalSection> ScopedWriteLock;
    typedef ScopedReadLock<CriticalSection> ScopedReadLock;
};

class SlimReadWriteLock
{
    SRWLOCK m_mutex;

public:
    SlimReadWriteLock()
    {
        InitializeSRWLock(&m_mutex);
    }
    ~SlimReadWriteLock()
    {
    }

    void WriteLock()
    {
        AcquireSRWLockExclusive(&m_mutex);
    }
    void WriteUnlock()
    {
        ReleaseSRWLockExclusive(&m_mutex);
    }
    void ReadLock()
    {
        AcquireSRWLockShared(&m_mutex);
    }
    void ReadUnlock()
    {
        ReleaseSRWLockShared(&m_mutex);
    }

    typedef ScopedWriteLock<SlimReadWriteLock> ScopedWriteLock;
    typedef ScopedReadLock<SlimReadWriteLock> ScopedReadLock;
};


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
#define CACHE_LINE_SIZE 64
    // treated as an atomic bool
    volatile DWORD m_writeRequested;
    volatile char pad0[CACHE_LINE_SIZE - 4];
#undef CACHE_LINE_SIZE

    DWORD m_tlsIndex;
    HANDLE m_writerDoneEvent;
    // this critical section excludes writers from each other
    CriticalSection m_csWriters;

    // this critical section protects the accessMap
    CriticalSection m_csMap;
    struct TlsData
    {
        volatile DWORD isReading;

        TlsData() : isReading(false)
        {
        }
    };
    // map ThreadID --> ptr-to-isReadingFlag
    typedef std::map<DWORD, TlsData*> AccessMap;
    AccessMap m_accessMap;

private:
    TlsData* getTlsData()
    {
        TlsData* pTlsData = (TlsData*)TlsGetValue(m_tlsIndex);
        if (pTlsData == NULL)
        {
            pTlsData = new TlsData();
            TlsSetValue(m_tlsIndex, (void*)pTlsData);
            DWORD threadId = GetCurrentThreadId();

            CriticalSection::ScopedWriteLock lk(m_csMap);
            m_accessMap[threadId] = pTlsData;
        }
        return pTlsData;
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
        for (AccessMap::iterator iter = m_accessMap.begin(), end = m_accessMap.end();
             iter != end; ++iter)
        {
            TlsData* pTlsData = iter->second;
            delete pTlsData;
        }
        TlsFree(m_tlsIndex);
    }

    void WriteLock()
    {
        m_csWriters.WriteLock();
        ResetEvent(m_writerDoneEvent);
        m_writeRequested = true;
        {
            // Locking m_csMap here also prevents new readers from acquiring
            // this read/write mutex, since they would be blocked upon
            // calling getTlsData(), which also locks m_csMap.
            CriticalSection::ScopedWriteLock lk(m_csMap);
            for (AccessMap::const_iterator iter = m_accessMap.begin(), end = m_accessMap.end();
                 iter != end; ++iter)
            {
                const TlsData* pTlsData = iter->second;
                while (pTlsData->isReading)
                {
                    Sleep(1);
                }
            }
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
        TlsData* pTlsData = getTlsData();
        pTlsData->isReading = true;
        while (m_writeRequested)
        {
            pTlsData->isReading = false;
            // wait until writer finishes
            WaitForSingleObject(m_writerDoneEvent, INFINITE);
            pTlsData->isReading = true;
        }
    }
    void ReadUnlock()
    {
        TlsData* pTlsData = getTlsData();
        pTlsData->isReading = false;
    }

    typedef ScopedWriteLock<UltraSpinReadWriteMutex> ScopedWriteLock;
    typedef ScopedReadLock<UltraSpinReadWriteMutex> ScopedReadLock;
};


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
#define CACHE_LINE_SIZE 64
    // treated as an atomic bool
    volatile DWORD m_writeRequested;
    volatile char pad0[CACHE_LINE_SIZE - 4];
#undef CACHE_LINE_SIZE

    DWORD m_tlsIndex;

    // A manual-reset even that is kept signaled except when
    // writer is attempting or has acquisition.
    HANDLE m_writerDoneEvent;
    // This critical section excludes writers from each other.
    CriticalSection m_csWriters;

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
    // map ThreadID --> ptr-to-isReadingFlag
    typedef std::map<DWORD, TlsData*> AccessMap;

    // this critical section protects the accessMap
    CriticalSection m_csMap;
    AccessMap m_accessMap;

private:
    TlsData* getTlsData()
    {
        TlsData* pTlsData = (TlsData*)TlsGetValue(m_tlsIndex);
        if (pTlsData == NULL)
        {
            pTlsData = new TlsData();
            TlsSetValue(m_tlsIndex, (void*)pTlsData);
            DWORD threadId = GetCurrentThreadId();

            CriticalSection::ScopedWriteLock lk(m_csMap);
            m_accessMap[threadId] = pTlsData;
        }
        return pTlsData;
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
        for (AccessMap::iterator iter = m_accessMap.begin(), end = m_accessMap.end();
             iter != end; ++iter)
        {
            TlsData* pTlsData = iter->second;
            delete pTlsData;
        }
        TlsFree(m_tlsIndex);
    }

    void WriteLock()
    {
        m_csWriters.WriteLock();
        ResetEvent(m_writerDoneEvent);
        m_writeRequested = true;
        {
            // Locking m_csMap here also prevents new readers from acquiring
            // this read/write mutex, since they would be blocked upon
            // calling getTlsData(), which also locks m_csMap.
            CriticalSection::ScopedWriteLock lk(m_csMap);
            for (AccessMap::iterator iter = m_accessMap.begin(), end = m_accessMap.end();
                 iter != end; ++iter)
            {
                TlsData* pTlsData = iter->second;
                while (pTlsData->isReading)
                {
                    WaitForSingleObject(pTlsData->readerDoneEvent, INFINITE);
                }
            }
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
        TlsData* pTlsData = getTlsData();
        pTlsData->isReading = true;
        while (m_writeRequested)
        {
            pTlsData->isReading = false;
            SetEvent(pTlsData->readerDoneEvent);
            WaitForSingleObject(m_writerDoneEvent, INFINITE);
            pTlsData->isReading = true;
        }
    }
    void ReadUnlock()
    {
        TlsData* pTlsData = getTlsData();
        pTlsData->isReading = false;
        if (m_writeRequested)
        {
            SetEvent(pTlsData->readerDoneEvent);
        }
    }

    typedef ScopedWriteLock<UltraFastReadWriteMutex> ScopedWriteLock;
    typedef ScopedReadLock<UltraFastReadWriteMutex> ScopedReadLock;
};


class UltraSpinSingleReadWriteMutex
{
#define CACHE_LINE_SIZE 64
    // treated as an atomic bool
    volatile DWORD m_writeRequested;
    volatile char m_pad0[CACHE_LINE_SIZE - 4];
    volatile DWORD m_isReading;
    volatile char m_pad1[CACHE_LINE_SIZE - 4];
#undef CACHE_LINE_SIZE

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

class UltraSyncSingleReadWriteMutex
{
#define CACHE_LINE_SIZE 64
    // treated as an atomic bool
    volatile DWORD m_writeRequested;
    volatile char m_pad0[CACHE_LINE_SIZE - 4];
    volatile DWORD m_isReading;
    volatile char m_pad1[CACHE_LINE_SIZE - 4];
#undef CACHE_LINE_SIZE

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

// NOTE: UltraSyncSingleReadWriteMutex completely dominates UltraSpinSingleReadWriteMutex.
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
