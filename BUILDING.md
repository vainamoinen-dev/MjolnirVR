# Building MjolnirVR

This builds one cumulative runtime for CE, H2, H3, ODST, Reach and H4, supporting
Steam and Microsoft Store editions. There is one Release preset. An unproven
camera adapter does not acquire VR ownership. Optional feature failures remain
isolated to that feature; failed eye frames are dropped without ending VR.
Generated files stay under ignored `out/`; ordinary building and packaging do
not write to an MCC installation.

## Requirements

- Windows x64.
- Visual Studio 2022 with **Desktop development with C++**.
- CMake 3.24 or newer.
- Python 3.10 or newer for production-function test generation.
- Git and network access for the first dependency download.

OpenXR, MinHook, and Dear ImGui are pinned to exact commits in
`CMakeLists.txt`. Fetches are shallow and shared under `out/deps`.

## Build and test

From a Developer PowerShell:

```powershell
cmake --preset release
cmake --build --preset release
ctest --preset release
```

There is a single `release` preset. It builds Release x64 with all six titles
compiled in. `camscan` is excluded: it is an
opt-in diagnostic with process-memory write modes, not a product target. The
standalone Reach runtime observer is also excluded and must be selected by name;
it is never linked into `HaloMCCVR.dll`. The build identity line reports
`ODST=ON, Reach=ON, ReachRender=ON`.

## Packaging

After committing, run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools/package.ps1
```

It builds the `release` preset, runs the tests, and writes to `out\packages\<version>\`:
`MjolnirVR-<version>-Build.zip` (launcher plus `ModFiles` with its install
manifest), `MjolnirVR-<version>-Manual.zip` (the finished `Halo_MCC_VR` folder),
`MjolnirVR-<version>-Source.zip` and `SHA256SUMS.txt`. It never installs
anything and never starts MCC. `-NoBuild` packages an existing build;
`-AssetsLocal <folder>` merges extra art (the per-gun scope masks, which are
not in this repository) into the payload. Releases are produced by the same
script in `.github/workflows/release.yml` on a GitHub runner.
