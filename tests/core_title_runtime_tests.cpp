// core_title_runtime_tests.cpp: title runtime state, slots, theatre contract, heartbeat policy and ownership.
// Units 39, 40, 41, 42, 43, 44, 45 of the former tests/core_tests.cpp main(), moved unchanged and in
// their order (E3). Built as halomccvr_core_title_runtime_tests and run by ctest; each unit prints its check count.
#include "core_test_support.h"

int main()
{
    CheckUnit(39);
    {
        Check(kTitleRuntimeSlotCount == 6 && TitleRuntimeSlotIndex(GameTitle::Halo3) == 0 &&
                  TitleRuntimeSlotIndex(GameTitle::Halo3ODST) == 1 &&
                  TitleRuntimeSlotIndex(GameTitle::HaloReach) == 2 && TitleRuntimeSlotIndex(GameTitle::Halo4) == 3 &&
                  TitleRuntimeSlotIndex(GameTitle::HaloCE) == 4 && TitleRuntimeSlotIndex(GameTitle::Halo2) == 5 &&
                  TitleRuntimeSlotIndex(GameTitle::None) == kInvalidTitleRuntimeSlot &&
                  TitleRuntimeSlotIndex(GameTitle::Unknown) == kInvalidTitleRuntimeSlot,
            "title-runtime slots cover only concrete MCC game modules");
        Check(TitleRuntimeSlotTitle(0) == GameTitle::Halo3 && TitleRuntimeSlotTitle(1) == GameTitle::Halo3ODST &&
                  TitleRuntimeSlotTitle(2) == GameTitle::HaloReach && TitleRuntimeSlotTitle(3) == GameTitle::Halo4 &&
                  TitleRuntimeSlotTitle(4) == GameTitle::HaloCE && TitleRuntimeSlotTitle(5) == GameTitle::Halo2 &&
                  TitleRuntimeSlotTitle(6) == GameTitle::None,
            "title-runtime slot mapping is reversible and bounds checked");
        Check(TitleRuntimeAvailabilityBit(GameTitle::Halo3) == 0x01u &&
                  TitleRuntimeAvailabilityBit(GameTitle::Halo3ODST) == 0x02u &&
                  TitleRuntimeAvailabilityBit(GameTitle::HaloReach) == 0x04u &&
                  TitleRuntimeAvailabilityBit(GameTitle::Halo4) == 0x08u &&
                  TitleRuntimeAvailabilityBit(GameTitle::HaloCE) == 0x10u &&
                  TitleRuntimeAvailabilityBit(GameTitle::Halo2) == 0x20u &&
                  TitleRuntimeAvailabilityBit(GameTitle::Unknown) == 0 && kTitleRuntimeAvailabilityMask == 0x3Fu,
            "title-runtime availability bits are stable and non-overlapping");
        Check(static_cast<uint32_t>(TitleCapability_ControllerInput) == 0x40u &&
                  static_cast<uint32_t>(TitleCapability_Haptics) == 0x80u &&
                  static_cast<uint32_t>(TitleCapability_CutsceneTheater) == 0x100u &&
                  kTitleRuntimeKnownCapabilities == 0x1FFu,
            "controller input, haptics, and cutscene theatre extend the known capability mask exactly");
        Check(TitleRuntimeHeartbeatWindowMs(GameTitle::Halo3) == 500 &&
                  TitleRuntimeHeartbeatWindowMs(GameTitle::Halo3ODST) == 5001 &&
                  TitleRuntimeHeartbeatWindowMs(GameTitle::HaloReach) == 500 &&
                  TitleRuntimeHeartbeatWindowMs(GameTitle::Halo4) == 500 &&
                  TitleRuntimeHeartbeatWindowMs(GameTitle::HaloCE) == 500 &&
                  TitleRuntimeHeartbeatWindowMs(GameTitle::Halo2) == 500 &&
                  TitleRuntimeHeartbeatWindowMs(GameTitle::Unknown) == 0,
            "heartbeat windows pin established titles and grant H2 500 ms only "
            "when a runtime camera stage is compiled");
        const TitleRuntimeHeartbeatPolicy windowPolicy = MakeTitleRuntimeHeartbeatPolicy();
        bool heartbeatWindowsMirrorTable = true;
        for (size_t slot = 0; slot < kTitleRuntimeSlotCount; ++slot)
            heartbeatWindowsMirrorTable =
                heartbeatWindowsMirrorTable &&
                windowPolicy.freshForMs[slot] == TitleRuntimeHeartbeatWindowMs(TitleRuntimeSlotTitle(slot));
        Check(heartbeatWindowsMirrorTable, "the built heartbeat policy mirrors the window table slot for slot");
        bool retainedTruthTable = true;
        for (uint32_t mask = 0; mask < (1u << kTitleRuntimeSlotCount); ++mask)
        {
            std::array<uint32_t, kTitleRuntimeSlotCount> generations{};
            size_t nonzeroCount = 0;
            size_t nonzeroSlot = 0;
            for (size_t slot = 0; slot < kTitleRuntimeSlotCount; ++slot)
            {
                if ((mask & (uint32_t{1} << slot)) == 0)
                    continue;
                generations[slot] = static_cast<uint32_t>(slot) + 7u;
                ++nonzeroCount;
                nonzeroSlot = slot;
            }
            const GameTitle expected = nonzeroCount == 1 ? TitleRuntimeSlotTitle(nonzeroSlot) : GameTitle::None;
            retainedTruthTable = retainedTruthTable && RetainedRuntimeTitleFromGenerations(generations) == expected;
        }
        Check(retainedTruthTable,
            "over all 64 generation combinations the retained runtime title is the unique nonzero-generation title, else None (H3-only stays Halo3, H3+ODST stays None)");

        bool levelGateTruthTable = true;
        for (uint32_t bits = 0; bits < 8; ++bits)
        {
            const bool active = (bits & 1u) != 0;
            const bool installed = (bits & 2u) != 0;
            const bool running = (bits & 4u) != 0;
            const TitleLevelGateAction expected =
                installed && (!active || !running) ? TitleLevelGateAction::RemoveInstalledCore
                                                   : (!active && !installed ? TitleLevelGateAction::RearmForFutureEntry
                                                                            : TitleLevelGateAction::HoldEvidence);
            levelGateTruthTable =
                levelGateTruthTable && ResolveTitleLevelGateAction(active, installed, running) == expected;
        }
        Check(levelGateTruthTable &&
                  ResolveTitleLevelGateAction(true, false, false) == TitleLevelGateAction::HoldEvidence &&
                  ResolveTitleLevelGateAction(false, false, false) == TitleLevelGateAction::RearmForFutureEntry,
            "all eight shared level-gate states preserve loading evidence only "
            "for the active title, retire installed cores at a boundary, and "
            "rearm inactive uninstalled titles for future entry");
    }

    CheckUnit(40);
    {
        const CinematicControlPublication locked{GameTitle::HaloCE, 77, CinematicControlState::AuthoredLocked, 1000};
        const uint32_t capability = TitleCapability_CutsceneTheater;
        Check(ResolveCinematicControl(locked, GameTitle::HaloCE, 77, capability, 1100) ==
                  CinematicControlState::AuthoredLocked,
            "a mock future title receives the generic theatre contract without a title check");
        const CinematicControlPublication halo4Locked{
            GameTitle::Halo4, 91, CinematicControlState::AuthoredLocked, 2000};
        Check(
            ResolveCinematicControl(halo4Locked, GameTitle::Halo4, 91, capability, 2250) ==
                    CinematicControlState::AuthoredLocked &&
                ResolveCinematicControl(halo4Locked, GameTitle::Halo3, 91, capability, 2250) ==
                    CinematicControlState::Unknown &&
                ResolveCinematicControl(halo4Locked, GameTitle::Halo4, 92, capability, 2250) ==
                    CinematicControlState::Unknown &&
                ResolveCinematicControl(halo4Locked, GameTitle::Halo4, 91, 0, 2250) == CinematicControlState::Unknown &&
                ResolveCinematicControl(halo4Locked, GameTitle::Halo4, 91, capability, 2501) ==
                    CinematicControlState::Unknown,
            "Stage 3CB admits Halo 4 theatre only for the matching live title, "
            "generation, capability, and fresh publication");
        Check(CutsceneTheaterRequested(true, CinematicControlState::AuthoredLocked) &&
                  !CutsceneTheaterRequested(false, CinematicControlState::AuthoredLocked) &&
                  !CutsceneTheaterRequested(true, CinematicControlState::PlayerControlled) &&
                  !CutsceneTheaterRequested(true, CinematicControlState::Unknown),
            "only enabled AuthoredLocked state requests theatre presentation");
        Check(ClassifyHalo3FamilyCinematicControl(true, true, true) == CinematicControlState::AuthoredLocked &&
                  ClassifyHalo3FamilyCinematicControl(true, false, false) == CinematicControlState::PlayerControlled &&
                  ClassifyHalo3FamilyCinematicControl(false, false, false) == CinematicControlState::Unknown &&
                  ClassifyHalo3FamilyCinematicControl(true, true, false) == CinematicControlState::Unknown,
            "Halo 3 and ODST require both their cinematic flag and shot-state proof");
        Halo3CinematicFacing facing;
        using Cine = CinematicControlState;
        for (int pulse = 0; pulse < 100; ++pulse)
        {
            Check(!facing.Observe(3, Cine::AuthoredLocked, -1, -1) && !facing.Observe(3, Cine::Unknown, -1, -1) &&
                      !facing.Observe(3, Cine::PlayerControlled, -1, -1),
                "Cortana flag pulses without a shot never rotate gameplay facing");
        }
        Check(!facing.Observe(3, Cine::AuthoredLocked, 2, -1) && !facing.Observe(3, Cine::AuthoredLocked, -1, 0) &&
                  facing.Observe(3, Cine::AuthoredLocked, 2, 0) && !facing.Observe(3, Cine::AuthoredLocked, 2, 0),
            "a real shot, including shot zero, aligns once and requires both IDs");
        Check(!facing.Observe(3, Cine::Unknown, -1, -1) && !facing.Observe(3, Cine::AuthoredLocked, -1, -1) &&
                  !facing.Observe(3, Cine::AuthoredLocked, 2, 0) && facing.Observe(3, Cine::AuthoredLocked, 2, 1) &&
                  facing.Observe(3, Cine::AuthoredLocked, 3, 0),
            "unavailable reads preserve the shot; actual scene/shot cuts still align");
        Check(facing.Observe(3, Cine::PlayerControlled, -1, -1) && !facing.Observe(3, Cine::PlayerControlled, -1, -1) &&
                  facing.Observe(3, Cine::AuthoredLocked, 3, 0) && !facing.Observe(4, Cine::PlayerControlled, -1, -1) &&
                  !facing.Observe(0, Cine::AuthoredLocked, 3, 0) && facing.Observe(4, Cine::AuthoredLocked, 3, 0),
            "a proven shot exits once; reload and unowned generations cannot inherit an exit");
        const float odstLockedLook[4] = {};
        const float odstNoLookRate[4] = {};
        const float odstFreeLook[4] = {0.0f, -0.1f, 0.0f, 0.2f};
        const float odstOpeningLookRate[4] = {0.0f, -0.01f, 0.0f, 0.01f};
        const float odstInvalidLook[4] = {0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f};
        Check(ClassifyOdstCinematicControl(CinematicControlState::AuthoredLocked, true, odstLockedLook, odstNoLookRate,
                  0) == CinematicControlState::AuthoredLocked &&
                  ClassifyOdstCinematicControl(CinematicControlState::AuthoredLocked, true, odstFreeLook,
                      odstNoLookRate, 0) == CinematicControlState::PlayerControlled &&
                  ClassifyOdstCinematicControl(CinematicControlState::AuthoredLocked, true, odstLockedLook,
                      odstOpeningLookRate, 60) == CinematicControlState::PlayerControlled &&
                  ClassifyOdstCinematicControl(CinematicControlState::AuthoredLocked, true, odstLockedLook,
                      odstOpeningLookRate, 0) == CinematicControlState::AuthoredLocked,
            "ODST qualifies only zero live look freedom with no active opening interpolation");
        Check(ClassifyOdstCinematicControl(CinematicControlState::AuthoredLocked, false, odstLockedLook, odstNoLookRate,
                  0) == CinematicControlState::Unknown &&
                  ClassifyOdstCinematicControl(CinematicControlState::AuthoredLocked, true, odstInvalidLook,
                      odstNoLookRate, 0) == CinematicControlState::Unknown &&
                  ClassifyOdstCinematicControl(CinematicControlState::AuthoredLocked, true, odstLockedLook,
                      odstNoLookRate, -1) == CinematicControlState::Unknown &&
                  ClassifyOdstCinematicControl(CinematicControlState::PlayerControlled, false, nullptr, nullptr, -1) ==
                      CinematicControlState::PlayerControlled,
            "ODST missing or invalid look proof fails closed without weakening known gameplay state");
        const float halo4LockedLook[4] = {};
        const float halo4NoLookRate[4] = {};
        const float halo4FreeLook[4] = {0.0f, -0.001f, 0.0f, 0.0f};
        const float halo4OpeningLookRate[4] = {0.0f, 0.001f, 0.0f, 0.0f};
        const float halo4BoundaryLook[4] = {
            kHalo4CinematicLookFreedomEpsilon, -kHalo4CinematicLookFreedomEpsilon, 0.0f, 0.0f};
        const float halo4BoundaryRate[4] = {
            0.0f, kHalo4CinematicLookFreedomEpsilon, -kHalo4CinematicLookFreedomEpsilon, 0.0f};
        const float halo4InvalidLook[4] = {0.0f, std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
        const float halo4InvalidRate[4] = {0.0f, 0.0f, std::numeric_limits<float>::infinity(), 0.0f};
        Check(kHalo4CinematicCameraType == 6 && kHalo4CinematicMaximumTicks == 360000 &&
                  std::fabs(kHalo4CinematicLookFreedomEpsilon - 0.0001f) < 1.0e-8f,
            "Stage 3BX preserves Halo 4's proven camera type, tick bound, and "
            "look-freedom epsilon");
        Check(ClassifyHalo4CinematicControl(false, false, false, 0, nullptr, nullptr, 0) ==
                      CinematicControlState::Unknown &&
                  ClassifyHalo4CinematicControl(true, false, false, 0, nullptr, nullptr, -1) ==
                      CinematicControlState::PlayerControlled,
            "Stage 3BX requires cinematic globals but treats a proven inactive "
            "cinematic as player-controlled before consulting camera state");
        Check(ClassifyHalo4CinematicControl(true, true, false, kHalo4CinematicCameraType, halo4LockedLook,
                  halo4NoLookRate, 0) == CinematicControlState::Unknown &&
                  ClassifyHalo4CinematicControl(true, true, true, kHalo4CinematicCameraType - 1, halo4LockedLook,
                      halo4NoLookRate, 0) == CinematicControlState::Unknown &&
                  ClassifyHalo4CinematicControl(true, true, true, kHalo4CinematicCameraType, nullptr, halo4NoLookRate,
                      0) == CinematicControlState::Unknown &&
                  ClassifyHalo4CinematicControl(true, true, true, kHalo4CinematicCameraType, halo4LockedLook,
                      halo4NoLookRate, -1) == CinematicControlState::Unknown &&
                  ClassifyHalo4CinematicControl(true, true, true, kHalo4CinematicCameraType, halo4LockedLook,
                      halo4NoLookRate, kHalo4CinematicMaximumTicks + 1) == CinematicControlState::Unknown,
            "Stage 3BX leaves theatre unknown for missing or wrong camera state, "
            "missing look limits, and invalid interpolation ticks");
        Check(ClassifyHalo4CinematicControl(true, true, true, kHalo4CinematicCameraType, halo4LockedLook, nullptr, 0) ==
                      CinematicControlState::AuthoredLocked &&
                  ClassifyHalo4CinematicControl(true, true, true, kHalo4CinematicCameraType, halo4LockedLook,
                      halo4InvalidRate, 0) == CinematicControlState::AuthoredLocked,
            "Stage 3BX locks a zero-tick authored shot without requiring or "
            "consulting look-rate storage");
        Check(ClassifyHalo4CinematicControl(true, true, true, kHalo4CinematicCameraType, halo4FreeLook, halo4NoLookRate,
                  0) == CinematicControlState::PlayerControlled &&
                  ClassifyHalo4CinematicControl(true, true, true, kHalo4CinematicCameraType, halo4InvalidLook,
                      halo4NoLookRate, 0) == CinematicControlState::PlayerControlled,
            "Stage 3BX treats finite or malformed live look freedom as player "
            "control, never as room-fixed theatre");
        Check(ClassifyHalo4CinematicControl(true, true, true, kHalo4CinematicCameraType, halo4LockedLook, nullptr, 1) ==
                      CinematicControlState::Unknown &&
                  ClassifyHalo4CinematicControl(true, true, true, kHalo4CinematicCameraType, halo4LockedLook,
                      halo4OpeningLookRate, 1) == CinematicControlState::PlayerControlled &&
                  ClassifyHalo4CinematicControl(true, true, true, kHalo4CinematicCameraType, halo4LockedLook,
                      halo4InvalidRate, 1) == CinematicControlState::PlayerControlled &&
                  ClassifyHalo4CinematicControl(true, true, true, kHalo4CinematicCameraType, halo4LockedLook,
                      halo4NoLookRate, kHalo4CinematicMaximumTicks) == CinematicControlState::AuthoredLocked,
            "Stage 3BX requires finite zero look rates only while interpolation "
            "is pending, including at the proven maximum tick count");
        Check(ClassifyHalo4CinematicControl(true, true, true, kHalo4CinematicCameraType, halo4BoundaryLook,
                  halo4BoundaryRate, 1) == CinematicControlState::AuthoredLocked,
            "Stage 3BX keeps exact epsilon-boundary angle and rate noise inside "
            "the authored-lock classification");
        Check(ClassifyReachCinematicControl(true, 1) == CinematicControlState::AuthoredLocked &&
                  ClassifyReachCinematicControl(true, 0) == CinematicControlState::PlayerControlled &&
                  ClassifyReachCinematicControl(false, 1) == CinematicControlState::Unknown,
            "Reach qualifies only its proven cinematic-globals +0x24 state");
        Check(ShouldDisableWidescreenCinematicFov(true, true, false) &&
                  !ShouldDisableWidescreenCinematicFov(true, true, true) &&
                  !ShouldDisableWidescreenCinematicFov(true, false, false) &&
                  !ShouldDisableWidescreenCinematicFov(false, true, false),
            "the stock cinematic FOV is restored only in theatre while immersive VR keeps its existing widening policy");
        Check(UseAuthoredCutsceneProjection(true, 4.0f / 3.0f) && !UseAuthoredCutsceneProjection(false, 4.0f / 3.0f) &&
                  !UseAuthoredCutsceneProjection(true, 0.0f),
            "authored projection replacement is isolated to active theatre with proven metadata");
        const Halo3CutsceneTheaterFrustum immersiveHalo3 =
            SelectHalo3CutsceneTheaterFrustum(false, 1.20f, 0.90f, 1.84f, 1.33f);
        Check(immersiveHalo3.valid && immersiveHalo3.projectionTangentX == 1.84f &&
                  immersiveHalo3.projectionTangentY == 1.33f && immersiveHalo3.projectionCenterYAdjustment == 0.0f &&
                  immersiveHalo3.cullingTangentX == 1.84f && immersiveHalo3.cullingTangentY == 1.33f,
            "Halo 3 immersive projection and culling remain byte-for-byte on the existing OpenXR cover");
        const Halo3CutsceneTheaterFrustum theaterHalo3 =
            SelectHalo3CutsceneTheaterFrustum(true, 1.20f, 0.90f, 1.84f, 1.33f);
        Check(theaterHalo3.valid && std::fabs(theaterHalo3.projectionTangentX - 1.02f) < 0.00001f &&
                  std::fabs(theaterHalo3.projectionTangentY - 0.765f) < 0.00001f &&
                  std::fabs(theaterHalo3.projectionCenterYAdjustment + 0.10f) < 0.00001f &&
                  theaterHalo3.cullingTangentX == 1.84f && theaterHalo3.cullingTangentY == 1.33f &&
                  std::fabs(theaterHalo3.projectionTangentX / theaterHalo3.projectionTangentY - 4.0f / 3.0f) < 0.00001f,
            "Halo 3 theatre tightens and lifts framing without changing aspect or the wider visibility cover");
        Check(!SelectHalo3CutsceneTheaterFrustum(true, 0.0f, 0.90f, 1.84f, 1.33f).valid &&
                  !SelectHalo3CutsceneTheaterFrustum(false, 1.20f, 0.90f, 0.0f, 1.33f).valid,
            "Halo 3 theatre rejects invalid authored or immersive frustum evidence");
        const float tangentAspect = CutsceneTheaterAspectFromTangents(4.0f / 3.0f, 1.0f);
        const float projectionAspect = CutsceneTheaterAspectFromProjectionScales(0.75f, 1.0f);
        const float dimensionAspect = CutsceneTheaterAspectFromDimensions(2912, 2100);
        Check(std::fabs(tangentAspect - 4.0f / 3.0f) < 0.00001f &&
                  std::fabs(projectionAspect - 4.0f / 3.0f) < 0.00001f &&
                  std::fabs(dimensionAspect - 2912.0f / 2100.0f) < 0.00001f &&
                  CutsceneTheaterAspectFromTangents(1.0f, 0.0f) == 0.0f &&
                  CutsceneTheaterAspectFromProjectionScales(0.0f, 1.0f) == 0.0f &&
                  CutsceneTheaterAspectFromDimensions(0, 2100) == 0.0f,
            "authored aspect decodes safely from camera tangents, projection scales, and image dimensions");
        Check(CutsceneTheaterProjectionMatchesAspect(4.0f / 3.0f, 0.75f, 1.0f) &&
                  !CutsceneTheaterProjectionMatchesAspect(16.0f / 9.0f, 0.75f, 1.0f),
            "a rebuilt theatre projection must still match the pristine authored aspect");
        const CutsceneTheaterProjectionPublication authoredProjection{GameTitle::HaloCE, 77, 4.0f / 3.0f, 1000};
        float resolvedAspect = 0.0f;
        Check(ResolveCutsceneTheaterProjection(
                  authoredProjection, GameTitle::HaloCE, 77, capability, 1100, resolvedAspect) &&
                  std::fabs(resolvedAspect - 4.0f / 3.0f) < 0.00001f,
            "a future title's fresh generation-tagged authored aspect resolves generically");
        Check(!ResolveCutsceneTheaterProjection(
                  authoredProjection, GameTitle::HaloCE, 78, capability, 1100, resolvedAspect) &&
                  !ResolveCutsceneTheaterProjection(
                      authoredProjection, GameTitle::Halo3, 77, capability, 1100, resolvedAspect) &&
                  !ResolveCutsceneTheaterProjection(
                      authoredProjection, GameTitle::HaloCE, 77, TitleCapability_None, 1100, resolvedAspect) &&
                  !ResolveCutsceneTheaterProjection(
                      authoredProjection, GameTitle::HaloCE, 77, capability, 1501, resolvedAspect),
            "authored aspect rejects title, generation, capability, and freshness mismatches");
        CinematicControlPublication publication = locked;
        publication.state = CinematicControlState::PlayerControlled;
        Check(ResolveCinematicControl(publication, GameTitle::HaloCE, 77, capability, 1100) ==
                  CinematicControlState::PlayerControlled,
            "player-controlled sequences remain immersive");
        publication.state = CinematicControlState::Unknown;
        Check(ResolveCinematicControl(publication, GameTitle::HaloCE, 77, capability, 1100) ==
                  CinematicControlState::Unknown,
            "unproven sequences remain immersive");
        Check(ResolveCinematicControl(locked, GameTitle::HaloCE, 78, capability, 1100) ==
                      CinematicControlState::Unknown &&
                  ResolveCinematicControl(locked, GameTitle::Halo3, 77, capability, 1100) ==
                      CinematicControlState::Unknown &&
                  ResolveCinematicControl(locked, GameTitle::HaloCE, 77, TitleCapability_None, 1100) ==
                      CinematicControlState::Unknown &&
                  ResolveCinematicControl(locked, GameTitle::HaloCE, 77, capability, 1501) ==
                      CinematicControlState::Unknown,
            "title transitions, generation changes, capability loss, and stale state exit theatre");

        float position[3]{0.032f, -0.004f, 0.002f};
        float orientation[4]{0.1f, 0.2f, 0.3f, 0.9f};
        ApplyCutsceneTheaterEyeTransform(true, 0.0f, position, orientation);
        Check(position[0] == 0.0f && position[1] == 0.0f && position[2] == 0.0f && orientation[0] == 0.0f &&
                  orientation[1] == 0.0f && orientation[2] == 0.0f && orientation[3] == 1.0f,
            "zero theatre depth is flat and uses parallel eye cameras");
        float doublePosition[3]{0.032f, -0.004f, 0.002f};
        float doubleOrientation[4]{0.1f, 0.2f, 0.3f, 0.9f};
        ApplyCutsceneTheaterEyeTransform(true, 2.0f, doublePosition, doubleOrientation);
        Check(std::fabs(doublePosition[0] - 0.064f) < 0.00001f && std::fabs(doublePosition[1] + 0.008f) < 0.00001f &&
                  std::fabs(doublePosition[2] - 0.004f) < 0.00001f,
            "two hundred percent theatre depth doubles natural eye separation");
        float unchangedPosition[3]{1.0f, 2.0f, 3.0f};
        float unchangedOrientation[4]{0.1f, 0.2f, 0.3f, 0.9f};
        ApplyCutsceneTheaterEyeTransform(false, 0.0f, unchangedPosition, unchangedOrientation);
        Check(unchangedPosition[0] == 1.0f && unchangedOrientation[0] == 0.1f,
            "inactive theatre leaves immersive eye transforms untouched");
        Check(CutsceneTheaterImageIndex(0, false) == 0 && CutsceneTheaterImageIndex(1, false) == 1 &&
                  CutsceneTheaterImageIndex(0, true) == 1 && CutsceneTheaterImageIndex(1, true) == 0,
            "Flip Depth swaps only the per-eye image slices");
        const float projectionEye[3]{0.0f, 0.0f, 0.0f};
        const float projectionOrientation[4]{0.0f, 0.0f, 0.0f, 1.0f};
        const float projectionCenter[3]{0.0f, 0.0f, -4.0f};
        const float projectionFov[4]{-0.785398163f, 0.785398163f, 0.785398163f, -0.785398163f};
        CutsceneTheaterClipVertex projectionVertices[4]{};
        Check(BuildCutsceneTheaterProjectionQuad(projectionEye, projectionOrientation, projectionCenter,
                  projectionOrientation, 6.0f, 3.375f, projectionFov, projectionVertices) &&
                  std::fabs(projectionVertices[0].x + 3.0f) < 0.00001f &&
                  std::fabs(projectionVertices[0].y - 1.6875f) < 0.00001f &&
                  std::fabs(projectionVertices[0].w - 4.0f) < 0.00001f &&
                  std::fabs(projectionVertices[3].x - 3.0f) < 0.00001f &&
                  std::fabs(projectionVertices[3].y + 1.6875f) < 0.00001f,
            "theatre screen projects into an ordinary stereo projection view");
        const float rightEye[3]{0.032f, 0.0f, 0.0f};
        CutsceneTheaterClipVertex rightEyeVertices[4]{};
        Check(BuildCutsceneTheaterProjectionQuad(rightEye, projectionOrientation, projectionCenter,
                  projectionOrientation, 6.0f, 3.375f, projectionFov, rightEyeVertices) &&
                  rightEyeVertices[0].x < projectionVertices[0].x && rightEyeVertices[3].x < projectionVertices[3].x,
            "room-fixed theatre projection preserves physical-eye parallax");
        constexpr float questRadians = 0.01745329251994329577f;
        const float questEyes[2][3]{{-0.03295f, 0.0f, 0.0f}, {0.03295f, 0.0f, 0.0f}};
        const float questFovs[2][4]{
            {-54.0f * questRadians, 40.0f * questRadians, 44.0f * questRadians, -55.0f * questRadians},
            {-40.0f * questRadians, 54.0f * questRadians, 44.0f * questRadians, -55.0f * questRadians}};
        const float questScreenCenter[3]{0.0f, 0.0f, -5.3f};
        bool questNativeViewsContainScreen = true;
        for (int eye = 0; eye < 2; ++eye)
        {
            CutsceneTheaterClipVertex questVertices[4]{};
            questNativeViewsContainScreen &= BuildCutsceneTheaterProjectionQuad(questEyes[eye], projectionOrientation,
                questScreenCenter, projectionOrientation, 6.0f, 3.375f, questFovs[eye], questVertices);
            for (const auto& vertex : questVertices)
            {
                questNativeViewsContainScreen &= vertex.w > 0.0f && std::fabs(vertex.x) <= vertex.w * 1.001f &&
                                                 std::fabs(vertex.y) <= vertex.w * 1.001f;
            }
        }
        Check(
            questNativeViewsContainScreen, "runtime-native asymmetric Quest views contain the configured room screen");
        const float behindCenter[3]{0.0f, 0.0f, 1.0f};
        Check(!BuildCutsceneTheaterProjectionQuad(projectionEye, projectionOrientation, behindCenter,
                  projectionOrientation, 6.0f, 3.375f, projectionFov, projectionVertices),
            "theatre projection rejects a screen behind the viewer");
        Check(std::fabs(CutsceneTheaterHeight(6.0f, 2912, 2100) - 6.0f * 2100.0f / 2912.0f) < 0.00001f &&
                  CutsceneTheaterHeight(6.0f, 0, 2100) == 0.0f,
            "theatre height preserves the native render-image aspect ratio");
        Check(std::fabs(CutsceneTheaterHeightFromAspect(6.0f, 4.0f / 3.0f) - 4.5f) < 0.00001f &&
                  CutsceneTheaterHeightFromAspect(6.0f, 0.0f) == 0.0f,
            "theatre quad height follows the authored cinematic projection aspect");

        // Halo 3 rasterizes its cutscene into the headset render shape, which
        // this machine's accepted run reports as 3262x2352 (1.387:1). Hiding
        // everything outside the 16:9 slice a monitor shows retains 78% of that
        // height, split evenly into two bars.
        const CutsceneTheaterMatte headsetShapeMatte =
            ComputeCutsceneTheaterMatte(3262.0f / 2352.0f, 16.0f / 9.0f, 0.0f);
        Check(headsetShapeMatte.active &&
                  std::fabs((headsetShapeMatte.vMax - headsetShapeMatte.vMin) - (3262.0f / 2352.0f) / (16.0f / 9.0f)) <
                      0.00001f &&
                  std::fabs((headsetShapeMatte.vMin + headsetShapeMatte.vMax) - 1.0f) < 0.00001f,
            "cine bars keep the centred 16:9 slice of the taller authored frame");
        const CutsceneTheaterMatte liftedMatte = ComputeCutsceneTheaterMatte(1.387f, 16.0f / 9.0f, 0.05f);
        Check(liftedMatte.active &&
                  std::fabs((liftedMatte.vMax - liftedMatte.vMin) - (headsetShapeMatte.vMax - headsetShapeMatte.vMin)) <
                      0.001f &&
                  liftedMatte.vMin < headsetShapeMatte.vMin,
            "a positive matte offset lifts the retained window without resizing it");
        const CutsceneTheaterMatte pinnedMatte = ComputeCutsceneTheaterMatte(1.387f, 16.0f / 9.0f, 0.5f);
        Check(pinnedMatte.active && pinnedMatte.vMin == 0.0f &&
                  std::fabs(pinnedMatte.vMax - (1.387f / (16.0f / 9.0f))) < 0.00001f,
            "an offset past the edge stops there at the full window size");
        Check(!ComputeCutsceneTheaterMatte(2.39f, 16.0f / 9.0f, 0.0f).active &&
                  !ComputeCutsceneTheaterMatte(1.387f, 0.0f, 0.0f).active &&
                  !ComputeCutsceneTheaterMatte(0.0f, 16.0f / 9.0f, 0.0f).active,
            "cine bars never rescale a frame already at or wider than the target");

        {
            // cutscene_fade_ms: both ends fade over the configured time (v7 150 ms).
            CutsceneTheaterTransition slow;
            slow.Update(1000, true, 150);
            Check(!slow.Update(1100, true, 150).active && slow.Update(1150, true, 150).switched,
                "a 150 ms fade switches to the theatre only at full black");
            Check(slow.Update(1225, true, 150).fadeAlpha > 0.4f && slow.Update(1300, true, 150).fadeAlpha == 0.0f,
                "and fades back in over 150 ms");
            Check(!slow.Update(1400, false, 150).switched && slow.Update(1450, false, 150).switched,
                "leaving the theatre fades to black as well");
            CutsceneTheaterTransition cut;
            Check(cut.Update(1000, true, 0).switched, "fade 0 is an instant cut");
            Check(CutsceneFadeMs(150.0f) == 150 && CutsceneFadeMs(5000.0f) == 1000 &&
                      CutsceneFadeMs(std::numeric_limits<float>::quiet_NaN()) == kCutsceneTheaterFadeMs,
                "fade time clamps; NaN keeps the legacy 100 ms");
            // A switch the game makes this frame cuts to black and fades back in.
            PresentationSwitchFade sw;
            Check(sw.Update(1000, true, true, 150) == 0.0f, "no fade without a switch");
            Check(sw.Update(1010, false, true, 150) == 1.0f && sw.Update(1085, false, true, 150) > 0.4f &&
                      sw.Update(1160, false, true, 150) == 0.0f,
                "VR to flat fades back in over the fade time");
            Check(sw.Update(1200, true, false, 150) == 0.0f, "menu or pause switches do not fade");
            // cutscene_3d_sections
            Check(CutsceneSectionStays3D("odst:3, reach:*", GameTitle::Halo3ODST, 3) &&
                      !CutsceneSectionStays3D("odst:3, reach:*", GameTitle::Halo3ODST, 4) &&
                      CutsceneSectionStays3D("odst:3, reach:*", GameTitle::HaloReach, -1) &&
                      !CutsceneSectionStays3D("odst:3, reach:*", GameTitle::Halo3, 3) &&
                      CutsceneSectionStays3D("H3:12;h2:*", GameTitle::Halo3, 12) &&
                      !CutsceneSectionStays3D("", GameTitle::Halo3, 1) &&
                      !CutsceneSectionStays3D("h3:", GameTitle::Halo3, 1) &&
                      !CutsceneSectionStays3D("h3:1x", GameTitle::Halo3, 1) &&
                      !CutsceneSectionStays3D("h3:5", GameTitle::Halo3, -1),
                "listed sections stay 3D; unlisted and malformed entries keep the theatre");
            char list[24]{};
            Check(AppendCutsceneSection(list, sizeof(list), GameTitle::Halo3ODST, 3) &&
                      std::string_view(list) == "odst:3" &&
                      !AppendCutsceneSection(list, sizeof(list), GameTitle::Halo3ODST, 3) &&
                      AppendCutsceneSection(list, sizeof(list), GameTitle::HaloReach, -1) &&
                      std::string_view(list) == "odst:3, reach:*" &&
                      CutsceneSectionStays3D(list, GameTitle::HaloReach, 7),
                "Keep this cutscene in 3D appends once, wildcard without a scene");
            Check(!AppendCutsceneSection(list, sizeof(list), GameTitle::Halo3, 123456) &&
                      std::string_view(list) == "odst:3, reach:*" &&
                      !AppendCutsceneSection(list, sizeof(list), GameTitle::None, 1),
                "a full list or unknown title is left untouched");
        }
        CutsceneTheaterTransition transition;
        Check(!transition.Update(1000, true).active, "theatre entry starts by fading the immersive view");
        const auto entryHalf = transition.Update(1050, true);
        const auto entered = transition.Update(1100, true);
        const auto entryFadeIn = transition.Update(1150, true);
        const auto entryDone = transition.Update(1200, true);
        Check(std::fabs(entryHalf.fadeAlpha - 0.5f) < 0.001f && entered.active && entered.switched &&
                  entered.fadeAlpha == 1.0f && entryFadeIn.active && std::fabs(entryFadeIn.fadeAlpha - 0.5f) < 0.001f &&
                  entryDone.active && entryDone.fadeAlpha == 0.0f,
            "theatre entry uses a 100 ms fade out and 100 ms fade in");
        const auto exitHalf = transition.Update(1250, false);
        const auto exited = transition.Update(1300, false);
        Check(exitHalf.active && std::fabs(exitHalf.fadeAlpha - 0.5f) < 0.001f && !exited.active && exited.switched &&
                  exited.fadeAlpha == 1.0f,
            "capability loss begins a comfort-faded theatre exit");
    }

    CheckUnit(41);
    {
        constexpr uint64_t kEpoch = 1000;
        constexpr uint64_t kFreshFor = 500;
        constexpr uint32_t kUnknownCapability = 0x80000000u;
        constexpr uint32_t kPublishedCapabilities = TitleCapability_Stereo | TitleCapability_ControllerAim |
                                                    TitleCapability_RuntimeModes | TitleCapability_ControllerInput |
                                                    TitleCapability_Haptics | TitleCapability_CutsceneTheater;
        constexpr uint32_t kArmRequiredCapabilities =
            TitleCapability_Stereo | TitleCapability_ControllerAim | TitleCapability_Hud | TitleCapability_ArmIk |
            TitleCapability_RoomScale | TitleCapability_Haptics | TitleCapability_CutsceneTheater;
        constexpr uint32_t kUnarmedCapabilities = TitleCapability_RuntimeModes | TitleCapability_ControllerInput;

        const auto isCanonicalFailure = [](const TitleRuntimeSnapshot& snapshot, uint32_t ownerCount)
        {
            return snapshot.owner == GameTitle::None && snapshot.generation == 0 && !snapshot.installed &&
                   !snapshot.armed && !snapshot.teardownRequested && snapshot.mode == RuntimeMode::Shell &&
                   snapshot.heartbeatMs == 0 && snapshot.enabledCapabilities == TitleCapability_None &&
                   snapshot.qualifyingOwnerCount == ownerCount;
        };

        TitleRuntimeResolveInput single{};
        single.availabilityMask = TitleRuntimeAvailabilityBit(GameTitle::Halo3);
        single.availabilitySetEpochMs = kEpoch;
        single.nowMs = kEpoch + 1;
        single.titles[TitleRuntimeSlotIndex(GameTitle::Halo3)] = {
            GameTitle::Halo3,
            11,
            900,
            true,
            false,
            false,
            RuntimeMode::Vehicle,
            kEpoch + 1,
            kFreshFor,
            kPublishedCapabilities | kUnknownCapability,
        };

        const TitleRuntimeSnapshot owner = ResolveTitleRuntime(single);
        Check(owner.owner == GameTitle::Halo3 && owner.generation == 11 && owner.installed && !owner.armed &&
                  !owner.teardownRequested && owner.mode == RuntimeMode::Vehicle && owner.heartbeatMs == kEpoch + 1 &&
                  owner.enabledCapabilities == kPublishedCapabilities && owner.qualifyingOwnerCount == 1,
            "one fresh installed title owns an exact generation-tagged snapshot");
        Check(TitleRuntimeMaskUnarmedCapabilities(owner, kArmRequiredCapabilities) == kUnarmedCapabilities,
            "an unarmed owner retains only caller-designated non-render capabilities");
        TitleRuntimeSnapshot armedOwner = owner;
        armedOwner.armed = true;
        armedOwner.enabledCapabilities |= kUnknownCapability;
        Check(TitleRuntimeMaskUnarmedCapabilities(armedOwner, kArmRequiredCapabilities) == kPublishedCapabilities,
            "an armed owner keeps all known capabilities and strips unknown bits");

        TitleRuntimeResolveInput zero{};
        zero.availabilityMask = TitleRuntimeAvailabilityBit(GameTitle::Halo3);
        zero.availabilitySetEpochMs = kEpoch;
        zero.nowMs = kEpoch + 1;
        Check(isCanonicalFailure(ResolveTitleRuntime(zero), 0),
            "zero fresh owners produces the canonical fail-open snapshot");

        auto epochBoundary = single;
        epochBoundary.nowMs = kEpoch;
        epochBoundary.titles[0].heartbeatMs = kEpoch;
        auto generationBoundary = single;
        generationBoundary.availabilitySetEpochMs = 800;
        generationBoundary.titles[0].generationStartMs = kEpoch + 1;
        generationBoundary.titles[0].heartbeatMs = kEpoch + 1;
        auto future = single;
        future.nowMs = kEpoch;
        auto zeroWindow = single;
        zeroWindow.titles[0].heartbeatFreshForMs = 0;
        Check(isCanonicalFailure(ResolveTitleRuntime(epochBoundary), 0) &&
                  isCanonicalFailure(ResolveTitleRuntime(generationBoundary), 0) &&
                  isCanonicalFailure(ResolveTitleRuntime(future), 0) &&
                  isCanonicalFailure(ResolveTitleRuntime(zeroWindow), 0),
            "epoch-equal, generation-equal, future, and zero-window heartbeats fail closed");

        auto lastFresh = single;
        lastFresh.nowMs = kEpoch + 1 + kFreshFor - 1;
        auto expired = lastFresh;
        expired.nowMs += 1;
        Check(ResolveTitleRuntime(lastFresh).owner == GameTitle::Halo3 &&
                  isCanonicalFailure(ResolveTitleRuntime(expired), 0),
            "heartbeat freshness accepts age window-minus-one and rejects exact expiry");

        auto uninstalled = single;
        uninstalled.titles[0].installed = false;
        auto teardown = single;
        teardown.titles[0].teardownRequested = true;
        auto zeroGeneration = single;
        zeroGeneration.titles[0].generation = 0;
        auto unavailable = single;
        unavailable.availabilityMask = 0;
        auto foreign = single;
        foreign.titles[0].title = GameTitle::Halo3ODST;
        Check(isCanonicalFailure(ResolveTitleRuntime(uninstalled), 0) &&
                  isCanonicalFailure(ResolveTitleRuntime(teardown), 0) &&
                  isCanonicalFailure(ResolveTitleRuntime(zeroGeneration), 0) &&
                  isCanonicalFailure(ResolveTitleRuntime(unavailable), 0) &&
                  isCanonicalFailure(ResolveTitleRuntime(foreign), 0),
            "uninstalled, teardown, zero-generation, unavailable, and foreign claims fail closed");

        auto multiple = single;
        multiple.availabilityMask |= TitleRuntimeAvailabilityBit(GameTitle::Halo3ODST);
        multiple.nowMs = kEpoch + 2;
        multiple.titles[TitleRuntimeSlotIndex(GameTitle::Halo3ODST)] = {
            GameTitle::Halo3ODST,
            22,
            900,
            true,
            true,
            false,
            RuntimeMode::Paused,
            kEpoch + 2,
            kFreshFor,
            TitleCapability_Stereo,
        };
        Check(isCanonicalFailure(ResolveTitleRuntime(multiple), 2),
            "two fresh title owners discard both complete snapshots");
        multiple.nowMs = kEpoch + 1 + kFreshFor;
        const TitleRuntimeSnapshot soleRemainder = ResolveTitleRuntime(multiple);
        Check(soleRemainder.owner == GameTitle::Halo3ODST && soleRemainder.generation == 22 &&
                  soleRemainder.mode == RuntimeMode::Paused && soleRemainder.qualifyingOwnerCount == 1,
            "one title wins only after the other expires at the exact freshness boundary");

        TitleRuntimeResolveInput pending{};
        pending.availabilityMask =
            TitleRuntimeAvailabilityBit(GameTitle::Halo3) | TitleRuntimeAvailabilityBit(GameTitle::Halo3ODST);
        pending.availabilitySetEpochMs = kEpoch;
        pending.nowMs = kEpoch;
        pending.titles[TitleRuntimeSlotIndex(GameTitle::Halo3)] = {
            GameTitle::Halo3,
            11,
            900,
            true,
            true,
            false,
            RuntimeMode::Gameplay,
            kEpoch,
            kFreshFor,
            kPublishedCapabilities,
        };
        Check(TitleRuntimeOwnershipMayBePending(pending, GameTitle::Halo3, 100),
            "a safe retained owner gets a short zero-owner post-transition grace");
        pending.nowMs = kEpoch + 99;
        Check(TitleRuntimeOwnershipMayBePending(pending, GameTitle::Halo3, 100),
            "pending ownership remains valid through grace-minus-one");
        pending.nowMs = kEpoch + 100;
        Check(!TitleRuntimeOwnershipMayBePending(pending, GameTitle::Halo3, 100),
            "pending ownership expires at the exact grace boundary");
        pending.nowMs = kEpoch - 1;
        Check(!TitleRuntimeOwnershipMayBePending(pending, GameTitle::Halo3, 100),
            "a future availability-set epoch cannot grant pending ownership");

        auto soleModulePending = pending;
        soleModulePending.nowMs = kEpoch;
        soleModulePending.availabilityMask = TitleRuntimeAvailabilityBit(GameTitle::Halo3);
        auto teardownPending = pending;
        teardownPending.nowMs = kEpoch;
        teardownPending.titles[0].teardownRequested = true;
        auto uninstalledPending = pending;
        uninstalledPending.nowMs = kEpoch;
        uninstalledPending.titles[0].installed = false;
        auto duplicatePending = pending;
        duplicatePending.nowMs = kEpoch;
        duplicatePending.titles[TitleRuntimeSlotIndex(GameTitle::HaloReach)] = duplicatePending.titles[0];
        auto multiplePending = multiple;
        multiplePending.nowMs = kEpoch + 2;
        Check(!TitleRuntimeOwnershipMayBePending(soleModulePending, GameTitle::Halo3, 100) &&
                  !TitleRuntimeOwnershipMayBePending(teardownPending, GameTitle::Halo3, 100) &&
                  !TitleRuntimeOwnershipMayBePending(uninstalledPending, GameTitle::Halo3, 100) &&
                  !TitleRuntimeOwnershipMayBePending(duplicatePending, GameTitle::Halo3, 100),
            "sole-title, teardown, uninstalled, and duplicate retained claims get no grace");
        Check(!TitleRuntimeOwnershipMayBePending(multiplePending, GameTitle::Halo3, 100),
            "multiple fresh owners fail closed immediately without pending grace");
    }

    CheckUnit(42);
    {
        constexpr size_t kHalo3Slot = TitleRuntimeSlotIndex(GameTitle::Halo3);
        constexpr size_t kOdstSlot = TitleRuntimeSlotIndex(GameTitle::Halo3ODST);
        constexpr size_t kReachSlot = TitleRuntimeSlotIndex(GameTitle::HaloReach);
        constexpr uintptr_t kHalo3Base = 0x100000;
        constexpr uintptr_t kHalo3ReboundBase = 0x110000;
        constexpr uintptr_t kOdstBase = 0x200000;

        TitleRuntimeState state;
        const TitleRuntimeAvailabilitySnapshot initial = state.LoadAvailability();
        Check(initial.stable && initial.availabilityMask == 0 && initial.availabilitySetEpochMs == 0 &&
                  state.Generation(GameTitle::Halo3) == 0 && state.Generation(GameTitle::None) == 0,
            "title-runtime atomic state starts stable, unavailable, and generation zero");

        TitleRuntimeModuleSet modules{};
        modules.availabilityMask = TitleRuntimeAvailabilityBit(GameTitle::Halo3);
        modules.moduleBases[kHalo3Slot] = kHalo3Base;
        Check(state.PublishModuleSet(modules, 100) && state.Generation(GameTitle::Halo3) == 1,
            "the first Halo 3 module load creates generation one");
        TitleRuntimeAvailabilitySnapshot availability = state.LoadAvailability();
        Check(availability.stable && availability.availabilityMask == modules.availabilityMask &&
                  availability.availabilitySetEpochMs == 100 && availability.moduleBases[kHalo3Slot] == kHalo3Base,
            "the first available set publishes its exact base and epoch");

        Check(state.PublishModuleSet(modules, 150) && state.Generation(GameTitle::Halo3) == 1 &&
                  state.LoadAvailability().availabilitySetEpochMs == 100,
            "an unchanged module poll preserves generation and set epoch");

        modules.availabilityMask |= TitleRuntimeAvailabilityBit(GameTitle::Halo3ODST);
        modules.moduleBases[kOdstSlot] = kOdstBase;
        Check(state.PublishModuleSet(modules, 200) && state.Generation(GameTitle::Halo3) == 1 &&
                  state.Generation(GameTitle::Halo3ODST) == 1 && state.LoadAvailability().availabilitySetEpochMs == 200,
            "adding ODST advances the set epoch without changing resident Halo 3 generation");

        const TitleRuntimeLifecycle odstLifecycle{true, false, false, TitleCapability_ControllerInput};
        const uint32_t odstGeneration = state.Generation(GameTitle::Halo3ODST);
        Check(state.PublishLifecycle(GameTitle::Halo3ODST, odstGeneration, odstLifecycle) &&
                  state.PublishHeartbeat(GameTitle::Halo3ODST, odstGeneration, 201),
            "resident ODST establishes a pre-rebind heartbeat");

        modules.moduleBases[kHalo3Slot] = kHalo3ReboundBase;
        Check(state.PublishModuleSet(modules, 250) && state.Generation(GameTitle::Halo3) == 2 &&
                  state.Generation(GameTitle::Halo3ODST) == 1 && state.LoadAvailability().availabilitySetEpochMs == 250,
            "a Halo 3 base change advances its generation and the complete-set epoch");
        TitleRuntimeHeartbeatPolicy baseChangePolicy{};
        baseChangePolicy.freshForMs[kOdstSlot] = 500;
        Check(state.Resolve(250, baseChangePolicy).owner == GameTitle::None,
            "a pre-rebind heartbeat from another resident title cannot survive the complete-set epoch");

        modules.availabilityMask &= ~TitleRuntimeAvailabilityBit(GameTitle::Halo3ODST);
        modules.moduleBases[kOdstSlot] = 0;
        Check(state.PublishModuleSet(modules, 300) && state.Generation(GameTitle::Halo3ODST) == 2 &&
                  state.LoadAvailability().availabilitySetEpochMs == 300,
            "ODST removal invalidates its load generation and advances the set epoch");
        modules.availabilityMask |= TitleRuntimeAvailabilityBit(GameTitle::Halo3ODST);
        modules.moduleBases[kOdstSlot] = kOdstBase;
        Check(state.PublishModuleSet(modules, 400) && state.Generation(GameTitle::Halo3ODST) == 3 &&
                  state.LoadAvailability().availabilitySetEpochMs == 400,
            "ODST reload receives generation three rather than reusing stale state");

        const uint32_t h3GenerationBeforeInvalid = state.Generation(GameTitle::Halo3);
        const uint32_t odstGenerationBeforeInvalid = state.Generation(GameTitle::Halo3ODST);
        auto unknownBitSet = modules;
        unknownBitSet.availabilityMask |= 0x80000000u;
        auto inconsistentSet = modules;
        inconsistentSet.moduleBases[kReachSlot] = 0x300000;
        Check(!state.PublishModuleSet(unknownBitSet, 450) && !state.PublishModuleSet(inconsistentSet, 450) &&
                  state.Generation(GameTitle::Halo3) == h3GenerationBeforeInvalid &&
                  state.Generation(GameTitle::Halo3ODST) == odstGenerationBeforeInvalid,
            "unknown availability bits and mask/base disagreement are rejected atomically");

        modules.availabilityMask &= ~TitleRuntimeAvailabilityBit(GameTitle::Halo3ODST);
        modules.moduleBases[kOdstSlot] = 0;
        Check(state.PublishModuleSet(modules, 350) && state.Generation(GameTitle::Halo3ODST) == 4 &&
                  state.LoadAvailability().availabilitySetEpochMs == 400,
            "a non-monotonic module observation cannot move the set epoch backward");
    }

    CheckUnit(43);
    {
        constexpr size_t kHalo3Slot = TitleRuntimeSlotIndex(GameTitle::Halo3);
        constexpr size_t kOdstSlot = TitleRuntimeSlotIndex(GameTitle::Halo3ODST);
        constexpr uint64_t kFreshFor = 500;
        constexpr uint32_t kUnknownCapability = 0x80000000u;
        constexpr uint32_t kCapabilities = TitleCapability_Stereo | TitleCapability_ControllerAim |
                                           TitleCapability_ControllerInput | TitleCapability_Haptics;

        TitleRuntimeState state;
        TitleRuntimeModuleSet modules{};
        modules.availabilityMask = TitleRuntimeAvailabilityBit(GameTitle::Halo3);
        modules.moduleBases[kHalo3Slot] = 0x100000;
        Check(state.PublishModuleSet(modules, 100), "atomic publication test establishes a Halo 3 module generation");
        const uint32_t generation1 = state.Generation(GameTitle::Halo3);

        Check(!state.PublishMode(GameTitle::Halo3, generation1, RuntimeMode::Gameplay),
            "mode publication requires a current installed lifecycle");
        const TitleRuntimeLifecycle unarmed{true, false, false, kCapabilities | kUnknownCapability};
        Check(state.PublishLifecycle(GameTitle::Halo3, generation1, unarmed) &&
                  state.PublishMode(GameTitle::Halo3, generation1, RuntimeMode::Vehicle) &&
                  !state.PublishMode(GameTitle::Halo3, generation1, static_cast<RuntimeMode>(255)),
            "current lifecycle tags a valid mode and rejects invalid mode values");
        Check(!state.PublishHeartbeat(GameTitle::Halo3, generation1, 100) &&
                  state.PublishHeartbeat(GameTitle::Halo3, generation1, 101) &&
                  !state.PublishHeartbeat(GameTitle::Halo3, generation1, 101) &&
                  !state.PublishHeartbeat(GameTitle::Halo3, generation1, 100),
            "heartbeats must be post-epoch and strictly monotonic within a generation");

        TitleRuntimeCandidate candidate{};
        Check(state.LoadCandidate(GameTitle::Halo3, kFreshFor, candidate) && candidate.title == GameTitle::Halo3 &&
                  candidate.generation == generation1 && candidate.generationStartMs == 100 && candidate.installed &&
                  !candidate.armed && !candidate.teardownRequested && candidate.mode == RuntimeMode::Vehicle &&
                  candidate.heartbeatMs == 101 && candidate.heartbeatFreshForMs == kFreshFor &&
                  candidate.enabledCapabilities == kCapabilities,
            "atomic candidate loads one coherent lifecycle, mode, heartbeat, and sanitized mask");

        TitleRuntimeHeartbeatPolicy policy{};
        policy.freshForMs[kHalo3Slot] = kFreshFor;
        TitleRuntimeSnapshot snapshot = state.Resolve(101, policy);
        Check(snapshot.owner == GameTitle::Halo3 && snapshot.generation == generation1 && !snapshot.armed &&
                  snapshot.mode == RuntimeMode::Vehicle && snapshot.enabledCapabilities == kCapabilities,
            "the production atomic resolver preserves an unarmed unique owner");

        modules.availabilityMask |= TitleRuntimeAvailabilityBit(GameTitle::Halo3ODST);
        modules.moduleBases[kOdstSlot] = 0x200000;
        Check(state.PublishModuleSet(modules, 200) && state.Resolve(200, policy).owner == GameTitle::None &&
                  !state.PublishHeartbeat(GameTitle::Halo3, generation1, 200) &&
                  state.PublishHeartbeat(GameTitle::Halo3, generation1, 201) &&
                  state.Resolve(201, policy).owner == GameTitle::Halo3,
            "a set transition requires a strictly newer heartbeat from a resident generation");

        modules.moduleBases[kHalo3Slot] = 0x110000;
        Check(state.PublishModuleSet(modules, 250), "a rebound Halo 3 base starts a new atomic generation");
        const uint32_t generation2 = state.Generation(GameTitle::Halo3);
        Check(generation2 == 2 && !state.PublishLifecycle(GameTitle::Halo3, generation1, unarmed) &&
                  !state.PublishMode(GameTitle::Halo3, generation1, RuntimeMode::Paused) &&
                  !state.PublishHeartbeat(GameTitle::Halo3, generation1, 251) &&
                  !state.ClearHeartbeat(GameTitle::Halo3, generation1),
            "stale lifecycle, mode, heartbeat, and clear publications are rejected");
        Check(!state.PublishLifecycle(GameTitle::Halo3ODST, generation2, unarmed) &&
                  !state.PublishMode(GameTitle::Halo3ODST, generation2, RuntimeMode::Gameplay) &&
                  !state.PublishHeartbeat(GameTitle::Halo3ODST, generation2, 251) &&
                  !state.ClearHeartbeat(GameTitle::Halo3ODST, generation2),
            "a generation token from another title cannot publish foreign state");

        Check(state.LoadCandidate(GameTitle::Halo3, kFreshFor, candidate) && candidate.generation == generation2 &&
                  candidate.generationStartMs == 250 && !candidate.installed && !candidate.armed &&
                  !candidate.teardownRequested && candidate.mode == RuntimeMode::Shell && candidate.heartbeatMs == 0 &&
                  candidate.enabledCapabilities == TitleCapability_None,
            "a new module generation cannot inherit prior lifecycle, mode, heartbeat, or capabilities");
        Check(!state.PublishMode(GameTitle::Halo3, generation2, RuntimeMode::Gameplay) &&
                  state.PublishLifecycle(GameTitle::Halo3, generation2, unarmed) &&
                  state.PublishMode(GameTitle::Halo3, generation2, RuntimeMode::Vehicle) &&
                  !state.PublishHeartbeat(GameTitle::Halo3, generation2, 250) &&
                  state.PublishHeartbeat(GameTitle::Halo3, generation2, 251),
            "the new generation admits mode and heartbeat only after its lifecycle and boundary");
        snapshot = state.Resolve(251, policy);
        Check(snapshot.owner == GameTitle::Halo3 && snapshot.generation == generation2 &&
                  snapshot.mode == RuntimeMode::Vehicle,
            "current generation publications restore one coherent owner");
        Check(!state.ClearHeartbeat(GameTitle::Halo3ODST, generation2) &&
                  state.Resolve(251, policy).owner == GameTitle::Halo3 &&
                  state.ClearHeartbeat(GameTitle::Halo3, generation2) &&
                  state.Resolve(251, policy).owner == GameTitle::None &&
                  state.LoadCandidate(GameTitle::Halo3, kFreshFor, candidate) && candidate.heartbeatMs == 0,
            "only the exact title generation can clear a resident heartbeat");

        Check(state.PublishHeartbeat(GameTitle::Halo3, generation2, 252),
            "a cleared current generation can publish a fresh heartbeat again");
        const TitleRuntimeLifecycle teardown{true, true, true, kCapabilities};
        Check(state.PublishLifecycle(GameTitle::Halo3, generation2, teardown) &&
                  !state.PublishMode(GameTitle::Halo3, generation2, RuntimeMode::Paused) &&
                  state.Resolve(252, policy).owner == GameTitle::None,
            "teardown vetoes ownership and further runtime-mode publication");
        const TitleRuntimeLifecycle notInstalled{false, false, false, kCapabilities};
        Check(state.PublishLifecycle(GameTitle::Halo3, generation2, notInstalled) &&
                  state.Resolve(252, policy).owner == GameTitle::None,
            "an uninstalled title cannot own through a retained fresh heartbeat");
        Check(state.PublishLifecycle(GameTitle::Halo3, generation2, unarmed) &&
                  state.PublishMode(GameTitle::Halo3, generation2, RuntimeMode::Gameplay) &&
                  state.Resolve(252, policy).owner == GameTitle::Halo3 &&
                  state.Resolve(252, policy).mode == RuntimeMode::Gameplay,
            "a safe unarmed lifecycle restores its exact generation-tagged mode");

        Check(!state.PublishLifecycle(GameTitle::None, generation2, unarmed) &&
                  !state.PublishMode(GameTitle::Unknown, generation2, RuntimeMode::Gameplay) &&
                  !state.PublishHeartbeat(GameTitle::None, generation2, 253) &&
                  !state.ClearHeartbeat(GameTitle::Unknown, generation2) &&
                  !state.LoadCandidate(GameTitle::None, kFreshFor, candidate),
            "non-module titles cannot enter the atomic runtime state API");
    }

    CheckUnit(44);
    {
        constexpr size_t kReachSlot = TitleRuntimeSlotIndex(GameTitle::HaloReach);
        TitleRuntimeState reachState;
        TitleRuntimeModuleSet modules{};
        modules.availabilityMask = TitleRuntimeAvailabilityBit(GameTitle::HaloReach);
        modules.moduleBases[kReachSlot] = 0x300000;
        Check(reachState.PublishModuleSet(modules, 100), "the generic runtime state can record Reach as available");
        const uint32_t generation = reachState.Generation(GameTitle::HaloReach);
        const TitleRuntimeLifecycle evidenceOnly{true, false, false, TitleCapability_None};
        Check(reachState.PublishLifecycle(GameTitle::HaloReach, generation, evidenceOnly) &&
                  reachState.PublishMode(GameTitle::HaloReach, generation, RuntimeMode::Unsupported) &&
                  reachState.PublishHeartbeat(GameTitle::HaloReach, generation, 101),
            "a synthetic Reach observation is generation tagged without enabling behavior");
        TitleRuntimeHeartbeatPolicy policy{};
        policy.freshForMs[kReachSlot] = 500;
        const TitleRuntimeSnapshot reachSnapshot = reachState.Resolve(101, policy);
        Check(reachSnapshot.owner == GameTitle::HaloReach && reachSnapshot.generation == generation &&
                  !reachSnapshot.armed && reachSnapshot.mode == RuntimeMode::Unsupported &&
                  reachSnapshot.enabledCapabilities == TitleCapability_None &&
                  TitleRuntimeMaskUnarmedCapabilities(reachSnapshot, kTitleRuntimeKnownCapabilities) ==
                      TitleCapability_None,
            "the shared foundation manufactures zero Reach capabilities");
    }

    CheckUnit(45);
    {
        Check(!OdstCameraOnlyScopeRequired(false, true, false),
            "a public build never claims the private ODST camera core");
        Check(OdstCameraOnlyScopeRequired(true, true, false),
            "the adapter transition remains camera-only after module polling");
        Check(OdstCameraOnlyScopeRequired(false, false, true),
            "owned teardown state remains isolated until presentation detaches");
        Check(OdstManualArmEligible(true, true, true, false),
            "stable camera plus explicit head/stereo toggles permits manual arm");
        Check(!OdstManualArmEligible(false, true, true, false),
            "manual ODST arm still requires the fresh-camera debounce");
        Check(!OdstManualArmEligible(true, true, true, true), "teardown always vetoes manual ODST arm");
        OdstHalo3LookAngles look{};
        Check(ComputeOdstHalo3LookAngles(1.0f, 0.25f, 0.5f, 0.2f, 0.4f, -1.0f, 1.0f, 0.1f, look),
            "ODST Halo 3 look ownership accepts finite tracked angles");
        Check(std::fabs(look.yaw - 0.75f) < 1e-5f && std::fabs(look.pitch - 0.3f) < 1e-5f &&
                  std::fabs(look.roll - 0.4f) < 1e-5f,
            "ODST look matches Halo 3 recentered-yaw and absolute pitch/roll");
        Check(ComputeOdstHalo3LookAngles(0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 1.0f, 1.0f, 0.0f, look) &&
                  std::fabs(look.pitch - 1.5f) < 1e-5f,
            "ODST Halo 3 look retains the proven pitch safety clamp");
        Check(OdstVrOwnsLookStick(true, true), "tracked private ODST consumes the stock look-stick axes");
        Check(!OdstVrOwnsLookStick(true, false), "ODST menus retain ordinary look-stick input before head tracking");
        Check(!OdstVrOwnsLookStick(false, true), "the ODST input rule cannot affect Halo 3 or public title paths");
        Check(OdstMotionAimEligible(true, true, true, false),
            "owned+armed+tracked private ODST permits the narrow motion-aim gate");
        Check(!OdstMotionAimEligible(false, true, true, false),
            "ODST motion aim requires the camera-only context (never a public title)");
        Check(!OdstMotionAimEligible(true, false, true, false),
            "ODST motion aim stays closed until the camera hooks are armed");
        Check(!OdstMotionAimEligible(true, true, false, false), "ODST motion aim follows head tracking off");
        Check(!OdstMotionAimEligible(true, true, true, true), "teardown always vetoes ODST motion aim");
        Check(OdstFirstPersonControlBlend(1.0f) && OdstFirstPersonControlBlend(0.998f),
            "on-foot and near-1 camera transitions retain the FP render classification");
        Check(!OdstFirstPersonControlBlend(0.0f), "blend-0 cameras stay outside the FP-only render classification");
        Check(OdstShouldStereoRedirect(true, true, true, true), "a proven slot-0 camera is stereo-redirected");
        Check(!OdstShouldStereoRedirect(true, true, true, false), "a non-redirectable custom camera renders stock");
        Check(!OdstShouldStereoRedirect(false, true, true, true), "a foreign camera slot is never stereo-redirected");
        Check(
            !OdstShouldStereoRedirect(true, false, true, true), "a broken single-user tail is never stereo-redirected");
        Check(!OdstShouldStereoRedirect(true, true, false, true),
            "a mismatched nested FP source is never stereo-redirected");
        Check(OdstCamCopyRequestsTeardown(true, true, false),
            "a broken slot-0 single-user tail tears down (level unload/transition)");
        Check(!OdstCamCopyRequestsTeardown(true, true, true),
            "an active non-FP camera with a valid tail never tears down (3D recovers)");
        Check(!OdstCamCopyRequestsTeardown(false, true, false), "camera-copy teardown requires the core to be armed");
        Check(!OdstCamCopyRequestsTeardown(true, false, false),
            "camera-copy teardown only fires for our own primary slot");
        Check(PausePresentationInputAllowed(true), "proven Halo 3 gameplay may control pause presentation");
        Check(!PausePresentationInputAllowed(false),
            "MCC shell and private ODST Start edges cannot head-lock presentation");
        Check(ReachShouldSwapLeftHandActions(true, false) && !ReachShouldSwapLeftHandActions(true, true) &&
                  !ReachShouldSwapLeftHandActions(false, false) && !ReachShouldSwapLeftHandActions(false, true),
            "Reach swaps LT/X only on foot and restores the native layout in every proven vehicle");
        Check(PauseToggleInputAllowed(true, false), "Halo 3 ownership admits the Y+B pause fallback");
        Check(PauseToggleInputAllowed(false, true), "ODST, Reach, or Halo 4 ownership admits the Y+B pause fallback");
        Check(!PauseToggleInputAllowed(false, false), "Y+B cannot inject Start without supported title ownership");
        Check(TitleSpecificPauseToggleOwner(GameTitle::Unknown, true) &&
                  TitleSpecificPauseToggleOwner(GameTitle::HaloReach, false) &&
                  TitleSpecificPauseToggleOwner(GameTitle::Halo4, false) &&
                  TitleSpecificPauseToggleOwner(GameTitle::Halo2, false) &&
                  TitleSpecificPauseToggleOwner(GameTitle::HaloCE, false) &&
                  !TitleSpecificPauseToggleOwner(GameTitle::Halo3, false) &&
                  !TitleSpecificPauseToggleOwner(GameTitle::Unknown, false),
            "the title-specific Y+B admission includes Halo 4 without broadening unsupported ownership");
        Check(OdstMustClearForeignPause(true, true, false) && OdstMustClearForeignPause(true, false, true),
            "private ODST entry clears either pending or active foreign pause state");
        Check(!OdstMustClearForeignPause(false, true, true),
            "foreign pause cleanup cannot affect non-ODST title ownership");
        Check(
            Halo2MustClearForeignPause(true, true, true, false) && Halo2MustClearForeignPause(true, true, false, true),
            "H2 title entry clears either pending or active inherited pause state");
        Check(!Halo2MustClearForeignPause(true, false, true, true) &&
                  !Halo2MustClearForeignPause(false, true, true, true),
            "H2 preserves its own Y+B pause after entry and leaves foreign titles alone");
        Check(Halo2ShouldRequestForeignPauseClear(true, true, false, true, false) &&
                  !Halo2ShouldRequestForeignPauseClear(true, true, false, true, true) &&
                  !Halo2ShouldRequestForeignPauseClear(true, false, true, true, false),
            "H2 requests one inherited-pause clear only on title entry");
        Check(Halo2PausePresentationOwnsStockScreen(true, true) &&
                  !Halo2PausePresentationOwnsStockScreen(true, false) &&
                  !Halo2PausePresentationOwnsStockScreen(false, true),
            "C-H2-72 gives the shared stock-screen path only to an active "
            "H2 displayed pause presentation");
        Halo2PauseResumeClock pauseClock;
        Check(!pauseClock.Update(true, true, true, true, 100) && !pauseClock.Update(true, true, true, true, 100) &&
                  !pauseClock.Update(true, true, true, true, 100) && !pauseClock.Update(true, true, true, true, 100) &&
                  !pauseClock.Update(true, true, true, true, 100),
            "C-H2-73 requires a proven frozen H2 clock before resume");
        Check(!pauseClock.Update(true, true, true, true, 101), "C-H2-73 requires two distinct resumed H2 ticks");
        Check(
            pauseClock.Update(true, true, true, true, 102), "C-H2-73 restores stereo after two native H2 ticks resume");

        {
            Halo2PauseEntryClock entry;
            Check(!entry.Update(1000, true, false, true, true, 50),
                "H2 Start pause: nothing happens without a Menu/Start press");
            entry.NoteIntent(1000);
            uint64_t t = 1000;
            uint32_t tick = 50;
            bool entered = false;
            for (int i = 0; i < 6; ++i)
            {
                t += 11;
                tick += (i % 3 == 0);
                entered |= entry.Update(t, true, false, true, true, tick);
            }
            Check(!entered, "H2 Start pause: a running 30 Hz clock never enters");
            for (int i = 0; i < 20 && !entered; ++i)
            {
                t += 11;
                entered = entry.Update(t, true, false, true, true, tick);
            }
            Check(entered && !entry.Pending(),
                "H2 Start pause: a clock frozen after Start enters VR pause presentation once");
            Halo2PauseEntryClock late;
            late.NoteIntent(1000);
            Check(!late.Update(3500, true, false, true, true, 9) && !late.Pending(),
                "H2 Start pause: a press older than the window expires");
            Halo2PauseEntryClock owned;
            owned.NoteIntent(1000);
            Check(!owned.Update(1010, true, true, true, true, 9) && !owned.Pending(),
                "H2 Start pause: already in pause presentation, the press is dropped");
        }
        Halo2PauseResumeClock menuClock;
        menuClock.Update(true, true, true, true, 200);
        menuClock.Update(true, true, true, true, 200);
        menuClock.Update(true, true, true, true, 200);
        Check(!menuClock.Update(true, true, true, true, 201) && !menuClock.Update(true, true, true, true, 202),
            "C-H2-73 cannot infer Resume from a moving clock that never froze");

        Halo2PauseResumeClock invalidClock;
        invalidClock.Update(true, true, true, true, 300);
        invalidClock.Update(true, true, true, true, 300);
        invalidClock.Update(true, true, true, true, 300);
        invalidClock.Update(true, true, true, true, 300);
        invalidClock.Update(true, true, true, true, 300);
        Check(!invalidClock.Update(true, true, false, true, 300) && !invalidClock.Update(true, true, true, true, 301) &&
                  !invalidClock.Update(true, true, true, true, 302),
            "C-H2-73 invalid H2 game-time samples reset pause evidence");
        Check(!Halo2RefreshCadenceSupported(0.0f) && !Halo2RefreshCadenceSupported(45.0f) &&
                  !Halo2RefreshCadenceSupported(60.0f) &&
                  !Halo2RefreshCadenceSupported(std::numeric_limits<float>::infinity()) &&
                  !Halo2RefreshCadenceSupported(std::numeric_limits<float>::quiet_NaN()),
            "H2 rejects unknown, non-finite, 45 Hz, and 60 Hz app cadence");
        Check(Halo2RefreshCadenceSupported(72.0f) && Halo2RefreshCadenceSupported(80.0f) &&
                  Halo2RefreshCadenceSupported(90.0f) && Halo2RefreshCadenceSupported(120.0f) &&
                  Halo2RefreshCadenceSupported(144.0f),
            "H2 admits the requested 72/80/90/120/144 Hz app cadences");
        Check(!Halo2RefreshCadenceSupported(71.5f) && !Halo2RefreshCadenceSupported(144.5f) &&
                  !Halo2RefreshCadenceSupported(71.999f) && !Halo2RefreshCadenceSupported(144.001f),
            "H2 float cadence admission is strictly inclusive 72-144 Hz");
        Check(Halo2PreparedCadenceSupported(13888889, 13888889) && Halo2PreparedCadenceSupported(12500000, 12500000) &&
                  Halo2PreparedCadenceSupported(11111111, 11111111) &&
                  Halo2PreparedCadenceSupported(8333333, 8333333) && Halo2PreparedCadenceSupported(6944444, 6944444),
            "H2 exact durations admit nominal 72/80/90/120/144 Hz");
        // The cadence is advisory (liveness only): a 45/60/30 Hz delivery or
        // a throttled 30 Hz target still renders its true per-eye pair.
        Check(Halo2PreparedCadenceSupported(11111111, 22222222) && Halo2PreparedCadenceSupported(11111111, 16666667) &&
                  Halo2PreparedCadenceSupported(33333333, 35842294) && Halo2PreparedCadenceSupported(11111111, 4069000),
            "H2 admits slow, throttled and early delivery: a slow frame still "
            "gets its pair");
        Check(!Halo2PreparedCadenceSupported(0, 11111111) && Halo2PreparedCadenceSupported(11111111, 0) &&
                  Halo2PreparedCadenceSupported(11111111, 1) && !Halo2CadencePeriodSupported(250000001) &&
                  !Halo2CadencePeriodSupported(999999),
            "H2 rejects an unknown target period and periods outside the "
            "4-1000 Hz liveness band; the predicted-display delta is not a "
            "gate");
        Check(!Halo2PresentationMayClaim(true, true, true, true, false, 11111111, 11111111) &&
                  !Halo2PresentationMayClaim(true, true, true, false, true, 11111111, 11111111),
            "H2 cannot claim while either half of pause clearing is active");
        Check(Halo2PresentationMayClaim(true, true, true, false, false, 11111111, 11111111),
            "H2 may claim only after its core and immersive presentation are ready");
        Check(!Halo2PresentationMayClaim(false, true, true, false, false, 11111111, 11111111) &&
                  !Halo2PresentationMayClaim(true, false, true, false, false, 11111111, 11111111) &&
                  !Halo2PresentationMayClaim(true, true, false, false, false, 11111111, 11111111) &&
                  !Halo2PresentationMayClaim(true, true, true, false, false, 0, 11111111),
            "H2 cannot claim outside its intended usable stereo context");
        Check(Halo2PreparedSerialMayFollowCompletedPair(0, 400) &&
                  Halo2PreparedSerialMayFollowCompletedPair(400, 401) &&
                  Halo2PreparedSerialMayFollowCompletedPair(400, 402) &&
                  Halo2PreparedSerialMayFollowCompletedPair(7031, 7038),
            "H2 preserves loading before its first pair, then admits any serial "
            "that moves forward - the headset advances prepared serials at its "
            "own rate, so a game rendering slower than the panel skips them "
            "normally and a gap is not half-rate stereo");
        Check(!Halo2PreparedSerialMayFollowCompletedPair(400, 400) &&
                  !Halo2PreparedSerialMayFollowCompletedPair(400, 399) &&
                  !Halo2PreparedSerialMayFollowCompletedPair(400, 0) &&
                  !Halo2PreparedSerialMayFollowCompletedPair(UINT64_MAX, 1),
            "H2 still rejects a repeated, backwards, missing, or wrapped serial "
            "after the first Complete, because those are what a stale or reused "
            "eye actually looks like");
        Check(!Halo2StructuralFailureRequiresQuarantine(0, false, false, false, false),
            "A clean zero-eye H2 failure retains its one stock replay");
        Check(Halo2StructuralFailureRequiresQuarantine(1, false, false, false, false) &&
                  Halo2StructuralFailureRequiresQuarantine(0, true, false, false, false) &&
                  Halo2StructuralFailureRequiresQuarantine(0, false, true, false, false) &&
                  Halo2StructuralFailureRequiresQuarantine(0, false, false, true, false) &&
                  Halo2StructuralFailureRequiresQuarantine(0, false, false, false, true),
            "Any eye render, failed owned-state restore/raster close, exception, "
            "or exact-call shape violation quarantines the H2 generation");
        Check(OdstNestedSourceIsCompatible(0, 0x1234),
            "ODST installation may precede the first nested FP source publish");
        Check(OdstNestedSourceIsCompatible(0x1234, 0x1234), "ODST accepts the proven nested FP source pointer");
        Check(!OdstNestedSourceIsCompatible(0x5678, 0x1234), "ODST rejects a nested FP source owned by another camera");
        Check(OdstInactiveCameraSlotsAreSafe(false, false, false),
            "constructed but inactive ODST split-screen slots are safe");
        Check(!OdstInactiveCameraSlotsAreSafe(false, true, false),
            "another active ODST camera blocks the single-user bring-up");
        Check(EvaluateOdstStereoFrame(false) == OdstStereoFrameAction::RenderStockWithoutCapture,
            "OpenXR no-render frames preserve ODST hooks without eye validation");
        Check(EvaluateOdstStereoFrame(true) == OdstStereoFrameAction::RenderStereoAndValidate,
            "active OpenXR frames still require validated stereo eye redirects");

        OdstHalo3FovMatch matchedFov{};
        Check(ComputeOdstHalo3FovMatch(std::atan(1.8418f), std::atan(1.3290f), matchedFov),
            "ODST accepts the live Halo 3 headset FOV pair");
        Check(std::fabs(matchedFov.compactVerticalInput - 1.8418f) < 0.0001f &&
                  std::fabs(matchedFov.compactReferenceInput - 1.3290f) < 0.0001f,
            "ODST feeds both compact FOV inputs from Halo 3's matched pair");
        Check(std::fabs(matchedFov.projectionX - 0.54295f) < 0.0001f &&
                  std::fabs(matchedFov.projectionY - 0.75244f) < 0.0001f,
            "ODST reproduces the live Halo 3 projection scales");
        Check(!ComputeOdstHalo3FovMatch(0.0f, 0.9f, matchedFov), "ODST rejects an invalid headset FOV pair");

        for (int count : {41, 42, 43, 45, 46})
        {
            OdstFpSkeletonLayout fp{};
            Check(ComputeOdstFpSkeletonLayout(count, fp), "official ODST FP combined-skeleton counts are accepted");
            Check(fp.rightShoulder == 2 && fp.rightElbow == 4 && fp.rightWrist == 6 && fp.leftShoulder == 1 &&
                      fp.leftElbow == 3 && fp.leftWrist == 5,
                "ODST uses its editing-kit-proven arm chains");
            Check(fp.cameraControl == count - 1, "ODST camera_control is the final combined node");
            Check((fp.leftHandDescendants & (uint64_t{1} << 5)) != 0 &&
                      (fp.leftHandDescendants & (uint64_t{1} << 6)) == 0,
                "ODST left-hand mask owns only the authored left subtree");
            Check((fp.rightHandAndWeaponDescendants & (uint64_t{1} << 6)) != 0 &&
                      (fp.rightHandAndWeaponDescendants & (uint64_t{1} << 37)) != 0 &&
                      (fp.rightHandAndWeaponDescendants & (uint64_t{1} << (fp.cameraControl - 1))) != 0 &&
                      (fp.rightHandAndWeaponDescendants & (uint64_t{1} << fp.cameraControl)) == 0,
                "ODST right carrier owns weapon nodes but excludes camera_control");
        }
        OdstFpSkeletonLayout invalidFp{};
        Check(!ComputeOdstFpSkeletonLayout(38, invalidFp) && !ComputeOdstFpSkeletonLayout(65, invalidFp),
            "ODST rejects unsafe or structurally incomplete FP skeletons");

        Check(TitleRegistry_AllowsSharedGameplayFeatures(GameTitle::None, false, false),
            "the MCC shell retains shared controller behavior");
        Check(TitleRegistry_AllowsSharedGameplayFeatures(GameTitle::Halo3, false, false),
            "an explicitly detected Halo 3 session retains shared features");
        Check(TitleRegistry_AllowsSharedGameplayFeatures(GameTitle::Unknown, true, false),
            "a fresh Halo 3 camera heartbeat resolves resident-module ambiguity");
        Check(!TitleRegistry_AllowsSharedGameplayFeatures(GameTitle::Halo3ODST, false, false),
            "public ODST XInput and presentation remain pass-through");
        Check(!TitleRegistry_AllowsSharedGameplayFeatures(GameTitle::Halo3ODST, true, false),
            "an explicit unsupported title beats a stale Halo 3 heartbeat");
        Check(!TitleRegistry_AllowsSharedGameplayFeatures(GameTitle::Unknown, false, false),
            "an ambiguous title without camera ownership fails closed");
        Check(TitleRegistry_AllowsSharedControllerInput(GameTitle::Unknown, false, false, true, false),
            "the private frontend exception retains controller input");
        Check(TitleRegistry_AllowsSharedControllerInput(GameTitle::Unknown, true, false, false, false),
            "a resolved owner's controller capability admits ambiguous input");
        Check(!TitleRegistry_AllowsSharedControllerInput(GameTitle::Unknown, false, false, false, false),
            "the normal build keeps ambiguous title input fail-closed");
        Check(TitleRegistry_AllowsSharedControllerInput(GameTitle::None, false, false, false, false),
            "the unambiguous MCC shell retains ordinary controller input");
        Check(TitleRegistry_AllowsSharedControllerInput(GameTitle::Halo3, false, false, false, true),
            "explicit Halo 3 retains ordinary controller input");
        Check(TitleRegistry_AllowsSharedControllerInput(GameTitle::Halo3ODST, false, true, false, true),
            "private ODST camera ownership permits ordinary gamepad input");
        Check(TitleRegistry_AllowsSharedControllerInput(GameTitle::Halo3ODST, false, false, false, true),
            "private ODST admits controller input before camera ownership");
        Check(!TitleRegistry_AllowsSharedControllerInput(GameTitle::Halo3ODST, false, true, false, false),
            "public ODST camera ownership keeps controller input stock");
        Check(!TitleRegistry_AllowsSharedControllerInput(GameTitle::HaloCE, false, false, true, false),
            "an explicit title without controller admission keeps stock input");
        Check(!TitleRegistry_AllowsSharedControllerInput(GameTitle::Unknown, false, true, true, true),
            "owned ODST teardown beats resident-module ambiguity");
        Check(TitleRegistry_AllowsSharedControllerInput(GameTitle::Unknown, true, true, true, true),
            "a unique ambiguous owner may retain ordinary controller input");
        Check(!TitleRegistry_AllowsSharedGameplayFeatures(GameTitle::Halo3, true, true),
            "private camera ownership overrides a stale Halo 3 signal");
        Check(!TitleRegistry_Halo3CameraOwnsAmbiguousState(1050, 999, 1000),
            "an ambiguous title cannot inherit a pre-transition heartbeat");
        Check(!TitleRegistry_Halo3CameraOwnsAmbiguousState(1050, 1000, 1000),
            "a heartbeat at the transition boundary is not new ownership");
        Check(TitleRegistry_Halo3CameraOwnsAmbiguousState(1099, 1001, 1000),
            "a post-transition Halo 3 heartbeat owns a short ambiguous window");
        Check(!TitleRegistry_Halo3CameraOwnsAmbiguousState(1101, 1001, 1000),
            "ambiguous Halo 3 ownership expires at the dedicated 100 ms limit");
        Check(!TitleRegistry_Halo3CameraOwnsAmbiguousState(999, 1001, 1000),
            "a non-monotonic camera timestamp fails closed");

        OdstCameraRearmGate gate;
        Check(gate.CanAttemptInstall(), "ODST camera install begins unblocked");
        gate.BlockUntilReload(true);
        Check(!gate.CanAttemptInstall(), "stale active camera memory cannot immediately rearm ODST hooks");
        gate.Observe(true, true);
        Check(!gate.CanAttemptInstall(), "an active-to-active observation is not a reload edge");
        gate.Observe(true, false);
        Check(!gate.CanAttemptInstall(), "an inactive camera waits for the next active level");
        gate.Observe(true, true);
        Check(gate.CanAttemptInstall(), "inactive-to-active camera transition rearms ODST hooks");
        gate.BlockUntilReload(true);
        gate.Observe(false, false);
        Check(gate.CanAttemptInstall(), "a genuine title exit clears the ODST rearm gate");

        gate.BlockUntilTitleExit();
        gate.Observe(true, false);
        gate.Observe(true, true);
        Check(!gate.CanAttemptInstall(), "unsupported/menu camera transitions cannot rearm in-session");
        gate.Observe(false, false);
        Check(gate.CanAttemptInstall(), "title exit clears the unsupported-camera session latch");

        OdstPauseRearmGate pauseGate;
        const uint64_t pauseStable = kOdstPauseRearmStableMs;
        pauseGate.Block();
        pauseGate.Observe(100, true, true, true);
        Check(!pauseGate.CanAttemptInstall(), "native ODST pause blocks camera-hook reinstallation");
        pauseGate.Observe(200, true, false, true);
        pauseGate.Observe(200 + pauseStable, true, false, true);
        Check(!pauseGate.CanAttemptInstall(), "pause exit requires more than the pause-rearm stable interval");
        pauseGate.Observe(201 + pauseStable, true, false, true);
        Check(pauseGate.CanAttemptInstall(), "stable gameplay after pause exit permits camera-hook reinstall");
        // Flicker tolerance: with the copy hook removed the live camera array
        // reads ready/not-ready frame-to-frame. A momentary not-ready sample
        // must NOT restart the settle window (a continuous-ready reset stalled
        // the post-pause rearm for tens of seconds in the headset log).
        pauseGate.Block();
        pauseGate.Observe(5000, true, false, true);  // pause cleared, camera seen live
        pauseGate.Observe(5100, true, false, false); // momentary not-ready flicker
        pauseGate.Observe(5000 + pauseStable, true, false, false);
        Check(!pauseGate.CanAttemptInstall(), "a not-ready flicker does not release the gate before the window");
        pauseGate.Observe(5001 + pauseStable, true, false, true);
        Check(pauseGate.CanAttemptInstall(), "a momentary flicker no longer restarts the pause-exit window");
        // A genuine re-pause, however, does restart the window from its exit.
        pauseGate.Block();
        pauseGate.Observe(6000, true, false, true); // window starts at 6000
        pauseGate.Observe(6100, true, true, true);  // re-pause before it elapses
        Check(!pauseGate.CanAttemptInstall(), "a re-pause during the settle window blocks reinstall again");
        pauseGate.Observe(6200, true, false, true); // window restarts at 6200
        pauseGate.Observe(6200 + pauseStable, true, false, true);
        Check(!pauseGate.CanAttemptInstall(), "the re-pause restarted the settle window from its exit");
        pauseGate.Observe(6201 + pauseStable, true, false, true);
        Check(pauseGate.CanAttemptInstall(), "a full settle window after the last pause exit clears the gate");
        pauseGate.Block();
        pauseGate.Observe(9000, false, false, false);
        Check(pauseGate.CanAttemptInstall(), "title exit clears the native-pause reinstall gate");

        const HudLayoutAdapter* halo3Hud = HudLayoutAdapterFor(HudLayoutProfile::Halo3);
        const HudLayoutAdapter* odstHud = HudLayoutAdapterFor(HudLayoutProfile::Halo3ODST);
        Check(halo3Hud && halo3Hud->expectedBlocks == 3, "Halo 3 HUD layout retains its three proven skin blocks");
        Check(
            odstHud && odstHud->expectedBlocks == 1, "ODST HUD layout accepts only its uniquely proven globals block");
        const HudLayoutAdapter* reachHud = HudLayoutAdapterFor(HudLayoutProfile::HaloReach);
        // clang-format off
        constexpr uint8_t expectedHalo3HudAnchor[24] = {
            0x00, 0x05, 0x00, 0x00, 0xD0, 0x02, 0x00, 0x00,
            0x00, 0x00, 0x5C, 0x42, 0x00, 0x40, 0x25, 0x44,
            0x00, 0x00, 0x68, 0x42, 0x00, 0x00, 0x80, 0x40,
            // clang-format on
        };
        // clang-format off
        constexpr uint8_t expectedOdstHudAnchor[24] = {
            0x00, 0x05, 0x00, 0x00, 0xD0, 0x02, 0x00, 0x00,
            0x00, 0x00, 0xFA, 0x44, 0x00, 0x00, 0xFA, 0x44,
            0x00, 0x00, 0x68, 0x42, 0x00, 0x00, 0x80, 0x40,
            // clang-format on
        };
        const auto anchorPrefixEquals = [](const HudLayoutAdapter* adapter, const uint8_t* expected, int n)
        {
            if (!adapter || adapter->anchorLength != n)
                return false;
            for (int i = 0; i < n; ++i)
            {
                if (adapter->anchor[i] != expected[i] || adapter->mask[i] != 0xFF)
                    return false;
            }
            return true;
        };
        Check(anchorPrefixEquals(halo3Hud, expectedHalo3HudAnchor, 24),
            "Halo 3 HUD adapter retains its evidence-exact anchor");
        Check(anchorPrefixEquals(odstHud, expectedOdstHudAnchor, 24),
            "ODST HUD adapter retains its evidence-exact 2000/2000 anchor");
        Check(halo3Hud && odstHud && halo3Hud->anchor != odstHud->anchor,
            "ODST cannot reuse Halo 3's title-specific safe-frame anchor");
        // Reach's record is a different shape, proven from HREK's own
        // chud_curvature_info_block postprocess and tag export. Copying Halo 3's
        // offsets would land 36 bytes short, inside the minimap points.
        Check(halo3Hud && odstHud && halo3Hud->safeFrameOffset == 24 && odstHud->safeFrameOffset == 24 &&
                  halo3Hud->depthFromSlot == -28 && odstHud->depthFromSlot == -28,
            "Halo 3 and ODST keep the shared s_chud_curvature_info offsets");
        Check(reachHud && reachHud->expectedBlocks == 3 && reachHud->safeFrameOffset == 60 &&
                  reachHud->anchorLength == 76,
            "Reach HUD adapter uses its own record shape, not Halo 3's");
        Check(reachHud && !HudLayoutHasDepthField(*reachHud),
            "Reach declares no HUD depth field: its curvature is baked at "
            "tag-block load, so writing it would be inert");
        Check(halo3Hud && odstHud && HudLayoutHasDepthField(*halo3Hud) && HudLayoutHasDepthField(*odstHud),
            "Halo 3 and ODST keep their live dest-offset-z depth control");
        Check(reachHud && reachHud->anchor[0] == 0x00 && reachHud->anchor[1] == 0x05 && reachHud->anchor[20] == 0x00 &&
                  reachHud->anchor[21] == 0x00 && reachHud->anchor[22] == 0xA0 && reachHud->anchor[23] == 0x42,
            "Reach compares its own vehicle-3d-sensor-radius field, which "
            "Halo 3's record does not contain at all");
        Check(reachHud && reachHud->mask[12] == 0x00 && reachHud->mask[16] == 0x00 && reachHud->mask[60] == 0x00 &&
                  reachHud->mask[64] == 0x00,
            "Reach wildcards its per-skin sensor values and the safe-frame "
            "pair this feature writes");
        {
            // The authored-origin compare: same hit set on retail bytes, a shifted record only with its authored origin,
            // and a decoy (wrong sensor radius) is rejected either way.
            const HudLayoutAnchorView view = HudLayoutAnchorAt(*reachHud, 0);
            std::array<uint8_t, 80> record{};
            for (int i = 0; i < view.anchorLength; ++i)
                record[i] = view.anchor[i];
            Check(HudLayoutAnchorEquals(record.data(), view), "retail Reach bytes still match unchanged");
            float shifted[2]{88.0f + 40.0f, 624.0f - 30.0f};
            std::array<uint8_t, 80> moved = record;
            memcpy(moved.data() + 8, shifted, 8);
            uint8_t authored[8];
            memcpy(authored, record.data() + 8, 8);
            Check(!HudLayoutAnchorEquals(moved.data(), view), "a shifted origin alone is not accepted");
            Check(HudLayoutAnchorEquals(moved.data(), view, authored),
                "a shifted origin is accepted with its authored value");
            std::array<uint8_t, 80> decoy = moved;
            decoy[20] ^= 0xFF; // sensor radius field: compared
            Check(!HudLayoutAnchorEquals(decoy.data(), view, authored),
                "a decoy record is rejected even with an authored origin");
        }
        Check(HudLayoutAdapterWellFormed(*halo3Hud) && HudLayoutAdapterWellFormed(*odstHud) &&
                  HudLayoutAdapterWellFormed(*reachHud),
            "every HUD layout adapter keeps a fully compared scan prefix");
        // Halo 3 and ODST keep their proven exact cardinality and their proven
        // private-read-write-only search. Only Reach may look wider.
        Check(HudLayoutAcceptedCountOk(*halo3Hud, 3) && !HudLayoutAcceptedCountOk(*halo3Hud, 2) &&
                  !HudLayoutAcceptedCountOk(*halo3Hud, 4),
            "Halo 3 still accepts exactly three layout blocks");
        Check(HudLayoutAcceptedCountOk(*odstHud, 1) && !HudLayoutAcceptedCountOk(*odstHud, 0) &&
                  !HudLayoutAcceptedCountOk(*odstHud, 2),
            "ODST still accepts exactly its one proven globals block");
        Check(HudLayoutAcceptedCountOk(*reachHud, 3) && HudLayoutAcceptedCountOk(*reachHud, 6) &&
                  !HudLayoutAcceptedCountOk(*reachHud, 2) && !HudLayoutAcceptedCountOk(*reachHud, 17),
            "Reach accepts a second identity-verified copy of its record but "
            "never fewer than its three skins");
        Check(!halo3Hud->scanMappedRegions && !odstHud->scanMappedRegions && reachHud->scanMappedRegions,
            "only Reach widens the search beyond private read-write memory");
        Check(!halo3Hud->forceWriteEveryPass && !odstHud->forceWriteEveryPass && reachHud->forceWriteEveryPass,
            "only Reach reasserts every pass, because it overwrites its own "
            "record live and a skip-when-matching write loses that race");
        Check(HudLayoutCanReacquireFromRemembered(1, 1) && HudLayoutCanReacquireFromRemembered(3, 3),
            "exact per-title remembered cardinality permits stock restoration");
        Check(!HudLayoutCanReacquireFromRemembered(0, 1) && !HudLayoutCanReacquireFromRemembered(1, 3) &&
                  !HudLayoutCanReacquireFromRemembered(0, 0),
            "missing, partial, and unproven remembered HUD sets fail closed");
        Check(!HudLayoutAdapterFor(HudLayoutProfile::None), "an unowned title has no writable HUD layout adapter");
        Check(!HudLayoutPublicationMatches(HudLayoutProfile::None, 9, HudLayoutProfile::None, 9),
            "matching generations cannot grant HUD ownership to no title");
        Check(HudLayoutPublicationMatches(HudLayoutProfile::Halo3ODST, 9, HudLayoutProfile::Halo3ODST, 9),
            "HUD scan results publish only to their exact title generation");
        Check(!HudLayoutPublicationMatches(HudLayoutProfile::Halo3ODST, 9, HudLayoutProfile::Halo3, 9) &&
                  !HudLayoutPublicationMatches(HudLayoutProfile::Halo3ODST, 9, HudLayoutProfile::Halo3ODST, 8),
            "foreign-title and stale-generation HUD results are rejected");

        Check(OdstHudLayoutEligible(true, true, true, true, false, false),
            "ODST starts shared HUD layout work on its first eligible camera heartbeat");
        Check(!OdstHudLayoutEligible(false, true, true, true, false, false) &&
                  !OdstHudLayoutEligible(true, false, true, true, false, false) &&
                  !OdstHudLayoutEligible(true, true, false, true, false, false) &&
                  !OdstHudLayoutEligible(true, true, true, false, false, false),
            "ODST HUD layout writes require private title ownership and a fresh camera");
        Check(!OdstHudLayoutEligible(true, true, true, true, true, false) &&
                  !OdstHudLayoutEligible(true, true, true, true, false, true),
            "teardown and native pause veto ODST HUD layout writes");

        OdstFreshCameraDebounce debounce;
        Check(!debounce.Update(100, true), "ODST camera does not arm on its first fresh frame");
        Check(!debounce.Update(1100, true), "ODST camera remains flat at the exact Halo 3 one-second boundary");
        Check(debounce.Update(1101, true), "ODST camera arms on the first millisecond after Halo 3 stability");
        debounce.Reset();
        Check(!debounce.Update(5000, true), "ODST session re-entry resets the stability debounce");

        OdstFreshCameraDebounce interruptedDebounce;
        Check(!interruptedDebounce.Update(2000, true) && !interruptedDebounce.Update(2500, false) &&
                  !interruptedDebounce.Update(3000, true),
            "loss of camera freshness restarts the ODST stability interval");
        Check(!interruptedDebounce.Update(4000, true),
            "the restarted interval remains flat at its exact one-second boundary");
        Check(interruptedDebounce.Update(4001, true), "only a continuous fresh-camera interval arms ODST after reset");

        // ODST's camera tail boolean toggles about ten times a second during
        // ordinary play. Restarting the interval on each toggle meant the
        // camera armed only when it caught a lucky quiet gap, which is why
        // arming was slow and a quick pause/unpause often never re-armed.
        OdstFreshCameraDebounce flickerDebounce;
        Check(!flickerDebounce.Update(1000, true), "the flicker-tolerant debounce still requires a stability interval");
        Check(!flickerDebounce.Update(1100, false) && !flickerDebounce.Update(1200, true) &&
                  !flickerDebounce.Update(1300, false) && !flickerDebounce.Update(1400, true),
            "a 100ms tail toggle does not arm ODST early");
        Check(flickerDebounce.Update(2001, true), "a flickering tail no longer restarts the interval, so ODST arms "
                                                  "one second after the camera first became fresh");

        OdstFreshCameraDebounce sustainedLossDebounce;
        Check(!sustainedLossDebounce.Update(1000, true) && !sustainedLossDebounce.Update(1400, false),
            "a gap beyond the tolerance is a genuine camera loss");
        Check(!sustainedLossDebounce.Update(2001, true), "a genuine loss still restarts the full stability interval");

        Check(EvaluateOdstHeartbeat(1700, 1000, 0, false, false) == OdstHeartbeatAction::None,
            "ODST tolerates a short delay before its first heartbeat");
        Check(EvaluateOdstHeartbeat(1800, 1000, 0, false, false) == OdstHeartbeatAction::LevelUnloaded,
            "an inactive camera after install is treated as an unload");
        Check(EvaluateOdstHeartbeat(6100, 1000, 0, false, true) == OdstHeartbeatAction::NoFirstHeartbeat,
            "active-looking memory cannot retain a hook without a heartbeat");
        Check(EvaluateOdstHeartbeat(7000, 1000, 6200, true, true) == OdstHeartbeatAction::None,
            "a short heartbeat gap with a ready camera is tolerated");
        Check(EvaluateOdstHeartbeat(6701, 1000, 6000, true, false) == OdstHeartbeatAction::None,
            "a transient heartbeat gap does not detach presentation even when camera readiness flickers");
        Check(EvaluateOdstHeartbeat(6751, 1000, 6000, true, false) == OdstHeartbeatAction::LevelUnloaded,
            "an unready camera must exceed the soft timeout before presentation detaches");
        Check(EvaluateOdstHeartbeat(12001, 1000, 7000, true, true) == OdstHeartbeatAction::LevelUnloaded,
            "a hard heartbeat timeout falls back even with stale ready bytes");
    }

    CheckUnit(-1);
    std::cout << "checks run: " << g_checks << '\n';
    if (g_failures == 0)
        std::cout << "HaloMCCVR core title runtime tests passed\n";
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
