// Decision core of the level-load gate: holds hook installation until a level is really
// running. Used by dll/core/game.cpp, dll/core/title_reentry_probe.cpp and tests/core_tests.cpp.
// Pure logic: no globals, no game memory, no logging.
#pragma once

#include <cstdint>

// Decision core of the level-load gate (PlayerViewLivenessGate in game.cpp).
// The gate's job: no title may install hooks while the engine is loading a
// level, because every capture of the load-bounce bug shows hooks going in
// during a loading screen and the load dying to the main menu 1.3-4 s later
// (8 bounced loads across three builds, all
// preceded by an install <= 1.6 s into the load; all 5 gated installs that
// waited for the real frozen-then-ticking transition survived).
//
// Input per 50 ms worker sample: did the engine's player-view fingerprint
// change since the last sample? Output: hold, or open with the reason.
//
// The two ways to open, and why each threshold is what it is:
//
// FROZEN, THEN TICKING - a real level load must produce this sequence: the
// loading screen leaves the player-view memory untouched (or a cold module's
// array is zeroed), then the new level's first frame writes it.
//   - The frozen half is satisfied by kFrozenMinSamples consecutive still
//     samples, and that constant is 1 ON MEASURED EVIDENCE. The captured
//     ODST pause-resume witnesses exactly ONE still sample between the
//     rearm boundary and gameplay's first tick, so any higher minimum sends
//     every pause-resume down the 6 s already-running path. An earlier build
//     shipped a 6-sample (300 ms) minimum as theoretical hardening against
//     a still hiccup inside the dying ticks, and headset testing rejected it
//     the same day as "way too slow to go into 3D". No preserved capture has
//     ever shown a frozen-then-tick misfire with the 1-sample rule: in
//     every bounced load, sampling began INSIDE the dying-tick run and no
//     stillness was ever witnessed (still=0 at the first gate log line in all
//     three bounces of an earlier build), so the hole the 6-sample rule guarded
//     is unobserved, while its cost was real and constant.
//   - The ticking half is the first change after the freeze. In every
//     capture nothing ticks the player view mid-load after the freeze
//     begins, so the first tick IS the level running.
//
// ALREADY RUNNING - a title that was mid-level when observation began never
// freezes, so uninterrupted change is accepted separately. The threshold
// must exceed the dying-tick window with margin: an earlier build shipped
// 12 samples (0.6 s), and it opened on the dying ticks of EVERY bounced
// load (all three in the preserved log). Dying ticks
// measured <= ~4 s; kAlreadyRunningSamples is 120 samples = 6 s of change
// with not one still sample, which no loading screen or menu return has
// ever produced. Cost: after a mid-play adapter flap, VR returns in 6 s
// instead of 0.6 s.
//
// There is deliberately NO time-based unconditional open. An earlier build
// carried a 12 s "open anyway" timeout; the preserved successful retry load
// froze for 11,766 ms - inside 300 ms of that timeout installing into the
// loading screen. A user idling in a lobby longer than any timeout would
// also convert it into an install-before-load. Holding is the correct state
// for a menu (the flat screen layer still shows in the headset); every real
// level entry produces frozen-then-ticking, and every already-running title
// produces the 6 s run, so no timeout is needed to avoid denying VR.
class LevelLoadGateLogic
{
public:
    enum class Decision : uint8_t
    {
        Hold = 0,
        OpenFrozenThenTicking,
        OpenAlreadyRunning,
    };

    // One still sample: the captured pause-resume only ever witnesses one.
    static constexpr uint32_t kFrozenMinSamples = 1;
    // 6 s of consecutive change at the 50 ms worker poll.
    static constexpr uint32_t kAlreadyRunningSamples = 120;

    Decision Observe(bool changed)
    {
        if (m_open)
            return m_lastOpenDecision;
        if (changed)
        {
            m_stillRun = 0;
            if (m_sawFrozen)
            {
                m_open = true;
                m_lastOpenDecision = Decision::OpenFrozenThenTicking;
                return m_lastOpenDecision;
            }
            if (++m_changeRun >= kAlreadyRunningSamples)
            {
                m_open = true;
                m_lastOpenDecision = Decision::OpenAlreadyRunning;
                return m_lastOpenDecision;
            }
            return Decision::Hold;
        }
        m_changeRun = 0;
        if (++m_stillRun >= kFrozenMinSamples)
            m_sawFrozen = true;
        return Decision::Hold;
    }

    void Reset()
    {
        m_changeRun = 0;
        m_stillRun = 0;
        m_sawFrozen = false;
        m_open = false;
        m_lastOpenDecision = Decision::Hold;
    }

    bool IsOpen() const { return m_open; }
    bool SawFrozen() const { return m_sawFrozen; }
    uint32_t ChangeRun() const { return m_changeRun; }
    uint32_t StillRun() const { return m_stillRun; }

private:
    uint32_t m_changeRun = 0;
    uint32_t m_stillRun = 0;
    bool m_sawFrozen = false;
    bool m_open = false;
    Decision m_lastOpenDecision = Decision::Hold;
};
