// haloce_runtime.cpp: the Halo CE runtime glue that lived in game.cpp (moved unchanged): the title hook
// supervisor's CE tick (the camera core and the CE feature polls), the CE AutoVr presentation tick (pause
// presentation, arm and detach, mode publication) and Game_CeControllerFeaturesRequested (game.h). Core
// reaches it through haloce_bridge.h and game.h; it reaches core through game_internal.h. The cross-title
// dispatchers with CE branches stay in game.cpp.
#include "../../core/game_internal.h"
#include "haloce_bridge.h"
using namespace game_core;
using namespace haloce_bridge;

namespace haloce_bridge
{
    void TickHaloCE(const TitleTickContext& c)
    {
        const TitleDescriptor* activeTitle = c.activeTitle;
        const bool activeLevelRunning = c.activeLevelRunning;
        const uint64_t pollNow = c.now;
        (void)pollNow;
        {
            uintptr_t ceBase = 0;
            size_t ceSize = 0;
            const bool ceActive = activeTitle && activeTitle->title == GameTitle::HaloCE &&
                                  !g_vrRuntimeFailureLatched.load(std::memory_order_acquire);
            if (ceActive)
                sig::ModuleRange(L"halo1.dll", ceBase, ceSize);
            HaloCE_Poll(ceBase, ceSize, TitleAdapter_GetGeneration(GameTitle::HaloCE), ceActive, activeLevelRunning);
            // Feature transactions retire independently. A missing hand
            // graph or HUD target must never disarm CE's camera core.
            const auto ceGeneration = TitleAdapter_GetGeneration(GameTitle::HaloCE);
            HaloCEControls_SetLookTutorial(g_config.ce_tutorial_head_look);
            (void)HaloCEControls_Poll(ceBase, ceSize, ceGeneration, ceActive && HaloCE_Armed());
            (void)HaloCEFirstPerson_Poll(ceBase, ceSize, ceGeneration, ceActive && HaloCE_Armed());
            (void)HaloCEContact_Poll(ceBase, ceSize, ceGeneration, ceActive && HaloCE_Armed());
            (void)HaloCEHud_Poll(ceBase, ceSize, ceGeneration, ceActive && HaloCE_Armed());
            (void)HaloCEHudLayout_Poll(ceBase, ceSize, ceGeneration, ceActive && HaloCE_Armed());
            (void)HaloCEComfort_Poll(ceBase, ceSize, ceGeneration, ceActive && HaloCE_Armed());
            (void)HaloCEOrientation_Poll(ceBase, ceSize, ceGeneration, ceActive && HaloCE_Armed());
            (void)HaloCENetworkInput_Poll(ceBase, ceSize, ceGeneration, ceActive && HaloCE_Armed());
            (void)HaloCEUnitControl_Poll(ceBase, ceSize, ceGeneration, ceActive && HaloCE_Armed());
        }
    }
} // namespace haloce_bridge

// CE presentation: camera-owned stereo, pause/menu presentation, mode publication.
// Returns true when this title owned the tick (Game_AutoVrTick returns immediately).
namespace haloce_bridge
{
    bool AutoVrTickHaloCE()
    {
        // CE owns its camera lifecycle. Keep the existing title branches intact.
        static bool wasCeContext = false;
        static halo_ce::RequestedMenuPresentation cePause;
        if (TitleAdapter_GetActiveTitle() == GameTitle::HaloCE)
        {
            if (!wasCeContext)
            {
                HaloCE_Recenter();
                g_autoVrUserVeto.store(false);
                VR_RequestPausePresentation(false);
            }
            wasCeContext = true;
            const bool ready = HaloCE_Armed() && !g_autoVrUserVeto.load() && !g_vrRuntimeFailureLatched.load();
            bool cePaused{}, ceSuppressed{}, ceInputKnown{};
            const bool cePauseKnown = ready && HaloCEControls_GetNativePaused(cePaused, &ceSuppressed, &ceInputKnown);
            const auto cePauseRequest = cePause.Observe(TitleAdapter_GetGeneration(GameTitle::HaloCE), ready,
                cePauseKnown, cePaused, ceInputKnown, ceSuppressed, VR_IsPausePresentationTarget(), GetTickCount64());
            if (cePauseRequest != halo_ce::PauseRequest::None)
            {
                VR_RequestPausePresentation(cePauseRequest == halo_ce::PauseRequest::Enter);
                if (!ready)
                    LOG("CE pause presentation: camera/presentation ownership ended; clearing head-lock for MCC shell");
                else
                    LOG("CE pause presentation: clock/input reconciliation restored %s",
                        cePauseRequest == halo_ce::PauseRequest::Enter ? "head-locked 2D" : "stereo 3D");
            }
            if (ready)
            {
                g_enabled.store(true, std::memory_order_release);
                g_autoVrOwned.store(true, std::memory_order_release);
                if (!VR_IsStereoEnabled())
                    VR_ToggleStereo();
                HaloCELocalPlayerState player{};
                RuntimeMode mode = cePauseKnown && cePaused ? RuntimeMode::Paused : RuntimeMode::Unsupported;
                if (mode != RuntimeMode::Paused && HaloCEControls_GetLocalPlayerState(player))
                {
                    if (player.nativePaused)
                        mode = RuntimeMode::Paused;
                    else if (player.nativeCinematicFlag)
                        mode = RuntimeMode::Cutscene;
                    else if (!player.hasControlledUnit)
                        mode = RuntimeMode::Dead;
                    else if (!player.nativeInputBlocked && !player.nativeLookBlocked)
                        mode = !player.onFoot                     ? RuntimeMode::Vehicle
                               : player.nativePreparesFirstPerson ? RuntimeMode::Gameplay
                                                                  : RuntimeMode::Unsupported;
                }
                TitleAdapter_PublishMode(GameTitle::HaloCE, TitleAdapter_GetGeneration(GameTitle::HaloCE),
                    VR_IsPausePresentationTarget() ? RuntimeMode::Paused : mode);
            }
            else if (g_autoVrOwned.exchange(false))
            {
                LOG("CE presentation detached by Game_AutoVrTick: core=%d userVeto=%d runtimeFailure=%d",
                    HaloCE_Armed() ? 1 : 0, g_autoVrUserVeto.load() ? 1 : 0, g_vrRuntimeFailureLatched.load() ? 1 : 0);
                g_enabled.store(false, std::memory_order_release);
                VR_DetachGamePresentation();
            }
            return true;
        }
        if (wasCeContext)
        {
            wasCeContext = false;
            cePause = {};
            HaloCE_PublishTracking({}, false);
            VR_RequestPausePresentation(false);
            g_enabled.store(false, std::memory_order_release);
            g_autoVrOwned.store(false, std::memory_order_release);
            VR_DetachGamePresentation();
        }
        return false;
    }
} // namespace haloce_bridge

bool Game_CeControllerFeaturesRequested()
{
    return TitleAdapter_GetActiveTitle() == GameTitle::HaloCE && g_enabled.load(std::memory_order_acquire) &&
           g_vrAim.load(std::memory_order_acquire) && VR_IsStereoEnabled();
}
