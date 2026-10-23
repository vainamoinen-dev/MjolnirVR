// Title registry: the table of supported MCC game modules, their capabilities and hook plans,
// and the small gates that decide which shared features a title may use.
// Implements title_registry.h for src/dll/core (title_adapter.h, title_reentry_probe.cpp) and the
// Reach action test guard; tested by tests/core_tests.cpp. Constant tables only: no state, no I/O.
#include "title_registry.h"

#include <cwctype>

namespace
{
    constexpr uint32_t kHalo3Capabilities = TitleCapability_Stereo | TitleCapability_ControllerAim |
                                            TitleCapability_Hud | TitleCapability_ArmIk | TitleCapability_RuntimeModes |
                                            TitleCapability_RoomScale | TitleCapability_ControllerInput |
                                            TitleCapability_Haptics | TitleCapability_CutsceneTheater;
    constexpr uint32_t kHalo3AdmissionCapabilities = TitleCapability_ControllerInput;
    // Reach camera + motion core: stereo, tracked look/turn, locomotion input,
    // room-scale translation, controller aim, articulated first-person arms,
    // and haptics now have title-specific runtime/evidence paths. Native HUD
    // remains withheld until Reach's final CHUD anchor is proven and wired.
    constexpr uint32_t kReachCapabilities = TitleCapability_Stereo | TitleCapability_ControllerAim |
                                            TitleCapability_ArmIk | TitleCapability_Hud | TitleCapability_RuntimeModes |
                                            TitleCapability_RoomScale | TitleCapability_ControllerInput |
                                            TitleCapability_Haptics | TitleCapability_CutsceneTheater;
    constexpr uint32_t kOdstAdmissionCapabilities = TitleCapability_ControllerInput;
    constexpr uint32_t kReachAdmissionCapabilities = TitleCapability_ControllerInput;
    // Halo 4 camera + motion core. Stereo, head tracking, 6DOF and
    // headset-owned pitch are proven in the headset; closed-loop controller
    // aim, VR turn and haptics run on the same shared code paths as the other
    // titles. The headset-proven native CUI-root and curvature consumers are
    // on; the rejected tag-basis writer remains dormant. Rigid floating hands
    // replace the rejected arm solve, so ArmIk is deliberately
    // absent. Halo 4's H4EK-derived cinematic globals and live authored-shot
    // look constraints now publish the common CutsceneTheater contract from
    // its claimed camera scope; unknown proof remains immersive.
    constexpr uint32_t kHalo4Capabilities = TitleCapability_Stereo | TitleCapability_ControllerAim |
                                            TitleCapability_Hud | TitleCapability_RuntimeModes |
                                            TitleCapability_RoomScale | TitleCapability_ControllerInput |
                                            TitleCapability_Haptics | TitleCapability_CutsceneTheater;
    constexpr uint32_t kHalo4AdmissionCapabilities = TitleCapability_ControllerInput;
    // Halo 2 stereo owns same-frame binocular geometry and headset room-scale only;
    // unclaimed frames retain stock screen presentation, while a partially
    // claimed failed stereo transaction drops only that frame.
    // The native gameplay CHUD stays inside the submitted OpenXR crop,
    // with its size/aspect/height layout controls. Cutscene
    // behavior remains denied.
    // The virtual gamepad (VR controllers merged over any physical
    // pad) and rumble are granted. Without ControllerInput the XInput hook
    // passed the physical pad through untouched inside every Halo 2 level,
    // so the VR controllers only worked in the shell.
    // ControllerAim is granted for the floating hands + gun mesh and the
    // bullet direction that follows them. It is NOT permission to steer the
    // camera: `Game_ComputeAimStick` refuses Halo 2 before any capability is
    // read, so the physical right stick keeps ordinary character/camera
    // turning. The second gate is the final-palette build switch plus its live
    // hook state; the rejected interpolator-space switch remains false.
    constexpr uint32_t kHalo2Capabilities =
        TitleCapability_Stereo | TitleCapability_RoomScale | TitleCapability_RuntimeModes |
        TitleCapability_ControllerInput | TitleCapability_Haptics | TitleCapability_ControllerAim | TitleCapability_Hud;
    constexpr uint32_t kHalo2AdmissionCapabilities = TitleCapability_ControllerInput;

    constexpr TitleDescriptor kTitles[] = {
        {GameTitle::Halo3, L"halo3.dll", "Halo 3", true, kHalo3Capabilities, kHalo3AdmissionCapabilities},
        {GameTitle::Halo3ODST, L"halo3odst.dll", "Halo 3: ODST", false, TitleCapability_None,
            kOdstAdmissionCapabilities},
        {GameTitle::HaloReach, L"haloreach.dll", "Halo: Reach", true, kReachCapabilities, kReachAdmissionCapabilities},
        {GameTitle::Halo4, L"halo4.dll", "Halo 4", false, kHalo4Capabilities, kHalo4AdmissionCapabilities},
        {GameTitle::HaloCE, L"halo1.dll", "Halo: CE Anniversary", false,
            TitleCapability_Stereo | TitleCapability_RoomScale | TitleCapability_ControllerInput |
                TitleCapability_RuntimeModes | TitleCapability_Haptics,
            TitleCapability_ControllerInput},
        {GameTitle::Halo2, L"halo2.dll", "Halo 2 Anniversary", false, kHalo2Capabilities, kHalo2AdmissionCapabilities},
    };

    bool EqualsModuleName(std::wstring_view left, std::wstring_view right)
    {
        size_t slash = left.find_last_of(L'/');
        const size_t backslash = left.find_last_of(static_cast<wchar_t>(92));
        if (backslash != std::wstring_view::npos && (slash == std::wstring_view::npos || backslash > slash))
            slash = backslash;
        if (slash != std::wstring_view::npos)
            left.remove_prefix(slash + 1);
        if (left.size() != right.size())
            return false;
        for (size_t i = 0; i < left.size(); ++i)
        {
            if (std::towlower(left[i]) != std::towlower(right[i]))
                return false;
        }
        return true;
    }
}

const TitleDescriptor* TitleRegistry_All(size_t& count)
{
    count = sizeof(kTitles) / sizeof(kTitles[0]);
    return kTitles;
}

const TitleDescriptor* TitleRegistry_Find(GameTitle title)
{
    for (const auto& descriptor : kTitles)
    {
        if (descriptor.title == title)
            return &descriptor;
    }
    return nullptr;
}

const TitleDescriptor* TitleRegistry_FromModuleName(std::wstring_view moduleName)
{
    for (const auto& descriptor : kTitles)
    {
        if (EqualsModuleName(moduleName, descriptor.moduleName))
            return &descriptor;
    }
    return nullptr;
}

TitleHookPlan TitleRegistry_HookPlan(GameTitle title)
{
    switch (title)
    {
    case GameTitle::Halo3:
        return TitleHookPlan::Halo3Full;
    case GameTitle::Halo3ODST:
        return TitleHookPlan::OdstExperimentalCameraCore;
    case GameTitle::HaloReach:
        return TitleHookPlan::ReachCameraCore;
    case GameTitle::HaloCE:
        return TitleHookPlan::HaloCECameraCore;
    case GameTitle::Halo4:
        return TitleHookPlan::Halo4CameraCore;
    case GameTitle::Halo2:
        return TitleHookPlan::Halo2StereoCore;
    default:
        return TitleHookPlan::None;
    }
}

bool TitleRegistry_AllowsSharedGameplayFeatures(GameTitle activeTitle, bool halo3CameraOwned, bool cameraOnlyOwned)
{
    if (cameraOnlyOwned)
        return false;
    if (activeTitle == GameTitle::None || activeTitle == GameTitle::Halo3)
        return true;
    return activeTitle == GameTitle::Unknown && halo3CameraOwned;
}

bool TitleRegistry_AllowsSharedControllerInput(GameTitle activeTitle, bool resolvedOwnerAllowsControllerInput,
    bool cameraOnlyOwned, bool allowAmbiguousFrontend, bool explicitTitleAllowsControllerInput)
{
    if (activeTitle == GameTitle::Unknown)
        return resolvedOwnerAllowsControllerInput || (allowAmbiguousFrontend && !cameraOnlyOwned);
    if (activeTitle == GameTitle::Halo3ODST)
        return explicitTitleAllowsControllerInput;
    if (cameraOnlyOwned)
        return false;
    if (activeTitle == GameTitle::None)
        return true;
    return explicitTitleAllowsControllerInput;
}

bool TitleRegistry_AllowsGenericDrawDistance(GameTitle activeTitle, bool activeLevelRunning)
{
    if (!activeLevelRunning)
        return false;
    // Halo 2 support is read-only apart from two scoped camera-position
    // writes. The generic draw-distance helper scans the active image and may
    // write an unrelated debug variable, so it remains unreachable there (and in CE).
    return activeTitle != GameTitle::None && activeTitle != GameTitle::Unknown && activeTitle != GameTitle::Halo2 &&
           activeTitle != GameTitle::HaloCE;
}

bool TitleRegistry_Halo3CameraOwnsAmbiguousState(uint64_t now, uint64_t lastCamera, uint64_t titleTransition)
{
    return titleTransition != 0 && lastCamera > titleTransition && now >= lastCamera &&
           now - lastCamera < kHalo3AmbiguousCameraOwnershipMs;
}

const char* RuntimeModeName(RuntimeMode mode)
{
    switch (mode)
    {
    case RuntimeMode::Shell:
        return "shell";
    case RuntimeMode::Loading:
        return "loading";
    case RuntimeMode::Gameplay:
        return "gameplay";
    case RuntimeMode::Paused:
        return "paused";
    case RuntimeMode::Cutscene:
        return "cutscene";
    case RuntimeMode::Vehicle:
        return "vehicle";
    case RuntimeMode::Turret:
        return "turret";
    case RuntimeMode::Dead:
        return "death";
    case RuntimeMode::Unsupported:
        return "unsupported";
    default:
        return "unknown";
    }
}
