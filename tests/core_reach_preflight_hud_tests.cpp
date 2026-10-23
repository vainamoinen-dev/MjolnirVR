// core_reach_preflight_hud_tests.cpp: Reach wind capture, loaded-image and CHUD proofs, outer preflight, compact camera and hand rays.
// Units 6, 27, 28, 37, 38, 229, 237 of the former tests/core_tests.cpp main(), moved unchanged and in
// their order (E3). Built as halomccvr_core_reach_preflight_hud_tests and run by ctest; each unit prints its check count.
#include "core_test_support.h"

int main()
{
    CheckUnit(6);
    {
        const auto copy = [](void* d, const void* s, size_t n)
        {
            std::memcpy(d, s, n);
            return true;
        };
        for (bool rightFirst : {false, true})
        {
            for (bool abortSecond : {false, true})
            {
                uint32_t state[12]{};
                for (unsigned i = 0; i < 12; ++i)
                    state[i] = 100 + i;
                ReachWindReplay pair{};
                Check(pair.Begin(state, copy), "Reach wind initial capture");
                for (unsigned i = 0; i < 12; ++i)
                    state[i] += 7 + i;
                uint32_t firstEye[12];
                std::memcpy(firstEye, state, sizeof(state));
                Check(pair.Replay(state, copy), "Reach wind second eye rewind");
                for (unsigned i = 0; i < 12; ++i)
                    state[i] += 7 + i;
                Check(std::memcmp(firstEye, state, sizeof(state)) == 0,
                    rightFirst ? "Wind right-first eyes agree" : "Wind left-first eyes agree");
                if (abortSecond)
                    state[0] = 0; // failed native second render
                Check(pair.Finish(state, copy) && std::memcmp(firstEye, state, sizeof(state)) == 0,
                    "Wind retains exactly one native update after success or abort");
                Check(pair.Finish(state, copy), "Wind cleanup is idempotent");
            }
        }
        uint32_t state[12]{};
        ReachWindReplay failed{};
        const auto reject = [](void*, const void*, size_t) { return false; };
        Check(!failed.Begin(state, reject) && !failed.Replay(state, copy) && failed.Finish(state, copy),
            "Wind read failure leaves native state alone");
        ReachWindReplay partial{};
        partial.Begin(state, copy);
        state[0] = 42;
        int calls = 0;
        auto partialWrite = [&](void* d, const void* s, size_t n)
        {
            if (++calls == 2)
            {
                std::memcpy(d, s, 4);
                return false;
            }
            return copy(d, s, n);
        };
        Check(!partial.Replay(state, partialWrite) && partial.Finish(state, copy) && state[0] == 42,
            "Wind failed rewind restores the completed native state");
    }

    CheckUnit(27);
    {
        constexpr std::array<uint8_t, 6> repeatedPattern{0xAA, 0xBB, 0xCC, 0xAA, 0xBB, 0xCC};
        constexpr std::array<uint8_t, 3> twice{0xAA, 0xBB, 0xCC};
        constexpr std::array<uint8_t, 3> once{0xBB, 0xCC, 0xAA};
        constexpr std::array<uint8_t, 2> absent{0xCC, 0xBB};
        Check(CountReachExactPattern(repeatedPattern.data(), repeatedPattern.size(), twice.data(), twice.size()) == 2,
            "Reach loaded-image proof rejects a multiple-match exact pattern");
        Check(CountReachExactPattern(repeatedPattern.data(), repeatedPattern.size(), once.data(), once.size()) == 1,
            "Reach loaded-image proof accepts one exact pattern match");
        Check(CountReachExactPattern(repeatedPattern.data(), repeatedPattern.size(), absent.data(), absent.size()) == 0,
            "Reach loaded-image proof rejects an absent exact pattern");
        Check(CountReachExactPattern(nullptr, repeatedPattern.size(), twice.data(), twice.size()) == 0 &&
                  CountReachExactPattern(repeatedPattern.data(), repeatedPattern.size(), nullptr, twice.size()) == 0 &&
                  CountReachExactPattern(repeatedPattern.data(), repeatedPattern.size(), twice.data(), 0) == 0,
            "Reach exact-pattern counting fails closed on invalid inputs");

        std::array<uint8_t, 5> call{0xE8, 0, 0, 0, 0};
        uintptr_t callTarget = 0;
        int32_t displacement = 0x20;
        std::memcpy(call.data() + 1, &displacement, sizeof(displacement));
        Check(ResolveReachRel32Call(0x1000, call.data(), call.size(), callTarget) && callTarget == 0x1025,
            "Reach rel32 proof resolves a forward call target exactly");
        displacement = -0x20;
        std::memcpy(call.data() + 1, &displacement, sizeof(displacement));
        Check(ResolveReachRel32Call(0x1000, call.data(), call.size(), callTarget) && callTarget == 0x0FE5,
            "Reach rel32 proof resolves a backward call target exactly");
        call[0] = 0xE9;
        Check(!ResolveReachRel32Call(0x1000, call.data(), call.size(), callTarget),
            "Reach rel32 proof rejects a non-call opcode");
        call[0] = 0xE8;
        Check(!ResolveReachRel32Call(0x1000, call.data(), 4, callTarget),
            "Reach rel32 proof rejects a truncated instruction");
        displacement = -6;
        std::memcpy(call.data() + 1, &displacement, sizeof(displacement));
        Check(!ResolveReachRel32Call(0, call.data(), call.size(), callTarget),
            "Reach rel32 proof rejects target-address underflow");
        displacement = 1;
        std::memcpy(call.data() + 1, &displacement, sizeof(displacement));
        Check(!ResolveReachRel32Call(std::numeric_limits<uintptr_t>::max() - 5, call.data(), call.size(), callTarget),
            "Reach rel32 proof rejects target-address overflow");

        constexpr uintptr_t playerArray = 0x100000;
        constexpr size_t playerStride = 0xA40;
        constexpr size_t playerCount = 4;
        for (uint32_t slot = 0; slot < playerCount; ++slot)
        {
            const ReachObservedView view =
                ClassifyReachObservedView(playerArray + slot * playerStride, playerArray, playerStride, playerCount);
            Check(view.kind == ReachObservedViewKind::NormalPlayerSlot && view.slot == slot,
                "Reach active-view proof recognizes each exact 0xA40 player slot");
        }
        Check(ClassifyReachObservedView(0, playerArray, playerStride, playerCount).kind == ReachObservedViewKind::None,
            "Reach active-view proof treats a null pointer as no transaction");
        Check(ClassifyReachObservedView(playerArray + 1, playerArray, playerStride, playerCount).kind ==
                  ReachObservedViewKind::OutsidePlayerArray,
            "Reach active-view proof rejects an interior non-slot pointer");
        Check(ClassifyReachObservedView(playerArray - 1, playerArray, playerStride, playerCount).kind ==
                      ReachObservedViewKind::OutsidePlayerArray &&
                  ClassifyReachObservedView(
                      playerArray + playerStride * playerCount, playerArray, playerStride, playerCount)
                          .kind == ReachObservedViewKind::OutsidePlayerArray,
            "Reach active-view proof rejects pointers outside the four-slot array");
        Check(ClassifyReachObservedView(playerArray, 0, playerStride, playerCount).kind ==
                      ReachObservedViewKind::OutsidePlayerArray &&
                  ClassifyReachObservedView(playerArray, playerArray, 0, playerCount).kind ==
                      ReachObservedViewKind::OutsidePlayerArray,
            "Reach active-view proof fails closed on an invalid array description");

        ReachObserverTransactionGate transactionGate;
        Check(!transactionGate.Observe(playerArray), "Reach observer ignores a mid-transaction value at session start");
        Check(!transactionGate.Observe(0) && transactionGate.Observe(playerArray),
            "Reach observer requires a witnessed clear before its first transaction");
        Check(!transactionGate.Observe(playerArray), "Reach observer counts a latched pointer only once");
        Check(transactionGate.Observe(playerArray + playerStride),
            "Reach observer recognizes a changed non-null transaction owner");
        transactionGate.RequireClear();
        Check(!transactionGate.Observe(playerArray + playerStride) && !transactionGate.HasLatchedValue() &&
                  !transactionGate.Observe(0) && transactionGate.Observe(playerArray + playerStride),
            "Reach continuity reset requires a new witnessed clear");
    }

    CheckUnit(28);
    {
        // Independent HREK literals: the validator must accept both optimized
        // official chud_draw_widget bodies and reject any ownership-bearing
        // byte drift. The rel32 itself is link-layout data and is intentionally
        // allowed to differ.
        // clang-format off
        constexpr std::array<uint8_t, 33> officialEntry{
            0x48, 0x8B, 0xC4, 0x44, 0x89, 0x48, 0x20, 0x48, 0x89, 0x50, 0x10,
            0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57,
            0x48, 0x8D, 0x68, 0xA9, 0x48, 0x81, 0xEC, 0xC0, 0x00, 0x00, 0x00};
        // clang-format on
        constexpr std::array<uint8_t, 3> tagDescriptorMove{0x4C, 0x8B, 0xF2};
        constexpr std::array<uint8_t, 3> sapienDescriptorMove{0x4C, 0x8B, 0xFA};
        constexpr std::array<uint8_t, 4> fifthArgumentLoad{0x4C, 0x8B, 0x4D, 0x7F};
        constexpr std::array<uint8_t, 6> tagClassRead{0x41, 0x0F, 0xBE, 0x56, 0x04, 0xE8};
        constexpr std::array<uint8_t, 6> sapienClassRead{0x41, 0x0F, 0xBE, 0x57, 0x04, 0xE8};
        constexpr std::array<uint8_t, 3> postClassCall{0x48, 0x8B, 0xD0};
        Check(kReachHrekChudDrawWidgetEntryBytes == officialEntry,
            "Reach CHUD entry bytes remain the independently recorded official HREK signature");

        std::array<uint8_t, kReachTagPlayChudDrawWidgetBodySize> tagPlay{};
        std::memcpy(tagPlay.data(), officialEntry.data(), officialEntry.size());
        std::memcpy(
            tagPlay.data() + kReachTagPlayChudDescriptorMoveOffset, tagDescriptorMove.data(), tagDescriptorMove.size());
        std::memcpy(tagPlay.data() + kReachTagPlayChudFifthArgumentLoadOffset, fifthArgumentLoad.data(),
            fifthArgumentLoad.size());
        std::memcpy(tagPlay.data() + kReachTagPlayChudClassReadOffset, tagClassRead.data(), tagClassRead.size());
        tagPlay[kReachTagPlayChudClassReadOffset + 6] = 0x12;
        tagPlay[kReachTagPlayChudClassReadOffset + 7] = 0x34;
        tagPlay[kReachTagPlayChudClassReadOffset + 8] = 0x56;
        tagPlay[kReachTagPlayChudClassReadOffset + 9] = 0x78;
        std::memcpy(tagPlay.data() + kReachTagPlayChudClassReadOffset + 10, postClassCall.data(), postClassCall.size());
        Check(ReachHrekChudDrawWidgetLayoutMatches(tagPlay),
            "Reach CHUD validator accepts the exact official reach_tag_play layout");
        Check(!ReachHrekChudDrawWidgetLayoutMatches(std::span<const uint8_t>(tagPlay.data(), tagPlay.size() - 1)),
            "Reach CHUD validator rejects a non-official tag-play body size");
        tagPlay[0] = 0x49;
        Check(!ReachHrekChudDrawWidgetLayoutMatches(tagPlay),
            "Reach CHUD validator rejects official-entry signature drift");
        tagPlay[0] = officialEntry[0];
        tagPlay[kReachTagPlayChudFifthArgumentLoadOffset + 3] = 0x7E;
        Check(!ReachHrekChudDrawWidgetLayoutMatches(tagPlay),
            "Reach CHUD validator rejects fifth-argument ABI drift in tag-play");
        tagPlay[kReachTagPlayChudFifthArgumentLoadOffset + 3] = 0x7F;
        tagPlay[kReachTagPlayChudClassReadOffset + 4] = 0x05;
        Check(!ReachHrekChudDrawWidgetLayoutMatches(tagPlay),
            "Reach CHUD validator rejects descriptor-class offset drift in tag-play");
        tagPlay[kReachTagPlayChudClassReadOffset + 4] = 0x04;

        std::array<uint8_t, kReachSapienPlayChudDrawWidgetBodySize> sapienPlay{};
        std::memcpy(sapienPlay.data(), officialEntry.data(), officialEntry.size());
        std::memcpy(sapienPlay.data() + kReachSapienPlayChudDescriptorMoveOffset, sapienDescriptorMove.data(),
            sapienDescriptorMove.size());
        std::memcpy(sapienPlay.data() + kReachSapienPlayChudFifthArgumentLoadOffset, fifthArgumentLoad.data(),
            fifthArgumentLoad.size());
        std::memcpy(
            sapienPlay.data() + kReachSapienPlayChudClassReadOffset, sapienClassRead.data(), sapienClassRead.size());
        sapienPlay[kReachSapienPlayChudClassReadOffset + 6] = 0xDE;
        sapienPlay[kReachSapienPlayChudClassReadOffset + 7] = 0xAD;
        sapienPlay[kReachSapienPlayChudClassReadOffset + 8] = 0xBE;
        sapienPlay[kReachSapienPlayChudClassReadOffset + 9] = 0xEF;
        std::memcpy(
            sapienPlay.data() + kReachSapienPlayChudClassReadOffset + 10, postClassCall.data(), postClassCall.size());
        Check(ReachHrekChudDrawWidgetLayoutMatches(sapienPlay),
            "Reach CHUD validator accepts the exact official sapien_play layout");
        sapienPlay[kReachSapienPlayChudDescriptorMoveOffset + 2] = 0xF2;
        Check(!ReachHrekChudDrawWidgetLayoutMatches(sapienPlay),
            "Reach CHUD validator rejects descriptor-register drift in sapien-play");
        sapienPlay[kReachSapienPlayChudDescriptorMoveOffset + 2] = 0xFA;
        sapienPlay[kReachSapienPlayChudClassReadOffset + 10] = 0x49;
        Check(!ReachHrekChudDrawWidgetLayoutMatches(sapienPlay),
            "Reach CHUD validator rejects post-class-call flow drift in sapien-play");

        using Action = ReachChudCrosshairAction;
        Check(ReachDecideChudCrosshairAction(false, true, kReachChudCrosshairScriptingClass, true, true, 0, false) ==
                      Action::DrawStock &&
                  ReachDecideChudCrosshairAction(
                      false, false, kReachChudCrosshairScriptingClass, true, true, -1, true) == Action::DrawStock,
            "Reach CHUD stays stock outside its owned stereo transaction");
        Check(ReachDecideChudCrosshairAction(true, false, kReachChudCrosshairScriptingClass, false, true, 0, false) ==
                  Action::RejectTransaction,
            "Owned Reach CHUD rejects the transaction when its scripting-class descriptor is unreadable");
        Check(ReachDecideChudCrosshairAction(true, true, 1, false, true, 0, false) == Action::DrawStock,
            "Reach CHUD never suppresses a non-crosshair scripting class");
        Check(ReachDecideChudCrosshairAction(true, true, kReachChudCrosshairScriptingClass, false, true, 0, false) ==
                  Action::Suppress,
            "The universal crosshair switch suppresses only the native class-2 widget");
        Check(ReachDecideChudCrosshairAction(true, true, kReachChudCrosshairScriptingClass, true, false, 0, false) ==
                  Action::DrawStock,
            "The universal kill-reticle switch deliberately restores the stock widget");
        Check(ReachDecideChudCrosshairAction(true, true, kReachChudCrosshairScriptingClass, true, true, 0, false) ==
                      Action::CaptureAuthored &&
                  ReachDecideChudCrosshairAction(true, true, kReachChudCrosshairScriptingClass, true, true, 1, false) ==
                      Action::Suppress,
            "Left-eye-first captures the authored widget once and suppresses its opposite-eye duplicate");
        Check(ReachDecideChudCrosshairAction(true, true, kReachChudCrosshairScriptingClass, true, true, 1, true) ==
                      Action::CaptureAuthored &&
                  ReachDecideChudCrosshairAction(true, true, kReachChudCrosshairScriptingClass, true, true, 0, true) ==
                      Action::Suppress,
            "Right-eye-first captures the authored widget once and suppresses its opposite-eye duplicate");
        Check(ReachDecideChudCrosshairAction(true, true, kReachChudCrosshairScriptingClass, true, true, -1, true) ==
                      Action::RejectTransaction &&
                  ReachDecideChudCrosshairAction(true, true, kReachChudCrosshairScriptingClass, true, true, 2, false) ==
                      Action::RejectTransaction,
            "Owned Reach CHUD rejects invalid eyes and never captures them as an authored fallback");

        Check(ReachAuthoredCrosshairPairComplete(false, true, false) &&
                  ReachAuthoredCrosshairPairComplete(true, false, false) &&
                  ReachAuthoredCrosshairPairComplete(true, true, true) &&
                  !ReachAuthoredCrosshairPairComplete(true, true, false),
            "A required Reach authored crosshair rejects a pair only when class 2 was emitted without a completed capture");

        Check(ReachCanSubmitCompleteProjection(false, false, true, false) &&
                  ReachCanSubmitCompleteProjection(false, true, false, false) &&
                  ReachCanSubmitCompleteProjection(true, true, false, true) &&
                  !ReachCanSubmitCompleteProjection(true, false, false, true) &&
                  !ReachCanSubmitCompleteProjection(true, true, true, true) &&
                  !ReachCanSubmitCompleteProjection(true, true, false, false),
            "Reach queues its world projection only after both eye uploads, authored upload success, and a live post-upload owner proof");
    }

    CheckUnit(37);
    {
        constexpr uintptr_t moduleBase = 0x10000000;
        ReachOuterRenderInput outer{};
        outer.moduleBase = moduleBase;
        outer.moduleSize = kReachRetailImageSize;
        outer.returnAddress = moduleBase + kReachNormalOuterReturnRva;
        outer.workspace = moduleBase + kReachDefaultWorkspaceRva;
        outer.playerView = moduleBase + kReachPlayerViewArrayRva;
        outer.playerWindowIndex = 0;
        // Retail initializes the camera-stack depth to -1. The normal
        // top-level push increments that sentinel to slot/depth zero.
        outer.cameraStackDepthBefore = -1;
        outer.nowMs = 1101;
        const ReachModuleEpoch epoch{moduleBase, 9};
        const ReachRenderCandidateProof outerProof = CompleteReachRenderCandidateProof();
        ReachPreflightPublication outerPreflightPublication;
        Check(outerPreflightPublication.Publish(epoch, outerProof),
            "Reach outer preflight publishes for the selected module epoch");
        outer.preflight = outerPreflightPublication.Get(epoch);
        ReachRenderFreshnessGate ownerFreshness;
        Check(ownerFreshness.AdvanceEpoch(epoch) && !ownerFreshness.Observe(100, epoch, 38, true, true).Stable() &&
                  !ownerFreshness.Observe(400, epoch, 39, true, true).Stable() &&
                  !ownerFreshness.Observe(700, epoch, 40, true, true).Stable() &&
                  !ownerFreshness.Observe(1000, epoch, 41, true, true).Stable(),
            "Reach owner freshness starts from exact prepared-frame observations");
        outer.freshCamera = ownerFreshness.Observe(1101, epoch, 42, true, true);
        outer.preparedFrame = ReachPreparedFrameToken::Create(epoch, 42, true);
        const ReachModuleEpoch wrongPreflightEpoch{moduleBase, 10};
        ReachPreflightPublication wrongPreflightPublication;
        Check(wrongPreflightPublication.Publish(wrongPreflightEpoch, outerProof),
            "Reach mismatch test publishes a distinct module generation");
        const ReachPreflightToken wrongPreflight = wrongPreflightPublication.Get(wrongPreflightEpoch);

        Check(ClassifyReachOuterRenderCaller(moduleBase, kReachRetailImageSize,
                  moduleBase + kReachNormalOuterReturnRva) == ReachOuterRenderCaller::NormalPlayer &&
                  ClassifyReachOuterRenderCaller(moduleBase, kReachRetailImageSize,
                      moduleBase + kReachScreenshotOuterReturnRva) == ReachOuterRenderCaller::ScreenshotTileBloom &&
                  ClassifyReachOuterRenderCaller(moduleBase, kReachRetailImageSize, moduleBase + 1) ==
                      ReachOuterRenderCaller::Unknown &&
                  ClassifyReachOuterRenderCaller(moduleBase, kReachRetailImageSize - 1,
                      moduleBase + kReachNormalOuterReturnRva) == ReachOuterRenderCaller::Unknown,
            "Reach outer routing distinguishes normal, screenshot, and unknown callers exactly");

        const auto outerRejects = [&outer](auto mutate)
        {
            auto candidate = outer;
            mutate(candidate);
            return !ReachNormalOuterInputMatches(candidate);
        };
        Check(ReachNormalOuterInputMatches(outer) && outerRejects([](auto& v) { v.preflight = {}; }) &&
                  outerRejects([](auto& v) { v.freshCamera = {}; }) &&
                  outerRejects([](auto& v) { v.preparedFrame = {}; }) &&
                  outerRejects([&wrongPreflight](auto& v) { v.preflight = wrongPreflight; }) &&
                  outerRejects([](auto& v)
                      { v.preparedFrame = ReachPreparedFrameToken::Create(v.preflight.Epoch(), 43, true); }) &&
                  outerRejects([](auto& v) { v.teardownRequested = true; }) &&
                  outerRejects([](auto& v) { v.nowMs = 0; }) && outerRejects([](auto& v) { ++v.moduleBase; }) &&
                  outerRejects([](auto& v) { ++v.playerWindowIndex; }) &&
                  outerRejects([](auto& v) { v.cameraStackDepthBefore = -2; }) &&
                  outerRejects([](auto& v) { v.cameraStackDepthBefore = 3; }) &&
                  outerRejects([](auto& v) { ++v.workspace; }) && outerRejects([](auto& v) { ++v.playerView; }) &&
                  outerRejects([](auto& v) { v.returnAddress = v.moduleBase + kReachScreenshotOuterReturnRva; }),
            "Only the exact fresh slot-zero normal owner can mint a Reach token");

        ReachRenderOwnerGate owner;
        Check(!owner.TryBegin(outer, ownerFreshness) && ownerFreshness.IsCurrent(outer.freshCamera) &&
                  owner.AdvanceEpoch(epoch) && !owner.AdvanceEpoch(epoch) && !owner.AdvanceEpoch({moduleBase, 8}) &&
                  owner.TryBegin(outer, ownerFreshness) && !ownerFreshness.IsCurrent(outer.freshCamera) &&
                  owner.Token().Active() && owner.Token().PreparedFrameSerial() == 42 &&
                  !owner.TryBegin(outer, ownerFreshness),
            "Reach owner gate requires an explicit monotonic epoch and rejects nesting");
        const ReachRenderOwnerToken ownerToken = owner.Token();
        Check(!owner.Finish(ownerToken, {}) && owner.IsCurrent(ownerToken),
            "A Reach owner cannot finish without a bound clean-completion token");

        ReachInnerRenderInput inner{};
        inner.returnAddress = moduleBase + kReachPlayerViewRenderReturnRva;
        inner.playerView = outer.playerView;
        inner.activeView = outer.playerView;
        inner.cameraStackDepth = 0;
        inner.topWorkspace = outer.workspace;
        inner.workspaceCallback = moduleBase + kReachCameraStackCallbackRva;
        inner.renderCameraOwner = outer.playerView + kReachPlayerViewCameraStateOffset;
        inner.selectedSpecialization = 0;
        inner.primaryCameraValid = true;
        inner.secondaryCameraValid = true;
        inner.preparedFrame = outer.preparedFrame;
        ReachDirectCopyGate directCopyGate;
        ReachDisplaySurfaceProof innerDisplayProof = CompleteReachDisplaySurfaceProof(epoch, outer.preflight);
        Check(directCopyGate.AdvanceEpoch(epoch) && directCopyGate.Publish(innerDisplayProof),
            "Reach inner admission begins with a live cold display-resource owner");
        inner.displayContinuity = innerDisplayProof.continuity;
        inner.directCopy = directCopyGate.Prepare(inner.preparedFrame, inner.displayContinuity);

        const auto innerRejects = [&owner, &inner, &directCopyGate](auto mutate)
        {
            auto candidate = inner;
            mutate(candidate);
            return !ReachInnerScopeMatches(owner, owner.Token(), directCopyGate, candidate);
        };
        Check(ReachInnerScopeMatches(owner, owner.Token(), directCopyGate, inner) &&
                  innerRejects([](auto& v) { ++v.returnAddress; }) &&
                  innerRejects([](auto& v) { v.preparedFrame = {}; }) &&
                  innerRejects([](auto& v)
                      { v.preparedFrame = ReachPreparedFrameToken::Create(v.directCopy.Epoch(), 43, true); }) &&
                  innerRejects([](auto& v) { v.directCopy = {}; }) &&
                  innerRejects(
                      [&directCopyGate](auto& v)
                      {
                          v.directCopy = directCopyGate.Prepare(
                              ReachPreparedFrameToken::Create(v.preparedFrame.Epoch(), 43, true), v.displayContinuity);
                      }) &&
                  innerRejects([](auto& v) { ++v.displayContinuity.surfaceArrayIdentity; }) &&
                  innerRejects([](auto& v) { ++v.playerView; }) && innerRejects([](auto& v) { ++v.activeView; }) &&
                  innerRejects([](auto& v) { ++v.cameraStackDepth; }) &&
                  innerRejects([](auto& v) { ++v.topWorkspace; }) &&
                  innerRejects([](auto& v) { ++v.workspaceCallback; }) &&
                  innerRejects([](auto& v) { ++v.renderCameraOwner; }) &&
                  innerRejects([](auto& v) { ++v.selectedSpecialization; }) &&
                  innerRejects([](auto& v) { v.primaryCameraValid = false; }) &&
                  innerRejects([](auto& v) { v.secondaryCameraValid = false; }) &&
                  innerRejects([](auto& v) { v.teardownRequested = true; }),
            "Reach inner admission checks the exact return edge, epoch, stack, camera, and target");

        const ReachPreflightToken incompletePreflight{};
        auto wrongInner = inner;
        ++wrongInner.returnAddress;
        Check(SelectReachRenderAction(false, outer.preflight, owner, ownerToken, directCopyGate, inner) ==
                      ReachRenderAction::StockOnce &&
                  SelectReachRenderAction(true, incompletePreflight, owner, ownerToken, directCopyGate, inner) ==
                      ReachRenderAction::StockOnce &&
                  SelectReachRenderAction(true, outer.preflight, owner, ownerToken, directCopyGate, wrongInner) ==
                      ReachRenderAction::StockOnce &&
                  SelectReachRenderAction(true, outer.preflight, owner, ownerToken, directCopyGate, inner) ==
                      ReachRenderAction::StereoTransaction,
            "Reach action selection consumes bound proof, owner, and inner-scope inputs");
        Check(SelectReachRenderAction(ReachAdapter_RuntimeHooksPermitted(), outer.preflight, owner, ownerToken,
                  directCopyGate, inner) == ReachRenderAction::StockOnce,
            "The adapter hard gate keeps an otherwise complete Reach scope stock-once");
        ReachLoadedImagePreflight invalidLoadedImage{};
        ReachLoadedImageModulePin invalidModulePin;
        ReachRenderCandidate_ColdPoll(0, 0, 0, false);
        Check(
            ReachRenderCandidate_Compiled() && !ReachRenderCandidate_RuntimeHooksEnabled() &&
                !ReachRender_RunLoadedImagePreflight(0, kReachRetailImageSize, invalidLoadedImage, invalidModulePin) &&
                !invalidModulePin.Valid() && invalidLoadedImage.failure == ReachLoadedImageFailure::InvalidInput &&
                !ReachRenderCandidate_GetPreflight(epoch).Complete() && !ReachRenderCandidate_IsPreflightCurrent({}) &&
                ReachRenderCandidate_SelectAction(outer.preflight, owner, ownerToken, directCopyGate, inner) ==
                    ReachRenderAction::StockOnce,
            "The compiled DLL-facing Reach wrapper remains hard-disabled");

        ReachRollbackGate rollback;
        Check(rollback.Bind(owner, ownerToken) && rollback.BeginFirstPass(ownerToken) && rollback.NeedsRollback() &&
                  !rollback.BeginFinalPass(ownerToken) && rollback.MarkFirstPassRestored(ownerToken) &&
                  !rollback.NeedsRollback() && rollback.BeginFinalPass(ownerToken) && rollback.NeedsRollback(),
            "Reach cleanup state tracks dirty first and final passes independently");
        const ReachCleanupToken completedCleanup = rollback.MarkFinalPassRestoredAndComplete(ownerToken);
        Check(completedCleanup.Valid() && rollback.Finished() && owner.Finish(ownerToken, completedCleanup) &&
                  !owner.Token().Active() && owner.LastCompletedSerial() == 42 &&
                  SelectReachRenderAction(true, outer.preflight, owner, ownerToken, directCopyGate, inner) ==
                      ReachRenderAction::StockOnce,
            "Owner completion requires clean rollback and invalidates every copied owner token");

        outer.freshCamera = ownerFreshness.Observe(1200, epoch, 43, true, true);
        outer.nowMs = 1200;
        outer.preparedFrame = ReachPreparedFrameToken::Create(epoch, 43, true);
        Check(
            owner.TryBegin(outer, ownerFreshness), "A fresh prepared-frame serial can mint the next Reach owner token");
        const ReachRenderOwnerToken abortedToken = owner.Token();
        ReachRollbackGate abortedRollback;
        Check(abortedRollback.Bind(owner, abortedToken) && abortedRollback.BeginFirstPass(abortedToken) &&
                  abortedRollback.NeedsRollback() && !owner.Abort(abortedToken, {}) &&
                  abortedRollback.MarkDirtyPassRestoredForAbort(abortedToken),
            "A dirty aborted pass must explicitly transition through restored state");
        const ReachCleanupToken abortedCleanup = abortedRollback.AbortClean(abortedToken);
        Check(abortedCleanup.Valid() && abortedRollback.Aborted() && owner.Abort(abortedToken, abortedCleanup) &&
                  !owner.TryBegin(outer, ownerFreshness),
            "A clean abort consumes its exact prepared-frame serial");
        Check(owner.Teardown(epoch) && ownerFreshness.Teardown(epoch) && !owner.AdvanceEpoch(epoch) &&
                  !owner.AdvanceEpoch({moduleBase, 8}),
            "Teardown retains the generation high-water mark against stale replay");
        const ReachModuleEpoch nextEpoch{moduleBase, 10};
        Check(outerPreflightPublication.Publish(nextEpoch, outerProof),
            "A newer module generation receives a newer preflight publication");
        outer.preflight = outerPreflightPublication.Get(nextEpoch);
        Check(owner.AdvanceEpoch(nextEpoch) && ownerFreshness.AdvanceEpoch(nextEpoch) &&
                  !ownerFreshness.Observe(100, nextEpoch, 39, true, true).Stable() &&
                  !ownerFreshness.Observe(400, nextEpoch, 40, true, true).Stable() &&
                  !ownerFreshness.Observe(700, nextEpoch, 41, true, true).Stable() &&
                  !ownerFreshness.Observe(1000, nextEpoch, 42, true, true).Stable(),
            "A newer Reach generation starts fresh owner and freshness state");
        outer.freshCamera = ownerFreshness.Observe(1101, nextEpoch, 43, true, true);
        outer.nowMs = 1101;
        outer.preparedFrame = ReachPreparedFrameToken::Create(nextEpoch, 43, true);
        Check(owner.TryBegin(outer, ownerFreshness), "A newer Reach module generation starts a fresh serial namespace");
        const ReachRenderOwnerToken nextToken = owner.Token();
        ReachRollbackGate nextRollback;
        Check(nextRollback.Bind(owner, nextToken), "The next generation binds cleanup to its live owner");
        const ReachCleanupToken nextAbort = nextRollback.AbortClean(nextToken);
        Check(!owner.Abort(nextToken, abortedCleanup) && owner.IsCurrent(nextToken) &&
                  owner.Abort(nextToken, nextAbort) && owner.Teardown(nextEpoch) && ownerFreshness.Teardown(nextEpoch),
            "A same-serial cleanup from an old generation cannot close the new owner");

        Check(ReachEyeForPass(0, false) == 0 && ReachEyeForPass(1, false) == 1 && ReachEyeForPass(0, true) == 1 &&
                  ReachEyeForPass(1, true) == 0 && ReachEyeForPass(2, false) == -1,
            "Reach pass order matches Halo 3 right-eye-first behavior exactly");
        const ReachStereoPassPolicy firstPass = SelectReachStereoPassPolicy(0, false, 1);
        const ReachStereoPassPolicy finalPass = SelectReachStereoPassPolicy(1, false, 1);
        const ReachStereoPassPolicy invalidPass = SelectReachStereoPassPolicy(2, false, 1);
        Check(firstPass.valid && firstPass.eye == 0 && firstPass.writeLastWindow && firstPass.lastWindowInput == 0 &&
                  firstPass.restoreLastWindowAfterPass && finalPass.valid && finalPass.eye == 1 &&
                  finalPass.writeLastWindow && finalPass.lastWindowInput == 1 &&
                  !finalPass.restoreLastWindowAfterPass && !invalidPass.valid && invalidPass.eye == -1 &&
                  !invalidPass.writeLastWindow && invalidPass.restoreLastWindowAfterPass,
            "Reach last-window policy fails closed for invalid passes and preserves the final result");
        Check(kReachRollbackLayout.workspaceSize == 0x2B0 && kReachCameraPairDataSize == 0x2A8 &&
                  kReachSecondaryDerivedOffset + kReachDerivedBlockSize == kReachCameraPairDataSize &&
                  kReachRollbackLayout.cameraStateOffset == 0x3B0 && kReachRollbackLayout.cameraStateSize == 0xC8 &&
                  kReachRollbackLayout.currentMatricesOffset == 0x490 &&
                  kReachRollbackLayout.currentMatricesSize == 0x2D0 &&
                  kReachRollbackLayout.previousMatricesOffset == 0x760 &&
                  kReachRollbackLayout.previousMatricesSize == 0x2D0 &&
                  kReachRollbackLayout.excludedLastWindowOffset == 0xA30 &&
                  kReachRollbackLayout.workspaceSize < kReachPlayerViewStride,
            "Reach rollback policy snapshots bounded regions and excludes the whole player view");
        Check(kReachVisibilityClusterLookupCallRva == 0x000C3320 &&
                  kReachVisibilityClusterLookupTargetRva == 0x00273458 &&
                  kReachVisibilitySecondaryCompactLeaRva == 0x00273468 && kReachVisibilityBuildCallRva == 0x000C335C &&
                  kReachVisibilityBuildTargetRva == 0x0027F408 &&
                  kReachVisibilitySecondaryDerivedLeaRva == 0x000C3339 &&
                  kReachVisibilitySecondaryCompactAddressRva == 0x00C9FC34 &&
                  kReachVisibilitySecondaryDerivedAddressRva == 0x00C9FCC4,
            "Reach visibility evidence identifies the exact secondary camera pair consumer");

        constexpr float reachRad = 3.14159265358979323846f / 180.0f;
        const ReachSymmetricFovCover fovCover = SelectReachSymmetricFovCover(
            -61.5f * reachRad, 43.4f * reachRad, 53.0f * reachRad, -53.0f * reachRad, 2912, 2100);
        const float halfV = fovCover.verticalFov * 0.5f;
        const float halfH = std::atan(std::tan(halfV) * (2912.0f / 2100.0f));
        const ReachProjectionHalfFovs decodedFov =
            DecodeReachProjectionHalfFovs(1.0f / std::tan(halfH), 1.0f / std::tan(halfV));
        Check(fovCover.valid && decodedFov.valid && std::fabs(decodedFov.horizontal - 61.5f * reachRad) < 0.0001f &&
                  ReachProjectionCoversOpenXr(decodedFov, fovCover),
            "Reach raster and OpenXR FOVs cover the same headset view");
        Check(!SelectReachSymmetricFovCover(0.1f, 0.2f, 0.3f, -0.3f, 2912, 2100).valid &&
                  !SelectReachSymmetricFovCover(-0.8f, 0.8f, 0.8f, -0.8f, 0, 2100).valid &&
                  !DecodeReachProjectionHalfFovs(0.0f, 1.0f).valid &&
                  !ReachProjectionCoversOpenXr(DecodeReachProjectionHalfFovs(2.0f, 2.0f), fovCover),
            "Reach FOV proof rejects invalid and under-covering projections");

        const std::array<ReachEyeCullFrustum, 2> asymmetricEyes{{
            {-61.5f * reachRad, 43.4f * reachRad, 53.0f * reachRad, -48.0f * reachRad, {0.0f, 0.0f, 0.0f, 1.0f}},
            {-43.4f * reachRad, 61.5f * reachRad, 49.0f * reachRad, -53.0f * reachRad, {0.0f, 0.0f, 0.0f, 1.0f}},
        }};
        const ReachSymmetricFovCover stereoIdentityCover = SelectReachStereoCullFovCover(asymmetricEyes, 2912, 2100);
        const float stereoIdentityHalfV = stereoIdentityCover.verticalFov * 0.5f;
        const float stereoIdentityHalfH = std::atan(std::tan(stereoIdentityHalfV) * (2912.0f / 2100.0f));
        Check(stereoIdentityCover.valid &&
                  std::fabs(stereoIdentityCover.requiredHalfHorizontal - 61.5f * reachRad) < 0.0001f &&
                  std::fabs(stereoIdentityCover.requiredHalfVertical - 53.0f * reachRad) < 0.0001f &&
                  stereoIdentityHalfH + 0.0001f >= stereoIdentityCover.requiredHalfHorizontal &&
                  stereoIdentityHalfV + 0.0001f >= stereoIdentityCover.requiredHalfVertical,
            "Reach stereo cull FOV encloses all eight asymmetric identity-eye corners");

        const auto yawQuaternion = [](float yaw, float scale = 1.0f)
        { return std::array<float, 4>{0.0f, std::sin(yaw * 0.5f) * scale, 0.0f, std::cos(yaw * 0.5f) * scale}; };

        // Adversarial headset geometry: each raw OpenXR eye is much wider on
        // its nasal side, while opposed cant rotates the originally narrow
        // temporal side outward. Reach rasterizes a symmetric per-eye FOV, so
        // the outer cull must contain those widened raster corners rather than
        // only the raw asymmetric angles.
        const std::array<ReachEyeCullFrustum, 2> adversarialRawEyes{{
            {-20.0f * reachRad, 70.0f * reachRad, 45.0f * reachRad, -35.0f * reachRad, yawQuaternion(10.0f * reachRad)},
            {-70.0f * reachRad, 20.0f * reachRad, 42.0f * reachRad, -38.0f * reachRad,
                yawQuaternion(-10.0f * reachRad)},
        }};
        const std::array<ReachSymmetricFovCover, 2> adversarialRasterCovers{{
            SelectReachSymmetricFovCover(adversarialRawEyes[0].angleLeft, adversarialRawEyes[0].angleRight,
                adversarialRawEyes[0].angleUp, adversarialRawEyes[0].angleDown, 2912, 2100),
            SelectReachSymmetricFovCover(adversarialRawEyes[1].angleLeft, adversarialRawEyes[1].angleRight,
                adversarialRawEyes[1].angleUp, adversarialRawEyes[1].angleDown, 2912, 2100),
        }};
        std::array<ReachEyeCullFrustum, 2> adversarialRasterEyes{};
        const bool builtAdversarialLeft = BuildReachSymmetricRasterCullFrustum(adversarialRasterCovers[0],
            adversarialRawEyes[0].relativeOrientation, 2912, 2100, adversarialRasterEyes[0]);
        const bool builtAdversarialRight = BuildReachSymmetricRasterCullFrustum(adversarialRasterCovers[1],
            adversarialRawEyes[1].relativeOrientation, 2912, 2100, adversarialRasterEyes[1]);
        const float adversarialRasterHalfV = adversarialRasterCovers[0].verticalFov * 0.5f;
        const float adversarialRasterHalfH = std::atan(std::tan(adversarialRasterHalfV) * (2912.0f / 2100.0f));
        Check(builtAdversarialLeft && builtAdversarialRight &&
                  std::fabs(adversarialRasterEyes[0].angleLeft + adversarialRasterHalfH) < 0.000001f &&
                  std::fabs(adversarialRasterEyes[0].angleRight - adversarialRasterHalfH) < 0.000001f &&
                  std::fabs(adversarialRasterEyes[0].angleUp - adversarialRasterHalfV) < 0.000001f &&
                  std::fabs(adversarialRasterEyes[0].angleDown + adversarialRasterHalfV) < 0.000001f &&
                  adversarialRasterEyes[0].angleLeft < adversarialRawEyes[0].angleLeft - 40.0f * reachRad &&
                  adversarialRasterEyes[1].angleRight > adversarialRawEyes[1].angleRight + 40.0f * reachRad &&
                  adversarialRasterEyes[0].relativeOrientation == adversarialRawEyes[0].relativeOrientation &&
                  adversarialRasterEyes[1].relativeOrientation == adversarialRawEyes[1].relativeOrientation,
            "Reach cull inputs describe the actual widened symmetric eye rasters");

        const ReachSymmetricFovCover adversarialRawCull = SelectReachStereoCullFovCover(adversarialRawEyes, 2912, 2100);
        const ReachSymmetricFovCover adversarialRasterCull =
            SelectReachStereoCullFovCover(adversarialRasterEyes, 2912, 2100);
        const float adversarialCullHalfV = adversarialRasterCull.verticalFov * 0.5f;
        const float adversarialCullHalfH = std::atan(std::tan(adversarialCullHalfV) * (2912.0f / 2100.0f));
        Check(adversarialRawCull.valid && adversarialRasterCull.valid &&
                  std::fabs(adversarialRasterCull.requiredHalfHorizontal - 80.0f * reachRad) < 0.0001f &&
                  adversarialRasterCull.requiredHalfHorizontal >
                      adversarialRawCull.requiredHalfHorizontal + 15.0f * reachRad &&
                  adversarialRasterCull.requiredHalfVertical >
                      adversarialRawCull.requiredHalfVertical + 15.0f * reachRad &&
                  adversarialCullHalfH + 0.0001f >= adversarialRasterCull.requiredHalfHorizontal &&
                  adversarialCullHalfV + 0.0001f >= adversarialRasterCull.requiredHalfVertical,
            "Reach binocular cull encloses widened asymmetric-and-canted raster corners");

        ReachEyeCullFrustum rejectedRasterFrustum{-0.5f, 0.5f, 0.5f, -0.5f, {0.0f, 0.0f, 0.0f, 1.0f}};
        ReachSymmetricFovCover invalidRasterCover = adversarialRasterCovers[0];
        invalidRasterCover.valid = false;
        const bool rejectsInvalidRasterCover = !BuildReachSymmetricRasterCullFrustum(
            invalidRasterCover, adversarialRawEyes[0].relativeOrientation, 2912, 2100, rejectedRasterFrustum);
        const bool clearsRejectedRasterFrustum =
            rejectedRasterFrustum.angleLeft == 0.0f && rejectedRasterFrustum.angleRight == 0.0f &&
            rejectedRasterFrustum.angleUp == 0.0f && rejectedRasterFrustum.angleDown == 0.0f;
        Check(rejectsInvalidRasterCover && clearsRejectedRasterFrustum &&
                  !BuildReachSymmetricRasterCullFrustum(
                      adversarialRasterCovers[0], {0.0f, 0.0f, 0.0f, 0.0f}, 2912, 2100, rejectedRasterFrustum) &&
                  !BuildReachSymmetricRasterCullFrustum(adversarialRasterCovers[0],
                      adversarialRawEyes[0].relativeOrientation, 2912, 0, rejectedRasterFrustum),
            "Reach symmetric raster cull conversion fails closed on invalid input");

        const std::array<ReachEyeCullFrustum, 2> cantedEyes{{
            {-40.0f * reachRad, 40.0f * reachRad, 35.0f * reachRad, -35.0f * reachRad, yawQuaternion(10.0f * reachRad)},
            {-40.0f * reachRad, 40.0f * reachRad, 35.0f * reachRad, -35.0f * reachRad,
                yawQuaternion(-10.0f * reachRad)},
        }};
        const ReachSymmetricFovCover cantedCover = SelectReachStereoCullFovCover(cantedEyes, 2912, 2100);
        std::array<ReachEyeCullFrustum, 2> scaledCantedEyes = cantedEyes;
        for (ReachEyeCullFrustum& eye : scaledCantedEyes)
            for (float& component : eye.relativeOrientation)
                component *= -3.25f;
        const ReachSymmetricFovCover scaledCantedCover = SelectReachStereoCullFovCover(scaledCantedEyes, 2912, 2100);
        const std::array<ReachEyeCullFrustum, 2> swappedCantedEyes{{cantedEyes[1], cantedEyes[0]}};
        const ReachSymmetricFovCover swappedCantedCover = SelectReachStereoCullFovCover(swappedCantedEyes, 2912, 2100);
        Check(
            cantedCover.valid && scaledCantedCover.valid && swappedCantedCover.valid &&
                std::fabs(cantedCover.requiredHalfHorizontal - 50.0f * reachRad) < 0.0001f &&
                cantedCover.requiredHalfVertical > 35.0f * reachRad &&
                std::fabs(cantedCover.verticalFov - scaledCantedCover.verticalFov) < 0.000001f &&
                std::fabs(cantedCover.requiredHalfHorizontal - swappedCantedCover.requiredHalfHorizontal) < 0.000001f &&
                std::fabs(cantedCover.requiredHalfVertical - swappedCantedCover.requiredHalfVertical) < 0.000001f,
            "Reach stereo cull FOV normalizes quaternion scale and is invariant to eye order");

        bool yawEnvelopeProperty = true;
        constexpr std::array<float, 5> yawDegrees{0.0f, 2.5f, 6.0f, 10.0f, 15.0f};
        constexpr std::array<float, 4> quaternionScales{0.25f, 1.0f, 3.0f, -2.0f};
        for (float yawDegreesValue : yawDegrees)
        {
            const float yaw = yawDegreesValue * reachRad;
            ReachSymmetricFovCover reference{};
            for (size_t scaleIndex = 0; scaleIndex < quaternionScales.size(); ++scaleIndex)
            {
                const float scale = quaternionScales[scaleIndex];
                const std::array<ReachEyeCullFrustum, 2> propertyEyes{{
                    {-30.0f * reachRad, 30.0f * reachRad, 25.0f * reachRad, -25.0f * reachRad,
                        yawQuaternion(yaw, scale)},
                    {-30.0f * reachRad, 30.0f * reachRad, 25.0f * reachRad, -25.0f * reachRad,
                        yawQuaternion(-yaw, scale)},
                }};
                const ReachSymmetricFovCover candidate = SelectReachStereoCullFovCover(propertyEyes, 2912, 2100);
                yawEnvelopeProperty =
                    yawEnvelopeProperty && candidate.valid &&
                    std::fabs(candidate.requiredHalfHorizontal - (30.0f + yawDegreesValue) * reachRad) < 0.0001f;
                if (scaleIndex == 0)
                    reference = candidate;
                else
                    yawEnvelopeProperty =
                        yawEnvelopeProperty && std::fabs(candidate.verticalFov - reference.verticalFov) < 0.000001f &&
                        std::fabs(candidate.requiredHalfVertical - reference.requiredHalfVertical) < 0.000001f;
            }
        }
        Check(yawEnvelopeProperty, "Reach stereo cull FOV encloses deterministic opposed-yaw properties");

        std::array<ReachEyeCullFrustum, 2> invalidStereoEyes = asymmetricEyes;
        invalidStereoEyes[0].relativeOrientation = {0.0f, 0.0f, 0.0f, 0.0f};
        const bool rejectsZeroQuaternion = !SelectReachStereoCullFovCover(invalidStereoEyes, 2912, 2100).valid;
        invalidStereoEyes = asymmetricEyes;
        invalidStereoEyes[1].relativeOrientation[2] = std::numeric_limits<float>::quiet_NaN();
        const bool rejectsNonFiniteQuaternion = !SelectReachStereoCullFovCover(invalidStereoEyes, 2912, 2100).valid;
        invalidStereoEyes = asymmetricEyes;
        invalidStereoEyes[0].angleLeft = -std::numeric_limits<float>::infinity();
        const bool rejectsNonFiniteFov = !SelectReachStereoCullFovCover(invalidStereoEyes, 2912, 2100).valid;
        invalidStereoEyes = asymmetricEyes;
        invalidStereoEyes[0].angleRight = 1.57079632679489661923f;
        const bool rejectsRearHemisphereFov = !SelectReachStereoCullFovCover(invalidStereoEyes, 2912, 2100).valid;
        const std::array<ReachEyeCullFrustum, 2> behindCenterEyes{{
            {-20.0f * reachRad, 20.0f * reachRad, 20.0f * reachRad, -20.0f * reachRad, yawQuaternion(80.0f * reachRad)},
            {-20.0f * reachRad, 20.0f * reachRad, 20.0f * reachRad, -20.0f * reachRad,
                yawQuaternion(-80.0f * reachRad)},
        }};
        Check(rejectsZeroQuaternion && rejectsNonFiniteQuaternion && rejectsNonFiniteFov && rejectsRearHemisphereFov &&
                  !SelectReachStereoCullFovCover(behindCenterEyes, 2912, 2100).valid &&
                  !SelectReachStereoCullFovCover(asymmetricEyes, 0, 2100).valid,
            "Reach stereo cull FOV rejects degenerate, non-finite, and behind-centre inputs");
    }

    CheckUnit(38);
    {
        std::array<uint8_t, 0x90> compactCamera{};
        const auto store = [&compactCamera](size_t offset, const auto& value)
        { std::memcpy(compactCamera.data() + offset, &value, sizeof(value)); };
        const float position[3]{10.0f, -2.0f, 5.0f};
        const float forward[3]{1.0f, 0.0f, 0.0f};
        const float up[3]{0.0f, 0.0f, 1.0f};
        const float verticalFov = 1.0f;
        const ReachObservedRect windowBounds{0, 0, 1080, 1920};
        const ReachObservedRect renderBounds{0, 0, 720, 1280};
        const ReachObservedRect clientBounds{0, 0, 720, 1280};
        store(0x00, position);
        store(0x0C, forward);
        store(0x18, up);
        store(0x28, verticalFov);
        store(0x38, windowBounds);
        store(0x4C, renderBounds);
        store(0x5C, clientBounds);

        ReachCompactCameraObservation observedCamera{};
        Check(ValidateReachCompactCamera(compactCamera.data(), compactCamera.size(), observedCamera) &&
                  observedCamera.position[0] == position[0] && observedCamera.position[1] == position[1] &&
                  observedCamera.position[2] == position[2] && observedCamera.verticalFov == verticalFov &&
                  observedCamera.clientBounds.x1 == clientBounds.x1,
            "Reach compact-camera proof accepts and decodes its exact validated layout");
        Check(!ValidateReachCompactCamera(nullptr, compactCamera.size(), observedCamera) &&
                  !ValidateReachCompactCamera(compactCamera.data(), compactCamera.size() - 1, observedCamera),
            "Reach compact-camera proof rejects null or truncated snapshots");

        const auto validateWithFloat = [&compactCamera, &observedCamera](size_t offset, float value)
        {
            auto candidate = compactCamera;
            std::memcpy(candidate.data() + offset, &value, sizeof(value));
            return ValidateReachCompactCamera(candidate.data(), candidate.size(), observedCamera);
        };
        Check(!validateWithFloat(0x00, std::numeric_limits<float>::quiet_NaN()) &&
                  !validateWithFloat(0x0C, std::numeric_limits<float>::infinity()),
            "Reach compact-camera proof rejects NaN and infinite vectors");
        Check(validateWithFloat(0x0C, std::sqrt(1.0005f)) && !validateWithFloat(0x0C, std::sqrt(1.0015f)),
            "Reach compact-camera proof enforces the HREK axis-length tolerance");

        {
            auto candidate = compactCamera;
            const float nearOrthogonalUp[3]{0.0005f, 0.0f, std::sqrt(1.0f - 0.0005f * 0.0005f)};
            std::memcpy(candidate.data() + 0x18, nearOrthogonalUp, sizeof(nearOrthogonalUp));
            Check(ValidateReachCompactCamera(candidate.data(), candidate.size(), observedCamera),
                "Reach compact-camera proof accepts dot products inside tolerance");
        }
        {
            auto candidate = compactCamera;
            const float nonOrthogonalUp[3]{0.0015f, 0.0f, std::sqrt(1.0f - 0.0015f * 0.0015f)};
            std::memcpy(candidate.data() + 0x18, nonOrthogonalUp, sizeof(nonOrthogonalUp));
            Check(!ValidateReachCompactCamera(candidate.data(), candidate.size(), observedCamera),
                "Reach compact-camera proof rejects non-orthogonal axes");
        }

        Check(!validateWithFloat(0x28, kReachCameraFovMin) &&
                  validateWithFloat(0x28, std::nextafter(kReachCameraFovMin, std::numeric_limits<float>::infinity())) &&
                  validateWithFloat(0x28, std::nextafter(kReachCameraFovMax, 0.0f)) &&
                  !validateWithFloat(0x28, kReachCameraFovMax),
            "Reach compact-camera proof enforces strict finite FOV bounds");
        {
            auto candidate = compactCamera;
            const ReachObservedRect unorderedWindow{1, 0, 1, 1920};
            std::memcpy(candidate.data() + 0x38, &unorderedWindow, sizeof(unorderedWindow));
            Check(!ValidateReachCompactCamera(candidate.data(), candidate.size(), observedCamera),
                "Reach compact-camera proof rejects unordered viewport bounds");
        }
        {
            auto candidate = compactCamera;
            const ReachObservedRect nonzeroClientOrigin{1, 0, 720, 1280};
            std::memcpy(candidate.data() + 0x5C, &nonzeroClientOrigin, sizeof(nonzeroClientOrigin));
            Check(!ValidateReachCompactCamera(candidate.data(), candidate.size(), observedCamera),
                "Reach compact-camera proof requires the proven zero client origin");
        }
        {
            auto candidate = compactCamera;
            const ReachObservedRect undersizedClient{0, 0, 7, 1280};
            std::memcpy(candidate.data() + 0x5C, &undersizedClient, sizeof(undersizedClient));
            Check(!ValidateReachCompactCamera(candidate.data(), candidate.size(), observedCamera),
                "Reach compact-camera proof enforces the producer's minimum extent");
        }

        ReachObserverFreshnessWindow freshness;
        Check(!freshness.ObserveTransaction(10), "One Reach transaction cannot satisfy freshness stability");
        Check(!freshness.ObserveTransaction(400),
            "A sub-500 ms Reach transaction preserves but cannot yet arm freshness");
        Check(!freshness.ObserveTransaction(800), "Repeated fresh Reach transactions stay below the safety interval");
        Check(!freshness.ObserveTransaction(1010),
            "Reach freshness remains unarmed through exactly one continuous second");
        Check(freshness.IsFresh(1010) && freshness.TransactionCount() == 4 && freshness.CurrentSpanMs() == 1000,
            "Reach freshness counts only sub-500 ms transaction intervals");
        Check(freshness.ObserveTransaction(1011) && freshness.IsStable(1011),
            "Reach freshness becomes observationally stable only after one second");
        Check(freshness.Tick(1510), "A 499 ms gap keeps the Reach transaction window fresh");
        Check(!freshness.Tick(1511) && freshness.TransactionCount() == 0,
            "A 500 ms gap resets the Reach freshness window");
        Check(!freshness.ObserveTransaction(2000), "A new Reach transaction starts a fresh observational window");
        Check(
            !freshness.ObserveTransaction(2500) && freshness.TransactionCount() == 1 && freshness.CurrentSpanMs() == 0,
            "A missed Reach pulse is inconclusive and begins a new window");
        Check(!freshness.ObserveTransaction(2400) && freshness.TransactionCount() == 1,
            "A non-monotonic Reach observation also fails closed");
        freshness.Reset();
        Check(!freshness.IsFresh(3000) && !freshness.IsStable(3000) && freshness.LastTransactionMs() == 0,
            "Reach freshness reset clears all observational state");

        Check(!freshness.ObserveTransaction(4000) && !freshness.ObserveTransaction(4400) &&
                  !freshness.ObserveTransaction(4800) && !freshness.Tick(5001) && freshness.CurrentSpanMs() == 800,
            "Elapsed wall time cannot satisfy a Reach gate without a new transaction");
        Check(freshness.ObserveTransaction(5101) && freshness.CurrentSpanMs() == 1101,
            "A new Reach transaction may satisfy the strict one-second span");
        Check(!freshness.Tick(5601) && freshness.TransactionCount() == 0,
            "A post-gate 500 ms pause expires the Reach freshness window");
        Check(!freshness.ObserveTransaction(6000) && !freshness.ObserveTransaction(6400) &&
                  !freshness.ObserveTransaction(6800) && freshness.ObserveTransaction(7001),
            "Reach freshness can establish a new stable window after pause recovery");

        std::array<ReachObserverFreshnessWindow, 4> slotFreshness{};
        Check(ReachObserverUniqueFreshOwner(slotFreshness.data(), slotFreshness.size(), 10) == kReachNoFreshOwner,
            "Reach observer reports no owner before a slot heartbeat");
        slotFreshness[0].ObserveTransaction(10);
        Check(ReachObserverUniqueFreshOwner(slotFreshness.data(), slotFreshness.size(), 10) == 0,
            "Reach observer identifies one fresh player-view owner");
        slotFreshness[1].ObserveTransaction(20);
        Check(
            ReachObserverUniqueFreshOwner(slotFreshness.data(), slotFreshness.size(), 20) == kReachMultipleFreshOwners,
            "Reach observer fails closed when two player slots are fresh");
        Check(ReachObserverUniqueFreshOwner(slotFreshness.data(), slotFreshness.size(), 510) == 1,
            "Reach observer expires a stale slot independently");
    }

    CheckUnit(229);
    const float degree = 3.14159265358979323846f / 180.0f;
    Check(std::fabs(std::atan(ExpandVisibilityTangent(std::tan(55 * degree))) / degree - 60) < 0.001f &&
              ExpandVisibilityTangent(-1) == -1 &&
              ExpandVisibilityTangent(std::tan(87 * degree)) == std::tan(87 * degree),
        "visibility guard covers five extra degrees per edge without narrowing an already wider view");
    const float sideHand[3]{0.0f, 0.3f, 0.0f};
    const float sideReticle[3]{10.0f, 0.3f, 0.0f};
    float handShot[3]{};
    Check(ReachBuildHandShotDirection(
              4, 4, 0x12340007, 0x12340007, 0x12340007, 500, 450, sideHand, sideReticle, handShot) &&
              std::fabs(handShot[0] - 1.0f) < 1e-6f && std::fabs(handShot[1]) < 1e-6f,
        "Reach side-positioned handgun ray remains parallel at close and distant ranges");
    Check(!ReachBuildHandShotDirection(
              4, 4, 0x12340007, 0x56780007, 0x12340007, 500, 450, sideHand, sideReticle, handShot) &&
              !ReachBuildHandShotDirection(4, 3, 7, 7, 7, 500, 450, sideHand, sideReticle, handShot) &&
              !ReachBuildHandShotDirection(4, 4, 7, 7, 7, 600, 450, sideHand, sideReticle, handShot) &&
              !ReachBuildHandShotDirection(4, 4, 7, 7, 7, 400, 450, sideHand, sideReticle, handShot) &&
              !ReachBuildHandShotDirection(4, 4, 7, 7, 7, 500, 450, sideHand, sideHand, handShot),
        "Reach hand shot rejects recycled units, old titles, stale/future poses, and zero rays");
    const float invalidHandTarget[3]{std::numeric_limits<float>::infinity(), 0, 0};
    Check(!ReachBuildHandShotDirection(4, 4, 7, 7, 7, 500, 450, sideHand, invalidHandTarget, handShot) &&
              !ReachBuildHandShotDirection(4, 4, 7, 7, 7, 500, 450, nullptr, sideReticle, handShot) &&
              ReachBuildHandShotDirection(4, 4, 7, 7, 7, 550, 450, sideHand, sideReticle, handShot),
        "Reach hand shot rejects nonfinite/null inputs and admits the exact freshness boundary");
    // Read-only live fp_battle_rifle datum FA361801: the import address is zero,
    // followed by one compression record at byte offset 012A9664.
    // clang-format off
    std::array<uint8_t,28> h2LiveModelHeader{
        0xE6,0x2C,0x00,0x0F,0x0C,0x00,0x00,0x00,
        0x0F,0x10,0x1C,0x1D,0,0,0,0,0,0,0,0,
        1,0,0,0,0x64,0x96,0x2A,0x01};
    // clang-format on
    int32_t h2CompressionOffset = 0;
    Check(Halo2ReadWeaponCompressionHeader(h2LiveModelHeader, h2CompressionOffset) && h2CompressionOffset == 0x012A9664,
        "H2 loaded battle-rifle compression follows the empty import block");
    Check(!Halo2ReadWeaponCompressionHeader(
              std::span<const uint8_t>(h2LiveModelHeader.data(), 27), h2CompressionOffset) &&
              h2CompressionOffset == 0,
        "H2 compression rejects a truncated loaded model");
    h2LiveModelHeader[0x14] = 0;
    Check(!Halo2ReadWeaponCompressionHeader(h2LiveModelHeader, h2CompressionOffset),
        "H2 compression does not read missing records");
    h2LiveModelHeader[0x14] = 2;
    Check(!Halo2ReadWeaponCompressionHeader(h2LiveModelHeader, h2CompressionOffset),
        "H2 compression does not guess between multiple records");
    h2LiveModelHeader[0x14] = 1;
    for (int byte = 0x18; byte < 0x1C; ++byte)
        h2LiveModelHeader[byte] = 0;
    Check(!Halo2ReadWeaponCompressionHeader(h2LiveModelHeader, h2CompressionOffset),
        "H2 compression rejects a nonempty count with a null address");
    // Exact pinned H2 tag getter: the ADD opcode's 05 is not displacement data.
    // clang-format off
    std::array<uint8_t, 26> h2TagGetter{
        0x48,0x8B,0x05,0x89,0x5C,0xE4,0x00,0x0F,0xB7,0xC9,
        0x48,0x03,0xC9,0x48,0x63,0x44,0xC8,0x08,
        0x48,0x03,0x05,0x7F,0x5C,0xE4,0x00,0xC3};
    // clang-format on
    Check(Halo2CollisionTagBaseSlot(0x180000000ull, 0x2A38000, 0x79EEA0, h2TagGetter) == 0x1815E4B38ull &&
              Halo2CollisionTagBaseSlot(0x180000000ull, 0x1000, 0x79EEA0, h2TagGetter) == 0 &&
              Halo2CollisionTagBaseSlot(
                  0x180000000ull, 0x2A38000, 0x79EEA0, std::span<const uint8_t>{h2TagGetter.data(), 25}) == 0,
        "H2 tag-data slot decodes instruction boundaries and rejects truncated or outside-image data");
    h2TagGetter[0x14] = 0x0D;
    Check(Halo2CollisionTagBaseSlot(0x180000000ull, 0x2A38000, 0x79EEA0, h2TagGetter) == 0,
        "H2 collision tag-data decoder rejects a changed native instruction");
    CheckUnit(237);
    // Reach muzzle retarget: every first-person system off the majority
    // marker is moved onto it; ties break toward the lower location index
    // (primary_trigger is index 0 in all six affected weapons).
    {
        // AR: round@0, smoke@0, long_brake@2, glow@0, all mode 1 -> one move.
        const unsigned short arM[] = {1, 1, 1, 1};
        const unsigned short arL[] = {0, 0, 2, 0};
        const ReachMuzzleRetargetPlan ar = ReachDecideMuzzleRetarget(arM, arL, 4);
        Check(ar.count == 1 && ar.elements[0] == 2 && ar.newLocation == 0,
            "The AR's odd first-person system is retargeted onto its siblings'");
        // DMR: two odd systems at location 2 -> both move.
        const unsigned short dmrM[] = {1, 1, 1, 1, 1};
        const unsigned short dmrL[] = {2, 2, 0, 0, 0};
        const ReachMuzzleRetargetPlan dmr = ReachDecideMuzzleRetarget(dmrM, dmrL, 5);
        Check(dmr.count == 2 && dmr.newLocation == 0 && dmr.elements[0] == 0 && dmr.elements[1] == 1,
            "Both DMR odd systems are retargeted");
        // Sniper: 4 at 0 vs 4 at 1 - tie breaks to the lower index.
        const unsigned short snM[] = {1, 1, 1, 1, 1, 1, 1, 1};
        const unsigned short snL[] = {0, 0, 1, 0, 1, 0, 1, 1};
        const ReachMuzzleRetargetPlan sn = ReachDecideMuzzleRetarget(snM, snL, 8);
        Check(sn.count == 4 && sn.newLocation == 0, "A tie between locations resolves to the lower index");
        // Uniform event: nothing to do.
        const unsigned short uniM[] = {1, 1, 1, 1, 1};
        const unsigned short uniL[] = {2, 2, 2, 2, 2};
        Check(ReachDecideMuzzleRetarget(uniM, uniL, 5).count == 0,
            "An event whose systems share one location is never touched");
        // Mode-0 systems at other markers never make a weapon eligible.
        const unsigned short othM[] = {0, 0, 1, 1, 1};
        const unsigned short othL[] = {1, 5, 0, 0, 0};
        Check(ReachDecideMuzzleRetarget(othM, othL, 5).count == 0,
            "Mode-0 systems at other markers never make a weapon eligible");
        // No agreeing pair -> ambiguous -> untouched.
        const unsigned short thinM[] = {1, 1, 0};
        const unsigned short thinL[] = {0, 2, 0};
        Check(ReachDecideMuzzleRetarget(thinM, thinL, 3).count == 0,
            "A single sibling is not a majority; nothing is moved");
    }

    CheckUnit(-1);
    std::cout << "checks run: " << g_checks << '\n';
    if (g_failures == 0)
        std::cout << "HaloMCCVR core reach preflight hud tests passed\n";
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
