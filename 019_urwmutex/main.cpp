#include <stdio.h>
#include <vector>
#include "urwmutex.h"


struct Stats
{
    double durationSeconds;
    double readsPerSecond;
    double writesPerSecond;
    double totalPerSecond;
    long numThreads;
    double readRatio;
    double writeRatio;
};


template <class TMutex>
class Test
{
public:
    Test(
        long readerThreadCount,
        long writerThreadCount)
    {
        m_hStartEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        m_done = 0;

        m_readerThreadCount = readerThreadCount;
        m_writerThreadCount = writerThreadCount;
        m_readerLockCount = 0;
        m_writerLockCount = 0;
    }
    ~Test()
    {
        CloseHandle(m_hStartEvent);
    }

    Stats Execute()
    {
        std::vector<HANDLE> threadHandles;

        const SIZE_T stackSize = 0x10000;
        for (long i = 0; i < m_readerThreadCount; i++)
        {
            HANDLE hThread = CreateThread(NULL, stackSize, (LPTHREAD_START_ROUTINE)&ReaderThreadProc, this, 0, NULL);
            threadHandles.push_back(hThread);
        }
        for (long i = 0; i < m_writerThreadCount; i++)
        {
            HANDLE hThread = CreateThread(NULL, 0x20000, (LPTHREAD_START_ROUTINE)&WriterThreadProc, this, 0, NULL);
            threadHandles.push_back(hThread);
        }
        long durationMilliseconds = 2000;
        printf("Running test for %d milliseconds...\n", durationMilliseconds);  fflush(stdout);
        // Allow all the threads to begin processing.
        SetEvent(m_hStartEvent);
        Sleep(durationMilliseconds);
        m_done = true;

        // Wait for all threads to complete.  If they don't, we probably have a deadlock.
        for (size_t u = 0; u < threadHandles.size(); u++)
        {
            HANDLE hThread = threadHandles[u];
            WaitForSingleObject(hThread, INFINITE);
        }

        // report statistics
        Stats stats = {0};
        stats.durationSeconds   = durationMilliseconds / 1000.0; 
        stats.readsPerSecond    = m_readerLockCount * 1.0 / stats.durationSeconds;
        stats.writesPerSecond   = m_writerLockCount * 1.0 / stats.durationSeconds;
        stats.totalPerSecond    = stats.readsPerSecond + stats.writesPerSecond;
        stats.numThreads        = m_readerThreadCount + m_writerThreadCount;
        stats.readRatio         = stats.readsPerSecond * stats.numThreads / stats.totalPerSecond;
        stats.writeRatio        = stats.writesPerSecond * stats.numThreads / stats.totalPerSecond;
        printf("readsPerSecond                    = %13.1f\n", stats.readsPerSecond);
        printf("writesPerSecond                   = %13.1f\n", stats.writesPerSecond);
        printf("totalPerSecond                    = %13.1f\n", stats.totalPerSecond);
        printf("numThreads                        = %d\n", stats.numThreads);
        printf("readerThreadCount=%3d, readRatio  = %13.1f\n",  m_readerThreadCount, stats.readRatio);
        printf("writerThreadCount=%3d, writeRatio = %13.1f\n", m_writerThreadCount, stats.writeRatio);
        printf("{%3.3dR, %3.3dW} : %13.1f\n", m_readerThreadCount, m_writerThreadCount, stats.totalPerSecond);
        printf("\n");

        return stats;
    }

private:
    void WriterThread()
    {
        WaitForSingleObject(m_hStartEvent, INFINITE);

        int count = 0;
        while (!m_done)
        {
            TMutex::ScopedWriteLock lk(m_mutex);
            count += 1;
        }

        {
            CriticalSection::ScopedWriteLock lk(m_countCs);
            m_writerLockCount += count;
        }
    }
    static void WriterThreadProc(void* p)
    {
        Test* pTest = (Test*)p;
        pTest->WriterThread();
    }

    void ReaderThread()
    {
        WaitForSingleObject(m_hStartEvent, INFINITE);

        int count = 0;
        while (!m_done)
        {
            TMutex::ScopedReadLock lk(m_mutex);
            count += 1;
        }

        {
            CriticalSection::ScopedWriteLock lk(m_countCs);
            m_readerLockCount += count;
        }
    }
    static void ReaderThreadProc(void* p)
    {
        Test* pTest = (Test*)p;
        pTest->ReaderThread();
    }

private:
    TMutex m_mutex;

    // Manual-reset-event that gates the execution of the test threads.
    HANDLE m_hStartEvent;
    volatile long m_done;

    long m_readerThreadCount;
    long m_writerThreadCount;

    CriticalSection m_countCs;

#define CACHE_LINE_SIZE 64
    volatile char pad0[CACHE_LINE_SIZE - 4];
    volatile __int64 m_readerLockCount;
    volatile char pad1[CACHE_LINE_SIZE - 8];
    volatile __int64 m_writerLockCount;
    volatile char pad2[CACHE_LINE_SIZE - 8];
#undef CACHE_LINE_SIZE
};


void DoTests(const long numReaders, const long numWriters, std::vector<Stats>& statss)
{
    Stats stats;

#if 0
    // NOTE: is perfectly fair
    printf("Mutex:\n");
    Test<Mutex> test_Mutex(numReaders, numWriters);
    test_Mutex.Execute();
#endif

#if 0
    // NOTE: is kind of fair
    printf("CriticalSection:\n");
    Test<CriticalSection> test_CriticalSection(numReaders, numWriters);
    test_CriticalSection.Execute();
#endif

#if 0
    // NOTE: is not fair
    printf("SlimReadWriteLock:\n");
    Test<SlimReadWriteLock> test_SlimReadWriteLock(numReaders, numWriters);
    test_SlimReadWriteLock.Execute();
#endif

#if 01
    // NOTE: is not fair
    printf("UltraSpinReadWriteMutex:\n");
    Test<UltraSpinReadWriteMutex> test_UltraSpinReadWriteMutex(numReaders, numWriters);
    stats = test_UltraSpinReadWriteMutex.Execute();
    statss.push_back(stats);
#endif

#if 01
    // NOTE: is not fair
    printf("UltraFastReadWriteMutex:\n");
    Test<UltraFastReadWriteMutex> test_UltraFastReadWriteMutex(numReaders, numWriters);
    stats = test_UltraFastReadWriteMutex.Execute();
    statss.push_back(stats);
#endif
}

void TestUltraSingleReadWriteMutex()
{
    Stats stats;

    printf("UltraSpinSingleReadWriteMutex:\n");
    Test<UltraSpinSingleReadWriteMutex> test_warmup(1, 0);
    stats = test_warmup.Execute();

    printf("UltraSpinSingleReadWriteMutex:\n");
    Test<UltraSpinSingleReadWriteMutex> test_UltraSpinSingleReadWriteMutex01(0, 1);
    stats = test_UltraSpinSingleReadWriteMutex01.Execute();

    printf("UltraSpinSingleReadWriteMutex:\n");
    Test<UltraSpinSingleReadWriteMutex> test_UltraSpinSingleReadWriteMutex10(1, 0);
    stats = test_UltraSpinSingleReadWriteMutex10.Execute();

    printf("UltraSpinSingleReadWriteMutex:\n");
    Test<UltraSpinSingleReadWriteMutex> test_UltraSpinSingleReadWriteMutex11(1, 1);
    stats = test_UltraSpinSingleReadWriteMutex11.Execute();

    printf("UltraSyncSingleReadWriteMutex:\n");
    Test<UltraSyncSingleReadWriteMutex> test_UltraSyncSingleReadWriteMutex01(0, 1);
    stats = test_UltraSyncSingleReadWriteMutex01.Execute();

    printf("UltraSyncSingleReadWriteMutex:\n");
    Test<UltraSyncSingleReadWriteMutex> test_UltraSyncSingleReadWriteMutex10(1, 0);
    stats = test_UltraSyncSingleReadWriteMutex10.Execute();

    printf("UltraSyncSingleReadWriteMutex:\n");
    Test<UltraSyncSingleReadWriteMutex> test_UltraSyncSingleReadWriteMutex11(1, 1);
    stats = test_UltraSyncSingleReadWriteMutex11.Execute();
}

int main()
{
    //TestUltraSingleReadWriteMutex();
    //return 0;

    const int trials = 10;

    std::vector<Stats> statss;
    for (int numReaders = 1; numReaders <= trials; numReaders++)
    {
        DoTests(numReaders, 0, statss);
    }

    printf("\ncsv =\n");
    const int testsPerTrial = (int)statss.size() / trials;
    for (int test = 0; test < testsPerTrial; test++)
    {
        for (int trial = 0; trial < trials; trial++)
        {
            Stats const& stats = statss[trial * testsPerTrial + test];
            printf("%9f, ", stats.readsPerSecond);
        }
        printf("\n");
    }
    printf("\n");


    return 0;
}
