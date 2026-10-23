// core_halo4_camera_render_tests.cpp: Halo 4 anchors, cold observation, camera install and readback, CUI, reticle, frustum and 6DOF basis.
// Units 47, 59, 60, 61, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75, 76, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92, 93, 94, 95, 96, 97, 236 of the former tests/core_tests.cpp main(), moved unchanged and in
// their order (E3). Built as halomccvr_core_halo4_camera_render_tests and run by ctest; each unit prints its check count.
#include "core_test_support.h"

int main()
{
    CheckUnit(47);
    {
        Check(VrBlitNeedsHalo4ReticleAlphaRepair(true, 512, 512) &&
                  !VrBlitNeedsHalo4ReticleAlphaRepair(false, 512, 512) &&
                  !VrBlitNeedsHalo4ReticleAlphaRepair(true, 511, 512) &&
                  !VrBlitNeedsHalo4ReticleAlphaRepair(true, 512, 511) &&
                  !VrBlitNeedsHalo4ReticleAlphaRepair(true, 2912, 2100),
            "Stage 3BO alpha repair selects only Halo 4's 512x512 authored "
            "reticle destination");
        const bool halo4ReticleFastPath =
            !VrBlitNeedsHalo4ReticleAlphaRepair(true, 512, 512) &&
            VrBlitCanUseDirectCopy(512, 512, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 512, 512, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        const bool otherTitleReticleFastPath =
            !VrBlitNeedsHalo4ReticleAlphaRepair(false, 512, 512) &&
            VrBlitCanUseDirectCopy(512, 512, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 512, 512, DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        const bool halo4EyeFastPath = !VrBlitNeedsHalo4ReticleAlphaRepair(true, 2912, 2100) &&
                                      VrBlitCanUseDirectCopy(2912, 2100, DXGI_FORMAT_R8G8B8A8_UNORM, 1, 2912, 2100,
                                          DXGI_FORMAT_R8G8B8A8_UNORM_SRGB);
        Check(!halo4ReticleFastPath && otherTitleReticleFastPath && halo4EyeFastPath,
            "Stage 3BO forces the Halo 4 reticle through the alpha-repair "
            "shader without disabling direct copies for other blits");
        Check(VrResolveAuthoredReticleCoverage(false, 17, 900) == 17 &&
                  VrResolveAuthoredReticleCoverage(true, 17, 900) == 900 &&
                  VrResolveAuthoredReticleCoverage(true, 901, 900) == 901,
            "Stage 3BL uses RGB coverage only for Halo 4 and never discards "
            "stronger alpha coverage");
        Check(VrHalo4ReticleRtvNeedsTypedFallback(true, true, true) &&
                  !VrHalo4ReticleRtvNeedsTypedFallback(false, true, true) &&
                  !VrHalo4ReticleRtvNeedsTypedFallback(true, false, true) &&
                  !VrHalo4ReticleRtvNeedsTypedFallback(true, true, false),
            "Stage 3BQ retries a typed RTV only for a failed Halo 4 authored-"
            "reticle view creation");
        bool writebackTruthTable = true;
        for (uint32_t bits = 0; bits < 16; ++bits)
        {
            const bool halo4Title = (bits & 1u) != 0;
            const bool contextReady = (bits & 2u) != 0;
            const bool sceneAlreadyLatched = (bits & 4u) != 0;
            const bool eyeCacheReady = (bits & 8u) != 0;
            for (int eye = -1; eye <= 2; ++eye)
            {
                const bool expected =
                    halo4Title && (eye == 0 || eye == 1) && contextReady && sceneAlreadyLatched && eyeCacheReady;
                writebackTruthTable =
                    writebackTruthTable && VrHalo4SceneWritebackEligible(halo4Title, eye, contextReady,
                                               sceneAlreadyLatched, eyeCacheReady) == expected;
            }
        }
        Check(writebackTruthTable, "Stage 3BU scene writeback requires Halo 4, a completed eye, "
                                   "context, a pre-existing scene target, and that eye's cache");
        constexpr uintptr_t authoredTarget = 0x1000;
        constexpr uintptr_t discardTarget = 0x2000;
        Check(VrHalo4AuthoredReticleDrawTargetMatches(
                  true, true, true, true, authoredTarget, authoredTarget, discardTarget) &&
                  !VrHalo4AuthoredReticleDrawTargetMatches(
                      true, true, true, true, discardTarget, authoredTarget, discardTarget) &&
                  VrHalo4AuthoredReticleDrawTargetMatches(
                      true, true, true, false, discardTarget, authoredTarget, discardTarget) &&
                  !VrHalo4AuthoredReticleDrawTargetMatches(
                      true, true, true, false, authoredTarget, authoredTarget, discardTarget) &&
                  !VrHalo4AuthoredReticleDrawTargetMatches(
                      false, true, true, true, authoredTarget, authoredTarget, discardTarget) &&
                  !VrHalo4AuthoredReticleDrawTargetMatches(
                      true, false, true, true, authoredTarget, authoredTarget, discardTarget) &&
                  !VrHalo4AuthoredReticleDrawTargetMatches(
                      true, true, false, true, authoredTarget, authoredTarget, discardTarget),
            "Stage 3BH/3BR draw framing admits only the active mode-selected "
            "slot-0 private target");
    }
    CheckUnit(59);
    const Halo4EvidenceIdentity& halo4Identity = Halo4Adapter_GetEvidenceIdentity();
    Check(Halo4ParityCommandFitsBucket(0x00) && Halo4ParityCommandFitsBucket(0x28) &&
              Halo4ParityCommandFitsBucket(0xFF) && !Halo4ParityCommandFitsBucket(-1) &&
              !Halo4ParityCommandFitsBucket(0x100) && Halo4ParityCommandBucket(0x28) == 0x28,
        "The Halo 4 parity trace bins only complete byte-range CUI command IDs");
    CheckUnit(60);
    {
        int32_t ids[kHalo4ParityTransformSlotCount]{};
        for (int32_t& id : ids)
            id = kHalo4ParityEmptyTransformId;
        ids[0] = 17;
        ids[1] = 42;
        Check(Halo4ParityFindTransformSlot(ids, 42) == 1 && Halo4ParityFindTransformSlot(ids, 99) == 2,
            "The Halo 4 parity trace reuses an exact transform ID and admits a new ID only into an empty bounded slot");
        for (size_t i = 0; i < kHalo4ParityTransformSlotCount; ++i)
            ids[i] = static_cast<int32_t>(i);
        Check(Halo4ParityFindTransformSlot(ids, 7) == 7 && Halo4ParityFindTransformSlot(ids, 99) == -1,
            "The Halo 4 parity trace reports full-table overflow without merging identities");
    }
    CheckUnit(61);
    const TitleDescriptor* halo4Row = TitleRegistry_Find(GameTitle::Halo4);
    Check(Halo4Adapter_GetStage() == Halo4AdapterStage::ControllerInputAndStereoCamera,
        "C-H4-3 stages Halo 4 at controller transport plus the camera core");
    Check(TitleRegistry_HookPlan(GameTitle::Halo4) == TitleHookPlan::Halo4CameraCore,
        "the shared title manager registers Halo 4's compiled camera core for "
        "generation-owned cross-title handoff");
    Check(Halo4Adapter_RuntimeHooksPermitted(), "C-H4-3 permits the camera core's hooks; the install proof, not this "
                                                "flag, is what actually admits them");
    CheckUnit(63);
    // Anchor table: every entry must be well-formed offline, because
    // the runtime scan trusts its shape (each pattern's on-disk uniqueness is
    // proven against the retail module; the offline
    // grammar here is deliberately STRICTER than sig::Find's - uppercase hex
    // and '??' only - so a house-style violation fails before it ships).
    Check(kHalo4RetailAnchorCount == 4, "C-H4-2 pins exactly the four E-H4-4 anchors");
    CheckUnit(64);
    for (const Halo4RetailAnchor& anchor : kHalo4RetailAnchors)
    {
        Check(anchor.name && anchor.name[0] && anchor.pattern && anchor.pattern[0] != '?' && anchor.rva != 0 &&
                  anchor.rva < kHalo4RetailImageSize,
            "Every Halo 4 anchor names itself, starts concrete, and pins an "
            "in-image RVA");
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
        Check(tokensValid && tokenBytes >= 12, "Every Halo 4 anchor pattern parses and is long enough to be "
                                               "meaningfully unique");
        // A rip decode must index bytes the pattern actually matched, and
        // must aim at a real in-image data anchor.
        Check(anchor.ripDispOffset == 0 || (static_cast<size_t>(anchor.ripDispOffset) + 4 <= tokenBytes &&
                                               anchor.ripTargetRva != 0 && anchor.ripTargetRva < kHalo4RetailImageSize),
            "Every Halo 4 rip decode stays inside its own match and lands "
            "in-image");
    }
    CheckUnit(65);
    Check(kHalo4PlayerViewArrayRva == 0x30AD1C0u && kHalo4PlayerViewStride == 0xAD0u &&
              kHalo4ViewStackTopRva == 0xE84634u,
        "The Halo 4 data anchors pin E-H4-4's array, stride, and stack top");
    Check(kHalo4PlayerViewArrayRva + 4ull * kHalo4PlayerViewStride <= kHalo4RetailImageSize &&
              kHalo4ViewStackTopRva + 4u <= kHalo4RetailImageSize,
        "The Halo 4 array span and stack top fit inside the pinned image");
    // The named indices are part of the contract: the level-load gate reuses
    // the ctor pattern by index, and push/pop must cross-check on ONE stack
    // top.
    Check(kHalo4RetailAnchors[kHalo4AnchorCtor].ripDispOffset == 0x0D &&
              kHalo4RetailAnchors[kHalo4AnchorCtor].ripTargetRva == kHalo4PlayerViewArrayRva &&
              kHalo4RetailAnchors[kHalo4AnchorPush].ripTargetRva == kHalo4ViewStackTopRva &&
              kHalo4RetailAnchors[kHalo4AnchorPop].ripTargetRva == kHalo4ViewStackTopRva &&
              kHalo4RetailAnchors[kHalo4AnchorClamp].ripDispOffset == 0,
        "The Halo 4 anchor indices bind ctor->array, push/pop->stack top, "
        "clamp->no decode");
    Check(kHalo4RetailAnchorRipTargets == 3, "Exactly three Halo 4 anchors carry rip decodes");

    // Verdict truth table: complete observations pass, and each
    // degraded field fails closed on its own.
    Halo4ColdObservationResult halo4Cold{};
    halo4Cold.moduleRangeValid = true;
    halo4Cold.peIdentity = true;
    halo4Cold.anchorsMatchedOnce = static_cast<uint32_t>(kHalo4RetailAnchorCount);
    halo4Cold.anchorsAtPinnedRva = static_cast<uint32_t>(kHalo4RetailAnchorCount);
    halo4Cold.ripTargetsAtPinnedRva = kHalo4RetailAnchorRipTargets;
    halo4Cold.mappingStable = true;
    Check(Halo4ColdObservationPass(halo4Cold), "A complete Halo 4 cold observation passes");
    CheckUnit(66);
    {
        Halo4ColdObservationResult broken = halo4Cold;
        broken.moduleRangeValid = false;
        Check(!Halo4ColdObservationPass(broken), "A wrong Halo 4 module size fails the cold observation closed");
        broken = halo4Cold;
        broken.peIdentity = false;
        Check(!Halo4ColdObservationPass(broken), "A wrong Halo 4 PE identity fails the cold observation closed");
        broken = halo4Cold;
        broken.anchorsMatchedOnce -= 1;
        Check(!Halo4ColdObservationPass(broken), "A missing or duplicated Halo 4 anchor fails the cold "
                                                 "observation closed");
        broken = halo4Cold;
        broken.anchorsAtPinnedRva -= 1;
        Check(!Halo4ColdObservationPass(broken), "A moved Halo 4 anchor fails the cold observation closed");
        broken = halo4Cold;
        broken.ripTargetsAtPinnedRva -= 1;
        Check(!Halo4ColdObservationPass(broken), "A Halo 4 rip decode off its pinned target fails the cold "
                                                 "observation closed");
        broken = halo4Cold;
        broken.mappingStable = false;
        Check(!Halo4ColdObservationPass(broken), "A remapped halo4.dll fails the cold observation closed");
    }

    CheckUnit(67);
    // ---- The camera anchors and the per-eye camera math ----
    // The first-person node accessor is anchored only
    // to prove the engine TLS index global - it is never hooked.
    Check(kHalo4CameraAnchorCount == 5, "The four E-H4-6 camera anchors plus C-H4-11's first-person "
                                        "accessor");
    CheckUnit(68);
    for (const Halo4RetailAnchor& anchor : kHalo4CameraAnchors)
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
        Check(anchor.name && anchor.name[0] && anchor.pattern && anchor.pattern[0] != '?' && anchor.rva != 0 &&
                  anchor.rva < kHalo4RetailImageSize && tokensValid && tokenBytes >= 12,
            "Every Halo 4 camera anchor names itself, parses, starts concrete "
            "and pins an in-image RVA");
        Check(anchor.ripDispOffset == 0 || (static_cast<size_t>(anchor.ripDispOffset) + 4 <= tokenBytes &&
                                               anchor.ripTargetRva != 0 && anchor.ripTargetRva < kHalo4RetailImageSize),
            "Every Halo 4 camera rip decode stays inside its own match and "
            "lands in-image");
    }
    CheckUnit(69);
    // The loop anchor's three displacements must all index bytes it matched -
    // a lengthened prefix that did not move these offsets would decode
    // garbage into a hook address.
    Check(kHalo4CameraAnchors[kHalo4CameraAnchorLoop].rva == kHalo4PerWindowLoopRva &&
              kHalo4CameraAnchors[kHalo4CameraAnchorLoop].ripDispOffset == kHalo4LoopElementRipOffset &&
              kHalo4LoopSetupRel32Offset < kHalo4LoopElementRipOffset &&
              kHalo4LoopElementRipOffset < kHalo4LoopWrapperRel32Offset,
        "The Halo 4 loop anchor's three displacements are in program order");
    Check(kHalo4CameraAnchors[kHalo4CameraAnchorSetup].rva == kHalo4SetupRva &&
              kHalo4CameraAnchors[kHalo4CameraAnchorSetup].ripTargetRva == kHalo4StackElementRva &&
              kHalo4CameraAnchors[kHalo4CameraAnchorLoop].ripTargetRva == kHalo4StackElementRva &&
              kHalo4CameraAnchors[kHalo4CameraAnchorWrapper].rva == kHalo4WrapperRva &&
              kHalo4CameraAnchors[kHalo4CameraAnchorWrapper].ripTargetRva == kHalo4ActiveViewRva,
        "The loop and setup anchors derive the SAME stack element, and the "
        "wrapper anchor derives the active-view global");
    Check(kHalo4CameraAnchorRipTargets == 4, "Four Halo 4 anchors carry rip decodes: three camera targets plus "
                                             "the engine TLS index the first-person blocks are reached through");
    Check(kHalo4CameraAnchors[kHalo4CameraAnchorFirstPerson].ripTargetRva == kHalo4EngineTlsIndexRva,
        "The first-person anchor derives the engine TLS index global, which "
        "is what keeps that address out of the shipped constants");
    Check(Halo4CameraLoopTargetsAgree(kHalo4SetupRva, kHalo4WrapperRva) &&
              !Halo4CameraLoopTargetsAgree(kHalo4SetupRva, kHalo4SetupRva) &&
              !Halo4CameraLoopTargetsAgree(0, kHalo4WrapperRva),
        "The loop's call-edge check accepts only the two pinned functions");
    Check(Halo4PreparedPairMatches(17, 17, 17) && !Halo4PreparedPairMatches(0, 0, 0) &&
              !Halo4PreparedPairMatches(18, 17, 18) && !Halo4PreparedPairMatches(18, 18, 17),
        "Halo 4 pair admission rejects zero, partial, and stale eye/FOV "
        "serials");
    Check(Halo4EyeCaptureIsCurrent(1, 1, true, true) && !Halo4EyeCaptureIsCurrent(1, -1, true, true) &&
              !Halo4EyeCaptureIsCurrent(1, 0, true, true) && !Halo4EyeCaptureIsCurrent(1, 1, false, true) &&
              !Halo4EyeCaptureIsCurrent(1, 1, true, false),
        "Halo 4 stamps an eye only while that exact raster scope is active, "
        "redirected, and backed by a cache");
    Check(Halo4XrPairUploadComplete(true, true, true, true) && !Halo4XrPairUploadComplete(false, true, true, true) &&
              !Halo4XrPairUploadComplete(true, false, true, true) &&
              !Halo4XrPairUploadComplete(true, true, false, true) &&
              !Halo4XrPairUploadComplete(true, true, true, false),
        "Halo 4 submits an XR pair only after exact acquire, wait, both-eye "
        "upload, and release completion");
    Check(Halo4XrPairSubmissionAccepted(true, true) && !Halo4XrPairSubmissionAccepted(false, true) &&
              !Halo4XrPairSubmissionAccepted(true, false),
        "Halo 4 reports a headset pair only when its projection was queued "
        "and xrEndFrame returned exact success");
    Check(kHalo4ObserverFovRatioOffset + 4 <= kHalo4ObserverSnapshotBytes &&
              kHalo4ObserverUpOffset + 12 <= kHalo4ObserverSnapshotBytes &&
              kHalo4ObserverForwardOffset + 12 <= kHalo4ObserverUpOffset &&
              kHalo4ObserverPositionOffset + 12 <= kHalo4ObserverForwardOffset &&
              kHalo4ElementProjectionMatrixOffset == 0x100,
        "Every substituted observer field fits inside the saved snapshot and "
        "the proven finished projection begins at element+0x100");

    // The install proof is all-or-nothing: each field fails closed alone.
    Halo4CameraInstallProof halo4Install{};
    halo4Install.coldObservationPassed = true;
    halo4Install.anchorsMatchedOnce = static_cast<uint32_t>(kHalo4CameraAnchorCount);
    halo4Install.anchorsAtPinnedRva = static_cast<uint32_t>(kHalo4CameraAnchorCount);
    halo4Install.ripTargetsAtPinnedRva = kHalo4CameraAnchorRipTargets;
    halo4Install.loopCallTargetsAgree = true;
    halo4Install.executableRange = true;
    halo4Install.mappingStable = true;
    Check(Halo4CameraInstallComplete(halo4Install), "A complete Halo 4 camera install proof admits the hooks");
    CheckUnit(70);
    {
        Halo4CameraInstallProof broken = halo4Install;
        broken.coldObservationPassed = false;
        Check(!Halo4CameraInstallComplete(broken), "Halo 4 refuses to hook without C-H4-2's PASS for this module");
        broken = halo4Install;
        broken.anchorsMatchedOnce -= 1;
        Check(!Halo4CameraInstallComplete(broken), "A missing or duplicated Halo 4 camera anchor refuses the hook");
        broken = halo4Install;
        broken.anchorsAtPinnedRva -= 1;
        Check(!Halo4CameraInstallComplete(broken), "A moved Halo 4 camera anchor refuses the hook");
        broken = halo4Install;
        broken.ripTargetsAtPinnedRva -= 1;
        Check(!Halo4CameraInstallComplete(broken), "A Halo 4 camera rip decode off target refuses the hook");
        broken = halo4Install;
        broken.loopCallTargetsAgree = false;
        Check(!Halo4CameraInstallComplete(broken), "A loop that does not call the two pinned functions refuses the "
                                                   "hook");
        broken = halo4Install;
        broken.executableRange = false;
        Check(!Halo4CameraInstallComplete(broken), "An out-of-image Halo 4 hook site refuses the hook");
        broken = halo4Install;
        broken.mappingStable = false;
        Check(!Halo4CameraInstallComplete(broken), "A remapped halo4.dll refuses the Halo 4 camera hook");
    }

    CheckUnit(71);
    // Halo 4's native reticle transform is an optional CUI feature. Its independent
    // proof fails closed, while every runtime miss fails open to stock without
    // affecting camera ownership or OpenXR.
    Check(kHalo4CuiReticleDispatcherRva == 0x003F0EA4 && kHalo4CuiReticleDispatcherEntryBytes.size() == 24 &&
              std::strcmp(kHalo4CuiReticleDispatcherEntryAob, "48 8B C4 55 56 57 41 56 41 57 48 8D A8 B8 FC FF FF "
                                                              "48 81 EC 50 04 00 00") == 0,
        "The Halo 4 CUI dispatcher is pinned by its unique 24-byte entry");
    Check(kHalo4CuiReticleCallerRva == 0x003F4B6B && kHalo4CuiReticleCallerFixedBytes.size() == 18 &&
              std::strcmp(kHalo4CuiReticleCallerAob, "49 8B 8F 10 04 00 00 4D 8D 8F 20 04 00 00 49 8B D6 "
                                                     "E8 ?? ?? ?? ??") == 0 &&
              kHalo4CuiReticleCallerCallOpcodeOffset == 17 && kHalo4CuiReticleCallerCallDisplacementOffset == 18 &&
              kHalo4CuiReticleCallerCallNextOffset == 22,
        "The Halo 4 CUI caller edge pins its context and rel32 geometry");
    Check(kHalo4CuiGameplayRenderRva == 0x003ACD60 && kHalo4CuiGameplayRenderEntryBytes.size() == 31 &&
              std::strcmp(kHalo4CuiGameplayRenderEntryAob, "48 8B C4 55 53 56 57 41 56 41 57 48 8D 68 B1 48 81 "
                                                           "EC A8 00 00 00 0F 29 78 B8 44 0F 29 40 A8") == 0,
        "Halo 4's CUI front end is pinned independently of its dispatcher");
    Check(kHalo4CuiGameplayCallerRva == 0x00375C51 &&
              std::strcmp(kHalo4CuiGameplayCallerAob, "8B 8E 8C 03 00 00 4C 8D 45 A0 45 33 C9 44 88 6C 24 28 "
                                                      "33 D2 89 7C 24 20 E8 ?? ?? ?? ?? 83 FB 03") == 0 &&
              kHalo4CuiGameplayCallerCallOpcodeOffset == 24 && kHalo4CuiGameplayCallerCallDisplacementOffset == 25 &&
              kHalo4CuiGameplayCallerCallNextOffset == 29 && kHalo4CuiGameplayCallerReturnRva == 0x00375C6E &&
              Halo4CuiGameplayCallerTargetsRender(kHalo4CuiGameplayRenderRva) &&
              !Halo4CuiGameplayCallerTargetsRender(kHalo4CuiReticleDispatcherRva),
        "Only the exact full-size gameplay CUI call can open reticle ownership");
    Check(kHalo4CuiCommandBegin == 0x28 && kHalo4CuiCommandEnd == 0x29 && kHalo4CuiCommandBeginPayloadSize == 0x0C &&
              Halo4CuiReticleCallerTargetsDispatcher(kHalo4CuiReticleDispatcherRva) &&
              !Halo4CuiReticleCallerTargetsDispatcher(kHalo4CuiReticleCallerRva),
        "The Halo 4 CUI command pair and decoded dispatcher edge stay pinned");

    CheckUnit(72);
    {
        std::array<uint8_t, 20 + kHalo4HudAnchorSpanFromBasis> tagBytes{};
        uint8_t* const basisBytes = tagBytes.data() + 20;
        std::memcpy(
            basisBytes + kHalo4HudDamagePrefixFromBasis, kHalo4HudDamagePrefix.data(), sizeof(kHalo4HudDamagePrefix));
        std::memcpy(basisBytes, kHalo4HudAuthoredBasis.data(), kHalo4HudBasisBytes);
        std::memcpy(basisBytes + kHalo4HudSpreadFromBasis, &kHalo4HudDamagePrefix.back(), sizeof(float));
        std::memset(basisBytes + kHalo4HudSpreadFromBasis + sizeof(float), 0xA5,
            16); // relocated tag reference is deliberately wildcarded
        const uint32_t zeroFlags = 0;
        std::memcpy(basisBytes + kHalo4HudFlagsFromBasis, &zeroFlags, sizeof(zeroFlags));
        std::memcpy(
            basisBytes + kHalo4HudContrastFromBasis, kHalo4HudContrastTail.data(), sizeof(kHalo4HudContrastTail));
        Check(Halo4HudAuthoredBasisMatches(basisBytes) && Halo4HudImmutableSurroundMatches(basisBytes),
            "Halo 4 HUD locator accepts the official basis and neighbors");
        basisBytes[kHalo4HudContrastFromBasis] ^= 1;
        Check(!Halo4HudImmutableSurroundMatches(basisBytes),
            "Halo 4 HUD locator rejects a changed immutable contrast tail");
        basisBytes[kHalo4HudContrastFromBasis] ^= 1;

        std::array<Halo4HudBasisPoint, 9> flat{};
        std::array<Halo4HudBasisPoint, 9> authored{};
        std::array<Halo4HudBasisPoint, 9> curved{};
        Check(Halo4ComputeHudBasis(1.0f, 1.0f, 0.0f, 0.0f, flat.data()) &&
                  std::memcmp(flat.data(), kHalo4HudFlatBasis.data(), sizeof(flat)) == 0,
            "Halo 4 hud_curvature=0 produces the identity grid");
        Check(Halo4ComputeHudBasis(1.0f, 1.0f, 0.5f, 0.0f, authored.data()) &&
                  std::fabs(authored[1].x - (-0.98f)) < 0.0001f && std::fabs(authored[3].y - (-0.92f)) < 0.0001f &&
                  std::fabs(authored[7].x - 0.98f) < 0.0001f,
            "Halo 4 hud_curvature=0.5 retains the authored basis");
        Check(Halo4ComputeHudBasis(0.5f, 0.25f, 1.0f, 36.0f, curved.data()) &&
                  std::fabs(curved[1].x - (-0.48f)) < 0.0001f && std::fabs(curved[3].y - (-0.31f)) < 0.0001f &&
                  std::fabs(curved[4].y - (-0.10f)) < 0.0001f,
            "Halo 4 HUD basis combines size, curvature, and positive-up height");
        Check(!Halo4ComputeHudBasis(0.1f, 1.0f, 0.5f, 0.0f, authored.data()) &&
                  !Halo4ComputeHudBasis(1.0f, 1.0f, 1.1f, 0.0f, authored.data()),
            "Halo 4 HUD layout rejects out-of-range config values");
    }

    CheckUnit(73);
    {
        // clang-format off
        std::array<uint8_t, kHalo4PauseReasonGetterBytes> pauseGetter{
            0x8B,0x15,0x11,0x22,0x33,0x44,
            0x65,0x48,0x8B,0x04,0x25,0x58,0x00,0x00,0x00,
            0x41,0xB8,0x90,0x00,0x00,0x00,0x48,0x8B,0x04,0xD0,
            0x49,0x8B,0x14,0x00,0x32,0xC0,0x38,0x02,0x74,0x0F,
            0xB8,0x01,0x00,0x00,0x00,0x66,0xD3,0xE0,0x66,0x85,
            0x42,0x02,0x0F,0x95,0xC0,0xC3};
        // clang-format on
        Check(Halo4PauseReasonGetterMatches(pauseGetter.data(), pauseGetter.size()),
            "Halo 4 native pause getter accepts the exact body with only its RIP displacement wildcarded");
        pauseGetter[22] ^= 1;
        Check(!Halo4PauseReasonGetterMatches(pauseGetter.data(), pauseGetter.size()),
            "Halo 4 native pause getter rejects any fixed-byte drift");

        Check(Halo4EffectDescriptorIsLocalFirstPerson(0x01) && Halo4EffectDescriptorIsLocalFirstPerson(0xA3) &&
                  !Halo4EffectDescriptorIsLocalFirstPerson(0x00) && !Halo4EffectDescriptorIsLocalFirstPerson(0xF1),
            "Halo 4 C50 effect admission keeps only local first-person descriptor families");

        Halo4HudAffine affine{};
        Check(Halo4ComputeNativeHudAffine(0.43f, 1.22f, 16.0f, affine) &&
                  std::fabs(affine.vertical - 0.43f) < 0.0001f && std::fabs(affine.horizontal - 0.5246f) < 0.0001f &&
                  std::fabs(affine.heightPixels - 16.0f) < 0.0001f,
            "Halo 4 native HUD affine matches Stage 3X size/aspect/height semantics");

        Check(Halo4NativeHudAdmitsCuiRoot(1, false, false) && Halo4NativeHudAdmitsCuiRoot(2, false, false) &&
                  !Halo4NativeHudAdmitsCuiRoot(0, false, false) && !Halo4NativeHudAdmitsCuiRoot(1, true, false) &&
                  !Halo4NativeHudAdmitsCuiRoot(1, false, true),
            "Halo 4 native HUD admits the complete frontend for visor siblings while private reticle replay and pause stay stock");

        Check(halo4_helmet_shader::IsVisorFramingShader(0x4BE62AC49C2BF210ULL) &&
                  !halo4_helmet_shader::IsVisorFramingShader(0x4BE62AC49C2BF211ULL),
            "Halo 4 helmet toggle admits only the headset-proven V6 visor shader hash");
        Check(halo4_helmet_shader::ShouldSuppress(true, true, false, true) &&
                  !halo4_helmet_shader::ShouldSuppress(true, true, true, true) &&
                  !halo4_helmet_shader::ShouldSuppress(true, false, false, true) &&
                  !halo4_helmet_shader::ShouldSuppress(false, true, false, true) &&
                  !halo4_helmet_shader::ShouldSuppress(true, true, false, false),
            "Halo 4 suppresses only the exact visor shader when the live checkbox is off");

        Check(halo4_screen_effect_shader::IsMotionSuckShader(0x47668A1953271934ULL) &&
                  !halo4_screen_effect_shader::IsMotionSuckShader(0x47668A1953271935ULL),
            "Halo 4 screen-effect bridge admits only the byte-proven H4EK/retail motion-suck shader");
        Check(halo4_screen_effect_shader::ShouldSuppress(true, true, true, true) &&
                  !halo4_screen_effect_shader::ShouldSuppress(false, true, true, true) &&
                  !halo4_screen_effect_shader::ShouldSuppress(true, false, true, true) &&
                  !halo4_screen_effect_shader::ShouldSuppress(true, true, false, true) &&
                  !halo4_screen_effect_shader::ShouldSuppress(true, true, true, false),
            "Halo 4 motion-suck suppression is exact, feature-local, title-local, and stereo-only");
    }

    CheckUnit(74);
    Halo4CuiReticleInstallProof halo4CuiInstall{};
    halo4CuiInstall.transformLayoutProven = true;
    halo4CuiInstall.anchorsMatchedOnce = kHalo4CuiReticleAnchorCount;
    halo4CuiInstall.anchorsAtPinnedRva = kHalo4CuiReticleAnchorCount;
    halo4CuiInstall.callerDecodesDispatcher = true;
    halo4CuiInstall.gameplayCallerDecodesRender = true;
    halo4CuiInstall.executableRange = true;
    halo4CuiInstall.mappingStable = true;
    Check(Halo4CuiReticleInstallComplete(halo4CuiInstall),
        "Complete transform, signatures, edge decode, and mapping admit the "
        "optional Halo 4 CUI reticle transaction");
    CheckUnit(75);
    {
        Halo4CuiReticleInstallProof broken = halo4CuiInstall;
        broken.transformLayoutProven = false;
        Check(
            !Halo4CuiReticleInstallComplete(broken), "Halo 4 CUI movement stays stock without the proven stack layout");
        broken = halo4CuiInstall;
        broken.anchorsMatchedOnce -= 1;
        Check(
            !Halo4CuiReticleInstallComplete(broken), "A missing or duplicated Halo 4 CUI signature refuses both hooks");
        broken = halo4CuiInstall;
        broken.anchorsAtPinnedRva -= 1;
        Check(!Halo4CuiReticleInstallComplete(broken), "A moved Halo 4 CUI signature refuses both hooks");
        broken = halo4CuiInstall;
        broken.callerDecodesDispatcher = false;
        Check(
            !Halo4CuiReticleInstallComplete(broken), "A Halo 4 CUI caller that misses the dispatcher refuses the hook");
        broken = halo4CuiInstall;
        broken.gameplayCallerDecodesRender = false;
        Check(!Halo4CuiReticleInstallComplete(broken), "A Halo 4 gameplay caller that misses the CUI front end refuses "
                                                       "both optional hooks");
        broken = halo4CuiInstall;
        broken.executableRange = false;
        Check(!Halo4CuiReticleInstallComplete(broken),
            "A Halo 4 CUI site outside executable image memory refuses the hook");
        broken = halo4CuiInstall;
        broken.mappingStable = false;
        Check(!Halo4CuiReticleInstallComplete(broken), "A remapped halo4.dll refuses only the CUI reticle transaction");
    }

    CheckUnit(76);
    const auto cuiInstalled =
        Halo4CuiReticleLifecycleFor(Halo4CuiReticleOptionalInstallState::Installed, halo4CuiInstall);
    Check(cuiInstalled.nativeTransformLive && !cuiInstalled.cleanupFeature,
        "Native Halo 4 CUI movement becomes live after a complete install");
    CheckUnit(77);
    {
        Halo4CuiReticleInstallProof incomplete = halo4CuiInstall;
        incomplete.mappingStable = false;
        Check(!Halo4CuiReticleLifecycleFor(Halo4CuiReticleOptionalInstallState::Installed, incomplete)
                      .nativeTransformLive &&
                  !Halo4CuiReticleLifecycleFor(Halo4CuiReticleOptionalInstallState::StockFallback, halo4CuiInstall)
                      .nativeTransformLive &&
                  !Halo4CuiReticleLifecycleFor(Halo4CuiReticleOptionalInstallState::CleanupRequired, halo4CuiInstall)
                      .nativeTransformLive,
            "Native Halo 4 CUI movement is never live before complete optional "
            "installation");
    }
    CheckUnit(78);
    Check(Halo4CuiReticleLifecycleFor(Halo4CuiReticleOptionalInstallState::CleanupRequired, halo4CuiInstall)
                  .cleanupFeature &&
              !Halo4CuiReticleLifecycleFor(Halo4CuiReticleOptionalInstallState::StockFallback, halo4CuiInstall)
                  .cleanupFeature,
        "A partial Halo 4 CUI install requests feature-local cleanup only");
    Check(Halo4CuiReticleOwnsNativeSuppression(true, true) && !Halo4CuiReticleOwnsNativeSuppression(true, false) &&
              !Halo4CuiReticleOwnsNativeSuppression(false, true) &&
              Halo4DecideCuiReticleAction(true, Halo4CuiReticleOwnsNativeSuppression(true, false), true,
                  kHalo4CuiCommandBegin, true, true, 0, false) == Halo4CuiReticleAction::DrawStock,
        "A fatal Halo 4 reticle-resource failure makes both optional hooks "
        "pass through to the stock CUI reticle without touching camera/OpenXR");
    CheckUnit(79);
    for (const Halo4CuiReticleOptionalInstallState state : {Halo4CuiReticleOptionalInstallState::StockFallback,
             Halo4CuiReticleOptionalInstallState::CleanupRequired, Halo4CuiReticleOptionalInstallState::Installed})
    {
        const auto lifecycle = Halo4CuiReticleLifecycleFor(state, halo4CuiInstall);
        Check(!lifecycle.disarmCameraCore && !lifecycle.endOpenXrSession,
            "No optional Halo 4 CUI install state disarms camera or OpenXR");
    }
    CheckUnit(80);
    Check(Halo4CuiReticleNeedsProceduralBootstrap(true, false, true, true) &&
              !Halo4CuiReticleNeedsProceduralBootstrap(true, true, true, true) &&
              !Halo4CuiReticleNeedsProceduralBootstrap(false, false, true, true) &&
              !Halo4CuiReticleNeedsProceduralBootstrap(true, false, true, false) &&
              !Halo4CuiReticleNeedsProceduralBootstrap(true, false, false, true),
        "Halo 4 keeps procedural gun-ray pixels only while a live authored "
        "hook is waiting for its first validated image");
    Check(Halo4CuiReticleUsesProceduralFallback(true, true, true) &&
              !Halo4CuiReticleUsesProceduralFallback(false, true, true) &&
              !Halo4CuiReticleUsesProceduralFallback(true, false, true) &&
              !Halo4CuiReticleUsesProceduralFallback(true, true, false),
        "Halo 4 fail-closes to the procedural bullet-ray reticle whenever "
        "the authored hook is live and native-reticle suppression is enabled");

    Check(Halo4CuiReticleUsesProceduralFallback(true, true, true) &&
              Halo4CuiReticleNeedsProceduralBootstrap(true, false, true, true) &&
              !Halo4CuiReticleNeedsProceduralBootstrap(true, true, true, true) &&
              !AuthoredReticleLayerHasContent(true, false) && AuthoredReticleLayerHasContent(true, true),
        "Stage 3BU's raw procedural request is effective only during authored "
        "bootstrap; validated held Halo 4 art replaces it");

    Check(kHalo4CuiCommandBegin == 0x28 && kHalo4CuiCommandEnd == 0x29 && kHalo4CuiCommandPolyart == 0x20 &&
              kHalo4CuiCommandPolyartThreeColor == 0x1F && kHalo4CuiCommandBeginPayloadSize == 0x0C,
        "Stage 3BP keeps the evidence-backed Halo 4 CUI selector commands");
    CheckUnit(81);
    {
        Halo4CuiCaptureSelectionState selection{};
        Check(!Halo4CuiCaptureKeepsTopTransform(selection) &&
                  !Halo4CuiCaptureMarkPolyartBeforeDraw(selection, true, kHalo4CuiCommandPolyart),
            "Stage 3BP ignores Polyart outside the reticle container");
        Halo4CuiCaptureAdvanceAfterDraw(selection, false, kHalo4CuiCommandBegin, kHalo4CuiCommandBeginPayloadSize);
        Halo4CuiCaptureAdvanceAfterDraw(selection, true, kHalo4CuiCommandBegin, kHalo4CuiCommandBeginPayloadSize - 1);
        Check(!selection.insideReticleContainer && !selection.polyartContainer,
            "Stage 3BP admits only a readable 0x28/0x0C reticle container");
        Halo4CuiCaptureAdvanceAfterDraw(selection, true, kHalo4CuiCommandBegin, kHalo4CuiCommandBeginPayloadSize);
        Check(selection.insideReticleContainer && !selection.polyartContainer &&
                  Halo4CuiCaptureKeepsTopTransform(selection) &&
                  !Halo4CuiCaptureMarkPolyartBeforeDraw(selection, true, 0x0B),
            "Stage 3BP keeps bitmap weapon art on target inside the selected "
            "reticle container");
        Check(Halo4CuiCaptureMarkPolyartBeforeDraw(selection, true, kHalo4CuiCommandPolyart) &&
                  selection.polyartContainer && !Halo4CuiCaptureKeepsTopTransform(selection),
            "Stage 3BP moves 0x20 Polyart indicators out of the authored "
            "weapon-reticle capture");
        Halo4CuiCaptureAdvanceAfterDraw(selection, true, kHalo4CuiCommandEnd, 0);
        Check(!selection.insideReticleContainer && !selection.polyartContainer,
            "Stage 3BP clears both selector flags at the matching CUI end");
        Halo4CuiCaptureAdvanceAfterDraw(selection, true, kHalo4CuiCommandBegin, kHalo4CuiCommandBeginPayloadSize);
        Check(Halo4CuiCaptureMarkPolyartBeforeDraw(selection, true, kHalo4CuiCommandPolyartThreeColor) &&
                  !Halo4CuiCaptureKeepsTopTransform(selection),
            "Stage 3BP applies the same exclusion to 0x1F three-colour "
            "Polyart indicators");
    }
    CheckUnit(82);
    {
        const float threshold = kHalo4CuiCaptureWidth * 0.5f;
        float adjusted = 0.0f;
        Check(Halo4CuiCaptureAdjustedTranslationX(0.0f, false, adjusted) && std::fabs(adjusted - 4134.312f) < 0.001f,
            "Stage 3BP shifts an unwanted on-screen child one measured CUI "
            "capture width off screen");
        Check(
            Halo4CuiCaptureAdjustedTranslationX(adjusted, false, adjusted) && std::fabs(adjusted - 4134.312f) < 0.001f,
            "Stage 3BP translation is idempotent for an already hidden child");
        Check(Halo4CuiCaptureAdjustedTranslationX(kHalo4CuiCaptureWidth, true, adjusted) &&
                  std::fabs(adjusted) < 0.001f && Halo4CuiCaptureAdjustedTranslationX(0.0f, true, adjusted) &&
                  std::fabs(adjusted) < 0.001f,
            "Stage 3BP restores selected weapon art exactly once and leaves "
            "already-visible art fixed");
        Check(Halo4CuiCaptureAdjustedTranslationX(threshold, true, adjusted) &&
                  std::fabs(adjusted + threshold) < 0.001f &&
                  Halo4CuiCaptureAdjustedTranslationX(threshold, false, adjusted) &&
                  std::fabs(adjusted - threshold) < 0.001f,
            "Stage 3BP owns the measured half-width boundary without "
            "oscillation");
        Check(!Halo4CuiCaptureAdjustedTranslationX(std::numeric_limits<float>::quiet_NaN(), true, adjusted),
            "Stage 3BP rejects a non-finite CUI translation");
    }
    CheckUnit(83);
    Check(std::fabs(kHalo4CuiCaptureWidth - 4134.312f) < 0.001f &&
              std::fabs(kHalo4CuiCaptureHeight - 1346.196f) < 0.001f &&
              std::fabs(kHalo4CuiCaptureTextureExtent - 512.0f) < 0.001f &&
              std::fabs(kHalo4CuiCaptureCenter - 256.0f) < 0.001f &&
              std::fabs(kHalo4CuiCaptureVerticalBias - 104.0f) < 0.001f &&
              std::fabs(kHalo4CuiCaptureScale - 2.5f) < 0.001f,
        "Stages 3BN and 3BR preserve Halo 4's measured capture framing");
    CheckUnit(84);
    {
        const Halo4CuiCaptureViewport base{56.0f, 10.0f, 400.0f, 200.0f, 0.125f, 0.875f};
        Halo4CuiCaptureViewport viewport{};
        Check(Halo4BuildCuiCaptureDrawViewport(base, 4, 3, viewport) &&
                  std::fabs(viewport.topLeftX - (-244.0f)) < 0.001f &&
                  std::fabs(viewport.topLeftY - (-176.941f)) < 0.002f && std::fabs(viewport.width - 1000.0f) < 0.001f &&
                  std::fabs(viewport.height - 750.0f) < 0.001f && std::fabs(viewport.minDepth - 0.125f) < 0.001f &&
                  std::fabs(viewport.maxDepth - 0.875f) < 0.001f && std::fabs(base.topLeftX - 56.0f) < 0.001f &&
                  std::fabs(base.topLeftY - 10.0f) < 0.001f && std::fabs(base.width - 400.0f) < 0.001f &&
                  std::fabs(base.height - 200.0f) < 0.001f,
            "Stages 3BN and 3BR derive aspect-correct 2.5x Halo 4 capture "
            "framing without mutating the base viewport");
        Check(Halo4BuildCuiCaptureDrawViewport(base, 0, 3, viewport) &&
                  std::fabs(viewport.topLeftX - (-244.0f)) < 0.001f &&
                  std::fabs(viewport.topLeftY - (-619.0f)) < 0.001f && std::fabs(viewport.width - 1000.0f) < 0.001f &&
                  std::fabs(viewport.height - 500.0f) < 0.001f,
            "Stage 3BN preserves the measured 104-pixel fallback bias when "
            "backbuffer aspect is unavailable");
        Halo4CuiCaptureViewport invalid = base;
        invalid.width = 0.0f;
        Check(!Halo4BuildCuiCaptureDrawViewport(invalid, 4, 3, viewport),
            "Halo 4 capture framing rejects a zero-width viewport");
        invalid = base;
        invalid.topLeftY = std::numeric_limits<float>::quiet_NaN();
        Check(!Halo4BuildCuiCaptureDrawViewport(invalid, 4, 3, viewport),
            "Halo 4 capture framing rejects non-finite viewport input");
    }
    CheckUnit(85);
    {
        // Capture-dump check: the live bias law hits every capture-dump
        // anchor the capture builder pinned, with the builder's own tolerances.
        Check(std::fabs(Halo4CuiLiveVerticalBiasRatio(336.549f) - kHalo4CuiCaptureVerticalBiasRatio) < 2e-5f,
            "3CX live bias reproduces the calibrated layout bit-tight");
        Check(std::fabs(Halo4CuiLiveVerticalBiasRatio(731.878f) - 0.003274f) < 2e-5f,
            "3CX live bias hits the 731.878 capture-dump anchor");
        Check(std::fabs(Halo4CuiLiveVerticalBiasRatio(582.373f) - 0.006040f) < 2e-5f,
            "3CX live bias hits the 582.373 capture-dump anchor");
        float previous = std::numeric_limits<float>::infinity();
        bool monotone = true;
        for (int step = 0; step < 80; ++step)
        {
            const float y = 336.549f + (731.878f - 336.549f) * static_cast<float>(step) / 79.0f;
            const float f = Halo4CuiLiveVerticalBiasRatio(y);
            if (f > previous + 1e-4f)
                monotone = false;
            previous = f;
        }
        Check(monotone, "3CX live bias is monotone decreasing across the observed "
                        "layout range (sub-pixel tolerance)");
        Check(Halo4CuiLiveVerticalBiasRatio(300.0f) <= kHalo4CuiCaptureVerticalBiasRatio &&
                  Halo4CuiLiveVerticalBiasRatio(100000.0f) >= 0.0f,
            "3CX live bias clamps to [0, calibrated] outside the anchors");
        Check(Halo4CuiLiveVerticalBiasRatio(0.0f) == kHalo4CuiCaptureVerticalBiasRatio &&
                  Halo4CuiLiveVerticalBiasRatio(-582.373f) == kHalo4CuiCaptureVerticalBiasRatio &&
                  Halo4CuiLiveVerticalBiasRatio(std::numeric_limits<float>::quiet_NaN()) ==
                      kHalo4CuiCaptureVerticalBiasRatio,
            "3CX live bias keeps the calibrated ratio for an invalid live "
            "baseY, exactly like the payload skipping its refresh");
        Check(std::fabs(Halo4CuiLiveCaptureScale(4134.312f) - 2.5f) < 1e-4f,
            "3CX live zoom reproduces the accepted 2.5x on the calibrated "
            "canvas");
        Check(std::fabs(Halo4CuiLiveCaptureScale(6543.256f) - 2.5f * 185.0f / 286.0f) < 1e-4f,
            "3CX live zoom hits the measured 6543.256 size anchor");
        Check(Halo4CuiLiveCaptureScale(0.0f) == kHalo4CuiCaptureScale &&
                  Halo4CuiLiveCaptureScale(-1.0f) == kHalo4CuiCaptureScale &&
                  Halo4CuiLiveCaptureScale(std::numeric_limits<float>::quiet_NaN()) == kHalo4CuiCaptureScale,
            "3CX live zoom keeps the calibrated 2.5x for an invalid live "
            "hide");
    }
    CheckUnit(86);
    {
        // The selector un-hides by the LIVE hide shift.
        // On the 6543.256 canvas the hidden reticle sits at 3*halfWidth =
        // 4907.442; a frozen 4134.312 left it outside the window.
        const float liveHide = 6543.256f;
        float adjusted = 0.0f;
        Check(Halo4CuiCaptureAdjustedTranslationX(4907.442f, true, liveHide, adjusted) &&
                  std::fabs(adjusted - (-1635.814f)) < 0.01f,
            "3CR restores the hidden reticle to its stock -halfWidth on a "
            "live canvas");
        Check(Halo4CuiCaptureAdjustedTranslationX(-1635.814f, false, liveHide, adjusted) &&
                  std::fabs(adjusted - 4907.442f) < 0.01f,
            "3CR pushes an unwanted visible child off screen by the live "
            "hide shift");
        Check(Halo4CuiCaptureAdjustedTranslationX(kHalo4CuiCaptureWidth, true, 0.0f, adjusted) &&
                  std::fabs(adjusted) < 0.001f,
            "3CR falls back to the calibrated width when no hide is live");
    }

    CheckUnit(87);
    using CuiAction = Halo4CuiReticleAction;
    Check(Halo4DecideCuiReticleAction(false, true, true, kHalo4CuiCommandBegin, true, true, 0, false) ==
              CuiAction::DrawStock,
        "Halo 4 CUI work outside VR ownership remains stock");
    Check(Halo4DecideCuiReticleAction(true, false, true, kHalo4CuiCommandBegin, true, true, 0, false) ==
              CuiAction::DrawStock,
        "An uninstalled Halo 4 CUI transform path remains stock");
    Check(Halo4DecideCuiReticleAction(true, true, true, kHalo4CuiCommandEnd, true, true, 0, false) ==
                  CuiAction::DrawStock &&
              Halo4DecideCuiReticleAction(true, true, true, 0x2A, true, true, 0, false) == CuiAction::DrawStock,
        "Only Halo 4's CUI begin command can move the reticle transform");
    Check(Halo4DecideCuiReticleAction(true, true, false, kHalo4CuiCommandBegin, true, true, 0, false) ==
              CuiAction::DrawStock,
        "An unreadable Halo 4 CUI command fails open to stock");
    Check(Halo4DecideCuiReticleAction(true, true, true, kHalo4CuiCommandBegin, true, true, -1, false) ==
                  CuiAction::DrawStock &&
              Halo4DecideCuiReticleAction(true, true, true, kHalo4CuiCommandBegin, true, true, 2, false) ==
                  CuiAction::DrawStock,
        "An invalid Halo 4 stereo eye fails open to stock");
    Check(Halo4DecideCuiReticleAction(true, true, true, kHalo4CuiCommandBegin, false, true, 0, false) ==
              CuiAction::HideNative,
        "crosshair=0 suppresses Halo 4's native CUI reticle");
    Check(Halo4DecideCuiReticleAction(true, true, true, kHalo4CuiCommandBegin, true, false, 0, false) ==
              CuiAction::DrawStock,
        "kill_reticle=0 preserves Halo 4's native CUI reticle");
    Check(Halo4DecideCuiReticleAction(true, true, true, kHalo4CuiCommandBegin, true, true, 0, false) ==
                  CuiAction::HideNative &&
              Halo4DecideCuiReticleAction(true, true, true, kHalo4CuiCommandBegin, true, true, 1, false) ==
                  CuiAction::HideNative &&
              Halo4DecideCuiReticleAction(true, true, true, kHalo4CuiCommandBegin, true, true, 1, true) ==
                  CuiAction::HideNative &&
              Halo4DecideCuiReticleAction(true, true, true, kHalo4CuiCommandBegin, true, true, 0, true) ==
                  CuiAction::HideNative,
        "Both Halo 4 eyes hide the native flat copy while authored pixels ride "
        "the exact OpenXR reticle quad; eye order cannot create a duplicate");

    CheckUnit(88);
    {
        const float forward[3] = {0.0f, 1.0f, 0.0f};
        const float up[3] = {0.0f, 0.0f, 1.0f};
        const float centerAim[3] = {0.0f, 1.0f, 0.0f};
        const Halo4CuiAimOffset center = Halo4ProjectAimToCuiOffset(forward, up, centerAim, 0.78539816f, 0.78539816f);
        Check(center.valid && fabsf(center.x) < 1.0e-6f && fabsf(center.y) < 1.0e-6f,
            "A camera-forward Halo 4 gun ray leaves the native reticle centred");
        const float rightAim[3] = {0.70710678f, 0.70710678f, 0.0f};
        const Halo4CuiAimOffset right = Halo4ProjectAimToCuiOffset(forward, up, rightAim, 0.78539816f, 0.78539816f);
        Check(right.valid && fabsf(right.x - 1.0f) < 1.0e-5f && fabsf(right.y) < 1.0e-5f,
            "The native Halo 4 reticle projection reaches the right frustum edge");
        const float upAim[3] = {0.0f, 0.70710678f, 0.70710678f};
        const Halo4CuiAimOffset upOffset = Halo4ProjectAimToCuiOffset(forward, up, upAim, 0.78539816f, 0.78539816f);
        Check(upOffset.valid && fabsf(upOffset.x) < 1.0e-5f && fabsf(upOffset.y - 1.0f) < 1.0e-5f,
            "Camera-up maps to positive Halo 4 reticle-matrix translation Y");
        const float behind[3] = {0.0f, -1.0f, 0.0f};
        Check(!Halo4ProjectAimToCuiOffset(forward, up, behind, 0.78539816f, 0.78539816f).valid,
            "A behind-camera Halo 4 gun ray never writes a reticle matrix");

        const Halo4CuiAimOffset rightPixels = Halo4MapAimToCuiTranslation(right, -1920.0f, 1080.0f, false);
        const Halo4CuiAimOffset upPixels = Halo4MapAimToCuiTranslation(upOffset, -1920.0f, 1080.0f, false);
        const Halo4CuiAimOffset hidden = Halo4MapAimToCuiTranslation(center, -1920.0f, 1080.0f, true);
        const Halo4CuiAimOffset directHidden = Halo4BuildHiddenCuiTranslation(-1920.0f, 1080.0f);
        Check(rightPixels.valid && fabsf(rightPixels.x - 1920.0f) < 1.0e-4f && fabsf(rightPixels.y) < 1.0e-4f &&
                  upPixels.valid && fabsf(upPixels.x) < 1.0e-4f && fabsf(upPixels.y - 1080.0f) < 1.0e-4f,
            "Halo 4 converts normalized gun-ray projection through the live "
            "pixel-space CUI half extents");
        Check(hidden.valid && fabsf(hidden.x - 7680.0f) < 1.0e-4f && fabsf(hidden.y) < 1.0e-4f,
            "crosshair=0 moves Halo 4's native reticle fully offscreen in its "
            "own CUI coordinate system");
        Check(directHidden.valid && fabsf(directHidden.x - 7680.0f) < 1.0e-4f && fabsf(directHidden.y) < 1.0e-4f &&
                  !Halo4BuildHiddenCuiTranslation(NAN, 1080.0f).valid,
            "Halo 4 native-copy hiding is independent of every aim coordinate");
        float captureBaseY = 0.0f;
        float captureHide = 0.0f;
        Check(Halo4SelectCuiCaptureCanvas(-1456.0f, 818.772f, -763.818f, 336.072f, captureBaseY, captureHide) &&
                  fabsf(captureBaseY - 818.772f) < 1.0e-4f && fabsf(captureHide - 5824.0f) < 1.0e-4f,
            "Halo 4 private reticle capture frames from its stock replay canvas instead of the scaled visible HUD canvas");
        Check(Halo4SelectCuiCaptureCanvas(0.0f, 0.0f, -763.818f, 336.072f, captureBaseY, captureHide) &&
                  fabsf(captureBaseY - 336.072f) < 1.0e-4f && fabsf(captureHide - 3055.272f) < 1.0e-3f,
            "Halo 4 capture keeps the accepted visible-canvas fallback before its first replay marker");
        Check(!Halo4SelectCuiCaptureCanvas(NAN, 0.0f, 0.0f, NAN, captureBaseY, captureHide),
            "Halo 4 capture canvas rejects invalid replay and visible samples without affecting camera VR");
        Check(!Halo4MapAimToCuiTranslation(right, 0.0f, 1080.0f, false).valid &&
                  !Halo4MapAimToCuiTranslation(right, -1920.0f, NAN, false).valid,
            "Invalid Halo 4 CUI extents fail open without a transform write");

        float angularScale = 0.0f;
        Check(Halo4MapAngularSizeToCuiScale(1080.0f, 0.78539816f, 7.2f, angularScale),
            "Halo 4 maps the shared crosshair angular-size knob into its "
            "title-native CUI scale");
        const float reconstructedRadians =
            2.0f * atanf((kHalo4CuiNominalReticleHeight * angularScale * 0.5f / 1080.0f) * tanf(0.78539816f));
        Check(fabsf(reconstructedRadians - 7.2f * 0.01745329252f) < 1.0e-5f,
            "Halo 4's nominal authored reticle height subtends exactly the "
            "configured angle");
        Check(!Halo4MapAngularSizeToCuiScale(0.0f, 0.78539816f, 7.2f, angularScale) &&
                  !Halo4MapAngularSizeToCuiScale(1080.0f, 0.0f, 7.2f, angularScale) &&
                  !Halo4MapAngularSizeToCuiScale(1080.0f, 0.78539816f, NAN, angularScale),
            "Invalid Halo 4 size inputs fail feature-locally");

        const float centerPosition[3] = {0.0f, 0.0f, 0.0f};
        const float leftEyePosition[3] = {-0.032f, 0.0f, 0.0f};
        const float engineForward[3] = {0.0f, 2.0f, 0.0f};
        float leftEyeRay[3]{};
        Check(Halo4BuildReticleEyeRay(centerPosition, leftEyePosition, engineForward, 2.0f, leftEyeRay) &&
                  fabsf(leftEyeRay[0] - 0.032f) < 1.0e-6f && fabsf(leftEyeRay[1] - 2.0f) < 1.0e-6f &&
                  fabsf(leftEyeRay[2]) < 1.0e-6f,
            "Halo 4 projects each eye to one finite gun-ray target so the "
            "native CUI reticle retains the VR crosshair's stereo depth");
        Check(!Halo4BuildReticleEyeRay(centerPosition, leftEyePosition, engineForward, 0.0f, leftEyeRay),
            "An invalid Halo 4 reticle range cannot write an eye projection");
        Check(Halo4CuiReticlePairPositionsNative(77, 77, 77, 77) &&
                  !Halo4CuiReticlePairPositionsNative(0, 77, 77, 77) &&
                  !Halo4CuiReticlePairPositionsNative(76, 77, 77, 77) &&
                  !Halo4CuiReticlePairPositionsNative(77, 77, 78, 78),
            "The procedural VR fallback is suppressed only after both Halo 4 "
            "eyes moved native reticle art for the exact rendered serial");
    }

    CheckUnit(89);
    // Generic cover math remains available for a later FOV milestone, but it
    // is not an observer-field layout claim: +0x78/+0x7C are full-vFOV and a
    // reference-FOV ratio, not tangents.
    {
        float tanX = 0.0f;
        float tanY = 0.0f;
        const float asymmetric[4] = {-0.9f, 0.7f, 0.8f, -1.0f};
        Check(Halo4SymmetricCoverFromFov(asymmetric, tanX, tanY) && fabsf(tanX - tanf(0.9f)) < 1e-5f &&
                  fabsf(tanY - tanf(1.0f)) < 1e-5f,
            "The dormant OpenXR cover helper takes the wider side of each axis");
        const float degenerate[4] = {0.0f, 0.0f, 0.0f, 0.0f};
        Check(!Halo4SymmetricCoverFromFov(degenerate, tanX, tanY),
            "A zero OpenXR FOV is refused by the generic cover helper");
        const float absurd[4] = {-1.6f, 1.6f, 1.6f, -1.6f};
        Check(!Halo4SymmetricCoverFromFov(absurd, tanX, tanY), "A past-90-degree half angle is refused");
    }

    CheckUnit(90);
    // Per-eye displacement changes pose only. Halo 4's stock full vertical FOV
    // and FOV-ratio fields are carried through byte-for-byte.
    {
        Halo4CameraBasis mono{};
        mono.forward[0] = 1.0f; // +X forward
        mono.up[2] = 1.0f;      // +Z up  => right = forward x up = -Y
        mono.position[0] = 10.0f;
        mono.verticalFov = 1.4f;
        mono.fovRatio = 1.125f;
        Check(Halo4ValidateCameraBasis(mono), "A unit Halo 4 camera basis validates");

        const float eyePosition[3] = {0.032f, 0.0f, 0.0f}; // 32 mm right
        Halo4CameraBasis eye{};
        Check(Halo4BuildEyeCamera(mono, eyePosition, nullptr, 0.33f, eye),
            "Halo 4 builds an eye camera from a valid basis");
        Check(fabsf(eye.position[0] - 10.0f) < 1e-6f && fabsf(eye.position[1] - (-0.032f * 0.33f)) < 1e-6f &&
                  fabsf(eye.position[2]) < 1e-6f,
            "The Halo 4 eye offset lands on the camera's own right axis, "
            "scaled to world units");
        Check(memcmp(&eye.verticalFov, &mono.verticalFov, sizeof(float)) == 0 &&
                  memcmp(&eye.fovRatio, &mono.fovRatio, sizeof(float)) == 0,
            "Eye separation preserves Halo 4's stock FOV fields bit-for-bit");
        Check(fabsf(eye.forward[0] - 1.0f) < 1e-6f && fabsf(eye.up[2] - 1.0f) < 1e-6f,
            "A null Halo 4 eye orientation leaves the basis uncanted");

        // A +90 degree yaw about the eye's own up axis turns the view LEFT.
        // The camera's right axis here is -Y, so left is +Y: forward must land
        // on +Y and up must be unchanged. This is what proves the cant is
        // applied in the camera's basis rather than in world axes.
        const float quarterTurnAboutUp[4] = {0.0f, 0.70710678f, 0.0f, 0.70710678f};
        Halo4CameraBasis canted{};
        Check(Halo4BuildEyeCamera(mono, eyePosition, quarterTurnAboutUp, 0.33f, canted) &&
                  fabsf(canted.forward[1] - 1.0f) < 1e-5f && fabsf(canted.forward[0]) < 1e-5f &&
                  fabsf(canted.up[2] - 1.0f) < 1e-5f,
            "A Halo 4 eye cant rotates forward about the camera's own up axis");

        // Fail-closed inputs.
        Halo4CameraBasis rejected{};
        Halo4CameraBasis degenerate = mono;
        degenerate.forward[0] = 0.0f;
        Check(!Halo4ValidateCameraBasis(degenerate) &&
                  !Halo4BuildEyeCamera(degenerate, eyePosition, nullptr, 0.33f, rejected),
            "A zero-length Halo 4 forward vector is refused");
        Halo4CameraBasis infinite = mono;
        infinite.position[1] = std::numeric_limits<float>::infinity();
        Check(!Halo4ValidateCameraBasis(infinite), "A non-finite Halo 4 camera position is refused");
        Halo4CameraBasis parallel = mono;
        memcpy(parallel.up, parallel.forward, sizeof(parallel.up));
        Check(!Halo4ValidateCameraBasis(parallel), "Parallel Halo 4 forward/up vectors are refused before eye "
                                                   "separation can collapse");
        const float invalidOrientation[4] = {std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f, 1.0f};
        Check(!Halo4BuildEyeCamera(mono, eyePosition, nullptr, 0.0f, rejected) &&
                  !Halo4BuildEyeCamera(mono, eyePosition, invalidOrientation, 0.33f, rejected),
            "A zero world scale or invalid eye orientation is refused");

        Halo4CameraBasis extremeRatio = mono;
        extremeRatio.fovRatio = 100.0f;
        Check(Halo4ValidateCameraBasis(extremeRatio), "The evidence-backed camera gate does not reject a legal wide "
                                                      "reference-FOV ratio");
        extremeRatio.fovRatio = -1.0f;
        Check(!Halo4ValidateCameraBasis(extremeRatio), "A negative Halo 4 reference-FOV ratio is refused");
    }

    CheckUnit(91);
    // The engine-held camera result and finished projection are the acceptance
    // proof. A material center term is rejected until the compositor can carry
    // an off-axis H4 raster exactly.
    {
        Halo4CameraBasis requested{};
        requested.position[0] = 10.0f;
        requested.forward[0] = 1.0f;
        requested.up[2] = 1.0f;
        requested.verticalFov = 1.4f;
        requested.fovRatio = 1.125f;
        Check(Halo4CameraOutputMatches(requested, requested.position, requested.forward, requested.up),
            "An exact Halo 4 setup readback proves the requested camera took");
        float movedPosition[3] = {10.0f, 0.0f, 0.0f};
        movedPosition[0] = std::nextafter(movedPosition[0], 11.0f);
        Check(!Halo4CameraOutputMatches(requested, movedPosition, requested.forward, requested.up),
            "Even a one-ULP engine-held camera difference fails the exact "
            "claim proof");

        float projection[16]{};
        projection[0] = 1.0f / tanf(0.7f);
        projection[5] = 1.0f / tanf(0.6f);
        projection[11] = -1.0f;
        float halfX = 0.0f, halfY = 0.0f, centerX = 0.0f, centerY = 0.0f;
        Check(Halo4DecodeSymmetricProjectionHalfFovs(projection, halfX, halfY, centerX, centerY) &&
                  fabsf(halfX - 0.7f) < 1e-5f && fabsf(halfY - 0.6f) < 1e-5f && centerX == 0.0f && centerY == 0.0f,
            "Halo 4's finished symmetric row-vector matrix decodes exactly");
        projection[0] = 0.0f;
        Check(!Halo4DecodeSymmetricProjectionHalfFovs(projection, halfX, halfY, centerX, centerY),
            "A zero Halo 4 projection scale is refused");
        projection[0] = -1.0f / tanf(0.7f);
        Check(!Halo4DecodeSymmetricProjectionHalfFovs(projection, halfX, halfY, centerX, centerY),
            "A negative scale is refused because H4's proven normal projection "
            "writes positive X/Y scales");
        projection[0] = 1.0f / tanf(0.7f);
        projection[11] = 0.0f;
        Check(!Halo4DecodeSymmetricProjectionHalfFovs(projection, halfX, halfY, centerX, centerY),
            "A matrix without Halo 4's proven row-vector -1 is refused");
        projection[11] = -1.0f;
        projection[8] = std::numeric_limits<float>::epsilon();
        Check(!Halo4DecodeSymmetricProjectionHalfFovs(projection, halfX, halfY, centerX, centerY),
            "Any nonzero off-axis center is refused by the symmetric API");
        projection[8] = 0.0f;
        projection[5] = std::numeric_limits<float>::quiet_NaN();
        Check(!Halo4DecodeSymmetricProjectionHalfFovs(projection, halfX, halfY, centerX, centerY),
            "A non-finite Halo 4 projection scale is refused");
    }

    CheckUnit(92);
    // Model the observer substitution itself: every byte except the three
    // proven pose vectors must survive unchanged, especially +0x78/+0x7C.
    {
        unsigned char stockObserver[kHalo4ObserverSnapshotBytes];
        unsigned char eyeObserver[kHalo4ObserverSnapshotBytes];
        for (size_t i = 0; i < sizeof(stockObserver); ++i)
            stockObserver[i] = static_cast<unsigned char>(i ^ 0xA5u);
        memcpy(eyeObserver, stockObserver, sizeof(eyeObserver));
        const float replacement[3] = {1.0f, 2.0f, 3.0f};
        memcpy(eyeObserver + kHalo4ObserverPositionOffset, replacement, sizeof(replacement));
        memcpy(eyeObserver + kHalo4ObserverForwardOffset, replacement, sizeof(replacement));
        memcpy(eyeObserver + kHalo4ObserverUpOffset, replacement, sizeof(replacement));
        bool untouchedBytesMatch = true;
        for (size_t i = 0; i < sizeof(stockObserver); ++i)
        {
            const bool mutableByte = (i >= kHalo4ObserverPositionOffset && i < kHalo4ObserverPositionOffset + 12) ||
                                     (i >= kHalo4ObserverForwardOffset && i < kHalo4ObserverForwardOffset + 12) ||
                                     (i >= kHalo4ObserverUpOffset && i < kHalo4ObserverUpOffset + 12);
            if (!mutableByte && eyeObserver[i] != stockObserver[i])
                untouchedBytesMatch = false;
        }
        Check(untouchedBytesMatch &&
                  memcmp(eyeObserver + kHalo4ObserverVerticalFovOffset, stockObserver + kHalo4ObserverVerticalFovOffset,
                      sizeof(float)) == 0 &&
                  memcmp(eyeObserver + kHalo4ObserverFovRatioOffset, stockObserver + kHalo4ObserverFovRatioOffset,
                      sizeof(float)) == 0,
            "C-H4-7 changes only position/forward/up and preserves every FOV "
            "input byte");

        // The cover additionally writes the vertical FOV. The FOV RATIO at +0x7C
        // must still survive: retail's converter scales both fields by one
        // shared factor (halo4.dll 0x38F0A8/0x38F0AC), so writing both would
        // double-apply, and the field's true meaning is still unresolved.
        const float widerVerticalFov = 2.2f;
        memcpy(eyeObserver + kHalo4ObserverVerticalFovOffset, &widerVerticalFov, sizeof(widerVerticalFov));
        Check(memcmp(eyeObserver + kHalo4ObserverFovRatioOffset, stockObserver + kHalo4ObserverFovRatioOffset,
                  sizeof(float)) == 0,
            "C-H4-8 widens the vertical FOV input and still leaves the FOV "
            "ratio at +0x7C bit-identical");
    }

    CheckUnit(93);
    // The native headset-FOV cover. Nothing here may be
    // headset-specific - every number is derived from a supplied XrFovf.
    {
        // The user's PSVR2 as SteamVR/OpenXR reports it: asymmetric on X.
        const float psvr2[4] = {-1.07338f, 0.75747f, 0.92502f, -0.92502f};
        float tangentX = 0.0f;
        float tangentY = 0.0f;
        Check(Halo4RequiredCoverTangents(psvr2, tangentX, tangentY) && fabsf(tangentX - tanf(1.07338f)) < 1.0e-4f &&
                  fabsf(tangentY - tanf(0.92502f)) < 1.0e-4f,
            "The required cover takes the WIDER side of an asymmetric frustum, "
            "so a canted-panel headset is fully covered");

        const float badFov[4] = {0.1f, 0.75747f, 0.92502f, -0.92502f};
        Check(!Halo4RequiredCoverTangents(badFov, tangentX, tangentY),
            "A frustum with a non-negative left angle is refused rather than "
            "silently producing a cover");

        // Halo 4's measured stock cover does NOT contain that frustum - this is
        // exactly the headset fault found in testing, reproduced offline.
        Check(!Halo4CoverContainsFov(0.88072f, 0.71805f, psvr2),
            "Halo 4's stock 50.46/41.14 deg cover fails to contain the PSVR2 "
            "frustum, which is why the whole slice was submitted at the wrong "
            "FOV");

        // Learn the mapping from the engine's own measured response. The retail
        // converter scales observer +0x78 by 0.785 and the builder halves it,
        // so writing 1.8295 must build a 0.71805 rad half-Y.
        Halo4FovCalibration calibration{};
        Check(!calibration.learned, "The FOV calibration starts unlearned, so the first Halo 4 stereo "
                                    "frame of a generation renders at the engine's own stock FOV");
        float solved = 0.0f;
        float expectedHalfY = 0.0f;
        Check(!Halo4SolveCoverVerticalFov(psvr2, calibration, 1.01f, solved, expectedHalfY),
            "No cover may be solved before the engine's response has been "
            "measured");

        Check(Halo4LearnFovCalibration(1.8295f, 0.88072f, 0.71805f, calibration) && calibration.learned &&
                  fabsf(calibration.gain - 0.3925f) < 1.0e-3f,
            "The learned gain reproduces retail's proven 0.785/2 mapping from "
            "the engine's own finished projection");

        Check(Halo4SolveCoverVerticalFov(psvr2, calibration, 1.01f, solved, expectedHalfY),
            "A learned calibration solves the vertical FOV write");
        // The solved write, put back through the engine's proven mapping,
        // must produce a cover that contains the frustum on all four edges.
        const float rebuiltHalfY = solved * 0.785f / 2.0f;
        const float rebuiltHalfX = atanf(tanf(rebuiltHalfY) * calibration.ratio);
        Check(Halo4CoverContainsFov(rebuiltHalfX, rebuiltHalfY, psvr2),
            "The solved cover, replayed through retail's own 0.785 mapping, "
            "contains the headset frustum - the containment vr.cpp requires "
            "before it can crop to native FOV");

        // Headset-agnostic: a different, wider, symmetric headset must also be
        // covered without touching any constant.
        const float wideFov[4] = {-1.3f, 1.3f, 1.2f, -1.2f};
        float wideSolved = 0.0f;
        float wideExpected = 0.0f;
        Check(Halo4SolveCoverVerticalFov(wideFov, calibration, 1.01f, wideSolved, wideExpected) && wideSolved > solved,
            "A wider headset solves a wider cover from the same calibration, "
            "with no headset named anywhere in the code");
        const float wideHalfY = wideSolved * 0.785f / 2.0f;
        Check(Halo4CoverContainsFov(atanf(tanf(wideHalfY) * calibration.ratio), wideHalfY, wideFov),
            "The wider headset's solved cover also contains its own frustum");

        // A flipped engine scale branch must not distort the world: the gain is
        // re-learned from the readback rather than assumed.
        Halo4FovCalibration flipped{};
        Check(Halo4LearnFovCalibration(1.8295f, 0.30f, 0.1538f, flipped) && fabsf(flipped.gain - 0.0841f) < 1.0e-3f,
            "A changed engine FOV scale is absorbed by re-learning the gain, "
            "instead of silently rendering the wrong field of view");

        // End-to-end lock on the exact retail numbers, derived independently
        // from halo4.dll and the preserved logs: the engine's stock pair is
        // observer +0x78 = 1.8295 rad -> element +0x28 = 1.4361 rad (K = 0.785)
        // -> built half 50.46/41.14 deg, and the projection builder halves
        // element +0x28 before tan (0x38F4F7-0x38F501). Feeding the PSVR2
        // frustum through that chain must ask the engine for 2.3701 rad and
        // build 61.74/53.30 deg, covering the native 61.5/53.0 on both axes.
        Halo4FovCalibration retail{};
        Check(Halo4LearnFovCalibration(1.8295f, 0.88072f, 0.71805f, retail),
            "The retail stock pair teaches the calibration");
        float retailWrite = 0.0f;
        float retailHalfY = 0.0f;
        Check(Halo4SolveCoverVerticalFov(psvr2, retail, 1.01f, retailWrite, retailHalfY),
            "The retail calibration solves a PSVR2 cover");
        Check(fabsf(retailWrite - 2.3702f) < 5.0e-3f,
            "The solved observer +0x78 write matches the independently derived "
            "2.3702 rad for this headset and this engine mapping");
        const float builtHalfY = retailWrite * 0.785f / 2.0f;
        const float builtHalfX = atanf(tanf(builtHalfY) * retail.ratio);
        Check(fabsf(builtHalfY * 57.2958f - 53.30f) < 0.05f && fabsf(builtHalfX * 57.2958f - 61.74f) < 0.10f,
            "Replayed through retail's own 0.785 scale and half-then-tan "
            "builder, the write produces 61.74/53.30 deg");
        Check(builtHalfX > 1.07338f && builtHalfY > 0.92502f,
            "Both built half-angles exceed PSVR2's native 61.5/53.0 deg, so "
            "the containment test vr.cpp applies can finally pass");
    }

    CheckUnit(94);
    // Head pose and 6DOF on Halo 4's PROVEN right-handed Z-up basis.
    {
        Halo4CameraBasis mono{};
        mono.position[0] = 48.73f;
        mono.position[1] = -4.92f;
        mono.position[2] = 25.23f;
        // The live engine dump, which proves the basis this math assumes.
        mono.forward[0] = 0.222f;
        mono.forward[1] = -0.975f;
        mono.forward[2] = 0.0f;
        mono.up[0] = 0.0f;
        mono.up[1] = 0.0f;
        mono.up[2] = 1.0f;
        mono.verticalFov = 1.8295f;
        mono.fovRatio = 1.5385f;
        // Normalise forward exactly so the basis passes validation.
        const float forwardLength = sqrtf(mono.forward[0] * mono.forward[0] + mono.forward[1] * mono.forward[1]);
        mono.forward[0] /= forwardLength;
        mono.forward[1] /= forwardLength;
        Check(Halo4ValidateCameraBasis(mono), "The live Halo 4 engine camera basis is a valid orthonormal Z-up "
                                              "basis");

        Halo4HeadPoseInput level{};
        level.quaternion[3] = 1.0f;
        level.positional = false;
        Halo4CameraBasis unchanged = mono;
        Check(Halo4ApplyHeadPose(unchanged, level) && fabsf(unchanged.forward[0] - mono.forward[0]) < 1.0e-4f &&
                  fabsf(unchanged.forward[1] - mono.forward[1]) < 1.0e-4f &&
                  fabsf(unchanged.forward[2] - mono.forward[2]) < 1.0e-4f,
            "A level head at the recenter reference leaves Halo 4's own camera "
            "exactly where the engine aimed it, so the gamepad still aims");

        // Yaw the head 30 degrees. The camera must yaw with it and stay level.
        Halo4HeadPoseInput yawed{};
        const float halfYaw = 0.5f * 0.5235988f; // 30 deg
        yawed.quaternion[1] = sinf(halfYaw);     // OpenXR yaw is about +Y
        yawed.quaternion[3] = cosf(halfYaw);
        yawed.positional = false;
        yawed.yawSign = 1.0f;
        Halo4CameraBasis turned = mono;
        const float baseYaw = atan2f(mono.forward[1], mono.forward[0]);
        Check(Halo4ApplyHeadPose(turned, yawed), "A yawed head produces a valid Halo 4 camera basis");
        const float turnedYaw = atan2f(turned.forward[1], turned.forward[0]);
        Check(fabsf(Halo4WrapPi(turnedYaw - baseYaw) - (-0.5235988f)) < 1.0e-3f,
            "Turning your head yaws Halo 4's camera by the same angle about "
            "world up, which is what makes the world stay put");
        Check(fabsf(turned.up[2] - 1.0f) < 1.0e-3f, "A pure yaw introduces no roll or pitch");
        Check(Halo4ValidateCameraBasis(turned), "The yawed basis stays orthonormal, so the IPD split remains exact");

        // Pitch is absolute and needs no reference: it composes onto the
        // engine's own pitch. This is the case that broke an earlier version, because it
        // left `up` at the engine's value and failed the orthogonality band.
        Halo4HeadPoseInput pitched{};
        const float halfPitch = 0.5f * 0.5235988f;
        pitched.quaternion[0] = sinf(halfPitch); // about +X
        pitched.quaternion[3] = cosf(halfPitch);
        pitched.positional = false;
        Halo4CameraBasis looked = mono;
        Check(Halo4ApplyHeadPose(looked, pitched) && Halo4ValidateCameraBasis(looked),
            "A pitched head keeps forward and up orthonormal - the C-H4-6 "
            "defect that rejected every frame past 2.87 degrees of pitch");
        Check(looked.forward[2] > 0.4f && looked.forward[2] < 0.6f,
            "Looking up pitches Halo 4's camera up, in its Z-up basis");

        // 6DOF: stepping right in room space must move the camera along the
        // camera's own right axis, which for Halo is forward x up.
        Halo4HeadPoseInput leaned{};
        leaned.quaternion[3] = 1.0f;
        leaned.position[0] = 1.0f; // one metre to the right
        leaned.positional = true;
        leaned.worldScale = 0.33f;
        Halo4CameraBasis moved = mono;
        Check(Halo4ApplyHeadPose(moved, leaned), "A leaning head produces a valid Halo 4 camera");
        const float right[3] = {mono.forward[1] * mono.up[2] - mono.forward[2] * mono.up[1],
            mono.forward[2] * mono.up[0] - mono.forward[0] * mono.up[2],
            mono.forward[0] * mono.up[1] - mono.forward[1] * mono.up[0]};
        float alongRight = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
            alongRight += (moved.position[axis] - mono.position[axis]) * right[axis];
        Check(fabsf(alongRight - 0.33f) < 1.0e-3f, "One metre of physical lean moves Halo 4's camera one metre along "
                                                   "its own right axis, scaled by world_scale");

        // The clamp is in world units and applies after the scale.
        Halo4HeadPoseInput absurd{};
        absurd.quaternion[3] = 1.0f;
        absurd.position[1] = 100.0f;
        absurd.positional = true;
        absurd.worldScale = 0.33f;
        Halo4CameraBasis clamped = mono;
        Check(Halo4ApplyHeadPose(clamped, absurd) && fabsf(clamped.position[2] - (mono.position[2] + 1.5f)) < 1.0e-4f,
            "An absurd tracked translation is clamped to 1.5 world units "
            "instead of throwing the camera out of the level");

        // Failure is feature-local: a broken pose must leave the basis alone.
        Halo4HeadPoseInput broken{};
        broken.quaternion[0] = std::numeric_limits<float>::quiet_NaN();
        broken.quaternion[3] = 1.0f;
        Halo4CameraBasis untouched = mono;
        Check(!Halo4ApplyHeadPose(untouched, broken), "A non-finite head pose is refused, so the eyes fall back to the "
                                                      "engine's own camera instead of rendering garbage");

        // ---- The headset owns pitch ----------------------------------------
        //
        // The reported defect: with the head level, the engine's own camera
        // pitch went straight into the view, so the look stick tilted the world
        // away from the player's real horizon.
        Halo4CameraBasis enginePitched = mono;
        {
            const float p = 0.4f; // engine looking ~23 deg up, e.g. after stick
            const float y = atan2f(mono.forward[1], mono.forward[0]);
            Halo4ComposeHeadOwnedBasis(y, p, 0.0f, enginePitched.forward, enginePitched.up);
            Check(
                Halo4ValidateCameraBasis(enginePitched), "Halo4ComposeHeadOwnedBasis builds an orthonormal Z-up basis");
            Check(fabsf(asinf(enginePitched.forward[2]) - p) < 1.0e-4f &&
                      fabsf(Halo4WrapPi(atan2f(enginePitched.forward[1], enginePitched.forward[0]) - y)) < 1.0e-4f,
                "It reproduces exactly the yaw and pitch it was asked for");
        }

        Halo4HeadPoseInput levelOwned{};
        levelOwned.quaternion[3] = 1.0f;
        levelOwned.positional = false;
        levelOwned.headOwnsPitch = true;
        Halo4CameraBasis levelled = enginePitched;
        Check(Halo4ApplyHeadPose(levelled, levelOwned) && fabsf(levelled.forward[2]) < 1.0e-4f,
            "C-H4-9: a LEVEL head produces a LEVEL horizon even though the "
            "engine's own camera is pitched 23 degrees up - the reported "
            "'the up and down stick is breaking my orientation on my head'");
        Check(fabsf(Halo4WrapPi(atan2f(levelled.forward[1], levelled.forward[0]) -
                                atan2f(mono.forward[1], mono.forward[0]))) < 1.0e-4f,
            "Taking pitch does not disturb the engine's HEADING, so the stick "
            "still turns the player, the aim and the view together");

        Halo4CameraBasis added = enginePitched;
        Check(Halo4ApplyHeadPose(added, level) && fabsf(asinf(added.forward[2]) - 0.4f) < 1.0e-3f,
            "With headOwnsPitch off the C-H4-8 behaviour is bit-for-bit "
            "unchanged: the engine's own pitch survives into the view");

        Halo4HeadPoseInput pitchedOwned = pitched;
        pitchedOwned.headOwnsPitch = true;
        Halo4CameraBasis ownedLook = enginePitched;
        Check(Halo4ApplyHeadPose(ownedLook, pitchedOwned) && fabsf(asinf(ownedLook.forward[2]) - 0.5235988f) < 1.0e-3f,
            "A 30 degree head pitch puts the view at 30 degrees, not at 30 "
            "PLUS whatever the engine was already holding");
        Check(Halo4ValidateCameraBasis(ownedLook), "The head-owned basis stays orthonormal, so the IPD split and the "
                                                   "|fwd.up| validator that rejected C-H4-6 both stay satisfied");

        // 6DOF is shared by both paths and must be identical in each.
        Halo4HeadPoseInput leanOwned{};
        leanOwned.quaternion[3] = 1.0f;
        leanOwned.position[0] = 1.0f;
        leanOwned.positional = true;
        leanOwned.worldScale = 0.33f;
        leanOwned.headOwnsPitch = true;
        Halo4CameraBasis movedOwned = mono;
        Halo4CameraBasis movedDelta = mono;
        Halo4HeadPoseInput leanDelta = leanOwned;
        leanDelta.headOwnsPitch = false;
        Check(Halo4ApplyHeadPose(movedOwned, leanOwned) && Halo4ApplyHeadPose(movedDelta, leanDelta),
            "Both composition paths accept the same leaning head");
        Check(fabsf(movedOwned.position[0] - movedDelta.position[0]) < 1.0e-5f &&
                  fabsf(movedOwned.position[1] - movedDelta.position[1]) < 1.0e-5f &&
                  fabsf(movedOwned.position[2] - movedDelta.position[2]) < 1.0e-5f,
            "6DOF is the SAME shared code on both paths, so taking pitch "
            "cannot quietly change how much the world leans");

        // A pitch far past the poles must clamp rather than degenerate.
        Halo4HeadPoseInput extreme{};
        const float halfExtreme = 0.5f * 3.0f;
        extreme.quaternion[0] = sinf(halfExtreme);
        extreme.quaternion[3] = cosf(halfExtreme);
        extreme.positional = false;
        extreme.headOwnsPitch = true;
        extreme.pitchTrim = 1.4f;
        Halo4CameraBasis extremeCamera = mono;
        Check(Halo4ApplyHeadPose(extremeCamera, extreme) && Halo4ValidateCameraBasis(extremeCamera) &&
                  fabsf(asinf(extremeCamera.forward[2])) <= 1.5f + 1.0e-3f,
            "An extreme head pitch plus trim clamps at 1.5 rad instead of "
            "degenerating the basis at the pole");

        // ---- Yaw ownership ------------------------------------------------
        //
        // The doubling hazard, pinned. Once the aim loop steers the engine's
        // heading toward the reference, a view that still reads the engine's
        // LIVE heading applies the head's yaw twice.
        {
            const float reference = 0.7f;
            const float headYaw = 0.4f; // 23 deg of head turn
            Halo4HeadPoseInput owned{};
            // OpenXR yaw is about +Y and Halo4DecodeHeadOrientation reports
            // atan2(hfx, -hfz), which negates it - so a +headYaw decoded yaw
            // comes from a -headYaw quaternion. The head-pose yaw test above
            // pins the same relationship from the other side.
            const float half = -0.5f * headYaw;
            owned.quaternion[1] = sinf(half);
            owned.quaternion[3] = cosf(half);
            owned.yawSign = 1.0f;
            owned.positional = false;
            owned.headOwnsPitch = true;
            owned.headOwnsYaw = true;
            owned.gameYawReference = reference;

            // The engine has already been steered to (reference + headYaw),
            // which is what a converged loop leaves behind.
            Halo4CameraBasis steered = mono;
            Halo4ComposeHeadOwnedBasis(reference + headYaw, 0.0f, 0.0f, steered.forward, steered.up);
            Halo4CameraBasis viewed = steered;
            Check(Halo4ApplyHeadPose(viewed, owned), "The yaw-owned path accepts a converged engine heading");
            Check(fabsf(Halo4WrapPi(atan2f(viewed.forward[1], viewed.forward[0]) - (reference + headYaw))) < 1.0e-3f,
                "C-H4-10: the view composes from the REFERENCE, so a converged "
                "aim loop leaves the view exactly where the head points");

            // The same input against the live-heading path is the bug: it
            // lands at reference + 2 * headYaw.
            Halo4HeadPoseInput live = owned;
            live.headOwnsYaw = false;
            Halo4CameraBasis doubled = steered;
            Check(Halo4ApplyHeadPose(doubled, live) &&
                      fabsf(Halo4WrapPi(
                          atan2f(doubled.forward[1], doubled.forward[0]) - (reference + 2.0f * headYaw))) < 1.0e-3f,
                "Reading the engine's live heading instead would double the "
                "head's yaw - the exact reason yaw ownership is not optional "
                "once the aim loop runs");

            // And with no turn and a level head, the reference IS the view.
            Halo4HeadPoseInput still{};
            still.quaternion[3] = 1.0f;
            still.positional = false;
            still.headOwnsPitch = true;
            still.headOwnsYaw = true;
            still.gameYawReference = reference;
            Halo4CameraBasis parked = mono;
            Check(Halo4ApplyHeadPose(parked, still) &&
                      fabsf(Halo4WrapPi(atan2f(parked.forward[1], parked.forward[0]) - reference)) < 1.0e-3f,
                "A level head at the reference looks exactly along it, "
                "whatever the engine's own camera was doing");
        }
    }

    CheckUnit(95);
    // ---- First-person hands ------------------------------------------
    {
        // The dimensions must reproduce the kit's own allocations, or the
        // layout being written into is not the one the kit dump proved.
        Check(kHalo4FirstPersonOrientationStride * kHalo4FirstPersonMaxWeapons * kHalo4FirstPersonMaxUsers == 0xF000u,
            "The orientation dimensions reproduce H4EK's 0xF000 allocation");
        Check(kHalo4FirstPersonWeaponsUserStride * kHalo4FirstPersonMaxUsers == 0x17D20u,
            "The fp-weapons dimensions reproduce H4EK's 0x17D20 allocation");
        Check(kHalo4FirstPersonMaxNodes == 120, "A first-person node bank holds 120 transforms of 0x20 bytes");
        // Corrected by a headset probe: the LIVE bank is
        // the first one. Reading the +0xF00 copy is what returned zeros.
        Check(kHalo4FirstPersonNodeArrayOffset == 0 && kHalo4FirstPersonPreviousNodeArrayOffset == 0xF00,
            "The live node bank is the record's FIRST half; +0xF00 is the "
            "per-frame copy the engine interpolates against");
        Check(kHalo4FirstPersonPreviousNodeArrayOffset + kHalo4FirstPersonMaxNodes * kHalo4FirstPersonNodeStride ==
                  kHalo4FirstPersonOrientationStride,
            "The two banks exactly fill the 0x1E00 orientation record");
        Check(kHalo4FirstPersonRootNode == 0, "The assembly is written at its root node");

        // These are title-specific H4EK/tag facts.  If
        // they drift, the runtime must not silently solve a different rig.
        Check(kHalo4ModelSkinningRva == 0x33D8B8u && kHalo4FirstPersonSkinningReturnRva == 0x36F3C9u,
            "Halo 4 VRIK pins the final palette consumer and its sole FP return");
        Check(kHalo4StormFpBodyNodeCount == 80 && kHalo4StormFpComposedNodeCount == 85 &&
                  kHalo4RightShoulderNode == 4 && kHalo4RightElbowNode == 16 && kHalo4RightHandNode == 29 &&
                  kHalo4LeftShoulderNode == 5 && kHalo4LeftElbowNode == 8 && kHalo4LeftHandNode == 37,
            "The Storm body/composed counts and both arm chains match the H4EK tag");
        const auto lengthSquared = [](const float value[3])
        { return value[0] * value[0] + value[1] * value[1] + value[2] * value[2]; };
        Check(fabsf(lengthSquared(kHalo4RightPoleDirection) - 1.0f) < 1.0e-3f &&
                  fabsf(lengthSquared(kHalo4LeftPoleDirection) - 1.0f) < 1.0e-3f,
            "The Blender-authored pole directions remain normalized");
        Check(fabsf(kHalo4RightAttachmentMetres[1] - 0.059896708f) < 1.0e-6f &&
                  fabsf(kHalo4LeftAttachmentMetres[1] - 0.059896648f) < 1.0e-6f,
            "The two controller-parented attachment offsets preserve authored metres");

        // Two headset sessions were lost to gating on
        // argument 7.  It is a skinning PALETTE SIZE, not a node count: the
        // live records measured 96, 5 and 33 while storm_fp genuinely has 80
        // nodes, and 80 is not even a reachable palette size for it.  The copy
        // is bounded by the record's fixed bank instead, which is a structural
        // fact rather than a belief about any model.
        Check(kHalo4FirstPersonBankTransforms == 120 && kHalo4FirstPersonBankTransforms >= kHalo4StormFpBodyNodeCount,
            "The record bank bound covers every Storm body node without "
            "consulting argument 7");
        Check(0xB0u + static_cast<uint32_t>(kHalo4FirstPersonBankTransforms) * 0x34u <= 0x1910u,
            "The whole copied bank fits inside one 0x1910 first-person record");
        Check(kHalo4FirstPersonRecordBankOffset == 0xB0u && kHalo4FirstPersonRecordFillFlagOffset == 0x08u &&
                  kHalo4FirstPersonBodyFillFlag == 0,
            "The record header offsets match the engine's own bank filler");

        // The Storm classifier is what identifies the record now, so it must
        // keep rejecting a rig that merely has enough nodes.
        Check(Halo4StormLinkLengthsMatch(
                  kHalo4StormUpperArmBind, kHalo4StormForearmBind, kHalo4StormUpperArmBind, kHalo4StormForearmBind),
            "The H4EK storm_fp bind lengths sit inside the admitted envelope");
        Check(!Halo4StormLinkLengthsMatch(
                  0.5f, kHalo4StormForearmBind, kHalo4StormUpperArmBind, kHalo4StormForearmBind) &&
                  !Halo4StormLinkLengthsMatch(
                      kHalo4StormUpperArmBind, 0.0f, kHalo4StormUpperArmBind, kHalo4StormForearmBind),
            "A single out-of-envelope link refuses the whole record");
        const float notANumber = std::numeric_limits<float>::quiet_NaN();
        Check(!Halo4StormLinkLengthsMatch(
                  notANumber, kHalo4StormForearmBind, kHalo4StormUpperArmBind, kHalo4StormForearmBind),
            "A NaN link length is refused by the comparisons themselves");

        // This helper is deliberately only a broad sanity envelope. These
        // values came from the old path's misrouted flag-0/120 native body,
        // not storm_fp; admitting them proves the helper is not being reused
        // as the per-eye record-identity predicate.
        Check(Halo4StormLinkLengthsMatch(0.2113f, 0.2341f, 0.2135f, 0.3144f),
            "The broad link sanity envelope remains non-identifying (window A)");
        Check(Halo4StormLinkLengthsMatch(0.2100f, 0.2209f, 0.2027f, 0.3192f),
            "The broad link sanity envelope remains non-identifying (window B)");
        // Mirror symmetry on the upper arm is the discriminator that survives
        // the widened absolute range: a real pair of arms agreed to within
        // 3.5% in every sample, and an unrelated rig will not.
        Check(!Halo4StormLinkLengthsMatch(0.2100f, 0.2300f, 0.0800f, 0.2000f),
            "Mismatched left/right upper arms are still refused");

        // The official H4EK meshes contain 82 upper/forearm
        // cross-weighted vertices per side and 222 right / 235 left
        // forearm/hand cross-weighted vertices. An over-reach must therefore
        // extend both links proportionally; assigning all excess translation
        // to the hand subtree creates the reported torn, badly weighted mesh.
        Halo4ArmReachPlan naturalReach{};
        Check(Halo4PlanArmReach(0.20f, 0.30f, 0.45f, naturalReach) && fabsf(naturalReach.stretch - 1.0f) < 1.0e-6f &&
                  fabsf(naturalReach.upperExtension) < 1.0e-6f && fabsf(naturalReach.lowerExtension) < 1.0e-6f,
            "Halo 4 leaves an in-reach authored arm at its stock link lengths");
        Halo4ArmReachPlan extendedReach{};
        Check(Halo4PlanArmReach(0.20f, 0.30f, 0.75f, extendedReach) && fabsf(extendedReach.stretch - 1.5f) < 1.0e-6f &&
                  fabsf(extendedReach.upperLength - 0.30f) < 1.0e-6f &&
                  fabsf(extendedReach.lowerLength - 0.45f) < 1.0e-6f &&
                  fabsf(extendedReach.upperExtension - 0.10f) < 1.0e-6f &&
                  fabsf(extendedReach.lowerExtension - 0.15f) < 1.0e-6f,
            "Halo 4 distributes tracked over-reach across both weighted arm links");
        Halo4ArmReachPlan boundedReach{};
        Check(Halo4PlanArmReach(0.20f, 0.30f, 2.0f, boundedReach) && fabsf(boundedReach.stretch - 1.8f) < 1.0e-6f,
            "Halo 4 retains the finite 1.8x arm-stretch safety bound");
        Halo4ArmReachPlan invalidReach{};
        Check(!Halo4PlanArmReach(0.0f, 0.30f, 0.4f, invalidReach) &&
                  !Halo4PlanArmReach(0.20f, 0.30f, std::numeric_limits<float>::quiet_NaN(), invalidReach),
            "Halo 4 refuses invalid arm-link or tracked-span inputs");

        Check(!Halo4ShouldCollapseFloatingNode(Halo4FloatingNodeRole::CollapseAtRightWrist, false) &&
                  !Halo4ShouldCollapseFloatingNode(Halo4FloatingNodeRole::CollapseAtLeftWrist, false) &&
                  Halo4ShouldCollapseFloatingNode(Halo4FloatingNodeRole::Hidden, false) &&
                  Halo4ShouldCollapseFloatingNode(Halo4FloatingNodeRole::CollapseAtRightWrist, true) &&
                  Halo4ShouldCollapseFloatingNode(Halo4FloatingNodeRole::CollapseAtLeftWrist, true) &&
                  !Halo4ShouldCollapseFloatingNode(Halo4FloatingNodeRole::RightHand, true) &&
                  !Halo4ShouldCollapseFloatingNode(Halo4FloatingNodeRole::LeftHand, true),
            "Halo 4 arms-with-hands shows only the solved chains; floating hands collapses both arms and preserves both hands");
        Check(Halo4VisibleArmSolveCanCommit(true, true, true) && !Halo4VisibleArmSolveCanCommit(true, false, true) &&
                  !Halo4VisibleArmSolveCanCommit(true, true, false) &&
                  !Halo4VisibleArmSolveCanCommit(false, true, true),
            "Halo 4 preserves the hand-only fallback unless both visible-arm solves succeed");

        // The 28 helper/fixup/armour bones between the joints. The shared
        // solver writes only the shoulder, the elbow and the hand mask, so
        // these are carried separately; if the band tables ever drift out of
        // the subtree tables the arm tears open between shoulder and hand.
        {
            // clang-format off
            const int rightShoulder[] = {
                4,11,12,14,15,16,17,18,22,26,27,29,30,31,34,36,38,40,41,42,
                44,45,49,50,52,53,55,56,58,63,65,66,67,68,71,72,76,77,78};
            // clang-format on
            // clang-format off
            const int rightElbow[] = {
                16,22,26,27,29,30,31,34,36,38,40,41,42,44,45,49,50,52,53,
                55,56,58,63,65,66,67,68,71,72,76,77,78};
            // clang-format on
            // clang-format off
            const int rightHand[] = {
                29,40,41,42,44,45,49,50,52,53,55,56,58,63,65,66,67,68,71,
                72,76,77,78};
            // clang-format on
            const int rightUpperBand[] = {11, 12, 14, 15, 17, 18};
            const int rightForearmBand[] = {22, 26, 27, 30, 31, 34, 36, 38};
            auto has = [](const int* set, size_t n, int v)
            {
                for (size_t i = 0; i < n; ++i)
                    if (set[i] == v)
                        return true;
                return false;
            };
            bool upperExact = true;
            for (int i = 0; i < 80; ++i)
            {
                const bool expected = has(rightShoulder, std::size(rightShoulder), i) &&
                                      !has(rightElbow, std::size(rightElbow), i) && i != 4;
                if (expected != has(rightUpperBand, std::size(rightUpperBand), i))
                    upperExact = false;
            }
            bool forearmExact = true;
            for (int i = 0; i < 80; ++i)
            {
                const bool expected =
                    has(rightElbow, std::size(rightElbow), i) && !has(rightHand, std::size(rightHand), i) && i != 16;
                if (expected != has(rightForearmBand, std::size(rightForearmBand), i))
                    forearmExact = false;
            }
            Check(upperExact, "The right upper-arm band is exactly shoulder minus elbow minus the joint");
            Check(forearmExact, "The right forearm band is exactly elbow minus hand minus the joint");
            Check(!has(rightUpperBand, std::size(rightUpperBand), 4) &&
                      !has(rightForearmBand, std::size(rightForearmBand), 16),
                "No joint appears inside the band it drives");
        }
        Check(Halo4StormSideOrderMatches(-0.1f, 0.1f) && !Halo4StormSideOrderMatches(0.1f, -0.1f) &&
                  !Halo4StormSideOrderMatches(0.1f, 0.1f),
            "Blam's left axis orders the right shoulder below the left one");

        Halo4FirstPersonNode stock{};
        stock.rotation[3] = 1.0f;
        stock.translation[0] = 0.2f;
        stock.scale = 1.0f;
        Check(Halo4FirstPersonNodeLooksValid(stock), "A unit quaternion with a sane scale reads as a valid node");

        // The whole point of the live probe: shapes that are NOT
        // {quat,translation,scale} must be refused, not written over.
        Halo4FirstPersonNode notANode = stock;
        notANode.rotation[3] = 37.0f; // a matrix row, or a float that is not a quaternion
        Check(!Halo4FirstPersonNodeLooksValid(notANode),
            "A non-unit rotation is refused, so a wrong layout can never be "
            "written into an engine bone array");
        Halo4FirstPersonNode absurd = stock;
        absurd.translation[1] = 5000.0f;
        Check(
            !Halo4FirstPersonNodeLooksValid(absurd), "A translation far outside the first-person envelope is refused");
        Halo4FirstPersonNode broken = stock;
        broken.scale = std::numeric_limits<float>::quiet_NaN();
        Check(!Halo4FirstPersonNodeLooksValid(broken), "A non-finite field is refused");

        // Placement: a controller held 30 cm forward of the head must put the
        // node 30 cm forward in the engine's frame, scaled to world units.
        Halo4HandPlacementInput input{};
        input.controllerOffset[2] = -0.30f; // OpenXR forward is -Z
        input.controllerOrientation[3] = 1.0f;
        input.worldScale = 0.33f;
        Halo4FirstPersonNode placed{};
        Check(Halo4BuildHandNode(input, stock, placed), "A tracked controller produces a valid first-person node");
        Check(fabsf(placed.translation[0] - 0.30f * 0.33f) < 1.0e-5f,
            "Reaching forward moves the gun forward along Blam's +X, scaled by "
            "world_scale");
        Check(fabsf(placed.translation[1]) < 1.0e-5f && fabsf(placed.translation[2]) < 1.0e-5f,
            "A purely forward reach introduces no lateral or vertical shift");
        Check(fabsf(placed.scale - stock.scale) < 1.0e-6f, "The engine's own node scale is preserved, never invented");

        // Right and up map onto Blam's -Y (left is +Y) and +Z.
        Halo4HandPlacementInput right{};
        right.controllerOffset[0] = 0.25f; // OpenXR +X is right
        right.controllerOrientation[3] = 1.0f;
        right.worldScale = 1.0f;
        Halo4FirstPersonNode placedRight{};
        Check(Halo4BuildHandNode(right, stock, placedRight) && fabsf(placedRight.translation[1] + 0.25f) < 1.0e-5f,
            "Moving your hand right moves the gun along Blam's -Y");
        Halo4HandPlacementInput up{};
        up.controllerOffset[1] = 0.25f;
        up.controllerOrientation[3] = 1.0f;
        up.worldScale = 1.0f;
        Halo4FirstPersonNode placedUp{};
        Check(Halo4BuildHandNode(up, stock, placedUp) && fabsf(placedUp.translation[2] - 0.25f) < 1.0e-5f,
            "Raising your hand raises the gun along Blam's +Z");

        // Mirroring is a left-handed reflection across the forward axis.
        Halo4HandPlacementInput mirrored = right;
        mirrored.mirrored = true;
        Halo4FirstPersonNode placedMirror{};
        Check(Halo4BuildHandNode(mirrored, stock, placedMirror) && fabsf(placedMirror.translation[1] - 0.25f) < 1.0e-5f,
            "Left-handed mirrors the lateral placement");

        // Trims are applied in the controller's frame, before scaling.
        Halo4HandPlacementInput trimmed{};
        trimmed.controllerOrientation[3] = 1.0f;
        trimmed.worldScale = 1.0f;
        trimmed.forwardTrim = 0.1f;
        Halo4FirstPersonNode placedTrim{};
        Check(Halo4BuildHandNode(trimmed, stock, placedTrim) && fabsf(placedTrim.translation[0] - 0.1f) < 1.0e-5f,
            "The forward trim moves the gun even with the controller at the "
            "head, so the config can tune an authored placement");

        // The output quaternion must stay unit-length, or the engine gets a
        // bone it cannot use.
        const float length = sqrtf(placed.rotation[0] * placed.rotation[0] + placed.rotation[1] * placed.rotation[1] +
                                   placed.rotation[2] * placed.rotation[2] + placed.rotation[3] * placed.rotation[3]);
        Check(fabsf(length - 1.0f) < 1.0e-4f, "The placed rotation is renormalised before it reaches the engine");

        // Refusal is total: a bad stock node yields no write at all.
        Halo4FirstPersonNode fromBad{};
        Check(!Halo4BuildHandNode(input, notANode, fromBad),
            "A stock node that failed the format proof produces no placement");

        // Final-palette ownership is a world pose built from the controller
        // and the stable recenter/body pair. Neither the current HMD pose nor
        // Halo 4's live aim-camera basis is an input.
        Halo4ControllerWorldPoseInput controllerWorld{};
        controllerWorld.controllerOrientation[3] = 1.0f;
        controllerWorld.controllerPosition[2] = -0.30f;
        controllerWorld.bodyOrigin[0] = 10.0f;
        controllerWorld.bodyOrigin[1] = 20.0f;
        controllerWorld.bodyOrigin[2] = 30.0f;
        controllerWorld.worldScale = 0.33f;
        Halo4ControllerWorldPose neutralWorld{};
        Check(Halo4BuildControllerWorldPose(controllerWorld, neutralWorld),
            "Halo 4 builds a controller world pose without a head-camera parent");
        Check(fabsf(neutralWorld.position[0] - 10.099f) < 1.0e-5f &&
                  fabsf(neutralWorld.position[1] - 20.0f) < 1.0e-5f &&
                  fabsf(neutralWorld.position[2] - 30.0f) < 1.0e-5f,
            "A neutral forward reach is anchored to the stable pre-HMD body origin");
        Check(fabsf(neutralWorld.basis[0] - 1.0f) < 1.0e-5f && fabsf(neutralWorld.basis[4] - 1.0f) < 1.0e-5f &&
                  fabsf(neutralWorld.basis[8] - 1.0f) < 1.0e-5f,
            "A neutral controller produces Halo 4's native forward-left-up basis");

        // A VR turn rotates position and orientation together about the same
        // body anchor. It does not need the engine camera that motion aim is
        // simultaneously steering toward the hand.
        Halo4ControllerWorldPoseInput snapTurn = controllerWorld;
        snapTurn.gameYawReference = 1.57079632679f;
        Halo4ControllerWorldPose turnedWorld{};
        Check(Halo4BuildControllerWorldPose(snapTurn, turnedWorld),
            "A Halo 4 VR turn produces a valid controller world pose");
        Check(fabsf(turnedWorld.position[0] - 10.0f) < 1.0e-5f && fabsf(turnedWorld.position[1] - 20.099f) < 1.0e-5f &&
                  fabsf(turnedWorld.basis[0]) < 1.0e-5f && fabsf(turnedWorld.basis[1] - 1.0f) < 1.0e-5f,
            "Snap turn rotates the hand and its reach once in the game frame");

        // A 30-degree controller yaw must appear exactly once. A rejected
        // earlier construction also multiplied by Halo 4's live aim
        // camera, which that same controller had already turned by 30 degrees,
        // and therefore emitted roughly 60 degrees.
        constexpr float controllerYaw = 0.52359877559f;
        Halo4ControllerWorldPoseInput aimed = controllerWorld;
        aimed.yawSign = 1.0f;
        aimed.controllerOrientation[1] = -sinf(controllerYaw * 0.5f);
        aimed.controllerOrientation[3] = cosf(controllerYaw * 0.5f);
        Halo4ControllerWorldPose aimedWorld{};
        Check(
            Halo4BuildControllerWorldPose(aimed, aimedWorld), "A yawed controller produces a valid Halo 4 world basis");
        Check(fabsf(aimedWorld.basis[0] - cosf(controllerYaw)) < 1.0e-5f &&
                  fabsf(aimedWorld.basis[1] - sinf(controllerYaw)) < 1.0e-5f,
            "Controller yaw is applied once, never again through the aim camera");
        Check(fabsf(aimedWorld.position[0] - neutralWorld.position[0]) < 1.0e-5f &&
                  fabsf(aimedWorld.position[1] - neutralWorld.position[1]) < 1.0e-5f,
            "Turning the controller does not orbit its tracked position around the head");

        // Recenter removes the physical facing from both orientation and
        // position. The controller remains 30 cm forward when neutral facing
        // happens to be 30 degrees around the room.
        Halo4ControllerWorldPoseInput recentered = controllerWorld;
        recentered.headYawReference = controllerYaw;
        recentered.controllerOrientation[1] = -sinf(controllerYaw * 0.5f);
        recentered.controllerOrientation[3] = cosf(controllerYaw * 0.5f);
        recentered.controllerPosition[0] = 0.30f * sinf(controllerYaw);
        recentered.controllerPosition[2] = -0.30f * cosf(controllerYaw);
        Halo4ControllerWorldPose recenteredWorld{};
        Check(Halo4BuildControllerWorldPose(recentered, recenteredWorld) &&
                  fabsf(recenteredWorld.position[0] - 10.099f) < 1.0e-5f &&
                  fabsf(recenteredWorld.position[1] - 20.0f) < 1.0e-5f &&
                  fabsf(recenteredWorld.basis[0] - 1.0f) < 1.0e-5f && fabsf(recenteredWorld.basis[1]) < 1.0e-5f,
            "The recenter pair removes room facing without consulting current head pose");

        Halo4ControllerWorldPoseInput controllerTrim = aimed;
        controllerTrim.controllerPosition[2] = 0.0f;
        controllerTrim.forwardTrim = 0.10f;
        Halo4ControllerWorldPose trimmedWorld{};
        Check(Halo4BuildControllerWorldPose(controllerTrim, trimmedWorld) &&
                  fabsf(trimmedWorld.position[0] - (10.0f + 0.10f * 0.33f * cosf(controllerYaw))) < 1.0e-5f &&
                  fabsf(trimmedWorld.position[1] - (20.0f + 0.10f * 0.33f * sinf(controllerYaw))) < 1.0e-5f,
            "Halo 4 hand trims are applied in the controller's own frame");

        Halo4ControllerWorldPoseInput badControllerWorld = controllerWorld;
        badControllerWorld.gameYawReference = std::numeric_limits<float>::quiet_NaN();
        Halo4ControllerWorldPose refusedWorld{};
        Check(!Halo4BuildControllerWorldPose(badControllerWorld, refusedWorld),
            "A non-finite controller reference fails closed for the rig only");

        // --- The model-frame hand, for the bank that has no camera --
        //
        // The body fill's root is NULL (or a pure translation), so the
        // arms bank is the model's own frame and the hand must be expressed as
        // the headset sees the controller. The invariants below are the ones
        // that distinguish that conversion from the world-space one above.
        Check(fabsf(Halo4MeasureRigScale(0.2113f, 0.0915251f) - 2.3086f) < 1.0e-3f,
            "The rig scale is measured from the bank against the authored bind");
        Check(Halo4MeasureRigScale(0.0f, 0.0915251f, 1.0f) == 1.0f &&
                  Halo4MeasureRigScale(std::numeric_limits<float>::quiet_NaN(), 0.0915251f, 1.0f) == 1.0f,
            "An unmeasurable arm falls back rather than scaling by garbage");
        Check(Halo4MeasureRigScale(1000.0f, 0.0915251f) == kHalo4RigScaleMax &&
                  Halo4MeasureRigScale(1.0e-4f, 10.0f) == kHalo4RigScaleMin,
            "A corrupt bank cannot produce an unbounded tracked reach");

        Halo4ModelHandInput modelHand{};
        modelHand.controllerOrientation[3] = 1.0f;
        modelHand.headOrientation[3] = 1.0f;
        modelHand.controllerPosition[2] = -0.30f; // 30 cm in front
        modelHand.worldScale = 0.33f;
        modelHand.rigScale = 2.0f;
        Halo4ModelHandPose neutralModel{};
        Check(Halo4BuildModelHandPose(modelHand, neutralModel),
            "Halo 4 builds a model-frame hand from the live head and controller");
        Check(fabsf(neutralModel.position[0] - 0.30f * 0.33f * 2.0f) < 1.0e-5f &&
                  fabsf(neutralModel.position[1]) < 1.0e-5f && fabsf(neutralModel.position[2]) < 1.0e-5f,
            "A hand held in front of the headset lands ahead of the model origin");
        Check(fabsf(neutralModel.basis[0] - 1.0f) < 1.0e-5f && fabsf(neutralModel.basis[4] - 1.0f) < 1.0e-5f &&
                  fabsf(neutralModel.basis[8] - 1.0f) < 1.0e-5f,
            "A neutral controller produces Halo 4's native forward-left-up basis");

        // The measured reach scales with the rig, which is the whole reason the
        // factor is read from the bank: the same physical reach must drive a
        // 2.3x rig 2.3x as far or the hand can never catch up with it.
        Halo4ModelHandInput unscaled = modelHand;
        unscaled.rigScale = 1.0f;
        Halo4ModelHandPose unscaledModel{};
        Check(Halo4BuildModelHandPose(unscaled, unscaledModel) &&
                  fabsf(neutralModel.position[0] - 2.0f * unscaledModel.position[0]) < 1.0e-5f,
            "Tracked reach is expressed in the units the bank actually uses");

        // THE INVARIANT THAT DEFINES THIS FRAME. Turning your whole body -
        // head and controller together - moves nothing in the model's frame,
        // because that frame is the camera's. No recenter reference and no
        // gameplay yaw is consulted to achieve it, which is precisely why the
        // world-space conversion above is the wrong one for this bank.
        const float bodyYaw = 0.7f;
        const float sy = sinf(bodyYaw * 0.5f), cy = cosf(bodyYaw * 0.5f);
        Halo4ModelHandInput turnedBody = modelHand;
        turnedBody.headOrientation[0] = 0.0f;
        turnedBody.headOrientation[1] = sy; // yaw about OpenXR +Y
        turnedBody.headOrientation[2] = 0.0f;
        turnedBody.headOrientation[3] = cy;
        turnedBody.controllerOrientation[1] = sy;
        turnedBody.controllerOrientation[3] = cy;
        // The controller stays 30 cm in front of the head after the turn.
        turnedBody.controllerPosition[0] = -0.30f * sinf(bodyYaw);
        turnedBody.controllerPosition[2] = -0.30f * cosf(bodyYaw);
        Halo4ModelHandPose turnedModel{};
        Check(Halo4BuildModelHandPose(turnedBody, turnedModel),
            "A turned player still produces a valid model-frame hand");
        for (int i = 0; i < 3; ++i)
            Check(fabsf(turnedModel.position[i] - neutralModel.position[i]) < 1.0e-4f,
                "Turning head and hand together does not move the hand in the "
                "model frame");
        Check(fabsf(turnedModel.basis[0] - 1.0f) < 1.0e-4f && fabsf(turnedModel.basis[1]) < 1.0e-4f,
            "Turning head and hand together does not rotate the hand either");

        // Moving the head alone must move the hand in the model frame, or the
        // arms would be welded to the face - the symptom seen in headset testing.
        Halo4ModelHandInput leanedHead = modelHand;
        leanedHead.headPosition[0] = 0.10f; // lean 10 cm right
        Halo4ModelHandPose leanedModel{};
        Check(Halo4BuildModelHandPose(leanedHead, leanedModel) &&
                  fabsf(leanedModel.position[1] - (neutralModel.position[1] + 0.10f * 0.33f * 2.0f)) < 1.0e-5f,
            "Leaning the head alone moves the hand within the model frame");

        // Controller yaw appears exactly once, with no camera to double it,
        // and in Halo's axes rather than OpenXR's. A left turn of the wrist is
        // a positive rotation about OpenXR +Y and must swing Blam's forward
        // column toward Blam +Y, which is left.
        Halo4ModelHandInput yawedHand = modelHand;
        yawedHand.controllerOrientation[1] = sinf(controllerYaw * 0.5f);
        yawedHand.controllerOrientation[3] = cosf(controllerYaw * 0.5f);
        Halo4ModelHandPose yawedModel{};
        Check(Halo4BuildModelHandPose(yawedHand, yawedModel) &&
                  fabsf(yawedModel.basis[0] - cosf(controllerYaw)) < 1.0e-4f &&
                  fabsf(yawedModel.basis[1] - sinf(controllerYaw)) < 1.0e-4f,
            "Controller yaw reaches the model-frame hand exactly once");

        Halo4ModelHandInput mirroredHand = modelHand;
        mirroredHand.controllerPosition[0] = 0.20f;
        mirroredHand.controllerOrientation[1] = sinf(controllerYaw * 0.5f);
        mirroredHand.controllerOrientation[3] = cosf(controllerYaw * 0.5f);
        mirroredHand.mirrored = true;
        Halo4ModelHandInput handedHand = mirroredHand;
        handedHand.mirrored = false;
        Halo4ModelHandPose mirroredModel{}, handedModel{};
        Check(Halo4BuildModelHandPose(mirroredHand, mirroredModel) &&
                  Halo4BuildModelHandPose(handedHand, handedModel) &&
                  fabsf(mirroredModel.position[1] + handedModel.position[1]) < 1.0e-5f,
            "Left-handed mirroring reflects the hand across the model's midline");
        // A reflection is not a rotation. Negating one basis column would send
        // the determinant to -1 and render the hand inside out, and nothing
        // downstream would catch it: NormalizedBasis checks column lengths and
        // orthogonality, and InvertBoneMatrix transposes, which a reflection
        // survives.
        {
            const float* m = mirroredModel.basis;
            const float determinant = m[0] * (m[4] * m[8] - m[5] * m[7]) - m[3] * (m[1] * m[8] - m[2] * m[7]) +
                                      m[6] * (m[1] * m[5] - m[2] * m[4]);
            Check(fabsf(determinant - 1.0f) < 1.0e-4f, "Mirrored hands stay a proper rotation, never an inside-out "
                                                       "reflection");
        }

        // The per-eye stereo offset and the hand share one frame conversion, so
        // a change to either cannot silently desynchronise them. With a neutral
        // head the conversion is the bare axis permutation.
        {
            const float identityHead[4] = {0.0f, 0.0f, 0.0f, 1.0f};
            const float delta[3] = {0.032f, 0.0f, 0.0f}; // half an IPD, right
            float blam[3] = {};
            Check(Halo4HeadLocalToBlam(identityHead, delta, blam) && fabsf(blam[0]) < 1.0e-6f &&
                      fabsf(blam[1] + 0.032f) < 1.0e-6f && fabsf(blam[2]) < 1.0e-6f,
                "A right-eye offset becomes a negative-left offset in Halo's "
                "axes");
            // Turning the head must carry the offset with it: the right eye is
            // still to the model's right whichever way the player faces.
            const float quarter = 0.5f * 1.5707963f;
            const float turned[4] = {0.0f, sinf(quarter), 0.0f, cosf(quarter)};
            const float worldDelta[3] = {0.0f, 0.0f, -0.032f};
            float turnedBlam[3] = {};
            Check(Halo4HeadLocalToBlam(turned, worldDelta, turnedBlam) && fabsf(turnedBlam[1] + 0.032f) < 1.0e-5f,
                "The eye offset is expressed in the model frame, not the room");
        }

        Halo4ModelHandInput badModelHand = modelHand;
        badModelHand.headPosition[1] = std::numeric_limits<float>::quiet_NaN();
        Halo4ModelHandPose refusedModel{};
        Check(!Halo4BuildModelHandPose(badModelHand, refusedModel),
            "A non-finite head pose refuses the hand instead of guessing one");

        // The rigid assembly transform: applying one rotation+translation to
        // every node must move the whole rig without depending on which node
        // is the root, and must PRESERVE the shape between nodes.
        Halo4FirstPersonNode a{}, b{};
        a.rotation[3] = 1.0f;
        a.translation[0] = 0.10f;
        a.scale = 1.0f;
        b.rotation[3] = 1.0f;
        b.translation[0] = 0.40f;
        b.scale = 1.0f;
        const float halfTurn = 0.5f * 1.5707963f; // 90 deg about Blam +Z (up)
        const float yaw[4] = {0.0f, 0.0f, sinf(halfTurn), cosf(halfTurn)};
        const float shift[3] = {1.0f, 2.0f, 3.0f};
        Halo4FirstPersonNode movedA{}, movedB{};
        Check(Halo4TransformAssemblyNode(a, yaw, shift, movedA) && Halo4TransformAssemblyNode(b, yaw, shift, movedB),
            "Every node accepts the rigid assembly transform");
        const float beforeGap = b.translation[0] - a.translation[0];
        float afterGap = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float d = movedB.translation[axis] - movedA.translation[axis];
            afterGap += d * d;
        }
        Check(fabsf(sqrtf(afterGap) - fabsf(beforeGap)) < 1.0e-4f,
            "The distance between two nodes is unchanged, so the gun and arms "
            "move as ONE RIGID BODY rather than being distorted");
        Check(fabsf(movedA.translation[1] - (0.10f + 2.0f)) < 1.0e-4f && fabsf(movedA.translation[0] - 1.0f) < 1.0e-4f,
            "A 90 degree yaw carries a node's own offset around with it before "
            "the translation is added");
        Check(fabsf(movedA.scale - a.scale) < 1.0e-6f, "The rigid transform never touches a node's scale");
        Halo4FirstPersonNode fromBadNode{};
        Check(!Halo4TransformAssemblyNode(notANode, yaw, shift, fromBadNode),
            "A node that fails the format check is left alone");

        // Pin the live per-eye record ownership that an earlier version routed
        // backwards. Argument 7 is deliberately absent from this decision.
        Halo4FloatingRecordPhase recordPhase = Halo4FloatingRecordPhase::AwaitStormHands;
        auto recordDecision = Halo4SelectFloatingRecord(recordPhase, 1, 80);
        Check(recordDecision.action == Halo4FloatingRecordAction::BuildStormHands &&
                  recordDecision.next == Halo4FloatingRecordPhase::AwaitHeldModel,
            "The first flag-1/80 record owns the exact Storm hands transaction");
        recordPhase = recordDecision.next;
        recordDecision = Halo4SelectFloatingRecord(recordPhase, 1, 5);
        Check(recordDecision.action == Halo4FloatingRecordAction::CarryHeldModel &&
                  recordDecision.next == Halo4FloatingRecordPhase::AwaitSequenceBoundary,
            "The immediately following flag-1 record consumes the same-eye hand delta as the held model");
        recordPhase = recordDecision.next;
        recordDecision = Halo4SelectFloatingRecord(recordPhase, 0, 120);
        Check(recordDecision.action == Halo4FloatingRecordAction::Stock &&
                  recordDecision.next == Halo4FloatingRecordPhase::AwaitStormHands,
            "The flag-0/120 native body stays stock and closes the record sequence");
        Check(Halo4SelectFloatingRecord(Halo4FloatingRecordPhase::AwaitStormHands, 1, 5).action ==
                      Halo4FloatingRecordAction::Stock &&
                  Halo4SelectFloatingRecord(Halo4FloatingRecordPhase::AwaitSequenceBoundary, 1, 80).action ==
                      Halo4FloatingRecordAction::Stock &&
                  Halo4SelectFloatingRecord(Halo4FloatingRecordPhase::AwaitStormHands, -1, 80).action ==
                      Halo4FloatingRecordAction::Stock,
            "A reversed, late, unreadable, or count-mismatched record can never inherit Storm ownership");
        Check(kHalo4StormFpRuntimeImportChecksum == 0x150D0000u && kHalo4FirstPersonSkinningRecordStride == 0x1910u,
            "The official Storm checksum is telemetry and exact record adjacency is the held-object ownership key");
        Check(Halo4ExpectedHeldRecordSource(0x100000u) == 0x101910u && Halo4ExpectedHeldRecordSource(0) == 0 &&
                  Halo4ExpectedHeldRecordSource(~uintptr_t{0}) == 0,
            "Held-model ownership requires exact forward 0x1910 adjacency and refuses null or overflowing sources");

        // Floating hands are a tested final-palette ownership map, not a
        // scale-everything guess inside the render detour.
        int rightHands = 0;
        int leftHands = 0;
        int collapseRight = 0;
        int collapseLeft = 0;
        int hidden = 0;
        for (int node = 0; node < kHalo4StormFpBodyNodeCount; ++node)
        {
            switch (Halo4ClassifyFloatingNode(node))
            {
            case Halo4FloatingNodeRole::RightHand:
                ++rightHands;
                break;
            case Halo4FloatingNodeRole::LeftHand:
                ++leftHands;
                break;
            case Halo4FloatingNodeRole::CollapseAtRightWrist:
                ++collapseRight;
                break;
            case Halo4FloatingNodeRole::CollapseAtLeftWrist:
                ++collapseLeft;
                break;
            case Halo4FloatingNodeRole::Hidden:
                ++hidden;
                break;
            case Halo4FloatingNodeRole::OutsideBody:
                Check(false, "Every Storm body node has a floating-hands role");
                break;
            }
        }
        Check(rightHands == 23 && leftHands == 20 && collapseRight == 16 && collapseLeft == 16 && hidden == 5,
            "The H4EK Storm mask keeps 43 hand nodes, wrist-collapses 32 arm "
            "influences, and hides the five unrelated body nodes");
        Check(Halo4ClassifyFloatingNode(kHalo4RightHandNode) == Halo4FloatingNodeRole::RightHand &&
                  Halo4ClassifyFloatingNode(kHalo4LeftHandNode) == Halo4FloatingNodeRole::LeftHand &&
                  kHalo4LeftThumbBaseNode == 46 &&
                  Halo4StormNodeInSet(kHalo4LeftHandSubtree, kHalo4LeftThumbBaseNode) &&
                  Halo4ClassifyFloatingNode(kHalo4RightElbowNode) == Halo4FloatingNodeRole::CollapseAtRightWrist &&
                  Halo4ClassifyFloatingNode(kHalo4LeftElbowNode) == Halo4FloatingNodeRole::CollapseAtLeftWrist,
            "Both wrists stay visible while each cross-weighted forearm "
            "collapses at its own solved wrist");
        Check(Halo4ClassifyFloatingNode(-1) == Halo4FloatingNodeRole::OutsideBody &&
                  Halo4ClassifyFloatingNode(kHalo4StormFpBodyNodeCount) == Halo4FloatingNodeRole::OutsideBody,
            "The floating-hands map never claims a node outside storm_fp");
        bool rightClosureExact = true;
        for (int node : kHalo4RightShoulderSubtree)
        {
            const auto role = Halo4ClassifyFloatingNode(node);
            rightClosureExact = rightClosureExact && (role == Halo4FloatingNodeRole::RightHand ||
                                                         role == Halo4FloatingNodeRole::CollapseAtRightWrist);
        }
        bool leftClosureExact = true;
        for (int node : kHalo4LeftShoulderSubtree)
        {
            const auto role = Halo4ClassifyFloatingNode(node);
            leftClosureExact = leftClosureExact && (role == Halo4FloatingNodeRole::LeftHand ||
                                                       role == Halo4FloatingNodeRole::CollapseAtLeftWrist);
        }
        Check(rightClosureExact && leftClosureExact, "Every H4EK arm descendant is either kept as a hand or co-located "
                                                     "with the matching wrist; no ribbon influence is left behind");

        // Dormant helpers from that earlier routing remain pinned so history cannot silently
        // change, but the live path never calls this previous-pair cache path.
        Check(Halo4FloatingHandCacheIsCurrent(7, 100, 7, 100) && Halo4FloatingHandCacheIsCurrent(7, 100, 7, 99),
            "The gun accepts only this pair's or the immediately prior body's "
            "untouched local wrist relation");
        Check(!Halo4FloatingHandCacheIsCurrent(7, 100, 7, 98) && !Halo4FloatingHandCacheIsCurrent(7, 100, 7, 101) &&
                  !Halo4FloatingHandCacheIsCurrent(7, 100, 8, 100) &&
                  !Halo4FloatingHandCacheIsCurrent(0, 100, 0, 100) && !Halo4FloatingHandCacheIsCurrent(7, 0, 7, 0),
            "A stale, future, cross-generation, or unkeyed body relation can "
            "never move a Halo 4 weapon record");
        Check(Halo4FloatingRelationPairIsCurrent(3, 7, 100, true, 3, 7, 99, true, 3, 7, 99) &&
                  !Halo4FloatingRelationPairIsCurrent(3, 7, 100, true, 3, 7, 99, false, 3, 7, 99) &&
                  !Halo4FloatingRelationPairIsCurrent(3, 7, 100, true, 3, 7, 99, true, 3, 7, 98) &&
                  !Halo4FloatingRelationPairIsCurrent(3, 7, 100, true, 3, 7, 99, true, 4, 7, 99),
            "Stereo gun admission accepts only a complete, same-serial, "
            "same-epoch relation pair");

        // Dormant invariant from that earlier routing: BODY wrists are world-rooted. Cache
        // inverse(eye)*stock, reconstruct through each eye, and derive both
        // body and gun deltas against one shared physical target.
        Halo4FloatingTransform eye0{}, eye1{}, localWrist{}, targetWorld{};
        eye0.translation[0] = -0.032f;
        eye1.translation[0] = 0.032f;
        // A non-yaw camera root does not commute with the wrist yaw.
        // Keeping both eyes at identity here would let an order/transpose bug
        // hide behind yaw-only algebra.
        const float eyePitch = 0.42f;
        const float ce = cosf(eyePitch);
        const float se = sinf(eyePitch);
        eye0.rotation[4] = ce;
        eye0.rotation[5] = se;
        eye0.rotation[7] = -se;
        eye0.rotation[8] = ce;
        const float eye1Pitch = -0.23f;
        const float ce1 = cosf(eye1Pitch);
        const float se1 = sinf(eye1Pitch);
        eye1.rotation[4] = ce1;
        eye1.rotation[5] = se1;
        eye1.rotation[7] = -se1;
        eye1.rotation[8] = ce1;
        localWrist.translation[0] = 0.45f;
        localWrist.translation[1] = -0.18f;
        localWrist.translation[2] = 0.08f;
        localWrist.scale = 1.25f;
        const float relationYaw = 0.35f;
        localWrist.rotation[0] = cosf(relationYaw);
        localWrist.rotation[1] = sinf(relationYaw);
        localWrist.rotation[3] = -sinf(relationYaw);
        localWrist.rotation[4] = cosf(relationYaw);
        targetWorld.translation[0] = 4.25f;
        targetWorld.translation[1] = -1.50f;
        targetWorld.translation[2] = 2.10f;
        targetWorld.scale = localWrist.scale;
        const float targetPitch = -0.6f;
        const float ct = cosf(targetPitch);
        const float st = sinf(targetPitch);
        targetWorld.rotation[0] = ct;
        targetWorld.rotation[2] = -st;
        targetWorld.rotation[6] = st;
        targetWorld.rotation[8] = ct;

        Halo4FloatingTransform stockEye0{}, stockEye1{};
        Check(Halo4ComposeFloatingTransforms(eye0, localWrist, stockEye0) &&
                  Halo4ComposeFloatingTransforms(eye1, localWrist, stockEye1),
            "Each stock BODY wrist is currentEye times one eye-local relation");
        Check(fabsf(stockEye0.translation[0] - (-0.032f + 0.45f)) < 1.0e-5f &&
                  fabsf(stockEye1.translation[0] - (0.032f + 0.45f)) < 1.0e-5f &&
                  fabsf(stockEye0.translation[1] - (ce * -0.18f - se * 0.08f)) < 1.0e-5f &&
                  fabsf(stockEye0.translation[2] - (se * -0.18f + ce * 0.08f)) < 1.0e-5f &&
                  fabsf(stockEye0.rotation[0] - cosf(relationYaw)) < 1.0e-5f &&
                  fabsf(stockEye0.rotation[1] - ce * sinf(relationYaw)) < 1.0e-5f &&
                  fabsf(stockEye0.rotation[2] - se * sinf(relationYaw)) < 1.0e-5f &&
                  fabsf(stockEye0.rotation[7] + se) < 1.0e-5f && fabsf(stockEye0.scale - 1.25f) < 1.0e-5f,
            "The eye-times-wrist result matches the manually expanded "
            "noncommuting column-basis transform and non-unit scale");
        Halo4FloatingTransform cached0{}, cached1{};
        Check(Halo4BuildFloatingEyeLocalWrist(eye0, stockEye0, cached0) &&
                  Halo4BuildFloatingEyeLocalWrist(eye1, stockEye1, cached1),
            "The body transaction strips only the current eye from stock");
        for (int i = 0; i < 9; ++i)
            Check(fabsf(cached0.rotation[i] - cached1.rotation[i]) < 1.0e-5f &&
                      fabsf(cached0.rotation[i] - localWrist.rotation[i]) < 1.0e-5f,
                "Both eyes cache the same stock-wrist orientation relation");
        for (int i = 0; i < 3; ++i)
            Check(fabsf(cached0.translation[i] - cached1.translation[i]) < 1.0e-5f &&
                      fabsf(cached0.translation[i] - localWrist.translation[i]) < 1.0e-5f,
                "Both eyes cache the same stock-wrist position relation");
        Check(fabsf(cached0.scale - localWrist.scale) < 1.0e-5f && fabsf(cached1.scale - localWrist.scale) < 1.0e-5f,
            "Eye-local reconstruction preserves the authored wrist scale");

        const auto landsOnTarget = [&](const Halo4FloatingTransform& eyeRoot, const Halo4FloatingTransform& stock,
                                       const Halo4FloatingTransform& cached)
        {
            Halo4FloatingTransform bodyDelta{}, bodyResult{};
            Halo4FloatingTransform rebuiltStock{}, gunDelta{}, gunResult{};
            return Halo4BuildFloatingWorldDelta(targetWorld, stock, bodyDelta) &&
                   Halo4ComposeFloatingTransforms(bodyDelta, stock, bodyResult) &&
                   Halo4ComposeFloatingTransforms(eyeRoot, cached, rebuiltStock) &&
                   Halo4BuildFloatingWorldDelta(targetWorld, rebuiltStock, gunDelta) &&
                   Halo4ComposeFloatingTransforms(gunDelta, rebuiltStock, gunResult) && [&]()
            {
                for (int i = 0; i < 9; ++i)
                    if (fabsf(bodyResult.rotation[i] - targetWorld.rotation[i]) >= 1.0e-5f ||
                        fabsf(gunResult.rotation[i] - targetWorld.rotation[i]) >= 1.0e-5f)
                        return false;
                for (int i = 0; i < 3; ++i)
                    if (fabsf(bodyResult.translation[i] - targetWorld.translation[i]) >= 1.0e-5f ||
                        fabsf(gunResult.translation[i] - targetWorld.translation[i]) >= 1.0e-5f)
                        return false;
                return fabsf(bodyResult.scale - targetWorld.scale) < 1.0e-5f &&
                       fabsf(gunResult.scale - targetWorld.scale) < 1.0e-5f;
            }();
        };
        Check(landsOnTarget(eye0, stockEye0, cached0) && landsOnTarget(eye1, stockEye1, cached1),
            "The dormant C-H4-34 direct BODY and reconstructed-gun algebra remains internally exact");
        Halo4FloatingTransform heldLocal{}, heldWorld{}, sameFrameDelta{};
        Halo4FloatingTransform movedHeld{}, expectedHeld{};
        heldLocal.translation[0] = 0.21f;
        heldLocal.translation[1] = -0.04f;
        heldLocal.translation[2] = 0.07f;
        Check(Halo4ComposeFloatingTransforms(stockEye0, heldLocal, heldWorld) &&
                  Halo4BuildFloatingWorldDelta(targetWorld, stockEye0, sameFrameDelta) &&
                  Halo4ComposeFloatingTransforms(sameFrameDelta, heldWorld, movedHeld) &&
                  Halo4ComposeFloatingTransforms(targetWorld, heldLocal, expectedHeld),
            "The current Storm wrist delta composes over the immediately adjacent held graph");
        bool heldMatches = true;
        for (int i = 0; i < 9; ++i)
            heldMatches = heldMatches && fabsf(movedHeld.rotation[i] - expectedHeld.rotation[i]) < 1.0e-5f;
        for (int i = 0; i < 3; ++i)
            heldMatches = heldMatches && fabsf(movedHeld.translation[i] - expectedHeld.translation[i]) < 1.0e-5f;
        Check(heldMatches, "Hands and gun share one same-eye rigid motion and preserve the authored grip relation");

        Halo4FloatingTransform rigidSupportTarget{};
        Check(Halo4BuildFloatingRigidSupportTarget(targetWorld, stockEye0, heldWorld, rigidSupportTarget),
            "Halo 4 two-hand support accepts the right-hand rigid world motion");
        bool rigidSupportMatchesHeld = true;
        for (int i = 0; i < 9; ++i)
            rigidSupportMatchesHeld =
                rigidSupportMatchesHeld && fabsf(rigidSupportTarget.rotation[i] - expectedHeld.rotation[i]) < 1.0e-5f;
        for (int i = 0; i < 3; ++i)
            rigidSupportMatchesHeld = rigidSupportMatchesHeld &&
                                      fabsf(rigidSupportTarget.translation[i] - expectedHeld.translation[i]) < 1.0e-5f;
        Check(rigidSupportMatchesHeld && fabsf(rigidSupportTarget.scale - expectedHeld.scale) < 1.0e-5f,
            "Halo 4 two-hand support preserves the authored hand-to-weapon relation instead of following left-controller translation");
        Halo4FloatingTransform invalidRigidSupport = stockEye0;
        invalidRigidSupport.translation[1] = std::numeric_limits<float>::quiet_NaN();
        Halo4FloatingTransform untouchedRigidSupport{};
        untouchedRigidSupport.translation[0] = 71.0f;
        Check(
            !Halo4BuildFloatingRigidSupportTarget(targetWorld, invalidRigidSupport, heldWorld, untouchedRigidSupport) &&
                untouchedRigidSupport.translation[0] == 71.0f,
            "an invalid Halo 4 rigid support relation publishes no partial target");

        // Replace only the current eye's orientation parent.  The
        // live eye-local wrist relation is the title's own authored mount; a
        // Blender bind/control quaternion is not.  Pitch(E), yaw(L), and
        // pitch(C) deliberately do not all commute.
        const float physicalTarget[3] = {
            targetWorld.translation[0], targetWorld.translation[1], targetWorld.translation[2]};
        Halo4FloatingTransform controllerCarrier{};
        Check(Halo4BuildFloatingControllerCarrier(
                  targetWorld.rotation, physicalTarget, false, 31.0f, -17.0f, 23.0f, controllerCarrier),
            "The production Halo 4 right carrier accepts a calibrated aim basis");
        bool rightCarrierUnchanged = true;
        for (int i = 0; i < 9; ++i)
            rightCarrierUnchanged =
                rightCarrierUnchanged && fabsf(controllerCarrier.rotation[i] - targetWorld.rotation[i]) < 1.0e-6f;
        Check(rightCarrierUnchanged,
            "The right carrier is exactly the aim basis: gun angles are never applied twice and no Blender seed survives");

        constexpr float carrierYawDeg = 31.0f;
        constexpr float carrierPitchDeg = -17.0f;
        constexpr float carrierRollDeg = 23.0f;
        Halo4FloatingTransform leftCarrier{};
        Check(Halo4BuildFloatingControllerCarrier(targetWorld.rotation, physicalTarget, true, carrierYawDeg,
                  carrierPitchDeg, carrierRollDeg, leftCarrier),
            "The dormant C-H4-36 left-carrier branch retains its mirrored presentation-trim algebra");
        constexpr float testDegreesToRadians = 0.01745329252f;
        const float leftYaw = -carrierYawDeg * testDegreesToRadians;
        const float leftPitch = carrierPitchDeg * testDegreesToRadians;
        const float leftRoll = -carrierRollDeg * testDegreesToRadians;
        const float lcp = cosf(leftPitch), lsp = sinf(leftPitch);
        const float lcy = cosf(leftYaw), lsy = sinf(leftYaw);
        const float lcr = cosf(leftRoll), lsr = sinf(leftRoll);
        const float leftForward[3] = {lcp * lcy, lcp * lsy, lsp};
        const float leftUp[3] = {(-lsp * lcy) * lcr + lsy * lsr, (-lsp * lsy) * lcr - lcy * lsr, lcp * lcr};
        Halo4FloatingTransform expectedLeftMount{};
        memcpy(expectedLeftMount.rotation, leftForward, sizeof(leftForward));
        expectedLeftMount.rotation[3] = leftUp[1] * leftForward[2] - leftUp[2] * leftForward[1];
        expectedLeftMount.rotation[4] = leftUp[2] * leftForward[0] - leftUp[0] * leftForward[2];
        expectedLeftMount.rotation[5] = leftUp[0] * leftForward[1] - leftUp[1] * leftForward[0];
        memcpy(expectedLeftMount.rotation + 6, leftUp, sizeof(leftUp));
        Halo4FloatingTransform rawController{};
        memcpy(rawController.rotation, targetWorld.rotation, sizeof(rawController.rotation));
        memcpy(rawController.translation, physicalTarget, sizeof(rawController.translation));
        Halo4FloatingTransform expectedLeftCarrier{};
        Check(Halo4ComposeFloatingTransforms(rawController, expectedLeftMount, expectedLeftCarrier),
            "The independent left-trim reference is a proper noncommuting basis");
        bool leftCarrierMatches = true;
        for (int i = 0; i < 9; ++i)
            leftCarrierMatches =
                leftCarrierMatches && fabsf(leftCarrier.rotation[i] - expectedLeftCarrier.rotation[i]) < 1.0e-5f;
        Check(leftCarrierMatches,
            "The left carrier postmultiplies exactly (-yaw, +pitch, -roll), matching Halo 3, ODST and Reach");

        // This replaces that unaccepted Halo 4 left mount with one
        // prepared-frame parent policy. Free uses the raw left controller;
        // support copies only the frozen right-aim rotation while retaining
        // the already-working left physical position and scale.
        const float leftPhysicalTarget[3] = {
            physicalTarget[0] + 0.37f, physicalTarget[1] - 0.21f, physicalTarget[2] + 0.12f};
        Halo4FloatingTransform rawLeftStateCarrier{};
        Check(Halo4BuildFloatingControllerCarrier(eye1.rotation, leftPhysicalTarget, false, carrierYawDeg,
                  carrierPitchDeg, carrierRollDeg, rawLeftStateCarrier),
            "The C-H4-38 free-left carrier accepts the raw prepared controller with no borrowed gun trim");
        bool rawLeftUntrimmed = true;
        for (int i = 0; i < 9; ++i)
            rawLeftUntrimmed = rawLeftUntrimmed && fabsf(rawLeftStateCarrier.rotation[i] - eye1.rotation[i]) < 1.0e-6f;
        Check(rawLeftUntrimmed, "Nonzero universal gun angles do not cant the independent Halo 4 hand");

        Halo4FloatingTransform selectedFreeCarrier{};
        Halo4FloatingTransform selectedSupportCarrier{};
        Check(
            Halo4BuildFloatingLeftCarrierForState(false, rawLeftStateCarrier, controllerCarrier, selectedFreeCarrier) &&
                memcmp(&selectedFreeCarrier, &rawLeftStateCarrier, sizeof(selectedFreeCarrier)) == 0,
            "Free mode preserves the raw left-controller carrier byte-for-byte");
        Check(
            Halo4BuildFloatingLeftCarrierForState(true, rawLeftStateCarrier, controllerCarrier, selectedSupportCarrier),
            "Support mode accepts the exact frozen right-aim carrier");
        Halo4FloatingTransform closestFreeCarrier{}, closestSupportCarrier{};
        Halo4FloatingTransform expectedClosestFreeCarrier{};
        Check(Halo4BuildFloatingClosestLeftCarrierForState(false, rawLeftStateCarrier, controllerCarrier, carrierYawDeg,
                  carrierPitchDeg, carrierRollDeg, closestFreeCarrier) &&
                  Halo4BuildFloatingControllerCarrier(rawLeftStateCarrier.rotation, rawLeftStateCarrier.translation,
                      true, carrierYawDeg, carrierPitchDeg, carrierRollDeg, expectedClosestFreeCarrier) &&
                  memcmp(&closestFreeCarrier, &expectedClosestFreeCarrier, sizeof(closestFreeCarrier)) == 0,
            "C-H4-42 free mode restores C-H4-37's mirrored left-controller carrier exactly");
        Check(Halo4BuildFloatingClosestLeftCarrierForState(true, rawLeftStateCarrier, controllerCarrier, carrierYawDeg,
                  carrierPitchDeg, carrierRollDeg, closestSupportCarrier) &&
                  memcmp(&closestSupportCarrier, &selectedSupportCarrier, sizeof(closestSupportCarrier)) == 0,
            "C-H4-42 support remains byte-identical to the accepted C-H4-38 right-aim parent");
        bool supportUsesRightAim = true;
        for (int i = 0; i < 9; ++i)
            supportUsesRightAim = supportUsesRightAim &&
                                  fabsf(selectedSupportCarrier.rotation[i] - controllerCarrier.rotation[i]) < 1.0e-6f;
        Check(supportUsesRightAim && selectedSupportCarrier.translation[0] == rawLeftStateCarrier.translation[0] &&
                  selectedSupportCarrier.translation[1] == rawLeftStateCarrier.translation[1] &&
                  selectedSupportCarrier.translation[2] == rawLeftStateCarrier.translation[2] &&
                  selectedSupportCarrier.scale == rawLeftStateCarrier.scale,
            "Support copies only the gun parent rotation and cannot move the left hand");
        Halo4FloatingTransform invalidStateRight = controllerCarrier;
        invalidStateRight.rotation[0] = std::numeric_limits<float>::quiet_NaN();
        Halo4FloatingTransform untouchedStateCarrier{};
        untouchedStateCarrier.translation[0] = 123.0f;
        Check(!Halo4BuildFloatingLeftCarrierForState(
                  true, rawLeftStateCarrier, invalidStateRight, untouchedStateCarrier) &&
                  untouchedStateCarrier.translation[0] == 123.0f,
            "An invalid support parent publishes no partial left carrier");
        Check(Halo4BuildFloatingLeftCarrierForState(
                  false, rawLeftStateCarrier, invalidStateRight, untouchedStateCarrier) &&
                  memcmp(&untouchedStateCarrier, &rawLeftStateCarrier, sizeof(untouchedStateCarrier)) == 0,
            "Free mode has no dependency on an unused support orientation");

        Halo4FloatingTransform rerooted0{}, rerooted1{};
        Check(Halo4BuildFloatingControllerRerootTarget(controllerCarrier, eye0, stockEye0, rerooted0) &&
                  Halo4BuildFloatingControllerRerootTarget(controllerCarrier, eye1, stockEye1, rerooted1),
            "Each current eye builds a controller-facing Halo 4 wrist from its live stock relation");
        Halo4FloatingTransform expectedReroot{};
        Check(Halo4ComposeFloatingTransforms(controllerCarrier, localWrist, expectedReroot),
            "The controller-times-live-wrist reference is valid");
        bool rerootRotationMatches = true;
        for (int i = 0; i < 9; ++i)
            rerootRotationMatches = rerootRotationMatches &&
                                    fabsf(rerooted0.rotation[i] - expectedReroot.rotation[i]) < 1.0e-5f &&
                                    fabsf(rerooted1.rotation[i] - expectedReroot.rotation[i]) < 1.0e-5f;
        Check(rerootRotationMatches,
            "The final wrist faces as controller times the live authored wrist, with no fixed seed rotation");
        Check(rerooted0.translation[0] == controllerCarrier.translation[0] &&
                  rerooted0.translation[1] == controllerCarrier.translation[1] &&
                  rerooted0.translation[2] == controllerCarrier.translation[2] &&
                  fabsf(rerooted0.scale - stockEye0.scale) < 1.0e-6f,
            "Orientation rerooting preserves C-H4-35's physical target position and the stock wrist scale");
        // The logged eye-local wrist is the authored relation in either
        // eye, and its quaternion rebuilds it.
        float eyeLocal0[9]{}, eyeLocal1[9]{}, eyeLocalQ[4]{}, eyeLocalBack[9]{};
        Check(Halo4EyeLocalWristBasis(eye0.rotation, stockEye0.rotation, eyeLocal0) &&
                  Halo4EyeLocalWristBasis(eye1.rotation, stockEye1.rotation, eyeLocal1) &&
                  Halo4BlamBasisToQuaternion(eyeLocal0, eyeLocalQ) &&
                  Halo4QuaternionToBlamBasis(eyeLocalQ, eyeLocalBack),
            "The eye-local wrist relation and its quaternion are built");
        bool eyeLocalMatches = eyeLocalQ[3] >= 0.0f;
        for (int i = 0; i < 9; ++i)
            eyeLocalMatches = eyeLocalMatches && fabsf(eyeLocal0[i] - localWrist.rotation[i]) < 1.0e-5f &&
                              fabsf(eyeLocal1[i] - localWrist.rotation[i]) < 1.0e-5f &&
                              fabsf(eyeLocalBack[i] - eyeLocal0[i]) < 1.0e-5f;
        Check(eyeLocalMatches,
            "The eye-local wrist equals the authored relation in both eyes and round-trips through its quaternion");
        // Each branch of the basis-to-quaternion conversion (trace and the three axes).
        bool quaternionBranches = true;
        const float branchQ[4][4] = {{0.1f, 0.2f, 0.3f, 0.927f}, {0.97f, 0.1f, 0.2f, 0.05f}, {0.1f, 0.97f, 0.2f, 0.05f},
            {0.1f, 0.2f, 0.97f, 0.05f}};
        for (const auto& source : branchQ)
        {
            float basis[9]{}, q[4]{}, again[9]{};
            quaternionBranches = quaternionBranches && Halo4QuaternionToBlamBasis(source, basis) &&
                                 Halo4BlamBasisToQuaternion(basis, q) && Halo4QuaternionToBlamBasis(q, again) &&
                                 q[3] >= 0.0f;
            for (int i = 0; i < 9; ++i)
                quaternionBranches = quaternionBranches && fabsf(again[i] - basis[i]) < 1.0e-5f;
        }
        Check(quaternionBranches, "Basis-to-quaternion inverts every branch of the conversion");

        Halo4FloatingTransform inverseEye0{}, expectedRerootDelta{};
        Halo4FloatingTransform rerootDelta{};
        Check(Halo4InvertFloatingTransform(eye0, inverseEye0) &&
                  Halo4ComposeFloatingTransforms(controllerCarrier, inverseEye0, expectedRerootDelta) &&
                  Halo4BuildFloatingWorldDelta(rerooted0, stockEye0, rerootDelta),
            "The final target produces one valid current-eye controller delta");
        bool rerootDeltaMatches = true;
        for (int i = 0; i < 9; ++i)
            rerootDeltaMatches =
                rerootDeltaMatches && fabsf(rerootDelta.rotation[i] - expectedRerootDelta.rotation[i]) < 1.0e-5f;
        Check(rerootDeltaMatches, "The wrist delta cancels to controller times inverse current-eye orientation");

        Halo4FloatingTransform freeStateReroot{};
        Check(Halo4BuildFloatingControllerRerootTarget(selectedFreeCarrier, eye0, stockEye0, freeStateReroot),
            "The C-H4-38 free hand reroots from the raw left-controller parent");
        Halo4FloatingTransform closestFreeReroot{};
        Check(Halo4BuildFloatingControllerRerootTarget(closestFreeCarrier, eye0, stockEye0, closestFreeReroot),
            "C-H4-42 restores the exact C-H4-37 mirrored-carrier current-eye reroot");
        Halo4FloatingTransform parityFreeTarget{}, expectedParityCarrier{};
        Check(Halo4BuildFloatingFreeLeftParityTarget(selectedFreeCarrier, carrierYawDeg, carrierPitchDeg,
                  carrierRollDeg, freeStateReroot, parityFreeTarget) &&
                  Halo4BuildFloatingControllerCarrier(selectedFreeCarrier.rotation, selectedFreeCarrier.translation,
                      true, carrierYawDeg, carrierPitchDeg, carrierRollDeg, expectedParityCarrier),
            "C-H4-40 builds the free wrist with the shared H3/ODST/Reach left-controller mount");
        bool parityFreeRotationMatches = true;
        for (int i = 0; i < 9; ++i)
            parityFreeRotationMatches = parityFreeRotationMatches && fabsf(parityFreeTarget.rotation[i] -
                                                                           expectedParityCarrier.rotation[i]) < 1.0e-6f;
        Check(parityFreeRotationMatches,
            "The free wrist equals controller times mirrored trim directly, without a camera-local wrist or finger-to-aim mapping");
        Check(parityFreeTarget.translation[0] == freeStateReroot.translation[0] &&
                  parityFreeTarget.translation[1] == freeStateReroot.translation[1] &&
                  parityFreeTarget.translation[2] == freeStateReroot.translation[2] &&
                  parityFreeTarget.scale == freeStateReroot.scale,
            "The C-H4-40 parity mount preserves C-H4-38 free-hand placement and stock scale exactly");
        bool parityDoesNotRetainEyeWrist = false;
        for (int i = 0; i < 9; ++i)
            parityDoesNotRetainEyeWrist = parityDoesNotRetainEyeWrist ||
                                          fabsf(parityFreeTarget.rotation[i] - freeStateReroot.rotation[i]) > 1.0e-3f;
        Check(parityDoesNotRetainEyeWrist,
            "The noncommuting fixture proves C-H4-40 removed the rejected live camera-local wrist relation");
        // This uses the named cross-title attachment frame, not wrist-bone
        // axis equality. Reach's official left_hand marker is identity on its
        // wrist. This is Halo 4's official left_hand marker quaternion on
        // identity child b_l_hand_marker_offset, converted to a Blam basis.
        const float halo4LeftHandMarkerBasis[9] = {0.0143071814f, -0.996801702f, -0.0786236117f, -0.983843685f,
            -0.0000000752f, -0.179029617f, 0.178457021f, 0.0799147531f, -0.980697060f};
        Halo4FloatingTransform markerParityTarget{};
        Check(Halo4BuildFloatingFreeLeftMarkerParityTarget(
                  expectedParityCarrier, halo4LeftHandMarkerBasis, freeStateReroot, markerParityTarget),
            "C-H4-43 solves Halo 4's wrist from its official left_hand marker and the Reach-style controller mount");
        Halo4FloatingTransform markerLocal{}, movedMarker{};
        memcpy(markerLocal.rotation, halo4LeftHandMarkerBasis, sizeof(markerLocal.rotation));
        Check(Halo4ComposeFloatingTransforms(markerParityTarget, markerLocal, movedMarker),
            "The solved Halo 4 wrist carries its named left_hand marker into world space");
        bool markerMatchesOtherTitles = true;
        for (int i = 0; i < 9; ++i)
            markerMatchesOtherTitles = markerMatchesOtherTitles &&
                                       fabsf(movedMarker.rotation[i] - expectedParityCarrier.rotation[i]) < 1.0e-5f;
        Check(markerMatchesOtherTitles,
            "Halo 4's left_hand marker—not its incompatible wrist bone axes—equals the H3/ODST/Reach controller target");
        Check(markerParityTarget.translation[0] == freeStateReroot.translation[0] &&
                  markerParityTarget.translation[1] == freeStateReroot.translation[1] &&
                  markerParityTarget.translation[2] == freeStateReroot.translation[2] &&
                  markerParityTarget.scale == freeStateReroot.scale,
            "Cross-title marker parity changes free-hand orientation only");
        float invalidMarkerBasis[9];
        memcpy(invalidMarkerBasis, halo4LeftHandMarkerBasis, sizeof(invalidMarkerBasis));
        invalidMarkerBasis[0] = std::numeric_limits<float>::quiet_NaN();
        Halo4FloatingTransform untouchedMarkerParity{};
        untouchedMarkerParity.translation[0] = 22.0f;
        Check(!Halo4BuildFloatingFreeLeftMarkerParityTarget(
                  expectedParityCarrier, invalidMarkerBasis, freeStateReroot, untouchedMarkerParity) &&
                  untouchedMarkerParity.translation[0] == 22.0f,
            "Invalid optional marker calibration publishes nothing over the C-H4-38 fallback");
        Halo4FloatingTransform untouchedParity{};
        untouchedParity.translation[0] = 44.0f;
        Check(!Halo4BuildFloatingFreeLeftParityTarget(selectedFreeCarrier, std::numeric_limits<float>::quiet_NaN(),
                  carrierPitchDeg, carrierRollDeg, freeStateReroot, untouchedParity) &&
                  untouchedParity.translation[0] == 44.0f,
            "Invalid optional C-H4-40 mount input publishes nothing over the C-H4-38 fallback");
        Halo4FloatingTransform supportLocalWrist{};
        supportLocalWrist.translation[0] = -0.16f;
        supportLocalWrist.translation[1] = 0.29f;
        supportLocalWrist.translation[2] = -0.05f;
        supportLocalWrist.scale = 0.92f;
        const float supportLocalRoll = -0.31f;
        supportLocalWrist.rotation[0] = cosf(supportLocalRoll);
        supportLocalWrist.rotation[1] = sinf(supportLocalRoll);
        supportLocalWrist.rotation[3] = -sinf(supportLocalRoll);
        supportLocalWrist.rotation[4] = cosf(supportLocalRoll);
        Halo4FloatingTransform stockSupportLeft{}, supportStateReroot{};
        Halo4FloatingTransform supportLeftDelta{};
        Check(Halo4ComposeFloatingTransforms(eye0, supportLocalWrist, stockSupportLeft) &&
                  Halo4BuildFloatingControllerRerootTarget(
                      selectedSupportCarrier, eye0, stockSupportLeft, supportStateReroot) &&
                  Halo4BuildFloatingWorldDelta(supportStateReroot, stockSupportLeft, supportLeftDelta),
            "The support hand consumes its own live wrist relation under the shared right-aim parent");
        bool supportDeltaMatchesGun = true;
        for (int i = 0; i < 9; ++i)
            supportDeltaMatchesGun =
                supportDeltaMatchesGun && fabsf(supportLeftDelta.rotation[i] - rerootDelta.rotation[i]) < 1.0e-5f;
        Check(supportDeltaMatchesGun,
            "Right wrist, held gun, and support hand receive one identical rotational parent change");
        Halo4FloatingTransform expectedSupportOrientation{};
        Check(Halo4ComposeFloatingTransforms(controllerCarrier, supportLocalWrist, expectedSupportOrientation),
            "The support reference composes right aim with the live Halo 4 left-wrist relation");
        bool supportRelationPreserved = true;
        for (int i = 0; i < 9; ++i)
            supportRelationPreserved =
                supportRelationPreserved &&
                fabsf(supportStateReroot.rotation[i] - expectedSupportOrientation.rotation[i]) < 1.0e-5f;
        Check(supportRelationPreserved,
            "Two-hand orientation preserves Halo 4's authored support relation instead of physical controller twist");

        // A held model has its own eye-local authored orientation, independent
        // of the Storm wrist bone axes.  The same delta must carry that exact
        // relation onto the controller frame.
        Halo4FloatingTransform heldEyeLocal{};
        const float heldRoll = -0.27f;
        heldEyeLocal.rotation[3] = 0.0f;
        heldEyeLocal.rotation[4] = cosf(heldRoll);
        heldEyeLocal.rotation[5] = sinf(heldRoll);
        heldEyeLocal.rotation[6] = 0.0f;
        heldEyeLocal.rotation[7] = -sinf(heldRoll);
        heldEyeLocal.rotation[8] = cosf(heldRoll);
        Halo4FloatingTransform heldFromEye{}, movedFromEye{};
        Halo4FloatingTransform expectedFromController{};
        Check(Halo4ComposeFloatingTransforms(eye0, heldEyeLocal, heldFromEye) &&
                  Halo4ComposeFloatingTransforms(rerootDelta, heldFromEye, movedFromEye) &&
                  Halo4ComposeFloatingTransforms(controllerCarrier, heldEyeLocal, expectedFromController),
            "The adjacent held graph composes through the production orientation reroot");
        bool heldFacesController = true;
        for (int i = 0; i < 9; ++i)
            heldFacesController =
                heldFacesController && fabsf(movedFromEye.rotation[i] - expectedFromController.rotation[i]) < 1.0e-5f;
        Check(heldFacesController,
            "The Halo 4 gun keeps its authored orientation while its eye-facing orientation parent becomes the aim controller");

        const float identityBasis[9] = {1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f};
        const float identityPosition[3] = {0.0f, 0.0f, 0.0f};
        Halo4FloatingTransform identityTarget{}, identityEye{}, identityWrist{};
        Check(Halo4BuildFloatingControllerCarrier(
                  identityBasis, identityPosition, true, 0.0f, 0.0f, 0.0f, identityTarget),
            "The dormant C-H4-36 left-carrier branch accepts a neutral controller and zero trim");
        Halo4FloatingTransform identityReroot{};
        Check(Halo4BuildFloatingControllerRerootTarget(identityTarget, identityEye, identityWrist, identityReroot),
            "A neutral left-hand relation produces a valid neutral target");
        bool neutralReroot = true;
        for (int i = 0; i < 9; ++i)
            neutralReroot = neutralReroot && fabsf(identityReroot.rotation[i] - identityTarget.rotation[i]) < 1.0e-6f;
        Check(neutralReroot, "No 49-degree Blender left-hand seed survives in the final neutral orientation");

        // The free palm turns over around Halo 4's live thumb ray,
        // never around a guessed controller axis. H4EK node 46 b_l_thumb1 is
        // a direct child of the wrist; its exact authored translation supplies
        // a stable non-axis-aligned thumb-outward ray that cannot wobble with
        // thumb2/thumb3 curl.
        const float thumbBaseOffset[3] = {0.0112261f, -0.00861943f, -0.01287f};
        float thumbAxis[3] = {thumbBaseOffset[0], thumbBaseOffset[1], thumbBaseOffset[2]};
        float thumbAxisLength =
            sqrtf(thumbAxis[0] * thumbAxis[0] + thumbAxis[1] * thumbAxis[1] + thumbAxis[2] * thumbAxis[2]);
        for (float& value : thumbAxis)
            value /= thumbAxisLength;
        Halo4FloatingTransform thumbLocal{};
        for (int axis = 0; axis < 3; ++axis)
            thumbLocal.translation[axis] = thumbBaseOffset[axis];
        Halo4FloatingTransform stockThumbBase{};
        Check(Halo4ComposeFloatingTransforms(stockEye0, thumbLocal, stockThumbBase),
            "The official direct-child thumb base can be represented in the live Storm wrist frame");
        Halo4FloatingTransform freeGripTarget{}, expectedFreeGrip{};
        Check(Halo4BuildFloatingFreeLeftGripTarget(selectedFreeCarrier, carrierYawDeg, carrierPitchDeg, carrierRollDeg,
                  stockEye0, stockThumbBase, freeStateReroot, freeGripTarget) &&
                  Halo4BuildFloatingLeftPresentationTarget(
                      false, stockEye0, stockThumbBase, parityFreeTarget, expectedFreeGrip),
            "C-H4-41 composes the parity controller grip with the proven live-thumb turnover");
        Check(memcmp(&freeGripTarget, &expectedFreeGrip, sizeof(freeGripTarget)) == 0,
            "The production free grip is exactly the C-H4-40 heading followed by the C-H4-37 back-facing turnover");
        Check(freeGripTarget.translation[0] == freeStateReroot.translation[0] &&
                  freeGripTarget.translation[1] == freeStateReroot.translation[1] &&
                  freeGripTarget.translation[2] == freeStateReroot.translation[2] &&
                  freeGripTarget.scale == freeStateReroot.scale,
            "C-H4-41 changes only free-hand orientation and preserves the accepted placement");
        Halo4FloatingTransform restoredClosestFree{};
        Check(Halo4BuildFloatingLeftPresentationTarget(
                  false, stockEye0, stockThumbBase, closestFreeReroot, restoredClosestFree),
            "C-H4-42 completes the restored C-H4-37 free pose with its original live-thumb turnover");
        Halo4FloatingTransform freePalmTarget{};
        Check(
            Halo4BuildFloatingLeftPresentationTarget(false, stockEye0, stockThumbBase, freeStateReroot, freePalmTarget),
            "The production free-left policy accepts a live non-axis-aligned thumb ray");
        Check(freePalmTarget.translation[0] == freeStateReroot.translation[0] &&
                  freePalmTarget.translation[1] == freeStateReroot.translation[1] &&
                  freePalmTarget.translation[2] == freeStateReroot.translation[2] &&
                  freePalmTarget.scale == freeStateReroot.scale,
            "The free-palm correction changes orientation only");
        const auto rotateDirection = [](const float basis[9], const float local[3], float world[3])
        {
            for (int row = 0; row < 3; ++row)
            {
                world[row] = 0.0f;
                for (int column = 0; column < 3; ++column)
                    world[row] += basis[column * 3 + row] * local[column];
            }
        };
        float thumbBefore[3]{}, thumbAfter[3]{};
        rotateDirection(freeStateReroot.rotation, thumbAxis, thumbBefore);
        rotateDirection(freePalmTarget.rotation, thumbAxis, thumbAfter);
        Check(fabsf(thumbBefore[0] - thumbAfter[0]) < 1.0e-5f && fabsf(thumbBefore[1] - thumbAfter[1]) < 1.0e-5f &&
                  fabsf(thumbBefore[2] - thumbAfter[2]) < 1.0e-5f,
            "Turning the free palm preserves the stable thumb-base outward direction exactly");
        float parityThumb[3]{}, gripThumb[3]{};
        rotateDirection(parityFreeTarget.rotation, thumbAxis, parityThumb);
        rotateDirection(freeGripTarget.rotation, thumbAxis, gripThumb);
        Check(fabsf(parityThumb[0] - gripThumb[0]) < 1.0e-5f && fabsf(parityThumb[1] - gripThumb[1]) < 1.0e-5f &&
                  fabsf(parityThumb[2] - gripThumb[2]) < 1.0e-5f,
            "The corrected grip turns the glove without swapping the reference thumb inward");

        // H4EK node 43 b_l_middle1 is another direct child of b_l_hand.
        // Its exact authored ray and the thumb-base ray define the title's
        // stable palm plane rather than an arbitrary algebra-only vector.
        const float middleBaseOffset[3] = {-0.00297f, -0.03872f, -0.00605f};
        Halo4FloatingTransform middleLocal{};
        for (int axis = 0; axis < 3; ++axis)
            middleLocal.translation[axis] = middleBaseOffset[axis];
        Halo4FloatingTransform stockMiddleBase{};
        Check(Halo4ComposeFloatingTransforms(stockEye0, middleLocal, stockMiddleBase),
            "The official direct-child middle base can be represented in the live Storm wrist frame");

        // This replaces only the rejected free-hand heading. The live
        // direct-child middle and thumb rays form an anatomical basis whose
        // transpose maps fingers to controller-forward, thumb outward to
        // controller-left, and the back of the hand to controller-up.
        float fingerForward[3] = {middleBaseOffset[0], middleBaseOffset[1], middleBaseOffset[2]};
        float fingerLength = sqrtf(fingerForward[0] * fingerForward[0] + fingerForward[1] * fingerForward[1] +
                                   fingerForward[2] * fingerForward[2]);
        for (float& value : fingerForward)
            value /= fingerLength;
        float thumbAlongFinger = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
            thumbAlongFinger += thumbAxis[axis] * fingerForward[axis];
        float thumbOutward[3] = {thumbAxis[0] - thumbAlongFinger * fingerForward[0],
            thumbAxis[1] - thumbAlongFinger * fingerForward[1], thumbAxis[2] - thumbAlongFinger * fingerForward[2]};
        float outwardLength = sqrtf(
            thumbOutward[0] * thumbOutward[0] + thumbOutward[1] * thumbOutward[1] + thumbOutward[2] * thumbOutward[2]);
        for (float& value : thumbOutward)
            value /= outwardLength;
        const float anatomyUp[3] = {fingerForward[1] * thumbOutward[2] - fingerForward[2] * thumbOutward[1],
            fingerForward[2] * thumbOutward[0] - fingerForward[0] * thumbOutward[2],
            fingerForward[0] * thumbOutward[1] - fingerForward[1] * thumbOutward[0]};
        Halo4FloatingTransform freeAnatomicalTarget{};
        Check(Halo4BuildFloatingFreeLeftAnatomicalTarget(selectedFreeCarrier, stockEye0, stockThumbBase,
                  stockMiddleBase, freePalmTarget, freeAnatomicalTarget),
            "C-H4-39 builds the free wrist from Halo 4's live middle/thumb anatomy");
        Check(freeAnatomicalTarget.translation[0] == freePalmTarget.translation[0] &&
                  freeAnatomicalTarget.translation[1] == freePalmTarget.translation[1] &&
                  freeAnatomicalTarget.translation[2] == freePalmTarget.translation[2] &&
                  freeAnatomicalTarget.scale == freePalmTarget.scale,
            "The C-H4-39 anatomical correction changes free-hand orientation only");
        float mappedFinger[3]{}, mappedOutward[3]{}, mappedUp[3]{};
        rotateDirection(freeAnatomicalTarget.rotation, fingerForward, mappedFinger);
        rotateDirection(freeAnatomicalTarget.rotation, thumbOutward, mappedOutward);
        rotateDirection(freeAnatomicalTarget.rotation, anatomyUp, mappedUp);
        bool anatomyMapsToController = true;
        for (int row = 0; row < 3; ++row)
            anatomyMapsToController = anatomyMapsToController &&
                                      fabsf(mappedFinger[row] - selectedFreeCarrier.rotation[row]) < 1.0e-5f &&
                                      fabsf(mappedOutward[row] - selectedFreeCarrier.rotation[3 + row]) < 1.0e-5f &&
                                      fabsf(mappedUp[row] - selectedFreeCarrier.rotation[6 + row]) < 1.0e-5f;
        Check(anatomyMapsToController,
            "Free fingers face controller-forward, thumb faces outward, and the back of the hand faces up");
        float anatomyBasis[9]{};
        Check(Halo4NormalizeFloatingBasis(freeAnatomicalTarget.rotation, anatomyBasis),
            "The free anatomical target remains a finite proper basis");
        const float anatomyDeterminant =
            anatomyBasis[0] * (anatomyBasis[4] * anatomyBasis[8] - anatomyBasis[7] * anatomyBasis[5]) -
            anatomyBasis[3] * (anatomyBasis[1] * anatomyBasis[8] - anatomyBasis[7] * anatomyBasis[2]) +
            anatomyBasis[6] * (anatomyBasis[1] * anatomyBasis[5] - anatomyBasis[4] * anatomyBasis[2]);
        Check(fabsf(anatomyDeterminant - 1.0f) < 1.0e-4f,
            "The free anatomical mount is a rotation, never a reflected hand");
        float palmNormal[3] = {thumbAxis[1] * middleBaseOffset[2] - thumbAxis[2] * middleBaseOffset[1],
            thumbAxis[2] * middleBaseOffset[0] - thumbAxis[0] * middleBaseOffset[2],
            thumbAxis[0] * middleBaseOffset[1] - thumbAxis[1] * middleBaseOffset[0]};
        const float palmLength =
            sqrtf(palmNormal[0] * palmNormal[0] + palmNormal[1] * palmNormal[1] + palmNormal[2] * palmNormal[2]);
        for (float& value : palmNormal)
            value /= palmLength;
        float parityPalm[3]{}, gripPalm[3]{};
        rotateDirection(parityFreeTarget.rotation, palmNormal, parityPalm);
        rotateDirection(freeGripTarget.rotation, palmNormal, gripPalm);
        Check(fabsf(parityPalm[0] + gripPalm[0]) < 1.0e-5f && fabsf(parityPalm[1] + gripPalm[1]) < 1.0e-5f &&
                  fabsf(parityPalm[2] + gripPalm[2]) < 1.0e-5f,
            "C-H4-41 reverses C-H4-40's player-facing palm so the back of the glove faces the player");
        float anatomicalPalmNormal[3]{};
        rotateDirection(freeAnatomicalTarget.rotation, palmNormal, anatomicalPalmNormal);
        Check(fabsf(anatomicalPalmNormal[0] + selectedFreeCarrier.rotation[6]) < 1.0e-5f &&
                  fabsf(anatomicalPalmNormal[1] + selectedFreeCarrier.rotation[7]) < 1.0e-5f &&
                  fabsf(anatomicalPalmNormal[2] + selectedFreeCarrier.rotation[8]) < 1.0e-5f,
            "The palm normal faces controller-down, placing the back of the free hand upward");
        float palmBefore[3]{}, palmAfter[3]{};
        rotateDirection(freeStateReroot.rotation, palmNormal, palmBefore);
        rotateDirection(freePalmTarget.rotation, palmNormal, palmAfter);
        Check(fabsf(palmBefore[0] + palmAfter[0]) < 1.0e-5f && fabsf(palmBefore[1] + palmAfter[1]) < 1.0e-5f &&
                  fabsf(palmBefore[2] + palmAfter[2]) < 1.0e-5f,
            "The same pi rotation reverses a palm normal perpendicular to the thumb");
        float freeBasis[9]{};
        Check(Halo4NormalizeFloatingBasis(freePalmTarget.rotation, freeBasis),
            "The free-palm result remains a proper finite rotation");
        const float freeDeterminant = freeBasis[0] * (freeBasis[4] * freeBasis[8] - freeBasis[7] * freeBasis[5]) -
                                      freeBasis[3] * (freeBasis[1] * freeBasis[8] - freeBasis[7] * freeBasis[2]) +
                                      freeBasis[6] * (freeBasis[1] * freeBasis[5] - freeBasis[4] * freeBasis[2]);
        Check(fabsf(freeDeterminant - 1.0f) < 1.0e-4f,
            "The palm flip is a 180-degree rotation, never a mesh-reflecting mirror");
        Halo4FloatingTransform baseThumbDelta{}, freeThumbDelta{};
        Halo4FloatingTransform baseMovedThumb{}, freeMovedThumb{};
        Check(Halo4BuildFloatingWorldDelta(freeStateReroot, stockEye0, baseThumbDelta) &&
                  Halo4BuildFloatingWorldDelta(freePalmTarget, stockEye0, freeThumbDelta) &&
                  Halo4ComposeFloatingTransforms(baseThumbDelta, stockThumbBase, baseMovedThumb) &&
                  Halo4ComposeFloatingTransforms(freeThumbDelta, stockThumbBase, freeMovedThumb),
            "Both the C-H4-38 raw free parent and its palm-flipped delta carry the official thumb-base node");
        Check(fabsf(baseMovedThumb.translation[0] - freeMovedThumb.translation[0]) < 1.0e-5f &&
                  fabsf(baseMovedThumb.translation[1] - freeMovedThumb.translation[1]) < 1.0e-5f &&
                  fabsf(baseMovedThumb.translation[2] - freeMovedThumb.translation[2]) < 1.0e-5f,
            "The end-to-end free-palm subtree carry leaves the thumb-base origin exactly where C-H4-36 placed it");

        Halo4FloatingTransform anatomyDelta{}, movedMiddle{};
        Check(Halo4BuildFloatingWorldDelta(freeAnatomicalTarget, stockEye0, anatomyDelta) &&
                  Halo4ComposeFloatingTransforms(anatomyDelta, stockMiddleBase, movedMiddle),
            "The C-H4-39 production delta carries the middle-base node with the anatomical wrist");
        float carriedMiddleRay[3] = {movedMiddle.translation[0] - freeAnatomicalTarget.translation[0],
            movedMiddle.translation[1] - freeAnatomicalTarget.translation[1],
            movedMiddle.translation[2] - freeAnatomicalTarget.translation[2]};
        const float carriedMiddleLength =
            sqrtf(carriedMiddleRay[0] * carriedMiddleRay[0] + carriedMiddleRay[1] * carriedMiddleRay[1] +
                  carriedMiddleRay[2] * carriedMiddleRay[2]);
        for (float& value : carriedMiddleRay)
            value /= carriedMiddleLength;
        Check(fabsf(carriedMiddleRay[0] - selectedFreeCarrier.rotation[0]) < 1.0e-5f &&
                  fabsf(carriedMiddleRay[1] - selectedFreeCarrier.rotation[1]) < 1.0e-5f &&
                  fabsf(carriedMiddleRay[2] - selectedFreeCarrier.rotation[2]) < 1.0e-5f,
            "End-to-end subtree carry leaves the free fingers facing controller-forward");

        Halo4FloatingTransform untouchedAnatomy{};
        untouchedAnatomy.translation[0] = 66.0f;
        Check(!Halo4BuildFloatingFreeLeftAnatomicalTarget(
                  selectedFreeCarrier, stockEye0, stockThumbBase, stockThumbBase, freePalmTarget, untouchedAnatomy) &&
                  untouchedAnatomy.translation[0] == 66.0f,
            "Collinear free anatomy refuses write-last and leaves C-H4-38 available as fallback");

        Halo4FloatingTransform invalidThumb = stockThumbBase;
        invalidThumb.translation[0] = std::numeric_limits<float>::quiet_NaN();
        Halo4FloatingTransform untouchedGrip{};
        untouchedGrip.translation[0] = 33.0f;
        Check(!Halo4BuildFloatingFreeLeftGripTarget(selectedFreeCarrier, carrierYawDeg, carrierPitchDeg, carrierRollDeg,
                  stockEye0, invalidThumb, freeStateReroot, untouchedGrip) &&
                  untouchedGrip.translation[0] == 33.0f,
            "Invalid optional C-H4-41 thumb input publishes nothing over the C-H4-38 fallback");
        Halo4FloatingTransform supportTarget{};
        supportTarget.translation[0] = 99.0f;
        Check(Halo4BuildFloatingLeftPresentationTarget(
                  true, stockSupportLeft, invalidThumb, supportStateReroot, supportTarget) &&
                  memcmp(&supportTarget, &supportStateReroot, sizeof(supportTarget)) == 0,
            "The exact prepared-frame two-hand state preserves the C-H4-38 shared-right-aim-rotation target byte-for-byte and ignores the free-palm dependency");
        Halo4FloatingTransform untouchedInvalidAnatomy{};
        untouchedInvalidAnatomy.translation[0] = 55.0f;
        Check(!Halo4BuildFloatingFreeLeftAnatomicalTarget(selectedFreeCarrier, stockEye0, invalidThumb, stockMiddleBase,
                  freePalmTarget, untouchedInvalidAnatomy) &&
                  untouchedInvalidAnatomy.translation[0] == 55.0f,
            "Non-finite optional anatomy cannot publish over the exact C-H4-38 fallback target");
        Halo4FloatingTransform zeroThumb = stockEye0;
        Halo4FloatingTransform untouchedPalm{};
        untouchedPalm.translation[0] = 88.0f;
        Check(!Halo4BuildFloatingLeftPresentationTarget(false, stockEye0, zeroThumb, freeStateReroot, untouchedPalm) &&
                  untouchedPalm.translation[0] == 88.0f,
            "A zero free-thumb ray refuses without publishing a partial correction");
        Halo4FloatingTransform untouchedNonfinitePalm{};
        untouchedNonfinitePalm.translation[0] = 77.0f;
        Check(!Halo4BuildFloatingLeftPresentationTarget(
                  false, stockEye0, invalidThumb, freeStateReroot, untouchedNonfinitePalm) &&
                  untouchedNonfinitePalm.translation[0] == 77.0f,
            "A non-finite free-thumb ray leaves the C-H4-38 raw-controller target available for feature-local fallback");

        Halo4FloatingTransform invalidEye = eye0;
        invalidEye.rotation[0] = std::numeric_limits<float>::quiet_NaN();
        Halo4FloatingTransform untouchedReroot{};
        untouchedReroot.translation[0] = 77.0f;
        Check(!Halo4BuildFloatingControllerRerootTarget(controllerCarrier, invalidEye, stockEye0, untouchedReroot) &&
                  untouchedReroot.translation[0] == 77.0f,
            "A non-finite current-eye relation refuses without publishing a partial target");
        Check(Halo4FloatingWristDeltaPlausible(0.7f, 0.33f) && !Halo4FloatingWristDeltaPlausible(300.0f, 0.33f) &&
                  !Halo4FloatingWristDeltaPlausible(std::numeric_limits<float>::quiet_NaN(), 0.33f),
            "The hand transaction admits physical movement but refuses the "
            "hundreds-of-units frame mix measured in the rejected line");
        Halo4FloatingTransform collapsedTransform{};
        collapsedTransform.scale = 0.0001f;
        Halo4FloatingTransform composedCollapsed{};
        Check(Halo4FloatingTransformValid(collapsedTransform) &&
                  !Halo4ComposeFloatingTransforms(collapsedTransform, eye0, composedCollapsed),
            "A final hidden wrist copy remains finite but can never re-enter "
            "the composable rigid-transform path");
    }

    CheckUnit(96);
    // The closed loop that keeps Halo 4's own look pitch - and so its
    // shot line - under the headset.
    {
        Halo4PitchServo servo;
        // A simulated engine: pitch integrates the stick through a mapping
        // whose SIGN the servo is not told. -1 is the inverted-look player.
        for (float mapping : {1.0f, -1.0f})
        {
            Halo4ResetPitchServo(servo);
            float enginePitch = 0.0f;
            const float target = 0.35f; // ~20 deg up
            int frames = 0;
            for (; frames < 400; ++frames)
            {
                const float stick = Halo4PitchServoStep(servo, enginePitch, target, kHalo4PitchServoGain);
                // ToRawStick floors every non-zero command at 27.5%, which is
                // the quantised actuator the rest hysteresis exists for.
                float applied = 0.0f;
                if (fabsf(stick) >= 1.0e-3f)
                {
                    applied = kAimServoStickFloor + fabsf(stick) * (1.0f - kAimServoStickFloor);
                    if (stick < 0.0f)
                        applied = -applied;
                }
                enginePitch += applied * mapping * 0.02f; // 0.02 rad/frame max
                if (fabsf(enginePitch - target) < 0.01f && stick == 0.0f)
                    break;
            }
            Check(frames < 400 && fabsf(enginePitch - target) < 0.01f,
                mapping > 0.0f ? "The loop drives the engine's own pitch onto the head's "
                                 "and parks there, so shots leave along the view"
                               : "It learns an INVERTED look mapping from what the engine "
                                 "actually did, instead of encoding a belief about it");
            Check(servo.direction == mapping, mapping > 0.0f
                                                  ? "The learned direction matches a normal look mapping"
                                                  : "The learned direction matches an inverted look mapping");
        }

        // Parked means EXACTLY zero. A proportional command cannot express a
        // small correction through a 27.5% floor, so anything else is the
        // turret limit cycle all over again.
        Halo4ResetPitchServo(servo);
        float parkedStick = 1.0f;
        for (int i = 0; i < 8; ++i)
            parkedStick = Halo4PitchServoStep(servo, 0.2f, 0.2f, 12.0f);
        Check(parkedStick == 0.0f, "With the gun already on the head's pitch the loop commands "
                                   "exactly zero rather than chattering against the stick floor");

        // Feature-local failure: a non-finite input must command nothing.
        Halo4ResetPitchServo(servo);
        Check(Halo4PitchServoStep(servo, std::numeric_limits<float>::quiet_NaN(), 0.1f, 12.0f) == 0.0f,
            "A non-finite engine pitch parks the stick instead of steering the "
            "camera with garbage");
    }
    CheckUnit(97);
    Check(halo4Row && halo4Row->admissionCapabilities == TitleCapability_ControllerInput,
        "Halo 4 admits shared controller input and nothing else");
    // Each exclusion below is a deliberate withholding, not an oversight, and
    // each cost a title a real defect when it was granted early.
    Check(
        halo4Row && (halo4Row->capabilities & (TitleCapability_Stereo | TitleCapability_ControllerAim |
                                                  TitleCapability_Haptics | TitleCapability_RuntimeModes |
                                                  TitleCapability_RoomScale | TitleCapability_ControllerInput)) ==
                        (TitleCapability_Stereo | TitleCapability_ControllerAim | TitleCapability_Haptics |
                            TitleCapability_RuntimeModes | TitleCapability_RoomScale | TitleCapability_ControllerInput),
        "Halo 4 advertises stereo, controller aim, haptics, runtime modes, "
        "room scale and controller input");
    Check(halo4Row && !(halo4Row->capabilities & TitleCapability_ArmIk),
        "Halo 4 withholds ArmIk: C-H4-43 has one rigid no-IK floating-hands "
        "transaction on the proven first-person return site");
    Check(halo4Row && (halo4Row->capabilities & TitleCapability_Hud),
        "Halo 4 grants Hud through the restored native CUI-root path while the rejected tag-basis writer stays dormant");
    Check(halo4Row && (halo4Row->capabilities & TitleCapability_CutsceneTheater),
        "Halo 4 advertises CutsceneTheater after its evidence-backed cinematic "
        "classifier and publication path are installed");
    Check(TitleRegistry_AllowsSharedControllerInput(GameTitle::Halo4, false, false, false, true),
        "Explicit Halo 4 receives the virtual pad through its own admission");
    Check(!TitleRegistry_AllowsSharedControllerInput(GameTitle::Halo4, false, true, false, true),
        "Camera-only ownership cannot leak controller input into Halo 4");
    Check(!TitleRegistry_AllowsSharedGameplayFeatures(GameTitle::Halo4, true, false),
        "Stale Halo 3 ownership cannot admit Halo 4 gameplay features");
    Check(halo4Row && std::wstring_view(halo4Identity.moduleName) == std::wstring_view(halo4Row->moduleName),
        "The Halo 4 adapter identity names the registry's own module");
    Check(std::string_view(halo4Identity.moduleSha256Steam) ==
                  "7C53E7D5BC9848545A1B70E2768242479336FBA1B7630D7AB955F7FD0C34FA84" &&
              std::string_view(halo4Identity.moduleSha256Store) ==
                  "5767CD564C1E8E8D012D002A8DE8E92960A3DE46442399ED054E3C4EF44AA496" &&
              halo4Identity.peTimestamp == 0x68A0E7BFu && halo4Identity.sizeOfImage == 0x04A3F000u &&
              kHalo4RetailFileSize == 17829336u &&
              std::string_view(halo4Identity.h4ekBuild) == "2023.06.27.176405.1-Release",
        "The Halo 4 adapter pins the independently verified retail and H4EK identities");
    Halo4HookProof halo4Proof{true, 1, true, true, true, true, true, true};
    Check(Halo4Adapter_HookProofComplete(halo4Proof),
        "A synthetic Halo 4 proof is complete only with every evidence gate present");
    halo4Proof.loadedImageMatchCount = 2;
    Check(!Halo4Adapter_HookProofComplete(halo4Proof),
        "A multiple-match loaded-image signature fails the Halo 4 proof closed");
    halo4Proof.loadedImageMatchCount = 1;
    halo4Proof.h4ekSemantics = false;
    Check(!Halo4Adapter_HookProofComplete(halo4Proof), "Missing H4EK semantics fail the Halo 4 proof closed");
    const GameTitle unsupportedTitles[] = {
        GameTitle::Unknown,
        GameTitle::None,
    };
    for (GameTitle title : unsupportedTitles)
        Check(TitleRegistry_HookPlan(title) == TitleHookPlan::None, "Unsupported titles never receive game hooks");
    CheckUnit(236);
    // Reach second muzzle flash: only a WORLD location on an effect that
    // declares a first-person weapon user is redirected onto the gun. Getting
    // this wrong would relocate damage or object-spawn points.
    {
        // fp mask 1, output user 0 -> a real first-person weapon effect.
        const ReachEffectFpDecision world = ReachDecideEffectLocation(0x01, 0x0007);
        Check(world.redirect && world.userIndex == 0 && world.markerIndex == 0x0007,
            "A world location on a first-person weapon effect is redirected");
        const ReachEffectFpDecision alreadyFp = ReachDecideEffectLocation(0x01, 0x8007);
        Check(!alreadyFp.redirect, "A location already flagged first-person is left to the engine");
        Check(!ReachDecideEffectLocation(0x00, 0x0007).redirect,
            "An effect with no first-person weapon user is never redirected");
        Check(!ReachDecideEffectLocation(0xF1, 0x0007).redirect,
            "An effect whose first-person output user is 'none' is not "
            "redirected");
        Check(!ReachDecideEffectLocation(0x01, 0xFFFE).redirect && !ReachDecideEffectLocation(0x01, 0xFFFF).redirect,
            "The engine's own -2/-1 designator cases are left untouched");
        const ReachEffectFpDecision user2 = ReachDecideEffectLocation(0x21, 0x0123);
        Check(user2.redirect && user2.userIndex == 2 && user2.markerIndex == 0x0123,
            "The first-person output user index is decoded from the high nibble");
    }

    CheckUnit(-1);
    std::cout << "checks run: " << g_checks << '\n';
    if (g_failures == 0)
        std::cout << "HaloMCCVR core halo4 camera render tests passed\n";
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
