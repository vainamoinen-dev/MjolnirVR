#pragma once
// Frame-cap removal for Reach, Halo 4, Halo 3, ODST and Halo 2: decisions, the shared per-tick step, and the
// saved pair that is written back when the key is switched off. Used by src/dll/core/game.cpp (which supplies
// the guarded memory access and logging through Ops) and menu.cpp (status); tested in tests/reach_frame_cap_tests.cpp.
// No Windows calls and no globals here.
//
// Halo Reach frame cap (haloreach.dll). MCC hands Reach a frame-rate choice
// that Reach's option setter (+0xD5D5) turns into a cap in frames per second
// (60/120/180/240, or 0 = unlimited) and a target frame time (1/cap, or 0).
// While a level runs, Reach's limiter (gate +0x328CC, target load +0x322F5)
// sleeps and spins the render thread to that target. In VR the headset's own
// xrWaitFrame paces every frame, so a 60 cap pins a 90 Hz headset at 60 fps:
// a frame is repeated on every third refresh, which shows as judder only
// while the head moves.
//
// The fix writes MCC's own "unlimited" values (cap 0, target 0, exactly what
// the setter writes for that choice). If MCC rewrites a limit, it is uncapped
// again. Switching the key off brings the game's own limit back at once, in
// every title (headset feedback; an earlier design never switched back), so the
// pair it replaced is saved and written back.
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace reach_frame_cap
{
    enum class Action
    {
        None,
        Uncap,
        Refuse
    };

    // Caps the setter can produce. Anything else is not the proven global.
    inline bool PlausibleCap(int32_t cap) noexcept
    {
        return cap == 0 || cap == 60 || cap == 120 || cap == 180 || cap == 240;
    }

    inline bool PlausibleTarget(int32_t cap, float target) noexcept
    {
        if (cap == 0)
            return target == 0.0f;
        const float expected = 1.0f / float(cap);
        return target > expected * 0.999f && target < expected * 1.001f;
    }

    // want = reach_uncap_framerate (Reach) / titles_uncap_framerate (Halo 4 and the others).
    inline Action Decide(int32_t cap, float target, bool want) noexcept
    {
        if (!PlausibleCap(cap) || !PlausibleTarget(cap, target))
            return Action::Refuse;
        return want && cap != 0 ? Action::Uncap : Action::None;
    }

    // Halo 2: the option stores only a cap (its limiter derives the frame
    // time, 1/cap, or 1 ms when the cap is 0 = MCC's "unlimited").
    inline Action DecideCapOnly(int32_t cap, bool want) noexcept
    {
        if (!PlausibleCap(cap))
            return Action::Refuse;
        return want && cap != 0 ? Action::Uncap : Action::None;
    }

    // Halo 2's second limiter (+0x6BFA53) keeps its own copy of MCC's choice:
    // a frame time (1/60, 1/120, 1/180 or 1/240 s; 0 = unlimited) and a "limited" flag
    // (choice != unlimited). Unlimited = both 0, exactly what its setter (+0x3CD05) writes.
    enum class FramePair
    {
        Unlimited,
        Limited,
        Unknown
    };
    inline FramePair ClassifyFrameTimeFlag(float frameTime, uint8_t flag) noexcept
    {
        if (frameTime == 0.0f && flag == 0)
            return FramePair::Unlimited;
        if (flag != 1)
            return FramePair::Unknown;
        for (const int fps : {60, 120, 180, 240})
        {
            const float expected = 1.0f / float(fps);
            if (frameTime > expected * 0.999f && frameTime < expected * 1.001f)
                return FramePair::Limited;
        }
        return FramePair::Unknown;
    }

    // Halo 2's option table maps the option index to a cap with four
    // "mov eax, imm32" (240/180/120/60). Uncapping clears each immediate's low byte
    // (the other three are zero), so every index maps to 0, MCC's own "unlimited";
    // switching it off writes the stock bytes back.
    inline constexpr uint8_t kHalo2CapLowBytes[4]{0xF0, 0xB4, 0x78, 0x3C};
    enum class Halo2Table
    {
        Stock,
        Patched,
        Unknown
    };
    inline Halo2Table Halo2CapImmediates(const uint8_t lowBytes[4]) noexcept
    {
        bool stock = true, patched = true;
        for (int i = 0; i < 4; ++i)
        {
            stock = stock && lowBytes[i] == kHalo2CapLowBytes[i];
            patched = patched && lowBytes[i] == 0;
        }
        return stock ? Halo2Table::Stock : patched ? Halo2Table::Patched : Halo2Table::Unknown;
    }

    // Key off with a saved pair. Our own zeros (MCC's "unlimited", which
    // only we wrote since the save) take the saved pair back; a plausible MCC
    // limit means MCC rewrote its setting, so the save is dropped; anything else
    // is not the proven pair. One plausibility rule, Decide's.
    enum class RestoreAction
    {
        Write,
        Drop,
        Refuse
    };
    inline RestoreAction DecideRestore(int32_t cap, float target) noexcept
    {
        if (!PlausibleCap(cap) || !PlausibleTarget(cap, target))
            return RestoreAction::Refuse;
        return cap == 0 ? RestoreAction::Write : RestoreAction::Drop;
    }

    // What the F1 menu says per title. One atomic byte per title, published
    // with release ordering by the tick, so the F1 render thread never sees half
    // of a state.
    enum class Status : uint8_t
    {
        NotChecked,
        Uncapped,
        McLimit,
        NotMatched,
        Unrecognised,
        Partial
    };
    // Halo 2 has two limiters: uncapped only when both are proven.
    inline Status Halo2Status(bool first, bool second, bool want) noexcept
    {
        if (!first && !second)
            return Status::NotMatched;
        if (!want)
            return Status::McLimit;
        return first && second ? Status::Uncapped : Status::Partial;
    }

    // The shared cap/target binding and its per-tick step, used by
    // Reach, Halo 4, Halo 3 and ODST (game.cpp supplies Ops: Log, and the guarded
    // Read/Write of the two globals), so a fixture can drive it with fake globals.
    struct Binding
    {
        const char* title = "Reach";
        const char* key = "reach_uncap_framerate"; // the cfg key, named in log lines
        std::atomic<uint8_t>* status = nullptr;    // F1's view (Status)
        uint32_t generation = 0;
        bool resolved = false, proven = false;
        volatile int32_t* cap = nullptr;
        volatile float* target = nullptr;
        bool refusedLogged = false;
        uint64_t uncaps = 0;
        // The pair this binding replaced. It outlives a generation on the same
        // module (base and image size), where our zeros stay in place, and is
        // restored only into a binding proven in the current generation.
        bool saved = false;
        uintptr_t savedBase = 0;
        size_t savedSize = 0;
        int32_t savedCap = 0;
        float savedTarget = 0.0f;
    };
    using Resolver = bool (*)(uintptr_t, size_t, volatile int32_t*&, volatile float*&);

    inline void Publish(const Binding& b, Status s) noexcept
    {
        if (b.status)
            b.status->store(static_cast<uint8_t>(s), std::memory_order_release);
    }

    // Ops::Log(format, ...); Ops::Read(binding, cap, target) and Ops::Write(binding,
    // cap, target) return false on a fault (the binding is then unproven).
    template <class Ops>
    void Tick(
        Binding& b, uintptr_t base, size_t size, uint32_t generation, bool coldWorkAllowed, Resolver resolve, bool want)
    {
        if (b.generation != generation)
        {
            Binding next;
            next.title = b.title;
            next.key = b.key;
            next.status = b.status;
            next.saved = b.saved;
            next.savedBase = b.savedBase;
            next.savedSize = b.savedSize;
            next.savedCap = b.savedCap;
            next.savedTarget = b.savedTarget;
            next.generation = generation;
            b = next;
            Publish(b, Status::NotChecked); // until this generation resolves
        }
        if (!coldWorkAllowed || !generation)
            return;
        if (!b.resolved)
        {
            b.resolved = true;
            b.proven = resolve(base, size, b.cap, b.target);
            Ops::Log("%s frame cap: %s", b.title,
                b.proven ? "option setter, limiter gate and target load proven; it runs "
                           "uncapped and the headset paces it"
                         : "binding NOT proven; it keeps MCC's frame-rate limit (stock)");
            // A save is restored only into this module, proven
            // in this generation; otherwise it is dropped and nothing is written.
            if (!b.proven || b.savedBase != base || b.savedSize != size)
                b.saved = false;
        }
        if (!b.proven)
        {
            Publish(b, Status::NotMatched);
            return;
        }
        int32_t cap = 0;
        float target = 0.0f;
        if (!Ops::Read(b, cap, target))
        {
            b.proven = false;
            Publish(b, Status::NotMatched);
            Ops::Log("%s frame cap: FAULTED reading the cap; stock", b.title);
            return;
        }
        if (!want && b.saved)
        {
            switch (DecideRestore(cap, target))
            {
            case RestoreAction::Write:
                // The saved pair back, target before cap (Ops::Write's order).
                if (!Ops::Write(b, b.savedCap, b.savedTarget))
                {
                    b.proven = false;
                    Publish(b, Status::NotMatched);
                    Ops::Log("%s frame cap: FAULTED writing; stock", b.title);
                    return;
                }
                b.saved = false;
                Publish(b, Status::McLimit);
                Ops::Log("%s frame cap: %s off; restored MCC's limit of %d fps", b.title, b.key, b.savedCap);
                return;
            case RestoreAction::Drop:
                b.saved = false; // MCC rewrote its own setting; its value stands
                break;
            case RestoreAction::Refuse:
                break; // Decide refuses the same pair below
            }
        }
        switch (Decide(cap, target, want))
        {
        case Action::Refuse:
            Publish(b, Status::Unrecognised);
            if (!b.refusedLogged)
            {
                b.refusedLogged = true;
                Ops::Log("%s frame cap: unexpected values (cap %d, target %.6f); left stock", b.title, cap, target);
            }
            return;
        case Action::Uncap:
            // MCC's own "unlimited" values; the pair they replace is saved.
            if (!Ops::Write(b, 0, 0.0f))
            {
                b.proven = false;
                Publish(b, Status::NotMatched);
                Ops::Log("%s frame cap: FAULTED writing; stock", b.title);
                return;
            }
            b.saved = true;
            b.savedBase = base;
            b.savedSize = size;
            b.savedCap = cap;
            b.savedTarget = target;
            Publish(b, Status::Uncapped);
            if (b.uncaps++ < 8)
                Ops::Log("%s frame cap: MCC limited %s to %d fps; set to unlimited "
                         "(the headset paces each frame)",
                    b.title, b.title, cap);
            return;
        case Action::None:
            Publish(b, want ? Status::Uncapped : Status::McLimit);
            return;
        }
    }
} // namespace reach_frame_cap
