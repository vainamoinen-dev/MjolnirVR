// halo4_bridge.h: the Halo 4 state and functions that core (game.cpp) uses, in namespace halo4_bridge.
// Defined in src/dll/titles/h4 (until the move: in game.cpp). Included by game.cpp and the Halo 4 files.
// Core reaches Halo 4 only through what is declared here; it must not grow core logic.
#pragma once
#include <atomic>
#include "../../core/game_internal.h"

// Defined in core (game.cpp, file scope): read by Game_HasAuthoritativePauseState, written by the Halo 4
// pause reader.
extern std::atomic<bool> g_halo4NativePauseAvailable;

namespace halo4_bridge
{
    using namespace game_core;
    using Halo4ModelSkinningFn = void(__fastcall*)(int32_t objectIndex, uint16_t renderModelIndex,
        const BoneMatrix* inputObjectNodeMatrices, const void* nodeMap, bool flagA, bool flagB,
        int32_t totalNodeMatrixCount, void* skinning);
    using Halo4CuiGameplayRenderFn = void(__fastcall*)(uint32_t windowIndex, uint32_t renderBufferChannel,
        const void* viewportBounds, const void* optionalProfileValue, uint32_t renderMode, bool flag);
    struct Halo4FirstPersonAccess
    {
        unsigned char* record = nullptr; // fp_weapons[user]
        unsigned char* nodes[kHalo4FirstPersonMaxWeapons]{};
        int32_t nodeCount[kHalo4FirstPersonMaxWeapons]{0, 0};
        int weapons = 0;
    };
    // Sequence-guarded so a reader never sees a half-written matrix, and each
    // serial stays zero until a record has actually solved.
    struct Halo4CenterRoot
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<float> value[12]{}; // position, forward, left, up
    };
    constexpr uint64_t kHalo4PhysicalMeleePulseMs = 120;
    // Argument 7 of the final-palette call is a per-render-model
    // count, and the first-person assembly submits about a dozen records per
    // eye. Which counts actually arrive was not reported before,
    // so the detour bins them into a fixed table: no allocation, no
    // lock, a bounded probe, and a value that means the same thing every run.
    constexpr int kHalo4VrikCountSlots = 16;
    constexpr int32_t kHalo4VrikCountSlotEmpty = -1;
    struct Halo4CameraCore
    {
        std::atomic<bool> installed{false};
        std::atomic<bool> armed{false};
        std::atomic<bool> teardownRequested{false};
        // Every detour increments on entry and decrements after the last
        // trampoline call returns, so teardown can prove quiescence before it
        // frees a trampoline or releases the module reference.
        std::atomic<int> activeCallbacks{0};
        uintptr_t base = 0;
        size_t size = 0;
        std::atomic<uint32_t> generation{0};
        // Invalidates render-thread TLS hand relations across remove/reinstall
        // even when the title generation happens to be reused.
        std::atomic<uint32_t> floatingHandsEpoch{0};
        HMODULE moduleReference = nullptr;
        void* setupTarget = nullptr;
        void* wrapperTarget = nullptr;
        void* modelSkinningTarget = nullptr;
        void* firstPersonProducerTarget = nullptr;
        // An optional feature transaction. The gameplay-CUI scope
        // and command dispatcher hooks install/remove together; either can
        // fail without changing camera ownership.
        void* cuiReticleTarget = nullptr;
        void* cuiReticleGameplayTarget = nullptr;
        std::atomic<bool> cuiReticleInstalled{false};
        bool cuiReticleCleanupRequired = false;
        uint32_t cuiReticleRejectedGeneration = 0;
        uint32_t cuiReticleResourceWaitLoggedGeneration = 0;
        uint64_t cuiReticleLastResourceAttemptMs = 0;
        uintptr_t elementAddress = 0;
        uintptr_t setupReturnAddress = 0;
        uintptr_t wrapperReturnAddress = 0;
        // Cold-install proof for the H4EK cinematic-globals registration. This
        // gates only CutsceneTheater; a missing/ambiguous match never affects
        // the camera core, reticle, or OpenXR ownership.
        std::atomic<bool> cinematicTheaterProof{false};
        // Render-scope theatre samples are rate-limited independently from
        // camera ownership. A bad sample publishes nothing and therefore lets
        // the shared 500 ms freshness gate return to immersive presentation.
        std::atomic<uint64_t> cinematicNextPublishMs{0};
        std::atomic<uint64_t> vrikSolvedPalettes{0};
        std::atomic<uint64_t> vrikStockPalettes{0};
        std::atomic<uint64_t> vrikAlignmentRefusals{0};
        // One bucket per Halo4VrikStage, indexed by the enum, plus the
        // number of calls that reached the proven first-person return site at
        // all. A single combined bucket made an earlier zero-solve
        // result unactionable.
        std::atomic<uint64_t> vrikStageRefusals[static_cast<size_t>(Halo4VrikStage::Count)]{};
        std::atomic<uint64_t> vrikExactReturnHits{0};
        // The exact Storm hands record and immediately following held record.
        std::atomic<uint64_t> vrikWeaponRecordsCarried{0};
        std::atomic<uint64_t> vrikWeaponRecordsRefused{0};
        // Aligns Halo 4's official left_hand marker to the same direct
        // controller mount H3/ODST/Reach target, and keeps the authored
        // support grip inside the right-hand/weapon rigid transform. Optional
        // marker failure keeps the free raw-controller reroot while right/gun carry
        // continues.
        std::atomic<uint64_t> vrikMarkerParityFallbacks{0};
        std::atomic<uint64_t> vrikMarkerParityApplications{0};
        std::atomic<uint64_t> vrikTwoHandRigidSupportLocks{0};
        // The producer flag is not an anatomy classifier. A headset
        // log proved flag 1 contains both the 80-node storm_fp hands and the
        // held gun, while flag 0 is the separate 120-node native body/legs.
        std::atomic<uint64_t> vrikBodyFillRecords{0};
        std::atomic<uint64_t> vrikWeaponFillRecords{0};
        std::atomic<uint64_t> vrikUnexpectedFillRecords{0};
        std::atomic<uint64_t> vrikUnreadableFillRecords{0};
        // Resolves the render model's exact input nodes.count from the
        // same tag data model_skinning consumes. A resolver failure is not an
        // ordinary non-owned palette and must remain visible in the log.
        std::atomic<uint64_t> vrikNodeCountResolveFailures{0};
        std::atomic<int32_t> vrikLastBodyNodeCount{-1};
        std::atomic<int32_t> vrikLastWeaponNodeCount{-1};
        std::atomic<uint64_t> vrikStormRecordCandidates{0};
        std::atomic<uint32_t> vrikLastStormChecksum{0};
        std::atomic<uint32_t> vrikLastHeldChecksum{0};
        std::atomic<uint32_t> vrikLastBodyChecksum{0};
        std::atomic<uint64_t> vrikBodyIdentityExact{0};
        std::atomic<uint64_t> vrikBodyLocalUnitMatches{0};
        std::atomic<uint64_t> vrikBodyLocalUnitMismatches{0};
        std::atomic<int32_t> vrikLastHeldNodeCount{-1};
        // The helper/fixup/armour bones between the joints, which the shared
        // solver does not reach. Counted so a torn arm cannot be reported as
        // a clean solve.
        std::atomic<uint64_t> vrikArmBandBonesCarried{0};
        std::atomic<uint64_t> vrikArmBandBonesSkipped{0};
        // Left-shoulder minus right-shoulder along the camera's second basis
        // column. Positive = that column really points left. Measured, not
        // assumed, and never a refusal.
        std::atomic<float> vrikShoulderTowardLeft{0.0f};
        // The four live arm-link distances of the last record to get past the
        // basis stage. A window that solves nothing still publishes what the
        // ENGINE holds, which is what decides whether the bind envelope or the
        // node map is wrong - never a log of only our own values.
        std::atomic<uint64_t> vrikLinkMeasurements{0};
        // What the engine holds in the first arm node of every record that
        // reaches the classifier, published before it is judged. Orthonormality
        // is a property no correctly-decoded animation matrix can lack, so
        // these separate a decode error from a wrong model.
        std::atomic<uint64_t> vrikClassifyAttempts{0};
        std::atomic<float> vrikProbeScale{0.0f};
        std::atomic<float> vrikProbeColumnError{0.0f};
        std::atomic<float> vrikProbeOrthoError{0.0f};
        std::atomic<float> vrikProbeTranslation{0.0f};
        // What the bank measures against the authored bind, and what
        // the record's own root node holds. Both are read-only measurements of
        // facts that were otherwise only assumed.
        // Halo 4's own first-person FOV squish, resolved by name and
        // turned off while VR owns the camera. Touched only on the install and
        // teardown paths, never from a render hook.
        uint8_t* fpSquishSlot{nullptr};
        uint8_t fpSquishStock{0};
        bool fpSquishApplied{false};
        std::atomic<float> vrikRigScale{0.0f};
        std::atomic<float> vrikPedestalTranslation{0.0f};
        std::atomic<float> vrikPedestalScale{0.0f};
        std::atomic<float> vrikPedestalTilt{0.0f};
        std::atomic<float> vrikShoulderSpan{0.0f};
        std::atomic<float> vrikTargetMiss{0.0f};
        // Weapon fills precede BODY. Compare the pair-frozen prior BODY
        // relation used for the gun with the current BODY wrist when it later
        // arrives; peak translation/rotation error exposes reload/recoil lag.
        std::atomic<float> vrikWeaponPredictionTranslation{0.0f};
        std::atomic<float> vrikWeaponPredictionAngle{0.0f};
        std::atomic<float> vrikLastRightUpper{0.0f};
        std::atomic<float> vrikLastRightLower{0.0f};
        std::atomic<float> vrikLastLeftUpper{0.0f};
        std::atomic<float> vrikLastLeftLower{0.0f};
        // Histogram of argument 7 at the first-person return site.
        std::atomic<int32_t> vrikCountValue[kHalo4VrikCountSlots]{};
        std::atomic<uint64_t> vrikCountHits[kHalo4VrikCountSlots]{};
        std::atomic<uint64_t> vrikCountOverflow{0};
        uint64_t installedAtMs = 0;
        // Reported by the worker, never from the detour.
        std::atomic<uint64_t> stereoFrames{0};
        std::atomic<uint64_t> stockFrames{0};
        std::atomic<uint64_t> droppedFrames{0};
        std::atomic<uint64_t> cameraClaims{0};
        std::atomic<uint64_t> projectionReadbacks{0};
        std::atomic<float> lastPositionError{0.0f};
        std::atomic<float> lastForwardError{0.0f};
        std::atomic<float> lastUpError{0.0f};
        std::atomic<float> lastProjectionCenterX{0.0f};
        std::atomic<float> lastProjectionCenterY{0.0f};
        std::atomic<float> lastHalfFovX{0.0f};
        std::atomic<float> lastHalfFovY{0.0f};
        // Pairs that rendered both eyes but whose eye image never reached a
        // cache, i.e. the scene-colour RTV redirect never happened. That is
        // the difference between "Halo 4 is not rendering twice" and "Halo 4
        // is rendering twice and we are not capturing it" - two very different
        // faults that look identical in a headset.
        std::atomic<uint64_t> uncapturedEyes{0};
        std::atomic<uint64_t> cuiGameplayPasses{0};
        std::atomic<uint64_t> cuiReticleBegins{0};
        std::atomic<uint64_t> cuiReticleCaptures{0};
        std::atomic<uint64_t> cuiReticleSuppressions{0};
        std::atomic<uint64_t> cuiReticleRedirectFailures{0};
        std::atomic<uint64_t> cuiReticleCompleted{0};
        std::atomic<uint64_t> cuiReticleForcedCleanup{0};
        // C-H4-D1: the dispatcher is already an evidence-backed optional hook.
        // Count its complete gameplay stream and retain every distinct 0x28
        // transform identity in bounded atomics. No file I/O, allocation,
        // locks, COM, or signature work occurs in the callback.
        std::atomic<uint64_t> parityCuiCommands[kHalo4ParityCommandBucketCount]{};
        std::atomic<uint64_t> parityCuiCommandsOutOfRange{0};
        std::atomic<uint64_t> parityCuiUnreadableHeaders{0};
        std::atomic<uint64_t> parityCuiTotalCommands{0};
        std::atomic<int32_t> parityTransformId[kHalo4ParityTransformSlotCount]{};
        std::atomic<uint64_t> parityTransformHits[kHalo4ParityTransformSlotCount]{};
        std::atomic<uint64_t> parityTransformReplayHits[kHalo4ParityTransformSlotCount]{};
        std::atomic<uint64_t> parityTransformNormalHits[kHalo4ParityTransformSlotCount]{};
        std::atomic<uint32_t> parityTransformPayload[kHalo4ParityTransformSlotCount]{};
        std::atomic<uint32_t> parityTransformStackCount[kHalo4ParityTransformSlotCount]{};
        std::atomic<float> parityTransformScale[kHalo4ParityTransformSlotCount]{};
        std::atomic<float> parityTransformX[kHalo4ParityTransformSlotCount]{};
        std::atomic<float> parityTransformY[kHalo4ParityTransformSlotCount]{};
        std::atomic<uint64_t> parityTransformOverflow{0};
        std::atomic<uint32_t> parityLastWindow{0};
        std::atomic<uint32_t> parityLastChannel{0};
        std::atomic<uint32_t> parityLastRenderMode{0};
        std::atomic<uint32_t> parityLastFlag{0};
        std::atomic<float> cuiReticleBaseX{0.0f};
        std::atomic<float> cuiReticleBaseY{0.0f};
        // Stock-transform canvas measured inside the private capture replay.
        // It is separate from the visible canvas above because the native HUD
        // controls intentionally affect only the visible gameplay pass.
        std::atomic<float> cuiReticleCaptureBaseX{0.0f};
        std::atomic<float> cuiReticleCaptureBaseY{0.0f};
        std::atomic<float> cuiReticleAimX{0.0f};
        std::atomic<float> cuiReticleAimY{0.0f};
        std::atomic<float> cuiReticleStockScale{0.0f};
        std::atomic<float> cuiReticleWrittenScale{0.0f};
        // Current prepared serial for which each eye completed at least one
        // reticle-only matrix write. The compositor suppresses its procedural
        // fallback only after both rendered eyes prove this exact-frame move.
        std::atomic<uint64_t> cuiReticleEyeSerial[2]{};
        // Consecutive eyes that rendered but were never captured, and the
        // latch it trips. Submitting no eye image means submitting NO LAYER,
        // which is a black headset - strictly worse for the player than the
        // flat screen. When capture is provably not happening we give the flat
        // screen back, loudly.
        std::atomic<uint32_t> consecutiveUncaptured{0};
        std::atomic<bool> sceneTargetMissing{false};
        std::atomic<uint32_t> lastRejection{0};

        // --- head pose -------------------------------------------------
        // Halo 4's own recenter reference, deliberately not shared with Halo
        // 3's g_headYawRef/g_headPosRef: the Halo 4 core is installed and
        // removed per level, so its reference has to be re-captured on each
        // install rather than inherited from another title's session.
        std::atomic<bool> headReferenceValid{false};
        std::atomic<float> headYawReference{0.0f};
        std::atomic<float> headPositionReference[3]{};
        std::atomic<uint64_t> headTrackedFrames{0};
        std::atomic<float> lastHeadYawDelta{0.0f};
        std::atomic<float> lastHeadPitchDelta{0.0f};
        std::atomic<float> lastLeanMagnitude{0.0f};
        // Per-axis so a headset report can say WHICH of the three translation
        // axes moved. A magnitude alone cannot distinguish real 6DOF from one
        // working axis.
        std::atomic<float> lastLean[3]{};

        // --- look pitch ownership --------------------------------------
        // The engine's own camera pitch, published by the render thread once
        // per owned frame from the observer it already reads. The serial is
        // what the input thread uses to prove the publication is live: a
        // stalled serial means no owned render is happening, and steering a
        // camera we cannot observe is how a runaway starts.
        std::atomic<bool> enginePitchValid{false};
        std::atomic<float> enginePitch{0.0f};
        std::atomic<uint64_t> enginePitchSerial{0};
        // The engine's own aim direction, published whole from the
        // same observer read as the pitch above so the input thread can never
        // combine a yaw from one frame with a pitch from the next. This IS the
        // ray Halo 4 spawns first-person shots along, so it is the feedback the
        // closed loop closes on.
        std::atomic<float> engineAimForward[3]{};
        // T-3 telemetry-only: the pristine observer (stock) position read at
        // the top of every proven stereo transaction, with a monotonic serial.
        // No gameplay reader exists; stores only, inside the same already-
        // guarded transaction.
        //
        // stockCameraVersion is the seqlock guard over the three fields below,
        // the same version-counter discipline as ReachCompletedEyePublication
        // and the Halo 2 observer publication: odd while the stereo transaction
        // is mid-publication, even with the payload at rest. The serial alone
        // cannot prove coherence - the writer overwrites the position BEFORE it
        // bumps the serial, so a serial read back unchanged can still cover a
        // position stored by the next writer iteration.
        std::atomic<float> stockCameraPosition[3]{};
        std::atomic<uint32_t> stockCameraVersion{0};
        std::atomic<bool> stockCameraValid{false};
        std::atomic<uint64_t> stockCameraSerial{0};
        // Halo 4's own game-yaw reference, the direct analogue of Halo 3's
        // g_gameYawRef. Captured from the engine's heading at each recentre and
        // moved only by the VR turn stick thereafter.
        std::atomic<bool> gameYawReferenceValid{false};
        std::atomic<float> gameYawReference{0.0f};
        std::atomic<uint64_t> vrTurns{0};

        // --- first-person hands ----------------------------------------
        // The node format is PROVEN from live engine values before a byte is
        // written; `nodeFormatProven` is what arms the write.
        std::atomic<bool> nodeFormatProven{false};
        std::atomic<bool> nodeFormatRejected{false};
        std::atomic<uint64_t> handsPlacedFrames{0};
        std::atomic<uint64_t> handsBlockedFrames{0};
        std::atomic<int32_t> lastRootNode{-1};
        std::atomic<int> lastWeaponCount{0};
        std::atomic<float> stockNodeQuaternionLength{0.0f};
        std::atomic<float> stockNodeScale{0.0f};
        std::atomic<float> stockNodeTranslation[3]{};
        std::atomic<float> lastHandTranslation[3]{};
        std::atomic<bool> lastWriteSurvived{false};
        std::atomic<int> lastNodesWritten{0};
        std::atomic<float> lastPitchError{0.0f};
        std::atomic<float> lastPitchCommand{0.0f};
        std::atomic<float> lastPitchTarget{0.0f};
        std::atomic<float> pitchServoDirection{1.0f};
        std::atomic<float> pitchServoMinStep{0.0f};
        std::atomic<uint64_t> pitchCommandedFrames{0};
        std::atomic<uint64_t> pitchParkedFrames{0};
        std::atomic<uint64_t> pitchServoStalls{0};

        // --- native FOV coverage ---------------------------------------
        // Learned from the engine's own finished projection, never assumed.
        std::atomic<float> fovGain{0.3925f};
        std::atomic<float> fovRatio{0.0f};
        std::atomic<bool> fovLearned{false};
        std::atomic<uint64_t> fovWidenedFrames{0};
        std::atomic<uint64_t> fovStockFovFrames{0};
        std::atomic<float> lastRequestedHalfX{0.0f};
        std::atomic<float> lastRequestedHalfY{0.0f};
        std::atomic<bool> lastCoverContained{false};
        // The raw pair that measures retail's converter scale K live.
        std::atomic<float> lastObserverVerticalFov{0.0f};
        std::atomic<float> lastElementVerticalFov{0.0f};
        std::atomic<float> lastConverterScale{0.0f};
    };

    // Defined in namespace halo4_bridge.
    void TickHalo4(const TitleTickContext& c);
    uint32_t Halo4RetainedRuntimeGeneration();
    bool Halo4ResolveFirstPerson(int user, Halo4FirstPersonAccess& out);
    bool Halo4ControllerAimActive();
    bool Halo4ReadAimReferences(float& gameYaw, float& headYaw, float aimForward[3]);
    bool Halo4CuiReticleNativeSuppressionLive();
    uint64_t Halo4PhysicalMeleeCooldownMs() noexcept;
    bool DiagnosticHalo4WeaponOwner(uint32_t expectedGeneration, uint32_t candidate, uint32_t& unitOut,
        uint32_t& weaponOut, uint32_t& detailOut) noexcept;
    PrimaryWeaponEvidence Halo4ObservePrimaryWeapon(uint32_t generation, uint32_t candidate) noexcept;
    bool AutoVrTickHalo4();

    extern Halo4ModelSkinningFn g_halo4OrigModelSkinning;
    extern Halo4CuiGameplayRenderFn g_halo4OrigCuiGameplayRender;
    extern std::atomic<float> g_halo4RenderHalfFovX[2];
    extern std::atomic<float> g_halo4RenderHalfFovY[2];
    extern std::atomic<uint64_t> g_halo4RenderFovSerial[2];
    extern uint32_t* g_halo4EngineTlsIndex;
    extern std::atomic<uint32_t> g_halo4ScopeFaultGeneration;
    extern std::atomic<uint32_t> g_halo4ScopeFaults;
    extern Halo4CenterRoot g_halo4EyeRoot[2];

    // Defined in halo4_camera_core.cpp: forward to the anonymous functions of the same name.
    bool Halo4ContactMeleeReady();
    const uint8_t* Halo4ContactObject(uint32_t handle, bool requireBiped = false);
    const uint8_t* Halo4ContactBiped(uint32_t handle);
    bool Halo4ReadVehicleInput(Halo4VehicleInputState& output) noexcept;
    uint64_t Halo4ReadVehicleIdentity(const uint8_t* parent) noexcept;
    CinematicControlState ReadHalo4CinematicControl() noexcept;
    extern Halo4CameraCore g_halo4Camera;
    // Defined in halo4_camera_core.cpp: core's reads of the contact-melee runtime (g_halo4Contact).
    uint32_t Halo4ContactGeneration() noexcept;
    const uint8_t* Halo4ContactObjectOfKinds(uint32_t handle, uint32_t kinds);
} // namespace halo4_bridge
