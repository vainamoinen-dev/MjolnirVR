// core_halo2_tests.cpp: Halo 2 anchors and cold observation, camera stages, stock-screen delivery, HUD layout and aim assist.
// Units 4, 7, 11, 46, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 99, 100, 242, 243 of the former tests/core_tests.cpp main(), moved unchanged and in
// their order (E3). Built as halomccvr_core_halo2_tests and run by ctest; each unit prints its check count.
#include "core_test_support.h"

int main()
{
    CheckUnit(4);
    {
        const float identity[]{0.f, 0.f, 0.f, 1.f}, zero[3]{};
        auto basis = [](float yaw)
        {
            Halo2CameraBasis b{};
            b.forward[0] = std::cos(yaw);
            b.forward[1] = std::sin(yaw);
            b.up[2] = 1;
            return b;
        };
        Halo2VehicleViewKey key{7, 0x12340002, 0x56780003, 0x56780003, 0, 9};
        // Exercise the view and controller construction while the native
        // actuator itself turns. A moving reference would never converge.
        for (int hz : {60, 90, 120, 144})
            for (float mapping : {-1.f, 1.f})
                for (float hand : {-.5f, .5f})
                {
                    Halo2VehicleViewState viewState;
                    Halo4PitchServo servo;
                    float engine = 0, firstTarget = 0;
                    bool parked = false;
                    const float q[]{0.f, std::sin(hand * .5f), 0.f, std::cos(hand * .5f)};
                    for (int frame = 0; frame < hz * 5; ++frame)
                    {
                        auto native = basis(engine);
                        native.position[0] = frame * .1f;
                        Halo2CameraBasis reference{}, view{}, gun{};
                        Check(viewState.Build(key, native, basis(0), false, 1000 + frame * 1000 / hz, reference),
                            "H2 occupied view reference follows translation without native aim feedback");
                        Halo2TrackedHeadInput head{};
                        head.worldScale = 1;
                        Check(Halo2BuildTrackedCenterCamera(reference, head, view) &&
                                  Halo2BuildStableControllerCarrier(reference, identity, zero, q, zero, 1, 0, gun),
                            "H2 seated view and gun use the same reference");
                        const float target = std::atan2(gun.forward[1], gun.forward[0]);
                        if (!frame)
                            firstTarget = target;
                        Check(std::fabs(target - firstTarget) < 1e-5f && std::fabs(view.forward[1]) < 1e-5f &&
                                  std::fabs(reference.position[0] - native.position[0]) < 1e-5f,
                            "H2 controller target does not chase the turning native camera");
                        const float command = Halo4PitchServoStep(servo, engine, target, kHalo4PitchServoGain);
                        if (command == 0 && servo.axis.resting)
                        {
                            parked = std::fabs(target - engine) < .04f;
                            break;
                        }
                        const float raw =
                            command == 0
                                ? 0
                                : std::copysign((9000.f + std::fabs(command) * (32767.f - 9000.f)) / 32767.f, command);
                        engine += mapping * raw * 3.14159265f / hz;
                    }
                    Check(parked,
                        "H2 held controller converges across frame rates, both turn directions and inverted controls");
                }
        Halo2VehicleViewState state;
        Halo2CameraBasis reference{};
        Check(state.Build(key, basis(.3f), basis(3.1f), true, 1000, reference) && state.followDelta == 0.f,
            "H2 hull-follow entry (no comfort yaw on the first frame)");
        Check(state.Build(key, basis(2.f), basis(-3.1f), true, 1010, reference) &&
                  std::fabs(std::atan2(reference.forward[1], reference.forward[0]) - .3831853f) < 1e-5f,
            "H2 hull follow crosses yaw seam without using cannon heading");
        Check(
            std::fabs(state.followDelta - .0831853f) < 1e-4f, "H2 comfort sees the followed hull yaw across the seam");
        const auto before = reference;
        Check(state.Build(key, basis(2.f), basis(-2.f), false, 1020, reference) &&
                  std::fabs(reference.forward[0] - before.forward[0]) < 1e-5f,
            "H2 follow toggle preserves current view");
        Check(state.followDelta == 0.f, "H2 comfort: no followed yaw once follow is off");
        for (unsigned change = 0; change < 6; ++change)
        {
            auto other = key;
            if (change == 0)
                ++other.generation;
            if (change == 1)
                other.unit += 0x10000;
            if (change == 2)
                other.parent += 0x10000;
            if (change == 3)
                ++other.seat;
            if (change == 4)
                ++other.space;
            if (change == 5)
                other.root += 0x10000;
            Check(state.Build(other, basis(.8f), basis(0), false, 1030 + change, reference) &&
                      std::fabs(std::atan2(reference.forward[1], reference.forward[0]) - .8f) < 1e-5f,
                "H2 occupation/tracking changes discard old vehicle heading");
        }
        Check(state.Build(key, basis(.6f), basis(0), false, 2000, reference) &&
                  std::fabs(std::atan2(reference.forward[1], reference.forward[0]) - .6f) < 1e-5f,
            "H2 stale observer gap reseeds the vehicle reference");
        const auto saved = reference;
        auto bad = basis(0);
        bad.forward[0] = NAN;
        Check(!state.Build(key, bad, basis(0), false, 2010, reference) && !state.active &&
                  std::memcmp(&saved, &reference, sizeof(saved)) == 0,
            "H2 invalid camera leaves output unchanged");
        Halo2ObserverPosePublication publication{};
        publication.stock = basis(.5f);
        publication.vehicleReference = basis(1.f);
        Check(&Halo2ControllerReference(publication) == &publication.stock, "H2 on-foot reference unchanged");
        publication.vehicleReferenceValid = true;
        Check(&Halo2ControllerReference(publication) == &publication.vehicleReference,
            "H2 seated publication supplies shared reference");
    }
    CheckUnit(7);
    {
        std::array<uint8_t, 0x500> memory{};
        auto put = [&](size_t at, auto value) { std::memcpy(memory.data() + at, &value, sizeof(value)); };
        put(0x20, uint32_t(2));
        put(0x24, uint32_t(0x224));
        put(0x48, uintptr_t(0x80));
        memory[0x29] = 1;
        put(0x80 + 0x224, uint16_t(9));
        Check(halo2_datum::Record(memory.data(), 0x90001, 0x224, 16) == memory.data() + 0x2a4,
            "H2 full salted handle resolves its own player record");
        Check(
            !halo2_datum::Record(memory.data(), 0x80001, 0x224, 16), "H2 checkpoint slot reuse rejects the old handle");
        Check(!halo2_datum::Record(memory.data(), 0x90002, 0x224, 16) &&
                  !halo2_datum::Record(memory.data(), 0x90001, 12, 0x2800) &&
                  !halo2_datum::Record(memory.data(), UINT32_MAX, 0x224, 16),
            "H2 wrong table type, capacity and invalid handles refuse");
        memory[0x29] = 0;
        Check(!halo2_datum::Record(memory.data(), 0x90001, 0x224, 16),
            "H2 invalidated level table refuses stale ownership");
        memory[0x29] = 1;
        put(0x48, UINTPTR_MAX);
        Check(!halo2_datum::Record(memory.data(), 0x90001, 0x224, 16),
            "H2 relative datum storage cannot overflow address space");
    }
    CheckUnit(11);
    {
        using namespace contact_melee;
        bool elapsedTime = true, independent = true;
        for (int hz : {60, 72, 90, 120, 144, 240})
            for (float scale : {0.164f, 0.328f, 0.656f})
            {
                Halo2ObserverPosePublication publication{};
                publication.generation = 7;
                publication.serial = 1;
                publication.stock.forward[0] = 1;
                publication.stock.up[2] = 1;
                publication.snapshot.valid = true;
                publication.snapshot.trackingSpaceEpoch = 4;
                publication.snapshot.predictedDisplayTimeNs = 1'000'000'000;
                publication.snapshot.leftControllerValid = true;
                publication.snapshot.independentRightAimValid = true;
                Frame frames[2]{};
                Motion motion[2];
                Sweeps sweeps{};
                for (unsigned hand = 0; hand < 2; ++hand)
                {
                    elapsedTime &=
                        Halo2PreparePhysicalContactFrame(publication, hand, 0x81230009, 5, scale, 11, frames[hand]);
                    frames[hand].count = 1;
                    frames[hand].points[0] = frames[hand].controllerPose.origin;
                    elapsedTime &= motion[hand].Advance(frames[hand], 5, sweeps) == AdvanceResult::Seeded;
                }
                const int64_t dt = 1'000'000'000 / hz;
                publication.serial++;
                publication.snapshot.predictedDisplayTimeNs += dt;
                const float distance = float(double(dt) * 6e-9);
                publication.snapshot.leftControllerPosition[0] = -distance;
                publication.snapshot.rightAimPosition[0] = distance;
                // The actor moved independently. Both ends must use this NEW frame.
                publication.stock.position[0] = 20;
                // A support-grip solver rotates visual aim; use physical right pose.
                publication.snapshot.twoHandAimActive = true;
                publication.snapshot.rightAimOrientation[1] = 0.5f;
                publication.snapshot.rightAimOrientation[3] = 0.8660254f;
                for (unsigned hand = 0; hand < 2; ++hand)
                {
                    elapsedTime &=
                        Halo2PreparePhysicalContactFrame(publication, hand, 0x81230009, 5, scale, 11, frames[hand]);
                    frames[hand].count = 1;
                    frames[hand].points[0] = frames[hand].controllerPose.origin;
                    elapsedTime &= motion[hand].Advance(frames[hand], 5, sweeps) == AdvanceResult::Advanced &&
                                   sweeps.count == 1 && std::abs(sweeps.values[0].speedMetresPerSecond - 6) < 0.001f;
                    independent &= sweeps.count == 1 &&
                                   (hand == 0 ? sweeps.values[0].trackingDirection.x < -.99f
                                              : sweeps.values[0].trackingDirection.x > .99f) &&
                                   std::abs(sweeps.values[0].start.x - 20) < 0.001f &&
                                   frames[hand].controllerPose.axis[0].x == 1;
                    elapsedTime &= motion[hand].Advance(frames[hand], 5, sweeps) == AdvanceResult::Duplicate;
                }
                publication.serial++;
                publication.snapshot.predictedDisplayTimeNs += dt;
                publication.referencePosition[0] = 1;
                elapsedTime &= Halo2PreparePhysicalContactFrame(publication, 1, 0x81230009, 5, scale, 11, frames[1]);
                frames[1].count = 1;
                elapsedTime &= motion[1].Advance(frames[1], 5, sweeps) == AdvanceResult::Seeded;
                publication.snapshot.independentRightAimValid = false;
                elapsedTime &= !Halo2PreparePhysicalContactFrame(publication, 1, 0x81230009, 5, scale, 11, frames[1]) &&
                               motion[1].Advance(frames[1], 5, sweeps) == AdvanceResult::Rejected;
            }
        Check(elapsedTime,
            "H2 physical frames retain elapsed-time speed at 60-240 Hz, deduplicate eyes and reset on recenter/tracking loss");
        Check(independent,
            "H2 physical hands move independently without actor motion or support-hand aim manufacturing velocity");
    }
    CheckUnit(46);
    const TitleDescriptor* halo3 = TitleRegistry_FromModuleName(L"halo3.dll");
    Check(halo3 != nullptr, "Halo 3 module is recognized");
    Check(halo3 && halo3->runtimeSupported, "Halo 3 is the supported baseline adapter");
    Check(halo3 && halo3->admissionCapabilities == TitleCapability_ControllerInput,
        "Halo 3 retains ordinary shared-controller admission");
    Check(halo3 && (halo3->capabilities & TitleCapability_CutsceneTheater) != 0,
        "Halo 3 advertises the proven cutscene-theatre capability");

    const TitleDescriptor* odst = TitleRegistry_FromModuleName(L"N:/MCC/HALO3ODST.DLL");
    Check(odst && odst->title == GameTitle::Halo3ODST, "ODST paths are matched case-insensitively");
    Check(odst && !odst->runtimeSupported, "ODST stays disabled until its adapter passes the title gate");
    Check(odst && odst->capabilities == TitleCapability_None,
        "ODST advertises no public capabilities during private bring-up");
    Check(odst && odst->admissionCapabilities == TitleCapability_ControllerInput,
        "The private preset grants ODST controller admission");

    Check(TitleRegistry_HookPlan(GameTitle::Halo3) == TitleHookPlan::Halo3Full,
        "Halo 3 keeps the full headset-confirmed hook plan");
    Check(TitleRegistry_HookPlan(GameTitle::Halo3ODST) == TitleHookPlan::OdstExperimentalCameraCore,
        "The explicit private build enables only the ODST camera core");

    const TitleDescriptor* reach = TitleRegistry_FromModuleName(L"haloreach.dll");
    Check(reach && reach->title == GameTitle::HaloReach, "Reach module is recognized");
    Check(reach && reach->runtimeSupported, "Reach is a permanent runtime-supported title");
    Check(reach && reach->capabilities ==
                       (TitleCapability_Stereo | TitleCapability_ControllerAim | TitleCapability_ArmIk |
                           TitleCapability_Hud | TitleCapability_RuntimeModes | TitleCapability_RoomScale |
                           TitleCapability_ControllerInput | TitleCapability_Haptics | TitleCapability_CutsceneTheater),
        "Reach advertises controller aim, arm IK, and proven cutscene theatre");
    Check(reach && (reach->capabilities & TitleCapability_Hud) != 0u,
        "Reach advertises native HUD capability now that its layout adapter "
        "locates Reach's own curvature record");
    Check(TitleRegistry_HookPlan(GameTitle::HaloReach) == TitleHookPlan::ReachCameraCore,
        "Reach receives its permanent camera-core hook plan");
    Check(reach && reach->admissionCapabilities == TitleCapability_ControllerInput,
        "The private preset grants Reach only controller admission");
    Check(ReachAdapter_GetStage() == ReachAdapterStage::ControllerInputOnly,
        "The private preset compiles only Reach controller transport");
    Check(!ReachAdapter_RuntimeHooksPermitted(),
        "Neither controller admission nor the hard-off render foundation can install Reach runtime hooks");
    const ReachEvidenceIdentity& reachIdentity = ReachAdapter_GetEvidenceIdentity();
    Check(std::wstring_view(reachIdentity.moduleName) == L"haloreach.dll" &&
              std::string_view(reachIdentity.moduleSha256) ==
                  "738DD2D24EA3AEA12E1EE9AA4A61094BF116027D42004C35A19E5048608B0894" &&
              reachIdentity.peTimestamp == 0x68A0EFE1u && reachIdentity.sizeOfImage == 0x04EDA000u &&
              std::string_view(reachIdentity.hrekBuild) == "2023.07.17.176677.1-QFE1",
        "The Reach adapter pins the independently verified retail and HREK identities");

    ReachHookProof proof{true, 1, true, true, true, true, true, true};
    Check(ReachAdapter_HookProofComplete(proof),
        "A synthetic proof is complete only when every evidence gate is present");
    proof.loadedImageMatchCount = 0;
    Check(!ReachAdapter_HookProofComplete(proof), "A zero-match loaded-image signature fails the Reach proof closed");
    proof.loadedImageMatchCount = 2;
    Check(
        !ReachAdapter_HookProofComplete(proof), "A multiple-match loaded-image signature fails the Reach proof closed");
    proof.loadedImageMatchCount = 1;
    proof.executableRange = false;
    Check(!ReachAdapter_HookProofComplete(proof), "A non-executable candidate range fails the Reach proof closed");
    proof.executableRange = true;
    proof.abi = false;
    Check(!ReachAdapter_HookProofComplete(proof), "An unproven ABI fails the Reach proof closed");
    Check(!TitleRegistry_AllowsSharedGameplayFeatures(GameTitle::HaloReach, true, false),
        "Stale Halo 3 ownership cannot admit Reach gameplay features");
    const bool reachControllerAdmission =
        reach && (reach->admissionCapabilities & TitleCapability_ControllerInput) != 0;
    Check(TitleRegistry_AllowsSharedControllerInput(
              GameTitle::HaloReach, true, false, true, reachControllerAdmission) == reachControllerAdmission,
        "Explicit Reach obeys its immutable controller admission policy");
    Check(
        !TitleRegistry_AllowsSharedControllerInput(GameTitle::HaloReach, false, true, false, reachControllerAdmission),
        "Camera-only ownership cannot leak controller input into Reach");
    Check(VrBlitCanUseDirectCopy(
              2912, 2100, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 2912, 2100, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) &&
              !VrBlitCanUseDirectCopy(
                  2912, 2100, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 2911, 2100, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB) &&
              !VrBlitCanUseDirectCopy(
                  2912, 2100, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 2912, 2100, DXGI_FORMAT_B8G8R8A8_UNORM) &&
              !VrBlitCanUseDirectCopy(
                  2912, 2100, DXGI_FORMAT_R8G8B8A8_UNORM, 4, 2912, 2100, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB),
        "Blit direct-copy admission exactly requires equal dimensions, "
        "matching format families, and an unmultisampled source");
    CheckUnit(48);
    Check(VrBlitResourcesReady(true, true, false, true) && !VrBlitResourcesReady(false, true, true, true) &&
              !VrBlitResourcesReady(true, false, true, true) && !VrBlitResourcesReady(true, true, false, false) &&
              VrBlitResourcesReady(true, true, true, false),
        "A direct copy requires both textures but no RTV, while the shader "
        "path additionally requires its destination RTV");

    // ---- Halo 2 staged evidence, temporal checks and 6DOF ----
    const TitleDescriptor* halo2Row = TitleRegistry_Find(GameTitle::Halo2);
    const Halo2EvidenceIdentity& halo2Identity = Halo2Adapter_GetEvidenceIdentity();
    constexpr uint32_t halo2ExpectedCapabilities =
        TitleCapability_Stereo | TitleCapability_RoomScale | TitleCapability_RuntimeModes |
        TitleCapability_ControllerInput | TitleCapability_Haptics | TitleCapability_ControllerAim | TitleCapability_Hud;
    constexpr TitleHookPlan halo2ExpectedHookPlan = TitleHookPlan::Halo2StereoCore;
    constexpr uint64_t halo2ExpectedHeartbeatWindowMs = 500;
    Check(Halo2Adapter_GetStage() == Halo2AdapterStage::SameFrameStereoSixDof,
        "C-H2-6 stages Halo 2 at same-frame stereo with tracked 6DOF");
    Check(Halo2Adapter_RuntimeHooksPermitted() && Halo2Adapter_EngineWritesPermitted(),
        "C-H2-6 permits its same-frame camera hooks and scoped pose writes");
    Check(!Halo2StockScreenNeedsFatalDrain(Halo2StockScreenXrOwnership::None) &&
              Halo2StockScreenNeedsFatalDrain(Halo2StockScreenXrOwnership::AcquiredUnresolved) &&
              !Halo2StockScreenNeedsFatalDrain(Halo2StockScreenXrOwnership::Released) &&
              Halo2StockScreenNeedsFatalDrain(Halo2StockScreenXrOwnership::FrameSubmissionUnresolved),
        "C-H2-6 reserves fatal drain for unresolved XR image or frame "
        "ownership");
    CheckUnit(49);
    {
        constexpr uint32_t generation = 7;
        constexpr uint64_t serial = 42;
        constexpr Halo2SynchronousPresentationStamp unclaimed{};
        constexpr Halo2SynchronousPresentationStamp claimed{
            generation, serial, Halo2SynchronousFrameDisposition::Claimed};
        constexpr Halo2SynchronousPresentationStamp complete{
            generation, serial, Halo2SynchronousFrameDisposition::Complete};
        Check(Halo2SynchronousSelectPresentation(false, false, true, false, generation, serial, claimed) ==
                  Halo2SynchronousPresentationDecision::Drop,
            "C-H2-6 drops a claimed H2 frame even when stereo becomes "
            "ineligible before submission");
        Check(Halo2SynchronousSelectPresentation(false, true, true, false, generation, serial, complete) ==
                  Halo2SynchronousPresentationDecision::Drop,
            "C-H2-6 retains a completed disposition across live-pair reset and "
            "drops when late state cannot admit synchronous stereo");
        Check(Halo2SynchronousSelectPresentation(false, false, true, false, generation, serial, unclaimed) ==
                  Halo2SynchronousPresentationDecision::SharedDefault,
            "C-H2-6 preserves ordinary late-ineligible screen presentation for "
            "an H2 frame that no eye transaction claimed");
        Check(Halo2SynchronousSelectPresentation(true, false, false, false, generation, serial, unclaimed) ==
                  Halo2SynchronousPresentationDecision::SharedDefault,
            "C-H2-6 cannot change another title's stereo presentation decision");
        Check(Halo2SynchronousSelectPresentation(true, true, false, false, generation, serial, claimed) ==
                  Halo2SynchronousPresentationDecision::Drop,
            "C-H2-6 keeps an exact claimed H2 frame out of SharedDefault after "
            "a late active-title transition");
        Check(Halo2SynchronousSelectPresentation(true, true, true, true, generation, serial, claimed) ==
                  Halo2SynchronousPresentationDecision::SynchronousStereo,
            "C-H2-6 gives an exact completed current pair precedence over a "
            "conservative claimed stamp");
        Check(Halo2SynchronousSelectPresentation(true, true, true, false, generation, serial, unclaimed) ==
                  Halo2SynchronousPresentationDecision::StockScreen,
            "C-H2-6 routes an unclaimed H2 frame to the stock screen-quad path");
        Check(Halo2SynchronousSelectPresentation(true, true, true, false, generation, serial, unclaimed, true) ==
                      Halo2SynchronousPresentationDecision::RepeatLastPair &&
                  Halo2SynchronousSelectPresentation(true, false, true, false, generation, serial, unclaimed, true) ==
                      Halo2SynchronousPresentationDecision::StockScreen &&
                  Halo2SynchronousSelectPresentation(false, true, true, false, generation, serial, unclaimed, true) ==
                      Halo2SynchronousPresentationDecision::SharedDefault &&
                  Halo2SynchronousSelectPresentation(true, true, true, false, generation, serial, claimed, true) ==
                      Halo2SynchronousPresentationDecision::Drop,
            "C-H2-13 re-presents the last complete pair for a serial with no "
            "pair of its own, but only with stereo eligible, the projection "
            "ready and no claim on the frame");
        Check(Halo2SynchronousSelectPresentation(true, false, true, false, generation, serial, unclaimed) ==
                  Halo2SynchronousPresentationDecision::StockScreen,
            "C-H2-6 routes an unclaimed H2 frame to the stock screen even when "
            "stereo is requested but its projection descriptor is not ready");
        Check(Halo2SynchronousSelectPresentation(true, false, true, true, generation, serial, complete) ==
                  Halo2SynchronousPresentationDecision::Drop,
            "C-H2-6 drops a completed H2 pair when its projection descriptor "
            "is not ready");
        Check(Halo2SynchronousSelectPresentation(true, true, true, false, generation, serial, claimed) ==
                  Halo2SynchronousPresentationDecision::Drop,
            "C-H2-6 drops a claimed frame whose live pair was reset instead of "
            "sampling its unproven backbuffer");
        Check(Halo2SynchronousSelectPresentation(true, true, true, false, generation + 1, serial, claimed) ==
                  Halo2SynchronousPresentationDecision::StockScreen,
            "C-H2-6 rejects a durable claim from another generation");
        Check(Halo2SynchronousSelectPresentation(true, true, true, false, generation, serial + 1, claimed) ==
                  Halo2SynchronousPresentationDecision::StockScreen,
            "C-H2-6 rejects a durable claim from another prepared serial");
        Check(Halo2SynchronousDropRequiresQuarantine(
                  generation, serial, claimed, Halo2SynchronousPresentationDecision::Drop) &&
                  Halo2SynchronousDropRequiresQuarantine(
                      generation, serial, complete, Halo2SynchronousPresentationDecision::Drop),
            "A current H2 Claim or Complete drop quarantines its generation");
        Check(!Halo2SynchronousDropRequiresQuarantine(
                  generation, serial, unclaimed, Halo2SynchronousPresentationDecision::Drop) &&
                  !Halo2SynchronousDropRequiresQuarantine(
                      generation + 1, serial, claimed, Halo2SynchronousPresentationDecision::Drop) &&
                  !Halo2SynchronousDropRequiresQuarantine(
                      generation, serial + 1, claimed, Halo2SynchronousPresentationDecision::Drop) &&
                  !Halo2SynchronousDropRequiresQuarantine(
                      generation, serial, complete, Halo2SynchronousPresentationDecision::SynchronousStereo),
            "Unclaimed, stale, foreign, and successfully admitted H2 frames "
            "cannot quarantine the stereo generation");
        Check(Halo2SynchronousRequiresStrictStockScreen(true, Halo2SynchronousFrameDisposition::Unclaimed,
                  Halo2SynchronousPresentationDecision::StockScreen) &&
                  Halo2SynchronousRequiresStrictStockScreen(true, Halo2SynchronousFrameDisposition::Unclaimed,
                      Halo2SynchronousPresentationDecision::SharedDefault),
            "active H2 unclaimed frames use strict stock-screen delivery with "
            "stereo either requested or detached");
        Check(!Halo2SynchronousRequiresStrictStockScreen(false, Halo2SynchronousFrameDisposition::Unclaimed,
                  Halo2SynchronousPresentationDecision::SharedDefault) &&
                  !Halo2SynchronousRequiresStrictStockScreen(
                      true, Halo2SynchronousFrameDisposition::Claimed, Halo2SynchronousPresentationDecision::Drop) &&
                  !Halo2SynchronousRequiresStrictStockScreen(true, Halo2SynchronousFrameDisposition::Complete,
                      Halo2SynchronousPresentationDecision::SynchronousStereo),
            "strict H2 stock-screen delivery cannot change another title or "
            "a claimed/complete H2 transaction");
    }
    CheckUnit(50);
    Check(!TitleRegistry_AllowsGenericDrawDistance(GameTitle::Halo2, true) &&
              !TitleRegistry_AllowsGenericDrawDistance(GameTitle::Halo2, false) &&
              !TitleRegistry_AllowsGenericDrawDistance(GameTitle::Unknown, true) &&
              TitleRegistry_AllowsGenericDrawDistance(GameTitle::Halo3, true) &&
              !TitleRegistry_AllowsGenericDrawDistance(GameTitle::Halo3, false),
        "The worker's shared draw-distance predicate hard-denies Halo 2 even "
        "after its read-only level gate opens");
    // Halo 2 grants controller aim for the floating hands/gun mesh and the
    // bullet direction only; the right stick keeps ordinary camera turning
    // because Game_ComputeAimStick refuses Halo 2 unconditionally. Native
    // HUD layout is also granted; arm IK and theatre remain denied.
    constexpr uint32_t halo2DeniedCapabilities = TitleCapability_ArmIk | TitleCapability_CutsceneTheater;
    constexpr uint32_t halo2ExpectedAdmission = TitleCapability_ControllerInput;
    Check(halo2Row && !halo2Row->runtimeSupported && halo2Row->capabilities == halo2ExpectedCapabilities &&
              (halo2Row->capabilities & halo2DeniedCapabilities) == 0 &&
              halo2Row->admissionCapabilities == halo2ExpectedAdmission &&
              TitleRegistry_HookPlan(GameTitle::Halo2) == halo2ExpectedHookPlan &&
              TitleRuntimeHeartbeatWindowMs(GameTitle::Halo2) == halo2ExpectedHeartbeatWindowMs &&
              std::wstring_view(halo2Row->moduleName) == L"halo2.dll" &&
              std::wstring_view(halo2Identity.moduleName) == std::wstring_view(halo2Row->moduleName),
        "The Halo 2 registry row exposes the exact staged capabilities, hook "
        "plan, heartbeat, and adapter module identity while denying arm IK "
        "and cutscene-theatre claims");
    CheckUnit(51);
    {
        constexpr size_t halo2Slot = TitleRuntimeSlotIndex(GameTitle::Halo2);
        TitleRuntimeState halo2Runtime;
        TitleRuntimeModuleSet modules{};
        modules.availabilityMask = TitleRuntimeAvailabilityBit(GameTitle::Halo2);
        modules.moduleBases[halo2Slot] = 0x180000000ull;
        Check(halo2Runtime.PublishModuleSet(modules, 100),
            "The enabled H2 camera stage establishes a generation-tagged "
            "runtime slot");
        const uint32_t generation = halo2Runtime.Generation(GameTitle::Halo2);
        const TitleRuntimeLifecycle armed{true, true, false, halo2ExpectedCapabilities};
        TitleRuntimeHeartbeatPolicy policy{};
        policy.freshForMs[halo2Slot] = TitleRuntimeHeartbeatWindowMs(GameTitle::Halo2);
        Check(generation != 0 && halo2Runtime.PublishLifecycle(GameTitle::Halo2, generation, armed) &&
                  halo2Runtime.PublishMode(GameTitle::Halo2, generation, RuntimeMode::Gameplay) &&
                  halo2Runtime.PublishHeartbeat(GameTitle::Halo2, generation, 101),
            "An armed H2 camera lifecycle admits one fresh exact-generation "
            "heartbeat and gameplay mode");
        const TitleRuntimeSnapshot owner = halo2Runtime.Resolve(101, policy);
        Check(owner.owner == GameTitle::Halo2 && owner.generation == generation && owner.installed && owner.armed &&
                  !owner.teardownRequested && owner.mode == RuntimeMode::Gameplay && owner.qualifyingOwnerCount == 1 &&
                  owner.enabledCapabilities == halo2ExpectedCapabilities &&
                  TitleRuntimeMaskUnarmedCapabilities(owner, TitleCapability_Stereo | TitleCapability_RoomScale) ==
                      halo2ExpectedCapabilities,
            "A fresh armed H2 generation resolves with its exact staged "
            "capability mask and no undeclared feature bits");
        Check(!halo2Runtime.PublishLifecycle(GameTitle::Halo2, generation + 1, armed) &&
                  !halo2Runtime.PublishMode(GameTitle::Halo2, generation + 1, RuntimeMode::Gameplay) &&
                  !halo2Runtime.PublishHeartbeat(GameTitle::Halo2, generation + 1, 102),
            "H2 rejects lifecycle, mode, and heartbeat publications from a "
            "different module generation");
        const TitleRuntimeLifecycle unarmed{true, false, false, halo2ExpectedCapabilities};
        Check(halo2Runtime.PublishLifecycle(GameTitle::Halo2, generation, unarmed),
            "H2 accepts an unarmed lifecycle only for its current generation");
        const TitleRuntimeSnapshot unarmedOwner = halo2Runtime.Resolve(101, policy);
        // This helper's explicit mask strips only Stereo and RoomScale; the
        // declared ControllerInput, ControllerAim and Haptics bits survive it
        // like RuntimeModes does (the worker applies the fuller
        // kRuntimeCapabilitiesRequiringArm).
        constexpr uint32_t halo2ExpectedUnarmedCapabilities =
            TitleCapability_RuntimeModes | TitleCapability_ControllerInput | TitleCapability_ControllerAim |
            TitleCapability_Haptics | TitleCapability_Hud;
        Check(
            unarmedOwner.owner == GameTitle::Halo2 && unarmedOwner.generation == generation && unarmedOwner.installed &&
                !unarmedOwner.armed && unarmedOwner.enabledCapabilities == halo2ExpectedCapabilities &&
                TitleRuntimeMaskUnarmedCapabilities(unarmedOwner, TitleCapability_Stereo | TitleCapability_RoomScale) ==
                    halo2ExpectedUnarmedCapabilities,
            "The explicit H2 mask suppresses Stereo and RoomScale while "
            "retaining its other declared capability bits");
        const TitleRuntimeSnapshot staleOwner = halo2Runtime.Resolve(602, policy);
        Check(staleOwner.owner == GameTitle::None && staleOwner.qualifyingOwnerCount == 0 &&
                  staleOwner.enabledCapabilities == TitleCapability_None,
            "An H2 heartbeat older than the exact 500 ms window resolves to "
            "stock with no runtime capabilities");
    }
    CheckUnit(52);
    Check(!TitleRegistry_AllowsSharedControllerInput(GameTitle::Halo2, false, false, true, false),
        "Halo 2 does not admit the shared virtual controller in C-H2-1, "
        "C-H2-2, or C-H2-3");
    Check(TitleRegistry_FromModuleName(L"C:\\games\\MCC\\halo2\\HALO2.DLL") == halo2Row,
        "Halo 2 lookup accepts a case-insensitive full module path");
    Check(std::string_view(halo2Identity.moduleSha256Steam) ==
                  "DE65B4F4FDBF3F0A5EAB7431FE530DA17DD815599182DFD6AE9B7E21CF171946" &&
              std::string_view(halo2Identity.moduleSha256Store) ==
                  "81E5F41A7F8409D27A5454A28BFBECB8CD273E389366FB9865DD1D01E6BE689D" &&
              halo2Identity.peTimestamp == 0x68A0F0F2u && halo2Identity.sizeOfImage == 0x02A38000u &&
              kHalo2RetailFileSize == 15807960u &&
              std::string_view(halo2Identity.h2ekBuild) == "2023.06.20.176294.1-Release" &&
              std::string_view(halo2Identity.h2ekTagTestSha256) ==
                  "D0B71186D3948C48DDD02E2CCB88FA13E77E25A3D8F7FA60922F23A2A0073E36",
        "The Halo 2 adapter pins both retail editions and the official H2EK "
        "identity");

    Check(kHalo2RetailAnchorCount == 6, "C-H2-1 pins two liveness and four render/camera anchors");
    Check(kHalo2KitNativeChudDrawRva == 0x002EA955u && kHalo2RetailNativeChudDrawRva == 0x007FFD70u &&
              sizeof(kHalo2RetailNativeChudDrawEntryBytes) == 16u,
        "C-H2-75 pins the H2EK native-CHUD semantic match, retail RVA, "
        "and exact entry-byte proof");
    Check(kHalo2KitHudAnchorBasisRva == 0x0030C365u && kHalo2RetailHudAnchorBasisRva == 0x00829490u &&
              sizeof(kHalo2RetailHudAnchorBasisEntryBytes) == 23u,
        "Stage 3D's rejected H2EK/retail native HUD anchor-basis evidence "
        "remains pinned for audit even though C-H2-77 no longer hooks it");
    const uint32_t expectedHalo2AnchorRvas[] = {
        0x7067F0u,
        0x706910u,
        0x7E1600u,
        0x7E2130u,
        0x7E30D0u,
        0x7DFCD0u,
    };
    const size_t expectedHalo2AnchorBytes[] = {
        11u,
        73u,
        37u,
        23u,
        44u,
        34u,
    };
    size_t halo2AnchorIndex = 0;
    CheckUnit(53);
    for (const Halo2RetailAnchor& anchor : kHalo2RetailAnchors)
    {
        size_t tokenBytes = 0;
        bool tokensValid = true;
        const char* p = anchor.pattern;
        while (*p)
        {
            if (*p == ' ')
            {
                ++p;
                continue;
            }
            const char a = p[0];
            const char b = p[1];
            const bool wild = a == '?' && b == '?';
            const bool hex = ((a >= '0' && a <= '9') || (a >= 'A' && a <= 'F')) &&
                             ((b >= '0' && b <= '9') || (b >= 'A' && b <= 'F'));
            if ((!wild && !hex) || (p[2] != ' ' && p[2] != '\0'))
            {
                tokensValid = false;
                break;
            }
            ++tokenBytes;
            p += 2;
        }
        Check(anchor.name && anchor.name[0] && anchor.pattern && anchor.pattern[0] != '?' && tokensValid &&
                  tokenBytes >= 11 && anchor.rva != 0 && anchor.rva < kHalo2RetailImageSize &&
                  tokenBytes <= kHalo2RetailImageSize - anchor.rva,
            "Every Halo 2 anchor has strict AOB grammar and an in-image span");
        Check(halo2AnchorIndex < sizeof(expectedHalo2AnchorRvas) / sizeof(expectedHalo2AnchorRvas[0]) &&
                  anchor.rva == expectedHalo2AnchorRvas[halo2AnchorIndex] &&
                  tokenBytes == expectedHalo2AnchorBytes[halo2AnchorIndex],
            "Every Halo 2 anchor retains its independently verified RVA and "
            "exact byte length");
        Check(anchor.relativeDispOffset == 0 ||
                  (static_cast<size_t>(anchor.relativeDispOffset) + 4 <= tokenBytes && anchor.relativeTargetRva != 0 &&
                      anchor.relativeTargetRva < kHalo2RetailImageSize),
            "Every Halo 2 relative decode stays inside its match and targets "
            "the pinned image");
        ++halo2AnchorIndex;
    }
    CheckUnit(54);
    Check(kHalo2RetailAnchorRelativeTargets == 2 &&
              kHalo2RetailAnchors[kHalo2AnchorGameTimeIncrement].relativeDispOffset == 0x03 &&
              kHalo2RetailAnchors[kHalo2AnchorGameTimeInit].relativeDispOffset == 0x07 &&
              kHalo2RetailAnchors[kHalo2AnchorGameTimeIncrement].relativeTargetRva == kHalo2GameTimeSlotRva &&
              kHalo2RetailAnchors[kHalo2AnchorGameTimeInit].relativeTargetRva == kHalo2GameTimeSlotRva,
        "The two independent Halo 2 lifecycle anchors decode the same "
        "game-time pointer slot");
    Check(kHalo2GameTimeSlotRva + sizeof(uintptr_t) <= kHalo2RetailImageSize && kHalo2GameTimeGlobalsSize == 0x2Cu &&
              kHalo2GameTimeInitializedOffset == 0 && kHalo2GameTimeTickRateOffset == 2 &&
              kHalo2GameTimeSecondsPerTickOffset == 4 && kHalo2GameTimeCurrentTickOffset == 8,
        "The H2EK-proven game-time object and retail pointer slot are bounded");
    Check(kHalo2RetailWindowStride == 0x120u && kHalo2RetailRenderPlayerWindowRva == 0x7E2130u &&
              kHalo2RetailRenderPlayerWindowCallRva == 0x7E1706u &&
              kHalo2RetailRenderPlayerWindowReturnRva == 0x7E170Bu &&
              kHalo2RetailRenderPlayerWindowOtherCallRva == 0x7F0A60u && kHalo2RenderCameraOffset == 0x0Cu &&
              kHalo2RasterCameraOffset == 0x80u && kHalo2CameraBytes == 0x74u && kHalo2CameraPositionOffset == 0x00u &&
              kHalo2CameraForwardOffset == 0x0Cu && kHalo2CameraUpOffset == 0x18u &&
              kHalo2CameraVerticalFovOffset == 0x28u && kHalo2CameraZFarOffset == 0x44u &&
              kHalo2CameraAsymmetricEnableOffset == 0x58u && kHalo2CameraFrustumCenterXOffset == 0x5Cu &&
              kHalo2CameraFrustumCenterYOffset == 0x60u && kHalo2CameraFrustumExtentScaleOffset == 0x64u &&
              kHalo2CameraPixelOffsetEnableOffset == 0x68u && kHalo2CameraPixelOffsetXOffset == 0x6Cu &&
              kHalo2CameraPixelOffsetYOffset == 0x70u &&
              kHalo2CameraPixelOffsetYOffset + sizeof(float) <= kHalo2CameraBytes,
        "The retail Halo 2 window/camera layout retains the native "
        "asymmetric-frustum controls while C-H2-2 leaves them stock");

    CheckUnit(55);
    {
        constexpr float pi = 3.14159265358979323846f;
        const auto nearlyEqual = [](float a, float b, float tolerance = 1.0e-5f)
        { return std::fabs(a - b) <= tolerance; };
        Check(nearlyEqual(kHalo2WorldUnitsPerMeter, 1.0f / 3.048f) && kHalo2MaxEyeOffsetMeters >= 0.25f,
            "C-H2-2 uses Halo 2's evidence-backed metric scale and a bounded "
            "OpenXR eye offset");

        Halo2CameraBasis renderCamera{};
        renderCamera.position[0] = 10.0f;
        renderCamera.position[1] = 20.0f;
        renderCamera.position[2] = 30.0f;
        renderCamera.forward[2] = -1.0f;
        renderCamera.up[1] = 1.0f;
        Halo2CameraBasis rasterCamera = renderCamera;
        rasterCamera.position[0] = -5.0f;
        rasterCamera.position[1] = 1.0f;
        rasterCamera.position[2] = 2.0f;
        const float eyeOffset[3]{0.03f, 0.01f, -0.02f};
        Halo2TemporalEyePositions eyePositions{};
        Check(Halo2BuildTemporalEyePositions(renderCamera, rasterCamera, eyeOffset, eyePositions) &&
                  nearlyEqual(eyePositions.render[0], 10.0f + 0.03f * kHalo2WorldUnitsPerMeter) &&
                  nearlyEqual(eyePositions.render[1], 20.0f + 0.01f * kHalo2WorldUnitsPerMeter) &&
                  nearlyEqual(eyePositions.render[2], 30.0f - 0.02f * kHalo2WorldUnitsPerMeter) &&
                  nearlyEqual(eyePositions.raster[0], -5.0f + 0.03f * kHalo2WorldUnitsPerMeter) &&
                  nearlyEqual(eyePositions.raster[1], 1.0f + 0.01f * kHalo2WorldUnitsPerMeter) &&
                  nearlyEqual(eyePositions.raster[2], 2.0f - 0.02f * kHalo2WorldUnitsPerMeter),
            "C-H2-2 offsets the render and raster cameras independently from "
            "their own stock origins and bases");

        Halo2TemporalEyePositions unchanged{};
        unchanged.render[0] = 123.0f;
        Halo2CameraBasis invalidCamera = renderCamera;
        invalidCamera.up[1] = 0.0f;
        invalidCamera.up[2] = -1.0f;
        const float tooLargeEye[3]{kHalo2MaxEyeOffsetMeters + 0.01f, 0.0f, 0.0f};
        Check(!Halo2BuildTemporalEyePositions(invalidCamera, rasterCamera, eyeOffset, unchanged) &&
                  unchanged.render[0] == 123.0f &&
                  !Halo2BuildTemporalEyePositions(renderCamera, rasterCamera, tooLargeEye, unchanged) &&
                  unchanged.render[0] == 123.0f,
            "A corrupt basis or unbounded eye pose rejects the H2 camera write "
            "without changing its output");

        Halo2CameraRectangle viewport{};
        viewport.y1 = 1080;
        viewport.x1 = 1920;
        Halo2SymmetricHalfFovs halfFovs{};
        Check(Halo2DeriveSymmetricHalfFovs(pi * 0.5f, viewport, halfFovs) &&
                  nearlyEqual(halfFovs.vertical, pi * 0.25f) &&
                  nearlyEqual(halfFovs.horizontal, std::atan(16.0f / 9.0f)) &&
                  !Halo2DeriveSymmetricHalfFovs(0.0f, viewport, halfFovs),
            "C-H2-2 derives the stock symmetric cover from H2's vertical FOV "
            "and proven y/x viewport ordering");
        viewport.x1 = 0;
        Check(!Halo2DeriveSymmetricHalfFovs(pi * 0.5f, viewport, halfFovs),
            "An empty or inverted H2 viewport rejects temporal publication");
        Check(Halo2StockProjectionIsSymmetric(0, 0, 0, 0) && !Halo2StockProjectionIsSymmetric(1, 0, 0, 0) &&
                  !Halo2StockProjectionIsSymmetric(0, 1, 0, 0) && !Halo2StockProjectionIsSymmetric(0, 0, 1, 0) &&
                  !Halo2StockProjectionIsSymmetric(0, 0, 0, 1),
            "Any native asymmetric or pixel-offset flag withholds C-H2-2's "
            "symmetric-cover claim");

        Check(Halo2TemporalEyeForSerial(0, false) == -1 && Halo2TemporalEyeForSerial(1, false) == kHalo2LeftEye &&
                  Halo2TemporalEyeForSerial(2, false) == kHalo2RightEye &&
                  Halo2TemporalEyeForSerial(1, true) == kHalo2RightEye &&
                  Halo2TemporalEyeForSerial(2, true) == kHalo2LeftEye,
            "Prepared serial parity, including right-eye-first, owns the H2 eye");

        const Halo2TemporalEyeStamp previous{7, 10, Halo2TemporalEyeForSerial(10, false), true};
        const Halo2TemporalEyeStamp current{7, 11, Halo2TemporalEyeForSerial(11, false), true};
        Check(SelectHalo2TemporalPairAction(previous, current, 7, 11, false) ==
                  Halo2TemporalPairAction::PublishAdjacentPair,
            "Only an exact same-generation N-1/N opposite-eye pair publishes");
        Halo2TemporalEyeStamp emptyStamp{};
        Halo2TemporalEyeStamp gap = current;
        gap.preparedSerial = 13;
        gap.eye = Halo2TemporalEyeForSerial(13, false);
        Check(SelectHalo2TemporalPairAction(emptyStamp, current, 7, 11, false) ==
                      Halo2TemporalPairAction::SeedWithCurrent &&
                  SelectHalo2TemporalPairAction(previous, gap, 7, 13, false) ==
                      Halo2TemporalPairAction::SeedWithCurrent &&
                  SelectHalo2TemporalPairAction(previous, current, 8, 11, false) ==
                      Halo2TemporalPairAction::RejectCurrent &&
                  SelectHalo2TemporalPairAction(previous, current, 7, 12, false) ==
                      Halo2TemporalPairAction::RejectCurrent,
            "A first eye or serial gap reseeds, while stale generation/frame "
            "claims reject outright");

        Check(SelectHalo2CameraPositionWrite(kHalo2WindowRenderPositionOffset, 12) ==
                      Halo2CameraPositionWrite::RenderPosition &&
                  SelectHalo2CameraPositionWrite(kHalo2WindowRasterPositionOffset, 12) ==
                      Halo2CameraPositionWrite::RasterPosition &&
                  SelectHalo2CameraPositionWrite(kHalo2RenderCameraOffset + kHalo2CameraZFarOffset, 4) ==
                      Halo2CameraPositionWrite::Reject &&
                  SelectHalo2CameraPositionWrite(kHalo2RenderCameraOffset, kHalo2CameraBytes) ==
                      Halo2CameraPositionWrite::Reject,
            "C-H2-2's engine-write allow-list contains only the two exact "
            "12-byte position spans and excludes z_far/whole-camera restore");

        Halo2TemporalTransactionInput transaction{};
        transaction.stereoRequested = true;
        transaction.hookArmed = true;
        transaction.coldObservationPassed = true;
        transaction.exactCaller = true;
        transaction.flagValid = true;
        transaction.windowReadable = true;
        transaction.playerIndex = 0;
        transaction.levelLive = true;
        transaction.captureReady = true;
        transaction.activeGeneration = 7;
        transaction.snapshotGeneration = 7;
        transaction.preparedSerial = 11;
        transaction.eye = Halo2TemporalEyeForSerial(11, false);
        transaction.renderCameraValid = true;
        transaction.rasterCameraValid = true;
        transaction.eyePositionValid = true;
        transaction.stockProjectionSymmetric = true;
        transaction.halfFovsValid = true;
        Check(SelectHalo2TemporalTransactionAction(transaction) ==
                  Halo2TemporalTransactionAction::ScopedPositionsAndCallOnce,
            "The complete exact H2 transaction admits its two scoped writes");
        Halo2TemporalTransactionInput foreign = transaction;
        foreign.exactCaller = false;
        Halo2TemporalTransactionInput malformed = transaction;
        malformed.serialAlreadyClaimed = true;
        Halo2TemporalTransactionInput badFlag = transaction;
        badFlag.flagValid = false;
        Check(SelectHalo2TemporalTransactionAction(foreign) == Halo2TemporalTransactionAction::CallStockOnce &&
                  SelectHalo2TemporalTransactionAction(malformed) ==
                      Halo2TemporalTransactionAction::RejectTemporalFrameAndCallStockOnce &&
                  SelectHalo2TemporalTransactionAction(badFlag) ==
                      Halo2TemporalTransactionAction::RejectTemporalFrameAndCallStockOnce,
            "Foreign H2 callers stay stock and malformed exact frames revoke "
            "only their temporal token");

        Halo2TemporalTransactionResult complete{};
        complete.originalCalls = 1;
        complete.originalReturned = true;
        complete.renderPositionWritten = true;
        complete.rasterPositionWritten = true;
        complete.renderPositionRestored = true;
        complete.rasterPositionRestored = true;
        Check(
            Halo2TemporalTransactionResultMatches(Halo2TemporalTransactionAction::ScopedPositionsAndCallOnce, complete),
            "A completed H2 temporal transaction proves exactly one original "
            "call and both selective restores");
        complete.otherCameraBytesWritten = true;
        Check(!Halo2TemporalTransactionResultMatches(
                  Halo2TemporalTransactionAction::ScopedPositionsAndCallOnce, complete),
            "Any H2 camera write outside the two position spans invalidates "
            "temporal publication");

        // ---- Halo 2 same-frame stereo + tracked 6DOF ----
        const auto cameraNearlyEqual = [&](const Halo2CameraBasis& left, const Halo2CameraBasis& right)
        {
            for (int axis = 0; axis < 3; ++axis)
            {
                if (!nearlyEqual(left.position[axis], right.position[axis]) ||
                    !nearlyEqual(left.forward[axis], right.forward[axis]) ||
                    !nearlyEqual(left.up[axis], right.up[axis]))
                {
                    return false;
                }
            }
            return true;
        };

        Halo2TrackedHeadInput sameReference{};
        sameReference.headOwnsPitch = false;
        sameReference.orientation[1] = 0.5f;
        sameReference.orientation[3] = 0.866025404f;
        sameReference.referenceOrientation[1] = 0.5f;
        sameReference.referenceOrientation[3] = 0.866025404f;
        sameReference.position[0] = sameReference.referencePosition[0] = 1.0f;
        sameReference.position[1] = sameReference.referencePosition[1] = -2.0f;
        sameReference.position[2] = sameReference.referencePosition[2] = 0.5f;
        Halo2CameraBasis referenceIdentity{};
        Check(Halo2BuildTrackedCenterCamera(renderCamera, sameReference, referenceIdentity) &&
                  cameraNearlyEqual(referenceIdentity, renderCamera),
            "C-H2-3 treats an identical nonzero tracked head reference as the "
            "stock center camera");

        Halo2TrackedHeadInput translatedHead{};
        translatedHead.headOwnsPitch = false;
        translatedHead.position[0] = 3.048f;
        translatedHead.position[1] = -1.524f;
        translatedHead.position[2] = 0.762f;
        Halo2CameraBasis translatedCenter{};
        Check(Halo2BuildTrackedCenterCamera(renderCamera, translatedHead, translatedCenter) &&
                  nearlyEqual(translatedCenter.position[0], 11.0f) &&
                  nearlyEqual(translatedCenter.position[1], 19.5f) &&
                  nearlyEqual(translatedCenter.position[2], 30.25f) &&
                  nearlyEqual(translatedCenter.forward[2], -1.0f) && nearlyEqual(translatedCenter.up[1], 1.0f),
            "C-H2-3 maps tracked right/up/back displacement through the H2 "
            "camera basis at exactly 1/3.048 world units per metre");

        // A long walk from the recenter point is clamped, like Halo 3, Reach
        // and Halo 4 do, never rejected: rejection cost the frame its pair.
        Halo2TrackedHeadInput tooFarHead{};
        tooFarHead.headOwnsPitch = false;
        tooFarHead.position[0] = kHalo2MaxHeadTranslationMeters + 10.0f;
        Halo2CameraBasis clampedFar{};
        Check(Halo2BuildTrackedCenterCamera(renderCamera, tooFarHead, clampedFar) &&
                  nearlyEqual(clampedFar.position[0],
                      renderCamera.position[0] + kHalo2MaxHeadTranslationMeters * kHalo2WorldUnitsPerMeter),
            "C-H2-12 clamps an out-of-range room displacement per axis "
            "instead of rejecting the sample");

        // Halo-world camera (+Z up): room forward/right/up land in the game's
        // horizontal frame at 1/3.048 world units per metre times world scale.
        Halo2CameraBasis haloCamera{};
        haloCamera.position[0] = 10.0f;
        haloCamera.position[1] = 20.0f;
        haloCamera.position[2] = 30.0f;
        haloCamera.forward[0] = 1.0f;
        haloCamera.up[2] = 1.0f;
        Halo2TrackedHeadInput leanHead{};
        leanHead.headOwnsPitch = false;
        leanHead.position[0] = 1.524f;  // room right
        leanHead.position[1] = 1.524f;  // room up
        leanHead.position[2] = -0.762f; // room forward (OpenXR -Z)
        // world_scale is world units per metre; twice the default here.
        leanHead.worldScale = 2.0f * kHalo2WorldUnitsPerMeter;
        Halo2CameraBasis leaned{};
        Check(Halo2BuildTrackedCenterCamera(haloCamera, leanHead, leaned) && nearlyEqual(leaned.position[0], 10.5f) &&
                  nearlyEqual(leaned.position[1], 19.0f) && nearlyEqual(leaned.position[2], 31.0f) &&
                  nearlyEqual(leaned.forward[0], 1.0f) && nearlyEqual(leaned.up[2], 1.0f),
            "C-H2-12 applies room lean in the game's horizontal frame with "
            "room up on world +Z, scaled by the universal world scale");

        Halo2TrackedHeadInput noLeanHead = leanHead;
        noLeanHead.positional = false;
        Halo2CameraBasis unleaned{};
        Check(
            Halo2BuildTrackedCenterCamera(haloCamera, noLeanHead, unleaned) && cameraNearlyEqual(unleaned, haloCamera),
            "C-H2-12 honours the F6 positional toggle: no lean when it is OFF");

        // A game camera yawed 90 degrees left: room forward is world +Y.
        Halo2CameraBasis yawedCamera = haloCamera;
        yawedCamera.forward[0] = 0.0f;
        yawedCamera.forward[1] = 1.0f;
        Halo2TrackedHeadInput forwardHead{};
        forwardHead.position[2] = -3.048f;
        Halo2CameraBasis yawedLean{};
        Check(Halo2BuildTrackedCenterCamera(yawedCamera, forwardHead, yawedLean) &&
                  nearlyEqual(yawedLean.position[0], 10.0f) && nearlyEqual(yawedLean.position[1], 21.0f) &&
                  nearlyEqual(yawedLean.position[2], 30.0f),
            "C-H2-12 re-applies room forward along the game's own yaw");

        // Recenter reference is YAW ONLY: a reference taken while pitched
        // 30 degrees down still shows the 30-degree pitch, while a reference
        // taken yawed 30 degrees cancels the yaw exactly.
        const float halfThirty = pi / 12.0f;
        Halo2TrackedHeadInput pitchedReference{};
        pitchedReference.orientation[0] = -std::sin(halfThirty);
        pitchedReference.orientation[3] = std::cos(halfThirty);
        std::memcpy(pitchedReference.referenceOrientation, pitchedReference.orientation,
            sizeof(pitchedReference.referenceOrientation));
        Halo2CameraBasis pitchedOut{};
        Check(Halo2BuildTrackedCenterCamera(haloCamera, pitchedReference, pitchedOut) &&
                  nearlyEqual(pitchedOut.forward[2], -0.5f) && nearlyEqual(pitchedOut.forward[0], 0.866025404f) &&
                  nearlyEqual(pitchedOut.forward[1], 0.0f),
            "C-H2-12 keeps the headset's absolute pitch across a recenter "
            "taken while looking down");

        // Pose ownership predicate.
        {
            Halo2CameraBasis tracked = haloCamera;
            tracked.position[0] += 0.3f;
            Halo2CameraBasis displaced = tracked;
            displaced.position[1] += 0.5f;
            Halo2CameraBasis rotated = tracked;
            rotated.forward[0] = std::cos(2.0f * pi / 180.0f);
            rotated.forward[1] = std::sin(2.0f * pi / 180.0f);
            Check(Halo2SelectPoseOwner(false, 7, 100, tracked, tracked, 7, 100) == Halo2PoseOwnerDecision::SelfTrack,
                "E-H2-6 no publication: the per-eye core tracks itself");
            Check(Halo2SelectPoseOwner(true, 7, 100, tracked, tracked, 7, 100) ==
                      Halo2PoseOwnerDecision::UsePublishedTracked,
                "E-H2-6 same serial, same camera: use the published centre");
            Check(Halo2SelectPoseOwner(true, 7, 99, tracked, tracked, 7, 100) ==
                      Halo2PoseOwnerDecision::RederiveFromPublishedStock,
                "E-H2-6 older serial, same camera: rederive from the stock");
            Check(Halo2SelectPoseOwner(true, 7, 100, tracked, displaced, 7, 100) == Halo2PoseOwnerDecision::SelfTrack,
                "E-H2-6 a camera the observer did not write (0x960780 "
                "override, or a skipped frame) is tracked by the core");
            Check(Halo2SelectPoseOwner(true, 7, 100, tracked, rotated, 7, 100) == Halo2PoseOwnerDecision::SelfTrack,
                "E-H2-6 a 2-degree rotation is not the published camera");
            Check(Halo2SelectPoseOwner(true, 6, 100, tracked, tracked, 7, 100) == Halo2PoseOwnerDecision::NoPose,
                "E-H2-6 a matching camera from another generation drops "
                "the frame");
            Halo2CameraBasis nearlySame = tracked;
            nearlySame.position[2] += 0.0005f;
            Check(Halo2CameraBasisMatches(tracked, nearlySame) && !Halo2CameraBasisMatches(tracked, displaced) &&
                      !Halo2CameraBasisMatches(tracked, rotated),
                "E-H2-6 basis match tolerates sub-millimetre jitter only");
        }

        Halo2TrackedHeadInput yawedReference{};
        yawedReference.orientation[1] = std::sin(halfThirty);
        yawedReference.orientation[3] = std::cos(halfThirty);
        std::memcpy(yawedReference.referenceOrientation, yawedReference.orientation,
            sizeof(yawedReference.referenceOrientation));
        Halo2CameraBasis yawedOut{};
        Check(Halo2BuildTrackedCenterCamera(haloCamera, yawedReference, yawedOut) &&
                  cameraNearlyEqual(yawedOut, haloCamera),
            "C-H2-12 cancels the recenter yaw exactly");

        Halo2CameraBasis diagonalCamera{};
        diagonalCamera.forward[0] = -0.408248290f;
        diagonalCamera.forward[1] = -0.408248290f;
        diagonalCamera.forward[2] = 0.816496581f;
        diagonalCamera.up[0] = 0.707106781f;
        diagonalCamera.up[1] = -0.707106781f;
        // Diagonal camera, horizontal forward (-1,-1,0)/sqrt2, right
        // (-1,1,0)/sqrt2. Room (4,4,4): forward -4 m, right +4 m, up +4 m ->
        // (0, 5.657, 4) m -> (0, 1.856, 1.312) wu; the Y axis hits the bound.
        Halo2TrackedHeadInput boundedHead{};
        boundedHead.position[0] = 4.0f;
        boundedHead.position[1] = 4.0f;
        boundedHead.position[2] = 4.0f;
        Halo2CameraBasis boundedCenter{};
        Check(Halo2ValidateCameraBasis(diagonalCamera) &&
                  Halo2BuildTrackedCenterCamera(diagonalCamera, boundedHead, boundedCenter) &&
                  nearlyEqual(boundedCenter.position[0], 0.0f) &&
                  nearlyEqual(boundedCenter.position[1], kHalo2MaxHeadTranslationWorldUnits) &&
                  nearlyEqual(boundedCenter.position[2], 4.0f * kHalo2WorldUnitsPerMeter),
            "C-H2-3 clamps a finite in-range head displacement at the exact "
            "per-axis world-space write bound");

        const float rotationHalfAngle = pi / 12.0f;
        const float rotationSin = std::sin(rotationHalfAngle);
        const float rotationCos = std::cos(rotationHalfAngle);
        const float headRotations[3][4] = {
            {rotationSin, 0.0f, 0.0f, rotationCos},
            {0.0f, rotationSin, 0.0f, rotationCos},
            {0.0f, 0.0f, rotationSin, rotationCos},
        };
        bool allHeadRotationsValid = true;
        for (const auto& rotation : headRotations)
        {
            Halo2TrackedHeadInput rotatedHead{};
            std::memcpy(rotatedHead.orientation, rotation, sizeof(rotatedHead.orientation));
            Halo2CameraBasis rotatedCenter{};
            const bool built = Halo2BuildTrackedCenterCamera(renderCamera, rotatedHead, rotatedCenter);
            bool finite = true;
            bool changed = false;
            for (int axis = 0; axis < 3; ++axis)
            {
                finite = finite && std::isfinite(rotatedCenter.position[axis]) &&
                         std::isfinite(rotatedCenter.forward[axis]) && std::isfinite(rotatedCenter.up[axis]);
                changed = changed || !nearlyEqual(rotatedCenter.forward[axis], renderCamera.forward[axis]) ||
                          !nearlyEqual(rotatedCenter.up[axis], renderCamera.up[axis]);
            }
            allHeadRotationsValid =
                allHeadRotationsValid && built && finite && changed && Halo2ValidateCameraBasis(rotatedCenter);
        }
        Check(allHeadRotationsValid, "C-H2-3 yaw, pitch, and roll each produce a finite orthonormal H2 "
                                     "camera basis");

        Halo2CameraBasis rasterCenter{};
        rasterCenter.position[0] = -5.0f;
        rasterCenter.position[1] = 1.0f;
        rasterCenter.position[2] = 2.0f;
        rasterCenter.forward[0] = 1.0f;
        rasterCenter.up[2] = 1.0f;
        constexpr float eyeHalfSeparationMeters = 0.032f;
        const float leftEyePosition[3]{-eyeHalfSeparationMeters, 0.0f, 0.0f};
        const float rightEyePosition[3]{eyeHalfSeparationMeters, 0.0f, 0.0f};
        const float cantHalfAngle = pi / 72.0f;
        const float cantSin = std::sin(cantHalfAngle);
        const float cantCos = std::cos(cantHalfAngle);
        const float leftEyeCant[4]{0.0f, -cantSin, 0.0f, cantCos};
        const float rightEyeCant[4]{0.0f, cantSin, 0.0f, cantCos};
        Halo2CameraBasis renderLeft{}, renderRight{};
        Halo2CameraBasis rasterLeft{}, rasterRight{};
        Check(Halo2BuildSynchronousEyeCamera(renderCamera, leftEyePosition, leftEyeCant, renderLeft) &&
                  Halo2BuildSynchronousEyeCamera(renderCamera, rightEyePosition, rightEyeCant, renderRight) &&
                  Halo2BuildSynchronousEyeCamera(rasterCenter, leftEyePosition, leftEyeCant, rasterLeft) &&
                  Halo2BuildSynchronousEyeCamera(rasterCenter, rightEyePosition, rightEyeCant, rasterRight) &&
                  Halo2ValidateCameraBasis(renderLeft) && Halo2ValidateCameraBasis(renderRight) &&
                  Halo2ValidateCameraBasis(rasterLeft) && Halo2ValidateCameraBasis(rasterRight) &&
                  nearlyEqual(renderRight.position[0] - renderLeft.position[0],
                      2.0f * eyeHalfSeparationMeters * kHalo2WorldUnitsPerMeter) &&
                  nearlyEqual(rasterRight.position[1] - rasterLeft.position[1],
                      -2.0f * eyeHalfSeparationMeters * kHalo2WorldUnitsPerMeter) &&
                  renderLeft.forward[0] * renderRight.forward[0] < 0.0f &&
                  rasterLeft.forward[1] * rasterRight.forward[1] < 0.0f,
            "C-H2-3 applies opposite eye offsets and cant independently to "
            "both the render and raster camera bases");

        const float excessiveEyePosition[3]{kHalo2MaxEyeOffsetMeters + 0.01f, 0.0f, 0.0f};
        const float invalidEyeOrientation[4]{};
        Halo2CameraBasis rejectedEye{};
        rejectedEye.position[0] = 321.0f;
        Check(!Halo2BuildSynchronousEyeCamera(renderCamera, excessiveEyePosition, leftEyeCant, rejectedEye) &&
                  rejectedEye.position[0] == 321.0f &&
                  !Halo2BuildSynchronousEyeCamera(renderCamera, leftEyePosition, invalidEyeOrientation, rejectedEye) &&
                  rejectedEye.position[0] == 321.0f,
            "C-H2-3 rejects an unbounded eye offset or invalid cant without "
            "changing the output camera");

        Check(SelectHalo2CameraPoseWrite(kHalo2WindowRenderPositionOffset, kHalo2CameraVectorBytes) ==
                      Halo2CameraPoseWrite::RenderPosition &&
                  SelectHalo2CameraPoseWrite(kHalo2WindowRenderForwardOffset, kHalo2CameraVectorBytes) ==
                      Halo2CameraPoseWrite::RenderForward &&
                  SelectHalo2CameraPoseWrite(kHalo2WindowRenderUpOffset, kHalo2CameraVectorBytes) ==
                      Halo2CameraPoseWrite::RenderUp &&
                  SelectHalo2CameraPoseWrite(kHalo2WindowRasterPositionOffset, kHalo2CameraVectorBytes) ==
                      Halo2CameraPoseWrite::RasterPosition &&
                  SelectHalo2CameraPoseWrite(kHalo2WindowRasterForwardOffset, kHalo2CameraVectorBytes) ==
                      Halo2CameraPoseWrite::RasterForward &&
                  SelectHalo2CameraPoseWrite(kHalo2WindowRasterUpOffset, kHalo2CameraVectorBytes) ==
                      Halo2CameraPoseWrite::RasterUp &&
                  SelectHalo2CameraPoseWrite(kHalo2WindowRenderVerticalFovOffset, sizeof(float)) ==
                      Halo2CameraPoseWrite::RenderVerticalFov &&
                  SelectHalo2CameraPoseWrite(kHalo2WindowRasterVerticalFovOffset, sizeof(float)) ==
                      Halo2CameraPoseWrite::RasterVerticalFov &&
                  SelectHalo2CameraPoseWrite(kHalo2WindowRenderPositionOffset, sizeof(float)) ==
                      Halo2CameraPoseWrite::Reject &&
                  SelectHalo2CameraPoseWrite(kHalo2RenderCameraOffset + kHalo2CameraZFarOffset,
                      kHalo2CameraVectorBytes) == Halo2CameraPoseWrite::Reject &&
                  SelectHalo2CameraPoseWrite(kHalo2RenderCameraOffset + kHalo2CameraAsymmetricEnableOffset,
                      sizeof(float)) == Halo2CameraPoseWrite::Reject &&
                  SelectHalo2CameraPoseWrite(kHalo2WindowRasterUpOffset, sizeof(Halo2CameraBasis)) ==
                      Halo2CameraPoseWrite::Reject &&
                  SelectHalo2CameraPoseWrite(kHalo2WindowRenderPositionOffset + sizeof(float),
                      kHalo2CameraVectorBytes) == Halo2CameraPoseWrite::Reject,
            "C-H2-3's write allow-list contains exactly six 12-byte pose spans "
            "and two 4-byte vertical-FOV cover fields while rejecting partial, "
            "overlapping, asymmetric-projection, z-far, or whole-camera writes");

        // -------------------------------------------------------------
        // Two renderers, and which one is live.
        // -------------------------------------------------------------
        Check(Halo2GraphicsModeFromAppliedMode(0) == Halo2GraphicsMode::Classic &&
                  Halo2GraphicsModeFromAppliedMode(1) == Halo2GraphicsMode::Remastered &&
                  Halo2GraphicsModeFromAppliedMode(2) == Halo2GraphicsMode::Remastered &&
                  Halo2GraphicsModeFromAppliedMode(-1) == Halo2GraphicsMode::Remastered,
            "E-H2-3 reads the applied render mode with the polarity the "
            "engine itself passes to the Saber isLegacy argument: zero is "
            "classic, anything else is remastered");

        Check(Halo2ClassicRenderTreeRuns(0) && !Halo2ClassicRenderTreeRuns(1) && !Halo2ClassicRenderTreeRuns(0xFF),
            "E-H2-3 treats the classic-disabled byte as the render-side "
            "truth that 0x95FEC0 actually tests");

        Check(Halo2GraphicsModeIsCoherent(0, 0) && Halo2GraphicsModeIsCoherent(1, 1) &&
                  !Halo2GraphicsModeIsCoherent(0, 1) && !Halo2GraphicsModeIsCoherent(1, 0),
            "E-H2-3 detects a torn read between the mode dword and the "
            "classic-disabled byte instead of trusting either alone");

        Check(Halo2ClassicRenderHookCanFire(Halo2GraphicsMode::Classic, 0) &&
                  !Halo2ClassicRenderHookCanFire(Halo2GraphicsMode::Remastered, 1) &&
                  !Halo2ClassicRenderHookCanFire(Halo2GraphicsMode::Unknown, 0) &&
                  !Halo2ClassicRenderHookCanFire(Halo2GraphicsMode::Classic, 1),
            "E-H2-3 knows a classic render hook cannot fire while the "
            "remastered renderer owns the frame, which is why C-H2-6 saw "
            "zero callbacks with correct addresses");

        // -------------------------------------------------------------
        // The observer is the shared camera root.
        // -------------------------------------------------------------
        static_assert(kHalo2ObserverResultArrayRva == kHalo2ObserverArrayRva + kHalo2ObserverResultOffset);
        Check(Halo2ObserverRecordOffset(0) == 0 && Halo2ObserverRecordOffset(1) == kHalo2ObserverStride &&
                  Halo2ObserverRecordOffset(3) == 3ull * kHalo2ObserverStride &&
                  Halo2ObserverResultOffsetForUser(0) == kHalo2ObserverResultOffset &&
                  Halo2ObserverResultOffsetForUser(2) == 2ull * kHalo2ObserverStride + kHalo2ObserverResultOffset,
            "E-H2-4 addresses the observer result exactly as the engine "
            "accessor does: base + user * 0x368 + 0xC4");

        Check(Halo2ObserverUserValid(0) && Halo2ObserverUserValid(3) && !Halo2ObserverUserValid(4) &&
                  !Halo2ObserverUserValid(0xFFFFFFFFu),
            "E-H2-4 refuses an out-of-range observer user rather than "
            "indexing past the four-record array");

        Check(Halo2ObserverSpanOffset(Halo2ObserverSpan::Position) == kHalo2ObserverResultPositionOffset &&
                  Halo2ObserverSpanOffset(Halo2ObserverSpan::Forward) == kHalo2ObserverResultForwardOffset &&
                  Halo2ObserverSpanOffset(Halo2ObserverSpan::Up) == kHalo2ObserverResultUpOffset &&
                  Halo2ObserverSpanBytes(Halo2ObserverSpan::Position) == kHalo2CameraVectorBytes,
            "E-H2-4 keeps the three observer pose spans at their proven, "
            "non-contiguous offsets");

        Check(Halo2ObserverSpanWriteAllowed(kHalo2ObserverResultPositionOffset, kHalo2CameraVectorBytes) &&
                  Halo2ObserverSpanWriteAllowed(kHalo2ObserverResultForwardOffset, kHalo2CameraVectorBytes) &&
                  Halo2ObserverSpanWriteAllowed(kHalo2ObserverResultUpOffset, kHalo2CameraVectorBytes) &&
                  !Halo2ObserverSpanWriteAllowed(kHalo2ObserverResultHorizontalFovOffset, kHalo2CameraVectorBytes) &&
                  !Halo2ObserverSpanWriteAllowed(kHalo2ObserverResultVerticalFovOffset, sizeof(float)) &&
                  !Halo2ObserverSpanWriteAllowed(kHalo2ObserverResultAspectOffset, sizeof(float)) &&
                  !Halo2ObserverSpanWriteAllowed(kHalo2ObserverResultPositionOffset, sizeof(float)) &&
                  !Halo2ObserverSpanWriteAllowed(
                      kHalo2ObserverResultPositionOffset + sizeof(float), kHalo2CameraVectorBytes) &&
                  !Halo2ObserverSpanWriteAllowed(kHalo2ObserverResultOffset, kHalo2ObserverResultOwnedBytes),
            "E-H2-4 owns exactly the three 12-byte observer pose vectors and "
            "never the engine-derived field of view, aspect, or a whole-"
            "struct copy");

        // -------------------------------------------------------------
        // The Blam to Saber hand-off, replicated from 0x5F510.
        // -------------------------------------------------------------
        Halo2CameraBasis saberSource{};
        saberSource.position[0] = 10.0f;
        saberSource.position[1] = 20.0f;
        saberSource.position[2] = 30.0f;
        saberSource.forward[0] = 1.0f;
        saberSource.up[2] = 1.0f;

        Halo2SaberCameraConstants zeroConstants{};
        Halo2SaberViewMatrix saberMatrix{};
        const bool saberBuilt = Halo2BuildSaberViewMatrix(saberSource, zeroConstants, saberMatrix);
        Check(saberBuilt && saberMatrix.m[8] == 1.0f && saberMatrix.m[9] == 0.0f && saberMatrix.m[10] == 0.0f &&
                  saberMatrix.m[4] == 0.0f && saberMatrix.m[5] == 1.0f && saberMatrix.m[6] == 0.0f &&
                  saberMatrix.m[0] == 0.0f && saberMatrix.m[1] == 0.0f && saberMatrix.m[2] == 1.0f &&
                  saberMatrix.m[3] == 0.0f && saberMatrix.m[7] == 0.0f && saberMatrix.m[11] == 0.0f &&
                  saberMatrix.m[15] == 1.0f,
            "The Saber view matrix swaps axes exactly as 0x5F510 does: row1 "
            "is saber(up), row2 is saber(forward), row0 is their cross, and "
            "the homogeneous column stays 0/0/0/1");

        Check(saberBuilt && std::fabs(saberMatrix.m[12] - 10.0f * kHalo2MetersPerWorldUnit) < 1.0e-3f &&
                  std::fabs(saberMatrix.m[13] - 30.0f * kHalo2MetersPerWorldUnit) < 1.0e-3f &&
                  std::fabs(saberMatrix.m[14] + 20.0f * kHalo2MetersPerWorldUnit) < 1.0e-3f,
            "The Saber translation converts Halo 2 world units to metres at "
            "3.048 and maps (x, y, z) to (x, z, -y) exactly as the engine "
            "bridge does");

        Halo2SaberCameraConstants offsetConstants{};
        offsetConstants.offsetX = 1.0f;
        offsetConstants.offsetZ = 2.0f;
        offsetConstants.offsetY = 3.0f;
        offsetConstants.forwardScale = 4.0f;
        Halo2SaberViewMatrix offsetMatrix{};
        Check(Halo2BuildSaberViewMatrix(saberSource, offsetConstants, offsetMatrix) &&
                  std::fabs(offsetMatrix.m[12] - (10.0f * kHalo2MetersPerWorldUnit + 1.0f + 4.0f)) < 1.0e-3f &&
                  std::fabs(offsetMatrix.m[13] - (30.0f * kHalo2MetersPerWorldUnit + 2.0f)) < 1.0e-3f &&
                  std::fabs(offsetMatrix.m[14] - (3.0f - 20.0f * kHalo2MetersPerWorldUnit)) < 1.0e-3f,
            "The Saber translation adds the engine-owned offsets and pushes "
            "the camera along saber(forward) by the engine's forward scale");

        Halo2CameraBasis degenerateBasis{};
        Halo2SaberViewMatrix untouched{};
        untouched.m[0] = 42.0f;
        Halo2SaberCameraConstants infiniteConstants{};
        infiniteConstants.offsetX = std::numeric_limits<float>::infinity();
        Check(!Halo2BuildSaberViewMatrix(degenerateBasis, zeroConstants, untouched) &&
                  !Halo2BuildSaberViewMatrix(saberSource, infiniteConstants, untouched) && untouched.m[0] == 42.0f,
            "A degenerate camera basis or a non-finite engine constant "
            "leaves the Saber matrix untouched instead of writing garbage "
            "into the renderer");

        float saberHorizontal = 0.0f;
        Check(Halo2SaberHorizontalFovDegreesFromVertical(90.0f, 1.0f, saberHorizontal) &&
                  std::fabs(saberHorizontal - 90.0f) < 1.0e-2f,
            "At a 1:1 aspect the Saber horizontal field of view equals the "
            "vertical one, matching 0xBC560");

        // 0xBC4F0(cam, 80) on the 640x480 constructor (aspect
        // 0.75 = height/width) leaves +0x150 = 80 and +0x154 =
        // 2*atan(tan 40 deg * 0.75) = 64.37 degrees; going the other way
        // must reproduce 80.
        float classicHorizontal = 0.0f;
        Check(Halo2SaberHorizontalFovDegreesFromVertical(64.3698f, 0.75f, classicHorizontal) &&
                  std::fabs(classicHorizontal - 80.0f) < 0.05f,
            "The Saber aspect is height/width: 64 deg vertical at 0.75 is "
            "the classic 80 deg horizontal");

        float rejectedHorizontal = 7.0f;
        Check(!Halo2SaberHorizontalFovDegreesFromVertical(0.0f, 1.0f, rejectedHorizontal) &&
                  !Halo2SaberHorizontalFovDegreesFromVertical(180.0f, 1.0f, rejectedHorizontal) &&
                  !Halo2SaberHorizontalFovDegreesFromVertical(90.0f, 0.0f, rejectedHorizontal) &&
                  rejectedHorizontal == 7.0f,
            "An out-of-range Saber field of view is refused without "
            "changing the caller's value");

        // The Saber eye cover is solved per axis from both eyes' native
        // frusta: Quest 3 through Steam Link reports L-54 R40 U44 D-55 and
        // the mirror for the other eye.
        {
            const float deg = pi / 180.0f;
            const float leftEye[4] = {-54.0f * deg, 40.0f * deg, 44.0f * deg, -55.0f * deg};
            const float rightEye[4] = {-40.0f * deg, 54.0f * deg, 44.0f * deg, -55.0f * deg};
            Halo2SaberEyeCover cover{};
            Check(Halo2DeriveSaberEyeCover(leftEye, rightEye, cover) &&
                      std::fabs(cover.halfHorizontalRadians - std::atan(std::tan(54.0f * deg) * 1.002f)) < 1.0e-4f &&
                      std::fabs(cover.halfVerticalRadians - std::atan(std::tan(55.0f * deg) * 1.002f)) < 1.0e-4f &&
                      cover.horizontalDegrees > 108.0f && cover.horizontalDegrees < 109.0f &&
                      cover.verticalDegrees > 110.0f && cover.verticalDegrees < 111.0f,
                "E-H2-7 the Saber eye cover contains both eyes per axis "
                "with the 0.2 percent margin");
            const float badEye[4] = {0.1f, 40.0f * deg, 44.0f * deg, -55.0f * deg};
            Halo2SaberEyeCover untouchedCover{};
            untouchedCover.horizontalDegrees = 3.0f;
            Check(
                !Halo2DeriveSaberEyeCover(badEye, rightEye, untouchedCover) && untouchedCover.horizontalDegrees == 3.0f,
                "E-H2-7 a frustum with the wrong sign convention is refused");
        }

        // The Saber cover is aspect-locked like 0xBC560 - the
        // horizontal axis derives from the vertical and +0x158 (height /
        // width), and both eyes still fit on both axes.
        {
            const float deg = pi / 180.0f;
            const float leftEye[4] = {-54.0f * deg, 40.0f * deg, 44.0f * deg, -55.0f * deg};
            const float rightEye[4] = {-40.0f * deg, 54.0f * deg, 44.0f * deg, -55.0f * deg};
            const float aspect = 2100.0f / 2912.0f;
            Halo2SaberEyeCover locked{};
            float derived = 0.0f;
            Check(Halo2DeriveSaberAspectLockedEyeCover(leftEye, rightEye, aspect, locked) &&
                      std::fabs(locked.halfVerticalRadians - std::atan(std::tan(55.0f * deg) * 1.002f)) < 1.0e-4f &&
                      Halo2SaberHorizontalFovDegreesFromVertical(locked.verticalDegrees, aspect, derived) &&
                      std::fabs(derived - locked.horizontalDegrees) < 1.0e-3f && locked.horizontalDegrees > 126.0f &&
                      locked.horizontalDegrees < 127.0f &&
                      std::tan(locked.halfHorizontalRadians) >= std::tan(54.0f * deg) * 1.002f - 1.0e-4f,
                "E-H2-13 the 2912x2100 Saber cover is 110 deg vertical with the "
                "126 deg horizontal 0xBC560 would derive, containing both eyes");
            // A tall raster (aspect 2.0): the horizontal requirement drives
            // the vertical cover up instead.
            Halo2SaberEyeCover tall{};
            Check(Halo2DeriveSaberAspectLockedEyeCover(leftEye, rightEye, 2.0f, tall) &&
                      std::fabs(std::tan(tall.halfVerticalRadians) - std::tan(54.0f * deg) * 1.002f * 2.0f) < 1.0e-3f &&
                      std::tan(tall.halfHorizontalRadians) >= std::tan(54.0f * deg) * 1.002f - 1.0e-4f,
                "E-H2-13 a tall raster lifts the vertical cover so the "
                "derived horizontal still contains both eyes");
            Halo2SaberEyeCover untouchedInner{};
            untouchedInner.verticalDegrees = 9.0f;
            Check(!Halo2DeriveSaberAspectLockedEyeCover(leftEye, rightEye, 0.0f, untouchedInner) &&
                      !Halo2DeriveSaberAspectLockedEyeCover(
                          leftEye, rightEye, std::numeric_limits<float>::quiet_NaN(), untouchedInner) &&
                      untouchedInner.verticalDegrees == 9.0f,
                "E-H2-13 a zero or NaN aspect is refused without touching the "
                "caller's cover");

            float visibility = 0.0f;
            const float stock = 91.9f * deg;
            Check(Halo2DeriveObserverVisibilityVerticalFov(leftEye, rightEye, stock, visibility) &&
                      visibility > 110.0f * deg && visibility < 111.0f * deg &&
                      Halo2ObserverVisibilityFovWriteAllowed(kHalo2ObserverResultVerticalFovOffset, sizeof(float)) &&
                      !Halo2ObserverVisibilityFovWriteAllowed(kHalo2ObserverResultHorizontalFovOffset, sizeof(float)) &&
                      !Halo2ObserverVisibilityFovWriteAllowed(kHalo2ObserverResultVerticalFovOffset, 12),
                "E-H2-62 the shared observer visibility FOV expands from Saber's "
                "91.9 degree stock cover to the 110.1 degree headset cover, and "
                "the write allow-list admits only that one float");
            Check(Halo2DeriveObserverVisibilityVerticalFov(leftEye, rightEye, stock, visibility, true) &&
                      visibility > 120.0f * deg && visibility < 121.0f * deg,
                "H2 pre-visibility guard extends the 110.1 degree raster cover by ten degrees total");
            const float authoredWide = 120.0f * deg;
            Check(Halo2DeriveObserverVisibilityVerticalFov(leftEye, rightEye, authoredWide, visibility) &&
                      std::fabs(visibility - authoredWide) < 1.0e-6f,
                "E-H2-62 an authored engine FOV wider than the headset cover is "
                "preserved exactly");
            const float badVisibilityEye[4] = {0.1f, 40.0f * deg, 44.0f * deg, -55.0f * deg};
            visibility = 7.0f;
            Check(!Halo2DeriveObserverVisibilityVerticalFov(badVisibilityEye, rightEye, stock, visibility) &&
                      !Halo2DeriveObserverVisibilityVerticalFov(leftEye, rightEye, 0.0f, visibility) &&
                      visibility == 7.0f,
                "E-H2-62 invalid XR or stock FOV evidence refuses the optional "
                "visibility feature without changing the caller's value");
        }

        // The headset owns pitch. A stock camera pitched
        // 30 deg up by the engine composes an identity head onto a LEVEL
        // camera; the engine pitch is read back from the stock basis and
        // the head pitch from the OpenXR quaternion with the same sign.
        {
            const float deg = pi / 180.0f;
            Halo2CameraBasis pitched{};
            pitched.forward[0] = std::cos(30.0f * deg);
            pitched.forward[2] = std::sin(30.0f * deg);
            pitched.up[0] = -std::sin(30.0f * deg);
            pitched.up[2] = std::cos(30.0f * deg);
            Halo2TrackedHeadInput levelHead{};
            Halo2CameraBasis composed{};
            Check(Halo2BuildTrackedCenterCamera(pitched, levelHead, composed) &&
                      std::fabs(composed.forward[0] - 1.0f) < 1.0e-4f && std::fabs(composed.forward[2]) < 1.0e-4f &&
                      std::fabs(composed.up[2] - 1.0f) < 1.0e-4f,
                "E-H2-16 the engine's own pitch is flattened out of the camera "
                "before the head is composed");
            float enginePitch = 0.0f;
            Check(Halo2EnginePitchRadians(pitched, enginePitch) && std::fabs(enginePitch - 30.0f * deg) < 1.0e-4f,
                "E-H2-16 engine pitch reads positive looking up");
            // Head pitched 20 deg up about +X (OpenXR): q = (sin10, 0, 0, cos10).
            const float headUp[4] = {std::sin(10.0f * deg), 0.0f, 0.0f, std::cos(10.0f * deg)};
            float headPitch = 0.0f;
            Check(Halo2HeadPitchRadians(headUp, headPitch) && std::fabs(headPitch - 20.0f * deg) < 1.0e-4f,
                "E-H2-16 head pitch reads positive looking up, same sign as "
                "the engine");
            Halo2TrackedHeadInput lookUp{};
            std::memcpy(lookUp.orientation, headUp, sizeof(headUp));
            Halo2CameraBasis lookedUp{};
            float composedPitch = 0.0f;
            Check(Halo2BuildTrackedCenterCamera(pitched, lookUp, lookedUp) &&
                      Halo2EnginePitchRadians(lookedUp, composedPitch) &&
                      std::fabs(composedPitch - 20.0f * deg) < 1.0e-3f,
                "E-H2-16 the composed camera pitch is the head's alone, not "
                "engine plus head");
            // Walking: tracked yaw 90 deg left of stock rotates the move vector.
            Halo2CameraBasis stockFacing{};
            stockFacing.forward[0] = 1.0f;
            stockFacing.up[2] = 1.0f;
            Halo2CameraBasis trackedFacing{};
            trackedFacing.forward[1] = 1.0f;
            trackedFacing.up[2] = 1.0f;
            float yawDelta = 0.0f;
            Check(Halo2HeadYawDeltaRadians(stockFacing, trackedFacing, yawDelta) &&
                      std::fabs(yawDelta - 90.0f * deg) < 1.0e-4f,
                "E-H2-16 the head yaw delta is tracked minus stock, wrapped");
            Halo2CameraBasis wrapped{};
            wrapped.forward[0] = std::cos(170.0f * deg);
            wrapped.forward[1] = std::sin(170.0f * deg);
            Halo2CameraBasis wrappedTracked{};
            wrappedTracked.forward[0] = std::cos(-170.0f * deg);
            wrappedTracked.forward[1] = std::sin(-170.0f * deg);
            Check(Halo2HeadYawDeltaRadians(wrapped, wrappedTracked, yawDelta) &&
                      std::fabs(yawDelta - 20.0f * deg) < 1.0e-3f,
                "E-H2-16 the yaw delta wraps across +-180");
        }

        // The interpolator reset's pinned entry decodes the buffer slot.
        {
            // 48 83 3D disp32 00: cmp qword [rip+disp],0 at 0x723010 (+8) -> 0x164B2E0.
            Check(0x00723010 + 8 + 0x00F282C8 == kHalo2FrameInterpolatorAllocatedSlotRva,
                "E-H2-22 the reset's guard tests the interpolator-allocated slot");
            Check(kHalo2FrameInterpolatorFirstPersonSlots == 4, "E-H2-22 four first-person slots per local player");
        }

        // The frame head-sample latch anchors and its pose guard.
        {
            // 0x818CA0 is first_person_weapons: mov rax,rsp / mov [rax+18],r8b
            // / mov [rax+10],edx / mov [rax+8],ecx / push rbp rbx rsi rdi.
            Check(sizeof(kHalo2FirstPersonWeaponsEntryBytes) == 17 && kHalo2FirstPersonWeaponsEntryBytes[0] == 0x48 &&
                      kHalo2FirstPersonWeaponsEntryBytes[1] == 0x8B && kHalo2FirstPersonWeaponsEntryBytes[2] == 0xC4 &&
                      kHalo2FirstPersonWeaponsEntryBytes[13] == 0x55,
                "E-H2-23 the weapon placement is pinned by its entry bytes");
            Check(kHalo2FirstPersonWeaponsRva == 0x00818CA0, "E-H2-23 the weapon placement keeps its proven RVA");
            Halo2CameraBasis a{};
            a.forward[0] = 1.0f;
            a.up[2] = 1.0f;
            a.position[0] = 12.5f;
            Halo2CameraBasis b = a;
            Check(Halo2CameraBasisMatchesExactly(a, b), "E-H2-23 an untouched record matches the published pose");
            b.position[0] += 0.001f;
            Check(!Halo2CameraBasisMatchesExactly(a, b), "E-H2-23 a record the engine moved does not match");
        }

        // The capture probe rotation walks every candidate.
        {
            Check(kHalo2CaptureCandidateSlots == 2 + kHalo2EyeBoundRtvSlots,
                "E-H2-24 the candidate set is the two slots plus the bound set");
            // Four candidates, source 0: the rotation must reach 1, 2 and 3.
            bool seen[4] = {false, false, false, false};
            int rotation = 0;
            for (int round = 0; round < 6; ++round)
            {
                int filled = 0;
                for (int slot = 0; slot < 2; ++slot)
                {
                    const int candidate = Halo2ProbeCandidateForSlot(rotation, slot, 0, 4);
                    if (candidate < 0)
                        continue;
                    Check(candidate != 0, "E-H2-24 the live source is never probed against itself");
                    seen[candidate] = true;
                    ++filled;
                }
                rotation = Halo2NextProbeRotation(rotation, 4, filled);
            }
            Check(seen[1] && seen[2] && seen[3], "E-H2-24 every candidate is probed within a few rounds");
            Check(
                Halo2ProbeCandidateForSlot(0, 0, 0, 1) < 0, "E-H2-24 a single candidate leaves the probe slots empty");
            Check(Halo2NextProbeRotation(3, 4, 0) == 0, "E-H2-24 an empty round resets the rotation");
        }

        // The matrix helpers the weapon view is built with.
        {
            float identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            float a[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
            float out[16]{};
            Halo2MultiplyMatrix4x4(a, identity, out);
            Check(Halo2MatricesClose(out, a, 1.0e-6f), "E-H2-26 multiplying by the identity returns the matrix");
            // A camera matrix with basis ROWS and a translation row: the
            // view without translation is the transpose of its rotation.
            float camera[16] = {0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1, 0, 7, 8, 9, 1};
            float view[16]{};
            Halo2SaberViewWithoutTranslation(camera, view);
            Check(view[0] == 0.0f && view[1] == -1.0f && view[4] == 1.0f && view[5] == 0.0f && view[10] == 1.0f &&
                      view[15] == 1.0f && view[12] == 0.0f && view[13] == 0.0f && view[14] == 0.0f,
                "E-H2-26 the view without translation transposes the rotation");
            float product[16]{};
            Halo2MultiplyMatrix4x4(view, camera, product);
            Check(std::fabs(product[0] - 1.0f) < 1.0e-5f && std::fabs(product[5] - 1.0f) < 1.0e-5f &&
                      std::fabs(product[1]) < 1.0e-5f,
                "E-H2-26 the view rotation undoes the camera rotation");
            float differs[16]{};
            for (int i = 0; i < 16; ++i)
                differs[i] = a[i];
            differs[7] += 0.5f;
            Check(!Halo2MatricesClose(differs, a, 1.0e-3f), "E-H2-26 a changed element fails the match");
        }

        // Typeless capture formats are named concretely.
        {
            Check(Halo2ConcreteFormat(90) == 87 && Halo2ConcreteFormat(27) == 28 && Halo2ConcreteFormat(92) == 88,
                "E-H2-27 typeless formats map to their unorm view format");
            Check(Halo2FormatIsTypeless(90) && !Halo2FormatIsTypeless(28) && Halo2ConcreteFormat(28) == 28,
                "E-H2-27 a concrete format is left alone");
        }

        // The weapon view follows the interpolator between ticks.
        {
            Halo2CameraBasis previousInner{};
            previousInner.forward[0] = 1.0f;
            previousInner.up[2] = 1.0f;
            previousInner.position[0] = 0.0f;
            Halo2CameraBasis currentInner{};
            currentInner.forward[1] = 1.0f;
            currentInner.up[2] = 1.0f;
            currentInner.position[0] = 10.0f;
            Halo2CameraBasis out{};
            Check(Halo2LerpCameraBasis(previousInner, currentInner, 0.0f, out) &&
                      std::fabs(out.position[0]) < 1.0e-4f && std::fabs(out.forward[0] - 1.0f) < 1.0e-4f,
                "E-H2-30 factor 0 is the previous tick");
            Check(Halo2LerpCameraBasis(previousInner, currentInner, 1.0f, out) &&
                      std::fabs(out.position[0] - 10.0f) < 1.0e-4f && std::fabs(out.forward[1] - 1.0f) < 1.0e-4f,
                "E-H2-30 factor 1 is the current tick");
            Check(Halo2LerpCameraBasis(previousInner, currentInner, 0.5f, out) &&
                      std::fabs(out.position[0] - 5.0f) < 1.0e-4f,
                "E-H2-30 the position blends linearly");
            float length = 0.0f;
            for (int axis = 0; axis < 3; ++axis)
                length += out.forward[axis] * out.forward[axis];
            Check(std::fabs(length - 1.0f) < 1.0e-4f, "E-H2-30 the blended forward is renormalised");
            float dot = 0.0f;
            for (int axis = 0; axis < 3; ++axis)
                dot += out.forward[axis] * out.up[axis];
            Check(std::fabs(dot) < 1.0e-4f, "E-H2-30 the blended basis stays orthogonal");
            Check(Halo2LerpCameraBasis(previousInner, currentInner, 2.0f, out) &&
                      std::fabs(out.position[0] - 10.0f) < 1.0e-4f,
                "E-H2-30 a factor outside 0..1 is clamped");
        }

        // The first-person node re-anchor follows the head exactly.
        {
            Halo2CameraBasis tick{};
            tick.forward[0] = 1.0f;
            tick.up[2] = 1.0f;
            tick.position[0] = 100.0f;
            tick.position[1] = 50.0f;
            Halo2CameraBasis frame = tick;
            float rotation[9]{};
            Check(Halo2BuildWorldDeltaRotation(tick, frame, rotation) && std::fabs(rotation[0] - 1.0f) < 1.0e-5f &&
                      std::fabs(rotation[4] - 1.0f) < 1.0e-5f && std::fabs(rotation[8] - 1.0f) < 1.0e-5f &&
                      std::fabs(rotation[1]) < 1.0e-5f,
                "E-H2-32 an unchanged camera gives the identity");
            // A 90-degree yaw: forward x -> y, with a small head move.
            frame.forward[0] = 0.0f;
            frame.forward[1] = 1.0f;
            frame.position[0] = 100.2f;
            frame.position[1] = 50.1f;
            Check(Halo2BuildWorldDeltaRotation(tick, frame, rotation), "E-H2-32 the yaw delta builds");
            // The node: axes = world identity, half a metre ahead of the tick
            // camera along its forward (x).
            float node[13]{};
            node[0] = 1.0f;
            node[1] = 1.0f;
            node[5] = 1.0f;
            node[9] = 1.0f; // X,Y,Z axes
            node[10] = 100.5f;
            node[11] = 50.0f;
            node[12] = 0.0f;
            Halo2ReanchorFirstPersonNode(node, rotation, tick.position, frame.position);
            // The offset (0.5, 0, 0) rotates to (0, 0.5, 0) and re-roots at
            // the frame camera: still half a metre ahead of the NEW forward.
            Check(std::fabs(node[10] - 100.2f) < 1.0e-4f && std::fabs(node[11] - 50.6f) < 1.0e-4f &&
                      std::fabs(node[12]) < 1.0e-4f,
                "E-H2-32 the node stays half a metre ahead of the camera");
            // The node X axis rotates with the yaw: x -> y.
            Check(std::fabs(node[1]) < 1.0e-4f && std::fabs(node[2] - 1.0f) < 1.0e-4f && std::fabs(node[3]) < 1.0e-4f,
                "E-H2-32 the node axes turn with the head");
            // The Z axis (up) is unchanged by a pure yaw.
            Check(std::fabs(node[9] - 1.0f) < 1.0e-4f, "E-H2-32 up survives a yaw untouched");
        }

        // Node-space classification, the stale-camera compensation,
        // and the idempotent slot cache.
        {
            const float tickCamera[3] = {500.0f, 600.0f, 1.0f};
            const float worldRoot[3] = {500.4f, 600.1f, 0.9f};
            const float relativeRoot[3] = {0.4f, 0.1f, -0.1f};
            const float farRoot[3] = {250.0f, 0.0f, 0.0f};
            Check(Halo2ClassifyFirstPersonNodeSpace(worldRoot, tickCamera) == Halo2FirstPersonNodeSpace::World,
                "E-H2-34 a node next to the camera is in world space");
            Check(Halo2ClassifyFirstPersonNodeSpace(relativeRoot, tickCamera) ==
                      Halo2FirstPersonNodeSpace::CameraRelative,
                "E-H2-34 a node near the origin with a far camera is camera-relative");
            Check(Halo2ClassifyFirstPersonNodeSpace(farRoot, tickCamera) == Halo2FirstPersonNodeSpace::Unknown,
                "E-H2-34 a node far from both is unknown");

            static Halo2FirstPersonSlotCache cache{};
            cache = Halo2FirstPersonSlotCache{};
            Halo2CameraBasis tick{};
            tick.forward[1] = 1.0f;
            tick.up[2] = 1.0f;
            tick.position[0] = 500.0f;
            tick.position[1] = 600.0f;
            tick.position[2] = 1.0f;
            Halo2FirstPersonPassCameras pass{};
            pass.frame = tick;
            pass.frameValid = true;
            pass.correct = tick;
            pass.correct.position[0] -= 0.01f; // left eye: 10 mm left of centre
            pass.viewing = tick;
            pass.viewing.position[0] += 0.01f; // the pass draws from the RIGHT eye
            pass.compensate = true;
            float nodes[2 * 13]{};
            for (int n = 0; n < 2; ++n)
            {
                float* node = nodes + n * 13;
                node[0] = 1.0f;
                node[1] = 1.0f;
                node[5] = 1.0f;
                node[9] = 1.0f;
                node[10] = 500.0f;
                node[11] = 600.5f + 0.1f * n;
                node[12] = 0.9f;
            }
            Halo2FirstPersonReanchorResult outcome{};
            Check(Halo2ReanchorFirstPersonSlot(nodes, 2, tick, pass, cache, outcome) && outcome.applied &&
                      outcome.compensated && !outcome.fromCache && outcome.space == Halo2FirstPersonNodeSpace::World,
                "E-H2-34 a world-space slot is re-anchored and compensated");
            // Same orientation for both eyes: the compensation is a pure
            // translation by (viewing - correct) = +20 mm along x, so the
            // stale right-eye camera sees what the left eye should.
            Check(std::fabs(nodes[10] - 500.02f) < 1.0e-4f && std::fabs(nodes[11] - 600.5f) < 1.0e-4f &&
                      std::fabs(nodes[23] - 500.02f) < 1.0e-4f && std::fabs(nodes[24] - 600.6f) < 1.0e-4f,
                "E-H2-34 the compensation shifts every node by the eye offset difference");
            Check(std::fabs(nodes[1] - 1.0f) < 1.0e-5f && std::fabs(nodes[5] - 1.0f) < 1.0e-5f,
                "E-H2-34 equal orientations leave the node axes alone");
            // Idempotent: hand the cache back exactly what it wrote.
            Check(Halo2ReanchorFirstPersonSlot(nodes, 2, tick, pass, cache, outcome) && outcome.fromCache &&
                      std::fabs(nodes[10] - 500.02f) < 1.0e-4f,
                "E-H2-34 a second pass over the same array does not accumulate");
            // Fresh engine output is taken as the new original.
            nodes[10] = 500.0f;
            nodes[11] = 600.7f;
            Check(Halo2ReanchorFirstPersonSlot(nodes, 2, tick, pass, cache, outcome) && !outcome.fromCache &&
                      std::fabs(nodes[10] - 500.02f) < 1.0e-4f && std::fabs(nodes[11] - 600.7f) < 1.0e-4f,
                "E-H2-34 fresh engine nodes replace the cached original");
            // Unknown space: untouched, refused.
            float farNode[13]{};
            farNode[0] = 1.0f;
            farNode[1] = 1.0f;
            farNode[5] = 1.0f;
            farNode[9] = 1.0f;
            farNode[10] = 250.0f;
            Check(!Halo2ReanchorFirstPersonSlot(farNode, 1, tick, pass, cache, outcome) &&
                      std::fabs(farNode[10] - 250.0f) < 1.0e-6f,
                "E-H2-34 nodes of unknown space are left as the engine made them");
            // Camera-relative nodes: rotation only, never translated.
            cache = Halo2FirstPersonSlotCache{};
            float rel[13]{};
            rel[0] = 1.0f;
            rel[1] = 1.0f;
            rel[5] = 1.0f;
            rel[9] = 1.0f;
            rel[10] = 0.0f;
            rel[11] = 0.5f;
            rel[12] = -0.1f;
            Halo2FirstPersonPassCameras turned = pass;
            turned.compensate = false;
            turned.frame.forward[0] = -1.0f;
            turned.frame.forward[1] = 0.0f; // yaw +90: y -> -x
            Check(Halo2ReanchorFirstPersonSlot(rel, 1, tick, turned, cache, outcome) &&
                      outcome.space == Halo2FirstPersonNodeSpace::CameraRelative &&
                      std::fabs(rel[10] + 0.5f) < 1.0e-4f && std::fabs(rel[11]) < 1.0e-4f,
                "E-H2-34 camera-relative nodes turn with the head about the origin");
        }

        // Controller carrier and rigid native-palette placement.
        {
            Halo2CameraBasis head{};
            head.position[0] = 10.0f;
            head.position[1] = 20.0f;
            head.position[2] = 30.0f;
            head.forward[0] = 1.0f;
            head.up[2] = 1.0f;
            const float identity[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            const float headPosition[3]{};
            const float controllerPosition[3] = {1.0f, 2.0f, -3.0f};
            Halo2CameraBasis carrier{};
            Check(Halo2BuildControllerCarrier(
                      head, identity, headPosition, identity, controllerPosition, 0.5f, 0.2f, carrier) &&
                      nearlyEqual(carrier.position[0], 11.6f) && nearlyEqual(carrier.position[1], 19.5f) &&
                      nearlyEqual(carrier.position[2], 31.0f) && nearlyEqual(carrier.forward[0], 1.0f),
                "C-H2-41 maps the controller's head-relative OpenXR position "
                "and gun-forward trim through Halo 2's tracked camera basis");

            const float halfYaw = pi * 0.25f;
            const float yawedController[4] = {0.0f, std::sin(halfYaw), 0.0f, std::cos(halfYaw)};
            Halo2CameraBasis yawedCarrier{};
            Check(Halo2BuildControllerCarrier(
                      head, identity, headPosition, yawedController, headPosition, 0.5f, 0.0f, yawedCarrier) &&
                      std::fabs(yawedCarrier.forward[0]) < 1.0e-4f && nearlyEqual(yawedCarrier.forward[1], 1.0f) &&
                      nearlyEqual(yawedCarrier.up[2], 1.0f),
                "C-H2-41 maps a controller yaw into Halo 2 world axes without "
                "copying another engine's basis convention");

            // No `relativeOrientation[0] = -x` mirror (an earlier version had one).
            // Negating one quaternion term is not an inverted pitch: it is
            // F * R^-1 * F, which reverses how the hand's yaw and roll compose
            // too. Pin the plain, unmirrored mapping instead - an OpenXR
            // controller pitched UP (+x) carries the Halo 2 carrier's forward
            // UP (+world Z), which is what both engines' own conventions say.
            const float halfPitch = pi / 12.0f;
            const float raisedController[4] = {std::sin(halfPitch), 0.0f, 0.0f, std::cos(halfPitch)};
            Halo2CameraBasis raisedCarrier{};
            Check(Halo2BuildControllerCarrier(
                      head, identity, headPosition, raisedController, headPosition, 0.5f, 0.0f, raisedCarrier) &&
                      raisedCarrier.forward[0] > 0.8f && raisedCarrier.forward[2] > 0.4f,
                "C-H2-45 keeps the controller carrier an unmirrored "
                "head-relative rotation (C-H2-44's sign flip is gone)");
            // The left-hand binding, built from Halo 2's
            // own model flags. The table below is the shipped Master Chief
            // fp_battle_rifle rig read out of the kit: 42 nodes, left wrist 5,
            // right wrist 6, weapon 37..40 hanging off the RIGHT wrist.
            {
                constexpr uint32_t kNodes = 42;
                uint8_t flags[kNodes]{};
                int16_t parents[kNodes]{};
                for (uint32_t i = 0; i < kNodes; ++i)
                {
                    flags[i] = kHalo2ModelFlagPrimary;
                    parents[i] = 0;
                }
                flags[0] = kHalo2ModelFlagPrimary | kHalo2ModelFlagLocalRoot;
                parents[0] = -1;
                parents[1] = 0;
                parents[2] = 0; // upper arms
                parents[3] = 1;
                parents[4] = 2; // forearms
                parents[5] = 3;
                parents[6] = 4; // wrists
                flags[1] |= kHalo2ModelFlagLeftArmMember;
                flags[3] |= kHalo2ModelFlagLeftArmMember;
                flags[5] |= kHalo2ModelFlagLeftHand | kHalo2ModelFlagLeftArmMember;
                flags[6] |= kHalo2ModelFlagRightHand;
                for (uint32_t i = 7; i <= 11; ++i)
                {
                    parents[i] = 5;
                    flags[i] |= kHalo2ModelFlagLeftArmMember;
                }
                for (uint32_t i = 12; i <= 16; ++i)
                    parents[i] = 6;
                for (uint32_t i = 17; i <= 21; ++i)
                {
                    parents[i] = static_cast<int16_t>(i - 10);
                    flags[i] |= kHalo2ModelFlagLeftArmMember;
                }
                for (uint32_t i = 22; i <= 26; ++i)
                    parents[i] = static_cast<int16_t>(i - 10);
                for (uint32_t i = 27; i <= 31; ++i)
                {
                    parents[i] = static_cast<int16_t>(i - 10);
                    flags[i] |= kHalo2ModelFlagLeftArmMember;
                }
                for (uint32_t i = 32; i <= 36; ++i)
                    parents[i] = static_cast<int16_t>(i - 10);
                parents[37] = 6; // gun, on the RIGHT wrist
                flags[37] = kHalo2ModelFlagSecondary | kHalo2ModelFlagLocalRoot;
                for (uint32_t i = 38; i <= 40; ++i)
                {
                    parents[i] = 37;
                    flags[i] = kHalo2ModelFlagSecondary;
                }
                parents[41] = 0;
                flags[41] = 0; // camera_control

                Halo2FirstPersonArmBinding binding{};
                bool built = Halo2BuildFirstPersonArmBinding(flags, parents, kNodes, binding);
                uint64_t expectedLeft = 1ull << 5;
                for (uint32_t i = 7; i <= 11; ++i)
                    expectedLeft |= 1ull << i;
                for (uint32_t i = 17; i <= 21; ++i)
                    expectedLeft |= 1ull << i;
                for (uint32_t i = 27; i <= 31; ++i)
                    expectedLeft |= 1ull << i;
                uint64_t expectedRight = 1ull << 6;
                for (uint32_t i = 12; i <= 16; ++i)
                    expectedRight |= 1ull << i;
                for (uint32_t i = 22; i <= 26; ++i)
                    expectedRight |= 1ull << i;
                for (uint32_t i = 32; i <= 40; ++i)
                    expectedRight |= 1ull << i;
                Check(built && binding.valid && binding.leftWrist == 5 && binding.rightWrist == 6 &&
                          binding.leftElbow == 3 && binding.rightElbow == 4 && binding.leftShoulder == 1 &&
                          binding.rightShoulder == 2 && binding.rigKind == Halo2FirstPersonRigKind::MasterChief &&
                          binding.leftArmAncestors == ((1ull << 1) | (1ull << 3)) &&
                          binding.rightArmAncestors == ((1ull << 2) | (1ull << 4)) &&
                          binding.leftSubtree == expectedLeft && binding.rightSubtree == expectedRight &&
                          binding.armAncestors == ((1ull << 1) | (1ull << 2) | (1ull << 3) | (1ull << 4)),
                    "C-H2-47 finds Halo 2's left wrist and its 15 finger nodes "
                    "and the right hand + gun subtree from the engine's own "
                    "model flags, with no hardcoded index");
                Check(built && (binding.leftSubtree & (1ull << 37)) == 0 && (binding.leftSubtree & (1ull << 6)) == 0 &&
                          (binding.rightSubtree & (1ull << 37)) != 0 && (binding.rightSubtree & (1ull << 1)) == 0 &&
                          (binding.rightSubtree & (1ull << 2)) == 0,
                    "Halo 2 keeps only the right hand and gun on the right "
                    "controller, excluding both upper arms");

                Halo2FirstPersonTransform armShoulder{}, armElbow{}, armStockWrist{}, armDesiredWrist{};
                armShoulder.scale = armElbow.scale = armStockWrist.scale = armDesiredWrist.scale = 1.0f;
                armShoulder.rotation[0] = armShoulder.rotation[4] = armShoulder.rotation[8] = 1.0f;
                armElbow.rotation[0] = armElbow.rotation[4] = armElbow.rotation[8] = 1.0f;
                armStockWrist.rotation[0] = armStockWrist.rotation[4] = armStockWrist.rotation[8] = 1.0f;
                armDesiredWrist.rotation[0] = armDesiredWrist.rotation[4] = armDesiredWrist.rotation[8] = 1.0f;
                armElbow.translation[0] = 1.0f;
                armStockWrist.translation[0] = 2.0f;
                armDesiredWrist.translation[0] = 1.0f;
                armDesiredWrist.translation[1] = 1.0f;
                Check(Halo2SolveFinalPacketArm(armShoulder, armElbow, armStockWrist, armDesiredWrist) &&
                          std::fabs(std::sqrt(armElbow.translation[0] * armElbow.translation[0] +
                                              armElbow.translation[1] * armElbow.translation[1]) -
                                    1.0f) < 1.0e-4f,
                    "H2 final-packet analytic arm solve preserves authored link lengths while reaching tracked wrist");
                Halo2FirstPersonTransform refusedShoulder{}, refusedElbow{};
                refusedShoulder.scale = refusedElbow.scale = 1.0f;
                refusedShoulder.rotation[0] = refusedShoulder.rotation[4] = refusedShoulder.rotation[8] = 1.0f;
                refusedElbow.rotation[0] = refusedElbow.rotation[4] = refusedElbow.rotation[8] = 1.0f;
                refusedElbow.translation[0] = 1.0f;
                Halo2FirstPersonTransform refusedWrist = armStockWrist;
                Halo2FirstPersonTransform unreachableWrist = armDesiredWrist;
                unreachableWrist.translation[0] = 6.0f;
                unreachableWrist.translation[1] = 0.0f;
                Check(!Halo2SolveFinalPacketArm(refusedShoulder, refusedElbow, refusedWrist, unreachableWrist) &&
                          refusedElbow.translation[0] == 1.0f,
                    "H2 arm solve refuses unreachable targets without partially changing its chain");

                static Halo2FirstPersonSlotCache splitCache{};
                splitCache = Halo2FirstPersonSlotCache{};
                float splitNodes[kNodes * kHalo2FirstPersonNodeFloats]{};
                for (uint32_t index = 0; index < kNodes; ++index)
                {
                    float* const matrix = splitNodes + index * kHalo2FirstPersonNodeFloats;
                    matrix[0] = 1.0f;
                    matrix[1] = matrix[5] = matrix[9] = 1.0f;
                }
                Halo2FirstPersonReanchorResult splitResult{};
                Check(Halo2PlaceFirstPersonSlotOnTwoControllers(
                          splitNodes, kNodes, head, head, head, binding, 1.0f, 1.0f, splitCache, splitResult) &&
                          splitResult.applied && nearlyEqual(splitNodes[0], 1.0f) &&
                          nearlyEqual(splitNodes[1 * kHalo2FirstPersonNodeFloats], 0.0001f) &&
                          nearlyEqual(splitNodes[2 * kHalo2FirstPersonNodeFloats], 0.0001f) &&
                          nearlyEqual(splitNodes[5 * kHalo2FirstPersonNodeFloats], 1.0f) &&
                          nearlyEqual(splitNodes[6 * kHalo2FirstPersonNodeFloats], 1.0f) &&
                          nearlyEqual(splitNodes[37 * kHalo2FirstPersonNodeFloats], 1.0f),
                    "Halo 2 floating hands collapse only the four arm ancestors "
                    "while preserving root, both hands, and the gun subtree");

                // The Elite rig: 36 nodes, weapon root at 31 not 37. The
                // binding must follow the FLAGS, never the indices.
                constexpr uint32_t kElite = 36;
                uint8_t eliteFlags[kElite]{};
                int16_t eliteParents[kElite]{};
                for (uint32_t i = 0; i < kElite; ++i)
                {
                    eliteFlags[i] = kHalo2ModelFlagPrimary;
                    eliteParents[i] = 0;
                }
                eliteParents[0] = -1;
                eliteFlags[0] |= kHalo2ModelFlagLocalRoot;
                eliteParents[1] = 0;
                eliteParents[2] = 0;
                eliteParents[3] = 1;
                eliteParents[4] = 2;
                eliteParents[5] = 3;
                eliteParents[6] = 4;
                eliteFlags[5] |= kHalo2ModelFlagLeftHand;
                eliteFlags[6] |= kHalo2ModelFlagRightHand;
                for (uint32_t i = 7; i < 31; ++i)
                    eliteParents[i] = static_cast<int16_t>(i % 2 ? 7 : 6);
                eliteParents[7] = eliteParents[9] = eliteParents[11] = eliteParents[13] = 5;
                eliteParents[31] = 6;
                eliteFlags[31] = kHalo2ModelFlagSecondary | kHalo2ModelFlagLocalRoot;
                for (uint32_t i = 32; i < kElite; ++i)
                {
                    eliteParents[i] = 31;
                    eliteFlags[i] = kHalo2ModelFlagSecondary;
                }
                Halo2FirstPersonArmBinding elite{};
                Check(Halo2BuildFirstPersonArmBinding(eliteFlags, eliteParents, kElite, elite) &&
                          elite.leftWrist == 5 && elite.rightWrist == 6 && elite.leftElbow == 3 &&
                          elite.rightElbow == 4 && elite.leftShoulder == 1 && elite.rightShoulder == 2 &&
                          elite.rigKind == Halo2FirstPersonRigKind::Elite && (elite.leftSubtree & (1ull << 31)) == 0,
                    "C-H2-47 binds the 36-node Elite rig, whose weapon root is "
                    "at 31 not 37, from the same flags");

                // Refusals: two left hands, none at all, a parent cycle, an
                // out-of-range parent, and a palette past the engine's own
                // 64-entry capacity must all fail to the single-carrier path.
                uint8_t bad[kNodes];
                std::memcpy(bad, flags, sizeof(bad));
                bad[9] |= kHalo2ModelFlagLeftHand;
                Halo2FirstPersonArmBinding refused{};
                Check(!Halo2BuildFirstPersonArmBinding(bad, parents, kNodes, refused),
                    "C-H2-47 refuses a rig with two left hands");
                std::memcpy(bad, flags, sizeof(bad));
                bad[5] &= static_cast<uint8_t>(~kHalo2ModelFlagLeftHand);
                Check(!Halo2BuildFirstPersonArmBinding(bad, parents, kNodes, refused),
                    "C-H2-47 refuses a rig with no left hand");
                int16_t badParents[kNodes];
                std::memcpy(badParents, parents, sizeof(badParents));
                badParents[3] = 5; // 5 -> 3 -> 5
                Check(!Halo2BuildFirstPersonArmBinding(flags, badParents, kNodes, refused),
                    "C-H2-47 refuses a cyclic parent tree");
                std::memcpy(badParents, parents, sizeof(badParents));
                badParents[7] = static_cast<int16_t>(kNodes);
                Check(!Halo2BuildFirstPersonArmBinding(flags, badParents, kNodes, refused),
                    "C-H2-47 refuses an out-of-range parent index");
                Check(!Halo2BuildFirstPersonArmBinding(flags, parents, kHalo2FirstPersonPaletteCapacity + 1, refused) &&
                          kHalo2FirstPersonPaletteCapacity == 64,
                    "C-H2-47 refuses a palette past the engine's own 64-node "
                    "first-person capacity");
                Check(kHalo2AnimationNodeStride == 0x20 && kHalo2AnimationNodeParentOffset == 0x08 &&
                          kHalo2AnimationNodeModelFlagsOffset == 0x0A && kHalo2AnimationGraphNodeCountOffset == 0x0C &&
                          kHalo2ModelFlagLeftHand == 0x08 && kHalo2ModelFlagRightHand == 0x10 &&
                          kHalo2ModelFlagLeftArmMember == 0x20 && kHalo2AnimationGraphDefinitionGetRva == 0x0079EEA0 &&
                          kHalo2AnimationGraphGetSkeletonNodeRva == 0x0079F430 &&
                          kHalo2AnimationGraphGetNodeCountRva == 0x0079F470 &&
                          kHalo2AnimationGraphFindNodeByFlagsRva == 0x0079E8D0,
                    "E-H2-41 pins the retail animation-graph node layout and "
                    "the three readers C-H2-47 calls");
            }

            Check(kHalo2VisibleConsumerControllerOwnershipEnabled,
                "C-H2-65's renderer-selected packet ownership is the one "
                "controller-ownership path (the rejected ones were deleted in C1)");
            Check(kHalo2MatrixComposeRva == 0x0072A150 && kHalo2FirstPersonPrimaryComposeReturnRva == 0x00818623 &&
                      kHalo2FirstPersonSecondaryComposeReturnRva == 0x00818773 &&
                      sizeof(kHalo2MatrixComposeEntryBytes) == 32,
                "E-H2-43 pins the final root composer and only its two "
                "first-person palette return sites");
            Check(kHalo2FirstPersonPacketBuilderRva == 0x008181F0 &&
                      kHalo2ClassicFirstPersonPacketCallerRva == 0x007E5430 &&
                      sizeof(kHalo2FirstPersonPacketBuilderEntryBytes) == 29 &&
                      kHalo2FirstPersonVisibleConsumerRva == 0x0006BB40 &&
                      sizeof(kHalo2FirstPersonVisibleConsumerEntryBytes) == 29 &&
                      kHalo2FirstPersonUserDataPointerRva == 0x0187C300 && kHalo2FirstPersonUserStride == 0x20FC &&
                      kHalo2FirstPersonWeaponDataOffset == 0x0C && kHalo2FirstPersonWeaponSlotStride == 0x1028 &&
                      kHalo2FirstPersonHandsRemapOffset == 0x214 &&
                      kHalo2FirstPersonAnimationNodeCountOffset == 0x31C &&
                      kHalo2FirstPersonRenderPacketHeaderBytes == 0x0C && kHalo2FirstPersonRenderPacketStride == 0x0D0C,
                "E-H2-45/E-H2-53/E-H2-55 pin H2EK's first-person packet "
                "builder, Classic persistent-packet caller, Anniversary "
                "pre-copy consumer, and weapon-data layout");
            Check(kHalo2NativeAimUpdateRva == 0x008FDF50 && sizeof(kHalo2NativeAimUpdateEntryBytes) == 22 &&
                      kHalo2ObjectDatumAccessorRva == 0x008D7000 && kHalo2ObjectsDataArrayPointerRva == 0x018B7398 &&
                      kHalo2UnitDesiredAimingVectorOffset == 0x168 && kHalo2UnitAimingVectorOffset == 0x174,
                "E-H2-54 pins Halo 2's native desired/current aiming-vector "
                "updater and local object-datum access path");
            {
                constexpr uint32_t kPacketNodes = 4;
                float hands[kPacketNodes * kHalo2FirstPersonNodeFloats]{};
                float gun[kHalo2FirstPersonNodeFloats]{};
                auto identityNode = [](float* node, float x)
                {
                    std::memset(node, 0, kHalo2FirstPersonNodeStride);
                    node[0] = 1.0f;
                    node[1] = node[5] = node[9] = 1.0f;
                    node[10] = x;
                };
                identityNode(hands + 0 * kHalo2FirstPersonNodeFloats, 0.0f);
                identityNode(hands + 1 * kHalo2FirstPersonNodeFloats, -2.0f);
                identityNode(hands + 2 * kHalo2FirstPersonNodeFloats, 2.0f);
                identityNode(hands + 3 * kHalo2FirstPersonNodeFloats, -3.0f);
                identityNode(gun, 4.0f);
                const int32_t remap[kPacketNodes] = {0, 1, 2, 3};
                Halo2FirstPersonArmBinding packetBinding{};
                packetBinding.valid = true;
                packetBinding.leftWrist = 1;
                packetBinding.rightWrist = 2;
                packetBinding.count = kPacketNodes;
                packetBinding.leftSubtree = (1ull << 1) | (1ull << 3);
                packetBinding.rightSubtree = 1ull << 2;
                packetBinding.leftDirectChildren = 1ull << 3;
                packetBinding.rigKind = Halo2FirstPersonRigKind::MasterChief;
                Halo2CameraBasis right{};
                right.position[0] = 10.0f;
                right.forward[1] = 1.0f;
                right.up[2] = 1.0f;
                Halo2CameraBasis left = right;
                left.position[0] = -10.0f;
                // The compose camera: in the stock game the weapon points down
                // it, and this synthetic identity root's barrel is +X, so the
                // camera looks down +X. (The automatic barrel swing is
                // then the identity here; it has its own test.)
                Halo2CameraBasis authoredRoot{};
                authoredRoot.forward[0] = 1.0f;
                authoredRoot.up[2] = 1.0f;
                Halo2FinalPacketOwnershipResult result{};
                {
                    // The live secondary map omits the right wrist. Guns must
                    // still follow independent controllers in a merged hand packet.
                    float dualHands[sizeof(hands) / sizeof(float)]{};
                    float primary[kHalo2FirstPersonNodeFloats]{};
                    float secondary[2 * kHalo2FirstPersonNodeFloats]{};
                    const int32_t secondaryRemap[kPacketNodes] = {-1, 1, -1, 3};
                    auto resetDual = [&]()
                    {
                        std::memcpy(dualHands, hands, sizeof(hands));
                        identityNode(primary, 4.0f);
                        identityNode(secondary, -4.0f);
                        identityNode(secondary + kHalo2FirstPersonNodeFloats, -5.0f);
                    };
                    resetDual();
                    Halo2CameraBasis dualLeft = left;
                    dualLeft.forward[1] = -1.0f;
                    Halo2FinalPacketOwnershipResult dualResult{};
                    auto ownDual = [&](const Halo2FirstPersonArmBinding& binding)
                    {
                        return Halo2OwnDualFirstPersonPackets(dualHands, kPacketNodes, remap, binding, primary, 1,
                            secondaryRemap, packetBinding, secondary, 2, authoredRoot, right, dualLeft, 2.0f, 0.5f,
                            1.0f, dualResult);
                    };
                    Check(ownDual(packetBinding) && dualResult.applied && dualResult.gunNodes == 3 &&
                              dualResult.leftNodes == 2 && nearlyEqual(primary[2], 1.0f) &&
                              nearlyEqual(secondary[2], -1.0f) && nearlyEqual(primary[10], 10.0f) &&
                              nearlyEqual(primary[11], 4.0f) && nearlyEqual(secondary[10], -10.0f) &&
                              nearlyEqual(secondary[11], 1.0f) &&
                              nearlyEqual(dualHands[kHalo2FirstPersonNodeFloats + 10], -10.0f) &&
                              nearlyEqual(dualHands[kHalo2FirstPersonNodeFloats + 11], 0.0f),
                        "H2 merged dual packets retain each gun's independent controller and grip with a partial secondary map");
                    resetDual();
                    // Failure on the last secondary node must leave all three
                    // native packets byte-identical, including the valid primary.
                    secondary[kHalo2FirstPersonNodeFloats + 10] = std::numeric_limits<float>::quiet_NaN();
                    float beforeSecondary[2 * kHalo2FirstPersonNodeFloats]{};
                    std::memcpy(beforeSecondary, secondary, sizeof(secondary));
                    Check(!ownDual(packetBinding) && !dualResult.applied &&
                              std::memcmp(dualHands, hands, sizeof(hands)) == 0 &&
                              std::memcmp(primary, gun, sizeof(gun)) == 0 &&
                              std::memcmp(secondary, beforeSecondary, sizeof(secondary)) == 0,
                        "H2 dual packet failure never partially mutates the hands or either gun");
                    resetDual();
                    auto invalidBinding = packetBinding;
                    invalidBinding.count = 65;
                    Check(!ownDual(invalidBinding) && !dualResult.applied &&
                              std::memcmp(dualHands, hands, sizeof(hands)) == 0,
                        "H2 dual ownership rejects oversized primary graphs before remap bit shifts");
                    invalidBinding = packetBinding;
                    invalidBinding.rightSubtree |= 1ull << 1;
                    Check(!ownDual(invalidBinding) && !dualResult.applied,
                        "H2 dual ownership rejects secondary remaps that steal primary right-hand bones");
                }
                // Anatomical routing is a presentation transaction: both real
                // meshes change role while both gun packets remain exact.
                const auto anatomicalPalmsMatch =
                    [&](const float* corrected, const float* baseline, Halo2FirstPersonRigKind rig)
                {
                    Halo2FirstPersonTransform marker[2]{}, before[2]{}, after[2]{};
                    for (int hand = 0; hand < 2; ++hand)
                    {
                        Halo2FirstPersonTransform beforeWrist{}, afterWrist{};
                        const int node = hand + 1;
                        if (!Halo2AnatomicalGripMarker(rig, hand == 0, marker[hand]) ||
                            !Halo2ReadFirstPersonTransform(baseline + node * 13, beforeWrist) ||
                            !Halo2ReadFirstPersonTransform(corrected + node * 13, afterWrist) ||
                            !Halo2ComposeFirstPersonTransforms(beforeWrist, marker[hand], before[hand]) ||
                            !Halo2ComposeFirstPersonTransforms(afterWrist, marker[hand], after[hand]))
                            return false;
                    }
                    for (int hand = 0; hand < 2; ++hand)
                    {
                        if (!nearlyEqual(after[hand].scale, before[1 - hand].scale))
                            return false;
                        for (int axis = 0; axis < 3; ++axis)
                            if (!nearlyEqual(after[hand].translation[axis], before[1 - hand].translation[axis]))
                                return false;
                        for (int value = 0; value < 9; ++value)
                            if (!nearlyEqual(after[hand].rotation[value], before[1 - hand].rotation[value]))
                                return false;
                    }
                    return true;
                };
                for (bool twoHand : {false, true})
                    for (auto rig : {Halo2FirstPersonRigKind::MasterChief, Halo2FirstPersonRigKind::Elite})
                    {
                        auto binding = packetBinding;
                        binding.rigKind = rig;
                        float baselineHands[kPacketNodes * kHalo2FirstPersonNodeFloats]{};
                        float leftHands[kPacketNodes * kHalo2FirstPersonNodeFloats]{};
                        float baselineGun[kHalo2FirstPersonNodeFloats]{};
                        float leftGun[kHalo2FirstPersonNodeFloats]{};
                        std::memcpy(baselineHands, hands, sizeof(hands));
                        std::memcpy(leftHands, hands, sizeof(hands));
                        std::memcpy(baselineGun, gun, sizeof(gun));
                        std::memcpy(leftGun, gun, sizeof(gun));
                        Halo2FinalPacketOwnershipResult baselineResult{}, leftResult{};
                        Check(Halo2OwnFinalFirstPersonPackets(baselineHands, kPacketNodes, remap, binding, baselineGun,
                                  1, authoredRoot, right, left, twoHand, 2.0f, 0.5f, 2.0f, baselineResult) &&
                                  Halo2OwnFinalFirstPersonPackets(leftHands, kPacketNodes, remap, binding, leftGun, 1,
                                      authoredRoot, right, left, twoHand, 2.0f, 0.5f, 2.0f, leftResult, true),
                            "H2 anatomical routing supports Chief/Elite and free/support grip");
                        Check(std::memcmp(leftGun, baselineGun, sizeof(leftGun)) == 0,
                            "H2 anatomical routing never changes primary gun placement");
                        Check(anatomicalPalmsMatch(leftHands, baselineHands, rig),
                            "H2 anatomical palms preserve the complete primary and support grip frames");
                        Check(nearlyEqual(leftHands[13], baselineHands[26]) &&
                                  nearlyEqual(leftHands[26], baselineHands[13]),
                            "H2 size controls remain assigned to weapon roles after anatomical routing");
                        const int32_t invalidRemap[kPacketNodes]{0, 1, 2, 1};
                        float before[kPacketNodes * kHalo2FirstPersonNodeFloats]{};
                        std::memcpy(before, leftHands, sizeof(before));
                        Check(!Halo2RouteLeftHandedPacketHands(
                                  leftHands, kPacketNodes, invalidRemap, binding, invalidRemap, binding, right, left) &&
                                  std::memcmp(before, leftHands, sizeof(before)) == 0,
                            "H2 duplicate anatomical wrist rejects without partial palette mutation");
                    }
                {
                    float nativeHands[kPacketNodes * kHalo2FirstPersonNodeFloats]{};
                    float handedHands[kPacketNodes * kHalo2FirstPersonNodeFloats]{};
                    float primary[13]{}, secondary[13]{}, handedPrimary[13]{}, handedSecondary[13]{};
                    const int32_t secondaryMap[kPacketNodes]{-1, 1, -1, 3};
                    std::memcpy(nativeHands, hands, sizeof(hands));
                    std::memcpy(handedHands, hands, sizeof(hands));
                    identityNode(primary, 4.0f);
                    identityNode(secondary, -4.0f);
                    std::memcpy(handedPrimary, primary, sizeof(primary));
                    std::memcpy(handedSecondary, secondary, sizeof(secondary));
                    Halo2CameraBasis secondaryCarrier = left;
                    secondaryCarrier.forward[1] = -1.0f;
                    Halo2FinalPacketOwnershipResult a{}, b{};
                    Check(Halo2OwnDualFirstPersonPackets(nativeHands, kPacketNodes, remap, packetBinding, primary, 1,
                              secondaryMap, packetBinding, secondary, 1, authoredRoot, right, secondaryCarrier, 2, 0.5f,
                              1, a) &&
                              Halo2OwnDualFirstPersonPackets(handedHands, kPacketNodes, remap, packetBinding,
                                  handedPrimary, 1, secondaryMap, packetBinding, handedSecondary, 1, authoredRoot,
                                  right, secondaryCarrier, 2, 0.5f, 1, b, true),
                        "H2 left-handed dual presentation accepts partial secondary hand remaps");
                    Check(std::memcmp(primary, handedPrimary, sizeof(primary)) == 0 &&
                              std::memcmp(secondary, handedSecondary, sizeof(secondary)) == 0 &&
                              anatomicalPalmsMatch(handedHands, nativeHands, packetBinding.rigKind),
                        "H2 independent guns stay exact while anatomical hands follow their owners");
                    const float dx = handedHands[49] - handedHands[23];
                    const float dy = handedHands[50] - handedHands[24];
                    const float dz = handedHands[51] - handedHands[25];
                    Check(nearlyEqual(std::sqrt(dx * dx + dy * dy + dz * dz), 2.0f),
                        "H2 anatomical finger subtree retains shape at its destination role scale");
                }
                const bool ownedPackets = Halo2OwnFinalFirstPersonPackets(hands, kPacketNodes, remap, packetBinding,
                    gun, 1, authoredRoot, right, left, false, 2.0f, 0.5f, 1.0f, result);
                Halo2FirstPersonTransform movedLeft{}, chiefMarker{}, composedMarker{};
                const float chiefMarkerQuaternion[4] = {0.016078f, 0.073613f, 0.085080f, -0.993521f};
                chiefMarker.scale = 1.0f;
                chiefMarker.translation[0] = 0.022750f;
                chiefMarker.translation[1] = -0.008561f;
                chiefMarker.translation[2] = 0.000398f;
                const bool markerComposed =
                    Halo2ReadFirstPersonTransform(hands + 1 * kHalo2FirstPersonNodeFloats, movedLeft) &&
                    Halo2QuaternionToFirstPersonBasis(chiefMarkerQuaternion, chiefMarker.rotation) &&
                    Halo2ComposeFirstPersonTransforms(movedLeft, chiefMarker, composedMarker);
                Check(ownedPackets && result.applied && result.rightNodes == 1 && result.leftNodes == 2 &&
                          result.collapsedNodes == 1 && result.gunNodes == 1 && nearlyEqual(hands[0], 0.0001f) &&
                          nearlyEqual(hands[1 * kHalo2FirstPersonNodeFloats], 0.5f) &&
                          nearlyEqual(hands[2 * kHalo2FirstPersonNodeFloats], 2.0f) &&
                          nearlyEqual(hands[2 * kHalo2FirstPersonNodeFloats + 10], 10.0f) && markerComposed &&
                          nearlyEqual(composedMarker.translation[0], -10.0f) &&
                          nearlyEqual(composedMarker.translation[1], 0.0f) &&
                          nearlyEqual(composedMarker.translation[2], 0.0f) &&
                          nearlyEqual(composedMarker.rotation[0], 0.0f) &&
                          nearlyEqual(composedMarker.rotation[1], 1.0f) &&
                          nearlyEqual(composedMarker.rotation[3], -1.0f) &&
                          nearlyEqual(composedMarker.rotation[4], 0.0f) &&
                          nearlyEqual(composedMarker.rotation[8], 1.0f) && nearlyEqual(gun[0], 2.0f) &&
                          nearlyEqual(gun[10], 10.0f) && nearlyEqual(gun[11], 4.0f) &&
                          nearlyEqual(result.rightWristDeltaWorld, 8.0f) && result.leftWristDeltaWorld > 7.9f &&
                          result.leftWristDeltaWorld < 8.1f,
                    "C-H2-70 solves Chief's marker onto the accepted "
                    "forward-left-up controller hand frame");

                Halo2FirstPersonTransform stockEliteWrist{};
                stockEliteWrist.scale = 1.0f;
                stockEliteWrist.rotation[0] = stockEliteWrist.rotation[4] = stockEliteWrist.rotation[8] = 1.0f;
                Halo2FirstPersonTransform desiredEliteWrist{}, eliteMarker{}, composedEliteMarker{};
                const float eliteMarkerQuaternion[4] = {-0.001854f, 0.001501f, -0.001833f, -0.999995f};
                eliteMarker.scale = 1.0f;
                eliteMarker.translation[0] = 0.033088f;
                eliteMarker.translation[1] = -0.009315f;
                eliteMarker.translation[2] = 0.000442f;
                const bool eliteMarkerSolved =
                    Halo2BuildCorrectedRigMarkerFreeLeftTarget(
                        Halo2FirstPersonRigKind::Elite, left, stockEliteWrist, 1.0f, desiredEliteWrist) &&
                    Halo2QuaternionToFirstPersonBasis(eliteMarkerQuaternion, eliteMarker.rotation) &&
                    Halo2ComposeFirstPersonTransforms(desiredEliteWrist, eliteMarker, composedEliteMarker);
                Check(eliteMarkerSolved && nearlyEqual(composedEliteMarker.translation[0], -10.0f) &&
                          nearlyEqual(composedEliteMarker.translation[1], 0.0f) &&
                          nearlyEqual(composedEliteMarker.translation[2], 0.0f) &&
                          nearlyEqual(composedEliteMarker.rotation[0], 0.0f) &&
                          nearlyEqual(composedEliteMarker.rotation[1], 1.0f) &&
                          nearlyEqual(composedEliteMarker.rotation[3], -1.0f) &&
                          nearlyEqual(composedEliteMarker.rotation[4], 0.0f) &&
                          nearlyEqual(composedEliteMarker.rotation[8], 1.0f),
                    "C-H2-70 solves Arbiter's left_hand_elite marker onto the "
                    "same forward-left-up semantic controller hand frame");

                constexpr uint32_t kElitePacketNodes = 7;
                float eliteHands[kElitePacketNodes * kHalo2FirstPersonNodeFloats]{};
                for (uint32_t node = 0; node < kElitePacketNodes; ++node)
                    identityNode(eliteHands + node * kHalo2FirstPersonNodeFloats, static_cast<float>(node));
                eliteHands[1 * kHalo2FirstPersonNodeFloats + 10] = 100.0f;
                eliteHands[2 * kHalo2FirstPersonNodeFloats + 10] = 200.0f;
                eliteHands[3 * kHalo2FirstPersonNodeFloats + 10] = -2.0f;
                eliteHands[4 * kHalo2FirstPersonNodeFloats + 10] = 2.0f;
                eliteHands[5 * kHalo2FirstPersonNodeFloats + 10] = -3.0f;
                eliteHands[6 * kHalo2FirstPersonNodeFloats + 10] = 3.0f;
                float eliteGun[kHalo2FirstPersonNodeFloats]{};
                identityNode(eliteGun, 4.0f);
                const int32_t eliteRemap[kElitePacketNodes] = {0, 1, 2, 3, 4, 5, 6};
                Halo2FirstPersonArmBinding elitePacketBinding{};
                elitePacketBinding.valid = true;
                elitePacketBinding.leftWrist = 3;
                elitePacketBinding.rightWrist = 4;
                elitePacketBinding.count = kElitePacketNodes;
                elitePacketBinding.leftSubtree = (1ull << 3) | (1ull << 5);
                elitePacketBinding.rightSubtree = (1ull << 4) | (1ull << 6);
                elitePacketBinding.leftDirectChildren = 1ull << 5;
                elitePacketBinding.leftArmAncestors = 1ull << 1;
                elitePacketBinding.rightArmAncestors = 1ull << 2;
                elitePacketBinding.armAncestors = (1ull << 1) | (1ull << 2);
                elitePacketBinding.rigKind = Halo2FirstPersonRigKind::Elite;
                Halo2FinalPacketOwnershipResult elitePacketResult{};
                const bool elitePacketOwned =
                    Halo2OwnFinalFirstPersonPackets(eliteHands, kElitePacketNodes, eliteRemap, elitePacketBinding,
                        eliteGun, 1, authoredRoot, right, left, false, 1.0f, 1.0f, 2.0f, elitePacketResult);
                const float* const hiddenLeft = eliteHands + 1 * kHalo2FirstPersonNodeFloats;
                const float* const hiddenRight = eliteHands + 2 * kHalo2FirstPersonNodeFloats;
                const float* const visibleLeft = eliteHands + 3 * kHalo2FirstPersonNodeFloats;
                const float* const visibleRight = eliteHands + 4 * kHalo2FirstPersonNodeFloats;
                Check(elitePacketOwned && elitePacketResult.applied && elitePacketResult.leftNodes == 2 &&
                          elitePacketResult.rightNodes == 2 && elitePacketResult.collapsedNodes == 3 &&
                          elitePacketResult.coLocatedArmNodes == 2 &&
                          nearlyEqual(hiddenLeft[0], visibleLeft[0] * 0.0001f) &&
                          nearlyEqual(hiddenRight[0], visibleRight[0] * 0.0001f) &&
                          nearlyEqual(hiddenLeft[10], visibleLeft[10]) &&
                          nearlyEqual(hiddenLeft[11], visibleLeft[11]) &&
                          nearlyEqual(hiddenLeft[12], visibleLeft[12]) &&
                          nearlyEqual(hiddenRight[10], visibleRight[10]) &&
                          nearlyEqual(hiddenRight[11], visibleRight[11]) &&
                          nearlyEqual(hiddenRight[12], visibleRight[12]),
                    "C-H2-70 co-locates Elite's hidden arm influences at their "
                    "solved wrists before collapse, preventing skinning ribbons");

                identityNode(hands + 0 * kHalo2FirstPersonNodeFloats, 0.0f);
                identityNode(hands + 1 * kHalo2FirstPersonNodeFloats, -2.0f);
                identityNode(hands + 2 * kHalo2FirstPersonNodeFloats, 2.0f);
                identityNode(hands + 3 * kHalo2FirstPersonNodeFloats, -3.0f);
                identityNode(gun, 4.0f);
                Halo2FirstPersonTransform visibleRightDelta{};
                Halo2FinalPacketOwnershipResult visibleHands{};
                Halo2FinalPacketOwnershipResult visibleGun{};
                // The legacy visible-consumer path maps the render
                // camera's frame onto the carrier; its expectations assume the
                // camera looks down the carrier's own +Y.
                Halo2CameraBasis legacyCamera{};
                legacyCamera.forward[1] = 1.0f;
                legacyCamera.up[2] = 1.0f;
                const bool ownedVisibleHands = Halo2OwnVisibleFirstPersonHands(hands, kPacketNodes, remap,
                    packetBinding, legacyCamera, right, left, false, 2.0f, 0.5f, 1.0f, visibleRightDelta, visibleHands);
                const bool ownedVisibleGun =
                    ownedVisibleHands && Halo2OwnVisibleFirstPersonGun(gun, 1, visibleRightDelta, visibleGun);
                Check(ownedVisibleHands && ownedVisibleGun && visibleHands.rightNodes == 1 &&
                          visibleHands.leftNodes == 2 && visibleHands.collapsedNodes == 1 && visibleGun.gunNodes == 1 &&
                          nearlyEqual(hands[1 * kHalo2FirstPersonNodeFloats + 10], -10.0f) &&
                          nearlyEqual(hands[2 * kHalo2FirstPersonNodeFloats + 10], 10.0f) &&
                          nearlyEqual(gun[10], 14.0f),
                    "C-H2-65 restores C-H2-63's sequential registered-consumer "
                    "hands and exact right-wrist held-model delta");

                float classicEye[kHalo2FirstPersonNodeFloats]{};
                identityNode(classicEye, 1.0f);
                Halo2FirstPersonPassCameras classicPass{};
                classicPass.frameValid = true;
                classicPass.compensate = true;
                classicPass.correct.position[0] = -0.03f;
                classicPass.correct.forward[1] = 1.0f;
                classicPass.correct.up[2] = 1.0f;
                classicPass.viewing = classicPass.correct;
                classicPass.viewing.position[0] = 0.03f;
                const bool classicCompensated = Halo2CompensateClassicFirstPersonEye(classicEye, 1, classicPass);
                Check(classicCompensated && nearlyEqual(classicEye[0], 1.0f) && nearlyEqual(classicEye[1], 1.0f) &&
                          nearlyEqual(classicEye[5], 1.0f) && nearlyEqual(classicEye[9], 1.0f) &&
                          nearlyEqual(classicEye[10], 1.06f) &&
                          nearlyEqual(
                              classicEye[10] - classicPass.viewing.position[0], 1.0f - classicPass.correct.position[0]),
                    "C-H2-63 moves a Classic persistent packet so its stale "
                    "viewing camera produces the correct eye-space geometry");

                float invalidClassic[kHalo2FirstPersonNodeFloats]{};
                identityNode(invalidClassic, 2.0f);
                invalidClassic[6] = std::numeric_limits<float>::quiet_NaN();
                float invalidClassicBefore[kHalo2FirstPersonNodeFloats]{};
                std::memcpy(invalidClassicBefore, invalidClassic, sizeof(invalidClassic));
                // Automatic barrel alignment. Whatever the
                // authored root frame says, the stock barrel line (the
                // compose camera's forward) must land exactly on the aim ray.
                {
                    Halo2FirstPersonTransform wrist{}, root{};
                    wrist.scale = 1.0f;
                    root.scale = 1.0f;
                    // identity wrist at a grip offset; root frame tipped 12
                    // degrees so its +X is NOT the camera forward
                    wrist.rotation[0] = wrist.rotation[4] = wrist.rotation[8] = 1.0f;
                    wrist.translation[0] = 0.4f;
                    wrist.translation[2] = -0.1f;
                    const float t = 12.0f * 0.0174533f;
                    root.rotation[0] = std::cos(t);
                    root.rotation[2] = -std::sin(t);
                    root.rotation[4] = 1.0f;
                    root.rotation[6] = std::sin(t);
                    root.rotation[8] = std::cos(t);
                    Halo2CameraBasis compose{};
                    compose.forward[0] = 1.0f; // stock barrel = +X
                    compose.up[2] = 1.0f;
                    Halo2CameraBasis carrierInner{};
                    carrierInner.forward[0] = 0.6f;
                    carrierInner.forward[1] = 0.8f; // aim ray
                    carrierInner.up[2] = 1.0f;
                    carrierInner.position[1] = 2.0f;
                    Halo2FirstPersonTransform delta{}, desired{};
                    Check(Halo2BuildAuthoredBarrelDelta(wrist, root, compose, carrierInner, 1.0f, delta, desired),
                        "C-H2-86 barrel delta builds");
                    float barrel[3] = {0.0f, 0.0f, 0.0f};
                    for (int c = 0; c < 3; ++c)
                        for (int r = 0; r < 3; ++r)
                            barrel[r] += delta.rotation[c * 3 + r] * compose.forward[c];
                    const float dot = barrel[0] * carrierInner.forward[0] + barrel[1] * carrierInner.forward[1] +
                                      barrel[2] * carrierInner.forward[2];
                    // The swing is disabled (headset: it tilted Anniversary);
                    // the mount must again map the root +X onto the ray
                    // exactly, with no dependence on the compose camera.
                    float rootX[3] = {0.0f, 0.0f, 0.0f};
                    const float unitX[3] = {1.0f, 0.0f, 0.0f};
                    for (int c = 0; c < 3; ++c)
                        for (int r = 0; r < 3; ++r)
                            rootX[r] += delta.rotation[c * 3 + r] *
                                        (root.rotation[0 * 3 + c] * unitX[0] + root.rotation[1 * 3 + c] * unitX[1] +
                                            root.rotation[2 * 3 + c] * unitX[2]);
                    (void)dot;
                    const float rootDot = rootX[0] * carrierInner.forward[0] + rootX[1] * carrierInner.forward[1] +
                                          rootX[2] * carrierInner.forward[2];
                    Check(rootDot > 0.9999f, "C-H2-87 the authored root +X lands exactly on the aim "
                                             "ray (C-H2-86 swing disabled)");
                    Check(std::fabs(desired.translation[1] - 2.0f) < 1e-4f && std::fabs(desired.translation[0]) < 1e-4f,
                        "C-H2-86 the wrist still pivots at the controller");
                }
                // The Halo 2 Classic-only mount trim.
                {
                    Halo2CameraBasis base{};
                    base.forward[1] = 1.0f; // +Y forward
                    base.up[2] = 1.0f;      // +Z up
                    base.position[0] = 1.0f;
                    Halo2CameraBasis out{};
                    Check(Halo2ApplyCarrierTrim(base, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.33f, out) &&
                              Halo2CameraBasesNearlyEqual(out, base),
                        "C-H2-85 all-zero trim leaves the carrier identical");
                    // +90 deg pitch about the carrier's right axis takes
                    // forward from +Y to +Z (muzzle up).
                    Check(Halo2ApplyCarrierTrim(base, 90.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.33f, out) &&
                              std::fabs(out.forward[2] - 1.0f) < 1e-4f && std::fabs(out.forward[1]) < 1e-4f,
                        "C-H2-85 pitch raises the muzzle about the gun's own "
                        "right axis");
                    // Offsets ride the carrier's own axes and scale to world.
                    Check(Halo2ApplyCarrierTrim(base, 0.0f, 0.0f, 0.0f, 2.0f, 0.0f, 0.0f, 0.5f, out) &&
                              std::fabs(out.position[1] - 1.0f) < 1e-4f && std::fabs(out.position[0] - 1.0f) < 1e-4f,
                        "C-H2-85 forward offset moves along the gun's own "
                        "forward axis, scaled to world units");
                    Halo2CameraBasis invalid{};
                    Check(!Halo2ApplyCarrierTrim(invalid, 5.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.33f, out) &&
                              !Halo2ApplyCarrierTrim(base, std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 0.0f,
                                  0.0f, 0.0f, 0.33f, out),
                        "C-H2-85 refuses an invalid basis or trim");
                }
                // The depth guard. Parity is the floor -
                // the compensation may run only when it cannot rotate the
                // rig and cannot move it further than one half-IPD.
                Halo2CameraBasis correctEye{};
                correctEye.position[0] = 0.03f;
                correctEye.forward[1] = 1.0f;
                correctEye.up[2] = 1.0f;
                Halo2CameraBasis sameOrientation = correctEye;
                sameOrientation.position[0] = 0.0f; // the pair centre
                Check(Halo2ClassicEyeCompensationAdmissible(correctEye, sameOrientation, 0.045f),
                    "C-H2-81 admits a half-IPD viewing camera that shares the "
                    "eye's orientation - the parallax it must restore");
                Halo2CameraBasis farAway = sameOrientation;
                farAway.position[0] = -0.40f;
                Check(!Halo2ClassicEyeCompensationAdmissible(correctEye, farAway, 0.045f),
                    "C-H2-81 refuses a viewing camera beyond one half-IPD, so "
                    "no measurement can displace the rig");
                Halo2CameraBasis rotated = sameOrientation;
                rotated.forward[0] = 0.05f;
                rotated.forward[1] = 0.9987f;
                Check(!Halo2ClassicEyeCompensationAdmissible(correctEye, rotated, 0.045f),
                    "C-H2-81 refuses any viewing camera whose orientation "
                    "differs, so no measurement can rotate the rig");
                Halo2CameraBasis invalidBasis{};
                Check(!Halo2ClassicEyeCompensationAdmissible(correctEye, invalidBasis, 0.045f) &&
                          !Halo2ClassicEyeCompensationAdmissible(correctEye, sameOrientation, 0.0f) &&
                          !Halo2ClassicEyeCompensationAdmissible(
                              correctEye, sameOrientation, std::numeric_limits<float>::quiet_NaN()),
                    "C-H2-81 refuses an invalid basis or bound and falls back "
                    "to Anniversary parity");
                Check(!Halo2CompensateClassicFirstPersonEye(invalidClassic, 1, classicPass) &&
                          std::memcmp(invalidClassic, invalidClassicBefore, sizeof(invalidClassic)) == 0,
                    "C-H2-63 refuses a non-finite Classic eye packet without "
                    "partially changing it");

                float unchangedInner[kPacketNodes * kHalo2FirstPersonNodeFloats]{};
                std::memcpy(unchangedInner, hands, sizeof(unchangedInner));
                const int32_t badRemap[kPacketNodes] = {0, 1, 99, 3};
                Halo2FinalPacketOwnershipResult refusedPacket{};
                Check(!Halo2OwnFinalFirstPersonPackets(hands, kPacketNodes, badRemap, packetBinding, gun, 1,
                          authoredRoot, right, left, false, 1.0f, 1.0f, 1.0f, refusedPacket) &&
                          std::memcmp(unchangedInner, hands, sizeof(unchangedInner)) == 0,
                    "C-H2-60 refuses an invalid authored remap before mutating "
                    "any final hand packet node");

                identityNode(hands + 0 * kHalo2FirstPersonNodeFloats, 0.0f);
                identityNode(hands + 1 * kHalo2FirstPersonNodeFloats, -2.0f);
                identityNode(hands + 2 * kHalo2FirstPersonNodeFloats, 2.0f);
                identityNode(hands + 3 * kHalo2FirstPersonNodeFloats, -3.0f);
                identityNode(gun, 4.0f);
                Halo2FinalPacketOwnershipResult supported{};
                const bool ownedSupport = Halo2OwnFinalFirstPersonPackets(hands, kPacketNodes, remap, packetBinding,
                    gun, 1, authoredRoot, right, left, true, 1.0f, 1.0f, 2.0f, supported);
                Check(ownedSupport && supported.applied &&
                          nearlyEqual(hands[1 * kHalo2FirstPersonNodeFloats + 10], 10.0f) &&
                          nearlyEqual(hands[1 * kHalo2FirstPersonNodeFloats + 11], -4.0f) &&
                          nearlyEqual(hands[2 * kHalo2FirstPersonNodeFloats + 10], 10.0f) &&
                          nearlyEqual(hands[3 * kHalo2FirstPersonNodeFloats + 10], 10.0f) &&
                          nearlyEqual(hands[3 * kHalo2FirstPersonNodeFloats + 11], -5.0f) &&
                          nearlyEqual(gun[10], 10.0f) && nearlyEqual(gun[11], 2.0f),
                    "C-H2-70 preserves the authored support grip under the "
                    "barrel-to-crosshair rigid delta");
            }
            {
                float wrist[kHalo2FirstPersonNodeFloats]{};
                wrist[0] = 1.0f;
                wrist[1] = wrist[5] = wrist[9] = 1.0f;
                wrist[10] = 3.0f;
                wrist[11] = -2.0f;
                wrist[12] = 7.0f;
                Halo2CameraBasis desired{};
                desired.position[0] = -4.0f;
                desired.position[1] = 9.0f;
                desired.position[2] = 2.0f;
                desired.forward[0] = 1.0f;
                desired.up[2] = 1.0f;
                float rotation[9]{}, stockPosition[3]{}, desiredPosition[3]{};
                const bool built =
                    Halo2BuildFinalPaletteWristDelta(wrist, desired, rotation, stockPosition, desiredPosition);
                if (built)
                    Halo2ReanchorFirstPersonNode(wrist, rotation, stockPosition, desiredPosition);
                Check(built && std::fabs(wrist[1]) < 1.0e-6f && std::fabs(wrist[2] + 1.0f) < 1.0e-6f &&
                          std::fabs(wrist[3]) < 1.0e-6f && std::fabs(wrist[4] - 1.0f) < 1.0e-6f &&
                          std::fabs(wrist[5]) < 1.0e-6f && std::fabs(wrist[6]) < 1.0e-6f &&
                          std::fabs(wrist[7]) < 1.0e-6f && std::fabs(wrist[8]) < 1.0e-6f &&
                          std::fabs(wrist[9] - 1.0f) < 1.0e-6f && std::fabs(wrist[10] + 4.0f) < 1.0e-6f &&
                          std::fabs(wrist[11] - 9.0f) < 1.0e-6f && std::fabs(wrist[12] - 2.0f) < 1.0e-6f,
                    "C-H2-50 aligns a final world-space wrist to a turned "
                    "controller without a facing-forward special case");
            }

            // Camera-local controller placement must be invariant under a
            // common world/body turn. An earlier version accidentally used C*H^T (a
            // world-space delta) where these camera-relative nodes need H^T*C;
            // the two agree facing forward and diverge after turning.
            {
                Halo2CameraBasis facing{};
                facing.forward[0] = 1.0f;
                facing.up[2] = 1.0f;
                Halo2CameraBasis controllerA = facing;
                controllerA.forward[0] = 0.0f;
                controllerA.forward[1] = 1.0f;
                controllerA.position[0] = 1.0f;
                controllerA.position[1] = 2.0f;
                controllerA.position[2] = 3.0f;
                Halo2CameraBasis turnedFacing = facing;
                turnedFacing.forward[0] = 0.0f;
                turnedFacing.forward[1] = 1.0f;
                Halo2CameraBasis controllerB = controllerA;
                controllerB.forward[0] = -1.0f;
                controllerB.forward[1] = 0.0f;
                controllerB.position[0] = -2.0f;
                controllerB.position[1] = 1.0f;
                float rotationA[9]{}, translationA[3]{};
                float rotationB[9]{}, translationB[3]{};
                bool invariant = Halo2ComputeCarrierDelta(facing, controllerA, rotationA, translationA) &&
                                 Halo2ComputeCarrierDelta(turnedFacing, controllerB, rotationB, translationB);
                for (int index = 0; index < 9 && invariant; ++index)
                    invariant = nearlyEqual(rotationA[index], rotationB[index]);
                for (int index = 0; index < 3 && invariant; ++index)
                    invariant = nearlyEqual(translationA[index], translationB[index]);
                Check(invariant, "Halo 2 controller tracking stays camera-local after a "
                                 "common body/camera turn instead of only facing forward");
            }

            {
                Halo2CameraBasis body{};
                body.position[0] = 10.0f;
                body.position[1] = 20.0f;
                body.position[2] = 30.0f;
                body.forward[0] = 1.0f;
                body.up[2] = 1.0f;
                const float identityQ[4] = {0.0f, 0.0f, 0.0f, 1.0f};
                const float recenterPosition[3] = {0.2f, 0.1f, -0.1f};
                const float controllerPositionInner[3] = {0.6f, 0.3f, -0.7f};
                Halo2CameraBasis stable{};
                Check(Halo2BuildStableControllerCarrier(
                          body, identityQ, recenterPosition, identityQ, controllerPositionInner, 0.5f, 0.2f, stable) &&
                          nearlyEqual(stable.position[0], 10.4f) && nearlyEqual(stable.position[1], 19.8f) &&
                          nearlyEqual(stable.position[2], 30.1f) && nearlyEqual(stable.forward[0], 1.0f) &&
                          nearlyEqual(stable.up[2], 1.0f),
                    "C-H2-60 maps controller displacement from the recenter "
                    "point onto the pre-HMD body camera and applies forward "
                    "trim once");

                float mirroredYaw[4]{};
                float mirroredRoll[4]{};
                Check(Halo2BuildMirroredLeftAimOrientation(identityQ, 30.0f, 0.0f, 0.0f, mirroredYaw) &&
                          Halo2BuildMirroredLeftAimOrientation(identityQ, 0.0f, 0.0f, 20.0f, mirroredRoll) &&
                          nearlyEqual(mirroredYaw[1], -std::sin(15.0f * 0.01745329251994329577f)) &&
                          nearlyEqual(mirroredYaw[3], std::cos(15.0f * 0.01745329251994329577f)) &&
                          nearlyEqual(mirroredRoll[2], std::sin(10.0f * 0.01745329251994329577f)) &&
                          nearlyEqual(mirroredRoll[3], std::cos(10.0f * 0.01745329251994329577f)),
                    "C-H2-60 mirrors configured yaw and roll on the independent "
                    "left hand while leaving right aim's trim single-applied");
            }

            // The carrier's whole point: mesh, VR crosshair and bullet share ONE
            // carrier. A shot leaving the carrier's own origin must therefore
            // travel exactly along the carrier's forward - the direction the
            // drawn gun points and the crosshair sits on - at any range.
            {
                Halo2CameraBasis oneCarrier = yawedCarrier;
                oneCarrier.position[0] = 3.0f;
                oneCarrier.position[1] = -2.0f;
                oneCarrier.position[2] = 7.0f;
                float alongGun[3]{};
                bool agrees = true;
                for (float range : {2.0f, 10.0f, 50.0f})
                {
                    if (!Halo2BuildControllerShotDirection(oneCarrier.position, oneCarrier, range, alongGun))
                    {
                        agrees = false;
                        break;
                    }
                    for (int axis = 0; axis < 3; ++axis)
                        if (!nearlyEqual(alongGun[axis], oneCarrier.forward[axis]))
                            agrees = false;
                }
                Check(agrees, "C-H2-46 sends the bullet along the same carrier forward "
                              "the hands, gun mesh and VR crosshair are built from");
            }

            const float shotOrigin[3] = {0.0f, 1.0f, 0.0f};
            Halo2CameraBasis shotCarrier = yawedCarrier;
            std::memcpy(shotCarrier.position, shotOrigin, sizeof(shotCarrier.position));
            float shotDirection[3]{};
            Check(Halo2BuildControllerShotDirection(shotOrigin, shotCarrier, 10.0f, shotDirection) &&
                      nearlyEqual(shotDirection[0], 0.0f) && nearlyEqual(shotDirection[1], 1.0f) &&
                      nearlyEqual(shotDirection[2], 0.0f),
                "C-H2-43 preserves the authored projectile origin and aims "
                "its direction at the controller ray without a camera input");

            const float offsetShotOrigin[3] = {1.0f, 0.0f, 0.0f};
            Halo2CameraBasis reticleCarrier = yawedCarrier;
            reticleCarrier.position[0] = 0.0f;
            reticleCarrier.position[1] = 0.0f;
            reticleCarrier.position[2] = 0.0f;
            Check(Halo2BuildControllerShotDirection(offsetShotOrigin, reticleCarrier, 10.0f, shotDirection) &&
                      nearlyEqual(shotDirection[0], -1.0f / std::sqrt(101.0f)) &&
                      nearlyEqual(shotDirection[1], 10.0f / std::sqrt(101.0f)) && nearlyEqual(shotDirection[2], 0.0f),
                "C-H2-44 converges an authored barrel origin on the exact "
                "presented VR-crosshair point instead of merely copying its ray");
            const float invalidShotOrigin[3] = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
            Check(!Halo2BuildControllerShotDirection(invalidShotOrigin, shotCarrier, 10.0f, shotDirection),
                "C-H2-43 rejects a non-finite shot origin locally");
            Check(Halo2ShouldAttemptDirectShotOwnership(true, true, true, true, 0) &&
                      Halo2ShouldAttemptDirectShotOwnership(true, true, true, true, 1) &&
                      !Halo2ShouldAttemptDirectShotOwnership(false, true, true, true, 0) &&
                      !Halo2ShouldAttemptDirectShotOwnership(true, false, true, true, 1),
                "C-H2-56 gives both stock useUnitAim modes the same guarded "
                "local-player crosshair-shot ownership policy");

            static Halo2FirstPersonSlotCache controllerCache{};
            controllerCache = Halo2FirstPersonSlotCache{};
            float node[13]{};
            node[0] = 1.0f;
            node[1] = node[5] = node[9] = 1.0f;
            node[10] = 0.4f;
            node[11] = 0.1f;
            node[12] = -0.2f;
            Halo2FirstPersonReanchorResult placed{};
            Check(Halo2PlaceFirstPersonSlotOnController(node, 1, head, carrier, 0.5f, controllerCache, placed) &&
                      placed.applied && !placed.fromCache &&
                      placed.space == Halo2FirstPersonNodeSpace::CameraRelative && nearlyEqual(node[0], 0.5f) &&
                      nearlyEqual(node[10], 0.7f) && nearlyEqual(node[11], 1.65f) && nearlyEqual(node[12], 0.9f),
                "C-H2-41 moves and scales the complete native camera-relative "
                "first-person assembly as one rigid controller-owned carrier");
            Check(Halo2PlaceFirstPersonSlotOnController(node, 1, head, carrier, 0.5f, controllerCache, placed) &&
                      placed.fromCache && nearlyEqual(node[10], 0.7f) && nearlyEqual(node[11], 1.65f),
                "C-H2-41 repeated renderer reads are idempotent rather than "
                "accumulating the controller transform");
            Halo2FirstPersonSlotCache yawedCache{};
            float yawedNode[13]{};
            yawedNode[0] = 1.0f;
            yawedNode[1] = yawedNode[5] = yawedNode[9] = 1.0f;
            Check(Halo2PlaceFirstPersonSlotOnController(yawedNode, 1, head, yawedCarrier, 1.0f, yawedCache, placed) &&
                      std::fabs(yawedNode[1]) < 1.0e-4f && nearlyEqual(yawedNode[2], 1.0f),
                "C-H2-41 applies the full controller carrier rotation to the "
                "native node axes");
            float invalidNode[13]{};
            invalidNode[0] = std::numeric_limits<float>::quiet_NaN();
            Check(!Halo2PlaceFirstPersonSlotOnController(invalidNode, 1, head, carrier, 1.0f, controllerCache, placed),
                "C-H2-41 rejects a non-finite native palette locally");
            Check(kHalo2WeaponAimHelperRva == 0x008F0F70 && sizeof(kHalo2WeaponAimHelperEntryBytes) == 22 &&
                      kHalo2WeaponAimHelperEntryBytes[0] == 0x48 && kHalo2WeaponAimHelperEntryBytes[21] == 0x00 &&
                      kHalo2PlayerUserIteratorRva == 0x006A1D50 && kHalo2PlayerUpdateRva == 0x006A3910 &&
                      kHalo2PlayersGlobalsPointerRva == 0x00E80A20 && kHalo2PlayersDataArrayPointerRva == 0x00E80A28 &&
                      kHalo2PlayerUserMappingOffset == 0x0C && kHalo2DataArrayStorageOffset == 0x48 &&
                      kHalo2PlayerDatumStride == 0x224 && kHalo2PlayerUnitIndexOffset == 0x2C &&
                      kHalo2MaximumPlayers == 16,
                "E-H2-37 pins the unique H2EK-matched firing-only helper "
                "and its output-user-0 player-unit guard used by C-H2-43");
        }

        // Main-view flags and Saber matrix matching.
        {
            Check(Halo2SaberViewRecordIsMainView(0) && Halo2SaberViewRecordIsMainView(0x100) &&
                      !Halo2SaberViewRecordIsMainView(0x8) && !Halo2SaberViewRecordIsMainView(0x40) &&
                      !Halo2SaberViewRecordIsMainView(0x4),
                "E-H2-21 a record with any of flags 0x4C is not a main view");
            float a[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1.0f, 2.0f, 3.0f, 1};
            float b[16]{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 1.0f, 2.0f, 3.0f, 1};
            Check(Halo2SaberViewMatricesMatch(a, b, 2.0e-3f, 0.02f), "E-H2-21 identical matrices match");
            b[12] += 0.015f;
            Check(Halo2SaberViewMatricesMatch(a, b, 2.0e-3f, 0.02f),
                "E-H2-21 a 15 mm translation difference is within tolerance");
            b[12] += 0.02f;
            Check(!Halo2SaberViewMatricesMatch(a, b, 2.0e-3f, 0.02f), "E-H2-21 a 35 mm translation difference is not");
            b[12] = a[12];
            b[5] += 0.01f;
            Check(
                !Halo2SaberViewMatricesMatch(a, b, 2.0e-3f, 0.02f), "E-H2-21 a rotation row difference of 0.01 is not");
            Check(!Halo2SaberViewMatricesMatch(nullptr, b, 2.0e-3f, 0.02f), "E-H2-21 null never matches");
        }

        // The classic first-person FOV constant is the same 49.594
        // degrees Saber bakes, stored as radians.
        {
            float stock = 0.0f;
            const uint32_t bits = kHalo2ClassicFirstPersonFovStockBits;
            std::memcpy(&stock, &bits, sizeof(stock));
            Check(std::fabs(stock * 180.0f / pi - kHalo2SaberFirstPersonVerticalFovDegrees) < 0.01f,
                "E-H2-20 0x3F5D9734 is 49.594 degrees in radians, the Saber "
                "first-person constant");
        }

        // The renderer switch guard decides from the request, the
        // applied mode and whether a switch input is present.
        {
            Halo2SwitchInputEvidence none{};
            Check(!Halo2SwitchInputPresent(none), "E-H2-19 no Tab, no pad, no fed Back: no switch input");
            Halo2SwitchInputEvidence tab{};
            tab.tabDown = true;
            Check(Halo2SwitchInputPresent(tab), "E-H2-19 keyboard Tab is a switch input");
            Halo2SwitchInputEvidence padBlocked{};
            padBlocked.physicalBackDown = true;
            padBlocked.physicalBackPassThrough = false;
            Check(!Halo2SwitchInputPresent(padBlocked), "E-H2-19 the physical pad's Back is swallowed unless it passes "
                                                        "through");
            padBlocked.physicalBackPassThrough = true;
            Check(Halo2SwitchInputPresent(padBlocked), "E-H2-19 the physical pad's Back with pass-through is a switch "
                                                       "input");
            Halo2SwitchInputEvidence fed{};
            fed.virtualBackAgeMs = 100;
            Check(Halo2SwitchInputPresent(fed), "E-H2-19 the mod's own Back fed 100 ms ago is a switch input");
            fed.virtualBackAgeMs = 900;
            Check(!Halo2SwitchInputPresent(fed), "E-H2-19 a Back fed 900 ms ago is no longer behind a switch");
            Check(Halo2DecideRenderModeSwitch(1, 1, false) == Halo2RenderModeSwitchDecision::NoChange,
                "E-H2-19 request == applied: nothing to decide");
            Check(Halo2DecideRenderModeSwitch(0, 1, true) == Halo2RenderModeSwitchDecision::Honour,
                "E-H2-19 a switch with an input behind it is honoured");
            Check(Halo2DecideRenderModeSwitch(0, 1, false) == Halo2RenderModeSwitchDecision::Suppress,
                "E-H2-19 a switch with nothing behind it is suppressed");
            Check(Halo2DecideRenderModeSwitch(1, 0, false) == Halo2RenderModeSwitchDecision::Suppress,
                "E-H2-19 suppression works in both directions");
        }

        // The Saber view record's second, first-person projection.
        {
            const float deg = pi / 180.0f;
            // 0x1C6D80's literal 0x424660D5 = 49.594 deg vertical; the
            // horizontal follows the record aspect (h/w 0.721 in the logs).
            Halo2SymmetricHalfFovs stockFp{};
            Check(Halo2SaberFirstPersonStockHalfFovs(0.721f, stockFp) &&
                      std::fabs(stockFp.vertical - 24.797f * deg) < 1.0e-4f &&
                      std::fabs(stockFp.horizontal - 32.65f * deg) < 0.05f * deg,
                "E-H2-18 the engine's stock first-person frustum is 65.3 x "
                "49.6 deg at the record aspect");
            Check(!Halo2SaberFirstPersonStockHalfFovs(0.0f, stockFp) &&
                      !Halo2SaberFirstPersonStockHalfFovs(-1.0f, stockFp),
                "E-H2-18 a degenerate aspect is refused");
            // At the 126.5 x 110.1 deg eye cover the weapon is drawn 3.1x
            // larger than the world: tan(55.05) / tan(24.797).
            const float magnification = std::tan(55.05f * deg) / std::tan(24.797f * deg);
            Check(magnification > 3.0f && magnification < 3.2f,
                "E-H2-18 the stock first-person projection magnifies the weapon "
                "about 3.1x against the eye's world");
            float world[16]{};
            world[0] = 1.0f / std::tan(63.25f * deg);
            world[5] = 1.0f / std::tan(55.05f * deg);
            world[11] = 1.0f;
            float firstPerson[16]{};
            firstPerson[0] = 1.0f / std::tan(32.65f * deg);
            firstPerson[5] = 1.0f / std::tan(24.797f * deg);
            firstPerson[11] = 1.0f;
            Check(!Halo2SaberProjectionScalesMatch(firstPerson, world, 0.01f),
                "E-H2-18 the stock first-person projection does not match the "
                "world projection");
            float unified[16]{};
            std::memcpy(unified, world, sizeof(unified));
            Check(Halo2SaberProjectionScalesMatch(unified, world, 0.01f),
                "E-H2-18 the unified first-person projection matches the world");
            unified[5] *= 1.02f;
            Check(!Halo2SaberProjectionScalesMatch(unified, world, 0.01f),
                "E-H2-18 a 2 percent scale difference is a mismatch at 1 percent");
            Check(!Halo2SaberProjectionScalesMatch(nullptr, world, 0.01f), "E-H2-18 a null projection never matches");
            Halo2ObserverPosePublication publication{};
            Check(!publication.snapshot.valid, "E-H2-18 a fresh publication carries no sample");
            publication.generation = 7;
            publication.serial = 41;
            publication.snapshot.valid = true;
            publication.snapshot.rightAimValid = true;
            publication.snapshot.leftControllerValid = true;
            Check(Halo2ObserverControllerSnapshotUsable(publication, 7) &&
                      !Halo2ObserverControllerSnapshotUsable(publication, 8),
                "C-H2-60 admits the controller snapshot carried by the observer "
                "publication without comparing it to a newer independently "
                "sampled VR serial, but still rejects another generation");
        }

        // The crop follows the projection the engine built, read
        // back as its two diagonal scales.
        {
            const float deg = pi / 180.0f;
            // Classic 0x7DF7A0 for a logged cover: 110.2 deg
            // vertical on a 2912x2100 raster gives P+0x8C = 1/tan(55.1) and
            // P+0x78 = 1/(1.3867 * tan(55.1)), i.e. 63.3 deg horizontal.
            const float tanY = std::tan(55.1f * deg);
            Halo2SymmetricHalfFovs classic{};
            Check(Halo2HalfFovsFromProjectionScales(1.0f / (2912.0f / 2100.0f * tanY), 1.0f / tanY, classic) &&
                      std::fabs(classic.vertical - 55.1f * deg) < 1.0e-4f &&
                      std::fabs(classic.horizontal - 63.3f * deg) < 0.05f * deg,
                "E-H2-12 the classic projection scales give back the cover "
                "that built them");
            // Saber 0x1C82C0 at the written degrees 108.1 x 110.1;
            // sign does not matter (reversed-Z builders negate).
            Halo2SymmetricHalfFovs saber{};
            Check(Halo2HalfFovsFromProjectionScales(
                      -1.0f / std::tan(54.05f * deg), 1.0f / std::tan(55.05f * deg), saber) &&
                      std::fabs(saber.horizontal - 54.05f * deg) < 1.0e-4f &&
                      std::fabs(saber.vertical - 55.05f * deg) < 1.0e-4f,
                "E-H2-12 the Saber projection scales give back the written "
                "degrees regardless of sign");
            // The stock Anniversary frame (100.9 x 82.3 deg) must NOT agree
            // with that cover: this is the disagreement the log names.
            Halo2SymmetricHalfFovs stock{};
            Check(Halo2HalfFovsFromProjectionScales(
                      1.0f / std::tan(50.45f * deg), 1.0f / std::tan(41.15f * deg), stock) &&
                      !Halo2HalfFovsAgree(stock, saber, kHalo2ProjectionReadbackToleranceRadians) &&
                      Halo2HalfFovsAgree(saber, saber, kHalo2ProjectionReadbackToleranceRadians) &&
                      !Halo2HalfFovsAgree(saber, saber, -1.0f),
                "E-H2-12 a stock-FOV render is a mismatch against the written "
                "cover; half a degree is the tolerance");
            Halo2SymmetricHalfFovs untouchedInner{0.5f, 0.5f};
            Check(
                !Halo2HalfFovsFromProjectionScales(0.0f, 1.0f, untouchedInner) &&
                    !Halo2HalfFovsFromProjectionScales(1.0f, 0.0f, untouchedInner) &&
                    !Halo2HalfFovsFromProjectionScales(std::numeric_limits<float>::quiet_NaN(), 1.0f, untouchedInner) &&
                    !Halo2HalfFovsFromProjectionScales(5.0e-4f, 1.0f, untouchedInner) &&
                    !Halo2HalfFovsFromProjectionScales(200.0f, 1.0f, untouchedInner) &&
                    untouchedInner.horizontal == 0.5f && untouchedInner.vertical == 0.5f,
                "E-H2-12 a zero, NaN, near-orthographic or near-zero-angle "
                "scale is refused without touching the caller's value");
            // The popped raster context: depth after render_view returned,
            // plus one, inside the three-slot stack at 0x1996D30.
            uint32_t slot = 0;
            Check(Halo2ClassicPoppedRasterContextSlot(-1, slot) && slot == 0x01996D30u &&
                      Halo2ClassicPoppedRasterContextSlot(0, slot) && slot == 0x01996D30u + 0x318u &&
                      Halo2ClassicPoppedRasterContextSlot(1, slot) && slot == 0x01996D30u + 2u * 0x318u,
                "E-H2-12 the popped raster context is slot depth+1");
            slot = 7;
            Check(!Halo2ClassicPoppedRasterContextSlot(2, slot) && !Halo2ClassicPoppedRasterContextSlot(-2, slot) &&
                      !Halo2ClassicPoppedRasterContextSlot(INT32_MAX, slot) && slot == 7,
                "E-H2-12 a depth outside the three-slot stack is refused");
        }

        constexpr uint32_t sameFrameGeneration = 17;
        constexpr uint64_t sameFrameSerial = 41;
        Halo2SameFramePairProof sameFrame{};
        sameFrame.generation = sameFrameGeneration;
        sameFrame.preparedSerial = sameFrameSerial;
        sameFrame.attemptToken = 9;
        sameFrame.eyeAttemptToken[0] = sameFrame.attemptToken;
        sameFrame.eyeAttemptToken[1] = sameFrame.attemptToken;
        sameFrame.renderSerial[0] = sameFrameSerial;
        sameFrame.renderSerial[1] = sameFrameSerial;
        sameFrame.captureSerial[0] = sameFrameSerial;
        sameFrame.captureSerial[1] = sameFrameSerial;
        sameFrame.eyeMask = 0x3;
        sameFrame.eyeRenderCount = 2;
        sameFrame.freshEyeCount = 2;
        sameFrame.allPoseSpansRestored = true;
        Check(Halo2SameFramePairMatches(sameFrame, sameFrameGeneration, sameFrameSerial),
            "C-H2-3 publishes only a complete two-eye render/capture proof for "
            "the current prepared serial and one game-frame attempt");

        const auto pairRejected = [&](const Halo2SameFramePairProof& proof)
        { return !Halo2SameFramePairMatches(proof, sameFrameGeneration, sameFrameSerial); };
        Halo2SameFramePairProof previousLeft = sameFrame;
        previousLeft.renderSerial[0] = sameFrameSerial - 1;
        previousLeft.captureSerial[0] = sameFrameSerial - 1;
        Halo2SameFramePairProof previousRight = sameFrame;
        previousRight.renderSerial[1] = sameFrameSerial - 1;
        previousRight.captureSerial[1] = sameFrameSerial - 1;
        Halo2SameFramePairProof bothPrevious = sameFrame;
        bothPrevious.renderSerial[0] = bothPrevious.renderSerial[1] = sameFrameSerial - 1;
        bothPrevious.captureSerial[0] = bothPrevious.captureSerial[1] = sameFrameSerial - 1;
        Halo2SameFramePairProof futureEye = sameFrame;
        futureEye.renderSerial[0] = sameFrameSerial + 1;
        futureEye.captureSerial[0] = sameFrameSerial + 1;
        Halo2SameFramePairProof futurePrepared = sameFrame;
        futurePrepared.preparedSerial = sameFrameSerial + 1;
        Check(pairRejected(previousLeft) && pairRejected(previousRight) && pairRejected(bothPrevious) &&
                  pairRejected(futureEye) && pairRejected(futurePrepared),
            "C-H2-3 mechanically rejects N-1/current, current/N-1, all-N-1, "
            "and future-serial eye pairs");

        Halo2SameFramePairProof wrongGeneration = sameFrame;
        ++wrongGeneration.generation;
        Halo2SameFramePairProof wrongEyeAttempt = sameFrame;
        ++wrongEyeAttempt.eyeAttemptToken[1];
        Halo2SameFramePairProof wrongPairAttempt = sameFrame;
        ++wrongPairAttempt.attemptToken;
        Halo2SameFramePairProof zeroAttempt = sameFrame;
        zeroAttempt.attemptToken = 0;
        zeroAttempt.eyeAttemptToken[0] = 0;
        zeroAttempt.eyeAttemptToken[1] = 0;
        Check(pairRejected(wrongGeneration) && pairRejected(wrongEyeAttempt) && pairRejected(wrongPairAttempt) &&
                  pairRejected(zeroAttempt) && !Halo2SameFramePairMatches(sameFrame, 0, sameFrameSerial) &&
                  !Halo2SameFramePairMatches(sameFrame, sameFrameGeneration, 0),
            "C-H2-3 rejects a different generation, mixed/replayed attempt, "
            "or missing active generation/prepared serial");

        Halo2SameFramePairProof duplicateEye = sameFrame;
        duplicateEye.eyeMask = 0x1;
        Halo2SameFramePairProof missingEye = sameFrame;
        missingEye.eyeMask = 0x2;
        Halo2SameFramePairProof oneRender = sameFrame;
        oneRender.eyeRenderCount = 1;
        Halo2SameFramePairProof threeRenders = sameFrame;
        threeRenders.eyeRenderCount = 3;
        Halo2SameFramePairProof oneFreshEye = sameFrame;
        oneFreshEye.freshEyeCount = 1;
        Halo2SameFramePairProof threeFreshEyes = sameFrame;
        threeFreshEyes.freshEyeCount = 3;
        Check(pairRejected(duplicateEye) && pairRejected(missingEye) && pairRejected(oneRender) &&
                  pairRejected(threeRenders) && pairRejected(oneFreshEye) && pairRejected(threeFreshEyes),
            "C-H2-3 rejects a duplicate/missing eye, render count other than "
            "two, or fresh-eye count other than two");

        Halo2SameFramePairProof missingCapture = sameFrame;
        missingCapture.captureSerial[1] = 0;
        Halo2SameFramePairProof missingRender = sameFrame;
        missingRender.renderSerial[0] = 0;
        Halo2SameFramePairProof notRestored = sameFrame;
        notRestored.allPoseSpansRestored = false;
        Check(pairRejected(missingCapture) && pairRejected(missingRender) && pairRejected(notRestored),
            "C-H2-3 withholds publication until both current eye renders and "
            "captures exist and all six camera-pose spans are restored");
    }

    CheckUnit(56);
    Halo2ColdObservationResult halo2Cold{};
    halo2Cold.moduleRangeValid = true;
    halo2Cold.peIdentity = true;
    halo2Cold.anchorsMatchedOnce = static_cast<uint32_t>(kHalo2RetailAnchorCount);
    halo2Cold.anchorsAtPinnedRva = static_cast<uint32_t>(kHalo2RetailAnchorCount);
    halo2Cold.relativeTargetsAtPinnedRva = kHalo2RetailAnchorRelativeTargets;
    halo2Cold.postInitializationTickObserved = true;
    halo2Cold.mappingStable = true;
    Check(Halo2ColdObservationPass(halo2Cold), "A complete Halo 2 cold observation passes");
    Check(!Halo2ColdObservationNeedsImageScan(true) && Halo2ColdObservationNeedsImageScan(false),
        "A completed Halo 2 module instance never repeats its image scan");
    Check(Halo2ColdObservationNeedsDerivedRebind(true, true, false, false) &&
              !Halo2ColdObservationNeedsDerivedRebind(false, true, false, false) &&
              !Halo2ColdObservationNeedsDerivedRebind(true, false, false, false) &&
              !Halo2ColdObservationNeedsDerivedRebind(true, true, true, false) &&
              !Halo2ColdObservationNeedsDerivedRebind(true, true, false, true),
        "A same-generation Halo 2 level rearms cleared derived bindings exactly once from cached proof");
    CheckUnit(57);
    {
        Halo2ColdObservationResult broken = halo2Cold;
        broken.moduleRangeValid = false;
        Check(!Halo2ColdObservationPass(broken), "A wrong Halo 2 module range fails cold observation closed");
        broken = halo2Cold;
        broken.peIdentity = false;
        Check(!Halo2ColdObservationPass(broken), "A wrong Halo 2 PE identity fails cold observation closed");
        broken = halo2Cold;
        --broken.anchorsMatchedOnce;
        Check(!Halo2ColdObservationPass(broken), "A missing or duplicate Halo 2 anchor fails cold observation closed");
        broken = halo2Cold;
        --broken.anchorsAtPinnedRva;
        Check(!Halo2ColdObservationPass(broken), "A moved Halo 2 anchor fails cold observation closed");
        broken = halo2Cold;
        --broken.relativeTargetsAtPinnedRva;
        Check(!Halo2ColdObservationPass(broken), "A wrong Halo 2 data decode fails cold observation closed");
        broken = halo2Cold;
        broken.postInitializationTickObserved = false;
        Check(!Halo2ColdObservationPass(broken), "A Halo 2 image scan without an active-level tick fails closed");
        broken = halo2Cold;
        broken.mappingStable = false;
        Check(!Halo2ColdObservationPass(broken), "A remapped halo2.dll fails cold observation closed");
    }

    CheckUnit(58);
    {
        using Decision = Halo2GameTimeGateLogic::Decision;
        Halo2GameTimeGateLogic gate;
        Check(gate.Observe(false, 0) == Decision::Hold,
            "An uninitialized Halo 2 clock supplies frozen evidence but holds");
        Check(gate.Observe(true, 40) == Decision::Hold,
            "The Halo 2 initialization transition establishes a baseline only");
        Check(gate.Observe(true, 41) == Decision::OpenAfterBoundaryThenTick,
            "The first real post-initialization tick opens the Halo 2 gate");
        Check(gate.Observe(false, 0) == Decision::Hold && !gate.IsOpen() && gate.Observe(true, 50) == Decision::Hold &&
                  gate.Observe(true, 51) == Decision::OpenAfterBoundaryThenTick,
            "An explicit Halo 2 dispose closes a previously open gate and a "
            "later level must earn a new baseline and tick");
        gate.InvalidateSample();
        Check(!gate.IsOpen() && gate.Observe(true, 60) == Decision::Hold &&
                  gate.Observe(true, 61) == Decision::OpenAfterBoundaryThenTick,
            "An invalid sample closes an open Halo 2 gate and forces a fresh "
            "baseline before preserved genuine lifecycle evidence may reopen");

        gate.Reset();
        Check(gate.Observe(true, 1000) == Decision::Hold, "An already-running Halo 2 clock begins with a baseline");
        for (uint32_t i = 1; i < Halo2GameTimeGateLogic::kAlreadyRunningSamples; ++i)
        {
            Check(gate.Observe(true, 1000 + i) == Decision::Hold,
                "The Halo 2 already-running gate holds before 120 changes");
        }
        Check(gate.Observe(true, 1000 + Halo2GameTimeGateLogic::kAlreadyRunningSamples) == Decision::OpenAlreadyRunning,
            "120 consecutive Halo 2 clock changes prove an already-running level");

        gate.Reset();
        Check(gate.Observe(true, 2000) == Decision::Hold && gate.Observe(true, 2000) == Decision::Hold &&
                  gate.Observe(true, 2001) == Decision::Hold && !gate.SawUninitialized() && gate.ChangeRun() == 1,
            "An unchanged but initialized Halo 2 clock cannot manufacture the "
            "fast uninitialized-then-tick path");

        gate.Reset();
        gate.Observe(false, 0);
        gate.Observe(true, 10);
        gate.InvalidateSample();
        Check(gate.Observe(true, 11) == Decision::Hold && gate.Observe(true, 12) == Decision::OpenAfterBoundaryThenTick,
            "A racy/unreadable Halo 2 sample cannot fabricate a tick; the next "
            "coherent value is only a new baseline");

        gate.Reset();
        gate.Observe(true, 3000);
        for (uint32_t i = 1; i < Halo2GameTimeGateLogic::kAlreadyRunningSamples; ++i)
        {
            Check(gate.Observe(true, 3000 + i) == Decision::Hold, "The Halo 2 gate remains closed through 119 changes");
        }
        gate.InvalidateSample();
        Check(gate.ChangeRun() == 0 && gate.Observe(true, 4000) == Decision::Hold &&
                  gate.Observe(true, 4001) == Decision::Hold && gate.ChangeRun() == 1,
            "An invalid Halo 2 sample breaks the consecutive already-running "
            "proof and forces a fresh baseline");
    }

    CheckUnit(99);
    {
        // Without this admission the XInput hook passed a physical
        // pad through untouched inside every Halo 2 level and the VR
        // controllers only worked in the shell.
        const TitleDescriptor* descriptor = TitleRegistry_Find(GameTitle::Halo2);
        const bool admitted = descriptor && (descriptor->admissionCapabilities & TitleCapability_ControllerInput) != 0;
        Check(admitted && TitleRegistry_AllowsSharedControllerInput(GameTitle::Halo2, false, false, true, admitted),
            "Halo 2 receives shared virtual-controller admission (C-H2-22)");
    }
    CheckUnit(100);
    Check(TitleRegistry_FromModuleName(L"MCC-Win64-Shipping.exe") == nullptr,
        "The MCC host is not mistaken for a game title");
    Check(std::string_view(RuntimeModeName(RuntimeMode::Vehicle)) == "vehicle",
        "Runtime modes have stable diagnostic names");

    Check(!Halo2ShouldSuppressClassicFirstPersonParticle(0, 0) && Halo2ShouldSuppressClassicFirstPersonParticle(0, 1) &&
              Halo2ShouldSuppressClassicFirstPersonParticle(0, 0xFF) &&
              !Halo2ShouldSuppressClassicFirstPersonParticle(1, 1) &&
              !Halo2ShouldSuppressClassicFirstPersonParticle(2, 1),
        "Halo 2 muzzle suppression requires both the live Classic gate and "
        "a nonzero current-user first-person particle classification");

    CheckUnit(242);
    // Halo 2 native HUD layout: keep authored CHUD inside the native FOV that
    // the compositor submits from the title's wider symmetric raster cover.
    {
        constexpr float radians = 3.14159265f / 180.0f;
        const Halo2CameraRectangle source{0, 0, 2730, 3786};
        const Halo2SymmetricHalfFovs cover{63.3f * radians, 55.1f * radians};
        const float leftEye[4] = {-54.0f * radians, 40.0f * radians, 44.0f * radians, -55.0f * radians};
        const float rightEye[4] = {-40.0f * radians, 54.0f * radians, 44.0f * radians, -55.0f * radians};

        Halo2CameraRectangle full{};
        Halo2CameraRectangle half{};
        Halo2CameraRectangle narrow{};
        Halo2CameraRectangle raised{};
        const bool fullOk =
            halo2_hud::ComputeVisibleLayoutRectangle(source, cover, leftEye, rightEye, 1.0f, 1.0f, 0.0f, full);
        const bool halfOk =
            halo2_hud::ComputeVisibleLayoutRectangle(source, cover, leftEye, rightEye, 0.5f, 1.0f, 0.0f, half);
        const bool narrowOk =
            halo2_hud::ComputeVisibleLayoutRectangle(source, cover, leftEye, rightEye, 0.5f, 0.5f, 0.0f, narrow);
        const bool raisedOk =
            halo2_hud::ComputeVisibleLayoutRectangle(source, cover, leftEye, rightEye, 0.5f, 1.0f, 100.0f, raised);
        const int fullWidth = full.x1 - full.x0;
        const int fullHeight = full.y1 - full.y0;
        const int halfWidth = half.x1 - half.x0;
        const int halfHeight = half.y1 - half.y0;
        const int narrowWidth = narrow.x1 - narrow.x0;
        Check(fullOk && halfOk && narrowOk && raisedOk && full.x0 > source.x0 && full.x1 < source.x1 &&
                  full.y0 > source.y0 && full.y1 < source.y1 && std::abs(halfWidth * 2 - fullWidth) <= 2 &&
                  std::abs(halfHeight * 2 - fullHeight) <= 2 && std::abs(narrowWidth * 2 - halfWidth) <= 2 &&
                  raised.y0 == half.y0 - 100 && raised.y1 == half.y1 - 100,
            "Halo 2 native HUD layout basis stays inside both submitted eye "
            "frusta and maps size, aspect, and height sliders");

        float rightX = static_cast<float>(source.x1);
        float topY = static_cast<float>(source.y0);
        const bool healthMapped = halo2_hud::MapNativeAnchorPoint(source, half, 0, rightX, topY);
        float leftX = static_cast<float>(source.x0);
        float bottomY = static_cast<float>(source.y1);
        const bool sensorMapped = halo2_hud::MapNativeAnchorPoint(source, half, 2, leftX, bottomY);
        float crosshairX = (source.x0 + source.x1) * 0.5f;
        float crosshairY = (source.y0 + source.y1) * 0.5f;
        const float stockCrosshairX = crosshairX;
        const float stockCrosshairY = crosshairY;
        const bool crosshairMapped =
            halo2_hud::MapNativeAnchorPoint(source, half, halo2_hud::kCrosshairAnchor, crosshairX, crosshairY);
        Check(healthMapped && sensorMapped && std::fabs(rightX - half.x1) < 0.01f &&
                  std::fabs(topY - half.y0) < 0.01f && std::fabs(leftX - half.x0) < 0.01f &&
                  std::fabs(bottomY - half.y1) < 0.01f && !crosshairMapped && crosshairX == stockCrosshairX &&
                  crosshairY == stockCrosshairY,
            "Halo 2 native health/weapon/sensor anchors map into the slider "
            "basis while the aiming crosshair remains stock");

        // This replaces the disproven zero-callback anchor guess with the
        // actual D3D11 pixel-shader identities used by a working Halo 2 HUD
        // mod. The crosshair has its own role so it can be captured as native
        // art instead of being distorted with the information widgets.
        bool allGameplayShadersClassified = true;
        for (const uint64_t hash : halo2_hud_shader::kGameplayHudHashes)
        {
            allGameplayShadersClassified =
                allGameplayShadersClassified && halo2_hud_shader::Classify(hash) == halo2_hud_shader::Role::GameplayHud;
        }
        const char fnvFixture[] = "abc";
        Check(allGameplayShadersClassified && halo2_hud_shader::kGameplayHudHashes.size() == 15 &&
                  halo2_hud_shader::Classify(halo2_hud_shader::kCrosshairHash) == halo2_hud_shader::Role::Crosshair &&
                  halo2_hud_shader::Classify(0x123456789abcdef0ULL) == halo2_hud_shader::Role::Other &&
                  halo2_hud_shader::MigotoFnv1(fnvFixture, sizeof(fnvFixture) - 1) == 0x014984000117d8a0ULL,
            "Halo 2 field-proven HUD/crosshair shader identities use the "
            "same unseeded FNV-1 contract as 3DMigoto");

        halo2_hud_shader::RasterAffine halfTransform{};
        halo2_hud_shader::RasterAffine narrowTransform{};
        halo2_hud_shader::RasterAffine raisedTransform{};
        const bool halfTransformOk = halo2_hud_shader::BuildRasterAffine(source, half, halfTransform);
        const bool narrowTransformOk = halo2_hud_shader::BuildRasterAffine(source, narrow, narrowTransform);
        const bool raisedTransformOk = halo2_hud_shader::BuildRasterAffine(source, raised, raisedTransform);
        Check(halfTransformOk && narrowTransformOk && raisedTransformOk &&
                  std::fabs(halo2_hud_shader::MapX(halfTransform, static_cast<float>(source.x0)) - half.x0) < 0.01f &&
                  std::fabs(halo2_hud_shader::MapX(halfTransform, static_cast<float>(source.x1)) - half.x1) < 0.01f &&
                  std::fabs(halo2_hud_shader::MapY(halfTransform, static_cast<float>(source.y0)) - half.y0) < 0.01f &&
                  std::fabs(halo2_hud_shader::MapY(halfTransform, static_cast<float>(source.y1)) - half.y1) < 0.01f &&
                  narrowTransform.scaleX < halfTransform.scaleX * 0.51f &&
                  std::fabs(narrowTransform.scaleY - halfTransform.scaleY) < 0.001f &&
                  std::fabs(raisedTransform.offsetY - (halfTransform.offsetY - 100.0f)) < 0.01f,
            "Halo 2 HUD size/aspect/vertical sliders materially alter the "
            "exact raster transform while preserving its source-to-layout map");

        Halo2CameraRectangle rejected{};
        float badFov[4] = {leftEye[0], leftEye[1], leftEye[2], leftEye[3]};
        badFov[0] = 2.0f;
        Check(
            !halo2_hud::ComputeVisibleLayoutRectangle(source, cover, badFov, rightEye, 0.5f, 1.0f, 0.0f, rejected) &&
                !halo2_hud::ComputeVisibleLayoutRectangle(source, cover, leftEye, rightEye, 0.1f, 1.0f, 0.0f, rejected),
            "Halo 2 native HUD layout rejects an uncovered FOV and "
            "out-of-range slider values");
    }

    CheckUnit(243);
    // Pin the official H2EK boolean identity and, importantly, the
    // lifecycle contract. VR-owned gameplay requests 1; leaving gameplay
    // returns the exact captured value rather than assuming stock was 0.
    Check(std::string_view(kHalo2DisableAimAssistDebugVar) == "sim_disable_aim_assist" &&
              kHalo2DebugVarTypeBoolean == 5 && Halo2AimAssistDebugValueValid(0) && Halo2AimAssistDebugValueValid(1) &&
              !Halo2AimAssistDebugValueValid(2) && Halo2AimAssistDebugValue(true, 0) == 1 &&
              Halo2AimAssistDebugValue(false, 0) == 0 && Halo2AimAssistDebugValue(false, 1) == 1,
        "Halo 2 rejected debug-global aim-assist path remains pinned but "
        "compiled dormant after its headset StockFallback result");

    // Reproduce the official H2EK and pinned-retail
    // neutral initializer exactly. In particular, preserve the 0x0C..0x17
    // and 0x1A..0x1B bytes that the engine itself does not initialize.
    float neutralControl[3] = {1.0f, -2.0f, 3.0f};
    Halo2AimAssistTargetingResult neutralTargeting{};
    std::memset(&neutralTargeting, 0xA5, sizeof(neutralTargeting));
    const bool neutralWritten = Halo2WriteNeutralAimAssistResults(neutralControl, &neutralTargeting);
    bool scratchPreserved = neutralTargeting.engineScratch1A == 0xA5A5;
    for (uint8_t value : neutralTargeting.engineScratch)
        scratchPreserved = scratchPreserved && value == 0xA5;
    Check(kHalo2AimAssistCalculateRva == 0x00759260 && neutralWritten && neutralControl[0] == 0.0f &&
              neutralControl[1] == 0.0f && neutralControl[2] == 0.0f && neutralTargeting.identifiers[0] == UINT32_MAX &&
              neutralTargeting.identifiers[1] == UINT32_MAX && neutralTargeting.identifiers[2] == UINT32_MAX &&
              neutralTargeting.flags == 0 && neutralTargeting.magnetismHorizontal == 0.0f &&
              neutralTargeting.magnetismVertical == 0.0f && scratchPreserved &&
              !Halo2WriteNeutralAimAssistResults(nullptr, &neutralTargeting) &&
              !Halo2WriteNeutralAimAssistResults(neutralControl, nullptr),
        "Halo 2 central aim-assist bypass writes the official neutral output "
        "fields and leaves engine-owned scratch bytes untouched");

    // Camera suppression must not erase the independent
    // engine-selected target that Halo 2 forwards to melee-target-unit.
    float cameraAssist[3] = {0.25f, -0.5f, 0.75f};
    Halo2AimAssistTargetingResult retainedTargeting{};
    std::memset(&retainedTargeting, 0x5A, sizeof(retainedTargeting));
    const Halo2AimAssistTargetingResult expectedTargeting = retainedTargeting;
    Check(Halo2SuppressCameraAimAssist(cameraAssist) && cameraAssist[0] == 0.0f && cameraAssist[1] == 0.0f &&
              cameraAssist[2] == 0.0f &&
              std::memcmp(&retainedTargeting, &expectedTargeting, sizeof(retainedTargeting)) == 0 &&
              !Halo2SuppressCameraAimAssist(nullptr),
        "Halo 2 camera-assist suppression zeros only camera control and "
        "preserves the complete engine-selected targeting result");

    // The controller direction can replace the H2EK-
    // identified player-control view result only inside the explicit central
    // aim-assist scope. Invalid or unscoped inputs leave the engine result
    // byte-for-byte stock.
    const float controllerTargetRay[3] = {0.6f, 0.0f, 0.8f};
    float selectedView[3] = {-1.0f, 2.0f, -3.0f};
    Check(kHalo2AimAssistViewDirectionRva == 0x006C0DF0 && kHalo2ControllerAimAssistTargetingEnabled &&
              Halo2OverrideAimAssistViewDirection(true, controllerTargetRay, selectedView) &&
              selectedView[0] == controllerTargetRay[0] && selectedView[1] == controllerTargetRay[1] &&
              selectedView[2] == controllerTargetRay[2],
        "Halo 2 scoped aim-assist view helper accepts the normalized "
        "controller target ray");
    const float stockView[3] = {0.1f, 0.2f, 0.3f};
    std::memcpy(selectedView, stockView, sizeof(stockView));
    const float invalidTargetRay[3] = {2.0f, 0.0f, 0.0f};
    Check(!Halo2OverrideAimAssistViewDirection(false, controllerTargetRay, selectedView) &&
              !Halo2OverrideAimAssistViewDirection(true, invalidTargetRay, selectedView) &&
              std::memcmp(selectedView, stockView, sizeof(stockView)) == 0,
        "Halo 2 aim-assist view direction stays stock outside the scope and "
        "rejects a non-unit controller ray");

    RecentSecondaryWeaponPresentation secondaryPresentation;
    DualWeaponAimSnapshot dualSample{};
    dualSample.generation = 3;
    dualSample.unit = 0x12340001;
    dualSample.weapons[0] = 0x56780001;
    dualSample.weapons[1] = 0x9abc0002;
    dualSample.trackingEpoch = 7;
    dualSample.sampleMs = 1000;
    dualSample.timeNs = 1000000000;
    dualSample.positions[0][0] = 0.3f;
    dualSample.positions[1][0] = -0.3f;
    dualSample.directions[0][2] = 1;
    dualSample.directions[1][0] = 1;
    DualWeaponAimPublication dualPublication;
    DualWeaponAimSnapshot dualRead{};
    Check(!dualPublication.Read(dualRead) && dualPublication.Publish(dualSample) && dualPublication.Read(dualRead) &&
              dualRead.weapons[1] == dualSample.weapons[1] && dualRead.positions[1][0] == -0.3f &&
              dualRead.directions[1][0] == 1,
        "dual ray publication preserves each weapon and its independent controller");
    Check(DualWeaponAimFresh(dualRead, 3, dualSample.unit, 7, 1050, 950, 1050000000) &&
              !DualWeaponAimFresh(dualRead, 4, dualSample.unit, 7, 1050, 950, 1050000000) &&
              !DualWeaponAimFresh(dualRead, 3, 0x22340001, 7, 1050, 950, 1050000000) &&
              !DualWeaponAimFresh(dualRead, 3, dualSample.unit, 8, 1050, 950, 1050000000) &&
              !DualWeaponAimFresh(dualRead, 3, dualSample.unit, 7, 1101, 950, 1050000000) &&
              !DualWeaponAimFresh(dualRead, 3, dualSample.unit, 7, 1050, 1001, 1050000000) &&
              !DualWeaponAimFresh(dualRead, 3, dualSample.unit, 7, 1050, 950, 999999999),
        "dual rays reject respawn salt, title or tracking changes, stale and replacement samples");
    const float origin[3]{};
    float firstRay[3]{}, secondRay[3]{};
    Check(BuildIndependentWeaponDirection(origin, dualRead.positions[0], dualRead.directions[0], 10, firstRay) &&
              BuildIndependentWeaponDirection(origin, dualRead.positions[1], dualRead.directions[1], 10, secondRay) &&
              firstRay[2] > 0.99f && secondRay[0] > 0.99f && secondRay[2] == 0,
        "separated dual controllers aim at separate targets from the native origin");
    const float zeroDirection[3]{};
    float refusedRay[3]{2, 3, 4};
    Check(!BuildIndependentWeaponDirection(origin, origin, zeroDirection, 10, refusedRay) &&
              !BuildIndependentWeaponDirection(origin, origin, firstRay, INFINITY, refusedRay) && refusedRay[0] == 2 &&
              refusedRay[1] == 3 && refusedRay[2] == 4,
        "invalid dual ray leaves the native direction untouched");
    Check(!secondaryPresentation.Active(3, 1000), "no secondary presentation before publication");
    secondaryPresentation.Publish(3, 1000);
    Check(secondaryPresentation.Active(3, 1150) && !secondaryPresentation.Active(3, 1151) &&
              !secondaryPresentation.Active(4, 1000) && !secondaryPresentation.Active(3, 999),
        "dual-grab exclusion expires and rejects foreign generations and backwards time");
    secondaryPresentation.Publish(4, UINT64_C(0xfffffff0));
    Check(
        secondaryPresentation.Active(4, UINT64_C(0x100000010)), "dual-grab exclusion survives the clock low-word wrap");
    SupportGripAdmission supportGrip;
    Check(!supportGrip.Observe(true, true, false) && !supportGrip.Observe(true, false, false) &&
              supportGrip.Observe(false, false, false) && supportGrip.Observe(true, false, false),
        "dropping a second weapon requires releasing grip before a new support grab");
    Check(!supportGrip.Observe(true, false, true) && !supportGrip.Observe(true, false, false) &&
              supportGrip.Observe(false, false, false),
        "changing handedness consumes a held grip instead of acquiring a false grab");
    Check(PhysicalHandForWeaponSlot(0, false) == 1 && PhysicalHandForWeaponSlot(1, false) == 0 &&
              PhysicalHandForWeaponSlot(0, true) == 0 && PhysicalHandForWeaponSlot(1, true) == 1 &&
              PhysicalHandForWeaponSlot(2, true) == -1,
        "weapon role mapping preserves both physical hand identities");
    Check(ResolveEquippedWeaponSlot(0x12340001, 0x12340001, 0x56780001, true, true) == 0 &&
              ResolveEquippedWeaponSlot(0x56780001, 0x12340001, 0x56780001, true, true) == 1 &&
              ResolveEquippedWeaponSlot(0x9abc0001, 0x12340001, 0x56780001, true, true) == -1 &&
              ResolveEquippedWeaponSlot(0x56780001, 0x12340001, 0x56780001, true, false) == -1,
        "dual firing rejects same-index stale salt and unavailable secondary slots");

    CheckUnit(-1);
    std::cout << "checks run: " << g_checks << '\n';
    if (g_failures == 0)
        std::cout << "HaloMCCVR core halo2 tests passed\n";
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
