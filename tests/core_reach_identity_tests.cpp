// core_reach_identity_tests.cpp: Reach body and leg identity, fingerprints, live-graph commit, native and vehicle identity pins, seat trims.
// Units 29, 30, 31, 32, 33, 34, 35, 36, 195, 196 of the former tests/core_tests.cpp main(), moved unchanged and in
// their order (E3). Built as halomccvr_core_reach_identity_tests and run by ctest; each unit prints its check count.
#include "core_test_support.h"

int main()
{
    CheckUnit(29);
    {
        static_assert(kReachSpartanFpBodyBoneMap.size() == 47);
        static_assert(kReachEliteFpBodyBoneMap.size() == 41);
        static_assert(kReachFpMaxSourceNodeCount == 120);

        // Independent evidence literals: do not derive these from the
        // production constants, so any fingerprint drift fails this test.
        // clang-format off
        constexpr std::array<int32_t, 47> expectedSpartan{{
            0, 3, 1, 2, 5, 4, 6, 9, 10, 8, 7, 13, 12, 14, 11, 22,
            26, 18, 21, 24, 19, 20, 15, 23, 16, 17, 25, 36, 35, 32,
            28, 33, 29, 27, 34, 31, 30, 46, 38, 42, 37, 40, 39, 43,
            // clang-format on
            45, 44, 41}};
        // clang-format off
        constexpr std::array<int32_t, 41> expectedElite{{
            0, 1, 2, 3, 5, 4, 6, 7, 8, 9, 10, 14, 12, 11, 13, 22,
            20, 18, 21, 19, 16, 23, 24, 17, 15, 27, 28, 25, 31, 32,
                // clang-format on
                30, 26, 29, 35, 34, 39, 33, 40, 36, 38, 37}};
        Check(kReachSpartanFpBodyBoneMap == expectedSpartan && kReachEliteFpBodyBoneMap == expectedElite,
            "Reach production fingerprints match independent full evidence literals");
        Check(expectedSpartan[6] == 6 && expectedSpartan[7] == 9 && expectedSpartan[8] == 10 &&
                  expectedSpartan[11] == 13 && expectedSpartan[13] == 14 && expectedSpartan[4] == 5 &&
                  expectedSpartan[10] == 7 && expectedSpartan[14] == 11 && expectedSpartan[5] == 4 &&
                  expectedElite[6] == 6 && expectedElite[9] == 9 && expectedElite[10] == 10 &&
                  expectedElite[11] == 14 && expectedElite[14] == 13 && expectedElite[4] == 5 &&
                  expectedElite[7] == 7 && expectedElite[13] == 11 && expectedElite[5] == 4,
            "Reach render-output chains map exactly to the shared source chains");

        const std::span<const int32_t> spartanMap{kReachSpartanFpBodyBoneMap};
        ReachFpBodyLayout spartan{};
        Check(ResolveReachFpBodyLayout(spartanMap, 65, spartan) && spartan.Valid() &&
                  spartan.kind == ReachFpBodyKind::Spartan && spartan.paletteBodyNodeCount == 47 &&
                  spartan.liveSourceNodeCount == 65,
            "Reach Spartan keeps the 47-node body distinct from the 65-node source");
        Check(spartan.rightShoulderSource == 6 && spartan.rightElbowSource == 9 && spartan.rightWristSource == 13 &&
                  spartan.leftShoulderSource == 5 && spartan.leftElbowSource == 7 && spartan.leftWristSource == 11 &&
                  spartan.cameraControlSource == 4,
            "Reach Spartan resolves the exact source arm and camera anchors");
        Check(spartan.leftHandSourceDescendants == 0x000003E0F81F8800ull &&
                  spartan.rightHandSourceDescendants == 0x00007C1F07E02000ull,
            "Reach Spartan maps exact hand descendants into source order");
        Check(kReachLeftControllerOwnedAuxiliarySourceMask == 0x00000000000011A0ull &&
                  kReachRightControllerOwnedAuxiliarySourceMask == 0x0000000000004640ull &&
                  spartan.leftControllerOwnedSourceBranch == 0x000003E0F81F99A0ull &&
                  spartan.rightControllerOwnedSourceBranch == 0x00007C1F07E06640ull &&
                  (spartan.leftControllerOwnedSourceBranch ^ spartan.leftHandSourceDescendants) ==
                      kReachLeftControllerOwnedAuxiliarySourceMask &&
                  (spartan.rightControllerOwnedSourceBranch ^ spartan.rightHandSourceDescendants) ==
                      kReachRightControllerOwnedAuxiliarySourceMask &&
                  (spartan.leftControllerOwnedSourceBranch & ~spartan.leftHandSourceDescendants) ==
                      kReachLeftControllerOwnedAuxiliarySourceMask &&
                  (spartan.rightControllerOwnedSourceBranch & ~spartan.rightHandSourceDescendants) ==
                      kReachRightControllerOwnedAuxiliarySourceMask &&
                  (spartan.leftHandSourceDescendants & kReachLeftControllerOwnedAuxiliarySourceMask) == 0 &&
                  (spartan.rightHandSourceDescendants & kReachRightControllerOwnedAuxiliarySourceMask) == 0 &&
                  (spartan.leftControllerOwnedSourceBranch & spartan.rightHandSourceDescendants) == 0 &&
                  (spartan.leftControllerOwnedSourceBranch & spartan.rightControllerOwnedSourceBranch) == 0 &&
                  (spartan.leftControllerOwnedSourceBranch >> spartan.paletteBodyNodeCount) == 0 &&
                  (spartan.rightControllerOwnedSourceBranch >> spartan.paletteBodyNodeCount) == 0,
            "Reach Spartan controllers own the exact hidden skin-influence closures");
        auto brokenSpartanOwnership = spartan;
        brokenSpartanOwnership.leftControllerOwnedSourceBranch ^= uint64_t{1} << 12;
        Check(!brokenSpartanOwnership.Valid(), "Reach Spartan rejects an incomplete left controller influence branch");
        auto brokenSpartanRightOwnership = spartan;
        brokenSpartanRightOwnership.rightControllerOwnedSourceBranch ^= uint64_t{1} << 14;
        Check(!brokenSpartanRightOwnership.Valid(),
            "Reach Spartan rejects an incomplete right controller influence branch");
        Check((spartan.leftHandSourceDescendants & (uint64_t{1} << spartan.leftWristSource)) != 0 &&
                  (spartan.rightHandSourceDescendants & (uint64_t{1} << spartan.rightWristSource)) != 0 &&
                  (spartan.leftHandSourceDescendants & spartan.rightHandSourceDescendants) == 0 &&
                  (spartan.leftHandSourceDescendants >> spartan.paletteBodyNodeCount) == 0,
            "Reach Spartan floating hands are disjoint exact wrist subtrees and exclude held objects");
    }

    CheckUnit(30);
    {
        const std::span<const int32_t> spartanMap{kReachSpartanFpBodyBoneMap};
        ReachFpBodyLayout spartan{};
        Check(ResolveReachFpBodyLayout(spartanMap, 65, spartan) &&
                  spartan.leftHandPaletteDescendants == kReachSpartanLeftHandPaletteMask &&
                  spartan.rightHandPaletteDescendants == kReachSpartanRightHandPaletteMask &&
                  !ReachFpSourceIndexIsHeldObject(spartan, -1) && !ReachFpSourceIndexIsHeldObject(spartan, 46) &&
                  ReachFpSourceIndexIsHeldObject(spartan, 47) && ReachFpSourceIndexIsHeldObject(spartan, 64) &&
                  !ReachFpSourceIndexIsHeldObject(spartan, 65),
            "Reach Spartan held-object range covers appended indices 47 through 64");
        ReachFpBodyLayout maximumSource{};
        Check(ResolveReachFpBodyLayout(spartanMap, kReachFpMaxSourceNodeCount, maximumSource) &&
                  maximumSource.paletteBodyNodeCount == 47 && maximumSource.liveSourceNodeCount == 120 &&
                  ReachFpSourceIndexIsHeldObject(maximumSource, 119),
            "Reach body resolution accepts the bounded 120-node source span");

        const std::span<const int32_t> eliteMap{kReachEliteFpBodyBoneMap};
        ReachFpBodyLayout elite{};
        Check(ResolveReachFpBodyLayout(eliteMap, 59, elite) && elite.Valid() && elite.kind == ReachFpBodyKind::Elite &&
                  elite.paletteBodyNodeCount == 41 && elite.liveSourceNodeCount == 59 &&
                  elite.rightShoulderSource == 6 && elite.rightElbowSource == 9 && elite.rightWristSource == 13 &&
                  elite.leftShoulderSource == 5 && elite.leftElbowSource == 7 && elite.leftWristSource == 11 &&
                  elite.cameraControlSource == 4,
            "Reach Elite resolves its 41-node body over the shared arm prefix");
        Check(elite.leftHandPaletteDescendants == kReachEliteLeftHandPaletteMask &&
                  elite.rightHandPaletteDescendants == kReachEliteRightHandPaletteMask &&
                  elite.leftHandSourceDescendants == 0x0000001E1E0F8800ull &&
                  elite.rightHandSourceDescendants == 0x000001E1E1F02000ull &&
                  !ReachFpSourceIndexIsHeldObject(elite, 40) && ReachFpSourceIndexIsHeldObject(elite, 41) &&
                  ReachFpSourceIndexIsHeldObject(elite, 58) && !ReachFpSourceIndexIsHeldObject(elite, 59),
            "Reach Elite maps exact hand sets and appended held-object range");
        Check(elite.leftControllerOwnedSourceBranch == 0x0000001E1E0F99A0ull &&
                  elite.rightControllerOwnedSourceBranch == 0x000001E1E1F06640ull &&
                  (elite.leftControllerOwnedSourceBranch ^ elite.leftHandSourceDescendants) ==
                      kReachLeftControllerOwnedAuxiliarySourceMask &&
                  (elite.rightControllerOwnedSourceBranch ^ elite.rightHandSourceDescendants) ==
                      kReachRightControllerOwnedAuxiliarySourceMask &&
                  (elite.leftControllerOwnedSourceBranch & ~elite.leftHandSourceDescendants) ==
                      kReachLeftControllerOwnedAuxiliarySourceMask &&
                  (elite.rightControllerOwnedSourceBranch & ~elite.rightHandSourceDescendants) ==
                      kReachRightControllerOwnedAuxiliarySourceMask &&
                  (elite.leftHandSourceDescendants & kReachLeftControllerOwnedAuxiliarySourceMask) == 0 &&
                  (elite.rightHandSourceDescendants & kReachRightControllerOwnedAuxiliarySourceMask) == 0 &&
                  (elite.leftControllerOwnedSourceBranch & elite.rightHandSourceDescendants) == 0 &&
                  (elite.leftControllerOwnedSourceBranch & elite.rightControllerOwnedSourceBranch) == 0 &&
                  (elite.leftControllerOwnedSourceBranch >> elite.paletteBodyNodeCount) == 0 &&
                  (elite.rightControllerOwnedSourceBranch >> elite.paletteBodyNodeCount) == 0,
            "Reach Elite controllers own the exact shared hidden arm branches");
        Check((elite.leftHandSourceDescendants & (uint64_t{1} << elite.leftWristSource)) != 0 &&
                  (elite.rightHandSourceDescendants & (uint64_t{1} << elite.rightWristSource)) != 0 &&
                  (elite.leftHandSourceDescendants & elite.rightHandSourceDescendants) == 0 &&
                  (elite.leftHandSourceDescendants >> elite.paletteBodyNodeCount) == 0,
            "Reach Elite floating hands are disjoint exact wrist subtrees and exclude held objects");
    }

    CheckUnit(31);
    {
        const std::span<const int32_t> spartanMap{kReachSpartanFpBodyBoneMap};
        auto wrongFingerprint = kReachSpartanFpBodyBoneMap;
        const int32_t first = wrongFingerprint[0];
        wrongFingerprint[0] = wrongFingerprint[1];
        wrongFingerprint[1] = first;
        ReachFpBodyLayout rejected{};
        Check(!ResolveReachFpBodyLayout(std::span<const int32_t>{wrongFingerprint}, 65, rejected) &&
                  !rejected.Valid() && rejected.kind == ReachFpBodyKind::None && rejected.paletteBodyNodeCount == 0 &&
                  rejected.liveSourceNodeCount == 0,
            "Reach rejects a full permutation with the wrong exact fingerprint");

        auto duplicate = kReachSpartanFpBodyBoneMap;
        duplicate[1] = duplicate[0];
        auto negative = kReachSpartanFpBodyBoneMap;
        negative[0] = -1;
        auto appendedInBody = kReachSpartanFpBodyBoneMap;
        appendedInBody[0] = 47;
        Check(!ResolveReachFpBodyLayout(std::span<const int32_t>{duplicate}, 65, rejected) &&
                  !ResolveReachFpBodyLayout(std::span<const int32_t>{negative}, 65, rejected) &&
                  !ResolveReachFpBodyLayout(std::span<const int32_t>{appendedInBody}, 65, rejected),
            "Reach rejects duplicate, negative, and appended body-map indices");

        ReachFpBodyLayout accepted{};
        Check(ResolveReachFpBodyLayout(spartanMap, 65, accepted),
            "Reach prepares a valid layout before fail-closed reset testing");
        rejected = accepted;
        Check(!ResolveReachFpBodyLayout(spartanMap.first(46), 65, rejected) && !rejected.Valid() &&
                  rejected.rightShoulderSource == -1 && rejected.rightHandSourceDescendants == 0,
            "Reach rejects a wrong palette span and clears stale output");
        Check(!ResolveReachFpBodyLayout(spartanMap, 46, rejected) &&
                  !ResolveReachFpBodyLayout(spartanMap, kReachFpMaxSourceNodeCount + 1, rejected) &&
                  !ResolveReachFpBodyLayout(std::span<const int32_t>{}, 65, rejected) &&
                  !ResolveReachFpBodyLayout(std::span<const int32_t>{kReachEliteFpBodyBoneMap}, 40, rejected),
            "Reach rejects unsafe live source counts and empty maps");
    }

    CheckUnit(32);
    {
        using LegKind = ReachFpLegPaletteKind;
        Check(ClassifyReachFpLegPalette(kReachSpartanFpLegRuntimeImportChecksum, kReachSpartanFpLegNodeCount) ==
                      LegKind::Spartan &&
                  ClassifyReachFpLegPalette(kReachEliteFpLegRuntimeImportChecksum, kReachEliteFpLegNodeCount) ==
                      LegKind::Elite,
            "Reach admits the exact HREK Spartan and Elite first-person leg identities");

        // Counts collide with the ordinary world render models, so their
        // distinct HREK checksums must remain on the collapse path.
        Check(ClassifyReachFpLegPalette(0x171B0502u, 82) == LegKind::None &&
                  ClassifyReachFpLegPalette(0x171C1D0Fu, 67) == LegKind::None,
            "Reach rejects same-count world Spartan and Elite render models as legs");
        Check(ClassifyReachFpLegPalette(kReachSpartanFpLegRuntimeImportChecksum, 81) == LegKind::None &&
                  ClassifyReachFpLegPalette(kReachEliteFpLegRuntimeImportChecksum, 68) == LegKind::None &&
                  ClassifyReachFpLegPalette(kReachSpartanFpLegRuntimeImportChecksum + 1, 82) == LegKind::None &&
                  ClassifyReachFpLegPalette(0, 0) == LegKind::None,
            "Reach leg identity rejects altered and empty observations");

        constexpr uint16_t kLegTag = 0x3902;
        constexpr uint16_t kArmsTag = 0x3901;
        Check(!ReachFpShouldCollapseVisiblePalette(true, LegKind::Spartan, kLegTag, kLegTag) &&
                  ReachFpLegNodeCountMatchesKind(LegKind::Spartan, kReachSpartanFpLegNodeCount) &&
                  ReachFpShouldCollapseVisiblePalette(true, LegKind::Spartan, kLegTag, kArmsTag) &&
                  !ReachFpLegNodeCountMatchesKind(LegKind::Spartan, kReachEliteFpLegNodeCount),
            "Reach preserves only the exact leg palette while exact arms still collapse");
        Check(ReachFpShouldCollapseVisiblePalette(false, LegKind::Spartan, kLegTag, kLegTag) &&
                  ReachFpShouldCollapseVisiblePalette(true, LegKind::None, kLegTag, kLegTag) &&
                  ReachFpShouldCollapseVisiblePalette(true, LegKind::Elite, 0x4702, kLegTag),
            "Reach collapses when legs are disabled, unproven, or tag-mismatched");
        Check(!ReachFpLegObservationCanValidate(true, 9, 100, 9, 100) &&
                  ReachFpLegObservationCanValidate(true, 9, 100, 9, 101) &&
                  !ReachFpLegObservationCanValidate(true, 9, 100, 10, 101) &&
                  !ReachFpLegObservationCanValidate(false, 9, 100, 9, 101) &&
                  !ReachFpLegObservationCanValidate(true, 9, 0, 9, 101),
            "Reach verifies a leg observation only on a later pair in the same title generation");
    }

    CheckUnit(33);
    {
        constexpr uint32_t kGeneration = 7;
        constexpr uint64_t kDiscoverySerial = 100;
        const auto decide = [](uint32_t learnedGeneration, uint64_t learnedSerial, uint32_t pairGeneration,
                                uint64_t pairSerial, bool valid = true, bool invalidated = false)
        {
            return DecideReachFpPairLayout(
                valid, invalidated, learnedGeneration, learnedSerial, pairGeneration, pairSerial);
        };

        Check(decide(kGeneration, kDiscoverySerial, kGeneration, kDiscoverySerial) == ReachFpPairLayoutDecision::Stock,
            "Reach keeps the discovery pair stock");
        Check(decide(kGeneration, kDiscoverySerial, kGeneration, kDiscoverySerial + 1) ==
                  ReachFpPairLayoutDecision::Active,
            "Reach activates a learned layout on a later prepared serial");
        Check(decide(kGeneration, kDiscoverySerial, kGeneration, kDiscoverySerial - 1) ==
                      ReachFpPairLayoutDecision::Stock &&
                  decide(kGeneration, kDiscoverySerial, kGeneration, kDiscoverySerial) ==
                      ReachFpPairLayoutDecision::Stock,
            "Reach rejects stale and same-serial layout activation");
        Check(decide(kGeneration, kDiscoverySerial, kGeneration + 1, kDiscoverySerial + 1) ==
                      ReachFpPairLayoutDecision::Stock &&
                  decide(0, kDiscoverySerial, 0, kDiscoverySerial + 1) == ReachFpPairLayoutDecision::Stock,
            "Reach rejects generation mismatch and zero-generation replay");
        Check(decide(kGeneration, kDiscoverySerial, kGeneration, kDiscoverySerial + 1, true, true) ==
                      ReachFpPairLayoutDecision::Stock &&
                  decide(kGeneration, kDiscoverySerial, kGeneration, kDiscoverySerial + 1, false, false) ==
                      ReachFpPairLayoutDecision::Stock &&
                  decide(kGeneration, 0, kGeneration, 1) == ReachFpPairLayoutDecision::Stock &&
                  decide(kGeneration, kDiscoverySerial, kGeneration, 0) == ReachFpPairLayoutDecision::Stock,
            "Reach invalidation, missing learning, and zero serials remain stock");

        const auto evaluateEyeOrder = [&](const std::array<int, 2>& eyeOrder)
        {
            std::array<ReachFpPairLayoutDecision, 2> eyeDecisions{
                ReachFpPairLayoutDecision::Stock, ReachFpPairLayoutDecision::Stock};
            for (int eye : eyeOrder)
            {
                eyeDecisions[static_cast<size_t>(eye)] =
                    decide(kGeneration, kDiscoverySerial, kGeneration, kDiscoverySerial + 1);
            }
            return eyeDecisions;
        };
        const auto leftFirst = evaluateEyeOrder({0, 1});
        const auto rightFirst = evaluateEyeOrder({1, 0});
        Check(leftFirst == rightFirst && leftFirst[0] == ReachFpPairLayoutDecision::Active &&
                  leftFirst[1] == ReachFpPairLayoutDecision::Active,
            "Reach pair activation is independent of stereo eye order");
    }

    CheckUnit(34);
    {
        using Action = ReachFpPaletteAction;
        const auto decide =
            [](bool current, bool frozen, bool transformed, bool exactBody, bool exactMatch, bool learnedTag)
        { return DecideReachFpPaletteAction(current, frozen, transformed, exactBody, exactMatch, learnedTag); };
        Check(decide(true, false, false, true, false, false) == Action::LearnStockOnly,
            "Reach first exact body discovery is stock-only");
        const Action precedingWeapon = decide(true, true, true, false, false, false);
        const Action body = decide(true, true, true, true, true, true);
        const Action followingWeapon = decide(true, true, true, false, false, false);
        Check(precedingWeapon == Action::ArticulateKnownTransaction && body == Action::ArticulateKnownTransaction &&
                  followingWeapon == Action::ArticulateKnownTransaction,
            "Reach reconstructs every final palette in a known source transaction");
        Check(decide(true, true, true, true, false, true) == Action::RestoreStockAndInvalidate &&
                  decide(true, true, true, true, false, false) == Action::RestoreStockAndInvalidate &&
                  decide(true, true, false, false, false, true) == Action::RestoreStockAndInvalidate,
            "Reach changed maps, body-tag transitions, and invalid known-body maps restore stock");
        Check(decide(true, true, false, false, false, false) == Action::PassThroughLive &&
                  decide(false, true, true, true, true, true) == Action::PassThroughLive,
            "Reach unsolved and stale transactions remain stock");
    }

    CheckUnit(35);
    {
        constexpr size_t kPlasmaLauncherNodes = 65;
        constexpr size_t kFloats = kPlasmaLauncherNodes * kReachFpBoneMatrixFloatCount;
        std::array<float, kFloats> stock{};
        for (size_t node = 0; node < kPlasmaLauncherNodes; ++node)
            stock[node * kReachFpBoneMatrixFloatCount] = 1.0f;
        auto candidate = stock;
        candidate[12] = 3.5f;
        candidate[kReachFpBoneMatrixFloatCount + 10] = -2.0f;
        auto live = stock;
        bool commitCalled = false;
        const bool committed = ReachFpCommitGraphIfFinite(std::span<const float>{candidate}, kPlasmaLauncherNodes,
            [&]()
            {
                commitCalled = true;
                live = candidate;
                return true;
            });
        Check(committed && commitCalled && live == candidate &&
                  ReachFpPackedGraphFinite(std::span<const float>{live}, kPlasmaLauncherNodes),
            "Reach validates and atomically commits a real 65-node live graph");

        const auto committedLive = live;
        auto nanCandidate = candidate;
        nanCandidate[3 * kReachFpBoneMatrixFloatCount + 6] = std::numeric_limits<float>::quiet_NaN();
        commitCalled = false;
        Check(!ReachFpCommitGraphIfFinite(std::span<const float>{nanCandidate}, kPlasmaLauncherNodes,
                  [&]()
                  {
                      commitCalled = true;
                      live = nanCandidate;
                      return true;
                  }) &&
                  !commitCalled && live == committedLive,
            "Reach NaN rejection leaves the stock/live graph byte-identical");

        auto invalidRoot = candidate;
        invalidRoot[0] = 0.0f;
        commitCalled = false;
        Check(
            !ReachFpCommitGraphIfFinite(std::span<const float>{invalidRoot}, kPlasmaLauncherNodes,
                [&]()
                {
                    commitCalled = true;
                    return true;
                }) &&
                !commitCalled &&
                !ReachFpPackedGraphFinite(std::span<const float>{candidate}.first(kFloats - 1), kPlasmaLauncherNodes) &&
                !ReachFpPackedGraphFinite(std::span<const float>{candidate}, kReachFpMaxSourceNodeCount + 1),
            "Reach invalid roots, malformed spans, and over-120 graphs never commit");
    }

    CheckUnit(36);
    {
        static_assert(kReachMainRenderViewAob.size() == 32);
        static_assert(kReachPlayerViewRenderAob.size() == 69);
        static_assert(kReachCameraStackCallbackAob.size() == 28);
        static_assert(kReachFrustumHelperAob.size() == 25);
        Check(std::string_view(kReachMainRenderViewBodySha256) ==
                      "95DF3EFFF9AC6EE29887D1272CCA8D7BF3E58F87041BAD8032107825B733FE89" &&
                  std::string_view(kReachPlayerViewRenderBodySha256) ==
                      "2628D1189621EACED7C95A1F295815D70E7783054F1C3CBA46799F838CC33C60" &&
                  std::string_view(kReachCameraStackCallbackBodySha256) ==
                      "6E2A249710A53498ADE7AFB12EE7414099D16315B2F06D90D8EC01D185E6B0C4" &&
                  kReachMainRenderViewBodySize == 515 && kReachPlayerViewRenderBodySize == 2314 &&
                  kReachCameraStackCallbackBodySize == 0x6C,
            "Reach render candidate pins the renderer and outer-camera callback identities");

        constexpr uint32_t vehicleGeneration = 37;
        constexpr uint64_t onFootSnapshot =
            ReachVehicleInputSnapshot(vehicleGeneration, ReachVehicleInputState::OnFoot);
        constexpr uint64_t vehicleSnapshot =
            ReachVehicleInputSnapshot(vehicleGeneration, ReachVehicleInputState::Vehicle);
        Check(kReachPlayerUnitByOutputUserRva == 0x00053EF8 && kReachUnitInVehicleRva == 0x004F9368 &&
                  kReachUnitInVehicleEvaluatorRva == 0x0019EF28 && kReachUnitInVehicleEvaluatorCallRva == 0x0019EF5E &&
                  kReachUnitInVehicleNameRva == 0x009FB710 && kReachUnitInVehicleDescriptorRva == 0x00A22B60 &&
                  kReachEngineTlsIndexRva == 0x00C17B18 &&
                  !ReachVehicleInputSnapshotIsVehicle(onFootSnapshot, vehicleGeneration) &&
                  ReachVehicleInputSnapshotIsVehicle(vehicleSnapshot, vehicleGeneration) &&
                  !ReachVehicleInputSnapshotIsVehicle(vehicleSnapshot, vehicleGeneration + 1) &&
                  !ReachVehicleInputSnapshotIsVehicle(vehicleSnapshot, 0),
            "Reach vehicle input pins the HREK-matched retail identities and rejects stale generations");

        Check(kReachUnitGetCameraInfoRva == 0x0048A4B4 && kReachUnitGetCameraInfoBodySize == 0x45A &&
                  kReachObjectMarkerResolverRva == 0x0047044C && kReachObjectMarkerResolverBodySize == 0x2DB &&
                  kReachObjectUltimateParentRva == 0x00473DC0 && kReachObjectUltimateParentBodySize == 0x3F &&
                  kReachVehicleTypeAccessorRva == 0x004AC1E4 && kReachVehicleTypeAccessorBodySize == 0x51 &&
                  kReachTagGetRva == 0x00031AE8 && kReachTagGetBodySize == 0x7B,
            "Reach native seat-camera, marker, carrier, type and tag bindings are pinned");
        // The trim bank carries one row MORE than the identity list -
        // the unmatched row every unresolved vehicle keys instead of the
        // shared universal trim.
        // Halo 3 seat flags, from real values recorded in ODST: the
        // Warthog passenger's 0x1070 fires a personal weapon, the driver's
        // 0x40014 does not. That single bit is what decides whether a seated
        // shot gets re-origined onto the engine's own eye.
        Check(kHalo3SeatAllowsWeaponsBit == (1u << 5) && kHalo3SeatThirdPersonCameraBit == (1u << 4) &&
                  Halo3SeatFiresPersonalWeapon(0x1070u) && !Halo3SeatFiresPersonalWeapon(0x40014u) &&
                  !Halo3SeatFiresPersonalWeapon(0u) && Halo3SeatFlagsLookLikePlayerSeat(0x1070u) &&
                  Halo3FirstPersonSeatFlags(0x1070u) == 0x1060u &&
                  Halo3SeatFiresPersonalWeapon(Halo3FirstPersonSeatFlags(0x1070u)),
            "Halo 3 allows-weapons is bit 5 and survives clearing the third-person bit");
        // Turret seats, both titles. A mounted gunner is a turret whatever it
        // hangs off; the walk-up emplacements and the shade are turrets on
        // their own identity. Nothing else may enter the servo.
        Check(Halo3SeatIsTurret(Halo3VehicleId::StationaryTurret, false) &&
                  Halo3SeatIsTurret(Halo3VehicleId::Warthog, true) &&
                  !Halo3SeatIsTurret(Halo3VehicleId::Warthog, false) &&
                  !Halo3SeatIsTurret(Halo3VehicleId::Unknown, false) &&
                  OdstSeatIsTurret(OdstVehicleId::StationaryTurret, false) &&
                  OdstSeatIsTurret(OdstVehicleId::Shade, false) && OdstSeatIsTurret(OdstVehicleId::Warthog, true) &&
                  !OdstSeatIsTurret(OdstVehicleId::Warthog, false) && !OdstSeatIsTurret(OdstVehicleId::Unknown, false),
            "only turret seats take the aim servo");
        {
            // The plant the loop actually drives, reproduced from measured
            // facts: MCC receives our stick through ToRawStick, whose 9000/32767
            // floor means the engine only ever hears "stop" or "at least 27.5%
            // deflection", and the aim then moves at the look rate, capped by
            // the seat's authored rate bounds (H3EK: warthog_g has NO pitch
            // cap, shade_d is 15 deg/s).
            const auto step = [](float aim, float command, float lookRateRad, float capRad, float dt)
            {
                float deflection = 0.0f;
                if (std::fabs(command) >= 1.0e-3f)
                {
                    deflection = std::fabs(command) < 0.2747f ? 0.2747f : std::fabs(command);
                    if (deflection > 1.0f)
                        deflection = 1.0f;
                    if (command < 0.0f)
                        deflection = -deflection;
                }
                float rate = deflection * lookRateRad;
                if (capRad > 0.0f)
                {
                    if (rate > capRad)
                        rate = capRad;
                    if (rate < -capRad)
                        rate = -capRad;
                }
                return aim + rate * dt;
            };
            const float dt = 1.0f / 120.0f;
            const float target = 0.30f;
            // Uncapped pitch (warthog_g) at a mid look rate. The proportional
            // form cannot rest, so it hunts forever; the servo parks.
            // Peak-to-peak of the settled aim, and how far it settled from the
            // hand. Both matter: parking anywhere is easy, parking ON the
            // target is the requirement.
            const auto settle = [&](bool useServo, float lookRateRad, float capRad, float* outFinalError)
            {
                AimServoAxis axis{};
                float aim = 0.0f;
                float commanded = 0.0f;
                float lo = 1.0e9f, hi = -1.0e9f;
                for (int frame = 0; frame < 900; ++frame)
                {
                    const float error = target - aim;
                    AimServoObserve(axis, aim, commanded);
                    float command;
                    if (useServo)
                    {
                        command =
                            AimServoCommand(axis, error, 12.0f, kAimServoRestEnterRadians, kAimServoRestExitRadians);
                    }
                    else
                    {
                        command = error * 12.0f;
                        if (command > 1.0f)
                            command = 1.0f;
                        if (command < -1.0f)
                            command = -1.0f;
                    }
                    commanded = std::fabs(command);
                    aim = step(aim, command, lookRateRad, capRad, dt);
                    if (frame >= 700)
                    {
                        if (aim < lo)
                            lo = aim;
                        if (aim > hi)
                            hi = aim;
                    }
                }
                if (outFinalError)
                    *outFinalError = std::fabs(target - aim);
                return hi - lo;
            };
            // 3 rad/s ~ 172 deg/s, a plain uncapped look rate. Measured: the
            // proportional loop never rests, cycling 0.0069 rad (0.39 deg)
            // peak to peak forever, while the servo parks dead still.
            const float stockUncapped = settle(false, 3.0f, 0.0f, nullptr);
            const float servoUncapped = settle(true, 3.0f, 0.0f, nullptr);
            Check(stockUncapped > 0.004f && servoUncapped < 0.001f,
                "uncapped pitch: the proportional loop limit-cycles, the servo parks");
            // The same actuator under the warthog turret's 60 deg/s YAW cap.
            // The cap binds only once the look rate would exceed it, and then
            // it shrinks the quantum and so the chatter -- which is exactly
            // why the player sees the wiggle up and down and not side to side.
            const float stockFast = settle(false, 6.0f, 0.0f, nullptr);
            const float stockFastCapped = settle(false, 6.0f, 1.047f, nullptr);
            Check(stockFastCapped < stockFast, "an authored rate cap shrinks the same chatter on yaw");
            // Robust across look sensitivity: the rest band is set by the
            // quantum the servo measures, so no sensitivity re-opens the
            // cycle. The parked offset is bounded by the actuator's OWN
            // resolution - a stick that can only move the aim in steps of S
            // cannot be parked closer than S/2 in the worst case, and no
            // controller can beat that. Worst case here is ~0.95 deg at the
            // top sensitivity, which the barrel-riding reticle then reports
            // honestly rather than hiding.
            bool servoStableEverywhere = true;
            for (float lookRate = 1.0f; lookRate <= 12.0f; lookRate += 0.5f)
            {
                const float quantum = kAimServoStickFloor * lookRate * dt;
                const float bound =
                    1.05f * (kAimServoRestEnterRadians > 0.6f * quantum ? kAimServoRestEnterRadians : 0.6f * quantum);
                float finalError = 0.0f;
                if (settle(true, lookRate, 0.0f, &finalError) >= 0.001f || finalError > bound)
                {
                    servoStableEverywhere = false;
                }
            }
            Check(servoStableEverywhere, "the servo parks within the actuator's own resolution at every sensitivity");
            // A real hand move must break the park and be followed.
            AimServoAxis axis{};
            float aim = 0.0f;
            float commanded = 0.0f;
            const float moved = target + 0.60f;
            bool retook = false;
            for (int frame = 0; frame < 1800; ++frame)
            {
                const float goal = frame < 900 ? target : moved;
                const float error = goal - aim;
                AimServoObserve(axis, aim, commanded);
                const float command =
                    AimServoCommand(axis, error, 12.0f, kAimServoRestEnterRadians, kAimServoRestExitRadians);
                commanded = std::fabs(command);
                if (frame > 900 && commanded != 0.0f)
                    retook = true;
                aim = step(aim, command, 3.0f, 0.0f, dt);
            }
            Check(retook && std::fabs(moved - aim) < 0.012f, "a real hand move breaks the park and the gun follows it");
            // A 15 deg/s shade cannot be made to follow a hand flick by any
            // gain, which is why its reticle must ride the barrel instead.
            static_assert(kAimServoTurretReticleRidesBarrel, "a turret's reticle reports the gun, not the hand");
        }
        {
            using aim_servo::ClampedAimCurrent;
            bool seated = true;
            auto seat = [&] { return seated; };
            Check(
                ClampedAimCurrent(1000, 1050, true, seat), "fresh turret reticle in the turret seat rides the barrel");
            Check(!ClampedAimCurrent(1000, 1101, false, seat),
                "an aim loop that stopped publishing (early return, seat left) expires the bent reticle");
            seated = false;
            Check(
                !ClampedAimCurrent(1000, 1010, true, seat), "a turret reticle never survives leaving the turret seat");
            Check(ClampedAimCurrent(1000, 1010, false, seat), "a passenger-cone clamp needs no turret seat");
            Check(!ClampedAimCurrent(0, 10, false, seat) && !ClampedAimCurrent(1000, 900, false, seat),
                "never published, or a clock step back: no clamp");
        }
        Check(kReachVehicleIdentityCount == kReachVehicleIdentityTrimCount &&
                  kReachVehicleTrimCount == kReachVehicleIdentityCount + 1 &&
                  kReachUnmatchedVehicleTrimId == kReachVehicleTrimCount &&
                  ReachVehicleSeatIsPlayer(ReachVehicleId::Falcon, 0) &&
                  ReachVehicleSeatIsPlayer(ReachVehicleId::Falcon, 3) &&
                  ReachVehicleSeatIsPlayer(ReachVehicleId::Falcon, 4) &&
                  !ReachVehicleSeatIsPlayer(ReachVehicleId::Falcon, 1) &&
                  ReachVehicleSeatIsPlayer(ReachVehicleId::Warthog, 1) &&
                  !ReachVehicleSeatIsPlayer(ReachVehicleId::Scorpion, 1) &&
                  ReachVehicleIsAircraft(ReachVehicleId::SpaceBanshee) &&
                  ReachVehicleIsWalkUpTurret(ReachVehicleId::Machinegun) &&
                  ReachVehicleIsAttachedWeapon(ReachVehicleId::WarthogGauss),
            "Reach HREK player-seat census and vehicle families stay explicit");
        // The 2026-08-04 full-census identities: the Pelican flies with four
        // passenger benches and no player driver, the Onager and corvette gun
        // are walk-up emplacements, the Scorpion's secondary MG is an
        // attached child gun, and the civilian line steers like the warthog.
        Check(ReachVehicleSeatIsPlayer(ReachVehicleId::Pelican, 4) &&
                  ReachVehicleSeatIsPlayer(ReachVehicleId::Pelican, 10) &&
                  !ReachVehicleSeatIsPlayer(ReachVehicleId::Pelican, 0) &&
                  ReachVehicleSeatIsPlayer(ReachVehicleId::SquadDropPod, 3) &&
                  ReachVehicleSeatIsPlayer(ReachVehicleId::CargoTruck, 1) &&
                  ReachVehicleIsAircraft(ReachVehicleId::Seraph) && ReachVehicleIsAircraft(ReachVehicleId::Pelican) &&
                  ReachVehicleIsWalkUpTurret(ReachVehicleId::MacCannon) &&
                  ReachVehicleIsWalkUpTurret(ReachVehicleId::CorvetteCannon) &&
                  ReachVehicleIsAttachedWeapon(ReachVehicleId::ScorpionAntiInfantry) &&
                  ReachVehicleIsAttachedWeapon(ReachVehicleId::SpacePhantomBeamTurret) &&
                  ReachVehicleIsLookSteered(ReachVehicleId::OniVan) && ReachVehicleUsesWheel(ReachVehicleId::Pickup) &&
                  !ReachVehicleSeatIsDriver(ReachVehicleId::Pelican, 4) &&
                  ReachVehicleExpectedPhysicsType(ReachVehicleId::Pelican) == 2 &&
                  ReachVehicleExpectedPhysicsType(ReachVehicleId::CorvetteCannon) == 6 &&
                  ReachVehicleExpectedPhysicsType(ReachVehicleId::Seraph) == 13,
            "The census identities carry their HREK-derived policy families");
        bool reachFingerprintsUnique = true;
        for (const auto& entry : kReachVehicleFingerprints)
        {
            reachFingerprintsUnique =
                reachFingerprintsUnique && ReachResolveVehicleFingerprint(entry.fingerprint) == entry.identity;
        }
        // The player-seat census counts each IDENTITY once; the fingerprint
        // table is larger than the identity list because retail maps carry
        // eight explicit full-tuple aliases for HREK import-rounding variants.
        int reachPlayerSeatCount = 0;
        for (int id = 1; id <= kReachVehicleIdentityCount; ++id)
            for (int seat = 0; seat < kReachVehicleSeatLimit; ++seat)
                if (ReachVehicleSeatIsPlayer(static_cast<ReachVehicleId>(id), seat))
                    ++reachPlayerSeatCount;
        ReachVehicleFingerprint alteredReachFingerprint = kReachVehicleFingerprints[0].fingerprint;
        alteredReachFingerprint.offsetZ ^= 1u;
        Check(reachFingerprintsUnique && reachPlayerSeatCount == 50 &&
                  ReachResolveVehicleFingerprint(alteredReachFingerprint) == ReachVehicleId::Unknown,
            "Reach HREK fingerprints resolve 34 identities/50 player seats and fail closed on mutation");
        // Retail aliases stay exact full tuples: six rows complete the
        // accepted-partial miss census beside the first two.
        Check(kReachVehicleFingerprintCount == kReachVehicleIdentityCount + 8 &&
                  ReachResolveVehicleFingerprint({0x3F978071, 0xBD406A82, 0xBAD03632, 0x3EC25783}) ==
                      ReachVehicleId::Warthog &&
                  ReachResolveVehicleFingerprint({0x3F2ABABB, 0x39815C02, 0x39CE6FFD, 0x3E7776E0}) ==
                      ReachVehicleId::Mongoose &&
                  ReachResolveVehicleFingerprint({0x3F978072, 0xBD406A82, 0xBAD03632, 0x3EC25783}) ==
                      ReachVehicleId::Warthog &&
                  ReachResolveVehicleFingerprint({0x3F73045A, 0x3DD8D24E, 0xBC93505D, 0x3F1EA4E9}) ==
                      ReachVehicleId::ShadeFlak &&
                  ReachResolveVehicleFingerprint({0x3F7A9153, 0x3E1B9697, 0xBC935062, 0x3F1EF5FC}) ==
                      ReachVehicleId::ShadePlasma &&
                  ReachResolveVehicleFingerprint({0x400D30E2, 0x3D70D852, 0x3D28430B, 0x3EBBF788}) ==
                      ReachVehicleId::Scorpion &&
                  ReachResolveVehicleFingerprint({0x3EE99B18, 0x3D9811F2, 0xB991445F, 0x3E8589DB}) ==
                      ReachVehicleId::Machinegun &&
                  ReachResolveVehicleFingerprint({0x40A69CFE, 0xBF19A963, 0xBB2A0198, 0x3F8AF1EF}) ==
                      ReachVehicleId::Sabre &&
                  ReachResolveVehicleFingerprint({0x3F786DCE, 0x3C8C975D, 0xBC8111A1, 0x3EE5C245}) ==
                      ReachVehicleId::Cart,
            "Eight exact retail fingerprint aliases resolve to their HREK identities");
        const ReachVehicleFingerprint shadePlasmaHrek{0x3F7A9153, 0x3E1B9697, 0xBC935063, 0x3F1EF5FC};
        const ReachVehicleFingerprint shadePlasmaRetail{0x3F7A9153, 0x3E1B9697, 0xBC935062, 0x3F1EF5FC};
        const ReachVehicleFingerprint shadeFlakHrek{0x3F73045B, 0x3DD8D24F, 0xBC93505D, 0x3F1EA4E9};
        const ReachVehicleFingerprint shadeFlakRetail{0x3F73045A, 0x3DD8D24E, 0xBC93505D, 0x3F1EA4E9};
        const ReachVehicleFingerprint machinegunHrek{0x3EE99B18, 0x3D9811F2, 0xB9914460, 0x3E8589DB};
        const ReachVehicleFingerprint machinegunRetail{0x3EE99B18, 0x3D9811F2, 0xB991445F, 0x3E8589DB};
        const ReachVehicleFingerprint plasmaTurret{0x3F000000, 0x00000000, 0x00000000, 0x3DCCCCCD};
        const ReachVehicleFingerprint scorpionAntiInfantry{0x3ED28405, 0x3DB8AD5D, 0xB8A6B78A, 0x3D8A1BD8};
        const ReachVehicleFingerprint corvetteCannon{0x40400000, 0x00000000, 0x00000000, 0x00000000};
        const ReachVehicleFingerprint warthogChaingun{0x3F1CC397, 0x3E86AFD5, 0xB3A6AFD5, 0x3E9C5885};
        const ReachVehicleFingerprint scorpionHrek{0x400D30E2, 0x3D70D852, 0x3D28430C, 0x3EBBF788};
        const ReachVehicleFingerprint scorpionRetail{0x400D30E2, 0x3D70D852, 0x3D28430B, 0x3EBBF788};
        const ReachVehicleFingerprint warthogRetail{0x3F978071, 0xBD406A82, 0xBAD03632, 0x3EC25783};
        Check(ReachVehiclePhysicsTypeMatches(ReachVehicleId::ShadePlasma, shadePlasmaHrek, 16) &&
                  ReachVehiclePhysicsTypeMatches(ReachVehicleId::ShadePlasma, shadePlasmaRetail, 6) &&
                  !ReachVehiclePhysicsTypeMatches(ReachVehicleId::ShadePlasma, shadePlasmaHrek, 6) &&
                  !ReachVehiclePhysicsTypeMatches(ReachVehicleId::ShadePlasma, shadePlasmaRetail, 16) &&
                  ReachVehiclePhysicsTypeMatches(ReachVehicleId::ShadeFlak, shadeFlakHrek, 16) &&
                  ReachVehiclePhysicsTypeMatches(ReachVehicleId::ShadeFlak, shadeFlakRetail, 6) &&
                  !ReachVehiclePhysicsTypeMatches(ReachVehicleId::ShadeFlak, shadeFlakHrek, 6) &&
                  !ReachVehiclePhysicsTypeMatches(ReachVehicleId::ShadeFlak, shadeFlakRetail, 16) &&
                  ReachVehiclePhysicsTypeMatches(ReachVehicleId::Machinegun, machinegunHrek, 16) &&
                  ReachVehiclePhysicsTypeMatches(ReachVehicleId::Machinegun, machinegunRetail, 6) &&
                  !ReachVehiclePhysicsTypeMatches(ReachVehicleId::Machinegun, machinegunHrek, 6) &&
                  !ReachVehiclePhysicsTypeMatches(ReachVehicleId::Machinegun, machinegunRetail, 16) &&
                  ReachVehiclePhysicsTypeMatches(ReachVehicleId::PlasmaTurret, plasmaTurret, 16) &&
                  ReachVehiclePhysicsTypeMatches(ReachVehicleId::PlasmaTurret, plasmaTurret, 6) &&
                  ReachVehiclePhysicsTypeMatches(ReachVehicleId::ScorpionAntiInfantry, scorpionAntiInfantry, 16) &&
                  ReachVehiclePhysicsTypeMatches(ReachVehicleId::ScorpionAntiInfantry, scorpionAntiInfantry, 6) &&
                  ReachVehiclePhysicsTypeMatches(ReachVehicleId::CorvetteCannon, corvetteCannon, 6) &&
                  ReachVehiclePhysicsTypeMatches(ReachVehicleId::Scorpion, scorpionHrek, 0) &&
                  ReachVehiclePhysicsTypeMatches(ReachVehicleId::Scorpion, scorpionRetail, 0) &&
                  ReachVehiclePhysicsTypeMatches(ReachVehicleId::Warthog, warthogRetail, 1) &&
                  // The chaingun Warthog is an HREK type-16 mounted weapon,
                  // so retail's turret bucket (6) is now an accepted pairing for
                  // its canonical tuple - measured on the rocket variant.
                  ReachVehiclePhysicsTypeMatches(ReachVehicleId::WarthogChaingun, warthogChaingun, 6) &&
                  !ReachVehiclePhysicsTypeMatches(ReachVehicleId::WarthogChaingun, warthogChaingun, 8) &&
                  !ReachVehiclePhysicsTypeMatches(ReachVehicleId::Scorpion, scorpionHrek, 6) &&
                  !ReachVehiclePhysicsTypeMatches(ReachVehicleId::ShadeFlak, shadePlasmaRetail, 6) &&
                  !ReachVehiclePhysicsTypeMatches(ReachVehicleId::Unknown, alteredReachFingerprint, -1),
            "Reach fingerprint/type proof accepts only canonical and evidenced retail pairs");
        Check(ReachVehicleSeatIsDriver(ReachVehicleId::Warthog, 0) &&
                  !ReachVehicleSeatIsDriver(ReachVehicleId::Warthog, 1) &&
                  !ReachVehicleSeatIsDriver(ReachVehicleId::WarthogChaingun, 0) &&
                  ReachVehicleIsLookSteered(ReachVehicleId::Revenant) &&
                  !ReachVehicleIsLookSteered(ReachVehicleId::Wraith) &&
                  ReachVehicleSeatFollowsHull(ReachVehicleId::Warthog, 1) &&
                  // The census no longer gates the camera, so an out-of-census
                  // seat on a proven identity rides its hull too; Unknown and
                  // walk-up turrets still claim no hull frame.
                  ReachVehicleSeatFollowsHull(ReachVehicleId::Warthog, 2) &&
                  !ReachVehicleSeatFollowsHull(ReachVehicleId::Unknown, 0) &&
                  !ReachVehicleSeatFollowsHull(ReachVehicleId::Machinegun, 0) &&
                  !ReachVehicleSeatFollowsPitch(ReachVehicleId::Sabre, 0) &&
                  ReachVehicleSeatAuthorsSteering(ReachVehicleId::Ghost, 0, true) &&
                  !ReachVehicleSeatAuthorsSteering(ReachVehicleId::Ghost, 0, false) &&
                  ReachVehicleUsesWheel(ReachVehicleId::Forklift) && !ReachVehicleUsesWheel(ReachVehicleId::Banshee),
            "Reach view-follow and steering ownership remain seat-explicit");
        // The carrier basis is intentionally refresh-rate independent: every supported
        // 72-144 Hz headset consumes that frame's render-matched carrier basis.
        // The older value-only lease (its switch since deleted) still preserves
        // full salted identities for the seat-bit lease below.
        {
            constexpr ReachSeatLeaseKey leaseKey{37, 0x12340007, 0x23450009, 0x00030021, 3};
            constexpr ReachSeatLeaseKey otherGeneration{38, 0x12340007, 0x23450009, 0x00030021, 3};
            constexpr ReachSeatLeaseKey otherUnit{37, 0x12350007, 0x23450009, 0x00030021, 3};
            constexpr ReachSeatLeaseKey otherParent{37, 0x12340007, 0x23460009, 0x00030021, 3};
            constexpr ReachSeatLeaseKey otherDefinition{37, 0x12340007, 0x23450009, 0x00040021, 3};
            constexpr ReachSeatLeaseKey otherSeat{37, 0x12340007, 0x23450009, 0x00030021, 4};
            constexpr uint32_t originalFlags = 0x04020014;
            constexpr uint32_t writtenFlags = originalFlags & ~kReachSeatThirdPersonCameraBit;
            Check(sizeof(ReachSeatLeaseKey) == 5 * sizeof(uint32_t) &&
                      sizeof(ReachSeatLeasePayload) == 7 * sizeof(uint32_t) && ReachSeatLeaseKeyValid(leaseKey) &&
                      ReachSeatLeaseKeyEqual(leaseKey, leaseKey) &&
                      !ReachSeatLeaseKeyEqual(leaseKey, otherGeneration) &&
                      !ReachSeatLeaseKeyEqual(leaseKey, otherUnit) && !ReachSeatLeaseKeyEqual(leaseKey, otherParent) &&
                      !ReachSeatLeaseKeyEqual(leaseKey, otherDefinition) &&
                      !ReachSeatLeaseKeyEqual(leaseKey, otherSeat) &&
                      !ReachSeatLeaseKeyValid({0, 0x12340007, 0x23450009, 0x00030021, 3}) &&
                      !ReachSeatLeaseKeyValid({37, -1, 0x23450009, 0x00030021, 3}) &&
                      !ReachSeatLeaseKeyValid({37, 0x12340007, -1, 0x00030021, 3}) &&
                      !ReachSeatLeaseKeyValid({37, 0x12340007, 0x23450009, 0xFFFFFFFFu, 3}) &&
                      !ReachSeatLeaseKeyValid({37, 0x12340007, 0x23450009, 0x00030021, 16}),
                "Reach body-hide lease key preserves generation, both salted handles, definition datum and raw seat");

            // The seat-bit lease is armed again, now with Halo 3's accepted
            // lease LIFETIME rather than the older value-only rule. The two rules that
            // make the difference are: the seat we already own is never
            // rewritten (so the word cannot toggle per frame - the rejected
            // shaking), and any other key is a new occupation that must be
            // restored first. The older rule's own flag stays permanently false.
            Check(kReachSeatFirstPersonPresentationEnabled && ReachSeatLeaseOwnsMutation(ReachSeatLeaseState::Active) &&
                      ReachSeatLeaseOwnsMutation(ReachSeatLeaseState::RestorePending) &&
                      !ReachSeatLeaseOwnsMutation(ReachSeatLeaseState::Empty) &&
                      !ReachSeatLeaseOwnsMutation(ReachSeatLeaseState::External) &&
                      // Same seat while Active: owned, equal, so the sampler returns
                      // early and performs no write.
                      (ReachSeatLeaseOwnsMutation(ReachSeatLeaseState::Active) &&
                          ReachSeatLeaseKeyEqual(leaseKey, leaseKey)) &&
                      // Any other seat/vehicle/generation is a different occupation.
                      !ReachSeatLeaseKeyEqual(leaseKey, otherSeat) && !ReachSeatLeaseKeyEqual(leaseKey, otherParent) &&
                      // A naturally first-person seat (the Workshop maps clear this
                      // bit in the tags) is External for its own key and is never
                      // rewritten or restored.
                      ReachSeatLeaseBlocksKey(ReachSeatLeaseState::External, leaseKey, leaseKey) &&
                      writtenFlags == (originalFlags & ~kReachSeatThirdPersonCameraBit) &&
                      (writtenFlags & kReachSeatAllowsWeaponsBit) == (originalFlags & kReachSeatAllowsWeaponsBit),
                "The Reach seat first-person lease keeps Halo 3 C20's lifetime and touches only bit 4");

            // A complete exact seat earns one re-centre regardless of headset
            // cadence or View Follow. Repeated frames carrying that same key
            // can never fire again after the successful application commits.
            bool cadenceInvariant = true;
            for (const uint32_t refreshRate : {72u, 90u, 120u, 144u})
            {
                for (const bool viewFollow : {false, true})
                {
                    (void)viewFollow; // deliberately absent from the policy
                    ReachSeatRecenterLatch latch;
                    uint32_t requests = 0;
                    for (uint32_t frame = 0; frame < refreshRate * 2; ++frame)
                    {
                        if (latch.NeedsApply(true, true, &leaseKey))
                        {
                            ++requests;
                            cadenceInvariant = cadenceInvariant && latch.Commit(leaseKey);
                        }
                    }
                    cadenceInvariant = cadenceInvariant && requests == 1;
                }
            }
            Check(cadenceInvariant,
                "Reach seat re-centre fires once for the same exact seat at 72-144 Hz with View Follow off or on");

            // Every member of the full key is an occupation boundary. Commit
            // remains explicit so a failed runtime application can retry.
            ReachSeatRecenterLatch changedKeyLatch;
            const bool exactChangesRearm =
                changedKeyLatch.NeedsApply(true, true, &leaseKey) && changedKeyLatch.Commit(leaseKey) &&
                changedKeyLatch.NeedsApply(true, true, &otherSeat) && changedKeyLatch.Commit(otherSeat) &&
                changedKeyLatch.NeedsApply(true, true, &otherParent) && changedKeyLatch.Commit(otherParent) &&
                changedKeyLatch.NeedsApply(true, true, &otherUnit) && changedKeyLatch.Commit(otherUnit) &&
                changedKeyLatch.NeedsApply(true, true, &otherDefinition) && changedKeyLatch.Commit(otherDefinition) &&
                changedKeyLatch.NeedsApply(true, true, &otherGeneration) && changedKeyLatch.Commit(otherGeneration) &&
                !changedKeyLatch.NeedsApply(true, true, &otherGeneration);
            Check(exactChangesRearm,
                "Reach seat re-centre treats seat, vehicle, unit, definition and generation changes as exact new occupations");
            ReachSeatRecenterLatch stagedCommitLatch;
            const bool rejectedStageRetries = stagedCommitLatch.NeedsApply(true, true, &leaseKey) &&
                                              stagedCommitLatch.NeedsApply(true, true, &leaseKey) &&
                                              stagedCommitLatch.Commit(leaseKey) &&
                                              !stagedCommitLatch.NeedsApply(true, true, &leaseKey);
            Check(rejectedStageRetries,
                "Reach rejected outer-camera stages remain pending until an explicit committed render consumes the exact seat");

            // No complete key means there is nothing safe to apply, but it is
            // not an exit. Preserve the applied key so a torn frame followed by
            // the same seat cannot yank the play space a second time.
            constexpr ReachSeatLeaseKey invalidKey{37, -1, 0x23450009, 0x00030021, 3};
            ReachSeatRecenterLatch missingLatch;
            const bool missingPreserves =
                missingLatch.NeedsApply(true, true, &leaseKey) && missingLatch.Commit(leaseKey) &&
                !missingLatch.NeedsApply(true, true, nullptr) && !missingLatch.NeedsApply(true, true, &invalidKey) &&
                !missingLatch.NeedsApply(true, true, &leaseKey) && missingLatch.NeedsApply(true, true, &otherSeat);
            Check(missingPreserves,
                "Reach seat re-centre preserves its exact key across missing, torn or invalid current samples");
            Check(ReachSeatOccupationObserved(leaseKey.directParent, leaseKey.seatIndex, 16) &&
                      !ReachSeatOccupationObserved(-1, leaseKey.seatIndex, 16) &&
                      !ReachSeatOccupationObserved(leaseKey.directParent, -1, 16) &&
                      !ReachSeatOccupationObserved(leaseKey.directParent, 16, 16),
                "Reach parent plus raw seat proves occupation independently of a torn seat-camera pointer");

            // A settled exit and an explicit config disable are real
            // boundaries. Both clear the latch so the next valid entry earns
            // one new request. Invalid commits must not replace good state.
            ReachSeatRecenterLatch boundaryLatch;
            const bool boundariesRearm =
                boundaryLatch.NeedsApply(true, true, &leaseKey) && boundaryLatch.Commit(leaseKey) &&
                !boundaryLatch.NeedsApply(true, false, nullptr) && boundaryLatch.NeedsApply(true, true, &leaseKey) &&
                boundaryLatch.Commit(leaseKey) && !boundaryLatch.NeedsApply(false, true, &leaseKey) &&
                boundaryLatch.NeedsApply(true, true, &leaseKey) && boundaryLatch.Commit(leaseKey) &&
                !boundaryLatch.Commit(invalidKey) && !boundaryLatch.NeedsApply(true, true, &leaseKey);
            Check(boundariesRearm,
                "Reach seat re-centre rearms only after settled exit/config disable and commits only valid exact keys");

            // Once a seat is admitted, later identity/marker/bounds proof can
            // miss without proving that the player left it. Feeding the
            // already-stable occupation into the shared debounce must hold the
            // latch indefinitely at every supported headset cadence. Only a
            // genuinely observed OnFoot run earns the exit boundary.
            bool proofMissesHoldOccupation = true;
            for (const uint32_t refreshRate : {72u, 90u, 120u, 144u})
            {
                Halo3VehicleDebounce occupation{};
                occupation.stable = Halo3VehicleState::Vehicle;
                occupation.candidate = Halo3VehicleState::Vehicle;
                ReachSeatRecenterLatch latch;
                proofMissesHoldOccupation =
                    proofMissesHoldOccupation && latch.NeedsApply(true, true, &leaseKey) && latch.Commit(leaseKey);
                for (uint32_t frame = 0; frame < refreshRate * 2; ++frame)
                {
                    proofMissesHoldOccupation =
                        proofMissesHoldOccupation &&
                        !occupation.Update(occupation.stable, kHalo3VehicleDebounceFrames) &&
                        !latch.NeedsApply(true, occupation.stable == Halo3VehicleState::Vehicle, nullptr);
                }
                proofMissesHoldOccupation = proofMissesHoldOccupation && !latch.NeedsApply(true, true, &leaseKey);
                for (uint32_t frame = 0; frame < kHalo3VehicleDebounceFrames; ++frame)
                {
                    occupation.Update(Halo3VehicleState::OnFoot, kHalo3VehicleDebounceFrames);
                }
                proofMissesHoldOccupation =
                    proofMissesHoldOccupation && occupation.stable == Halo3VehicleState::OnFoot &&
                    !latch.NeedsApply(true, false, nullptr) && latch.NeedsApply(true, true, &leaseKey);
            }
            Check(proofMissesHoldOccupation,
                "Reach camera-proof misses preserve an occupied seat at 72-144 Hz while a true OnFoot sample rearms entry");

            float entryYaw = 0.0f;
            const float forward[3]{0.0f, 1.0f, 0.0f};
            const float vertical[3]{0.0f, 0.0f, 1.0f};
            const float nonFinite[3]{std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f};
            const bool headingGuarded =
                ReachVehicleEntryYaw(forward, entryYaw) && std::fabs(entryYaw - 1.57079632679f) < 1.0e-5f &&
                !ReachVehicleEntryYaw(vertical, entryYaw) && !ReachVehicleEntryYaw(nonFinite, entryYaw) &&
                !ReachVehicleEntryYaw(nullptr, entryYaw);
            Check(headingGuarded,
                "Reach seat re-centre consumes only a finite render-matched horizontal vehicle heading");
            constexpr float packedGameYaw = -2.375f;
            constexpr float packedHeadYaw = 1.125f;
            constexpr uint64_t packedYawReferences = ReachPackYawReferencePair(packedGameYaw, packedHeadYaw);
            constexpr ReachYawReferencePair unpackedYawReferences = ReachUnpackYawReferencePair(packedYawReferences);
            Check(unpackedYawReferences.gameYaw == packedGameYaw && unpackedYawReferences.headYaw == packedHeadYaw &&
                      ReachYawReferencePairValid(unpackedYawReferences) &&
                      !ReachYawReferencePairValid(ReachUnpackYawReferencePair(kReachInvalidYawReferencePair)),
                "Reach publishes game/head yaw references as one bit-exact input snapshot");
            Check(ReachSeatLeaseRetainsHook(ReachSeatLeaseState::Installing) &&
                      ReachSeatLeaseRetainsHook(ReachSeatLeaseState::Active) &&
                      ReachSeatLeaseRetainsHook(ReachSeatLeaseState::RestorePending) &&
                      !ReachSeatLeaseRetainsHook(ReachSeatLeaseState::External) &&
                      !ReachSeatLeaseRetainsHook(ReachSeatLeaseState::Empty) &&
                      !ReachSeatLeaseRetainsHook(ReachSeatLeaseState::CleanupLocked) &&
                      ReachSeatLeaseOwnsMutation(ReachSeatLeaseState::Active) &&
                      ReachSeatLeaseOwnsMutation(ReachSeatLeaseState::RestorePending) &&
                      !ReachSeatLeaseOwnsMutation(ReachSeatLeaseState::Installing) &&
                      ReachSeatLeaseBlocksKey(ReachSeatLeaseState::External, leaseKey, leaseKey) &&
                      !ReachSeatLeaseBlocksKey(ReachSeatLeaseState::External, leaseKey, otherSeat) &&
                      ReachSeatLeaseBlocksKey(ReachSeatLeaseState::Active, leaseKey, otherSeat),
                "Reach body-hide lease retains teardown ownership only while installing/owned/pending and never retries an external exact key");
            Check(ReachSeatLeaseCanAcquireCleanupLock(ReachSeatLeaseState::Empty) &&
                      ReachSeatLeaseCanAcquireCleanupLock(ReachSeatLeaseState::External) &&
                      !ReachSeatLeaseCanAcquireCleanupLock(ReachSeatLeaseState::Installing) &&
                      !ReachSeatLeaseCanAcquireCleanupLock(ReachSeatLeaseState::Active) &&
                      !ReachSeatLeaseCanAcquireCleanupLock(ReachSeatLeaseState::RestorePending) &&
                      !ReachSeatLeaseCanAcquireCleanupLock(ReachSeatLeaseState::CleanupLocked) &&
                      ReachSeatLeaseCleanupLocked(ReachSeatLeaseState::CleanupLocked) &&
                      !ReachSeatLeaseCleanupLocked(ReachSeatLeaseState::Empty) &&
                      ReachSeatLeaseBlocksKey(ReachSeatLeaseState::CleanupLocked, leaseKey, otherSeat),
                "Reach body-hide cleanup lock races conclusively with Installing on the shared state and blocks every new key");
            Check(writtenFlags == 0x04020004 &&
                      ReachClassifySeatLeaseRestore(originalFlags, originalFlags, writtenFlags) ==
                          ReachSeatLeaseRestoreDisposition::AlreadyOriginal &&
                      ReachClassifySeatLeaseRestore(writtenFlags, originalFlags, writtenFlags) ==
                          ReachSeatLeaseRestoreDisposition::RestoreOriginal &&
                      ReachClassifySeatLeaseRestore(originalFlags ^ 0x40u, originalFlags, writtenFlags) ==
                          ReachSeatLeaseRestoreDisposition::ExternalWrite &&
                      ReachClassifySeatLeaseRestore(writtenFlags, writtenFlags, writtenFlags) ==
                          ReachSeatLeaseRestoreDisposition::AlreadyOriginal,
                "Reach body-hide restoration writes only exact owned flags and yields to engine/tag changes");
        }
        // Reach's actual body-visibility policy is unit-camera WORD bit 2.
        // It is scoped to one admitted outer render; no seat camera-mode bit is
        // changed, so the native first-person camera and both view-follow
        // settings remain independent.
        {
            constexpr uint16_t original = 0xA521;
            constexpr uint16_t written = ReachUnitCameraHidePlayerFlags(original);
            constexpr uint16_t alreadyHidden = 0xA525;
            constexpr uint16_t changedDuringRender = static_cast<uint16_t>(written ^ 0x0080u);
            Check(kReachSeatCameraOffset == 0x70 && kReachUnitCameraFlagsOffset == 0x00 &&
                      kReachUnitCameraHidePlayerBit == 0x0004 && written == 0xA525 &&
                      static_cast<uint16_t>(written ^ original) == kReachUnitCameraHidePlayerBit &&
                      ReachUnitCameraHidePlayerFlags(alreadyHidden) == alreadyHidden &&
                      ReachUnitCameraRestorePlayerFlags(written, original) == original &&
                      ReachUnitCameraRestorePlayerFlags(changedDuringRender, original) ==
                          static_cast<uint16_t>(original ^ 0x0080u) &&
                      ReachClassifySeatLeaseRestore(written, original, written) ==
                          ReachSeatLeaseRestoreDisposition::RestoreOriginal &&
                      ReachClassifySeatLeaseRestore(static_cast<uint16_t>(written ^ 0x0080u), original, written) ==
                          ReachSeatLeaseRestoreDisposition::ExternalWrite,
                "Reach body hide sets only unit-camera bit 2 and restores only that owned bit while preserving unrelated changes");
        }
        // Entering a Reach seat selects the same native aim feedback
        // under BOTH view-follow settings.
        {
            float nativeAim[3] = {0.3f, 0.4f, 0.0f};
            float normalizedAim[3] = {};
            const float invalidAim[3] = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f};
            Check(
                ReachNormalizeUnitAimingVector(nativeAim, normalizedAim) &&
                    std::fabs(normalizedAim[0] - 0.6f) < 1.0e-6f && std::fabs(normalizedAim[1] - 0.8f) < 1.0e-6f &&
                    normalizedAim[2] == 0.0f && !ReachNormalizeUnitAimingVector(invalidAim, normalizedAim) &&
                    ReachSelectAimFeedbackSource(true, true, false) == ReachAimFeedbackSource::SeatedUnitAim &&
                    ReachSelectAimFeedbackSource(true, true, true) == ReachAimFeedbackSource::SeatedUnitAim &&
                    ReachSelectAimFeedbackSource(true, false, false) == ReachAimFeedbackSource::SeatedCompactFallback &&
                    ReachSelectAimFeedbackSource(false, true, true) == ReachAimFeedbackSource::OnFootCompact &&
                    ReachAimFeedbackCanDriveReticle(ReachAimFeedbackSource::SeatedUnitAim) &&
                    !ReachAimFeedbackCanDriveReticle(ReachAimFeedbackSource::SeatedCompactFallback) &&
                    !ReachAimFeedbackCanDriveReticle(ReachAimFeedbackSource::OnFootCompact) &&
                    !ReachVehicleSeatAuthorsSteering(ReachVehicleId::Warthog, 0, false) &&
                    ReachVehicleSeatAuthorsSteering(ReachVehicleId::Warthog, 0, true),
                "Reach unit-aim feedback stays active under both view-follow modes");
        }
        // Completed-pair reticle truth is expressed in the final Reach camera
        // basis. Identity here means Reach world +X is camera forward, world
        // -Y is camera right and world +Z is camera up.
        {
            const float forward[3] = {1.0f, 0.0f, 0.0f};
            const float up[3] = {0.0f, 0.0f, 1.0f};
            const float right[3] = {0.0f, -1.0f, 0.0f};
            float localForward[3]{};
            float localRight[3]{};
            float localUp[3]{};
            const float scaledForward[3] = {2.0f, 0.0f, 0.0f};
            float localScaledForward[3]{};
            Check(ReachWorldAimToCameraLocal(forward, forward, up, localForward) &&
                      ReachWorldAimToCameraLocal(right, forward, up, localRight) &&
                      ReachWorldAimToCameraLocal(up, forward, up, localUp) &&
                      ReachWorldAimToCameraLocal(scaledForward, forward, up, localScaledForward) &&
                      std::fabs(localForward[0]) < 1.0e-6f && std::fabs(localForward[1]) < 1.0e-6f &&
                      std::fabs(localForward[2] + 1.0f) < 1.0e-6f && std::fabs(localRight[0] - 1.0f) < 1.0e-6f &&
                      std::fabs(localRight[1]) < 1.0e-6f && std::fabs(localRight[2]) < 1.0e-6f &&
                      std::fabs(localUp[0]) < 1.0e-6f && std::fabs(localUp[1] - 1.0f) < 1.0e-6f &&
                      std::fabs(localUp[2]) < 1.0e-6f && std::fabs(localScaledForward[2] + 1.0f) < 1.0e-6f,
                "Reach completed-pair aim maps forward/right/up to OpenXR -Z/+X/+Y and normalizes");

            // Non-trivial yaw, pitch and roll: world -> local -> world must
            // round-trip through the exact F/U/R basis and sign convention.
            constexpr float yaw = 0.71f;
            constexpr float pitch = -0.29f;
            constexpr float roll = 0.23f;
            const float cy = std::cos(yaw), sy = std::sin(yaw);
            const float cp = std::cos(pitch), sp = std::sin(pitch);
            const float cr = std::cos(roll), sr = std::sin(roll);
            const float rolledForward[3] = {cp * cy, cp * sy, sp};
            const float rolledUp[3] = {(-sp * cy) * cr + sy * sr, (-sp * sy) * cr - cy * sr, cp * cr};
            const float rolledRight[3] = {rolledForward[1] * rolledUp[2] - rolledForward[2] * rolledUp[1],
                rolledForward[2] * rolledUp[0] - rolledForward[0] * rolledUp[2],
                rolledForward[0] * rolledUp[1] - rolledForward[1] * rolledUp[0]};
            const float worldAim[3] = {0.2f, -0.7f, 0.6f};
            float normalizedWorldAim[3]{};
            float rolledLocal[3]{};
            float roundTrip[3]{};
            const bool converted = ReachNormalizeUnitAimingVector(worldAim, normalizedWorldAim) &&
                                   ReachWorldAimToCameraLocal(worldAim, rolledForward, rolledUp, rolledLocal);
            if (converted)
            {
                for (int component = 0; component < 3; ++component)
                {
                    roundTrip[component] = rolledRight[component] * rolledLocal[0] +
                                           rolledUp[component] * rolledLocal[1] -
                                           rolledForward[component] * rolledLocal[2];
                }
            }
            Check(converted && std::fabs(roundTrip[0] - normalizedWorldAim[0]) < 1.0e-5f &&
                      std::fabs(roundTrip[1] - normalizedWorldAim[1]) < 1.0e-5f &&
                      std::fabs(roundTrip[2] - normalizedWorldAim[2]) < 1.0e-5f,
                "Reach camera-local aim round-trips through a rolled yaw/pitch basis");

            const float zero[3] = {};
            const float nonUnitForward[3] = {1.1f, 0.0f, 0.0f};
            const float nonOrthogonalUp[3] = {1.0f, 0.0f, 0.0f};
            const float invalidAim[3] = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 1.0f};
            const float invalidUp[3] = {0.0f, std::numeric_limits<float>::infinity(), 1.0f};
            float rejected[3]{};
            Check(!ReachWorldAimToCameraLocal(zero, forward, up, rejected) &&
                      !ReachWorldAimToCameraLocal(invalidAim, forward, up, rejected) &&
                      !ReachWorldAimToCameraLocal(forward, nonUnitForward, up, rejected) &&
                      !ReachWorldAimToCameraLocal(forward, forward, nonOrthogonalUp, rejected) &&
                      !ReachWorldAimToCameraLocal(forward, forward, invalidUp, rejected),
                "Reach camera-local aim rejects zero/non-finite aim and invalid camera bases");
        }

        // Admission is exact to one completed prepared serial and one complete
        // salted seat occupation. View-follow never appears in this decision;
        // only native SeatedUnitAim may take reticle ownership.
        {
            constexpr uint32_t generation = 37;
            constexpr uint64_t serial = 0x1020304050607080ull;
            constexpr ReachSeatLeaseKey occupation{generation, 0x12340007, 0x23450009, 0x00030021, 3};
            ReachReticleAimSample sample{};
            sample.generation = generation;
            sample.preparedSerial = serial;
            sample.source = ReachAimFeedbackSource::SeatedUnitAim;
            sample.occupation = occupation;
            sample.cameraLocalDirection[0] = 0.6f;
            sample.cameraLocalDirection[1] = 0.0f;
            sample.cameraLocalDirection[2] = -0.8f;
            Check(ReachReticleAimSampleAdmitted(sample, generation, serial) &&
                      !ReachReticleAimSampleAdmitted(sample, generation + 1, serial) &&
                      !ReachReticleAimSampleAdmitted(sample, generation, serial + 1),
                "Reach reticle sample admission requires current generation and exact prepared serial");

            ReachReticleAimSample fallback = sample;
            fallback.source = ReachAimFeedbackSource::SeatedCompactFallback;
            ReachReticleAimSample onFoot = sample;
            onFoot.source = ReachAimFeedbackSource::OnFootCompact;
            ReachReticleAimSample invalidKey = sample;
            invalidKey.occupation.directParent = -1;
            ReachReticleAimSample wrongKeyGeneration = sample;
            wrongKeyGeneration.occupation.generation = generation + 1;
            ReachReticleAimSample invalidVector = sample;
            invalidVector.cameraLocalDirection[0] = std::numeric_limits<float>::quiet_NaN();
            ReachReticleAimSample zeroVector = sample;
            zeroVector.cameraLocalDirection[0] = 0.0f;
            zeroVector.cameraLocalDirection[1] = 0.0f;
            zeroVector.cameraLocalDirection[2] = 0.0f;
            Check(!ReachReticleAimSampleAdmitted(fallback, generation, serial) &&
                      !ReachReticleAimSampleAdmitted(onFoot, generation, serial) &&
                      !ReachReticleAimSampleAdmitted(invalidKey, generation, serial) &&
                      !ReachReticleAimSampleAdmitted(wrongKeyGeneration, generation, serial) &&
                      !ReachReticleAimSampleAdmitted(invalidVector, generation, serial) &&
                      !ReachReticleAimSampleAdmitted(zeroVector, generation, serial),
                "Reach reticle sample rejects fallback sources, incomplete full keys and invalid vectors");

            ReachReticleAimSample next = sample;
            next.occupation.seatIndex = 4;
            next.cameraLocalDirection[0] = -0.8f;
            next.cameraLocalDirection[2] = -0.6f;
            const ReachReticleAimSample afterFailed = ReachReticleAimSampleAfterAttempt(sample, next, false);
            const ReachReticleAimSample afterCompleted = ReachReticleAimSampleAfterAttempt(sample, next, true);
            ReachReticleAimSample revocation = next;
            revocation.source = ReachAimFeedbackSource::SeatedCompactFallback;
            const ReachReticleAimSample afterCompletedRevocation =
                ReachReticleAimSampleAfterAttempt(sample, revocation, true);
            Check(afterFailed.occupation.seatIndex == 3 && afterFailed.cameraLocalDirection[0] == 0.6f &&
                      afterCompleted.preparedSerial == serial && afterCompleted.occupation.seatIndex == 4 &&
                      afterCompleted.cameraLocalDirection[0] == -0.8f &&
                      ReachReticleAimSampleAdmitted(afterCompleted, generation, serial) &&
                      afterCompletedRevocation.occupation.seatIndex == 4 &&
                      !ReachReticleAimSampleAdmitted(afterCompletedRevocation, generation, serial),
                "Reach reticle publication preserves a prior pair across failure and replaces it on every later completed same-serial pair");
        }

        Check(
            ReachSeatAimCanDriveReticle(ReachAimFeedbackSource::SeatedUnitAim, kReachSeatAllowsWeaponsBit) &&
                !ReachSeatAimCanDriveReticle(ReachAimFeedbackSource::SeatedUnitAim, 0) &&
                !ReachSeatAimCanDriveReticle(ReachAimFeedbackSource::SeatedCompactFallback, kReachSeatAllowsWeaponsBit),
            "Reach unit aim drives only personal-weapon seat reticles, never vehicle barrels");

        Check(
            ReachPassengerNeedsFirstPersonRenderAdmission(true, true, true, true, kReachSeatAllowsWeaponsBit, 0) &&
                !ReachPassengerNeedsFirstPersonRenderAdmission(true, true, true, true, 0, 0) &&
                !ReachPassengerNeedsFirstPersonRenderAdmission(
                    true, true, true, true, kReachSeatAllowsWeaponsBit, 0xFFFFu) &&
                !ReachPassengerNeedsFirstPersonRenderAdmission(
                    true, false, true, true, kReachSeatAllowsWeaponsBit, 0) &&
                !ReachPassengerNeedsFirstPersonRenderAdmission(true, true, true, false, kReachSeatAllowsWeaponsBit, 0),
            "Reach passenger first-person render admission is exact to an active VR-owned allows-weapons seat and never overrides stock admission");

        // The visible controller reticle stays in charge; a native selected-barrel
        // direction redirect is admitted only for one fresh exact
        // local occupation/target pair. Fifty milliseconds covers three
        // scheduled intervals at 72 Hz without allowing long-stale aim at
        // 144 Hz.
        {
            constexpr uint32_t generation = 43;
            constexpr uint64_t nowMs = 7000;
            constexpr ReachSeatLeaseKey occupation{generation, 0x12340007, 0x23450009, 0x00030021, 3};
            ReachShotDirectionSample sample{};
            sample.currentGeneration = generation;
            sample.nowMs = nowMs;
            sample.occupationSampleMs = nowMs - kReachShotDirectionFreshMs;
            sample.targetSampleMs = nowMs - kReachShotDirectionFreshMs;
            sample.active = true;
            sample.firingUnitIndex = occupation.unitHandle;
            sample.occupation = occupation;
            sample.targetKey = occupation;
            sample.origin[0] = 1.0f;
            sample.origin[1] = 2.0f;
            sample.origin[2] = 3.0f;
            sample.target[0] = 401.0f;
            sample.target[1] = 2.0f;
            sample.target[2] = 3.0f;
            Check(ReachShotDirectionSampleAdmitted(sample),
                "Reach vehicle shot direction admits the exact fresh full-key boundary while retaining the controller reticle");

            ReachShotDirectionSample staleOccupation = sample;
            staleOccupation.occupationSampleMs = nowMs - kReachShotDirectionFreshMs - 1;
            ReachShotDirectionSample staleTarget = sample;
            staleTarget.targetSampleMs = nowMs - kReachShotDirectionFreshMs - 1;
            ReachShotDirectionSample futureOccupation = sample;
            futureOccupation.occupationSampleMs = nowMs + 1;
            ReachShotDirectionSample futureTarget = sample;
            futureTarget.targetSampleMs = nowMs + 1;
            ReachShotDirectionSample wrongGeneration = sample;
            wrongGeneration.currentGeneration = generation + 1;
            ReachShotDirectionSample wrongSalt = sample;
            wrongSalt.targetKey.unitHandle ^= 0x00010000;
            ReachShotDirectionSample wrongUnit = sample;
            wrongUnit.firingUnitIndex ^= 0x00010000;
            ReachShotDirectionSample inactive = sample;
            inactive.active = false;
            ReachShotDirectionSample invalidOrigin = sample;
            invalidOrigin.origin[1] = std::numeric_limits<float>::quiet_NaN();
            ReachShotDirectionSample zeroLength = sample;
            memcpy(zeroLength.target, zeroLength.origin, sizeof(zeroLength.target));
            ReachShotDirectionSample excessiveDistance = sample;
            excessiveDistance.target[0] = excessiveDistance.origin[0] + 2049.0f;
            Check(!ReachShotDirectionSampleAdmitted(staleOccupation) &&
                      !ReachShotDirectionSampleAdmitted(staleTarget) &&
                      !ReachShotDirectionSampleAdmitted(futureOccupation) &&
                      !ReachShotDirectionSampleAdmitted(futureTarget) &&
                      !ReachShotDirectionSampleAdmitted(wrongGeneration) &&
                      !ReachShotDirectionSampleAdmitted(wrongSalt) && !ReachShotDirectionSampleAdmitted(wrongUnit) &&
                      !ReachShotDirectionSampleAdmitted(inactive) && !ReachShotDirectionSampleAdmitted(invalidOrigin) &&
                      !ReachShotDirectionSampleAdmitted(zeroLength) &&
                      !ReachShotDirectionSampleAdmitted(excessiveDistance),
                "Reach vehicle shot direction rejects stale, future, mismatched and invalid samples");
        }

        // Personal-weapon origin substitution requires one fresh, matching
        // full-salt seat/eye pair. The 100 ms boundary is admitted exactly;
        // every stale, mismatched or vehicle-barrel case remains stock.
        {
            constexpr uint32_t generation = 41;
            constexpr uint64_t nowMs = 5000;
            constexpr ReachSeatLeaseKey occupation{generation, 0x12340007, 0x23450009, 0x00030021, 3};
            ReachShotOriginSample sample{};
            sample.currentGeneration = generation;
            sample.nowMs = nowMs;
            sample.occupationSampleMs = nowMs - kReachShotOriginFreshMs;
            sample.renderedEyeSampleMs = nowMs - kReachShotOriginFreshMs;
            sample.renderedEyePreparedSerial = 77;
            sample.active = true;
            sample.allowsWeapons = true;
            sample.firingUnitIndex = 7;
            sample.occupation = occupation;
            sample.renderedEye = occupation;
            sample.renderedEyePosition[0] = 1.0f;
            sample.renderedEyePosition[1] = 2.0f;
            sample.renderedEyePosition[2] = 3.0f;
            Check(ReachShotOriginSampleAdmitted(sample),
                "Reach personal-shot origin admits the exact fresh full-key boundary");
            ReachShotOriginSample saltedFiringArgument = sample;
            saltedFiringArgument.firingUnitIndex = 0x7ABC0007;
            Check(ReachShotOriginSampleAdmitted(saltedFiringArgument),
                "Reach personal-shot origin matches the engine firing argument by object index");

            ReachShotOriginSample staleOccupation = sample;
            staleOccupation.occupationSampleMs = nowMs - kReachShotOriginFreshMs - 1;
            ReachShotOriginSample staleEye = sample;
            staleEye.renderedEyeSampleMs = nowMs - kReachShotOriginFreshMs - 1;
            ReachShotOriginSample futureEye = sample;
            futureEye.renderedEyeSampleMs = nowMs + 1;
            ReachShotOriginSample wrongGeneration = sample;
            wrongGeneration.currentGeneration = generation + 1;
            ReachShotOriginSample wrongSalt = sample;
            wrongSalt.renderedEye.unitHandle ^= 0x00010000;
            ReachShotOriginSample wrongParent = sample;
            wrongParent.renderedEye.directParent ^= 0x00010000;
            ReachShotOriginSample wrongDefinition = sample;
            wrongDefinition.renderedEye.definitionDatum ^= 1;
            ReachShotOriginSample wrongSeat = sample;
            wrongSeat.renderedEye.seatIndex = 4;
            ReachShotOriginSample barrelSeat = sample;
            barrelSeat.allowsWeapons = false;
            ReachShotOriginSample wrongUnit = sample;
            wrongUnit.firingUnitIndex = 8;
            ReachShotOriginSample noCompletedPair = sample;
            noCompletedPair.renderedEyePreparedSerial = 0;
            ReachShotOriginSample invalidEye = sample;
            invalidEye.renderedEyePosition[1] = std::numeric_limits<float>::quiet_NaN();
            Check(!ReachShotOriginSampleAdmitted(staleOccupation) && !ReachShotOriginSampleAdmitted(staleEye) &&
                      !ReachShotOriginSampleAdmitted(futureEye) && !ReachShotOriginSampleAdmitted(wrongGeneration) &&
                      !ReachShotOriginSampleAdmitted(wrongSalt) && !ReachShotOriginSampleAdmitted(wrongParent) &&
                      !ReachShotOriginSampleAdmitted(wrongDefinition) && !ReachShotOriginSampleAdmitted(wrongSeat) &&
                      !ReachShotOriginSampleAdmitted(barrelSeat) && !ReachShotOriginSampleAdmitted(wrongUnit) &&
                      !ReachShotOriginSampleAdmitted(noCompletedPair) && !ReachShotOriginSampleAdmitted(invalidEye),
                "Reach personal-shot origin rejects stale, mismatched, barrel and invalid samples");
        }

        // The same validated stereo centre supplies both native-aim rotation
        // and ray origin. q/-q sign equivalence and symmetric eye cant must not
        // create a false centre rotation.
        {
            const float identity[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            const float negativeScaledIdentity[4] = {0.0f, 0.0f, 0.0f, -2.0f};
            const float leftPosition[3] = {-0.032f, 1.5f, -2.0f};
            const float rightPosition[3] = {0.032f, 1.5f, -2.0f};
            ReachStereoCenterPose signAligned{};
            Check(ReachBuildStereoCenterPose(
                      identity, leftPosition, negativeScaledIdentity, rightPosition, signAligned) &&
                      std::fabs(signAligned.orientation[0]) < 1.0e-6f &&
                      std::fabs(signAligned.orientation[1]) < 1.0e-6f &&
                      std::fabs(signAligned.orientation[2]) < 1.0e-6f &&
                      std::fabs(signAligned.orientation[3] - 1.0f) < 1.0e-6f &&
                      std::fabs(signAligned.position[0]) < 1.0e-6f &&
                      std::fabs(signAligned.position[1] - 1.5f) < 1.0e-6f &&
                      std::fabs(signAligned.position[2] + 2.0f) < 1.0e-6f,
                "Reach stereo centre aligns q/-q and uses the exact eye-position midpoint");

            constexpr float halfCant = 0.10f;
            const float cantSin = std::sin(halfCant);
            const float cantCos = std::cos(halfCant);
            const float leftCant[4] = {0.0f, cantSin, 0.0f, cantCos};
            const float rightCant[4] = {0.0f, -cantSin, 0.0f, cantCos};
            ReachStereoCenterPose symmetricCant{};
            Check(ReachBuildStereoCenterPose(leftCant, leftPosition, rightCant, rightPosition, symmetricCant) &&
                      std::fabs(symmetricCant.orientation[0]) < 1.0e-6f &&
                      std::fabs(symmetricCant.orientation[1]) < 1.0e-6f &&
                      std::fabs(symmetricCant.orientation[2]) < 1.0e-6f &&
                      std::fabs(symmetricCant.orientation[3] - 1.0f) < 1.0e-6f,
                "Reach stereo centre averages symmetric canted-eye orientations without bias");

            const float zeroQuaternion[4] = {};
            const float quarterTurnX[4] = {1.0f, 0.0f, 0.0f, 0.0f};
            const float invalidQuaternion[4] = {0.0f, 0.0f, std::numeric_limits<float>::quiet_NaN(), 1.0f};
            const float invalidPosition[3] = {0.0f, std::numeric_limits<float>::infinity(), 0.0f};
            ReachStereoCenterPose rejected{};
            Check(!ReachBuildStereoCenterPose(zeroQuaternion, leftPosition, identity, rightPosition, rejected) &&
                      !ReachBuildStereoCenterPose(identity, leftPosition, quarterTurnX, rightPosition, rejected) &&
                      !ReachBuildStereoCenterPose(invalidQuaternion, leftPosition, identity, rightPosition, rejected) &&
                      !ReachBuildStereoCenterPose(identity, invalidPosition, identity, rightPosition, rejected),
                "Reach stereo centre rejects zero/non-finite and ambiguous eye poses");
        }
        ReachSeatCameraBasis reachBasis{};
        reachBasis.forward[0] = 1.0f;
        reachBasis.left[1] = 1.0f;
        reachBasis.up[2] = 1.0f;
        const float reachBase[3] = {10.0f, 20.0f, 30.0f};
        float reachPoint[3]{};
        Check(ReachComposeSeatCameraPoint(reachBase, reachBasis, 2.0f, 3.0f, 4.0f, reachPoint) &&
                  reachPoint[0] == 12.0f && reachPoint[1] == 17.0f && reachPoint[2] == 34.0f,
            "Reach camera trims use Blender +forward/+right/+up axes");
        reachBasis.scale = 2.0f;
        Check(ReachComposeSeatCameraPoint(reachBase, reachBasis, 2.0f, 3.0f, 4.0f, reachPoint) &&
                  reachPoint[0] == 14.0f && reachPoint[1] == 14.0f && reachPoint[2] == 38.0f,
            "Reach camera trims honor authored marker uniform scale");
        constexpr uint32_t reachTrimGeneration = 0x11223344;
        constexpr uint64_t reachTrimSnapshot = ReachVehicleTrimSnapshot(reachTrimGeneration, ReachVehicleId::Falcon, 4);
        Check(
            ReachVehicleTrimSnapshotSlot(reachTrimSnapshot, reachTrimGeneration, kReachVehicleSeatSlots) ==
                    ConfigReachSeatTrimSlot(static_cast<int>(ReachVehicleId::Falcon), 4) &&
                ReachVehicleTrimSnapshotSlot(reachTrimSnapshot, reachTrimGeneration + 1, kReachVehicleSeatSlots) == -1,
            "Reach menu trim publication is generation-bound and slot-compatible");
        // An unmatched vehicle publishes the generic sentinel: the snapshot
        // stays generation-live (so FpActive, recenter and body hide keep
        // working) and decodes to the dedicated unmatched trim
        // row. It must never decode to -1, which meant the shared universal
        // trim and let a seated F1 edit move every other seat in every title.
        constexpr uint64_t reachGenericSnapshot =
            ReachVehicleTrimSnapshot(reachTrimGeneration, ReachVehicleId::Unknown, 2);
        Check(
            reachGenericSnapshot != 0 && static_cast<uint32_t>(reachGenericSnapshot >> 32) == reachTrimGeneration &&
                ((reachGenericSnapshot >> 8) & 0xFFu) == kReachVehicleGenericIdentityCode &&
                ReachVehicleTrimSnapshotSlot(reachGenericSnapshot, reachTrimGeneration, kReachVehicleSeatSlots) ==
                    ConfigReachSeatTrimSlot(kReachUnmatchedVehicleTrimId, 2) &&
                ReachVehicleTrimSnapshotSlot(reachGenericSnapshot, reachTrimGeneration, kReachVehicleSeatSlots) >= 0 &&
                ReachVehicleTrimSnapshot(0, ReachVehicleId::Unknown, 2) == 0 &&
                ReachVehicleTrimSnapshot(reachTrimGeneration, ReachVehicleId::Unknown, 16) == 0,
            "An unmatched Reach seat stays live under the generic sentinel and keys its own trim row");

        constexpr float kReachTestIpdMeters = 0.064f;
        Check(std::isfinite(kReachWorldUnitsPerMeter) && kReachWorldUnitsPerMeter > 0.0f &&
                  std::fabs(kReachWorldUnitsPerMeter * 3.048f - 1.0f) < 1.0e-6f &&
                  std::fabs(kReachTestIpdMeters * kReachWorldUnitsPerMeter - 0.020997375f) < 1.0e-7f,
            "Reach head translation and runtime IPD use the exact ten-foot world-unit conversion");

        Check(kReachSsaoCallRva == 0x0026E81D && kReachRainGateAfterSsaoRva == 0x0026E822 &&
                  kReachSsaoCallRva + 5 == kReachRainGateAfterSsaoRva &&
                  kReachLightmapShadowsRenderCallRva < kReachSsaoCallRva && kReachSsaoRva == 0x002A13A0 &&
                  kReachSsaoEndRva == 0x002A1907 && kReachSsaoBodySize == 0x567 &&
                  kReachSsaoShadowMaskCallRvas[0] == 0x002A169A && kReachSsaoShadowMaskCallRvas[1] == 0x002A1755 &&
                  kReachSsaoShadowMaskCallRvas[0] >= kReachSsaoRva &&
                  kReachSsaoShadowMaskCallRvas[0] < kReachSsaoShadowMaskCallRvas[1] &&
                  kReachSsaoShadowMaskCallRvas[1] < kReachSsaoEndRva && kReachShadowMaskAcquireRva == 0x00252F08 &&
                  kReachShadowMaskSurfaceIndex == 2 &&
                  std::string_view(kReachSsaoBodySha256) ==
                      "760D2BEC3AA13ABFA0AB2002E2873C9C8A9F1FEA9EE63238585C2A6C92943EE7",
            "Reach SSAO candidate pins the exact retail caller, callee, range, and body identity");

        constexpr uintptr_t motionBlurTestBase = 0x10000000u;
        Check(
            ReachMotionBlurSlotsMatchPinnedImage(motionBlurTestBase, kReachRetailImageSize,
                motionBlurTestBase + kReachMotionBlurScaleValueRva, motionBlurTestBase + kReachMotionBlurMaxValueRva) &&
                !ReachMotionBlurSlotsMatchPinnedImage(motionBlurTestBase, kReachRetailImageSize,
                    motionBlurTestBase + kReachMotionBlurMaxValueRva,
                    motionBlurTestBase + kReachMotionBlurScaleValueRva) &&
                !ReachMotionBlurSlotsMatchPinnedImage(motionBlurTestBase, kReachRetailImageSize - 1,
                    motionBlurTestBase + kReachMotionBlurScaleValueRva,
                    motionBlurTestBase + kReachMotionBlurMaxValueRva) &&
                kReachMotionBlurMaxOverScaleDivideRva == 0x00287561 &&
                kReachMotionBlurScaledMaxDivideRva == 0x002875AD &&
                ReachMotionBlurSuppressionValuesValid(0.35f, 0.08f) &&
                ReachMotionBlurSuppressionValuesValid(0.35f, 0.0f) &&
                !ReachMotionBlurSuppressionValuesValid(0.0f, 0.0f) &&
                !ReachMotionBlurSuppressionValuesValid(1.0e-6f, 0.08f) &&
                !ReachMotionBlurSuppressionValuesValid(0.35f, -0.01f) &&
                !ReachMotionBlurSuppressionValuesValid(0.35f, std::numeric_limits<float>::infinity()) &&
                !ReachMotionBlurSuppressionValuesValid(std::numeric_limits<float>::quiet_NaN(), 0.08f),
            "Reach blur suppression pins the exact slots/divisions and keeps a positive scale with zero max");

        bool patchyFogPolicyExact = true;
        for (unsigned original = 0; original <= 0xFFu; ++original)
        {
            const uint8_t originalByte = static_cast<uint8_t>(original);
            const uint8_t suppressed = ReachPatchyFogSuppressedFlags(originalByte);
            patchyFogPolicyExact = patchyFogPolicyExact && (suppressed & kReachPatchyFogSkipMask) != 0 &&
                                   (suppressed & static_cast<uint8_t>(~kReachPatchyFogSkipMask)) ==
                                       (originalByte & static_cast<uint8_t>(~kReachPatchyFogSkipMask));
            for (unsigned current = 0; current <= 0xFFu; ++current)
            {
                const uint8_t currentByte = static_cast<uint8_t>(current);
                const uint8_t restored = ReachPatchyFogRestoredFlags(currentByte, originalByte);
                patchyFogPolicyExact =
                    patchyFogPolicyExact &&
                    (restored & kReachPatchyFogSkipMask) == (originalByte & kReachPatchyFogSkipMask) &&
                    (restored & static_cast<uint8_t>(~kReachPatchyFogSkipMask)) ==
                        (currentByte & static_cast<uint8_t>(~kReachPatchyFogSkipMask));
            }
        }
        Check(patchyFogPolicyExact && kReachPatchyFogGateTestRva == 0x0026CC59 &&
                  kReachPatchyFogSkipJumpRva == 0x0026CC60 && kReachPatchyFogCallRva == 0x0026CC65 &&
                  kReachPatchyFogTargetRva == 0x0026EFEC && kReachPatchyFogFlagsRva == 0x00CA0240 &&
                  kReachPatchyFogSkipMask == 0x08,
            "Reach VR patchy-fog policy sets and restores only the exact proven skip bit");

        // Atmospheric fog is the OPPOSITE polarity to patchy fog above: the bit
        // means "enabled", so suppression CLEARS it and restore SETS it. Both
        // must leave every other bit in the shared render flags byte alone.
        bool atmosphereFogPolicyExact = true;
        for (unsigned original = 0; original <= 0xFFu; ++original)
        {
            const uint8_t originalByte = static_cast<uint8_t>(original);
            const uint8_t suppressed = ReachAtmosphereFogSuppressedFlags(originalByte);
            const uint8_t restored = ReachAtmosphereFogRestoredFlags(originalByte);
            constexpr uint8_t kOther = static_cast<uint8_t>(~kReachAtmosphereFogEnableMask);
            atmosphereFogPolicyExact = atmosphereFogPolicyExact && (suppressed & kReachAtmosphereFogEnableMask) == 0 &&
                                       (suppressed & kOther) == (originalByte & kOther) &&
                                       (restored & kReachAtmosphereFogEnableMask) == kReachAtmosphereFogEnableMask &&
                                       (restored & kOther) == (originalByte & kOther) &&
                                       // Suppress-then-restore returns the byte whenever the bit was
                                       // set to begin with, which is the only case that suppresses.
                                       (!(originalByte & kReachAtmosphereFogEnableMask) ||
                                           ReachAtmosphereFogRestoredFlags(suppressed) == originalByte);
        }
        Check(atmosphereFogPolicyExact && kReachAtmosphereFogHelperRva == 0x0026D5B4 &&
                  kReachAtmosphereFogHelperCallRva == 0x0026CC54 && kReachAtmosphereFogTlsIndexLoadRva == 0x0026D5DF &&
                  kReachAtmosphereFogTlsIndexRva == 0x00C17B18 && kReachAtmosphereFogSlotLoadRva == 0x0026D5E5 &&
                  kReachAtmosphereFogGateTestRva == 0x0026D5F3 && kReachAtmosphereFogSkipJumpRva == 0x0026D5F6 &&
                  kReachAtmosphereFogFlagsSlotOffset == 0x168 && kReachAtmosphereFogEnableMask == 0x04 &&
                  kReachRenderTlsIndexLimit == 256 &&
                  // The two fog systems are separate structures. Sharing a mask
                  // between them would silently make one control the other.
                  kReachAtmosphereFogEnableMask != kReachPatchyFogSkipMask,
            "Reach atmospheric-fog policy clears and restores only the exact proven enable bit");

        Check(kReachDebugVarTypeBoolean == 5 && kReachRenderRainValueRva == 0x00B4444C &&
                  kReachRenderRainGateRva == 0x0026CC92 && kReachRainParticleRenderRva == 0x00288D60 &&
                  ReachRenderRainSlotMatchesPinnedImage(
                      0x180000000u, kReachRetailImageSize, 0x180000000u + kReachRenderRainValueRva) &&
                  // A slot one byte away is the NEIGHBOURING boolean, not rain.
                  !ReachRenderRainSlotMatchesPinnedImage(
                      0x180000000u, kReachRetailImageSize, 0x180000000u + kReachRenderRainValueRva + 1) &&
                  !ReachRenderRainSlotMatchesPinnedImage(
                      0x180000000u, kReachRetailImageSize - 1, 0x180000000u + kReachRenderRainValueRva),
            "Reach render_rain is a boolean debug var cross-checked against the pinned image");

        Check(kReachSkyParallaxSignatureRva == 0x0024B5C4 && kReachSkyParallaxQuantizeRva == 0x0024B5DB &&
                  kReachModelFlagsOffset == 0x015C && kReachModelSkyParallaxOffset == 0x01B4 &&
                  kReachObjectSkyParallaxByteOffset == 0x000B && kReachModelAttachToCameraMask == 0x40 &&
                  kReachSkyParallaxQuantizeOriginal == std::array<uint8_t, 4>{0xF3, 0x0F, 0x2C, 0xC0} &&
                  kReachSkyParallaxQuantizeNeutral == std::array<uint8_t, 4>{0x31, 0xC0, 0x90, 0x90},
            "Reach camera-attached sky neutralization pins the generic model flag, parallax field, object property, and exact reversible quantizer");

        Check(kReachFpWeaponIkDecisionPreludeRva == 0x002B506E && kReachFpWeaponIkDisableCompareRva == 0x002B507F &&
                  kReachFpWeaponIkDisableBranchRva == 0x002B5085 && kReachFpWeaponIkDisabledEpilogueRva == 0x002B52D1 &&
                  kReachFpWeaponIkDisableNameRva == 0x009F2AD8 && kReachFpWeaponIkDisableEntryRva == 0x00B3AEB8 &&
                  kReachFpWeaponIkDisableValueRva == 0x04E38B61 && kReachDebugBooleanType == 5,
            "Reach native weapon-IK bypass pins the exact named control and stock no-IK edge");

        std::array<uint8_t, kReachMainRenderViewAob.size()> exactMask{};
        exactMask.fill(0xFF);
        std::array<uint8_t, kReachMainRenderViewAob.size() * 2 + 1> repeatedMain{};
        std::memcpy(repeatedMain.data(), kReachMainRenderViewAob.data(), kReachMainRenderViewAob.size());
        std::memcpy(repeatedMain.data() + kReachMainRenderViewAob.size() + 1, kReachMainRenderViewAob.data(),
            kReachMainRenderViewAob.size());
        Check(CountReachMaskedPattern(repeatedMain.data(), repeatedMain.size(), kReachMainRenderViewAob.data(),
                  exactMask.data(), kReachMainRenderViewAob.size()) == 2,
            "Reach render scanner counts every exact executable candidate");

        auto playerEntry = kReachPlayerViewRenderAob;
        playerEntry[49] = 0x12;
        playerEntry[50] = 0x34;
        playerEntry[51] = 0x56;
        playerEntry[52] = 0x78;
        Check(CountReachMaskedPattern(playerEntry.data(), playerEntry.size(), kReachPlayerViewRenderAob.data(),
                  kReachPlayerViewRenderAobMask.data(), kReachPlayerViewRenderAob.size()) == 1,
            "Reach inner signature wildcards only the four cookie-displacement bytes");
        playerEntry[48] ^= 1;
        Check(CountReachMaskedPattern(playerEntry.data(), playerEntry.size(), kReachPlayerViewRenderAob.data(),
                  kReachPlayerViewRenderAobMask.data(), kReachPlayerViewRenderAob.size()) == 0,
            "Reach inner signature rejects a changed non-cookie byte");

        auto outerCameraCallbackEntry = kReachCameraStackCallbackAob;
        for (size_t index = 9; index <= 12; ++index)
            outerCameraCallbackEntry[index] = static_cast<uint8_t>(index * 7);
        for (size_t index = 16; index <= 19; ++index)
            outerCameraCallbackEntry[index] = static_cast<uint8_t>(index * 11);
        Check(CountReachMaskedPattern(outerCameraCallbackEntry.data(), outerCameraCallbackEntry.size(),
                  kReachCameraStackCallbackAob.data(), kReachCameraStackCallbackAobMask.data(),
                  kReachCameraStackCallbackAob.size()) == 1,
            "Reach outer-camera callback signature masks only its two RIP-relative globals");
        outerCameraCallbackEntry[20] ^= 1;
        Check(CountReachMaskedPattern(outerCameraCallbackEntry.data(), outerCameraCallbackEntry.size(),
                  kReachCameraStackCallbackAob.data(), kReachCameraStackCallbackAobMask.data(),
                  kReachCameraStackCallbackAob.size()) == 0,
            "Reach outer-camera callback signature rejects a changed fixed byte");

        auto frustumEntry = kReachFrustumHelperAob;
        std::array<uint8_t, kReachFrustumHelperAob.size()> frustumMask{};
        frustumMask.fill(0xFF);
        Check(CountReachMaskedPattern(frustumEntry.data(), frustumEntry.size(), kReachFrustumHelperAob.data(),
                  frustumMask.data(), kReachFrustumHelperAob.size()) == 1,
            "Reach production pattern pins the canonical 25-byte frustum entry");
        frustumEntry.back() ^= 1;
        Check(CountReachMaskedPattern(frustumEntry.data(), frustumEntry.size(), kReachFrustumHelperAob.data(),
                  frustumMask.data(), kReachFrustumHelperAob.size()) == 0,
            "Reach production pattern checks the byte omitted by the historical observer");
        Check(CountReachMaskedPattern(
                  nullptr, 1, kReachMainRenderViewAob.data(), exactMask.data(), kReachMainRenderViewAob.size()) == 0 &&
                  CountReachMaskedPattern(repeatedMain.data(), repeatedMain.size(), nullptr, exactMask.data(),
                      kReachMainRenderViewAob.size()) == 0,
            "Reach masked scanning fails closed on invalid buffers");

        auto fpCameraEntry = kReachFpCameraRebuildAob;
        fpCameraEntry[22] = 0x12;
        fpCameraEntry[23] = 0x34;
        fpCameraEntry[24] = 0x56;
        fpCameraEntry[25] = 0x78;
        Check(CountReachMaskedPattern(fpCameraEntry.data(), fpCameraEntry.size(), kReachFpCameraRebuildAob.data(),
                  kReachFpCameraRebuildAobMask.data(), kReachFpCameraRebuildAob.size()) == 1,
            "Reach FP camera signature masks only the LEA displacement");
        fpCameraEntry.back() ^= 1;
        Check(CountReachMaskedPattern(fpCameraEntry.data(), fpCameraEntry.size(), kReachFpCameraRebuildAob.data(),
                  kReachFpCameraRebuildAobMask.data(), kReachFpCameraRebuildAob.size()) == 0,
            "Reach FP camera signature rejects a changed fixed byte");
        auto fpUploadEntry = kReachFpCameraUploadAob;
        std::array<uint8_t, kReachFpCameraUploadAob.size()> fpUploadMask{};
        fpUploadMask.fill(0xFF);
        Check(CountReachMaskedPattern(fpUploadEntry.data(), fpUploadEntry.size(), kReachFpCameraUploadAob.data(),
                  fpUploadMask.data(), kReachFpCameraUploadAob.size()) == 1,
            "Reach FP camera uploader signature is exact");
        fpUploadEntry[19] ^= 1;
        Check(CountReachMaskedPattern(fpUploadEntry.data(), fpUploadEntry.size(), kReachFpCameraUploadAob.data(),
                  fpUploadMask.data(), kReachFpCameraUploadAob.size()) == 0,
            "Reach FP camera uploader rejects a changed fixed byte");

        struct ReachFpNestedSelectorInput
        {
            uintptr_t moduleBase;
            size_t moduleSize;
            uintptr_t stackTop;
            uintptr_t workspaceCallback;
            uintptr_t fpView;
        };
        const uintptr_t fpModuleBase = 0x10000000u;
        const ReachFpNestedSelectorInput validFpNested{fpModuleBase, kReachRetailImageSize,
            fpModuleBase + kReachFpCameraWorkspaceRva, fpModuleBase + kReachFpCameraWorkspaceCallbackRva,
            fpModuleBase + kReachFpCameraViewRva};
        const auto selectFpNested = [](const ReachFpNestedSelectorInput& input)
        {
            return SelectReachFpCameraNestedWorkspace(
                input.moduleBase, input.moduleSize, input.stackTop, input.workspaceCallback, input.fpView);
        };
        const auto fpNestedRejects = [&validFpNested, &selectFpNested](auto mutate)
        {
            auto candidate = validFpNested;
            mutate(candidate);
            return selectFpNested(candidate) == 0;
        };
        Check(selectFpNested(validFpNested) == validFpNested.stackTop,
            "Reach FP camera selector accepts the exact nested workspace, callback, and view");
        const uintptr_t selectedFpNested = selectFpNested(validFpNested);
        Check(selectedFpNested + kReachSecondaryDerivedOffset == fpModuleBase + kReachFpCameraWorkspaceRva + 0x1E4 &&
                  kReachSecondaryDerivedOffset + kReachDerivedBlockSize <= kReachFpCameraWorkspaceCallbackOffset,
            "Reach FP camera selector targets nested+0x1E4 without crossing the callback");
        Check(kReachFpCameraWorkspaceCallbackOffset + sizeof(uintptr_t) == kReachRenderScopeSnapshotSize &&
                  kReachFpCameraWorkspaceRva <= kReachRetailImageSize - kReachRenderScopeSnapshotSize,
            "Reach FP camera callback and full snapshot stay inside the fixed nested workspace range");
        Check(fpNestedRejects(
                  [](auto& v)
                  {
                      v.moduleBase = 0;
                      v.stackTop = kReachFpCameraWorkspaceRva;
                      v.workspaceCallback = kReachFpCameraWorkspaceCallbackRva;
                      v.fpView = kReachFpCameraViewRva;
                  }) &&
                  fpNestedRejects([](auto& v) { v.moduleSize = 0; }) &&
                  fpNestedRejects([](auto& v) { v.moduleSize = kReachRetailImageSize - 1; }) &&
                  fpNestedRejects([](auto& v) { v.moduleSize = kReachRetailImageSize + 1; }) &&
                  fpNestedRejects([](auto& v) { --v.stackTop; }) && fpNestedRejects([](auto& v) { ++v.stackTop; }) &&
                  fpNestedRejects([](auto& v) { v.workspaceCallback = 0; }) &&
                  fpNestedRejects([](auto& v) { --v.workspaceCallback; }) &&
                  fpNestedRejects([](auto& v) { ++v.workspaceCallback; }) &&
                  fpNestedRejects([](auto& v) { v.fpView = 0; }) && fpNestedRejects([](auto& v) { --v.fpView; }) &&
                  fpNestedRejects([](auto& v) { ++v.fpView; }) &&
                  fpNestedRejects([](auto& v) { v.stackTop = v.moduleBase + kReachDefaultWorkspaceRva; }) &&
                  fpNestedRejects(
                      [](auto& v)
                      {
                          v.moduleBase = std::numeric_limits<uintptr_t>::max() - kReachFpCameraWorkspaceRva + 2;
                          v.stackTop = v.moduleBase + kReachFpCameraWorkspaceRva;
                          v.workspaceCallback = v.moduleBase + kReachFpCameraWorkspaceCallbackRva;
                          v.fpView = v.moduleBase + kReachFpCameraViewRva;
                      }),
            "Reach FP camera selector fails closed for every invalid identity component");
        const uintptr_t fpBoundaryBase = std::numeric_limits<uintptr_t>::max() - kReachFpCameraWorkspaceRva;
        const ReachFpNestedSelectorInput boundaryFpNested{fpBoundaryBase, kReachRetailImageSize,
            fpBoundaryBase + kReachFpCameraWorkspaceRva, fpBoundaryBase + kReachFpCameraWorkspaceCallbackRva,
            fpBoundaryBase + kReachFpCameraViewRva};
        Check(selectFpNested(boundaryFpNested) == std::numeric_limits<uintptr_t>::max(),
            "Reach FP camera selector accepts the last non-overflowing nested workspace");

        ReachRenderCandidateProof proof = CompleteReachRenderCandidateProof();
        Check(
            ReachRenderCandidateProofComplete(proof), "Reach render proof requires every exact static preflight gate");
        const auto proofRejects = [&proof](auto mutate)
        {
            auto candidate = proof;
            mutate(candidate);
            return !ReachRenderCandidateProofComplete(candidate);
        };
        Check(proofRejects([](auto& p) { p.retailIdentity = false; }) &&
                  proofRejects([](auto& p) { p.mainRenderViewMatchCount = 0; }) &&
                  proofRejects([](auto& p) { p.mainRenderViewMatchCount = 2; }) &&
                  proofRejects([](auto& p) { p.mainRenderViewAtExpectedRva = false; }) &&
                  proofRejects([](auto& p) { p.mainRenderViewBodyHash = false; }) &&
                  proofRejects([](auto& p) { p.playerViewRenderMatchCount = 0; }) &&
                  proofRejects([](auto& p) { p.playerViewRenderMatchCount = 2; }) &&
                  proofRejects([](auto& p) { p.playerViewRenderAtExpectedRva = false; }) &&
                  proofRejects([](auto& p) { p.playerViewRenderBodyHash = false; }) &&
                  proofRejects([](auto& p) { p.cameraStackCallbackMatchCount = 0; }) &&
                  proofRejects([](auto& p) { p.cameraStackCallbackMatchCount = 2; }) &&
                  proofRejects([](auto& p) { p.cameraStackCallbackAtExpectedRva = false; }) &&
                  proofRejects([](auto& p) { p.cameraStackCallbackBodyHash = false; }) &&
                  proofRejects([](auto& p) { p.frustumHelperMatchCount = 0; }) &&
                  proofRejects([](auto& p) { p.frustumHelperMatchCount = 2; }) &&
                  proofRejects([](auto& p) { p.frustumHelperAtExpectedRva = false; }) &&
                  proofRejects([](auto& p) { p.frustumHelperExecutableRange = false; }) &&
                  proofRejects([](auto& p) { p.fpCameraRebuildMatchCount = 0; }) &&
                  proofRejects([](auto& p) { p.fpCameraRebuildMatchCount = 2; }) &&
                  proofRejects([](auto& p) { p.fpCameraRebuildAtExpectedRva = false; }) &&
                  proofRejects([](auto& p) { p.fpCameraRebuildBodyHash = false; }) &&
                  proofRejects([](auto& p) { p.fpCameraUploadMatchCount = 0; }) &&
                  proofRejects([](auto& p) { p.fpCameraUploadMatchCount = 2; }) &&
                  proofRejects([](auto& p) { p.fpCameraUploadAtExpectedRva = false; }) &&
                  proofRejects([](auto& p) { p.fpCameraUploadBodyHash = false; }) &&
                  proofRejects([](auto& p) { p.fpCameraWrapperBodyHashes = false; }) &&
                  proofRejects([](auto& p) { p.exactFpCameraFlowEdges = false; }) &&
                  proofRejects([](auto& p) { p.exactOuterCallerEdges = false; }) &&
                  proofRejects([](auto& p) { p.exactInnerCallerEdge = false; }) &&
                  proofRejects([](auto& p) { p.fixedDataRanges = false; }),
            "Reach render proof fails closed when any identity gate is absent");

        const ReachModuleEpoch proofEpoch{0x10000000u, 1};
        ReachPreflightPublication preflightPublication;
        Check(preflightPublication.Publish(proofEpoch, proof),
            "Reach preflight publication accepts one complete exact proof");
        const ReachPreflightToken preflight = preflightPublication.Get(proofEpoch);
        auto incompleteProof = proof;
        incompleteProof.frustumHelperMatchCount = 0;
        ReachPreflightPublication incompletePublication;
        Check(preflight.Complete() && IsPreflightCurrent(preflight) &&
                  ReachSameModuleEpoch(preflight.Epoch(), proofEpoch) &&
                  !incompletePublication.Publish(proofEpoch, incompleteProof) &&
                  !incompletePublication.Get(proofEpoch).Complete(),
            "Reach preflight authorization is publication-bound and requires the full proof");
        ReachPreflightPublication replacementPublication;
        Check(replacementPublication.Publish(proofEpoch, proof),
            "Reach preflight replacement test begins with one current proof");
        const ReachPreflightToken replacedPreflight = replacementPublication.Get(proofEpoch);
        Check(replacedPreflight.Complete() && replacementPublication.IsCurrent(replacedPreflight) &&
                  !replacementPublication.Publish(proofEpoch, incompleteProof) &&
                  !replacementPublication.HasCurrent() && !replacementPublication.IsCurrent(replacedPreflight),
            "A failed preflight replacement revokes prior readiness and copied tokens");

        ReachDisplaySurfaceProof displayProof = CompleteReachDisplaySurfaceProof(proofEpoch, preflight);
        Check(ReachDisplaySurfaceProofComplete(displayProof),
            "Reach display proof accepts exact buffer0 shape and record0 structural continuity");
        const auto displayRejects = [&displayProof](auto mutate)
        {
            auto candidate = displayProof;
            mutate(candidate);
            return !ReachDisplaySurfaceProofComplete(candidate);
        };
        Check(
            displayRejects([](auto& p) { p.preflight = {}; }) &&
                displayRejects([](auto& p) { ++p.continuity.epoch.generation; }) &&
                displayRejects([](auto& p) { p.continuity.resourceRevision = 0; }) &&
                displayRejects([](auto& p) { p.continuity.lifecycleSerial = 0; }) &&
                displayRejects([](auto& p) { p.continuity.lifecycleSerial = std::numeric_limits<uint64_t>::max(); }) &&
                displayRejects([](auto& p) { p.continuity.swapchainIdentity = 0; }) &&
                displayRejects([](auto& p) { p.continuity.buffer0Identity = 0; }) &&
                displayRejects([](auto& p) { p.continuity.surfaceArrayIdentity = 0; }) &&
                displayRejects([](auto& p) { p.continuity.record0RtvIdentity = 0; }) &&
                displayRejects([](auto& p) { p.continuity.record0SrvIdentity = 0; }) &&
                displayRejects([](auto& p) { ++p.continuity.selectedRtvIdentity; }) &&
                displayRejects([](auto& p) { p.continuity.deviceIdentity = 0; }) &&
                displayRejects([](auto& p) { ++p.continuity.immediateContextIdentity; }) &&
                displayRejects([](auto& p) { ++p.continuity.eyeResourceIdentities[0]; }) &&
                displayRejects([](auto& p) { ++p.continuity.eyeResourceIdentities[1]; }) &&
                displayRejects([](auto& p) { p.continuity.specializationCount = 3; }) &&
                displayRejects([](auto& p) { p.continuity.selectedSpecialization = 1; }) &&
                displayRejects([](auto& p) { p.continuity.teardownRequested = true; }) &&
                displayRejects([](auto& p) { p.eyeResourceIdentities[1] = p.eyeResourceIdentities[0]; }) &&
                displayRejects([](auto& p) { p.eyeResourceIdentities[0] = p.continuity.buffer0Identity; }) &&
                displayRejects([](auto& p) { p.immediateContextIdentity = 0; }) &&
                displayRejects([](auto& p) { p.source.width = 0; }) &&
                displayRejects([](auto& p) { p.source.format = 0; }) &&
                displayRejects([](auto& p) { ++p.eyes[0].width; }) &&
                displayRejects([](auto& p) { ++p.eyes[1].sampleCount; }) &&
                displayRejects([](auto& p) { p.readyEyeMask = 1; }) &&
                displayRejects([](auto& p) { p.engineSwapchainMatchesPresent = false; }) &&
                displayRejects([](auto& p) { p.selectedRtvMatchesRecord0 = false; }) &&
                displayRejects([](auto& p) { p.swapchainContract = false; }) &&
                displayRejects([](auto& p) { p.sameDevice = false; }) &&
                displayRejects([](auto& p) { p.immediateContext = false; }),
            "Reach display proof rejects stale identity, aliasing, partial eyes, and every copy-shape/context mismatch");

        ReachDirectCopyGate displayGate;
        const ReachPreparedFrameToken displayFrame = ReachPreparedFrameToken::Create(proofEpoch, 11, true);
        Check(displayGate.AdvanceEpoch(proofEpoch) && displayGate.Publish(displayProof),
            "Reach direct-copy gate admits one monotonic cold resource proof");
        const ReachDirectCopyToken firstCopy = displayGate.Prepare(displayFrame, displayProof.continuity);
        Check(firstCopy.Ready() && displayGate.IsCurrent(firstCopy, displayProof.continuity),
            "A current display proof mints one live direct-copy token");
        const auto liveContinuityRejects = [&displayGate, &firstCopy, &displayFrame, &displayProof](auto mutate)
        {
            auto live = displayProof.continuity;
            mutate(live);
            return !displayGate.IsCurrent(firstCopy, live) && !displayGate.Prepare(displayFrame, live).Ready();
        };
        Check(liveContinuityRejects([](auto& c) { c.epoch.moduleBase += 0x10000; }) &&
                  liveContinuityRejects([](auto& c) { ++c.epoch.generation; }) &&
                  liveContinuityRejects([](auto& c) { ++c.resourceRevision; }) &&
                  liveContinuityRejects([](auto& c) { ++c.lifecycleSerial; }) &&
                  liveContinuityRejects([](auto& c) { ++c.swapchainIdentity; }) &&
                  liveContinuityRejects([](auto& c) { ++c.buffer0Identity; }) &&
                  liveContinuityRejects([](auto& c) { ++c.surfaceArrayIdentity; }) &&
                  liveContinuityRejects([](auto& c) { ++c.record0RtvIdentity; }) &&
                  liveContinuityRejects([](auto& c) { ++c.record0SrvIdentity; }) &&
                  liveContinuityRejects([](auto& c) { ++c.selectedRtvIdentity; }) &&
                  liveContinuityRejects([](auto& c) { ++c.deviceIdentity; }) &&
                  liveContinuityRejects([](auto& c) { ++c.immediateContextIdentity; }) &&
                  liveContinuityRejects([](auto& c) { ++c.eyeResourceIdentities[0]; }) &&
                  liveContinuityRejects([](auto& c) { ++c.eyeResourceIdentities[1]; }) &&
                  liveContinuityRejects([](auto& c) { ++c.specializationCount; }) &&
                  liveContinuityRejects([](auto& c) { ++c.selectedSpecialization; }) &&
                  liveContinuityRejects([](auto& c) { c.teardownRequested = true; }),
            "Every live Reach display-continuity field invalidates copied and newly prepared tokens when it drifts");
        ReachDisplaySurfaceProof invalidReplacement = CompleteReachDisplaySurfaceProof(proofEpoch, preflight, 2);
        invalidReplacement.sameDevice = false;
        Check(!displayGate.Publish(invalidReplacement) && !displayGate.Ready() &&
                  !displayGate.IsCurrent(firstCopy, displayProof.continuity),
            "A failed display replacement revokes prior readiness and copied tokens");
        auto changedContinuity = displayProof.continuity;
        ++changedContinuity.record0RtvIdentity;
        Check(!displayGate.IsCurrent(firstCopy, changedContinuity) && displayGate.Invalidate(proofEpoch) &&
                  !displayGate.IsCurrent(firstCopy, displayProof.continuity) && !displayGate.Publish(displayProof),
            "Resize or live view drift invalidates copied tokens and forbids resource-revision replay");
        ReachDisplaySurfaceProof nextDisplayProof = CompleteReachDisplaySurfaceProof(proofEpoch, preflight, 2);
        const ReachPreparedFrameToken nextDisplayFrame = ReachPreparedFrameToken::Create(proofEpoch, 12, true);
        Check(displayGate.Publish(nextDisplayProof),
            "A higher same-epoch resource revision may re-arm after ResizeBuffers or resident title ambiguity");
        const ReachDirectCopyToken nextCopy = displayGate.Prepare(nextDisplayFrame, nextDisplayProof.continuity);
        Check(nextCopy.Ready() && nextCopy.PreparedFrameSerial() == 12 && nextCopy.ResourceRevision() == 2 &&
                  !displayGate.IsCurrent(firstCopy, nextDisplayProof.continuity),
            "A new resource publication invalidates its predecessor");

        ReachPreflightPublication differentPublication;
        Check(differentPublication.Publish(proofEpoch, proof) && !differentPublication.IsCurrent(preflight),
            "A Reach preflight token cannot cross publication owners");
        const uint64_t firstPublicationNonce = preflight.PublicationNonce();
        Check(preflightPublication.Publish(proofEpoch, proof),
            "The same module epoch may receive a newer cold-proof publication");
        const ReachPreflightToken reissuedPreflight = preflightPublication.Get(proofEpoch);
        Check(reissuedPreflight.Complete() && IsPreflightCurrent(reissuedPreflight) &&
                  reissuedPreflight.PublicationNonce() > firstPublicationNonce && preflight.Complete() &&
                  !IsPreflightCurrent(preflight) && !ReachDisplaySurfaceProofComplete(nextDisplayProof) &&
                  !displayGate.Ready() && !displayGate.IsCurrent(nextCopy, nextDisplayProof.continuity) &&
                  !displayGate.Publish(nextDisplayProof),
            "A newer preflight nonce rejects copied-token replay and stale display publication");
        ReachDisplaySurfaceProof reissuedDisplayProof =
            CompleteReachDisplaySurfaceProof(proofEpoch, reissuedPreflight, 3);
        const ReachPreparedFrameToken reissuedDisplayFrame = ReachPreparedFrameToken::Create(proofEpoch, 13, true);
        Check(displayGate.Publish(reissuedDisplayProof),
            "A current preflight nonce can publish a newer display revision");
        const ReachDirectCopyToken reissuedCopy =
            displayGate.Prepare(reissuedDisplayFrame, reissuedDisplayProof.continuity);
        Check(reissuedCopy.Ready() && displayGate.IsCurrent(reissuedCopy, reissuedDisplayProof.continuity) &&
                  displayGate.Teardown(proofEpoch) &&
                  !displayGate.IsCurrent(reissuedCopy, reissuedDisplayProof.continuity) &&
                  !displayGate.AdvanceEpoch(proofEpoch) && displayGate.AdvanceEpoch({proofEpoch.moduleBase, 2}) &&
                  displayGate.Teardown({proofEpoch.moduleBase, 2}),
            "Reach direct-copy teardown rejects old resource and module-generation replay");

        ReachRenderFreshnessGate freshness;
        const ReachModuleEpoch freshnessEpoch{0x10000000u, 7};
        Check(freshness.AdvanceEpoch(freshnessEpoch) &&
                  !freshness.Observe(100, freshnessEpoch, 1, true, true).Stable() &&
                  !freshness.Observe(499, freshnessEpoch, 2, true, true).Stable() &&
                  !freshness.Observe(898, freshnessEpoch, 3, true, true).Stable() &&
                  !freshness.Observe(1100, freshnessEpoch, 4, true, true).Stable() && freshness.CurrentSpanMs() == 1000,
            "Reach production freshness arms only on a new transaction after one second");
        const ReachFreshCameraToken stableFreshness = freshness.Observe(1101, freshnessEpoch, 5, true, true);
        Check(stableFreshness.Stable() && freshness.IsCurrent(stableFreshness) &&
                  stableFreshness.PreparedFrameSerial() == 5,
            "Reach stable freshness is bound to the exact observed frame serial");
        const ReachModuleEpoch staleFreshnessEpoch{0x10000000u, 6};
        const ReachFreshCameraToken nextFreshness = freshness.Observe(1200, freshnessEpoch, 6, true, true);
        Check(!freshness.IsCurrent(stableFreshness) && nextFreshness.Stable() && freshness.IsCurrent(nextFreshness) &&
                  !freshness.AdvanceEpoch(staleFreshnessEpoch) &&
                  !freshness.Consume(nextFreshness, ReachPreparedFrameToken::Create(freshnessEpoch, 6, true), 1700) &&
                  !freshness.IsCurrent(nextFreshness) &&
                  !freshness.Observe(1700, freshnessEpoch, 7, true, true).Stable() &&
                  freshness.TransactionCount() == 1 && !freshness.Teardown(staleFreshnessEpoch),
            "Reach freshness invalidates saved tokens on a new observation or continuity gap");
        Check(freshness.Teardown(freshnessEpoch) && !freshness.AdvanceEpoch(freshnessEpoch) &&
                  freshness.AdvanceEpoch({0x10000000u, 8}) &&
                  !freshness.Observe(1800, {0x10000000u, 8}, 1, false, true).Stable() &&
                  freshness.TransactionCount() == 0 && freshness.Teardown({0x10000000u, 8}) &&
                  !freshness.AdvanceEpoch({0x10000000u, 8}) && freshness.AdvanceEpoch({0x10000000u, 9}),
            "Reach freshness rejects stale epochs, gaps, non-normal owners, and generation replay");
    }

    CheckUnit(195);
    // An unmatched vehicle's seat must key its own row, so a seated F1
    // adjustment can never reach the universal trim shared by all three
    // titles. This is the exact defect a headset session hit.
    {
        const uint64_t unmatchedSnapshot = ReachVehicleTrimSnapshot(7u, ReachVehicleId::Unknown, 3);
        const uint64_t knownSnapshot = ReachVehicleTrimSnapshot(7u, ReachVehicleId::Warthog, 1);
        Check(ReachVehicleTrimSnapshotSlot(unmatchedSnapshot, 7u, kReachVehicleSeatSlots) ==
                      ConfigReachSeatTrimSlot(kReachUnmatchedVehicleTrimId, 3) &&
                  ReachVehicleTrimSnapshotSlot(knownSnapshot, 7u, kReachVehicleSeatSlots) ==
                      ConfigReachSeatTrimSlot(static_cast<int>(ReachVehicleId::Warthog), 1) &&
                  ReachVehicleTrimSnapshotSlot(unmatchedSnapshot, 8u, kReachVehicleSeatSlots) == -1,
            "An unmatched Reach seat keys its own trim row, never the universal trim");
    }
    CheckUnit(196);
    // The retail maps report the official type-turret physics block for HREK's
    // type-16 mounted weapons. Measured 2026-08-06: the rocket Warthog turret
    // arrived as the canonical HREK tuple with observed type 6 and was
    // rejected, which is what dropped it onto the universal trim.
    {
        const ReachVehicleFingerprint rocketHog{0x3F12852A, 0x3DC5E04F, 0x3BE1BCFB, 0x3EA9708D};
        const ReachVehicleFingerprint warthog{0x3F978072, 0xBD406A82, 0xBAD03632, 0x3EC25783};
        Check(ReachVehiclePhysicsTypeMatches(ReachVehicleId::WarthogRocket, rocketHog, 6) &&
                  ReachVehiclePhysicsTypeMatches(ReachVehicleId::WarthogRocket, rocketHog, 16) &&
                  !ReachVehiclePhysicsTypeMatches(ReachVehicleId::WarthogRocket, rocketHog, 8) &&
                  ReachVehiclePhysicsTypeMatches(ReachVehicleId::Warthog, warthog, 1) &&
                  !ReachVehiclePhysicsTypeMatches(ReachVehicleId::Warthog, warthog, 6),
            "HREK type-16 mounted weapons also resolve at retail's turret physics type");
    }
    CheckUnit(-1);
    std::cout << "checks run: " << g_checks << '\n';
    if (g_failures == 0)
        std::cout << "HaloMCCVR core reach identity tests passed\n";
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
