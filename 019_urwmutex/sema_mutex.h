#pragma once

#include "common.h"
#include "scoped_locks.h"

template <class TSema>
class SemaMutex
{
    TSema m_sema;

public:
    SemaMutex() : m_sema(1, 1)
    {
    }

    void WriteLock()
    {
        m_sema.P();
    }
    void WriteUnlock()
    {
        m_sema.V();
    }

    void ReadLock()
    {
        WriteLock();
    }
    void ReadUnlock()
    {
        WriteUnlock();
    }

    typedef ScopedWriteLock<SemaMutex<TSema> > ScopedWriteLock;
    typedef ScopedReadLock<SemaMutex<TSema> > ScopedReadLock;
};
