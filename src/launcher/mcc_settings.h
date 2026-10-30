// MCC's flat-play settings around a VR launch (launcher.cpp): the VR preset the
// launcher writes into GameUserSettings.ini before MCC starts, the snapshot of
// the player's own values, and the restore after MCC exits. Tested by
// tests/mcc_settings_tests.cpp. Never touches a game process.
#pragma once
#include <cstddef>
#include <string>

namespace mcc_settings
{
    // A key the launcher snapshots. vrValue is the VR preset value. nullptr is
    // a pass-through key: never written by the launcher, but MCC rewrites it
    // itself during a VR session (it runs -WINDOWED -ResX -ResY and the mod
    // sizes its window to the render surface), so it is saved before launch and
    // put back on restore like the preset keys.
    struct Key
    {
        const char* key;
        const char* vrValue;
    };
    inline constexpr Key kSnapshotKeys[] = {
        {"FOVSetting[0]", "120"},
        {"FOVSetting[1]", "120"},
        {"FOVSetting[2]", "120"},
        {"FOVSetting[3]", "120"},
        {"FOVSetting[4]", "120"},
        {"FOVSetting[5]", "120"},
        {"FOVSetting[6]", "120"},
        {"VehicleFOVSetting[0]", "120"},
        {"VehicleFOVSetting[1]", "120"},
        {"VehicleFOVSetting[2]", "120"},
        {"VehicleFOVSetting[3]", "120"},
        {"VehicleFOVSetting[4]", "120"},
        {"VehicleFOVSetting[5]", "120"},
        {"VehicleFOVSetting[6]", "120"},
        {"bUseVSync", "False"},
        // Unlimited: the headset's own refresh paces every title in VR (the mod
        // also lifts Reach's in-game limit; headset feedback, 2026-10-09).
        {"FrameRateLimit", "0.000000"},
        // Resolution and display mode: pass-through (found 2026-10-24, flat MCC
        // opened windowed at the VR render size).
        {"ResolutionSizeX", nullptr},
        {"ResolutionSizeY", nullptr},
        {"LastUserConfirmedResolutionSizeX", nullptr},
        {"LastUserConfirmedResolutionSizeY", nullptr},
        {"FullscreenMode", nullptr},
        {"LastConfirmedFullscreenMode", nullptr},
        {"PreferredFullscreenMode", nullptr},
    };

    // The value span of a "key=" line (from after '=' to the line end, CR/LF
    // excluded). Works on the ini and on a snapshot (the same key=value lines).
    bool FindIniValue(const std::string& text, const char* key, size_t& valueBegin, size_t& valueEnd);
    // key=value lines (CRLF) for every snapshot key the ini holds.
    std::string BuildSnapshot(const std::string& ini);
    // Writes the VR preset values into the ini; returns how many changed.
    int ApplyPreset(std::string& ini);
    // Replays every snapshot line whose key the ini holds (a missing key is
    // skipped); returns how many were put back. A snapshot from an older
    // launcher restores whatever keys it holds.
    int RestoreSnapshot(std::string& ini, const std::string& snapshot);
    // The first snapshot key the ini holds with another value; empty when they
    // all match. Keys the ini lacks are skipped, as the restore skips them.
    std::string FirstMismatch(const std::string& ini, const std::string& snapshot);
    // The pass-through values a snapshot holds, for the log ("-" when absent).
    std::string DisplaySummary(const std::string& snapshot);

    // The file steps; log takes printf arguments (the launcher log).
    using Log = void (*)(const char* fmt, ...);
    // Saves the snapshot (when saveSnapshot and no unrestored snapshot is on
    // disk), then writes the preset. A snapshot still on disk means the last
    // session never restored (launcher killed, PC crashed): it holds the real
    // flat values, so it is kept as is.
    void ApplyVrSettings(const std::wstring& iniPath, const std::wstring& snapshotPath, bool saveSnapshot, Log log);
    // Puts the snapshot back, reads the file back and checks every key, then
    // deletes the snapshot. Returns true when nothing is left to restore; a
    // failed write or check keeps the snapshot for the next launch.
    bool RestoreFlatSettings(const std::wstring& iniPath, const std::wstring& snapshotPath, Log log);
} // namespace mcc_settings
