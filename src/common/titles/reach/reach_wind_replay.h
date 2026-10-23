// Replays Reach's decorator wind state so both eyes render the same wind while one native advance is kept.
// Used by src/dll/core/game.cpp; tested in tests/core_tests.cpp and reach_scope_tests.cpp.
// Memory copies go through the caller's Copy function: no Windows calls, no globals, no logging.
#pragma once
#include <cstdint>
#include <cstddef>

// HREK decorator wind is four accumulated integers plus two shader vectors.
// Replay the first eye's input for the second eye, and retain one native advance
// even when that second render aborts. Camera/LOD/tag selection remain native.
struct ReachWindReplay
{
    static constexpr size_t kBytes = 48;
    uint8_t before[kBytes]{};
    uint8_t after[kBytes]{};
    bool captured = false;
    bool restore = false;

    template <class Copy>
    bool Begin(void* state, Copy copy)
    {
        captured = state && copy(before, state, kBytes);
        return captured;
    }
    template <class Copy>
    bool Replay(void* state, Copy copy)
    {
        if (!captured || !state || !copy(after, state, kBytes))
            return false;
        // Set before writing: a failed partial write still needs restoration.
        restore = true;
        return copy(state, before, kBytes);
    }
    template <class Copy>
    bool Finish(void* state, Copy copy)
    {
        const bool ok = !restore || (state && copy(state, after, kBytes));
        restore = false;
        return ok;
    }
};
