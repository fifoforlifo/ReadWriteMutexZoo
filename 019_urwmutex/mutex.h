#pragma once

#include "common.h"
#include "scoped_locks.h"

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
