// halo3_family_internal.h: core names the Halo 3 family code shares with the ODST translation unit,
// in namespace game_core. Defined in game.cpp (camera hooks, the Halo 3 family HUD chain, cinematic
// state, hands and bones). Included by game.cpp and titles/odst; in a later round it becomes the
// interface of the Halo 3 family's own translation unit (the definitions move, no new declarations).
#pragma once
#include "game_internal.h"

namespace game_core
{
    constexpr uint32_t kHalo3RuntimeCapabilities =
        TitleCapability_Stereo | TitleCapability_ControllerAim | TitleCapability_Hud | TitleCapability_ArmIk |
        TitleCapability_RuntimeModes | TitleCapability_RoomScale | TitleCapability_ControllerInput |
        TitleCapability_Haptics | TitleCapability_CutsceneTheater;
    using CamCopyFn = void*(__fastcall*)(void* dst, void* src);
    using ObserverCameraEffectFn = void(__fastcall*)(int userIndex);
    using RenderViewFn = void(__fastcall*)(void* view);
    using PrepareViewFn = void(__fastcall*)(void* view, int viewIndex);
    using BuildViewportFn = void(__fastcall*)(void* camera, void* temporary);
    using BuildMatricesFn = void(__fastcall*)(void* camera, void* temporary, void* output, float scale);
    using FpCameraRebuildFn = void(__fastcall*)(void* view, unsigned char flag);
    using FpCameraUploadFn = void(__fastcall*)(void* compactCamera, void* derivedBlock);
    using FpDriverFn = void(__fastcall*)(void* view, unsigned char flag);
    using FpInterpolateFn = bool(__fastcall*)(int view, int id, int slot, BoneMatrix** outBones, int* outCount);
    using FpVisiblePaletteFn = void(__fastcall*)(uint16_t tag, const BoneMatrix* root, BoneMatrix* destination,
        uintptr_t unused, const BoneMatrix* source, const int32_t* boneMap);
    typedef bool(__fastcall* GameIsPlaybackFn)();
    // chud_compute_anchor_basis produces a real_matrix4x3-like basis. Its final
    // vector starts at +0x28, with the vertical screen coordinate at +0x2C.
    // Moving that coordinate translates every native HUD widget without
    // changing its scale/aspect or the curvature tag data.
    using HudAnchorBasisFn = bool(__fastcall*)(int, void*, int, void*);
    // Per-element HUD (Halo 3 / ODST), keyed by screen position because these
    // titles expose no per-widget class. Each anchor chain (the parent anchor
    // 12 recurses through the hooked entry) records the origin its proven
    // screen anchor wrote; any other anchor in the chain leaves it stock. The
    // outermost call applies one translation per slot. The virtual HUD size is
    // the game's own bottom-right (anchor 2) origin, per title and generation.
    // Like the existing height code beside it, these basis reads/writes are
    // unguarded: the native call has just written this basis successfully.
    struct Halo3FamilyHudSize
    {
        const char* title;
        int placementSet; // hud_element::PlacementSet
        std::atomic<float> width{0.0f}, height{0.0f};
        std::atomic<uint32_t> generation{0};
    };
    struct Halo3FamilyHudChain
    {
        uint32_t depth = 0;
        bool skip = false, haveAnchor = false;
        float x = 0, y = 0;
    };
    // Read by Halo3FamilyHudChainScope's constructor below.
    extern thread_local Halo3FamilyHudChain g_halo3FamilyHudChain;
    struct Halo3FamilyHudChainScope
    {
        bool outermost;
        Halo3FamilyHudChainScope() noexcept : outermost(g_halo3FamilyHudChain.depth++ == 0)
        {
            if (outermost)
            {
                g_halo3FamilyHudChain.skip = false;
                g_halo3FamilyHudChain.haveAnchor = false;
            }
        }
        ~Halo3FamilyHudChainScope() noexcept { --g_halo3FamilyHudChain.depth; }
    };
    using Halo3PlayerUnitGetterFn = int(__fastcall*)(int outputUser);
    using Halo3UnitInVehicleFn = unsigned char(__fastcall*)(int unitHandle);
    using Halo3VehicleTypeFn = int(__fastcall*)(unsigned short defIndex);
    // Read-only render-node accessors used by Halo's own native camera-marker
    // path. The first returns the same interpolated 0x34-byte node bank the
    // visible-object renderer consumes; the second resolves an interpolated
    // marker (arg6=true) and is used only for the occupant's animated head.
    using Halo3InterpolatedNodesFn = unsigned char(__fastcall*)(
        int objectHandle, Halo3Matrix4x3** outMatrices, int* outCount);
    using Halo3MarkersInternalFn = int16_t(__fastcall*)(int objectHandle, int markerSid, Halo3ObjectMarker* outMarkers,
        int maximumMarkerCount, bool localOnly, bool interpolateNodes);

    // Defined in game.cpp (namespace game_core; LegacyRouteLeftHandedPalette by left_hand_presentation.inl,
    // LegacyApplyVisualHandOffsets by visual_hand_palette.inl).
    legacy_floating_hands::Layout LegacyFloatingHandsLayout(const FpInterpolationContext& c);
    bool LegacyRouteLeftHandedPalette(GameTitle title, uint16_t tag, const int32_t* boneMap, const BoneMatrix& root,
        const FpInterpolationContext& context, FpInterpolationContext& contactContext);
    void Halo3FamilyObserveHudAnchor(int anchorType, const void* basis, Halo3FamilyHudSize& size, uint32_t generation);
    void Halo3FamilyAdjustHudElement(void* basis, const Halo3FamilyHudSize& size, uint32_t generation);
    void __fastcall HudDrawWidgetHook(
        int userIndex, void* descriptor, unsigned short widgetIndex, unsigned char useAlternatePath, void* drawState);
    bool __fastcall HudCrosshairVisibleHook(int userIndex);
    void ResolveCinematicFovVar(uintptr_t base, size_t size);
    void UpdateCinematicFovPolicy();
    void PublishCinematicSection(GameTitle title, int32_t scene);
    CinematicControlState ReadCinematicControl(int32_t& scene, int32_t& shot);
    bool CinematicFovPolicyStockReady();
    void LocateCinematicState(uintptr_t base, size_t size);
    void LegacyApplyVisualHandOffsets(GameTitle title, uint16_t tag, const int32_t* boneMap,
        const FpInterpolationContext& context, BoneMatrix* destination);
    // Defined by left_hand_presentation.inl (the default argument stays on the definition).
    int LegacyAnatomicalRenderNodeCount(GameTitle title, uint16_t tag, uint32_t* checksum);
    // Defined by halo3_muzzle_publication.inl (included by halo3_dual_wield_runtime.inl, in game_core);
    // ODST's muzzle traits publish through them.
    void LegacyPublishLensAnchor(GameTitle title, uint8_t slot, const weapon_muzzle::Palette& lens) noexcept;
    void LegacyPublishReticleRay(GameTitle title, uint8_t slot, const weapon_muzzle::Palette& palette) noexcept;

    extern uint32_t* g_cinematicTlsIndex;
    extern std::atomic<int32_t> g_cinematicRebaseScene;
    extern std::atomic<int32_t> g_cinematicRebaseShot;
    extern std::atomic<uint32_t> g_cinematicRebaseSerial;
    extern std::atomic<uint64_t> g_legacyAnatomicalApplications[2];
    extern std::atomic<uint64_t> g_legacyAnatomicalRefusals[2];
    extern HudCrosshairVisibleFn g_realHudCrosshairVisible;
    extern GameIsPlaybackFn g_gameIsPlayback;
    extern HudDrawWidgetFn g_realHudDrawWidget;
    extern thread_local bool g_authoredReticleCaptureStarted;
    extern Halo3FamilyHudSize g_halo3HudSize;
    extern Halo3FamilyHudSize g_odstHudSize;
    extern std::atomic<uint8_t*> g_reduceCinematicFov;
    extern std::atomic<bool> g_reduceCinematicFovApplied;
    extern std::atomic<float> g_halo3SteerAxis;
    extern std::atomic<uint32_t> g_halo3WheelDriving;
    extern const char* kObserverCameraEffectSig;
    // Defined in game.cpp (namespace game_core); declared here, after their types.
    extern BuildMatricesFn g_buildMatrices;
    extern BuildViewportFn g_buildViewport;
    extern Halo3PlayerUnitGetterFn g_halo3PlayerUnitGetter;
    extern Halo3UnitInVehicleFn g_halo3UnitInVehicle;
    extern RenderViewFn g_origRenderView;
    extern PrepareViewFn g_prepareView;
    // Halo 3 render-model tag layout (game.cpp, left_hand_presentation.inl and the Halo 3 runtime read it).
    inline constexpr uintptr_t kHalo3RenderModelNodesBlockOffset = 0x30;
    // Defined by halo3_dual_wield_runtime.inl (namespace game_core); the default argument stays
    // on the definition.
    bool Halo3ReadOwnedWeapons(uint32_t owner, uint32_t weapons[2], bool requireDual, bool* primaryAbsentOut);
} // namespace game_core
