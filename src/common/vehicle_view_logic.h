#pragma once
// Runtime seat-view switch (first-person seat view <-> the game's own third-person vehicle camera).
// Pure logic: the caller supplies the seated vehicle-class key (0 = not seated) and the button edge,
// and applies the result to the live vehicle_first_person flag. The configured default is never changed.
// Used by src/dll/core/game.cpp; tested by tests/vehicle_view_tests.cpp.
#include <cstdint>

namespace vehicle_view
{
    enum class Mode : uint8_t
    {
        Unset = 0,
        First,
        Third
    };

    // Remembers the chosen view per vehicle class (key = title and vehicle). Small ring: the oldest entry is replaced.
    class Memory
    {
    public:
        Mode Get(uint32_t key) const noexcept
        {
            for (unsigned i = 0; i < used_; ++i)
                if (key_[i] == key)
                    return mode_[i];
            return Mode::Unset;
        }
        void Set(uint32_t key, Mode mode) noexcept
        {
            for (unsigned i = 0; i < used_; ++i)
                if (key_[i] == key)
                {
                    mode_[i] = mode;
                    return;
                }
            const unsigned slot = used_ < kCapacity ? used_++ : (next_++ % kCapacity);
            key_[slot] = key;
            mode_[slot] = mode;
        }
        void Clear() noexcept
        {
            used_ = 0;
            next_ = 0;
        }

    private:
        static constexpr unsigned kCapacity = 32;
        uint32_t key_[kCapacity]{};
        Mode mode_[kCapacity]{};
        unsigned used_{}, next_{};
    };

    struct State
    {
        uint32_t key{};
        bool pressed{};
    };
    struct Event
    {
        bool switched{};
        bool from{}, to{};
        bool entered{};
    };

    // One input-thread step. `key` is 0 when not seated. `press` is the raw button level.
    // `liveFirst` is the live flag (in/out); `configFirst` the configured default it falls back to.
    inline Event Step(State& s, Memory& memory, uint32_t key, bool press, bool configFirst, bool& liveFirst) noexcept
    {
        Event e{};
        const bool rising = press && !s.pressed;
        s.pressed = press;
        if (!key)
        {
            if (s.key)
                liveFirst = configFirst; // left the vehicle: the configured default again
            s.key = 0;
            return e;
        }
        if (key != s.key)
        {
            s.key = key;
            e.entered = true;
            const Mode remembered = memory.Get(key);
            liveFirst = remembered == Mode::Unset ? configFirst : remembered == Mode::First;
        }
        if (rising)
        {
            e.switched = true;
            e.from = liveFirst;
            e.to = !liveFirst;
            liveFirst = e.to;
            memory.Set(key, e.to ? Mode::First : Mode::Third);
        }
        return e;
    }

    // X belongs to the view switch while seated. After the seat ends it stays withheld until the button
    // is released once, so a hold that outlived the seat never reaches the game as a fresh press on foot.
    class XGuard
    {
    public:
        bool Update(bool seated, bool xDown) noexcept
        {
            if (seated)
            {
                latched_ = true;
                return true;
            }
            if (latched_ && xDown)
                return true;
            latched_ = false;
            return false;
        }

    private:
        bool latched_{};
    };

} // namespace vehicle_view
