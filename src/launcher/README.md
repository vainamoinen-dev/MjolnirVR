# Launcher and installer

The existing Steam suspended-process launch and Microsoft Store packaged
activation code remains in `launcher.cpp`. A new menu selects an installation
and calls that same launch path only when the user selects **Launch MCC**.
Installation and update discovery do not start MCC.

Package layout:

```
HaloMCCVRLauncher.exe
ModFiles/
  HaloMCCVR.dll
  halomccvr.cfg
  INSTALL-MANIFEST.sha256
  BUILD-IDENTITY.txt
  assets/fonts/Oxanium.ttf
  assets/fonts/OFL-Oxanium.txt
  assets/fonts/SOURCE.txt
  ...licenses, notes and other distributed assets...
```

The installer/updater executable is a separate player download beside
`ModFiles`. The Windows manual archive includes the legacy game-launch/injection
helper within `Halo_MCC_VR`, but does not include the installer/updater UI. The
Linux compatible manual archive has the same payload files directly at ZIP root
to avoid an extra extraction folder. `ModFiles` remains the payload for manual
installation. Its
manifest is UTF-8/ASCII, one
`SHA256  relative/path` per file, excluding the manifest itself. Paths are
relative to `ModFiles`; backslashes and forward slashes are supported. Do not
list directories, duplicate names, absolute paths or the manifest itself.
`BUILD-IDENTITY.txt` contains `source_commit=<40 hex>`,
`build_kind=UNTESTED_LOCAL_CANDIDATE` (or `PUBLIC_RELEASE`) and
`release_tag=<exact published GitHub tag, otherwise empty>`, one per line.
The menu displays the installed identity, describes discovery as the latest
public release, and labels replacement of a candidate as Install public release.

Detection reads Steam registry/library metadata and the current Windows
account's `Microsoft.Chelan_8wekyb3d8bbwe` package registration. A bounded
fixed-drive XboxGames/XBOX fallback and Browse cover additional libraries.
Each result must contain one of MCC's actual shipping executable layouts.
The installer always targets that root's dedicated `Halo_MCC_VR` folder.
It refuses active MCC processes, path junctions/symbolic links and corrupt
payload hashes. The launcher runs as the current user (no UAC prompt), so a
protected library folder needs the launcher started with "Run as administrator"
for that one install. The launcher executable
is installed as a separate file beside the mod payload inside `Halo_MCC_VR`;
it is not part of the source `ModFiles` tree; the separately packaged manual
archives provide the legacy launch helper for manual users.

Existing files that are replaced are copied and verified under
`Halo_MCC_VR/backups/<unique timestamp>`. A replacement failure restores earlier
replacements from that backup. Unknown files and logs remain untouched. An
interrupted process/power loss is not an atomic directory transaction: keep the
backup and rerun the installer before launching if installation was interrupted.

Keep-settings defaults on. Existing settings, explicit Unbound values, comments
and unknown/custom keys are retained; absent new defaults are appended once.
Legacy `halo3xr.cfg` is imported if the primary file is absent. Migration marker
keys and calibration keys that still require runtime migration are not blindly
appended. Unchecking keep-settings installs this build's defaults, with the
previous primary config included in the same backup.

There is no online updater. This build split off from upstream
(moistman42069/MCCVR-Halo-Build) on 2026-10-08, so the upstream "Check for
updates", "Install update" and "Release page" actions were removed: they would
have installed upstream's release over this one. Install new builds from a
local package with **Install VR mod** / **Install / update from this package**.

The menu privately loads unchanged OFL-licensed Oxanium. An installed Halo font
can be used for the heading; no proprietary font or game artwork is bundled.
Bahnschrift is the system fallback. Both editions use the same menu and payload.

Validation: `halomccvr_installer_tests` exercises pure policy and synthetic
Steam/Store layouts, first install, cfg retention/reset, hashing, backups,
and locked-destination rollback. It never launches or writes to a real MCC
installation. Native Steam/Store launch behavior still requires user
runtime testing; those paths were preserved, not runtime-accepted by these tests.
