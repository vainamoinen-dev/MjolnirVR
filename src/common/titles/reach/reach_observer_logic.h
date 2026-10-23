// Reach observer camera checks: byte-pattern counting, rel32 call decoding, view classification and camera validation.
// Used by src/dll/core/game.cpp; tested in tests/core_tests.cpp.
// Pure logic: no Windows calls, no globals, no logging.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

inline constexpr uint64_t kReachObserverFreshMs = 500;
inline constexpr uint64_t kReachObserverStableMs = 1000;
inline constexpr float kReachCameraAxisTolerance = 0.001f;
inline constexpr float kReachCameraFovMin = 0.0001f;
inline constexpr float kReachCameraFovMax = 3.1414928436f;

inline size_t CountReachExactPattern(const uint8_t* bytes, size_t size, const uint8_t* pattern, size_t patternSize)
{
    if (!bytes || !pattern || !patternSize || size < patternSize)
        return 0;
    size_t count = 0;
    for (size_t i = 0; i <= size - patternSize; ++i)
    {
        if (std::memcmp(bytes + i, pattern, patternSize) == 0)
            ++count;
    }
    return count;
}

inline bool ResolveReachRel32Call(uintptr_t instructionAddress, const uint8_t* bytes, size_t size, uintptr_t& target)
{
    if (!bytes || size < 5 || bytes[0] != 0xE8 || instructionAddress > std::numeric_limits<uintptr_t>::max() - 5)
        return false;
    int32_t displacement = 0;
    std::memcpy(&displacement, bytes + 1, sizeof(displacement));
    const uintptr_t next = instructionAddress + 5;
    if (displacement >= 0)
    {
        const uintptr_t positive = static_cast<uint32_t>(displacement);
        if (positive > std::numeric_limits<uintptr_t>::max() - next)
            return false;
        target = next + positive;
    }
    else
    {
        const uintptr_t magnitude = static_cast<uintptr_t>(-(static_cast<int64_t>(displacement)));
        if (magnitude > next)
            return false;
        target = next - magnitude;
    }
    return true;
}

enum class ReachObservedViewKind : uint8_t
{
    None = 0,
    NormalPlayerSlot,
    OutsidePlayerArray,
};

struct ReachObservedView
{
    ReachObservedViewKind kind = ReachObservedViewKind::None;
    uint32_t slot = 0;
};

inline ReachObservedView ClassifyReachObservedView(uintptr_t pointer, uintptr_t arrayBase, size_t stride, size_t count)
{
    if (!pointer)
        return {};
    if (!arrayBase || !stride || !count || count > (std::numeric_limits<uintptr_t>::max() - arrayBase) / stride)
        return {ReachObservedViewKind::OutsidePlayerArray, 0};

    const uintptr_t arrayEnd = arrayBase + stride * count;
    if (pointer < arrayBase || pointer >= arrayEnd)
        return {ReachObservedViewKind::OutsidePlayerArray, 0};

    const uintptr_t offset = pointer - arrayBase;
    if (offset % stride != 0)
        return {ReachObservedViewKind::OutsidePlayerArray, 0};
    return {
        ReachObservedViewKind::NormalPlayerSlot,
        static_cast<uint32_t>(offset / stride),
    };
}

struct ReachObservedRect
{
    int16_t y0 = 0;
    int16_t x0 = 0;
    int16_t y1 = 0;
    int16_t x1 = 0;
};

struct ReachCompactCameraObservation
{
    float position[3]{};
    float forward[3]{};
    float up[3]{};
    float verticalFov = 0.0f;
    ReachObservedRect windowBounds{};
    ReachObservedRect renderBounds{};
    ReachObservedRect clientBounds{};
};

// The observer can attach while Reach is already inside a short set/use/clear
// scope. Never label that first non-null value as a witnessed transaction.
// Every new observation session and every continuity reset must first observe a
// clear value; later non-null pointer changes are accepted as new transactions.
class ReachObserverTransactionGate
{
public:
    bool Observe(uintptr_t pointer)
    {
        if (!pointer)
        {
            m_awaitingClear = false;
            m_activeLatched = false;
            m_latchedPointer = 0;
            return false;
        }
        if (m_awaitingClear)
            return false;
        if (m_activeLatched && m_latchedPointer == pointer)
            return false;
        m_activeLatched = true;
        m_latchedPointer = pointer;
        return true;
    }

    void RequireClear()
    {
        m_awaitingClear = true;
        m_activeLatched = false;
        m_latchedPointer = 0;
    }

    bool HasLatchedValue() const { return m_activeLatched; }

private:
    bool m_awaitingClear = true;
    bool m_activeLatched = false;
    uintptr_t m_latchedPointer = 0;
};

namespace reach_observer_detail
{
    template <typename T>
    inline T Load(const uint8_t* bytes, size_t offset)
    {
        T value{};
        std::memcpy(&value, bytes + offset, sizeof(value));
        return value;
    }

    inline ReachObservedRect LoadRect(const uint8_t* bytes, size_t offset)
    {
        ReachObservedRect result{};
        std::memcpy(&result, bytes + offset, sizeof(result));
        return result;
    }

    inline bool Ordered(const ReachObservedRect& rect)
    {
        return rect.y0 < rect.y1 && rect.x0 < rect.x1;
    }
}

// Reach-only evidence establishes this compact-camera layout. HREK validates
// position/forward/up at these exact offsets, including the strict normalized
// axis and orthogonality checks reproduced below. The bounds are signed i16 in
// y0,x0,y1,x1 order.
inline bool ValidateReachCompactCamera(const uint8_t* bytes, size_t size, ReachCompactCameraObservation& out)
{
    if (!bytes || size < 0x90)
        return false;

    for (size_t i = 0; i < 3; ++i)
    {
        out.position[i] = reach_observer_detail::Load<float>(bytes, 0x00 + i * sizeof(float));
        out.forward[i] = reach_observer_detail::Load<float>(bytes, 0x0C + i * sizeof(float));
        out.up[i] = reach_observer_detail::Load<float>(bytes, 0x18 + i * sizeof(float));
        if (!std::isfinite(out.position[i]) || !std::isfinite(out.forward[i]) || !std::isfinite(out.up[i]))
            return false;
    }

    const float forwardLengthSquared =
        out.forward[0] * out.forward[0] + out.forward[1] * out.forward[1] + out.forward[2] * out.forward[2];
    const float upLengthSquared = out.up[0] * out.up[0] + out.up[1] * out.up[1] + out.up[2] * out.up[2];
    const float dot = out.forward[0] * out.up[0] + out.forward[1] * out.up[1] + out.forward[2] * out.up[2];
    if (!std::isfinite(forwardLengthSquared) || !std::isfinite(upLengthSquared) || !std::isfinite(dot) ||
        std::fabs(forwardLengthSquared - 1.0f) >= kReachCameraAxisTolerance ||
        std::fabs(upLengthSquared - 1.0f) >= kReachCameraAxisTolerance || std::fabs(dot) >= kReachCameraAxisTolerance)
        return false;

    out.verticalFov = reach_observer_detail::Load<float>(bytes, 0x28);
    if (!std::isfinite(out.verticalFov) || out.verticalFov <= kReachCameraFovMin ||
        out.verticalFov >= kReachCameraFovMax)
        return false;

    out.windowBounds = reach_observer_detail::LoadRect(bytes, 0x38);
    out.renderBounds = reach_observer_detail::LoadRect(bytes, 0x4C);
    out.clientBounds = reach_observer_detail::LoadRect(bytes, 0x5C);
    if (!reach_observer_detail::Ordered(out.windowBounds) || !reach_observer_detail::Ordered(out.renderBounds) ||
        !reach_observer_detail::Ordered(out.clientBounds))
        return false;

    // Reach's GetClientRect-backed producer writes a zero origin and enforces
    // an eight-pixel minimum before publishing these current display bounds.
    return out.clientBounds.y0 == 0 && out.clientBounds.x0 == 0 && out.clientBounds.y1 >= 8 && out.clientBounds.x1 >= 8;
}

class ReachObserverFreshnessWindow
{
public:
    bool ObserveTransaction(uint64_t now)
    {
        if (!m_hasTransaction || now < m_lastTransaction || now - m_lastTransaction >= kReachObserverFreshMs)
        {
            m_firstTransaction = now;
            m_transactionCount = 1;
        }
        else
        {
            ++m_transactionCount;
        }
        m_lastTransaction = now;
        m_hasTransaction = true;
        return IsStable(now);
    }

    bool Tick(uint64_t now)
    {
        if (m_hasTransaction && (now < m_lastTransaction || now - m_lastTransaction >= kReachObserverFreshMs))
            Reset();
        return IsStable(now);
    }

    bool IsFresh(uint64_t now) const
    {
        return m_hasTransaction && now >= m_lastTransaction && now - m_lastTransaction < kReachObserverFreshMs;
    }

    bool IsStable(uint64_t now) const
    {
        return IsFresh(now) && m_transactionCount >= 2 && m_lastTransaction >= m_firstTransaction &&
               m_lastTransaction - m_firstTransaction > kReachObserverStableMs;
    }

    uint64_t CurrentSpanMs() const
    {
        return m_hasTransaction && m_lastTransaction >= m_firstTransaction ? m_lastTransaction - m_firstTransaction : 0;
    }

    uint64_t LastTransactionMs() const { return m_hasTransaction ? m_lastTransaction : 0; }

    uint64_t TransactionCount() const { return m_transactionCount; }

    void Reset()
    {
        m_hasTransaction = false;
        m_firstTransaction = 0;
        m_lastTransaction = 0;
        m_transactionCount = 0;
    }

private:
    bool m_hasTransaction = false;
    uint64_t m_firstTransaction = 0;
    uint64_t m_lastTransaction = 0;
    uint64_t m_transactionCount = 0;
};

inline constexpr int kReachNoFreshOwner = -1;
inline constexpr int kReachMultipleFreshOwners = -2;

inline int ReachObserverUniqueFreshOwner(const ReachObserverFreshnessWindow* windows, size_t count, uint64_t now)
{
    if (!windows || !count)
        return kReachNoFreshOwner;
    int owner = kReachNoFreshOwner;
    for (size_t slot = 0; slot < count; ++slot)
    {
        if (!windows[slot].IsFresh(now))
            continue;
        if (owner != kReachNoFreshOwner)
            return kReachMultipleFreshOwners;
        owner = static_cast<int>(slot);
    }
    return owner;
}
