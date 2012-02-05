#pragma once

#include "common.h"
#include "scoped_locks.h"

/// Originally inspired by http://vorlon.case.edu/~jrh23/338/HW3.pdf
template <class TSemaphore>
class FairReadWriteMutex
{
private:
    TSemaphore m_queueSema, m_writerSema;
    volatile long m_readerCount;

public:
    FairReadWriteMutex()
        : m_queueSema(1, 1)
        , m_writerSema(1, 1)
    {
        m_readerCount = 0;
    }

    void WriteLock()
    {
        m_queueSema.P();
        m_writerSema.P();
        m_queueSema.V();
        _ASSERT(m_readerCount == 0);
    }
    void WriteUnlock()
    {
        m_writerSema.V();
    }

    void ReadLock()
    {
        m_queueSema.P();

        long count = _InterlockedIncrement(&m_readerCount);
        if (count == 1)
        {
            m_writerSema.P();
        }

        m_queueSema.V();
    }
    void ReadUnlock()
    {
        long count = _InterlockedDecrement(&m_readerCount);
        if (count == 0)
        {
            m_writerSema.V();
        }
    }

    typedef ScopedWriteLock<FairReadWriteMutex<TSemaphore> > ScopedWriteLock;
    typedef ScopedReadLock<FairReadWriteMutex<TSemaphore> > ScopedReadLock;
};