#pragma once

#include "common.h"
#include "scoped_locks.h"

// based on http://doc.trolltech.com/qq/qq11-mutex.html

template <class TMutex, class TSemaphore, long TMaxConcurrentReaders>
class QtReadWriteMutex
{
private:
    TMutex m_mutex;
    TSemaphore m_sema;

public:
    QtReadWriteMutex() : m_sema(TMaxConcurrentReaders, TMaxConcurrentReaders)
    {
    }

    void WriteLock()
    {
        m_mutex.WriteLock();
        for (long i = 0; i < TMaxConcurrentReaders; i++)
        {
            m_sema.P();
        }
        m_mutex.WriteUnlock();

    }
    void WriteUnlock()
    {
        m_sema.V(TMaxConcurrentReaders);
    }

    void ReadLock()
    {
        m_sema.P();
    }
    void ReadUnlock()
    {
        m_sema.V();
    }

    typedef ScopedWriteLock<QtReadWriteMutex<TMutex, TSemaphore, TMaxConcurrentReaders> > ScopedWriteLock;
    typedef ScopedReadLock<QtReadWriteMutex<TMutex, TSemaphore, TMaxConcurrentReaders> > ScopedReadLock;

    class ScopedWriteLock
    {
    public:
        ScopedWriteLock(QtReadWriteMutex* pMutex) : m_pMutex(pMutex)
        {
            m_pMutex->WriteLock();
        }
        ~ScopedWriteLock()
        {
            m_pMutex->WriteUnlock();
        }

    private:
        QtReadWriteMutex* m_pMutex;
    };

    class ScopedReadLock
    {
    public:
        ScopedReadLock(QtReadWriteMutex* pMutex) : m_pMutex(pMutex)
        {
            m_pMutex->ReadLock();
        }
        ~ScopedReadLock()
        {
            m_pMutex->ReadUnlock();
        }

    private:
        QtReadWriteMutex* m_pMutex;
    };
};
