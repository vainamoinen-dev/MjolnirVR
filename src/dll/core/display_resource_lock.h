// display_resource_lock.h: the lock that serialises the game swapchain's display resources, and its RAII guard.
// Defined in vr.cpp (core). Taken by the Reach display worker, by authored-reticle preparation for every title,
// and across a swapchain resize. Core owns it; title code only borrows it.
#pragma once
#include <windows.h>

namespace vr_core
{
    extern SRWLOCK g_reachDisplayResourceLock;

    class ReachExclusiveResourceLock
    {
    public:
        explicit ReachExclusiveResourceLock(SRWLOCK& lock) noexcept : m_lock(&lock) { AcquireSRWLockExclusive(m_lock); }
        ~ReachExclusiveResourceLock() { ReleaseSRWLockExclusive(m_lock); }
        ReachExclusiveResourceLock(const ReachExclusiveResourceLock&) = delete;
        ReachExclusiveResourceLock& operator=(const ReachExclusiveResourceLock&) = delete;

    private:
        PSRWLOCK m_lock;
    };
} // namespace vr_core
