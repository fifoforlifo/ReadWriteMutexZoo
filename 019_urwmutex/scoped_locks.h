#pragma once

#include "common.h"

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
