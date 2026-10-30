// Tests the launcher's MCC flat-settings snapshot and restore against a fixture
// GameUserSettings.ini. Compiles src/launcher/mcc_settings.cpp; built as
// halomccvr_mcc_settings_tests. Writes only inside its own
// HaloMCCVR-mcc-settings-test-* folder in the temp directory, never MCC's file.
#include "../src/launcher/mcc_settings.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <utility>

using namespace mcc_settings;
namespace fs = std::filesystem;
static void Require(bool condition, const char* reason)
{
    if (!condition)
        throw std::runtime_error(reason);
}
static void Write(const fs::path& path, const std::string& value)
{
    std::ofstream file(path, std::ios::binary);
    file << value;
    file.close();
    Require(static_cast<bool>(file), "fixture write");
}
static std::string Read(const fs::path& path)
{
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
static bool Has(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}
static std::string g_log;
static void CaptureLog(const char* fmt, ...)
{
    char line[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    g_log += line;
    g_log += "\n";
}

// A flat player's file: 1920x1080 borderless, FOV 78, V-Sync on, 60 fps. No
// VehicleFOVSetting[6] and no PreferredFullscreenMode (a missing key).
static const char kFlatIni[] = "[/Script/MCC.MCCGameUserSettings]\r\n"
                               "FOVSetting[0]=78\r\nFOVSetting[1]=78\r\nFOVSetting[2]=78\r\nFOVSetting[3]=78\r\n"
                               "FOVSetting[4]=78\r\nFOVSetting[5]=78\r\nFOVSetting[6]=78\r\n"
                               "VehicleFOVSetting[0]=90\r\nVehicleFOVSetting[1]=90\r\nVehicleFOVSetting[2]=90\r\n"
                               "VehicleFOVSetting[3]=90\r\nVehicleFOVSetting[4]=90\r\nVehicleFOVSetting[5]=90\r\n"
                               "bUseVSync=True\r\n"
                               "ResolutionSizeX=1920\r\nResolutionSizeY=1080\r\n"
                               "LastUserConfirmedResolutionSizeX=1920\r\nLastUserConfirmedResolutionSizeY=1080\r\n"
                               "WindowPosX=-1\r\nWindowPosY=-1\r\n"
                               "FullscreenMode=1\r\nLastConfirmedFullscreenMode=1\r\n"
                               "FrameRateLimit=60.000000\r\n"
                               "\r\n[ScalabilityGroups]\r\nsg.ResolutionQuality=100.000000\r\n";

// What MCC leaves after a VR session: the preset plus its own window rewrite.
static std::string VrExit(std::string ini)
{
    static const std::pair<const char*, const char*> kRewrites[] = {{"ResolutionSizeX", "3786"},
        {"ResolutionSizeY", "2730"}, {"LastUserConfirmedResolutionSizeX", "3786"},
        {"LastUserConfirmedResolutionSizeY", "2730"}, {"FullscreenMode", "2"}, {"LastConfirmedFullscreenMode", "2"},
        {"FOVSetting[3]", "100"}};
    for (const auto& [key, value] : kRewrites)
    {
        size_t b, e;
        Require(FindIniValue(ini, key, b, e), "VR exit key present");
        ini.replace(b, e - b, value);
    }
    return ini;
}

static void Strings()
{
    const std::string flat = kFlatIni;
    const std::string snapshot = BuildSnapshot(flat);
    for (const char* line :
        {"ResolutionSizeX=1920\r\n", "ResolutionSizeY=1080\r\n", "LastUserConfirmedResolutionSizeX=1920\r\n",
            "LastUserConfirmedResolutionSizeY=1080\r\n", "FullscreenMode=1\r\n", "LastConfirmedFullscreenMode=1\r\n",
            "FOVSetting[6]=78\r\n", "bUseVSync=True\r\n", "FrameRateLimit=60.000000\r\n"})
        Require(Has(snapshot, line), "the snapshot holds every preset and display key the file has");
    Require(!Has(snapshot, "PreferredFullscreenMode") && !Has(snapshot, "VehicleFOVSetting[6]"),
        "a key the file lacks is not in the snapshot");
    Require(!Has(snapshot, "WindowPosX") && !Has(snapshot, "sg.ResolutionQuality"), "other keys are not snapshotted");
    Require(Has(snapshot, "\nFullscreenMode=1") && Has(snapshot, "LastConfirmedFullscreenMode=1"),
        "FullscreenMode is its own line, not the end of another key");

    std::string vr = flat;
    Require(ApplyPreset(vr) == 15, "15 preset values change (VehicleFOVSetting[6] is missing)");
    Require(
        Has(vr, "FOVSetting[0]=120\r\n") && Has(vr, "bUseVSync=False\r\n") && Has(vr, "FrameRateLimit=0.000000\r\n"),
        "the preset is written");
    Require(Has(vr, "ResolutionSizeX=1920\r\n") && Has(vr, "\nFullscreenMode=1\r\n"),
        "pass-through keys are never written by the preset");
    Require(ApplyPreset(vr) == 0, "the preset applied twice changes nothing");

    std::string back = VrExit(vr);
    Require(!FirstMismatch(back, snapshot).empty(), "a VR file does not match the snapshot");
    Require(
        FirstMismatch(back, snapshot).starts_with("FOVSetting[0] is 120, snapshot 78"), "the mismatch names the key");
    Require(RestoreSnapshot(back, snapshot) == 21, "every snapshot key is put back");
    Require(back == flat, "the restore gives back the flat file byte for byte");
    Require(FirstMismatch(back, snapshot).empty(), "a restored file matches the snapshot");

    // A snapshot key the file lacks is skipped (no error, not a mismatch).
    std::string missing = flat;
    Require(
        RestoreSnapshot(missing, "NoSuchKey=1\r\nFOVSetting[0]=90\r\n") == 1 && Has(missing, "FOVSetting[0]=90\r\n"),
        "a missing key is skipped, the rest is restored");
    Require(FirstMismatch(missing, "NoSuchKey=1\r\nFOVSetting[0]=90\r\n").empty(), "a missing key is not a mismatch");

    Require(DisplaySummary(snapshot) ==
                "ResolutionSizeX=1920, ResolutionSizeY=1080, LastUserConfirmedResolutionSizeX=1920, "
                "LastUserConfirmedResolutionSizeY=1080, FullscreenMode=1, "
                "LastConfirmedFullscreenMode=1, PreferredFullscreenMode=-",
        "the display summary names every display key, - when absent");
}

static void Files(const fs::path& root)
{
    const fs::path ini = root / "GameUserSettings.ini", snap = root / "mcc_flat_settings.txt";

    // A VR session: snapshot before launch, MCC rewrites the window, restore after.
    Write(ini, kFlatIni);
    g_log.clear();
    ApplyVrSettings(ini.wstring(), snap.wstring(), true, CaptureLog);
    Require(fs::exists(snap) && Read(snap) == BuildSnapshot(kFlatIni), "the snapshot is written before launch");
    Require(Has(Read(ini), "FOVSetting[2]=120\r\n"), "the preset is in the file");
    Require(Has(g_log, "15 values changed") && Has(g_log, "display values: ResolutionSizeX=1920, ResolutionSizeY=1080"),
        "the apply log names the snapshot's display values");
    Write(ini, VrExit(Read(ini)));
    g_log.clear();
    Require(RestoreFlatSettings(ini.wstring(), snap.wstring(), CaptureLog), "the restore succeeds");
    Require(Read(ini) == kFlatIni, "the flat file is back, resolution and display mode included");
    Require(!fs::exists(snap), "a verified restore deletes the snapshot");
    Require(Has(g_log, "restored 21 flat-play values (display: ResolutionSizeX=1920") && Has(g_log, "restore verified"),
        "the restore log names the values and the check");
    Require(RestoreFlatSettings(ini.wstring(), snap.wstring(), CaptureLog) && Read(ini) == kFlatIni,
        "no snapshot: nothing to restore, the file is untouched");

    // A snapshot already on disk (the last session never restored) is kept.
    const std::string old = "FOVSetting[0]=78\r\nResolutionSizeX=1920\r\nFullscreenMode=1\r\n";
    Write(snap, old);
    Write(ini, VrExit(kFlatIni));
    g_log.clear();
    ApplyVrSettings(ini.wstring(), snap.wstring(), true, CaptureLog);
    Require(Read(snap) == old, "an unrestored snapshot is not overwritten");
    Require(Has(g_log, "earlier unrestored snapshot kept") &&
                Has(g_log, "display values: ResolutionSizeX=1920, ResolutionSizeY=-, ") &&
                Has(g_log, "FullscreenMode=1"),
        "the apply log shows the kept snapshot's display values");
    Require(RestoreFlatSettings(ini.wstring(), snap.wstring(), CaptureLog), "the kept snapshot restores");
    const std::string restored = Read(ini);
    Require(Has(restored, "FOVSetting[0]=78\r\n") && Has(restored, "ResolutionSizeX=1920\r\n") &&
                Has(restored, "\nFullscreenMode=1\r\n"),
        "the kept snapshot's values come back");
    Require(Has(restored, "ResolutionSizeY=2730\r\n"), "a key the snapshot lacks is left as it is");

    // Mode 2 (apply and keep): no snapshot.
    Write(ini, kFlatIni);
    ApplyVrSettings(ini.wstring(), snap.wstring(), false, CaptureLog);
    Require(!fs::exists(snap) && Has(Read(ini), "FOVSetting[0]=120\r\n"), "apply-and-keep writes no snapshot");

    // No settings file (first MCC run): nothing written.
    fs::remove(ini);
    g_log.clear();
    ApplyVrSettings(ini.wstring(), snap.wstring(), true, CaptureLog);
    Require(!fs::exists(ini) && !fs::exists(snap) && Has(g_log, "preset skipped"), "no file: the preset is skipped");
}

int main()
{
    const fs::path root =
        fs::temp_directory_path() / ("HaloMCCVR-mcc-settings-test-" + std::to_string(GetCurrentProcessId()));
    try
    {
        fs::create_directories(root);
        Strings();
        Files(root);
        fs::remove_all(root);
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << "\n";
        std::error_code ec;
        fs::remove_all(root, ec);
        return 1;
    }
    std::cout << "mcc settings tests passed\n";
    return 0;
}
