// The laser pointer for the game's own menus: when it owns input, the packed pointer state,
// and how the pad drives MCC's pause and Save & Quit dialogs.
// Used by dll/core/native_menu_pointer.cpp, dll/core/vr.cpp and tests/game_menu_pointer_tests.cpp.
// Pure logic: no globals, no logging.
#pragma once
#include "runtime_types.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace game_menu_pointer
{
    // While the laser owns an in-game pause menu, the pad's Menu button
    // resumes, as Start does on MCC's pause menu without VR. The laser path passes
    // no gamepad buttons, so the press is sent as one held Start. MCC's own shell
    // menus (no in-game pause presentation) are left alone: Start does other
    // things there. The ODST camera context plays no part: it is gone after
    // ODST's pause-boundary teardown, while the in-game pause is still showing.
    inline bool MenuResumesPause(bool pointerOwnsInput, bool inGamePause, bool menuEdge) noexcept
    {
        return pointerOwnsInput && inGamePause && menuEdge;
    }

    inline bool MenuMode(RuntimeMode mode, bool pauseScreen) noexcept
    {
        // MCC's shell preloads all six modules. The camera-owner resolver calls
        // that Unsupported; it is not evidence that the displayed menu is absent.
        // Callers additionally require an actual stock-screen submission.
        return mode == RuntimeMode::Shell || mode == RuntimeMode::Unsupported || mode == RuntimeMode::Paused ||
               pauseScreen;
    }

    // One atomic word carries coordinates, button and freshness together. A missed
    // ray is inactive; it cannot press at the last valid cursor location.
    inline uint64_t Pack(uint32_t now, bool active, float u, float v, bool pressed) noexcept
    {
        active = active && std::isfinite(u) && std::isfinite(v) && u >= 0 && u <= 1 && v >= 0 && v <= 1;
        const uint32_t x = active ? uint32_t(u * 32767.0f) : 0;
        const uint32_t y = active ? uint32_t(v * 32767.0f) : 0;
        return (uint64_t(now) << 32) | x | (y << 15) | (uint32_t(pressed) << 30) | (uint32_t(active) << 31);
    }
    inline bool FreshActive(uint64_t packet, uint32_t now) noexcept
    {
        return (packet & (1ull << 31)) && uint32_t(now - uint32_t(packet >> 32)) <= 200;
    }

    // MCC's "Save & Quit" confirmation clears the title's native pause, so our
    // pause presentation and laser end while the dialog is still up, and no
    // controller input reaches it (found in headset testing: only keyboard Right+Enter
    // worked, in every title). When a pause ends WITHOUT the player asking to
    // resume (no Menu/Start or B in the last kResumeGraceMs), the controller also
    // acts as keys: A = Enter, stick left/right = Left/Right (nothing else; B keeps
    // its own mapping). A laser click just before the end ("Resume" picked with the
    // laser) is a resume too. The window closes on the first gameplay input
    // (trigger pull, walking) or after kDialogWindowMs.
    inline constexpr uint64_t kResumeGraceMs = 1500, kLaserResumeGraceMs = 700, kDialogWindowMs = 20000;
    inline bool PauseEndedByGame(
        uint64_t pauseEndMs, uint64_t lastResumeInputMs, uint64_t lastLaserClickMs = 0) noexcept
    {
        auto within = [&](uint64_t at, uint64_t grace) { return at && at <= pauseEndMs && pauseEndMs - at <= grace; };
        return pauseEndMs && !within(lastResumeInputMs, kResumeGraceMs) &&
               !within(lastLaserClickMs, kLaserResumeGraceMs);
    }
    // Gameplay input that proves no dialog is up: a trigger pull or walking.
    inline bool GameplayInput(float trigger, float moveY) noexcept
    {
        return trigger >= 0.65f || std::fabs(moveY) >= 0.6f;
    }
    inline bool DialogKeysWindow(uint64_t now, uint64_t gameEndedPauseMs) noexcept
    {
        return gameEndedPauseMs && now >= gameEndedPauseMs && now - gameEndedPauseMs <= kDialogWindowMs;
    }
    // Stick/D-pad direction for arrow keys: -1/+1 past 0.6, 0 inside (dead zone).
    inline int StickDir(float v) noexcept
    {
        return v <= -0.6f ? -1 : v >= 0.6f ? 1 : 0;
    }

    struct Result
    {
        bool move = false, pressed = false;
        int x = 0, y = 0;
    };
    class Pointer
    {
        bool armed = false, hadHit = false;
        float u = 0, v = 0;

    public:
        void Reset() noexcept { armed = hadHit = false; }
        Result Update(uint64_t packet, uint32_t now, bool allowed, int width, int height) noexcept
        {
            if (!allowed || !FreshActive(packet, now) || width <= 0 || height <= 0)
            {
                Reset();
                return {};
            }
            const float nextU = float(packet & 32767) / 32767.0f;
            const float nextV = float((packet >> 15) & 32767) / 32767.0f;
            u = hadHit ? u + (nextU - u) * 0.35f : nextU;
            v = hadHit ? v + (nextV - v) * 0.35f : nextV;
            hadHit = true;
            const bool trigger = (packet & (1ull << 30)) != 0;
            if (!trigger)
                armed = true;
            return {true, armed && trigger, std::clamp(int(u * width), 0, width - 1),
                std::clamp(int(v * height), 0, height - 1)};
        }
    };
}
