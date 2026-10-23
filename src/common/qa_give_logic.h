#pragma once
// Test-rig "give all weapons" one-shot for Halo CE, used only by automated testing.
// Used by src/dll/titles/ce/haloce_unit_control.cpp; tested by tests/qa_give_tests.cpp.
// Pure decision logic: no globals, no game calls (the caller makes the one native call).
//
// Pure decision logic: the request comes from the environment variable
// MJOLNIRVR_QA_GIVE_WEAPON at process start (never a config key, never the F1 menu), and the native call happens once,
// after the local on-foot player has been proven for kStableCalls consecutive game-thread updates, in local simulation only.
#include <cstdint>
#include <cstring>

namespace qa_give
{
    inline constexpr uint32_t kStableCalls = 120; // about 2 s of unit-control updates

    // All: every loaded weapon in an arc around the player. Sniper: only the sniper rifle,
    // dropped straight ahead of the player (same native spawn, a one-entry list).
    enum class Request : uint8_t
    {
        None,
        All,
        Sniper
    };

    inline bool MatchesWord(const char* value, const char* word) noexcept
    {
        for (; *word; ++word, ++value)
        {
            char c = *value;
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + ('a' - 'A'));
            if (c != *word)
                return false;
        }
        return *value == 0;
    }

    // Exactly "all" or "sniper" (ASCII case-insensitive); anything else, including unset, requests nothing.
    inline Request ParseRequest(const char* value) noexcept
    {
        if (!value)
            return Request::None;
        if (MatchesWord(value, "all"))
            return Request::All;
        if (MatchesWord(value, "sniper"))
            return Request::Sniper;
        return Request::None;
    }

    enum class Action : uint8_t
    {
        Inert,
        Wait,
        Call,
        Skip
    };

    struct Inputs
    {
        bool bindingVerified = false; // PE identity + byte signature of the native routine matched
        bool localSimulation = false; // not a network client/server simulation
        bool playerProven = false;    // local controlled on-foot unit, not paused/cinematic/blocked
    };

    class OneShot
    {
    public:
        explicit OneShot(Request request = Request::None) noexcept : request_(request) {}
        bool Done() const noexcept { return done_; }
        Request Requested() const noexcept { return request_; }
        const char* Reason() const noexcept { return reason_; }
        // Skip and Call each happen once; the caller logs Reason() on Skip and "called" on Call.
        Action Step(const Inputs& in) noexcept
        {
            if (request_ == Request::None || done_)
                return Action::Inert;
            if (!in.bindingVerified)
                return Finish("native routine not verified for this halo1.dll build");
            if (!in.localSimulation)
                return Finish("network simulation (single player and local games only)");
            if (!in.playerProven)
            {
                stable_ = 0;
                return Action::Wait;
            }
            if (++stable_ < kStableCalls)
                return Action::Wait;
            done_ = true;
            reason_ = "";
            return Action::Call;
        }

    private:
        Action Finish(const char* reason) noexcept
        {
            done_ = true;
            reason_ = reason;
            return Action::Skip;
        }
        Request request_;
        bool done_ = false;
        uint32_t stable_ = 0;
        const char* reason_ = "";
    };
} // namespace qa_give
