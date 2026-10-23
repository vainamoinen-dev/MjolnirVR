# Contributing to MjolnirVR

Thanks for looking. This is a hook-based VR mod for six games we cannot change, so most of the work is careful: find the game's own behaviour, trigger it, and prove the result.

## Before you open a pull request

1. **Build and run the tests.** `cmake --preset release`, `cmake --build --preset release --parallel 8`, `ctest --preset release`. All tests pass on a clean tree; a change that needs a test disabled needs an explanation.
2. **One change per pull request**, described in terms a player understands: which game, what you see before and after, and how to switch it off. Every player-facing behaviour has a setting in `halomccvr.cfg` and a control in the F1 menu.
3. **Pure logic goes in `src/common/`** with a unit test in `tests/`. Game hooks go in the game's adapter under `src/dll/titles/<game>/`; shared VR behaviour goes in `src/dll/core/`.
4. **Hooks** are installed through the shared helpers (`src/common/hook_registry.h`, `src/common/gated_hook.h`) so they can be removed cleanly. Never weaken a signature or byte check to make a hook land.
5. **Hands are by role, not side.** `vr.cpp` already swaps the primary and support hands for left-handed players; do not apply the left-handed setting a second time.
6. **Say what changed for the player** in the commit message, including the new default if a setting changed.

## Reporting a problem

Open an issue with the game and mission, store (Steam or Microsoft Store), headset and OpenXR runtime, mod version (the first line of `HaloMCCVR.log`), and what you did. A log excerpt helps more than a video.

## What we will not merge

- Anything that touches multiplayer, matchmaking or anti-cheat.
- Game files or assets.
- Features that change what the player sees without a setting to turn them off.
