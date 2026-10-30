# What MjolnirVR touches on your PC

Everything below is read from the source in this repository. If you find
something the mod does that is not listed here, open an issue; that is a bug.

## Nothing goes online

The mod, the launcher and the packages contain no network code: no update
check, no download, no crash reporting, no telemetry upload. The launcher's
former update checker was removed on 8 October 2026. The optional telemetry
recorder writes a local file next to the mod and nothing reads it but you.

## Files and folders

| What | Where | When |
|---|---|---|
| The mod itself | `Halo_MCC_VR\` inside your MCC folder: `HaloMCCVR.dll`, `HaloMCCVRLauncher.exe`, `halomccvr.cfg`, `nvngx_dlss.dll` (NVIDIA DLSS runtime), fonts, licences, `MANUAL-README.txt` | Install, by the launcher's Install button or by copying the folder yourself |
| Backup of a previous mod install | `Halo_MCC_VR\backup\<date-time>\` | Only when the launcher updates an existing install |
| Settings | `Halo_MCC_VR\halomccvr.cfg`, rewritten when you save in the F1 menu. An old `halo3xr.cfg` is imported once. | While playing |
| Logs | `Halo_MCC_VR\HaloMCCVR.log` (and `.log.prev`), `HaloMCCVR-native-faults.log`, `HaloMCCVRLauncher.log` | While playing |
| Telemetry (local) | `Halo_MCC_VR\HaloMCCVR-Telemetry.jsonl` | Only after you press Start on the F1 menu's Telemetry Recorder page |
| MCC's own settings | `%USERPROFILE%\AppData\LocalLow\MCC\Saved\Config\WindowsNoEditor\GameUserSettings.ini`: the launcher sets FOV 120 for every title, V-Sync off and no frame cap for the VR session. It first saves your old values, and your resolution and display mode (MCC changes those itself in VR), to `Halo_MCC_VR\mcc_flat_settings.txt`, puts them back when the game closes and checks they came back. Set `mcc_vr_settings = 0` in `halomccvr.cfg` to leave the file alone. | Every launch and exit |

No game file is replaced. No file is written anywhere else: not Program
Files outside the MCC folder, not Windows, not your Documents.

## Registry, services, start-up

- The launcher reads Steam's library list to find MCC, and asks Windows for the Xbox app's package location for the Store edition (a read-only API call, then a bounded look in `X:\XboxGames`). It writes nothing to the registry.
- No service, driver, scheduled task, start-up entry, shortcut or uninstall entry is created. Uninstall is: delete the `Halo_MCC_VR` folder.

## Permissions

- The launcher runs as the user who started it. It never asks for administrator rights and never starts the game elevated.
- If your MCC folder is under `Program Files`, Windows will not let a normal user write there. For the install step only, run the launcher as administrator; playing never needs it.

## How the game is started

The launcher starts MCC's own executable (`MCC-Win64-Shipping.exe` on Steam,
`MCCWinStore-Win64-Shipping.exe` from the Xbox app) exactly as Steam's "Play
without anti-cheat" entry does, so Easy Anti-Cheat never runs. It starts the
game windowed at the VR render size (`-WINDOWED -ResX -ResY`) and, on Steam,
sets the `SteamAppId` environment variable so Steam does not relaunch the
process. It then loads `HaloMCCVR.dll` into that process (`CreateRemoteThread`
+ `LoadLibraryW` while the process is still suspended). That loading step is what some antivirus
heuristics flag as an "injector"; it is how every MCC mod that adds a DLL works.
The mod only ever runs inside the anti-cheat-disabled game, which cannot join
matchmaking.

## What is in the packages

- `MjolnirVR-<version>-Build.zip`: the launcher plus a `ModFiles` folder with the mod and `INSTALL-MANIFEST.sha256`. The launcher checks every file against that manifest before installing and refuses on any mismatch.
- `MjolnirVR-<version>-Manual.zip`: the finished `Halo_MCC_VR` folder, for copying by hand.
- `MjolnirVR-<version>-Source.zip`: the exact source commit.
- `SHA256SUMS.txt`: hashes of all of the above.
- `assets\scope-masks` inside the packages: scope window shapes derived from Halo's own HUD art, included for non-commercial use under Microsoft's Game Content Usage Rules. They are not in this source repository and are not covered by its MIT licence.

Every release file is built by this repository's GitHub Actions workflow
(`.github/workflows/release.yml`) from the tagged commit and carries a build
provenance attestation. Verify one with:

```
gh attestation verify MjolnirVR-<version>-Build.zip -R vainamoinen-dev/MjolnirVR
```

Or build it yourself from the same commit: see `BUILDING.md`.
