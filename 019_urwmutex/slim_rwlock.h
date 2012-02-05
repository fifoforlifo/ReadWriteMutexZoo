#pragma once

#include "common.h"
#include "scoped_locks.h"

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
