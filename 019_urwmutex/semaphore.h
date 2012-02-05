#pragma once

#include "common.h"

class Semaphore
{
private:
    HANDLE m_hSemaphore;

public:
    Semaphore(long initialCount, long maxCount) : m_hSemaphore(NULL)
    {
        m_hSemaphore = CreateSemaphoreA(NULL, initialCount, maxCount, NULL);
        assert(m_hSemaphore != NULL);
    }

    ~Semaphore()
    {
        CloseHandle(m_hSemaphore);
    }

    void P()
    {
        WaitForSingleObject(m_hSemaphore, INFINITE);
    }

    long V(long count)
    {
        long prevCount;
        ReleaseSemaphore(m_hSemaphore, count, &prevCount);
        return prevCount;
    }

    long V()
    {
        return V(1);
    }
};
