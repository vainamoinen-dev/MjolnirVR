#pragma once
// Which first-person arm rigs are known, and a once-per-rig note of unknown ones.
// Used by dll/core/game.cpp and tests/fp_rig_coverage_tests.cpp.
//
// First-person rig coverage. Reach (47/41-node HREK arms) and
// Halo 4 (80-node storm_fp) only re-root hands on pinned rigs; any other rig
// stays stock with its arms showing. The render hooks note each unmapped rig
// (lock-free, hot path); the poll thread logs each distinct one once, so a
// tester can send the line and the rig can be pinned.
//
// Pure logic: no globals.
#include "runtime_types.h"
#include <atomic>
#include <cstdint>

namespace fp_rig_coverage
{
    inline constexpr uint64_t PackRig(GameTitle title, int32_t nodeCount, uint32_t id) noexcept
    {
        return (uint64_t{static_cast<uint8_t>(title)} << 56) |
               (uint64_t{static_cast<uint32_t>(nodeCount) & 0xFFFFFFu} << 32) | id;
    }
    inline constexpr GameTitle RigTitle(uint64_t key) noexcept
    {
        return static_cast<GameTitle>(key >> 56);
    }
    inline constexpr int32_t RigNodeCount(uint64_t key) noexcept
    {
        return static_cast<int32_t>((key >> 32) & 0xFFFFFFu);
    }
    inline constexpr uint32_t RigId(uint64_t key) noexcept
    {
        return static_cast<uint32_t>(key);
    }

    // FNV-1a over a palette bone map: tells two rigs with the same node count apart.
    inline uint32_t BoneMapFingerprint(const int32_t* map, int count) noexcept
    {
        uint32_t h = 2166136261u;
        for (int i = 0; map && i < count; ++i)
            for (int b = 0; b < 4; ++b)
            {
                h ^= (static_cast<uint32_t>(map[i]) >> (8 * b)) & 0xFFu;
                h *= 16777619u;
            }
        return h;
    }

    class UnmappedRigLog
    {
    public:
        void Note(uint64_t key) noexcept
        {
            if (key)
                m_pending.store(key, std::memory_order_relaxed);
        }
        // Poll thread: true once per distinct rig (up to kSeen; later ones still log).
        bool TakeNew(uint64_t& key) noexcept
        {
            key = m_pending.exchange(0, std::memory_order_relaxed);
            if (!key)
                return false;
            for (unsigned i = 0; i < m_count; ++i)
                if (m_seen[i] == key)
                    return false;
            if (m_count < kSeen)
                m_seen[m_count++] = key;
            return true;
        }
        void Reset() noexcept
        {
            m_count = 0;
            m_pending.store(0, std::memory_order_relaxed);
        }

    private:
        static constexpr unsigned kSeen = 32;
        std::atomic<uint64_t> m_pending{0};
        uint64_t m_seen[kSeen]{};
        unsigned m_count = 0;
    };
} // namespace fp_rig_coverage
