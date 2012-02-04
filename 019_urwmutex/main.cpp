#include <stdio.h>
#include <vector>
#include "urwmutex.h"

//#define READER_LOOP_NL_SLEEP Sleep(0)
//#define WRITER_LOOP_NL_SLEEP Sleep(0)
#define READER_LOOP_NL_SLEEP
#define WRITER_LOOP_NL_SLEEP
//#define READER_LOOP_LK_SLEEP Sleep(0)
//#define WRITER_LOOP_LK_SLEEP Sleep(0)
#define READER_LOOP_LK_SLEEP
#define WRITER_LOOP_LK_SLEEP


struct Stats
{
    std::string name;
    double durationSeconds;
    double readsPerSecond;
    double writesPerSecond;
    double totalPerSecond;
    long numThreads;
    double readRatio;
    double writeRatio;
    long r1NumThreads;
    double r1ReadRatio;
    double r1WriteRatio;
};


template <class TMutex>
class Test
{
public:
    Test(
        long readerThreadCount,
        long writerThreadCount,
        const char* pName)
    {
        m_hStartEvent = CreateEventA(NULL, TRUE, FALSE, NULL);
        m_done = 0;

        m_readerThreadCount = readerThreadCount;
        m_writerThreadCount = writerThreadCount;
        m_readerLockCount = 0;
        m_writerLockCount = 0;

        m_name = pName ? pName : "";
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
        Stats stats;
        stats.name = m_name;
        stats.durationSeconds   = durationMilliseconds / 1000.0; 
        stats.readsPerSecond    = m_readerLockCount * 1.0 / stats.durationSeconds;
        stats.writesPerSecond   = m_writerLockCount * 1.0 / stats.durationSeconds;
        stats.totalPerSecond    = stats.readsPerSecond + stats.writesPerSecond;
        stats.numThreads        = m_readerThreadCount + m_writerThreadCount;
        stats.readRatio         = stats.readsPerSecond * stats.numThreads / stats.totalPerSecond;
        stats.writeRatio        = stats.writesPerSecond * stats.numThreads / stats.totalPerSecond;
        stats.r1NumThreads      = 1 + m_writerThreadCount;
        stats.r1ReadRatio       = stats.readsPerSecond * stats.r1NumThreads / stats.totalPerSecond;
        stats.r1WriteRatio      = stats.writesPerSecond * stats.r1NumThreads / stats.totalPerSecond;
        printf("%s:\n", m_name.c_str());
        printf("readsPerSecond                    = %13.1f\n", stats.readsPerSecond);
        printf("writesPerSecond                   = %13.1f\n", stats.writesPerSecond);
        printf("totalPerSecond                    = %13.1f\n", stats.totalPerSecond);
        printf("numThreads                        = %d\n", stats.numThreads);
        printf("readerThreadCount=%3d, readRatio  = %13.6f\n", m_readerThreadCount, stats.readRatio);
        printf("writerThreadCount=%3d, writeRatio = %13.6f\n", m_writerThreadCount, stats.writeRatio);
        printf("r1NumThreads                      = %d\n", stats.r1NumThreads);
        printf("r1ReadRatio                       = %13.6f\n", stats.r1ReadRatio);
        printf("r1WriteRatio                      = %13.6f\n", stats.r1WriteRatio);
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
            WRITER_LOOP_NL_SLEEP;
            TMutex::ScopedWriteLock lk(m_mutex);
            count += 1;
            WRITER_LOOP_LK_SLEEP;
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
            READER_LOOP_NL_SLEEP;
            TMutex::ScopedReadLock lk(m_mutex);
            count += 1;
            READER_LOOP_LK_SLEEP;
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

    volatile char pad0[CACHE_LINE_SIZE - 8];
    volatile __int64 m_readerLockCount;
    volatile char pad1[CACHE_LINE_SIZE - 8];
    volatile __int64 m_writerLockCount;
    volatile char pad2[CACHE_LINE_SIZE - 8];

    std::string m_name;
};


void DoTests(
    const long numReaders,
    const long numWriters,
    std::vector<Stats>& statss)
{
    const char* pName = "";
    Stats stats;

#if 0
    // NOTE: is perfectly fair
    pName = "Mutex";
    Test<Mutex> test_Mutex(numReaders, numWriters);
    test_Mutex.Execute();
#endif

#if 0
    // NOTE: is kind of fair
    pName = "CriticalSection";
    Test<CriticalSection> test_CriticalSection(numReaders, numWriters, pName);
    test_CriticalSection.Execute();
#endif

#if 0
    // NOTE: is not fair
    pName = "SlimReadWriteLock";
    Test<SlimReadWriteLock> test_SlimReadWriteLock(numReaders, numWriters, pName);
    test_SlimReadWriteLock.Execute();
#endif

#if 0
    // NOTE: is kind of fair
    pName = "UltraSpinReadWriteMutex";
    Test<UltraSpinReadWriteMutex> test_UltraSpinReadWriteMutex(numReaders, numWriters, pName);
    stats = test_UltraSpinReadWriteMutex.Execute();
    statss.push_back(stats);
#endif

#if 0
    // NOTE: is kind of fair
    pName = "UltraFastReadWriteMutex";
    Test<UltraFastReadWriteMutex> test_UltraFastReadWriteMutex(numReaders, numWriters, pName);
    stats = test_UltraFastReadWriteMutex.Execute();
    statss.push_back(stats);
#endif

#if 0
    // NOTE: is kind of fair
    pName = "UltraLightReadWriteMutex";
    Test<UltraLightReadWriteMutex> test_UltraLightReadWriteMutex(numReaders, numWriters, pName);
    stats = test_UltraLightReadWriteMutex.Execute();
    statss.push_back(stats);
#endif

#if 01
    // NOTE: is kind of fair
    pName = "FastFairReadWriteMutex";
    Test<FastFairReadWriteMutex> test_FastFairReadWriteMutex(numReaders, numWriters, pName);
    stats = test_FastFairReadWriteMutex.Execute();
    statss.push_back(stats);
#endif
}

void TestUltraSingleReadWriteMutex()
{
    const char* pName = "";
    Stats stats;

    pName = "UltraSpinSingleReadWriteMutex";
    Test<UltraSpinSingleReadWriteMutex> test_UltraSpinSingleReadWriteMutex01(0, 1, pName);
    stats = test_UltraSpinSingleReadWriteMutex01.Execute();

    pName = "UltraSpinSingleReadWriteMutex";
    Test<UltraSpinSingleReadWriteMutex> test_UltraSpinSingleReadWriteMutex10(1, 0, pName);
    stats = test_UltraSpinSingleReadWriteMutex10.Execute();

    pName = "UltraSpinSingleReadWriteMutex";
    Test<UltraSpinSingleReadWriteMutex> test_UltraSpinSingleReadWriteMutex11(1, 1, pName);
    stats = test_UltraSpinSingleReadWriteMutex11.Execute();

    pName = "UltraSyncSingleReadWriteMutex";
    Test<UltraSyncSingleReadWriteMutex> test_UltraSyncSingleReadWriteMutex01(0, 1, pName);
    stats = test_UltraSyncSingleReadWriteMutex01.Execute();

    pName = "UltraSyncSingleReadWriteMutex";
    Test<UltraSyncSingleReadWriteMutex> test_UltraSyncSingleReadWriteMutex10(1, 0, pName);
    stats = test_UltraSyncSingleReadWriteMutex10.Execute();

    pName = "UltraSyncSingleReadWriteMutex";
    Test<UltraSyncSingleReadWriteMutex> test_UltraSyncSingleReadWriteMutex11(1, 1, pName);
    stats = test_UltraSyncSingleReadWriteMutex11.Execute();
}

int main()
{
    {
        const char* pName = "warmup";
        Test<UltraSpinReadWriteMutex> test_warmup(1, 0, pName);
        test_warmup.Execute();
    }

    //TestUltraSingleReadWriteMutex();
    //return 0;

    const int readerTrials = 6;
    const int writerTrials = 2;
    const int trials = readerTrials * writerTrials;

    std::vector<Stats> statss;
    for (int numWriters = 1; numWriters <= writerTrials; numWriters++)
    {
        for (int numReaders = 1; numReaders <= readerTrials; numReaders++)
        {
            DoTests(numReaders, numWriters, statss);
        }
    }

    printf("\ncsv =\n");
    const int testsPerTrial = (int)statss.size() / trials;
    for (int test = 0; test < testsPerTrial; test++)
    {
        printf("\"%s rps\",", statss[test].name.c_str());
        for (int trial = 0; trial < trials; trial++)
        {
            Stats const& stats = statss[trial * testsPerTrial + test];
            printf("%9f,", stats.readsPerSecond);
        }
        printf("\n");
    }
    for (int test = 0; test < testsPerTrial; test++)
    {
        printf("\"%s wps\",", statss[test].name.c_str());
        for (int trial = 0; trial < trials; trial++)
        {
            Stats const& stats = statss[trial * testsPerTrial + test];
            printf("%9f,", stats.writesPerSecond);
        }
        printf("\n");
    }
    for (int test = 0; test < testsPerTrial; test++)
    {
        printf("\"%s rr\",", statss[test].name.c_str());
        for (int trial = 0; trial < trials; trial++)
        {
            Stats const& stats = statss[trial * testsPerTrial + test];
            printf("%9f,", stats.readRatio);
        }
        printf("\n");
    }
    for (int test = 0; test < testsPerTrial; test++)
    {
        printf("\"%s wr\",", statss[test].name.c_str());
        for (int trial = 0; trial < trials; trial++)
        {
            Stats const& stats = statss[trial * testsPerTrial + test];
            printf("%9f,", stats.writeRatio);
        }
        printf("\n");
    }
    for (int test = 0; test < testsPerTrial; test++)
    {
        printf("\"%s r1rr\",", statss[test].name.c_str());
        for (int trial = 0; trial < trials; trial++)
        {
            Stats const& stats = statss[trial * testsPerTrial + test];
            printf("%9f,", stats.r1ReadRatio);
        }
        printf("\n");
    }
    for (int test = 0; test < testsPerTrial; test++)
    {
        printf("\"%s r1wr\",", statss[test].name.c_str());
        for (int trial = 0; trial < trials; trial++)
        {
            Stats const& stats = statss[trial * testsPerTrial + test];
            printf("%9f,", stats.r1WriteRatio);
        }
        printf("\n");
    }
    printf("\n");


    return 0;
}
