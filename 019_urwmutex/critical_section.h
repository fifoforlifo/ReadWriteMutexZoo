#pragma once

#include "common.h"
#include "scoped_locks.h"

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
