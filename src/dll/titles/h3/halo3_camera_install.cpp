// halo3_camera_install.cpp: Halo 3's camera install, moved unchanged from game.cpp: InstallHalo3CameraHooks
// and RemoveHalo3CameraHooks, the camera, render-view, first-person, compose-bones and HUD hooks with their
// guarded wrappers, signatures and state, and the first-person rig helpers only those hooks call. The Halo 3
// lifecycle (halo3_runtime.cpp) and the title supervisor call it through game_internal.h; it reaches core
// the same way.
#include "../../core/halo3_family_internal.h"
#include "halo3_bridge.h"
#include "../../core/guard_count.h"
using namespace game_core;
using namespace halo3_bridge;

namespace
{
    constexpr uintptr_t kCamCopyRva = 0x2A628C; // fastcall(dst, src) camera copy
    constexpr uintptr_t kSrcFwd = 0x28;         // forward vec offset in src
    constexpr uintptr_t kSrcUp = 0x34;          // up vec offset in src

    constexpr uintptr_t kSrcPos = 0x00;   // camera position (x,y,z) in src
    constexpr uintptr_t kSrcProjX = 0x68; // horizontal projection/FOV scale
    constexpr uintptr_t kSrcProjY = 0x6C; // vertical projection/FOV scale

    // Gun/overlay camera: first element of the engine's 4-slot camera-object
    // array (0x2820 bytes each). +0x30/+0x34 hold the
    // overlay frustum tangents (~0.858/0.874 = ~81 deg). The overlay is
    // stretched over the whole frame, so in the widened stereo raster
    // (~123 deg) the first-person weapon and HUD appear ~2x oversized unless
    // these tangents are rewritten to match the world projection.
    constexpr uintptr_t kGunCamRva = 0x2D2F680; // expected for build 1.3528
    constexpr uintptr_t kGunProjX = 0x30;
} // namespace
namespace game_core
{
    constinit std::atomic<bool> g_hooked{false};
} // namespace game_core
namespace
{
    // True only when both halves of the visible-palette path are live:
    // 0x184B08 identifies the interpolated slot, and 0x2C561C consumes a
    // private reconstructed copy with the exact render root. While live, all
    // older sim-bank/root experiments are bypassed so no controller pose can
    // feed back into the gameplay camera.
    constinit std::atomic<bool> g_fpInterpolatorHooked{false};
    constinit std::atomic<uintptr_t> g_gunCamera{0}; // resolved from kGunCamRefSig
    constinit CamCopyFn g_origCamCopy = nullptr;
    constinit ObserverCameraEffectFn g_origObserverCameraEffect = nullptr;
} // namespace
namespace game_core
{
    constinit RenderViewFn g_origRenderView = nullptr;
} // namespace game_core
namespace
{
    using ComposeBonesFn = void(__fastcall*)(void*, int, int, BoneMatrix*, void*, void*);
    using ComposeSpecialBonesFn = void(__fastcall*)(void*, BoneMatrix*, void*, void*, int, int);
    constinit ComposeBonesFn g_origComposeBones = nullptr;
    constinit ComposeSpecialBonesFn g_origComposeSpecialBones = nullptr;
    constinit unsigned char** g_animationTagData = nullptr;
    constinit FpInterpolateFn g_origFpInterpolate = nullptr;
    constinit FpVisiblePaletteFn g_origFpVisiblePalette = nullptr;
    constinit std::atomic<int> g_fpOrientationIndex[2] = {{-1}, {-1}};
    // Arm-IK chain, walked up the node parent table from the wrist:
    //   wrist (r_hand) -> parent = elbow (r_forearm) -> parent = shoulder
    //   (r_upperarm). Node names confirmed from the H3EK fp_body render_model.
    constinit std::atomic<int> g_fpElbowIndex[2] = {{-1}, {-1}};
    constinit std::atomic<int> g_fpShoulderIndex[2] = {{-1}, {-1}};
    constinit std::atomic<int> g_fpLElbowIndex[2] = {{-1}, {-1}};
    constinit std::atomic<int> g_fpLShoulderIndex[2] = {{-1}, {-1}};
    constinit std::atomic<uint64_t> g_fpLWristDescendants[2] = {{0}, {0}};
    // (Weapon-node anchoring for the dual seat was tried and headset-DISPROVEN
    // 2026-07-19 23:3x: the weapon node's origin/axes are arbitrary per weapon
    // - plasma rifle center, spiker backwards. The universal anchor is the
    // carrier hand bone, whose authored grip is correct for every weapon.)

    constinit AtomicBoneMatrix g_fpWristToCamera[2];
} // namespace
namespace game_core
{
    bool Halo3CaptureMuzzleOwner(int player, int slot, uint32_t& unit, uint32_t& weapon) noexcept;
    void Halo3PublishMuzzlePalette(uint16_t tag, const FpInterpolationContext& context, const BoneMatrix* destination,
        uint32_t generation) noexcept;
} // namespace game_core
namespace
{
    // The former scheduler above is only a point-to-point adapter. These
    // independently verified entries also receive direct start/vector queries.
    inline constexpr char kHalo3CollisionVectorSignature[] =
        "48 8B C4 4C 89 40 18 88 50 10 48 89 48 08 55 53 56 57 41 54 "
        "41 55 41 56 41 57 48 8D A8 18 BB FF FF B8 A8 45 00 00 E8 ?? ?? ?? ?? 48 2B E0";
} // namespace
namespace game_core
{
    bool InstallHalo3DualAim(uintptr_t base, size_t size, uint32_t generation);
    bool RemoveHalo3DualAim();
    void PublishHalo3DualAim();
} // namespace game_core
namespace
{
    // THE FLAT-GUN FIX (2026-07-18, proven offline). The engine renders the
    // first-person layer (gun + arms + CHUD) through the view's SECOND camera
    // pair {compact @view+0x08, derived @view+0x1E8}, rebuilt by 0x279BEC
    // inside every per-view render immediately before each first-person draw
    // pass (6 call sites in the render driver 0x2837xx-0x283Bxx). Each call
    // re-copies the compact camera from the pointer at view+0x2A8 (the
    // CENTER-eye pose CamCopy left), forces the tangents to a fixed viewmodel
    // FOV (publishing render_first_person_fov_scale), derives matrices into
    // view+0x1E8 via the same buildViewport/buildMatrices helpers we already
    // use, and tail-jumps into the shader-constant uploader 0x2770F0. So the
    // gun/HUD layer was drawn IDENTICALLY in both eyes (zero stereo disparity,
    // wrong FOV) no matter what any bone hook did: a flat mono layer over a
    // stereo world. The fix: after the engine's rebuild, overwrite the pair
    // with the CURRENT EYE's world camera + derived block (snapshotted by
    // RenderViewHook) and re-run the uploader so the constants match.
    constinit FpCameraRebuildFn g_origFpCameraRebuild = nullptr;
    constinit FpCameraUploadFn g_fpCameraUpload = nullptr;

    constinit HudXformFn g_realHudXform = nullptr;

    void __fastcall HudXformHook(float x, float y, float z)
    {
        const float b = GameBrightnessScale();
        if (b != 1.0f)
        {
            x *= b;
            y *= b;
        }
        g_realHudXform(x, y, z);
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true))
            LOG("brightness hook active (game_brightness %.2f)", b);
    }
    constinit HudPlaceFn g_realHudPlace = nullptr;
    void __fastcall HudPlaceHook(void* context, void* widgetDefinition, void* placementInput, void* placementOutput)
    {
        g_realHudPlace(context, widgetDefinition, placementInput, placementOutput);
        if (!g_authoredReticleCaptureStarted || TitleAdapter_GetActiveTitle() != GameTitle::Halo3)
            return;
        g_authoredCrosshairColorStateAccum |= Halo3AuthoredCrosshairColorState(placementOutput);
        g_authoredCrosshairColorState.store(g_authoredCrosshairColorStateAccum, std::memory_order_release);
    }
    constinit HudAnchorBasisFn g_realHudAnchorBasis = nullptr;

    bool __fastcall HudAnchorBasisHook(int userIndex, void* drawWidgetData, int anchorType, void* basis)
    {
        const Halo3FamilyHudChainScope chainScope;
        const bool result = g_realHudAnchorBasis(userIndex, drawWidgetData, anchorType, basis);
        const uint32_t generation = g_halo3RuntimeGeneration.load(std::memory_order_acquire);
        if (result && basis)
            Halo3FamilyObserveHudAnchor(anchorType, basis, g_halo3HudSize, generation);
        if (result && basis && !g_authoredReticleCaptureStarted && g_enabled.load(std::memory_order_relaxed) &&
            VR_IsStereoEnabled())
        {
            const float height = g_config.hud_vertical_offset;
            if (isfinite(height) && height >= kHudHeightMin && height <= kHudHeightMax)
            {
                // Halo screen Y grows downward. The user-facing setting follows
                // normal height semantics: positive raises, negative lowers.
                reinterpret_cast<float*>(basis)[0x2C / sizeof(float)] -= height;
            }
            if (chainScope.outermost)
                Halo3FamilyAdjustHudElement(basis, g_halo3HudSize, generation);
        }
        return result;
    }
    // Per-eye snapshot handed from RenderViewHook to the FP hooks below. The
    // buffers are written before g_origRenderView and read during it (same
    // thread ordering via the render call; atomic pointer pairs visibility).
    constinit std::atomic<void*> g_eyeFpView{nullptr};
    alignas(16) constinit unsigned char g_eyeCompactCamera[0x90];
    alignas(16) constinit unsigned char g_eyeDerivedBlock[0x90];
    // DLSS sub-pixel jitter added to this eye's world projection centre
    // ([8]/[9], NDC units). The first-person rebuild hook applies the same
    // offset to the FP pair so gun and HUD jitter with the world.
    constinit float g_eyeProjectionJitterNdc[2][2]{};

    // FP RENDER DRIVER (2026-07-19): the engine's FP render driver
    // (0x2835D4; contains all six FP camera rebuild calls + the FP passes).
    // Instrumented to answer, from one desktop run: does it execute inside our
    // per-eye windows (g_stereoEye 0/1) or outside (-1), on which thread, how
    // often, with which flag. The next step invokes it per eye ourselves.
    constinit FpDriverFn g_origFpDriver = nullptr;
    constinit int32_t* g_fpDriverGuard = nullptr; // zero-init global gating call site 1
    constinit unsigned char* g_halo3OwnedClassGate = nullptr;

    void RememberInstalledGameHook(void* target)
    {
        g_halo3Hooks.Remember(target);
    }
} // namespace
namespace game_core
{
    bool RemoveHalo3CameraHooks();
} // namespace game_core
namespace
{
    void __fastcall FpDriverHook(void* view, unsigned char flag)
    {
        // During the scope pass this driver must still run so the palette and
        // HUD hooks can suppress their final submissions. Returning here left
        // previously prepared gun/HUD packets available to the renderer.
        // (The hud_zoom layout-factor poke that lived here is retired
        // 2026-07-19: [view+0x2B0]+0x174 never resized the visible HUD. HUD
        // sizing is now the vr.cpp HUD panel - capture, erase, re-present.)
        // RECONSTRUCTION Phase A (2026-07-19). Measured architecture: the
        // engine STAGES the FP camera once per frame outside the eye windows
        // (center pose, crushed depth) and its in-window driver runs (~2 per
        // eye, this exact call) DRAW using that staged camera - hence the flat
        // zero-disparity gun. Fix: immediately before each in-window run,
        // stamp BOTH staged FP camera pairs ({view+0x158,view+0x1E8} and the
        // sub-view {+0x6C8+0x08,+0x6C8+0x1E8}) with THIS EYE's world camera +
        // full world projection; the driver's own apply/upload then pushes OUR
        // values to the GPU through the engine's own path. Per-frame weapon
        // pose, per-eye camera - the standard VR renderer architecture. The
        // out-of-window staging runs are left untouched (they also rebuild
        // packets/palette); their center camera is re-stamped here before any
        // in-window draw, same thread, so ordering is deterministic.
        char* eyeView = static_cast<char*>(g_eyeFpView.load(std::memory_order_acquire));
        if (eyeView && view)
        {
            // Stamp the DRIVER'S OWN view object (the earlier == eyeView gate
            // silently never matched - different object; identity settled by
            // the one-shot below). Both FP pairs + the engine's own constant
            // upload, so recorded FP draws executing later in this eye window
            // bind this eye's camera.
            char* base = static_cast<char*>(view);
            // The FP driver runs ~3x per eye; after the first stamp of a given
            // eye the pairs already hold this eye's camera. Skip the re-stamp +
            // (costly) constant upload when nothing changed. memcmp is cheap and
            // self-correcting: if the engine rewrote the pair between runs the
            // compare fails and we stamp + upload exactly as before. Counters
            // below prove how often we stamp vs skip.
            static std::atomic<uint32_t> stamps{0}, skips{0};
            const bool needStamp = memcmp(base + 0x158, g_eyeCompactCamera, sizeof(g_eyeCompactCamera)) != 0 ||
                                   memcmp(base + 0x1E8, g_eyeDerivedBlock, sizeof(g_eyeDerivedBlock)) != 0 ||
                                   memcmp(base + 0x6C8 + 0x08, g_eyeCompactCamera, sizeof(g_eyeCompactCamera)) != 0 ||
                                   memcmp(base + 0x6C8 + 0x1E8, g_eyeDerivedBlock, sizeof(g_eyeDerivedBlock)) != 0;
            if (needStamp)
            {
                memcpy(base + 0x158, g_eyeCompactCamera, sizeof(g_eyeCompactCamera));
                memcpy(base + 0x1E8, g_eyeDerivedBlock, sizeof(g_eyeDerivedBlock));
                memcpy(base + 0x6C8 + 0x08, g_eyeCompactCamera, sizeof(g_eyeCompactCamera));
                memcpy(base + 0x6C8 + 0x1E8, g_eyeDerivedBlock, sizeof(g_eyeDerivedBlock));
                if (g_fpCameraUpload)
                    g_fpCameraUpload(base + 0x6C8 + 0x08, base + 0x6C8 + 0x1E8);
            }
            static std::atomic<bool> logged{false};
            if (!logged.exchange(true))
                LOG("per-eye FP render active - eye camera stamped into both FP pairs "
                    "before the in-window driver (true stereo weapon)");
        }
        g_origFpDriver(view, flag);
    }
    constinit MotionBlurVar g_motionBlurVars[4]{};
    constinit std::atomic<bool> g_motionBlurZeroed{false};
    void ResolveMotionBlurVars(uintptr_t base, size_t size)
    {
        static const char* kNames[4] = {
            "motion_blur_scale_x", "motion_blur_scale_y", "motion_blur_max_x", "motion_blur_max_y"};
        int count = 0;
        for (const char* name : kNames)
        {
            if (float* slot = FindDebugVarFloat(base, size, name))
                g_motionBlurVars[count++] = {slot, *slot};
        }
        g_motionBlurVarCount.store(count, std::memory_order_release);
        if (count == 4)
            LOG("motion-blur tuning vars resolved (scale/max x+y); toggle is live");
        else
            LOG("motion-blur vars: only %d of 4 resolved; toggle disabled", count);
    }
    // DIAGNOSTIC (hud_probe): dump engine debug-var NAMES that look HUD-related,
    // so we can find a safe-area / HUD-scale / crosshair lever for the edge-crop
    // fix (the HUD gets cut off at the headset lens edges). Read-only one-time
    // scan of the module's strings; logs any name containing a HUD-ish token
    // that also resolves to a live value slot (name -> resolvable = a real var).
    void DumpHudDebugVars(uintptr_t base, size_t size)
    {
        if (!g_config.hud_probe)
            return;
        static const char* kTokens[] = {"safe_area", "safe_frame", "hud_scale", "hud_", "chud", "reticle", "crosshair",
            "widescreen", "aspect", "overscan", "fov_scale"};
        const uint8_t* m = reinterpret_cast<const uint8_t*>(base);
        int dumped = 0;
        LOG("HUD-VARS: scanning module for HUD-related debug-var names...");
        for (size_t i = 1; i + 4 < size && dumped < 80; ++i)
        {
            // Start of a C string (preceded by a null, printable ASCII run).
            if (m[i - 1] != 0 || m[i] < 0x20 || m[i] > 0x7E)
                continue;
            size_t len = 0;
            while (i + len < size && m[i + len] >= 0x20 && m[i + len] <= 0x7E && len < 64)
                ++len;
            if (len < 4 || i + len >= size || m[i + len] != 0)
            {
                i += len;
                continue;
            }
            const char* s = reinterpret_cast<const char*>(m + i);
            bool hit = false;
            for (const char* tok : kTokens)
                if (strstr(s, tok))
                {
                    hit = true;
                    break;
                }
            if (hit)
            {
                float* slot = FindDebugVarFloat(base, size, s);
                if (slot)
                {
                    LOG("HUD-VARS: '%s' = %.4f (@%p)", s, *slot, (void*)slot);
                    ++dumped;
                }
            }
            i += len;
        }
        LOG("HUD-VARS: %d resolvable HUD-related var(s) logged", dumped);
    }
    // Camera/wrist separation diagnostic only. Neither value is the native
    // firing origin or an authored muzzle marker. The firing helper projects
    // and collision-adjusts an origin later; this probe cannot measure it.
    void ProbeBulletOrigin()
    {
        if (!g_config.bullet_probe)
            return;
        VrPadState pad;
        VR_GetPadState(pad);
        static bool prev = false;
        const bool trig = pad.valid && pad.trigR > 0.5f;
        if (trig && !prev)
        {
            const float cam[3] = {g_camX.load(), g_camY.load(), g_camZ.load()};
            BoneMatrix w{};
            float ms = 1.0f;
            if (DesiredWristWorld(false, w, ms))
            {
                const float dx = cam[0] - w.translation[0], dy = cam[1] - w.translation[1],
                            dz = cam[2] - w.translation[2];
                LOG("BULLET-PROBE trigger: camera=(%.2f,%.2f,%.2f) "
                    "wrist=(%.2f,%.2f,%.2f) separation=%.2f wu (%.2f m); native shot origin not sampled",
                    cam[0], cam[1], cam[2], w.translation[0], w.translation[1], w.translation[2],
                    sqrtf(dx * dx + dy * dy + dz * dz), sqrtf(dx * dx + dy * dy + dz * dz) * 3.048f);
            }
        }
        prev = trig;
    }

    // Called every frame from CamCopyHook. Zero wins over the engine's tag
    // reload while the toggle is off; originals are restored on re-enable.
    // Reads/writes are SEH-guarded: on a Halo3 title reload these pointers are
    // re-resolved into a fresh module instance, but a stray call from a stale
    // detour (or a race with that re-resolve) must never take MCC down for a
    // comfort setting.
    void ApplyMotionBlurSetting()
    {
        if (g_motionBlurVarCount.load(std::memory_order_acquire) != 4)
            return;
        if (!g_config.motion_blur)
        {
            for (auto& var : g_motionBlurVars)
            {
                float current = 0.0f;
                if (!SafeReadFloat(var.slot, &current))
                    return;
                if (current != 0.0f)
                    var.original = current;
                if (!SafeWriteFloat(var.slot, 0.0f))
                    return;
            }
            if (!g_motionBlurZeroed.exchange(true))
                LOG("motion blur forced OFF (blur scale/max zeroed; artifact probe active)");
        }
        else if (g_motionBlurZeroed.exchange(false))
        {
            for (auto& var : g_motionBlurVars)
                if (!SafeWriteFloat(var.slot, var.original))
                    break;
            LOG("motion blur restored to engine values");
        }
    }
    void StoreAtomicBoneMatrix(AtomicBoneMatrix& destination, const BoneMatrix& value)
    {
        destination.sequence.fetch_add(1, std::memory_order_acq_rel); // writer active (odd)
        const float* source = reinterpret_cast<const float*>(&value);
        for (int i = 0; i < 13; ++i)
            destination.value[i].store(source[i], std::memory_order_relaxed);
        destination.sequence.fetch_add(1, std::memory_order_release); // complete (even)
    }
    // THE LEVER, the earlier CE VR mod's pattern (root fed INTO composition) applied to the
    // input the mesh actually reads. Proven by elimination across headset
    // tests + the composer disassembly (0x23200C):
    //   output[0] = defaultsRoot * sourceRecord[0]            (0x23203B)
    //   output[i] = output[parent[i]] * sourceRecord[i]       (0x232099)
    // - Writing `defaults` (03:27 build) moved ONLY the muzzle flash: the
    //   composed output feeds markers/effects, and nothing else.
    // - The visible MESH recomposes from the 0x20-byte orientation bank - the
    //   composers' `source` argument - with its own camera-derived root
    //   (that is how it stays head-glued in vanilla). Editing the bank root
    //   is the only lever that has ever moved the actual gun in a headset.
    // So replace the wrist ancestor's bank record (quaternion + translation)
    // with the controller pose expressed in the head-camera frame; the mesh's
    // own camera root then cancels the head and lands the gun on the hand.
    // The composed output inherits the same pose through that child, so the
    // muzzle flash stays correct WITHOUT touching `defaults` (writing both
    // would double-apply the transform).
    //
    // No new hooks: 0x20 bytes into data the engine hands us and immediately
    // consumes. Detouring additional engine functions is what crashed the game.
    // Write the controller pose into the first bank record on the wrist's
    // ancestry chain BELOW the root (found by walking the tag node table's
    // parent words, never guessed). Rationale, from tonight's falsifications:
    // the renderer rebuilds the mesh from the bank's CHILD records under its
    // own camera-derived root - record 0 is replaced by that root (why the
    // record-0 test moved only the camera feedback, never the mesh), children
    // are kept. The game-thread composition consumes the same record, so the
    // markers/flash inherit the pose with no separate camera_control edit.
    bool ApplyControllerToBankChild(void* model, BoneMatrix* output, float* bank)
    {
        if (!model || !bank || !output || !g_animationTagData || !*g_animationTagData)
            return false;
        int slot = -1;
        unsigned char* weapon = nullptr;
        if (!FindFirstPersonWeapon(output, slot, weapon))
            return false;
        const int count = *reinterpret_cast<int*>(weapon + 0x49C);
        if (count <= 0 || count > 64 ||
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(model) + 0x14) != count)
            return false;
        const int recordOffset = *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(model) + 0x18);
        if (!recordOffset)
            return false;
        auto* records = *g_animationTagData + static_cast<ptrdiff_t>(recordOffset) * 4;
        constexpr uint32_t kRightHandStringIndex = 0xA6;
        int wristIndex = -1;
        for (int i = 0; i < count; ++i)
            if (*reinterpret_cast<const uint32_t*>(records + i * 0x20) == kRightHandStringIndex)
            {
                wristIndex = i;
                break;
            }
        if (wristIndex < 0)
            return false;
        // Walk wrist -> root; stop at the child whose parent IS the root (0).
        int child = wristIndex;
        for (int guard = 0; guard < 16; ++guard)
        {
            const int parent = *reinterpret_cast<const int16_t*>(records + child * 0x20 + 8);
            if (parent < 0 || parent >= count)
                return false;
            if (parent == 0)
                break;
            child = parent;
        }
        // Cache the authored alignment nodes for the render-thread
        // interpolator. Publish the count last so an acquiring reader never
        // observes indices from a half-updated skeleton.
        const int selectedIndex = *reinterpret_cast<int*>(weapon + 0x11A4);
        const int orientationIndex = selectedIndex >= 0 && selectedIndex < count ? selectedIndex : wristIndex;
        // Arm-IK chain: elbow = parent(wrist), shoulder = parent(elbow).
        auto parentOf = [&](int node) -> int
        {
            if (node < 0 || node >= count)
                return -1;
            const int p = *reinterpret_cast<const int16_t*>(records + node * 0x20 + 8);
            return (p >= 0 && p < count) ? p : -1;
        };
        const int elbowIndex = parentOf(wristIndex);
        const int shoulderIndex = parentOf(elbowIndex);
        // Descendant mask: every bone whose parent chain passes through the
        // given wrist (plus the wrist itself). Capped at 64 bones (mask width).
        auto subtreeOf = [&](int wrist) -> uint64_t
        {
            if (wrist < 0)
                return 0;
            uint64_t mask = (wrist < 64) ? (1ull << wrist) : 0;
            for (int i = 0; i < count && i < 64; ++i)
            {
                int n = i;
                for (int g = 0; g < 16; ++g)
                {
                    if (n == wrist)
                    {
                        mask |= (1ull << i);
                        break;
                    }
                    n = parentOf(n);
                    if (n < 0)
                        break;
                }
            }
            return mask;
        };
        const uint64_t descendants = subtreeOf(wristIndex);
        // Left arm: l_hand global string id 0xA2 - LIVE-PROVEN (2026-07-19
        // skeleton dump: index 5 = id 0xA2, parent chain 5<-3<-1, subtree 16;
        // right mirror 6=0xA6<-4<-2, subtree 21 incl. the 5 gun bones 37-41).
        // Both offline derivations (0xA1, then 0x9E from the disk pointer
        // table) were falsified live - trust only the runtime skeleton dump.
        constexpr uint32_t kLeftHandStringIndex = 0xA2;
        int lWristIndex = -1;
        for (int i = 0; i < count; ++i)
            if (*reinterpret_cast<const uint32_t*>(records + i * 0x20) == kLeftHandStringIndex)
            {
                lWristIndex = i;
                break;
            }
        // TOPOLOGICAL FALLBACK (2026-07-19): every offline id derivation for
        // the left hand has been falsified live (0xA1, then 0x9E: "L wrist
        // -1"), so stop depending on the id at all. The left wrist is
        // structurally unmistakable: the DEEPEST node with a hand-sized
        // subtree (>=10 bones) that neither contains the right wrist nor
        // lies inside its subtree. Deepest == smallest qualifying subtree
        // (l_hand < l_radius < l_humerus). Log the id found there so the
        // true value becomes a recorded fact.
        if (lWristIndex < 0)
        {
            int bestPop = count;
            for (int i = 0; i < count && i < 64; ++i)
            {
                if ((descendants >> i) & 1)
                    continue; // inside right subtree
                const uint64_t sub = subtreeOf(i);
                if ((sub >> wristIndex) & 1)
                    continue; // ancestor of right wrist
                const int pop = (int)__popcnt64(sub);
                if (pop >= 10 && pop <= count / 2 && pop < bestPop)
                {
                    bestPop = pop;
                    lWristIndex = i;
                }
            }
            static std::atomic<bool> loggedTopo{false};
            if (lWristIndex >= 0 && !loggedTopo.exchange(true))
                LOG("arm IK: left wrist found TOPOLOGICALLY: index %d, subtree %d, "
                    "record string id 0x%08X (record this id!)",
                    lWristIndex, bestPop, *reinterpret_cast<const uint32_t*>(records + lWristIndex * 0x20));
        }
        const int lElbowIndex = parentOf(lWristIndex);
        const int lShoulderIndex = parentOf(lElbowIndex);
        const uint64_t lDescendants = subtreeOf(lWristIndex);
        g_fpWristIndex[slot].store(wristIndex, std::memory_order_relaxed);
        g_fpOrientationIndex[slot].store(orientationIndex, std::memory_order_relaxed);
        g_fpElbowIndex[slot].store(elbowIndex, std::memory_order_relaxed);
        g_fpShoulderIndex[slot].store(shoulderIndex, std::memory_order_relaxed);
        g_fpWristDescendants[slot].store(descendants, std::memory_order_relaxed);
        g_fpLWristIndex[slot].store(lWristIndex, std::memory_order_relaxed);
        g_fpLElbowIndex[slot].store(lElbowIndex, std::memory_order_relaxed);
        g_fpLShoulderIndex[slot].store(lShoulderIndex, std::memory_order_relaxed);
        g_fpLWristDescendants[slot].store(lDescendants, std::memory_order_relaxed);
        g_fpBoneCount[slot].store(count, std::memory_order_release);
        // PROBE (2026-07-19, shotgun left-hand-stuck): the analysis above
        // re-runs every frame for whatever weapon is up, but this log was
        // once-per-session, so only the FIRST weapon's skeleton was ever
        // recorded. Key it on the skeleton's identity instead: every weapon
        // SWITCH dumps its chain + skeleton, and a missing left wrist is
        // called out loudly. Rate-limited (dual-wield could alternate
        // skeletons per frame). Log-only - behavior unchanged.

        // The CE probe proved the interpolated render packet is the first safe
        // gun/arms-only boundary. Once that hook is live, this sim-bank path
        // becomes topology discovery only: writing here would apply the hand
        // twice and can leak through camera_control into the gameplay camera.
        if (g_fpInterpolatorHooked.load(std::memory_order_acquire))
            return true;

        float target[3], desired[9];
        if (!GetControllerFirstPersonTransform(slot, target, desired))
            return false;
        // Never hand the engine a non-finite value: that is how a bad frame
        // becomes a crash deep inside the renderer instead of a visible glitch.
        for (float v : target)
            if (!isfinite(v))
                return false;
        for (float v : desired)
            if (!isfinite(v))
                return false;
        const float meshScale = Clamp(g_config.gun_scale, 0.3f, 3.0f);
        if (!isfinite(meshScale) || meshScale <= 0.0f)
            return false;

        // Column-basis matrix (desired[c*3+r], columns = forward/left/up) ->
        // quaternion, robust in all four trace branches.
        const float m00 = desired[0], m10 = desired[1], m20 = desired[2];
        const float m01 = desired[3], m11 = desired[4], m21 = desired[5];
        const float m02 = desired[6], m12 = desired[7], m22 = desired[8];
        float qx, qy, qz, qw;
        const float tr = m00 + m11 + m22;
        if (tr > 0.0f)
        {
            const float s = sqrtf(tr + 1.0f) * 2.0f;
            qw = 0.25f * s;
            qx = (m21 - m12) / s;
            qy = (m02 - m20) / s;
            qz = (m10 - m01) / s;
        }
        else if (m00 > m11 && m00 > m22)
        {
            const float s = sqrtf(1.0f + m00 - m11 - m22) * 2.0f;
            qw = (m21 - m12) / s;
            qx = 0.25f * s;
            qy = (m01 + m10) / s;
            qz = (m02 + m20) / s;
        }
        else if (m11 > m22)
        {
            const float s = sqrtf(1.0f + m11 - m00 - m22) * 2.0f;
            qw = (m02 - m20) / s;
            qx = (m01 + m10) / s;
            qy = 0.25f * s;
            qz = (m12 + m21) / s;
        }
        else
        {
            const float s = sqrtf(1.0f + m22 - m00 - m11) * 2.0f;
            qw = (m10 - m01) / s;
            qx = (m02 + m20) / s;
            qy = (m12 + m21) / s;
            qz = 0.25f * s;
        }
        if (!isfinite(qx) || !isfinite(qy) || !isfinite(qz) || !isfinite(qw))
            return false;
        const float qLength = sqrtf(qx * qx + qy * qy + qz * qz + qw * qw);
        if (!isfinite(qLength) || qLength < 0.001f)
            return false;
        qx /= qLength;
        qy /= qLength;
        qz /= qLength;
        qw /= qLength;

        // The engine record is not composed with anymore: its content is the
        // per-tick camera bake (rest pose identity), and composing with it
        // re-imports the head at whatever phase the animator sampled - the
        // dual-tracking / snap-turn fling. The record is replaced outright.

        // This target record's scale is deliberately NOT written: camera_control
        // descends from this node, and changing it zooms the game camera -
        // the "scale control scales the entire world" headset report. The
        // engine's animated value is preserved; a mesh-only size lever is an
        // open follow-up (gun_scale currently has no effect on the mesh).
        // the earlier CE VR mod's actual anti-contortion mechanism (WeaponHandler.cpp):
        //     if (bone.Parent == 0 || boneArray[bone.Parent].Parent == 0)
        //         outBoneTransforms[i].scale = 0.0f;   // hide the arms
        // The span from the camera-anchored body to the hand-anchored wrist
        // CANNOT be posed away - the shipped Halo VR mod hides the geometry
        // that spans it. Ours: apply the same parent-or-grandparent criterion
        // only outside the controller and camera root branches, i.e. to the
        // body/other-arm geometry stretching between the two
        // anchors. Our own branch (child) keeps its scale, and the branch
        // camera_control descends from is never touched - a scale there zooms
        // the game camera (the "scale moves the whole world" report).
        const int cc = selectedIndex;
        int camBranch = -1;
        if (cc >= 0 && cc < count)
        {
            int n = cc;
            for (int g = 0; g < 16; ++g)
            {
                const int p = *reinterpret_cast<const int16_t*>(records + n * 0x20 + 8);
                if (p < 0 || p >= count)
                    break;
                if (p == 0)
                {
                    camBranch = n;
                    break;
                }
                n = p;
            }
        }
        auto rootBranchOf = [&](int node)
        {
            for (int g = 0; g < 16 && node > 0 && node < count; ++g)
            {
                const int p = *reinterpret_cast<const int16_t*>(records + node * 0x20 + 8);
                if (p == 0)
                    return node;
                if (p < 0 || p >= count)
                    break;
                node = p;
            }
            return -1;
        };
        auto writeRecord = [&](float* base)
        {
            float* r = base + static_cast<ptrdiff_t>(child) * 8;
            r[0] = qx;
            r[1] = qy;
            r[2] = qz;
            r[3] = qw; // i,j,k,w
            r[4] = target[0];
            r[5] = target[1];
            r[6] = target[2];
            for (int i = 1; i < count; ++i)
            {
                const int branch = rootBranchOf(i);
                if (branch == child || branch == camBranch)
                    continue;
                const int parent = *reinterpret_cast<const int16_t*>(records + i * 0x20 + 8);
                const int grandparent = (parent > 0 && parent < count)
                                            ? *reinterpret_cast<const int16_t*>(records + parent * 0x20 + 8)
                                            : -1;
                if (parent == 0 || grandparent == 0)
                    base[static_cast<ptrdiff_t>(i) * 8 + 7] = 0.0f;
            }
        };
        writeRecord(bank);
        // The renderer interpolates TWO sim snapshots of these records (the
        // engine's 60Hz-sim -> 120Hz-render path). Writing only the half being
        // composed leaves the other half head-glued and the visible gun lands
        // midway between head and hand - the reported "weird in-between
        // state". Write the sibling half too (banks at TLS+0x560, one 0x1000
        // bank per slot, two 0x800 halves of 64 records each).
        auto** tlsSlots = reinterpret_cast<void**>(__readgsqword(0x58));
        auto* tls2 = tlsSlots ? reinterpret_cast<unsigned char*>(tlsSlots[*g_engineTlsIndex]) : nullptr;
        auto* banks = tls2 ? *reinterpret_cast<unsigned char**>(tls2 + 0x560) : nullptr;
        if (banks)
        {
            auto* slotBank = banks + static_cast<size_t>(slot) * 0x1000;
            auto* half = reinterpret_cast<unsigned char*>(bank);
            const bool match = (half == slotBank || half == slotBank + 0x800);
            static std::atomic<bool> loggedHalf{false};
            if (!loggedHalf.exchange(true))
                LOG("diagnostic: bank source=%p slotBank=%p sibling-write=%s", half, slotBank,
                    match ? "ACTIVE" : "MISSED (blend may persist)");
            if (match)
                writeRecord(reinterpret_cast<float*>(slotBank + ((half - slotBank) ^ 0x800)));
        }
        static std::atomic<unsigned> logged{0};
        const unsigned bit = 1u << slot;
        if (!(logged.fetch_or(bit) & bit))
            LOG("slot %d bank CHILD node %d (wrist ancestor under root) bound to the %s controller (scale %.2f)", slot,
                child, slot == 0 ? "right" : "left", meshScale);
        return true;
    }

    void CacheAuthoredFirstPersonAlignment(BoneMatrix* bones, int first, int composedCount)
    {
        if (!bones || first != 0)
            return;
        int slot = -1;
        unsigned char* weapon = nullptr;
        if (!FindFirstPersonWeapon(bones, slot, weapon) || slot < 0 || slot > 1)
            return;
        const int count = *reinterpret_cast<int*>(weapon + 0x49C);
        const int wrist = g_fpWristIndex[slot].load(std::memory_order_acquire);
        const int cameraControl = g_fpOrientationIndex[slot].load(std::memory_order_acquire);
        if (count <= 0 || count > 64 || composedCount < count || wrist < 0 || wrist >= count || cameraControl < 0 ||
            cameraControl >= count)
            return;

        BoneMatrix inverseWrist{}, wristToCamera{};
        if (!InvertBoneMatrix(bones[wrist], inverseWrist) ||
            !ComposeBoneMatrices(inverseWrist, bones[cameraControl], wristToCamera))
            return;
        StoreAtomicBoneMatrix(g_fpWristToCamera[slot], wristToCamera);
        static std::atomic<unsigned> loggedSlots{0};
        const unsigned bit = 1u << slot;
        if (!(loggedSlots.fetch_or(bit) & bit))
            LOG("cached authored wrist->camera_control relation for FP slot %d "
                "(wrist %d, camera_control %d)",
                slot, wrist, cameraControl);
    }

    bool ApplyControllerToComposedBones(void* model, BoneMatrix* bones)
    {
        if (g_fpInterpolatorHooked.load(std::memory_order_acquire))
            return false;
        if (!model || !bones || !g_engineTlsIndex || !g_animationTagData || !*g_animationTagData)
            return false;
        auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
        if (!slots)
            return false;
        auto* tls = reinterpret_cast<unsigned char*>(slots[*g_engineTlsIndex]);
        if (!tls)
            return false;
        auto* weapons = *reinterpret_cast<unsigned char**>(tls + 0x568);
        if (!weapons)
            return false;
        int slot = -1;
        unsigned char* weapon = nullptr;
        for (int candidate = 0; candidate < 2; ++candidate)
        {
            auto* w = weapons + candidate * 0x11BC;
            if (reinterpret_cast<BoneMatrix*>(w + 0x4A4) == bones)
            {
                slot = candidate;
                weapon = w;
                break;
            }
        }
        if (!weapon)
            return false;
        const int count = *reinterpret_cast<int*>(weapon + 0x49C);
        if (count <= 0 || count > 64 ||
            *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(model) + 0x14) != count)
            return false;
        const int recordOffset = *reinterpret_cast<int*>(reinterpret_cast<unsigned char*>(model) + 0x18);
        if (!recordOffset)
            return false;
        auto* records = *g_animationTagData + static_cast<ptrdiff_t>(recordOffset) * 4;
        // Runtime animation node records use the raw global string index, not
        // the packed map StringId.  This is also how the engine calls its own
        // node finder at 0x2323AC (for example, edx=0xD9/0x1D9).  r_hand is
        // global string index 0xA6; using 0x0C0000A6 made every pose silently
        // miss the wrist and left the view model attached to the camera.
        constexpr uint32_t kRightHandStringIndex = 0xA6;
        int wristIndex = -1;
        for (int i = 0; i < count; ++i)
            if (*reinterpret_cast<const uint32_t*>(records + i * 0x20) == kRightHandStringIndex)
            {
                wristIndex = i;
                break;
            }
        if (wristIndex < 0)
        {
            static std::atomic<bool> loggedMissing{false};
            if (!loggedMissing.exchange(true))
            {
                LOG("r_hand node 0x%X absent from first-person skeleton (%d bones; first nodes %X,%X,%X,%X)",
                    kRightHandStringIndex, count, count > 0 ? *reinterpret_cast<const uint32_t*>(records) : 0,
                    count > 1 ? *reinterpret_cast<const uint32_t*>(records + 0x20) : 0,
                    count > 2 ? *reinterpret_cast<const uint32_t*>(records + 0x40) : 0,
                    count > 3 ? *reinterpret_cast<const uint32_t*>(records + 0x60) : 0);
            }
            return false;
        }
        // Halo resolves the per-weapon camera_control node (string index 0xD9)
        // itself and stores its bone index here. Unlike a guessed `gun` name,
        // this exists in the live 43-node skeleton and follows each weapon's
        // authored barrel orientation.
        const int selectedIndex = *reinterpret_cast<int*>(weapon + 0x11A4);
        const int orientationIndex = selectedIndex >= 0 && selectedIndex < count ? selectedIndex : wristIndex;

        // Proven offline (2026-07-15): the root transform handed to the composer
        // at halo3+0x2C4626 is scale 1, rotation IDENTITY, translation ~0, so
        // these composed bones are CAMERA-space. Log the real values once to
        // confirm the magnitudes match that reading in the live game.
        static std::atomic<bool> loggedBones{false};
        if (!loggedBones.exchange(true))
            LOG("PROBE: composed bones camera-space check: wrist[%d] t=(%.4f,%.4f,%.4f) "
                "scale=%.3f | camera_control[%d] t=(%.4f,%.4f,%.4f) fwd=(%.3f,%.3f,%.3f)",
                wristIndex, bones[wristIndex].translation[0], bones[wristIndex].translation[1],
                bones[wristIndex].translation[2], bones[wristIndex].scale, selectedIndex,
                bones[orientationIndex].translation[0], bones[orientationIndex].translation[1],
                bones[orientationIndex].translation[2], bones[orientationIndex].rotation[0],
                bones[orientationIndex].rotation[1], bones[orientationIndex].rotation[2]);

        if (g_config.weapon_probe)
        {
            // DECISIVE PROBE. No controller, no head, no rotation: shove the
            // whole composed assembly a fixed 0.3 world units (~1 m) to the
            // LEFT (camera space: +x forward, +y left, +z up). The visible gun
            // either moves or it does not, and that single bit tells us whether
            // the mesh reads these matrices at all - which the disassembly
            // cannot, because our edits provably reach both the effects anchor
            // and the mesh's own render packet.
            for (int i = 0; i < count; ++i)
                bones[i].translation[1] += 0.3f;
            static std::atomic<bool> loggedProbe{false};
            if (!loggedProbe.exchange(true))
                LOG("PROBE ACTIVE: all %d bones of slot %d pushed +0.3 left; "
                    "if the GUN MESH does not move, it does not read weapon+0x4A4",
                    count, slot);
            return true;
        }

        float target[3], desired[9];
        if (!GetControllerFirstPersonTransform(slot, target, desired))
            return false;
        const float anchor[3] = {
            bones[wristIndex].translation[0], bones[wristIndex].translation[1], bones[wristIndex].translation[2]};
        float current[9];
        for (int column = 0; column < 3; ++column)
        {
            float len = 0;
            for (int j = 0; j < 3; ++j)
            {
                current[column * 3 + j] = bones[orientationIndex].rotation[column * 3 + j];
                len += current[column * 3 + j] * current[column * 3 + j];
            }
            len = sqrtf(len);
            if (len < 0.001f)
                return false;
            for (int j = 0; j < 3; ++j)
                current[column * 3 + j] /= len;
        }
        auto rotateDelta = [&](const float in[3], float out[3])
        {
            float component[3]{};
            for (int column = 0; column < 3; ++column)
                for (int j = 0; j < 3; ++j)
                    component[column] += in[j] * current[column * 3 + j];
            for (int j = 0; j < 3; ++j)
                out[j] = desired[j] * component[0] + desired[3 + j] * component[1] + desired[6 + j] * component[2];
        };

        // Halo 3's weapon vertices are weighted across r_hand, camera_control,
        // root and weapon-specific nodes. Moving only the wrist descendants
        // leaves some weights head-driven, producing the reported dual
        // head+hand motion. Apply one rigid delta and one uniform mesh scale
        // to the complete composed assembly so every influence agrees. The
        // bones are camera-space world units and the overlay frustum matches
        // the world projection, so 1.0 draws the weapon at authored size.
        const float meshScale = Clamp(g_config.gun_scale, 0.3f, 3.0f);
        for (int i = 0; i < count; ++i)
        {
            const float d[3] = {bones[i].translation[0] - anchor[0], bones[i].translation[1] - anchor[1],
                bones[i].translation[2] - anchor[2]};
            float rt[3];
            rotateDelta(d, rt);
            for (int j = 0; j < 3; ++j)
                bones[i].translation[j] = target[j] + rt[j] * meshScale;
            bones[i].scale *= meshScale;
            for (int column = 0; column < 3; ++column)
            {
                float rotated[3];
                rotateDelta(&bones[i].rotation[column * 3], rotated);
                for (int j = 0; j < 3; ++j)
                    bones[i].rotation[column * 3 + j] = rotated[j];
            }
        }
        static std::atomic<unsigned> logged{0};
        const unsigned bit = 1u << slot;
        if (!(logged.fetch_or(bit) & bit))
            LOG("complete first-person slot %d bound to %s controller (%d bones, wrist %d, camera_control %d, scale %.2f)",
                slot, slot == 0 ? "right" : "left", count, wristIndex, selectedIndex, meshScale);
        return true;
    }

    bool LoadCachedRenderRoot(int player, BoneMatrix& root)
    {
        if (player < 0 || player >= 4 || !g_engineTlsIndex)
            return false;
        auto** tlsSlots = reinterpret_cast<void**>(__readgsqword(0x58));
        if (!tlsSlots)
            return false;
        auto* tls = reinterpret_cast<unsigned char*>(tlsSlots[*g_engineTlsIndex]);
        if (!tls)
            return false;
        auto* renderPlayers = *reinterpret_cast<unsigned char**>(tls + 0x568);
        if (!renderPlayers)
            return false;
        root = *reinterpret_cast<const BoneMatrix*>(renderPlayers + static_cast<uintptr_t>(player) * 0x2430 + 0x23F0);
        return isfinite(root.scale) && fabsf(root.scale) > 0.001f;
    }
    bool ApplyControllerToMarkerBones(int player, BoneMatrix* bones, int count, int wrist, int cameraControl, bool dual)
    {
        (void)cameraControl;
        BoneMatrix root{};
        if (!LoadCachedRenderRoot(player, root))
            return false;
        const int leftWrist = g_fpLWristIndex[1].load(std::memory_order_acquire);
        return ApplyControllerToMarkerBonesFromRoot(root, bones, count, wrist, leftWrist, dual);
    }
    bool __fastcall FpInterpolateHook(int view, int id, int slot, BoneMatrix** outBones, int* outCount)
    {
        // Diagnostic-only FP entry + ungated before-read (no gameplay effect).
        uint32_t diagnosticBeforeUnit = UINT32_MAX;
        uint32_t diagnosticBeforeWeapon = UINT32_MAX;
        uint32_t diagnosticBeforeDetail = 0;
        const bool diagnosticArmed = Telemetry_WeaponEventsAccepting() && slot == 0 && view == 0;
        if (diagnosticArmed)
        {
            Game_DiagnosticNoteFpThread(GameTitle::Halo3);
            PublishH3FpDiagnostic(WeaponOrderEventKind::FpEntry, WeaponOrderEventStatus::NoObservation,
                g_fpStereoSolveScope.armed ? g_fpStereoSolveScope.anatomicalTracking.serial : 0, UINT32_MAX, UINT32_MAX,
                uint64_t(slot), uint64_t(view));
            uint32_t probeUnit = UINT32_MAX, probeWeapon = UINT32_MAX;
            uint32_t probeDetail = 0;
            if (Game_DiagnosticReadPrimaryWeapon(GameTitle::Halo3, probeUnit, probeWeapon, probeDetail))
            {
                diagnosticBeforeUnit = probeUnit;
                diagnosticBeforeWeapon = probeWeapon;
                diagnosticBeforeDetail = probeDetail;
            }
        }
        // Persistent support grip production evidence (root invocation of the
        // stereo pair only: whichever slot-0 view interpolates first resolves,
        // the other eye reuses the pair's frozen decision, and a later
        // invocation of the same pair must not overwrite it). The before-read
        // is taken here; the after-read lands IMMEDIATELY after the native
        // original below, BEFORE any support-dependent MCCVR transform.
        // Read-only with respect to the durable
        // relationship. PG off: no read, no publication, no change.
        const bool supportRootInvocation = g_config.persistent_support_grip && slot == 0 &&
                                           !(g_fpStereoSolveScope.armed && g_fpStereoSolveScope.supportGripResolved);
        PrimaryWeaponEvidence supportBefore{};
        const bool supportBeforeOk =
            supportRootInvocation && Game_ReadPrimaryWeaponEvidence(GameTitle::Halo3, supportBefore);
        uint32_t muzzleUnit = UINT32_MAX, muzzleWeapon = UINT32_MAX;
        const bool muzzleOwner = Halo3CaptureMuzzleOwner(view, slot, muzzleUnit, muzzleWeapon);
        const bool result = g_origFpInterpolate(view, id, slot, outBones, outCount);
        if (supportRootInvocation)
        {
            PrimaryWeaponEvidence supportAfter{};
            const bool supportAfterOk = Game_ReadPrimaryWeaponEvidence(GameTitle::Halo3, supportAfter);
            const SupportInvocationResolution resolution = Game_ResolveSupportInvocation(
                GameTitle::Halo3, supportBefore, supportBeforeOk, supportAfter, supportAfterOk, true);
            if (g_fpStereoSolveScope.armed)
            {
                g_fpStereoSolveScope.supportGripAttached = resolution.trusted;
                g_fpStereoSolveScope.supportGripResolved = true;
            }
        }
        if (slot == 0 || slot == 1)
        {
            g_fpInterpolationContexts[slot] = {};
            if (result && outBones && outCount && *outBones)
            {
                const int count = *outCount;
                const int cached = g_fpBoneCount[slot].load(std::memory_order_acquire);
                const int wrist = g_fpWristIndex[slot].load(std::memory_order_acquire);
                const int cameraControl = g_fpOrientationIndex[slot].load(std::memory_order_acquire);
                const int elbow = g_fpElbowIndex[slot].load(std::memory_order_acquire);
                const int shoulder = g_fpShoulderIndex[slot].load(std::memory_order_acquire);
                if (count > 0 && count <= 64 && cached == count && wrist >= 0 && wrist < count && cameraControl >= 0 &&
                    cameraControl < count)
                {
                    auto& context = g_fpInterpolationContexts[slot];
                    context.source = *outBones;
                    context.generation = g_halo3RuntimeGeneration.load(std::memory_order_acquire);
                    context.count = count;
                    context.player = view;
                    context.slot = slot;
                    uint32_t currentUnit = UINT32_MAX, currentWeapon = UINT32_MAX;
                    if (muzzleOwner && Halo3CaptureMuzzleOwner(view, slot, currentUnit, currentWeapon) &&
                        currentUnit == muzzleUnit && currentWeapon == muzzleWeapon)
                    {
                        context.muzzleUnit = muzzleUnit;
                        context.muzzleWeapon = muzzleWeapon;
                    }
                    context.wrist = wrist;
                    context.cameraControl = cameraControl;
                    context.elbow = elbow;
                    context.shoulder = shoulder;
                    context.wristDescendants = g_fpWristDescendants[slot].load(std::memory_order_acquire);
                    context.lWrist = g_fpLWristIndex[slot].load(std::memory_order_acquire);
                    context.lElbow = g_fpLElbowIndex[slot].load(std::memory_order_acquire);
                    context.lShoulder = g_fpLShoulderIndex[slot].load(std::memory_order_acquire);
                    context.lWristDescendants = g_fpLWristDescendants[slot].load(std::memory_order_acquire);
                    context.valid = true;
                    if (slot == 1 && view == 0 && g_enabled.load(std::memory_order_acquire))
                        VR_ObserveSecondaryWeaponPresentation(GameTitle::Halo3, context.generation);
                    memcpy(
                        g_fpUnmodifiedInterpolations[slot], *outBones, static_cast<size_t>(count) * sizeof(BoneMatrix));
                    if (slot == 1)
                    {
                        static std::atomic<bool> loggedDual{false};
                        if (!loggedDual.exchange(true))
                            LOG("DUAL: slot 1 FP context captured (%d bones, wrist %d, "
                                "camera_control %d) - left-hand weapon path live",
                                count, wrist, cameraControl);
                    }
                    // Measure the authored barrel-in-wrist direction (row 0 of the
                    // wrist rotation = invRot * camera-forward; storage m[c*3+r]).
                    // Per slot: each weapon hand auto-aligns to its own ray.
                    {
                        const float* wr = g_fpUnmodifiedInterpolations[slot][wrist].rotation;
                        float b[3] = {wr[0], wr[3], wr[6]};
                        const float bl = sqrtf(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
                        if (bl > 1e-4f && isfinite(bl))
                        {
                            b[0] /= bl;
                            b[1] /= bl;
                            b[2] /= bl;
                            if (g_barrelInWristValid[slot].load(std::memory_order_relaxed))
                            {
                                const float a = 0.02f;
                                for (int j = 0; j < 3; ++j)
                                    b[j] = g_barrelInWrist[slot][j].load(std::memory_order_relaxed) * (1.0f - a) +
                                           b[j] * a;
                                const float l2 = sqrtf(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]);
                                if (l2 > 1e-4f)
                                {
                                    b[0] /= l2;
                                    b[1] /= l2;
                                    b[2] /= l2;
                                }
                            }
                            for (int j = 0; j < 3; ++j)
                                g_barrelInWrist[slot][j].store(b[j], std::memory_order_relaxed);
                            g_barrelInWristValid[slot].store(true, std::memory_order_release);
                            static std::atomic<bool> loggedBarrel{false};
                            if (slot == 0 && !loggedBarrel.exchange(true))
                                LOG("arm IK: authored barrel-in-wrist measured "
                                    "(%.3f, %.3f, %.3f) - expect ~(1,0,0)+cant if the "
                                    "camera-forward invariant holds",
                                    b[0], b[1], b[2]);
                        }
                    }
                    ApplyControllerToMarkerBones(view, *outBones, count, wrist, cameraControl, slot == 1);
                }
            }
        }
        // Diagnostic-only stable commit, mirroring the existing ownership
        // stable point ungated: the inventory identity AND the engine FP
        // record agreement (bit2) must survive the interpolation unchanged.
        // The Capture probe records bit2 raw without requiring it, so the
        // pre-latch measurement stays the inventory view.
        if (diagnosticArmed && result && diagnosticBeforeWeapon != UINT32_MAX && (diagnosticBeforeDetail & 0x4u) != 0)
        {
            uint32_t afterUnit = UINT32_MAX, afterWeapon = UINT32_MAX;
            uint32_t afterDetail = 0;
            if (Game_DiagnosticReadPrimaryWeapon(GameTitle::Halo3, afterUnit, afterWeapon, afterDetail) &&
                (afterDetail & 0x4u) != 0 && afterUnit == diagnosticBeforeUnit && afterWeapon == diagnosticBeforeWeapon)
                PublishH3FpDiagnostic(WeaponOrderEventKind::FpWeaponCommit, WeaponOrderEventStatus::Success,
                    g_fpStereoSolveScope.armed ? g_fpStereoSolveScope.anatomicalTracking.serial : 0, afterUnit,
                    afterWeapon, uint64_t(slot), afterDetail);
        }
        return result;
    }
    void __fastcall FpVisiblePaletteHook(uint16_t tag, const BoneMatrix* root, BoneMatrix* destination,
        uintptr_t unused, const BoneMatrix* source, const int32_t* boneMap)
    {
        const uint32_t collisionGeneration = g_halo3RuntimeGeneration.load(std::memory_order_acquire);
        (void)LegacyObserveWeaponRenderModel(GameTitle::Halo3, tag, collisionGeneration, source, boneMap);
        // Match this palette submission to its slot's interpolation context by
        // the source pointer (each slot interpolates into its own bank), then
        // consume that context so a stale frame can never be re-applied.
        FpInterpolationContext context{};
        for (auto& candidate : g_fpInterpolationContexts)
            if (candidate.valid && source && candidate.source == source)
            {
                context = candidate;
                candidate.valid = false;
                break;
            }
        const BoneMatrix* selectedSource = source;
        // Never let the visible palette consume the live buffer after the
        // marker/effect preservation rewrite. If the final reconstruction is
        // temporarily unavailable (for example while a new weapon's authored
        // relation is publishing), use the untouched snapshot for this frame.
        if (context.valid && source == context.source)
            selectedSource = g_fpUnmodifiedInterpolations[context.slot];
        bool reconstructed = false;
        if (root && source)
            reconstructed = ReconstructVisiblePaletteSource(tag, context, *root, source, selectedSource);
        FpInterpolationContext contactContext = context;
        if (reconstructed && root && selectedSource == g_fpPaletteScratch &&
            g_fpStereoSolveScope.anatomicalTracking.handAlignment)
        {
            const bool routed =
                LegacyRouteLeftHandedPalette(GameTitle::Halo3, tag, boneMap, *root, context, contactContext);
            (routed ? g_legacyAnatomicalApplications[0] : g_legacyAnatomicalRefusals[0])
                .fetch_add(1, std::memory_order_relaxed);
        }
        if (reconstructed && root && selectedSource == g_fpPaletteScratch)
        {
            LegacyPublishReloadTarget(
                GameTitle::Halo3, tag, collisionGeneration, contactContext, *root, selectedSource, boneMap);
            LegacyPublishWorldCollisionVolumes(GameTitle::Halo3, tag, contactContext, *root, selectedSource,
                LegacyCollisionIgnoredObject(GameTitle::Halo3), collisionGeneration);
        }

        // FLOATING HANDS (floating_hands, ON by default): a pure presentation filter
        // over the already-solved palette. The VRIK solve above still tracks the
        // hands to the controllers exactly as normal; here we only collapse the
        // bones that are NOT part of either hand-or-gun subtree - the shoulders,
        // elbows, and forearms - so their skinned geometry vanishes and only the
        // hands and held guns remain. Scale is a PROVEN render input (the same
        // field the gun_scale trim resizes the visible mesh with), so shrinking a
        // bone's scale toward zero drives its vertices to the joint origin: an
        // invisible speck, no crash risk. Only runs when we own the scratch
        // buffer (reconstruction succeeded); otherwise the arms simply show this
        // frame rather than risk mutating an engine-owned buffer.
        // Arm joints collapse onto their own wrist, not in place (no tethers);
        // legacy_floating_hands_logic.h owns the node map.
        if (g_config.floating_hands && reconstructed && context.valid && selectedSource == g_fpPaletteScratch &&
            context.count > 0 && context.count <= 64)
            legacy_floating_hands::Apply(
                LegacyFloatingHandsLayout(context), g_fpPaletteScratch, g_config.floating_hands_collapse_at_wrist);

        // The scope is a magnified world view, not a second first-person view.
        // Let Halo finish the normal FP submission path, but feed its final
        // palette a private zero-scale copy so gun, hands, and arms contribute
        // no pixels. The normal stereo palettes remain completely untouched.
        if (g_scopeRenderActive.load(std::memory_order_acquire) && context.valid && selectedSource &&
            context.count > 0 && context.count <= 64)
        {
            memcpy(g_scopeHiddenPalette, selectedSource, static_cast<size_t>(context.count) * sizeof(BoneMatrix));
            for (int i = 0; i < context.count; ++i)
                g_scopeHiddenPalette[i].scale = 0.0001f;
            selectedSource = g_scopeHiddenPalette;
        }

        g_origFpVisiblePalette(tag, root, destination, unused, selectedSource, boneMap);
        if (reconstructed && selectedSource == g_fpPaletteScratch &&
            !g_scopeRenderActive.load(std::memory_order_acquire))
        {
            Halo3PublishMuzzlePalette(tag, context, destination, collisionGeneration);
            LegacyApplyVisualHandOffsets(GameTitle::Halo3, tag, boneMap, context, destination);
            if (context.valid && context.player == 0 && context.slot == 0)
                ObserveHalo3AvatarFp(tag, destination);
        }
    }

    void __fastcall FpCameraRebuildHook(void* view, unsigned char flag)
    {
        // Let the engine do its full rebuild (including publishing
        // render_first_person_fov_scale for whatever else reads it) ...
        g_origFpCameraRebuild(view, flag);
        // 0x279BEC just built a compressed viewmodel camera + projection at
        // {view+0x08, view+0x1E8} (its argument's own offsets). While one of
        // OUR eye renders is active, overwrite BOTH with this eye's WORLD
        // camera and WORLD derived block (position, orientation, FOV AND the
        // depth terms), then re-run the engine's own constant uploader. This
        // is the last writer before the FP draw, so the gun/HUD render in true
        // world perspective instead of the crushed viewmodel depth slab.
        // No +0x6C8 gate: the earlier gate never matched (proven by the
        // diagnostic) and the layer kept its flattened depth. Applying to
        // whatever 0x279BEC rebuilds during our eye pass is exactly the set of
        // FP overlays that need world depth.
        char* eyeView = static_cast<char*>(g_eyeFpView.load(std::memory_order_acquire));
        if (!view || !eyeView)
            return;
        char* base = static_cast<char*>(view);
        memcpy(base + 0x08, g_eyeCompactCamera, sizeof(g_eyeCompactCamera));
        memcpy(base + 0x1E8, g_eyeDerivedBlock, sizeof(g_eyeDerivedBlock));
        // The 0x90-byte derived copy above stops short of the projection's
        // centre terms, which the engine just rebuilt for this FP pair. When
        // DLSS jitters the world projection, shift these by the same amount so
        // the gun/HUD layer samples the same sub-pixel phase; zero when off.
        {
            const int fpEye = g_stereoEye.load(std::memory_order_relaxed);
            if (fpEye >= 0 && fpEye <= 1)
            {
                float* fpProjection = reinterpret_cast<float*>(base + 0x1E8 + 0x78);
                fpProjection[8] += g_eyeProjectionJitterNdc[fpEye][0];
                fpProjection[9] += g_eyeProjectionJitterNdc[fpEye][1];
            }
        }
        if (g_fpCameraUpload)
            g_fpCameraUpload(base + 0x08, base + 0x1E8);
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true))
            LOG("per-eye FP camera active - gun/HUD now render in full world "
                "projection (depth uncrushed)");
    }

    // (EnforceHudElements + ChudStateCopyHook REMOVED 2026-07-19 evening. They
    // force-wrote chud+0x144..0x14A every frame with an offset map the headset
    // DISPROVED (0x146 was a nav dot, not the crosshair; 0x32F97C copies only
    // 0x144..0x147) - the stomping suppressed the whole HUD except the objective
    // text and the F1 element checkboxes did nothing. The CHUD struct is now
    // fully game-managed; the only element control is the class-gated
    // crosshair predicate in 0x2EDF24. Do not write into
    // the chud byte block again without headset-verified offsets.)

    // DIAGNOSTIC (hud_probe): log the bytes in the CHUD struct that CHANGE, to
    // locate (a) the enemy target-lock state that turns the OG reticle red and
    // (b) the per-element visibility flags (health / motion sensor / ammo).
    // Aim at an enemy, then away, then toggle HUD elements - the flipped
    // offsets appear in the log. Log-only; changes nothing. Called from
    // CamCopyHook where the CHUD pointer is already resolved.
    void ChudProbe()
    {
        if (!g_config.hud_probe || !g_engineTlsIndex)
            return;
        auto** slots = reinterpret_cast<void**>(__readgsqword(0x58));
        if (!slots)
            return;
        auto* tls = reinterpret_cast<unsigned char*>(slots[*g_engineTlsIndex]);
        if (!tls)
            return;
        auto* chud = *reinterpret_cast<unsigned char**>(tls + 0x220);
        if (!chud)
            return;
        // A window generous enough to span the known crosshair/show bytes
        // (0x144/0x146) plus nearby state. We force 0x144/0x146 every frame, so
        // ignore those two offsets in the diff.
        constexpr int kStart = 0x100, kEnd = 0x260;
        static unsigned char prev[kEnd - kStart];
        static bool have = false;
        if (!have)
        {
            memcpy(prev, chud + kStart, sizeof(prev));
            have = true;
            LOG("HUD-PROBE armed: watching CHUD +0x%03X..+0x%03X for changes "
                "(aim at an enemy to find the red-reticle byte)",
                kStart, kEnd);
            return;
        }
        static std::atomic<uint64_t> lastMs{0};
        const uint64_t now = GetTickCount64();
        char line[512];
        int n = 0;
        int changes = 0;
        for (int off = kStart; off < kEnd; ++off)
        {
            const int i = off - kStart;
            const unsigned char cur = chud[off];
            if (cur == prev[i])
                continue;
            if (off == 0x144 || off == 0x146)
            {
                prev[i] = cur;
                continue;
            } // we drive these
            if (changes < 24 && n < (int)sizeof(line) - 24)
                n += snprintf(line + n, sizeof(line) - n, "+0x%03X:%02X->%02X ", off, prev[i], cur);
            prev[i] = cur;
            ++changes;
        }
        if (changes && now - lastMs.load() > 250)
        {
            lastMs.store(now);
            LOG("HUD-PROBE %d byte(s) changed: %s", changes, line);
        }
    }

    constinit thread_local bool g_insideSpecialCompose = false;

    // Place the assembly by writing the root BEFORE the engine composes.
    // Post-editing the composed output is retired: it is downstream of the
    // engine's own weapon-lag pass (0x2C484B), which rotates every bone except
    // camera_control and so overwrote the mesh while leaving the muzzle flash
    // on our pose - exactly the reported split. `weapon_probe` still drives the
    // old output path, and only that, as a diagnostic.
    // THE MESH FIX - a call-site patch, not a detour (2026-07-15 ~04:00).
    // Proven chain, all read offline: the visible first-person mesh is built by
    // the object-node recomposer at halo3+0x341768 (single caller 0x3424DD),
    // which dequantizes the object's compressed animation and roots the chain
    // with `call 0x3453DC` at +0x341A5B - a generic object-root getter with 56
    // callers that fabricates {MakeTransformFromXZ(fwd@+0x5C, up@+0x68),
    // pos@+0x50} from the object datum. The FP arms/weapon objects sit exactly
    // at the camera every frame; that collocation IS the head-glue, and it is
    // also how the shim identifies them without knowing their handles.
    //
    // We patch the 4 aligned displacement bytes of that ONE call (atomic
    // InterlockedExchange, installed at DLL load before any level runs) to a
    // 12-byte trampoline that reaches FpRootShim. The shim calls the REAL
    // getter, and only if the returned root is camera-collocated replaces it
    // with the controller's world pose - a write into a STACK buffer the
    // renderer consumes immediately. No engine function is detoured, none of
    // the other 55 callers are affected, and no simulation state is touched.
    // Failure mode if MCC updates: signature miss -> log + gun stays glued.
    using FpRootFn = BoneMatrix*(__fastcall*)(uint16_t index, BoneMatrix* out);
    constinit FpRootFn g_realFpRoot = nullptr;

    // THE HEAD-GLUE, finally acted on safely: right after composition, the FP
    // evaluator loops every bone EXCEPT camera_control (cmp r9d,[rdi+0x11A4])
    // and rotates it via `call 0x120DF8` at halo3+0x2C485B with a camera-
    // pitch/turn matrix. That is the exact flash-vs-mesh partition observed in
    // every headset test tonight. Detouring 0x120DF8 crashes on level load
    // (proven, banned); patching THIS ONE CALL SITE affects no other caller -
    // the same aligned-disp32 technique that survived a full session at
    // 0x341A5B. The shim skips the rotation only for bone addresses inside an
    // assembly we re-rooted this frame (thread_local ranges, same thread that
    // composes), and forwards everything else to the real function untouched.
    using SwayApplyFn = void(__fastcall*)(void* sway, BoneMatrix* bone);
    constinit SwayApplyFn g_realSwayApply = nullptr;
    // Repurposed 2026-07-19 as the VRIK bone probe. This call-site patch
    // sits inside the OBJECT-node recomposer (0x341768) - the pipeline that
    // animates every visible biped, including the player's own natively
    // visible legs. With the Bone-probe checkbox on, every recomposed object's
    // root is pushed 0.3 wu left. Legs/NPCs visibly shifting = this boundary
    // reaches biped pixels = arm IK has a real write path.
    // The old camera-collocated re-anchor is retired (the FP camera anchor in
    // RenderViewHook owns gun placement now).
    // Retired to a clean passthrough (2026-07-19). This object recomposer's
    // root output was proven NOT to drive the visible body (the +0.6 lift test
    // moved nothing on screen), and the FP camera anchor owns gun placement.
    // Kept only so the call-site patch remains a stable, harmless no-op rather
    // than reintroducing an unpatch path.
    BoneMatrix* __fastcall FpRootShim(uint16_t index, BoneMatrix* out)
    {
        return g_realFpRoot(index, out);
    }
    void __fastcall ComposeBonesHook(
        void* model, int start, int count, BoneMatrix* output, void* source, void* defaults)
    {
        if (!g_insideSpecialCompose)
        {
            g_fpSkipBounds[0] = g_fpSkipBounds[1] = g_fpSkipBounds[2] = g_fpSkipBounds[3] = 0;
            ApplyControllerToBankChild(model, output, reinterpret_cast<float*>(source));
        }
        g_origComposeBones(model, start, count, output, source, defaults);
        // This is the last full authored pose before Halo's caller applies the
        // camera-lag transform (camera_control alone is exempt). Preserve that
        // relation for exact render-side lag removal.
        CacheAuthoredFirstPersonAlignment(output, start, count);
        // NOTE (2026-07-18): wiring ApplyControllerToComposedWristSubtree here
        // (the "bullet_snap" experiment) caused the RIGHT HAND to spin
        // uncontrollably and pushed bullets to stage-left - the composed output
        // is downstream of the engine weapon-lag pass and re-snapping the wrist
        // fights it. Reverted to the known-good gun tracking. Bullet origin
        // will be fixed via a weapon-fire hook (origin+direction swap) instead,
        // not by editing composed bones. The call is intentionally gone.
        // 04:17 falsified the output rewrite as a mesh lever for good (43-bone
        // rewrite confirmed running; mesh unmoved; "wrist moves the world" =
        // camera_control feedback). Probe-only again. The defaults-root write
        // is retired for the same reason: both only fed markers + the camera.
        if (!g_insideSpecialCompose && g_config.weapon_probe)
            ApplyControllerToComposedBones(model, output);
    }
    void __fastcall ComposeSpecialBonesHook(
        void* model, BoneMatrix* output, void* source, void* defaults, int firstSpecial, int secondSpecial)
    {
        g_fpSkipBounds[0] = g_fpSkipBounds[1] = g_fpSkipBounds[2] = g_fpSkipBounds[3] = 0;
        ApplyControllerToBankChild(model, output, reinterpret_cast<float*>(source));
        g_insideSpecialCompose = true;
        g_origComposeSpecialBones(model, output, source, defaults, firstSpecial, secondSpecial);
        g_insideSpecialCompose = false;
        int slot = -1;
        unsigned char* weapon = nullptr;
        if (FindFirstPersonWeapon(output, slot, weapon))
            CacheAuthoredFirstPersonAlignment(output, 0, *reinterpret_cast<int*>(weapon + 0x49C));
        // (bullet_snap reverted here too - see ComposeBonesHook note.)
        if (g_config.weapon_probe)
            ApplyControllerToComposedBones(model, output);
    }
    void Halo3PreparePhysicalCrouchCamera(uintptr_t base, size_t size);
    void Halo3ApplyPhysicalCrouchCamera(float* position, float physicalDown, bool playerControlled);
    bool ApplyHeadLook(void* src)
    {
        if (!src)
            return false;

        float q[4], hpos[3];
        if (!VR_GetHeadPose(q, hpos))
            return false;
        // Head forward (OpenXR: -Z forward, +Y up).
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        const float hfx = -2.0f * (w * y + x * z);
        const float hfy = 2.0f * (w * x - y * z);
        const float hfz = -(1.0f - 2.0f * (x * x + y * y));
        const float hy = atan2f(hfx, -hfz);
        const float hp = asinf(Clamp(hfy, -1.0f, 1.0f));

        // Head roll around the forward axis. Compare the headset's actual up
        // vector with a horizon-level up vector at the same yaw/pitch. Using
        // only the latter, rolling your head makes the world appear to tilt
        // with you instead of remaining fixed in space.
        const float hux = 2.0f * (x * y - w * z);
        const float huy = 1.0f - 2.0f * (x * x + z * z);
        const float huz = 2.0f * (y * z + w * x);
        float hrx = -hfz, hrz = hfx; // horizon-right = cross(head forward, room up)
        float hrLen = sqrtf(hrx * hrx + hrz * hrz);
        if (hrLen < 1e-4f)
            hrLen = 1e-4f;
        hrx /= hrLen;
        hrz /= hrLen;
        const float hnux = -hfy * hrz;
        const float hnuy = hrLen;
        const float hnuz = hfy * hrx;
        const float headRoll = atan2f(hux * hrx + huz * hrz, hux * hnux + huy * hnuy + huz * hnuz);

        float* fwd = reinterpret_cast<float*>(reinterpret_cast<char*>(src) + kSrcFwd);
        float* up = reinterpret_cast<float*>(reinterpret_cast<char*>(src) + kSrcUp);
        float* pos = reinterpret_cast<float*>(reinterpret_cast<char*>(src) + kSrcPos);

        // A cutscene shot changes Halo's authored camera, but the old VR yaw
        // reference otherwise survives the cut. That can spawn the viewer
        // facing backward in the new shot. The engine's exact scene/shot IDs
        // are read from its TLS state; rebase the current physical head-forward
        // to the authored camera only on entry, an ID transition, or exit.
        // Pitch and roll remain entirely HMD-owned, avoiding an artificial
        // camera rotation during continuous cinematic motion.
        static thread_local Halo3CinematicFacing cinematicFacing;
        int32_t cinematicScene = -1;
        int32_t cinematicShot = -1;
        const CinematicControlState cinematicControl = ReadCinematicControl(cinematicScene, cinematicShot);
        const bool cinematic = cinematicControl == CinematicControlState::AuthoredLocked;
        const uint32_t cinematicGeneration = g_halo3RuntimeGeneration.load(std::memory_order_relaxed);
        if (cinematicGeneration)
        {
            TitleAdapter_PublishCinematicControl(
                GameTitle::Halo3, cinematicGeneration, cinematicControl, GetTickCount64());
            PublishCinematicSection(GameTitle::Halo3, cinematic ? cinematicScene : -1); // -1 once it ends
        }
        if (cinematic && VR_IsCutsceneTheaterActive())
            return false; // authored camera + stereo eye offsets only in theatre
        if (cinematic && (cinematicScene < 0 || cinematicShot < 0))
            g_halo3ShotlessFacingSamples.fetch_add(1, std::memory_order_relaxed);
        const bool cinematicBoundary =
            cinematicFacing.Observe(cinematicGeneration, cinematicControl, cinematicScene, cinematicShot);

        const bool manualRecenter = g_needRecenter.exchange(false);
        if (manualRecenter || cinematicBoundary)
        {
            g_gameYawRef = atan2f(fwd[1], fwd[0]); // align current head to current heading
            g_headYawRef = hy;
            NoteYawReferenceLatched(GameTitle::Halo3, cinematicGeneration, manualRecenter ? "recenter" : "cut",
                g_gameYawRef, g_headYawRef, true);
            if (manualRecenter)
            {
                g_headPosRef[0] = hpos[0];
                g_headPosRef[1] = hpos[1];
                g_headPosRef[2] = hpos[2];
                VR_NotePositionRecenter(); // the cabin fade re-captures its seated eye
                g_needPosRecenter = false;
                LOG("head tracking recentered (game yaw %.1f deg)", g_gameYawRef * 57.2958f);
            }
            if (cinematicBoundary)
            {
                g_cinematicRebaseScene.store(cinematic ? cinematicScene : -1, std::memory_order_relaxed);
                g_cinematicRebaseShot.store(cinematic ? cinematicShot : -1, std::memory_order_relaxed);
                g_cinematicRebaseSerial.fetch_add(1, std::memory_order_release);
            }
        }
        else if (g_needPosRecenter.exchange(false))
        {
            // Enabling leaning: capture the neutral head position only, so the
            // aim/yaw baseline is left untouched (no view snap).
            g_headPosRef[0] = hpos[0];
            g_headPosRef[1] = hpos[1];
            g_headPosRef[2] = hpos[2];
            VR_NotePositionRecenter(); // the cabin fade re-captures its seated eye
        }
        NoteSharedYawFrame(GameTitle::Halo3, cinematicGeneration); // after the latch decision

        // Rotation: yaw relative + recenter, pitch absolute + trim.
        const float gy = g_gameYawRef + g_yawSign.load() * WrapPi(hy - g_headYawRef);
        const float gp = Clamp(g_pitchSign.load() * hp + g_pitchTrim.load(), -1.5f, 1.5f);
        const float cgp = cosf(gp), sgp = sinf(gp), cgy = cosf(gy), sgy = sinf(gy);

        float hullYaw = 0.0f, hullPitch = 0.0f, followBasis[9];
        const bool rotationFollows = Halo3ReadRollStableFollow(hullYaw, hullPitch) &&
                                     Halo3ComposeFollowBasisWithRoll(hullYaw, hullPitch, g_gameYawRef,
                                         g_yawSign.load() * WrapPi(hy - g_headYawRef), gp, headRoll, followBasis);
        if (rotationFollows)
        {
            memcpy(fwd, followBasis, sizeof(float) * 3);
            if (g_writeUp.load())
                memcpy(up, followBasis + 6, sizeof(float) * 3);
        }
        else
        {
            // The mapping without pitch follow: world-locked when the toggle is OFF, and
            // accepted yaw-only follow when only the pitch sample is refused.
            fwd[0] = cgp * cgy;
            fwd[1] = cgp * sgy;
            fwd[2] = sgp;
            if (g_writeUp.load())
            {
                const float cr = cosf(headRoll), sr = sinf(headRoll);
                // Horizon-level up plus roll toward the camera's right vector.
                up[0] = (-sgp * cgy) * cr + sgy * sr;
                up[1] = (-sgp * sgy) * cr - cgy * sr;
                up[2] = cgp * cr;
            }
        }

        // Native-seat ownership was rejected in headset testing: it shifted every
        // authored placement and did not cure the vehicle/eye mismatch. The sampler
        // supplies the exact Blender point already parented through Halo's live
        // rendered node; consuming that world point directly is essential.
        if (!cinematic && Halo3VehicleFpActive())
        {
            float anchor[3];
            if (Halo3ComputeAuthoredAnchor(anchor))
            {
                memcpy(pos, anchor, sizeof(anchor));
            }
        }

        Halo3ApplyPhysicalCrouchCamera(
            pos, -Clamp((hpos[1] - g_headPosRef[1]) * g_worldScale.load(), -1.5f, 1.5f), !cinematic);
        Roomscale_Camera(GameTitle::Halo3, !cinematic && Game_RoomscaleCameraAllowed(GameTitle::Halo3), pos, hpos, q,
            fwd, g_headPosRef, g_worldScale.load());

        // Position (leaning): shift the camera by the headset's room-space move,
        // decomposed in the head's horizontal frame and re-applied in the game's
        // frame so it stays correct as you turn. Added to the game's own
        // position each frame (the sim rewrites pos before our hook, so this
        // does not accumulate).
        if (g_positional.load())
        {
            const float dx = hpos[0] - g_headPosRef[0];
            const float dy = hpos[1] - g_headPosRef[1];
            const float dz = hpos[2] - g_headPosRef[2];
            float hlen = sqrtf(hfx * hfx + hfz * hfz);
            if (hlen < 1e-4f)
                hlen = 1e-4f;
            const float hfhx = hfx / hlen, hfhz = hfz / hlen; // head forward (horizontal)
            const float fwdComp = dx * hfhx + dz * hfhz;      // room move along look dir
            const float rightComp = dx * (-hfhz) + dz * hfhx; // room move to the right
            const float s = g_worldScale.load();
            float ox, oy, oz;
            float positionBasis[9];
            const float positionLocal[3] = {fwdComp, -rightComp, dy};
            float positionWorld[3];
            const bool positionFollows = rotationFollows &&
                                         Halo3ComposeFollowBasisWithRoll(hullYaw, hullPitch, g_gameYawRef,
                                             g_yawSign.load() * WrapPi(hy - g_headYawRef), 0.0f, 0.0f, positionBasis) &&
                                         Halo3TransformBasisVector(positionBasis, positionLocal, positionWorld);
            if (positionFollows)
            {
                ox = positionWorld[0] * s;
                oy = positionWorld[1] * s;
                oz = positionWorld[2] * s;
            }
            else
            {
                ox = (cgy * fwdComp + sgy * rightComp) * s;
                oy = (sgy * fwdComp - cgy * rightComp) * s;
                oz = dy * s;
            }
            ox = Clamp(ox, -1.5f, 1.5f);
            oy = Clamp(oy, -1.5f, 1.5f);
            oz = Clamp(oz, -1.5f, 1.5f);
            pos[0] += ox;
            pos[1] += oy;
            pos[2] += oz;
        }

        // Alternate-eye proof: offset only the render camera by half the
        // measured PSVR2 IPD. Halo right in the horizontal plane is
        // (sin(yaw), -cos(yaw), 0). This does not accumulate because the game
        // rewrites the source camera before every call.
        // Per-eye separation is applied later by RenderViewHook to the compact
        // render-only camera. Keeping it out of the authoritative source avoids
        // feeding stereo offsets back into simulation or temporal history.
        return rotationFollows;
    }
    constinit std::atomic<uint64_t> g_halo3VehicleProbeKey{~0ull};
    void* __fastcall CamCopyHook(void* dst, void* src)
    {
        VR_NotifyCameraTransform();
        const uint64_t cameraNowMs = GetTickCount64();
        const uint32_t runtimeGeneration = g_halo3RuntimeGeneration.load(std::memory_order_acquire);
        if (runtimeGeneration)
        {
            g_halo3LastCamCopyMs.store(cameraNowMs, std::memory_order_relaxed);
            TitleAdapter_PublishHeartbeat(GameTitle::Halo3, runtimeGeneration, cameraNowMs);
            Halo3SampleVehicleState(cameraNowMs);
        }
        // Low-frequency timing proof paired with vr.cpp's HMD sample-rate log.
        // The hook normally runs multiple times per presented frame, and every
        // call below reads the latest once-per-OpenXR-frame predicted pose.
        ChudProbe();
        ApplyMotionBlurSetting();
        ProbeBulletOrigin();
        ApplyBodySetting();
        // Tracing: this function copies src+0x68/+0x6C into the compact
        // render camera at dst+0x28/+0x2C. Record only the first few calls so
        // we can distinguish world, weapon, and other camera passes without
        // producing a frame-sized log forever.
        static std::atomic<unsigned> traceCount{0};
        if (src)
        {
            const float srcTanX = *reinterpret_cast<const float*>(reinterpret_cast<const char*>(src) + kSrcProjX);
            g_projectionTanX.store(srcTanX);
            g_projectionTanY.store(*reinterpret_cast<const float*>(reinterpret_cast<const char*>(src) + kSrcProjY));
            // ZOOM DETECTION (for the weapon scope): Halo narrows the camera's
            // projection tangent when the player zooms. Track the widest
            // (unzoomed) tangent as the baseline; a meaningfully smaller current
            // tangent = zoomed, with magnification = baseline/current. Logged so
            // the scope render can be built on a confirmed signal.
            if (srcTanX > 0.2f && srcTanX < 3.0f)
            {
                static float baseTan = 0.0f;
                if (srcTanX > baseTan)
                    baseTan = srcTanX; // widest seen = hip FOV
                const float factor = (baseTan > 1e-3f) ? baseTan / srcTanX : 1.0f;
                const bool zoomed = factor > 1.15f;
                g_zoomFactor.store(zoomed ? factor : 1.0f);
                static bool wasZoomed = false;
            }
            // Camera buffers are heap-allocated and move on level changes;
            // log every new one so external tools (camscan aimwrite) can
            // always read the current address from the log.
        }
        if (src)
        {
            // The game recomputes this forward from its aim state every frame,
            // so pre-overwrite it equals the true aim direction (bullets follow
            // it even while head look repaints the view).
            const float* fwd = reinterpret_cast<const float*>(reinterpret_cast<const char*>(src) + kSrcFwd);
            g_aimFwdX.store(fwd[0]);
            g_aimFwdY.store(fwd[1]);
            g_aimFwdZ.store(fwd[2]);
            g_aimSeen = true;
        }
        if (g_enabled.load())
        {
            // Turn the shared yaw reference with the hull BEFORE anything
            // consumes it this frame, and republish who owns steering - the
            // turn stick below and the aim author both branch on it.
            Halo3ApplySeatYawFollow();
            ApplyVrTurn();
            if (src)
            {
                // Snapshot the engine-owned locomotion/body origin before
                // ApplyHeadLook adds this frame's headset lean. The visible
                // weapon reads this origin but never writes it.
                const float* p = reinterpret_cast<const float*>(reinterpret_cast<const char*>(src) + kSrcPos);
                g_baseCamX.store(p[0]);
                g_baseCamY.store(p[1]);
                g_baseCamZ.store(p[2]);
                g_baseCamValid.store(true, std::memory_order_release);
            }
            // The head pose LIVES in this authoritative camera (the proven
            // regime). A 07-15 experiment saved/restored the original values
            // around the copy so gameplay would keep the aim pose - but the
            // first-person bone frame is head-camera-relative, and splitting
            // the two frames made the hand-anchored weapon visibly pick up
            // both head and aim motion. Do not scope this write again.
            const bool rollStableFollowApplied = ApplyHeadLook(src);
            if (src)
            {
                // Post-head-look camera position (includes leaning): the
                // reference FpRootShim uses to recognize camera-glued FP
                // objects and to place the controller in world space.
                const float* p = reinterpret_cast<const float*>(reinterpret_cast<const char*>(src) + kSrcPos);
                g_camX.store(p[0]);
                g_camY.store(p[1]);
                g_camZ.store(p[2]);
                const float* f = reinterpret_cast<const float*>(reinterpret_cast<const char*>(src) + kSrcFwd);
                const float* u = reinterpret_cast<const float*>(reinterpret_cast<const char*>(src) + kSrcUp);
                for (int j = 0; j < 3; ++j)
                {
                    g_camFwd[j].store(f[j]);
                    g_camUp[j].store(u[j]);
                }
                g_camValid.store(true);

                // Measure the world-up axis from the engine's camera-up (see the
                // g_worldUp declaration). Bootstrap from the first sample, then EMA
                // slowly toward each tick's up - but ONLY while looking roughly
                // level (camera-fwd within ~25 deg of horizontal), so staring up or
                // down a slope for a while can't drag the estimate off vertical.
                {
                    // The pitch-follow camera up is deliberately vehicle-pitched. It is
                    // not gravity and must never train this persistent world-up
                    // estimator or the shoulder frame will tilt after a hill.
                    if (!rollStableFollowApplied && !g_worldUpInit.load())
                    {
                        const float l = sqrtf(u[0] * u[0] + u[1] * u[1] + u[2] * u[2]);
                        if (l > 1e-4f)
                            for (int j = 0; j < 3; ++j)
                                g_worldUp[j].store(u[j] / l);
                        g_worldUpInit.store(true);
                    }
                    else if (!rollStableFollowApplied)
                    {
                        float wu[3] = {g_worldUp[0].load(), g_worldUp[1].load(), g_worldUp[2].load()};
                        const float lookLevel = fabsf(f[0] * wu[0] + f[1] * wu[1] + f[2] * wu[2]);
                        if (lookLevel < 0.42f) // forward within ~25 deg of horizontal
                        {
                            const float a = 0.01f;
                            for (int j = 0; j < 3; ++j)
                                wu[j] += (u[j] - wu[j]) * a;
                            const float l = sqrtf(wu[0] * wu[0] + wu[1] * wu[1] + wu[2] * wu[2]);
                            if (l > 1e-4f)
                                for (int j = 0; j < 3; ++j)
                                    g_worldUp[j].store(wu[j] / l);
                        }
                    }
                    static std::atomic<int> wuFrames{0};
                    const int n = wuFrames.fetch_add(1);
                    if (n == 600)
                        LOG("measured world-up = (%.3f, %.3f, %.3f)", g_worldUp[0].load(), g_worldUp[1].load(),
                            g_worldUp[2].load());
                }
            }
        }
        else
        {
            g_camValid.store(false);
            g_baseCamValid.store(false);
        }

        void* result = g_origCamCopy(dst, src);
        // The native copy has now completed the compact camera (+28/+2C).
        // Widen before upstream visibility can consume it; the eye renderer
        // later restores its exact raster projection independently.
        if (dst && src && g_enabled.load(std::memory_order_acquire) && VR_IsStereoEnabled() &&
            !VR_IsCutsceneTheaterActive())
        {
            float left[4]{}, right[4]{};
            if (VR_GetEyeFov(0, left) && VR_GetEyeFov(1, right))
            {
                bool valid = true;
                for (int edge = 0; edge < 4; ++edge)
                    valid = valid && std::isfinite(left[edge]) && std::isfinite(right[edge]) &&
                            std::fabs(left[edge]) < 1.5f && std::fabs(right[edge]) < 1.5f;
                valid = valid && left[0] < 0 && left[1] > 0 && left[2] > 0 && left[3] < 0 && right[0] < 0 &&
                        right[1] > 0 && right[2] > 0 && right[3] < 0;
                if (valid)
                {
                    const float x = ExpandVisibilityTangent(
                        std::tan(std::max(std::max(-left[0], left[1]), std::max(-right[0], right[1]))));
                    const float y = ExpandVisibilityTangent(
                        std::tan(std::max(std::max(left[2], -left[3]), std::max(right[2], -right[3]))));
                    float* tangents = reinterpret_cast<float*>(static_cast<char*>(dst) + 0x28);
                    if (std::isfinite(tangents[0]) && std::isfinite(tangents[1]) && tangents[0] > 0 && tangents[1] > 0)
                    {
                        tangents[0] = std::max(tangents[0], x);
                        tangents[1] = std::max(tangents[1], y);
                    }
                }
            }
        }
        return result;
    }

    void __fastcall ObserverCameraEffectHook(int userIndex)
    {
        // Log-only vehicle probe: capture the slot-0 observer record's float
        // window (pre-effect, i.e. the stable camera) for the worker.
        if (userIndex == 0)
            Halo3CaptureObserver();
        // Halo applies weapon recoil, explosions, and other authored camera
        // impulses after it computes the observer's stable camera result. A
        // monitor can move the view independently of the player; an HMD must
        // never do that. Keep the stock effects when VR head tracking is off,
        // but make the tracked headset the sole owner of the VR view pose.
        if (!g_enabled.load(std::memory_order_relaxed))
            g_origObserverCameraEffect(userIndex);
    }

    void __fastcall RenderViewHook(void* view)
    {
        if (!VR_IsStereoEnabled() || !g_enabled.load() || !view)
        {
            g_origRenderView(view);
            return;
        }

        // Compact camera produced by CamCopy: position +0x00, forward +0x0C,
        // up +0x18. It begins at view+0x08. Snapshot it so both eye calls start
        // from identical frame state and the engine sees its original afterward.
        char* camera = reinterpret_cast<char*>(view) + 8;
        alignas(16) unsigned char saved[0x90];
        alignas(16) unsigned char savedDerived[0x90];
        alignas(16) unsigned char savedCameraCopy[0x90];
        alignas(16) unsigned char savedDerivedCopy[0x90];
        memcpy(saved, camera, sizeof(saved));
        memcpy(savedDerived, reinterpret_cast<char*>(view) + 0x98, sizeof(savedDerived));
        memcpy(savedCameraCopy, reinterpret_cast<char*>(view) + 0x158, sizeof(savedCameraCopy));
        memcpy(savedDerivedCopy, reinterpret_cast<char*>(view) + 0x1E8, sizeof(savedDerivedCopy));
        const bool cutsceneTheater = VR_IsCutsceneTheaterActive();
        const float* authoredTangents = reinterpret_cast<const float*>(saved + 0x28);
        const float authoredAspect = CutsceneTheaterAspectFromTangents(authoredTangents[0], authoredTangents[1]);
        const uint32_t theaterGeneration = g_halo3RuntimeGeneration.load(std::memory_order_relaxed);
        if (theaterGeneration && authoredAspect > 0.0f && (!cutsceneTheater || CinematicFovPolicyStockReady()))
        {
            TitleAdapter_PublishCutsceneTheaterProjection(
                GameTitle::Halo3, theaterGeneration, authoredAspect, GetTickCount64());
        }
        const float* fwd = reinterpret_cast<const float*>(saved + 0x0C);
        const float* up = reinterpret_cast<const float*>(saved + 0x18);
        float right[3] = {
            fwd[1] * up[2] - fwd[2] * up[1], fwd[2] * up[0] - fwd[0] * up[2], fwd[0] * up[1] - fwd[1] * up[0]};
        // STEREO GHOSTING - root cause finally OBSERVED (2026-07-14, the
        // CopyResource probe): between the eye passes, the engine snapshots
        // the full-resolution scene into a sampleable texture
        // (COPY eye=-1, 2912x2100 fmt29 -> fmt29) - its "last frame"
        // source for temporal effects. In stereo that snapshot is made from
        // whichever eye rendered LAST, and BOTH eyes sample it next frame:
        // the last eye reads itself (clean), the first eye reads the other
        // eye (trailing after-images offset by the eye separation). This
        // explains every earlier result: fixed order -> steady first-eye
        // ghost; alternation -> flicker; a discarded warm-up render -> clean
        // (it flushed the foreign snapshot through the effect chain) at the
        // cost of a third render (60 fps).
        //
        // The fix (vr.cpp, VR_Begin/EndRasterEye + the CopyResource hook):
        // keep a per-eye copy of that snapshot - captured after each eye's
        // own render, substituted into the game's snapshot texture right
        // before that eye renders again. Each eye then always samples its own
        // previous frame. Three texture copies per frame instead of a third
        // world render, so this runs at the full two-render rate.
        // Cache visible FP palette solves only within this one stereo pair.
        // Exact input matching in ReconstructVisiblePaletteSource keeps any
        // changed animation pass on the existing full-solve path.
        PublishHalo3DualAim();
        g_fpStereoSolveScope = {};
        g_fpStereoSolveScope.armed = true;
        g_fpStereoSolveScope.twoHandAimActive = VR_IsTwoHandAiming();
        (void)VR_GetContactTrackingSnapshot(g_fpStereoSolveScope.anatomicalTracking);
        BeginHalo3AvatarPair(saved);
        __try
        {
            const int firstEye = g_config.right_eye_first ? 1 : 0;
            for (int pass = 0; pass < 2; ++pass)
            {
                const int eye = pass == 0 ? firstEye : 1 - firstEye;
                g_stereoEye = eye;
                VR_BeginRasterEye(eye);
                memcpy(camera, saved, sizeof(saved));
                float* pos = reinterpret_cast<float*>(camera);
                const float sign = eye == 0 ? -1.0f : 1.0f;
                // Render from the exact eye offsets returned by xrLocateViews.
                // Quest 2's physical lens spacing is adjustable, and OpenXR may
                // also report non-horizontal offsets. A fixed PSVR2 IPD makes the
                // raster disparity disagree with the poses sent to the compositor,
                // which presents as two images that cannot be fused. Keep the old
                // 67.5 mm baseline only as a safe fallback before views are valid.
                float eyePosition[3] = {sign * 0.5f * 0.0675f, 0.0f, 0.0f};
                float eyeOrientation[4]{};
                const bool haveEyeView = VR_GetEyeViewOffset(eye, eyePosition, eyeOrientation);
                ApplyCutsceneTheaterEyeTransform(
                    cutsceneTheater, g_config.cutscene_theater_depth, eyePosition, eyeOrientation);
                const float eyeScale = g_worldScale.load();
                for (int axis = 0; axis < 3; ++axis)
                    pos[axis] +=
                        (right[axis] * eyePosition[0] + up[axis] * eyePosition[1] - fwd[axis] * eyePosition[2]) *
                        eyeScale;

                // Cant: PSVR2 mounts each display angled outward a few degrees,
                // and the per-eye FOV OpenXR reports is measured around that
                // canted axis. Turn this eye's raster camera by the same relative
                // rotation (OpenXR view axes +X/+Y/+Z=right/up/-forward mapped
                // onto the camera basis) so the raster covers exactly what the
                // compositor displays; rendering both eyes straight ahead leaves
                // the outward lens edge uncovered = black border per eye. The
                // matching per-eye orientation is submitted in vr.cpp. (Assumes
                // the default yaw/pitch mapping; F4/F5 flips would mirror it.)
                if (haveEyeView)
                {
                    const float sinHalf =
                        sqrtf(eyeOrientation[0] * eyeOrientation[0] + eyeOrientation[1] * eyeOrientation[1] +
                              eyeOrientation[2] * eyeOrientation[2]);
                    if (sinHalf > 1e-5f)
                    {
                        float angle = 2.0f * atan2f(sinHalf, eyeOrientation[3]);
                        if (angle > 3.14159265f)
                            angle -= 6.2831853f; // shortest arc
                        const float ax = eyeOrientation[0] / sinHalf;
                        const float ay = eyeOrientation[1] / sinHalf;
                        const float az = eyeOrientation[2] / sinHalf;
                        const float axis[3] = {ax * right[0] + ay * up[0] - az * fwd[0],
                            ax * right[1] + ay * up[1] - az * fwd[1], ax * right[2] + ay * up[2] - az * fwd[2]};
                        const float cosA = cosf(angle), sinA = sinf(angle);
                        RotateAboutAxis(reinterpret_cast<float*>(camera + 0x0C), axis, cosA, sinA);
                        RotateAboutAxis(reinterpret_cast<float*>(camera + 0x18), axis, cosA, sinA);
                    }
                }

                // Rebuild exactly the same derived blocks as the engine's camera
                // setup function. Without this, changing the compact camera here
                // is too late and the GPU keeps using the center-eye matrices.
                alignas(16) unsigned char temporary[0x40]{};
                if (g_buildViewport && g_buildMatrices)
                {
                    float eyeFov[4];
                    float immersiveTangentX = tanf(1.07338f);
                    float immersiveTangentY = tanf(0.92502f);
                    if (VR_GetEyeFov(eye, eyeFov))
                    {
                        immersiveTangentX = tanf(fmaxf(-eyeFov[0], eyeFov[1]));
                        immersiveTangentY = tanf(fmaxf(eyeFov[2], -eyeFov[3]));
                    }
                    const bool preserveAuthoredProjection =
                        UseAuthoredCutsceneProjection(cutsceneTheater, authoredAspect) &&
                        CinematicFovPolicyStockReady();
                    Halo3CutsceneTheaterFrustum theaterFrustum =
                        SelectHalo3CutsceneTheaterFrustum(preserveAuthoredProjection, authoredTangents[0],
                            authoredTangents[1], immersiveTangentX, immersiveTangentY);
                    if (!theaterFrustum.valid)
                    {
                        // Invalid authored metadata rejects only the theatre FOV
                        // override. The proven immersive projection/culling path
                        // still completes both eye renders and the VR session.
                        theaterFrustum =
                            SelectHalo3CutsceneTheaterFrustum(false, 0.0f, 0.0f, immersiveTangentX, immersiveTangentY);
                    }
                    // Native R3 may narrow Halo's compact camera. Keep the headset
                    // view and its culling volume at the normal OpenXR FOV in
                    // immersive VR; only the separate scope pass consumes the
                    // weapon zoom. Theatre deliberately separates the compact
                    // visibility tangents from the derived raster projection:
                    // authored framing stays slightly tighter while visibility
                    // retains the proven immersive cover.
                    float* cameraTangents = reinterpret_cast<float*>(camera + 0x28);
                    cameraTangents[0] = preserveAuthoredProjection
                                            ? theaterFrustum.cullingTangentX
                                            : ExpandVisibilityTangent(theaterFrustum.cullingTangentX);
                    cameraTangents[1] = preserveAuthoredProjection
                                            ? theaterFrustum.cullingTangentY
                                            : ExpandVisibilityTangent(theaterFrustum.cullingTangentY);
                    g_buildViewport(camera, temporary);
                    g_buildMatrices(camera, temporary, reinterpret_cast<char*>(view) + 0x98, 0.0f);
                    const float* finalProjection =
                        reinterpret_cast<const float*>(reinterpret_cast<const char*>(view) + 0x98 + 0x78);
                    float* vrProjection = reinterpret_cast<float*>(reinterpret_cast<char*>(view) + 0x98 + 0x78);
                    vrProjection[0] = 1.0f / theaterFrustum.projectionTangentX;
                    vrProjection[5] = 1.0f / theaterFrustum.projectionTangentY;
                    // In Halo 3's row-vector projection layout, index 9 is the
                    // vertical center term. Preserve the engine's current value
                    // (including its tiny per-frame offset) and lift only the
                    // theatre capture; immersive policy returns an exact zero.
                    if (theaterFrustum.projectionCenterYAdjustment != 0.0f)
                    {
                        vrProjection[9] += theaterFrustum.projectionCenterYAdjustment;
                    }
                    finalProjection = vrProjection;
                    if (fabsf(finalProjection[0]) > 0.01f && fabsf(finalProjection[5]) > 0.01f)
                    {
                        g_renderHalfFovX = atanf(1.0f / fabsf(finalProjection[0]));
                        g_renderHalfFovY = atanf(1.0f / fabsf(finalProjection[5]));
                    }
                    // Capture the engine's real matrix layout before introducing
                    // off-axis center terms. This is logged once and left
                    // untouched so the diagnostic build remains distortion-free.
                    static std::atomic<bool> loggedProjection{false};
                    if (!loggedProjection.exchange(true))
                    {
                        const float* p =
                            reinterpret_cast<const float*>(reinterpret_cast<const char*>(view) + 0x98 + 0x78);
                        LOG("projection rows: [%.5f %.5f %.5f %.5f] [%.5f %.5f %.5f %.5f] [%.5f %.5f %.5f %.5f] [%.5f %.5f %.5f %.5f]",
                            p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13],
                            p[14], p[15]);
                    }
                    // Optional DLSS: jitter this eye's projection centre by the
                    // sub-pixel offset vr.cpp asked for (only while DLSS is
                    // consuming frames) and publish the exact camera rasterized.
                    // Theatre consumes the same reconstructed surface, so its
                    // authored projection also needs the sub-pixel sample offsets.
                    {
                        float jitterNdcX = 0.0f, jitterNdcY = 0.0f;
                        // Halo's w = -z projection flips the centre terms, so the
                        // offset ADDED to [8]/[9] is jitter * sign (live matrix:
                        // [11] = -1); the sample carries the nominal jitter.
                        const float centerSign = dlss::ProjectionCenterSign(vrProjection);
                        if (VR_GetEyeJitter(eye, jitterNdcX, jitterNdcY))
                        {
                            vrProjection[8] += jitterNdcX * centerSign;
                            vrProjection[9] += jitterNdcY * centerSign;
                        }
                        g_eyeProjectionJitterNdc[eye][0] = jitterNdcX * centerSign;
                        g_eyeProjectionJitterNdc[eye][1] = jitterNdcY * centerSign;
                        VrEyeCameraSample sample{};
                        memcpy(sample.position, camera + 0x00, sizeof(sample.position));
                        memcpy(sample.forward, camera + 0x0C, sizeof(sample.forward));
                        memcpy(sample.up, camera + 0x18, sizeof(sample.up));
                        memcpy(sample.projection, vrProjection, sizeof(sample.projection));
                        sample.jitterNdcX = jitterNdcX;
                        sample.jitterNdcY = jitterNdcY;
                        VR_PublishEyeCamera(eye, sample);
                    }
                    // {view+0x158, view+0x1E8} is a SECOND camera+derived pair on
                    // the main view, consumed by the vtable render method 0x28331C
                    // via 0x295DC0. It is NOT the first-person pair (that is the
                    // sub-view at view+0x6C8: compact +0x6D0, derived +0x8B0 -
                    // corrected 2026-07-19). Its exact role is OPEN again; the
                    // previous-frame/temporal-reprojection reading from the ghost
                    // notes is back on the table. These copies keep it coherent
                    // with the current eye camera, as shipped for a long time.
                    memcpy(reinterpret_cast<char*>(view) + 0x158, camera, 0x90);
                    memcpy(reinterpret_cast<char*>(view) + 0x1E8, reinterpret_cast<char*>(view) + 0x98, 0x90);
                }
                // Match the gun/HUD overlay frustum EXACTLY to the widened world
                // raster, otherwise the ~81 deg overlay stretched across the
                // ~123 deg frame magnifies the first-person weapon and HUD ~2x -
                // and any deliberate mismatch would also shift where the
                // hand-anchored weapon projects, breaking controller registration.
                // Written BEFORE the per-view preparation below so the tangents are
                // in place for everything preparation triggers.
                if (const uintptr_t gunCam = g_gunCamera.load())
                {
                    float* gunTan = reinterpret_cast<float*>(gunCam + kGunProjX);
                    static std::atomic<bool> loggedGunTan{false};
                    if (!loggedGunTan.exchange(true))
                        LOG("gun overlay tangents: game (%.4f, %.4f) -> world match (%.4f, %.4f)", gunTan[0], gunTan[1],
                            tanf(g_renderHalfFovX.load()), tanf(g_renderHalfFovY.load()));
                    gunTan[0] = tanf(g_renderHalfFovX.load());
                    gunTan[1] = tanf(g_renderHalfFovY.load());
                    // Experimental HUD sizing: the other three overlay cameras get
                    // a scaled frustum (>1 = smaller HUD). Element 0 (the weapon)
                    // is never scaled - its projection must match the world for
                    // controller registration. Default 1.0 = byte-identical no-op.
                    // Elements 1-3 are inactive split-screen player cameras, not
                    // independent HUD layers. HUD placement must be solved at the
                    // CHUD draw itself; never move this shared camera to the hand.
                }
                // THE DEPTH FIX (2026-07-19). The gun/HUD first-person layer is
                // drawn through the FP camera pair {view+0x158, view+0x1E8}, which
                // the engine builds with a CRUSHED viewmodel depth range (a thin
                // near-far slab so the gun never clips walls on a flat screen). In
                // VR that reads as an orthographic, flattened space: the gun looks
                // squashed, warps when twisted, and cannot move forward - the
                // exact headset report. The fix is to render the FP layer through
                // this eye's FULL WORLD camera + projection (position, orientation,
                // FOV AND depth terms), so the weapon lives in true world
                // perspective. The authoritative override happens during the
                // render in FpCameraRebuildHook (the engine rebuilds this pair
                // mid-render); this pre-render copy keeps it coherent beforehand.
                memcpy(reinterpret_cast<char*>(view) + 0x158, camera, 0x90);
                memcpy(reinterpret_cast<char*>(view) + 0x1E8, reinterpret_cast<char*>(view) + 0x98, 0x90);
                if (g_fpCameraUpload)
                    g_fpCameraUpload(reinterpret_cast<char*>(view) + 0x158, reinterpret_cast<char*>(view) + 0x1E8);
                // Snapshot this eye's finished camera + derived block and ARM the
                // FP hooks BEFORE the per-view preparation: the measured in-eye FP
                // driver runs (~3 per eye per frame, exactly-equal histogram
                // buckets) are triggered BY g_prepareView below - arming after it
                // meant every stamp silently missed (2026-07-19 evening logs).
                memcpy(g_eyeCompactCamera, camera, sizeof(g_eyeCompactCamera));
                memcpy(g_eyeDerivedBlock, reinterpret_cast<char*>(view) + 0x98, sizeof(g_eyeDerivedBlock));
                g_eyeFpView.store(view, std::memory_order_release);
                // The draw routine consumes camera state uploaded to engine globals
                // by this per-view preparation stage, not the view structure
                // directly. Re-run it after each eye matrix rebuild.
                if (g_prepareView)
                    g_prepareView(view, 0);
                g_origRenderView(view);
                g_eyeFpView.store(nullptr, std::memory_order_release);
                VR_CaptureRenderedEye(eye);
                VR_EndRasterEye();
            }
        }
        __finally
        {
            EndHalo3AvatarPair();
            g_fpStereoSolveScope.armed = false;
        }
        g_stereoEye = -1;
        g_eyeFpView.store(nullptr, std::memory_order_release);

        // A faulted generation never re-enters the pass (the cockpit branch renders
        // regardless of Game_HasScopeRenderer) and shows no cockpit screen.
        if (g_halo3ScopeFaultGeneration.load(std::memory_order_acquire) ==
                g_halo3RuntimeGeneration.load(std::memory_order_acquire) &&
            g_halo3ScopeFaultGeneration.load(std::memory_order_acquire) != 0)
            VR_NoteCockpitScreen(false);
        else if (!Halo3GuardedScopePass(view, camera, saved))
            LOG("Halo 3 scope StockFallback: the optional scope/cockpit view faulted (%llu); scope off for this "
                "module generation, camera core retained",
                static_cast<unsigned long long>(g_halo3ScopeFaults.load(std::memory_order_relaxed)));
        memcpy(camera, saved, sizeof(saved));
        memcpy(reinterpret_cast<char*>(view) + 0x98, savedDerived, sizeof(savedDerived));
        memcpy(reinterpret_cast<char*>(view) + 0x158, savedCameraCopy, sizeof(savedCameraCopy));
        memcpy(reinterpret_cast<char*>(view) + 0x1E8, savedDerivedCopy, sizeof(savedDerivedCopy));
    }
    // Byte pattern of the camera-copy function's prologue, with the RIP
    // displacement and the short-jump offset wildcarded. Found by signature so
    // the mod survives MCC updates that shift addresses (per the project rules).
    //   mov [rsp+8],rbx; push rdi; sub rsp,0x30; movaps [rsp+0x20],xmm6;
    //   mov rdi,rdx; mov rbx,rcx; test rdx,rdx; je short ??; movss xmm3,[rip+??]
    constinit const char* kCamCopySig =
        "48 89 5C 24 08 57 48 83 EC 30 0F 29 74 24 20 48 8B FA 48 8B D9 48 85 D2 74 ?? F3 0F 10 1D ?? ?? ?? ??";
    // halo3.dll+0x286A14 in build 1.3528: inner per-view renderer, called by
    // the engine's native view loop with rcx = the prepared view structure.
    constinit const char* kRenderViewSig =
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 40 8B 3D ?? ?? ?? ?? 48 8B F1 85 FF 0F 84 ?? ?? ?? ??";
    constinit const char* kPrepareViewSig =
        "48 89 5C 24 08 57 48 83 EC 20 83 3D ?? ?? ?? ?? 03 8B FA 48 8B D9 48 89 0D ?? ?? ?? ??";
    constinit const char* kBuildViewportSig =
        "40 53 48 83 EC 30 44 0F BF 49 62 4C 8B D9 4C 8B 41 38 48 8B DA 0F BF 51 50";
    constinit const char* kBuildMatricesSig =
        "48 8B C4 48 89 58 08 48 89 78 10 55 48 8D 68 E8 48 81 EC 10 01 00 00 80 3D ?? ?? ?? ?? 00";
    // Start of the engine function that constructs the 4-slot camera-object
    // array: mov [rsp+8],rbx; push rdi; sub rsp,0x20; lea rbx,[rip+array];
    // mov edi,4; mov rcx,rbx; call ctor; add rbx,0x2820; sub rdi,1; jnz.
    // The lea's RIP displacement is at match+13 and the instruction ends at
    // match+17, so the array (= gun/overlay camera) is match+17+disp32. The
    // 0x2820 stride distinguishes it from an identical builder of another
    // camera array.
    constinit const char* kGunCamRefSig =
        "48 89 5C 24 08 57 48 83 EC 20 48 8D 1D ?? ?? ?? ?? BF 04 00 00 00 48 8B CB E8 ?? ?? ?? ?? 48 81 C3 20 28 00 00 48 83 EF 01 75 EB";
    // Bone composition boundaries called by the first-person animator at
    // 0x2C4663/0x2C4633. The render packet copy happens immediately afterward.
    constinit const char* kComposeBonesSig =
        "45 85 C0 0F 8E ?? ?? ?? ?? 48 89 5C 24 08 57 48 83 EC 20 45 8B D0 49 8B F9 4C 8B C9";
    constinit const char* kComposeSpecialBonesSig =
        "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 4C 89 60 20 55 41 55 41 56 48 8D 68 B8";
    constinit const char* kFpInterpolateSig =
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 54 41 55 41 56 41 57 48 83 EC 20 33 DB 49 63 E8";
    // Final visible first-person skin palette consumer. Unlike 0x2C13B8
    // (marker/effect packets), this function maps interpolated bones into the
    // actual render palette and receives the exact root as argument 2.
    constinit const char* kFpVisiblePaletteSig =
        "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 20 48 8B 05 ?? ?? ?? ?? 49 8B F0 0F B7 C9 4C 8B F2";
    // First-person camera rebuild (0x279BEC in build 1.3528). Re-copies the FP
    // compact camera from [view+0x2A8] (center pose), forces the viewmodel FOV,
    // derives view+0x1E8, then tail-jumps into the constant uploader. Hooked so
    // each eye render substitutes its own world camera afterwards.
    constinit const char* kFpCameraRebuildSig =
        "48 8B C4 48 89 58 08 48 89 70 10 57 48 83 EC 50 48 8D 79 08 0F 29 78 E8 F3 0F 10 3D ?? ?? ?? ?? 48 8B F1";
    // The uploader that rebuild tail-jumps into (0x2770F0):
    // fastcall(compactCamera, derivedBlock). The hook calls it again after the
    // substitution so the constants the original already pushed are redone.
    constinit const char* kFpCameraUploadSig =
        "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 18 55 48 8D 68 A1 48 81 EC C0 00 00 00 0F 29 70 E8 48 8B FA F3 0F 10 35 ?? ?? ?? ?? 48 8D 55 B7";

    // Halo 3 vehicle-detection identities.
    // Each measured exactly one raw match in the pinned module, twice
    // (analyst + independent adversarial re-scan). Native unit_in_vehicle
    // seated-parent predicate (halo3.dll+0x3A36F4 in build 1.3528):
    constinit const char* kHalo3UnitInVehicleSig = "48 89 5C 24 08 57 48 83 EC 20 45 32 DB 41 83 C9 FF 41 3B C9 "
                                                   "0F 84 82 00 00 00 8B 15 ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 "
                                                   "41 B8 38 00 00 00 0F B7 C9 48 8B 04 D0 4A 8B 14 00 48 8D 04 49";
    // player_mapping_get_unit_by_output_user (+0xEE48C). The engine body has
    // no 0..3 bounds check of its own; the sampler passes only the constant 0.
    constinit const char* kHalo3PlayerUnitGetterSig =
        "83 C8 FF 3B C8 74 27 8B 15 ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 "
        "41 B8 10 01 00 00 48 63 C9 48 8B 04 D0 4A 8B 04 00 "
        "8B 84 88 C8 00 00 00 C3";
    // vehicle_definition_get_type (+0x396D84): cx = vehi tag index, returns
    // the first non-empty physics block 0..9 or 0xB when none is authored.
    constinit const char* kHalo3VehicleTypeAccessorSig =
        "48 8B 05 ?? ?? ?? ?? 0F B7 C9 8B 54 C8 04 85 D2 75 04 33 D2 EB 0B "
        "48 8B 05 ?? ?? ?? ?? 48 8D 14 90 33 C0 45 33 C0 "
        "48 81 C2 C0 02 00 00 B9 0B 00 00 00 83 3A 00";
    // Native render-node path. halo3.dll+0x1846AC returns the exact
    // interpolated 0x34-byte node bank consumed by both the visible-object
    // renderer (+0x3496F3 caller) and the native marker resolver
    // (+0x343E6F caller). Its fixed prologue and argument moves prove the ABI:
    // object handle, matrix-bank output, node-count output.
    inline constexpr uintptr_t kHalo3InterpolatedNodesExpectedRva = 0x1846AC;
    constinit const char* kHalo3InterpolatedNodesSig = "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 "
                                                       "57 41 54 41 55 41 56 41 57 48 83 EC 30 "
                                                       "4C 8B EA 0F 29 70 C8 48 8D 50 20 8B F9 4D 8B F0";
    // Internal object_get_markers_by_string_id implementation. The full
    // six-argument ABI is proven by Halo's unit-camera evaluator, which calls
    // it for the occupant head with localOnly=false/interpolateNodes=true.
    // The 0x70-byte output carries final world translation at +0x60.
    inline constexpr uintptr_t kHalo3MarkersInternalExpectedRva = 0x343D74;
    constinit const char* kHalo3MarkersInternalSig = "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 "
                                                     "66 44 89 48 20 57 41 54 41 55 41 56 41 57 48 83 EC 70 "
                                                     "45 33 F6 8B E9 49 8B F8 44 8B FA 8B D9 45 0F B7 DE";

    // Native pause state owner. The HaloScript external global named
    // game_paused is only a developer override and does not change when MCC's
    // real pause menu opens. Four alternating live snapshots plus a 2 ms
    // transition trace identified the engine flag written by this unique code
    // path: it changes before MCC's generic game-thread suspension flags on
    // both entry and exit. The final mov's RIP target is the pause byte.
    constinit const char* kNativePauseOwnerSig = "E8 ?? ?? ?? ?? 84 C0 74 18 B9 03 00 00 00 "
                                                 "E8 ?? ?? ?? ?? 84 C0 75 0A 8B D1 40 8A CE "
                                                 "E8 ?? ?? ?? ?? 40 88 35 ?? ?? ?? ?? E9 ?? ?? ?? ??";
    void LocateNativePauseFlag(uintptr_t base, size_t size)
    {
        const uintptr_t hit = sig::Find(base, size, kNativePauseOwnerSig);
        if (!hit || sig::Find(hit + 1, base + size - hit - 1, kNativePauseOwnerSig))
        {
            g_nativePauseFlag = 0;
            g_enginePauseValidated = false;
            LOG("pause state: native pause signature missing or ambiguous; "
                "using transition fallback");
            return;
        }

        // Signature starts at halo3.dll+0xB682 in build 1.3528. The target
        // instruction begins at +33, its disp32 is +36, and RIP after it is
        // +40. Resolve rather than retaining the observed +0xA3CA9A offset.
        const int32_t displacement = *reinterpret_cast<const int32_t*>(hit + 36);
        const uintptr_t flag = hit + 40 + displacement;
        if (flag < base || flag >= base + size)
        {
            g_nativePauseFlag = 0;
            g_enginePauseValidated = false;
            LOG("pause state: native pause signature resolved outside halo3.dll; "
                "using transition fallback");
            return;
        }

        const uint8_t initial = *reinterpret_cast<const uint8_t*>(flag);
        if (initial > 1)
        {
            g_nativePauseFlag = 0;
            g_enginePauseValidated = false;
            LOG("pause state: native pause flag failed boolean validation (%u); "
                "using transition fallback",
                static_cast<unsigned>(initial));
            return;
        }

        g_nativePauseFlag = flag;
        g_enginePauseValidated = true;
        LOG("pause state: authoritative native flag at halo3.dll+0x%llX "
            "(initial=%u, owner signature +0x%llX)",
            (unsigned long long)(flag - base), static_cast<unsigned>(initial), (unsigned long long)(hit - base));
    }
    __declspec(noinline) void* __fastcall GuardedCamCopyHook(void* dst, void* src)
    {
        g_halo3CoreCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            if (!g_halo3InstallReady.load(std::memory_order_acquire))
                return g_origCamCopy(dst, src);
            return CamCopyHook(dst, src);
        }
        __finally
        {
            g_halo3CoreCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    __declspec(noinline) void __fastcall GuardedObserverCameraEffectHook(int userIndex)
    {
        g_halo3CoreCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            if (!g_halo3InstallReady.load(std::memory_order_acquire))
                return g_origObserverCameraEffect(userIndex);
            return ObserverCameraEffectHook(userIndex);
        }
        __finally
        {
            g_halo3CoreCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    __declspec(noinline) void __fastcall GuardedComposeBonesHook(
        void* model, int start, int count, BoneMatrix* output, void* source, void* defaults)
    {
        g_halo3CoreCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            if (!g_halo3InstallReady.load(std::memory_order_acquire))
                return g_origComposeBones(model, start, count, output, source, defaults);
            return ComposeBonesHook(model, start, count, output, source, defaults);
        }
        __finally
        {
            g_halo3CoreCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    __declspec(noinline) void __fastcall GuardedComposeSpecialBonesHook(
        void* model, BoneMatrix* output, void* source, void* defaults, int firstSpecial, int secondSpecial)
    {
        g_halo3CoreCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            if (!g_halo3InstallReady.load(std::memory_order_acquire))
                return g_origComposeSpecialBones(model, output, source, defaults, firstSpecial, secondSpecial);
            return ComposeSpecialBonesHook(model, output, source, defaults, firstSpecial, secondSpecial);
        }
        __finally
        {
            g_halo3CoreCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    __declspec(noinline) bool __fastcall GuardedFpInterpolateHook(
        int view, int id, int slot, BoneMatrix** outBones, int* outCount)
    {
        g_halo3CoreCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            if (!g_halo3InstallReady.load(std::memory_order_acquire))
                return g_origFpInterpolate(view, id, slot, outBones, outCount);
            return FpInterpolateHook(view, id, slot, outBones, outCount);
        }
        __finally
        {
            g_halo3CoreCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    __declspec(noinline) void __fastcall GuardedFpVisiblePaletteHook(uint16_t tag, const BoneMatrix* root,
        BoneMatrix* destination, uintptr_t unused, const BoneMatrix* source, const int32_t* boneMap)
    {
        g_halo3CoreCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            if (!g_halo3InstallReady.load(std::memory_order_acquire))
                return g_origFpVisiblePalette(tag, root, destination, unused, source, boneMap);
            return FpVisiblePaletteHook(tag, root, destination, unused, source, boneMap);
        }
        __finally
        {
            g_halo3CoreCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    __declspec(noinline) void __fastcall GuardedFpCameraRebuildHook(void* view, unsigned char flag)
    {
        g_halo3CoreCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            if (!g_halo3InstallReady.load(std::memory_order_acquire))
                return g_origFpCameraRebuild(view, flag);
            return FpCameraRebuildHook(view, flag);
        }
        __finally
        {
            g_halo3CoreCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    __declspec(noinline) void __fastcall GuardedHudXformHook(float x, float y, float z)
    {
        g_halo3CoreCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            if (!g_halo3InstallReady.load(std::memory_order_acquire))
                return g_realHudXform(x, y, z);
            return HudXformHook(x, y, z);
        }
        __finally
        {
            g_halo3CoreCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    __declspec(noinline) bool __fastcall GuardedHudAnchorBasisHook(
        int userIndex, void* drawWidgetData, int anchorType, void* basis)
    {
        g_halo3CoreCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            if (!g_halo3InstallReady.load(std::memory_order_acquire))
                return g_realHudAnchorBasis(userIndex, drawWidgetData, anchorType, basis);
            return HudAnchorBasisHook(userIndex, drawWidgetData, anchorType, basis);
        }
        __finally
        {
            g_halo3CoreCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    __declspec(noinline) void __fastcall GuardedHudPlaceHook(
        void* context, void* widgetDefinition, void* placementInput, void* placementOutput)
    {
        g_halo3CoreCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            if (!g_halo3InstallReady.load(std::memory_order_acquire))
                return g_realHudPlace(context, widgetDefinition, placementInput, placementOutput);
            return HudPlaceHook(context, widgetDefinition, placementInput, placementOutput);
        }
        __finally
        {
            g_halo3CoreCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    __declspec(noinline) bool __fastcall GuardedHudCrosshairVisibleHook(int userIndex)
    {
        g_halo3CoreCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            if (!g_halo3InstallReady.load(std::memory_order_acquire))
                return g_realHudCrosshairVisible(userIndex);
            return HudCrosshairVisibleHook(userIndex);
        }
        __finally
        {
            g_halo3CoreCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    __declspec(noinline) void __fastcall GuardedHudDrawWidgetHook(
        int userIndex, void* descriptor, unsigned short widgetIndex, unsigned char useAlternatePath, void* drawState)
    {
        g_halo3CoreCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            if (!g_halo3InstallReady.load(std::memory_order_acquire))
                return g_realHudDrawWidget(userIndex, descriptor, widgetIndex, useAlternatePath, drawState);
            return HudDrawWidgetHook(userIndex, descriptor, widgetIndex, useAlternatePath, drawState);
        }
        __finally
        {
            g_halo3CoreCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    __declspec(noinline) void __fastcall GuardedFpDriverHook(void* view, unsigned char flag)
    {
        g_halo3CoreCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            if (!g_halo3InstallReady.load(std::memory_order_acquire))
                return g_origFpDriver(view, flag);
            return FpDriverHook(view, flag);
        }
        __finally
        {
            g_halo3CoreCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    __declspec(noinline) void __fastcall GuardedRenderViewHook(void* view)
    {
        g_halo3CoreCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            if (!g_halo3InstallReady.load(std::memory_order_acquire))
                return g_origRenderView(view);
            return RenderViewHook(view);
        }
        __finally
        {
            g_halo3CoreCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }
} // namespace
namespace game_core
{
    bool RemoveHalo3CameraHooks()
    {
        g_halo3InstallReady.store(false, std::memory_order_release);
        g_halo3WorldCollision.runtimeBoundsProven.store(false, std::memory_order_release);
        g_halo3WorldCollision.installed.store(false, std::memory_order_release);
        const bool disabled = g_halo3Hooks.DisableAll([](void* target)
            { return MCCVR_DisableHookForRetirement(target); }, [](void* target, MH_STATUS status)
            { LOG("Halo 3 core cleanup pending: disable target=%p status=%d", target, static_cast<int>(status)); });
        if (!disabled)
            return false;
        {
            const void* functions[]{reinterpret_cast<const void*>(&GuardedCamCopyHook),
                reinterpret_cast<const void*>(&GuardedObserverCameraEffectHook),
                reinterpret_cast<const void*>(&GuardedComposeBonesHook),
                reinterpret_cast<const void*>(&GuardedComposeSpecialBonesHook),
                reinterpret_cast<const void*>(&GuardedFpInterpolateHook),
                reinterpret_cast<const void*>(&GuardedFpVisiblePaletteHook),
                reinterpret_cast<const void*>(&GuardedFpCameraRebuildHook)};
            const void* originals[]{reinterpret_cast<const void*>(g_origCamCopy),
                reinterpret_cast<const void*>(g_origObserverCameraEffect),
                reinterpret_cast<const void*>(g_origComposeBones),
                reinterpret_cast<const void*>(g_origComposeSpecialBones),
                reinterpret_cast<const void*>(g_origFpInterpolate),
                reinterpret_cast<const void*>(g_origFpVisiblePalette),
                reinterpret_cast<const void*>(g_origFpCameraRebuild)};
            if (!WaitForNativeDetourQuiescence(functions, originals, std::size(functions), g_halo3CoreCallbacks))
            {
                LOG("Halo 3 core cleanup pending: render/camera callbacks or ingress busy");
                return false;
            }
        }
        {
            const void* functions[]{reinterpret_cast<const void*>(&GuardedHudXformHook),
                reinterpret_cast<const void*>(&GuardedHudAnchorBasisHook),
                reinterpret_cast<const void*>(&GuardedHudPlaceHook),
                reinterpret_cast<const void*>(&GuardedHudCrosshairVisibleHook),
                reinterpret_cast<const void*>(&GuardedHudDrawWidgetHook),
                reinterpret_cast<const void*>(&GuardedFpDriverHook),
                reinterpret_cast<const void*>(&GuardedRenderViewHook)};
            const void* originals[]{reinterpret_cast<const void*>(g_realHudXform),
                reinterpret_cast<const void*>(g_realHudAnchorBasis), reinterpret_cast<const void*>(g_realHudPlace),
                reinterpret_cast<const void*>(g_realHudCrosshairVisible),
                reinterpret_cast<const void*>(g_realHudDrawWidget), reinterpret_cast<const void*>(g_origFpDriver),
                reinterpret_cast<const void*>(g_origRenderView)};
            if (!WaitForNativeDetourQuiescence(functions, originals, std::size(functions), g_halo3CoreCallbacks))
            {
                LOG("Halo 3 core cleanup pending: render/camera callbacks or ingress busy");
                return false;
            }
        }
        if (g_halo3WorldCollision.target)
        {
            const void* functions[]{reinterpret_cast<const void*>(&Halo3CollisionResolveDetour)};
            const void* originals[]{reinterpret_cast<const void*>(g_halo3WorldCollision.original)};
            if (!WaitForNativeDetourQuiescence(functions, originals, 1, g_halo3WorldCollision.callbacks))
            {
                LOG("Halo 3 core cleanup pending: world-contact callbacks or ingress busy");
                return false;
            }
        }
        Halo3RestoreNativeSeatPatch();
        // Optional publishers are now stopped too. Retire every old feature
        // before any new module can overwrite its target/original pair.
        if (!RemoveHalo3Avatar())
            return false;
        if (!RemoveHalo3WeaponHaptics())
            return false;
        if (!RemoveHalo3DualAim())
            return false;
        if (!DisableAndRemoveHalo3ContactMelee())
            return false;
        if (!g_halo3Hooks.RemoveAllNewestFirst([](void* target) { return MCCVR_RemoveHookChecked(target); },
                [](void* target, MH_STATUS status)
                { LOG("Halo 3 core cleanup pending: remove target=%p status=%d", target, static_cast<int>(status)); }))
            return false;
        g_renderHooked = false;
        g_fpInterpolatorHooked = false;
        g_halo3WorldCollision.target = nullptr;
        g_halo3WorldCollision.original = nullptr;
        g_halo3WorldCollision.resolver = nullptr;
        return true;
    }
} // namespace game_core
namespace
{
    // Creates a Halo 3 detour and queues it for the install's atomic enable. On
    // success the target joins g_halo3Hooks; on failure a created hook is removed
    // again and its trampoline cleared, so a half-installed hook is never left
    // behind (the FP-driver and render-view sites used to skip that).
    bool QueueHalo3Hook(uintptr_t target, void* detour, void** original)
    {
        void* const address = reinterpret_cast<void*>(target);
        const MH_STATUS created = MCCVR_CreateHookChecked(address, detour, original);
        if (created == MH_OK && MH_QueueEnableHook(address) == MH_OK)
        {
            RememberInstalledGameHook(address);
            return true;
        }
        if (created == MH_OK)
        {
            MCCVR_RemoveHookChecked(address);
            *original = nullptr;
        }
        return false;
    }
} // namespace
namespace game_core
{
    bool InstallHalo3CameraHooks(uintptr_t base, size_t size, uint32_t runtimeGeneration)
    {
        if (!runtimeGeneration)
            return false;
        LocateNativePauseFlag(base, size);
        LocateCinematicState(base, size);
        uintptr_t hit = sig::Find(base, size, kCamCopySig);
        if (!hit)
        {
            LOG("camera signature NOT FOUND - MCC may have updated. Head tracking is");
            LOG("disabled; the game and the VR screen still work normally.");
            return false;
        }
        // Uniqueness check - if the pattern matched twice we can't trust it.
        const uintptr_t after = hit + 1;
        if (sig::Find(after, base + size - after, kCamCopySig))
        {
            LOG("camera signature is not unique; leaving stock Halo 3 untouched");
            return false;
        }
        LOG("camera-copy found by signature at halo3.dll+0x%llX (expected 0x%llX for build 1.3528)",
            (unsigned long long)(hit - base), (unsigned long long)kCamCopyRva);

        void* target = reinterpret_cast<void*>(hit);
        g_halo3RuntimeGeneration.store(runtimeGeneration, std::memory_order_release);
        g_halo3LastCamCopyMs.store(0, std::memory_order_release);
        // A new module generation must begin disarmed even if the prior title
        // instance or an early manual toggle left the shared flag set.
        g_enabled.store(false, std::memory_order_release);
        g_autoVrOwned.store(false, std::memory_order_release);
        g_autoVrUserVeto.store(false, std::memory_order_release);
        TitleAdapter_ClearHeartbeat(GameTitle::Halo3, runtimeGeneration);
        TitleAdapter_ClearCinematicControl(GameTitle::Halo3, runtimeGeneration);
        const MH_STATUS createStatus = MCCVR_CreateHookChecked(
            target, reinterpret_cast<void*>(&GuardedCamCopyHook), reinterpret_cast<void**>(&g_origCamCopy));
        if (createStatus != MH_OK)
        {
            LOG("FAILED to create camera-copy hook at %p (%d); "
                "head tracking unavailable",
                target, static_cast<int>(createStatus));
            TitleAdapter_ClearHeartbeat(GameTitle::Halo3, runtimeGeneration);
            TitleAdapter_ClearCinematicControl(GameTitle::Halo3, runtimeGeneration);
            g_halo3RuntimeGeneration.store(0, std::memory_order_release);
            return false;
        }
        const MH_STATUS enableStatus = MH_QueueEnableHook(target);
        if (enableStatus != MH_OK)
        {
            const MH_STATUS disableStatus = MH_DisableHook(target);
            const MH_STATUS removeStatus = MCCVR_RemoveHookChecked(target);
            LOG("FAILED to enable camera-copy hook at %p (%d); "
                "rollback disable=%d remove=%d",
                target, static_cast<int>(enableStatus), static_cast<int>(disableStatus),
                static_cast<int>(removeStatus));
            if (removeStatus == MH_OK || removeStatus == MH_ERROR_NOT_CREATED)
                g_origCamCopy = nullptr;
            TitleAdapter_ClearHeartbeat(GameTitle::Halo3, runtimeGeneration);
            TitleAdapter_ClearCinematicControl(GameTitle::Halo3, runtimeGeneration);
            g_halo3RuntimeGeneration.store(0, std::memory_order_release);
            return false;
        }
        if (!PublishHalo3Lifecycle(true, false, false))
        {
            LOG("title generation changed during camera install; rolling back");
            MH_DisableHook(target);
            MCCVR_RemoveHookChecked(target);
            TitleAdapter_ClearHeartbeat(GameTitle::Halo3, runtimeGeneration);
            TitleAdapter_ClearCinematicControl(GameTitle::Halo3, runtimeGeneration);
            g_halo3RuntimeGeneration.store(0, std::memory_order_release);
            g_origCamCopy = nullptr;
            return false;
        }
        TitleAdapter_PublishMode(GameTitle::Halo3, runtimeGeneration, RuntimeMode::Loading);
        LOG("camera hooked. F2 head tracking, F3 recenter, F6 leaning, F8/F9 pitch trim, F10 screen-follow (yaw/pitch/up flips: F1 menu)");

        RememberInstalledGameHook(target);
        // Comfort invariant: the OpenXR pose, not Halo's authored screen-shake
        // transform, owns the view while head tracking is active. This hook is
        // independent from stereo and fails open to Halo's stock behavior.
        {
            uintptr_t effectHit = sig::Find(base, size, kObserverCameraEffectSig);
            const bool uniqueEffect =
                effectHit && !sig::Find(effectHit + 1, base + size - effectHit - 1, kObserverCameraEffectSig);
            bool effectHooked = false;
            if (uniqueEffect)
            {
                effectHooked = QueueHalo3Hook(effectHit, reinterpret_cast<void*>(&GuardedObserverCameraEffectHook),
                    reinterpret_cast<void**>(&g_origObserverCameraEffect));
            }
            if (effectHooked)
                LOG("VR comfort: native camera recoil/shake suppressed at "
                    "halo3.dll+0x%llX while head tracking is active",
                    (unsigned long long)(effectHit - base));
            else
                LOG("VR comfort: camera-effect signature missing/ambiguous or "
                    "hook failed; native camera recoil/shake remains active");
        }

        Halo3PreparePhysicalCrouchCamera(base, size);

        // Halo 3 vehicle-camera transactions. The three established vehicle
        // identities own only the base sampler. Render-node interpolation and
        // animated-head lookup are independent optional features: either may
        // fall back to stock without disarming the base vehicle path or camera.
        {
            g_halo3VehicleBinding.store(
                static_cast<uint8_t>(Halo3VehicleBindingState::NotInstalled), std::memory_order_release);
            g_halo3NodeBinding.store(
                static_cast<uint8_t>(Halo3NodeBindingState::NotInstalled), std::memory_order_release);
            g_halo3HeadBinding.store(
                static_cast<uint8_t>(Halo3NodeBindingState::NotInstalled), std::memory_order_release);
            g_halo3VehicleSnapshot.store(0, std::memory_order_release);
            g_halo3PlayerUnitGetter = nullptr;
            g_halo3UnitInVehicle = nullptr;
            g_halo3VehicleTypeAccessor = nullptr;
            g_halo3InterpolatedNodes = nullptr;
            g_halo3MarkersInternal = nullptr;
            g_halo3TagInstanceTable = nullptr;
            g_halo3TagDataBase = nullptr;

            const uintptr_t nativeHit = sig::Find(base, size, kHalo3UnitInVehicleSig);
            const uintptr_t getterHit = sig::Find(base, size, kHalo3PlayerUnitGetterSig);
            const uintptr_t typeHit = sig::Find(base, size, kHalo3VehicleTypeAccessorSig);
            const bool nativeUnique =
                nativeHit && !sig::Find(nativeHit + 1, base + size - nativeHit - 1, kHalo3UnitInVehicleSig);
            const bool getterUnique =
                getterHit && !sig::Find(getterHit + 1, base + size - getterHit - 1, kHalo3PlayerUnitGetterSig);
            const bool typeUnique =
                typeHit && !sig::Find(typeHit + 1, base + size - typeHit - 1, kHalo3VehicleTypeAccessorSig);
            if (nativeUnique && getterUnique && typeUnique)
            {
                g_halo3UnitInVehicle = reinterpret_cast<Halo3UnitInVehicleFn>(nativeHit);
                g_halo3PlayerUnitGetter = reinterpret_cast<Halo3PlayerUnitGetterFn>(getterHit);
                g_halo3VehicleTypeAccessor = reinterpret_cast<Halo3VehicleTypeFn>(typeHit);
                // Decode the two rip-relative globals from the matched
                // accessor body (instruction layout: mov rax,[rip+d] at
                // +0 ends +7 = tag instance table; mov rax,[rip+d] at +22
                // ends +29 = tag data base). Self-anchoring, no absolute
                // address is shipped.
                g_halo3TagInstanceTable =
                    reinterpret_cast<void**>(typeHit + 7 + *reinterpret_cast<const int32_t*>(typeHit + 3));
                g_halo3TagDataBase =
                    reinterpret_cast<void**>(typeHit + 29 + *reinterpret_cast<const int32_t*>(typeHit + 25));
                g_halo3VehicleProbeKey.store(~0ull, std::memory_order_relaxed);
                g_halo3VehicleBinding.store(
                    static_cast<uint8_t>(Halo3VehicleBindingState::Installed), std::memory_order_release);
                LOG("H3 vehicle evidence: resolved unit_in_vehicle=+0x%llX "
                    "player_unit=+0x%llX type_accessor=+0x%llX "
                    "[unique; expected +0x%llX/+0x%llX/+0x%llX]; tag globals "
                    "instanceTable=+0x%llX tagBase=+0x%llX (decoded from the "
                    "matched body)",
                    (unsigned long long)(nativeHit - base), (unsigned long long)(getterHit - base),
                    (unsigned long long)(typeHit - base), (unsigned long long)kHalo3UnitInVehicleNativeRva,
                    (unsigned long long)kHalo3PlayerUnitGetterRva, (unsigned long long)kHalo3VehicleTypeAccessorRva,
                    (unsigned long long)(reinterpret_cast<uintptr_t>(g_halo3TagInstanceTable) - base),
                    (unsigned long long)(reinterpret_cast<uintptr_t>(g_halo3TagDataBase) - base));
            }
            else
            {
                g_halo3VehicleBinding.store(
                    static_cast<uint8_t>(Halo3VehicleBindingState::StockFallback), std::memory_order_release);
                LOG("H3 vehicle evidence: signature resolve failed "
                    "(native=%d/%d getter=%d/%d type=%d/%d found/unique); "
                    "vehicle transaction absent, all vehicle behavior stock; "
                    "camera core unaffected",
                    nativeHit ? 1 : 0, nativeUnique ? 1 : 0, getterHit ? 1 : 0, getterUnique ? 1 : 0, typeHit ? 1 : 0,
                    typeUnique ? 1 : 0);
            }

            const uintptr_t nodeHit = sig::Find(base, size, kHalo3InterpolatedNodesSig);
            const bool nodeUnique =
                nodeHit && !sig::Find(nodeHit + 1, base + size - nodeHit - 1, kHalo3InterpolatedNodesSig);
            if (nodeUnique)
            {
                g_halo3InterpolatedNodes = reinterpret_cast<Halo3InterpolatedNodesFn>(nodeHit);
                g_halo3NodeBinding.store(
                    static_cast<uint8_t>(Halo3NodeBindingState::Installed), std::memory_order_release);
                LOG("H3 vehicle render nodes: Installed provider=+0x%llX "
                    "[unique; expected +0x%llX]",
                    (unsigned long long)(nodeHit - base), (unsigned long long)kHalo3InterpolatedNodesExpectedRva);
            }
            else
            {
                g_halo3NodeBinding.store(
                    static_cast<uint8_t>(Halo3NodeBindingState::StockFallback), std::memory_order_release);
                LOG("H3 vehicle render nodes: StockFallback "
                    "(provider=%d/%d found/unique, first=+0x%llX, "
                    "expected +0x%llX); smoothing ON stays stock, raw-node A/B "
                    "and camera core unaffected",
                    nodeHit ? 1 : 0, nodeUnique ? 1 : 0, (unsigned long long)(nodeHit ? nodeHit - base : 0),
                    (unsigned long long)kHalo3InterpolatedNodesExpectedRva);
            }

            const uintptr_t markerHit = sig::Find(base, size, kHalo3MarkersInternalSig);
            const bool markerUnique =
                markerHit && !sig::Find(markerHit + 1, base + size - markerHit - 1, kHalo3MarkersInternalSig);
            if (markerUnique)
            {
                g_halo3MarkersInternal = reinterpret_cast<Halo3MarkersInternalFn>(markerHit);
                g_halo3HeadBinding.store(
                    static_cast<uint8_t>(Halo3NodeBindingState::Installed), std::memory_order_release);
                LOG("H3 vehicle head bounce: Installed marker resolver=+0x%llX "
                    "[unique; expected +0x%llX]",
                    (unsigned long long)(markerHit - base), (unsigned long long)kHalo3MarkersInternalExpectedRva);
            }
            else
            {
                g_halo3HeadBinding.store(
                    static_cast<uint8_t>(Halo3NodeBindingState::StockFallback), std::memory_order_release);
                LOG("H3 vehicle head bounce: StockFallback "
                    "(resolver=%d/%d found/unique, first=+0x%llX, "
                    "expected +0x%llX); render-node feature, base vehicle "
                    "path, and camera core unaffected",
                    markerHit ? 1 : 0, markerUnique ? 1 : 0, (unsigned long long)(markerHit ? markerHit - base : 0),
                    (unsigned long long)kHalo3MarkersInternalExpectedRva);
            }
        }

        uintptr_t composeHit = sig::Find(base, size, kComposeBonesSig);
        uintptr_t specialHit = sig::Find(base, size, kComposeSpecialBonesSig);
        if (composeHit && specialHit && !sig::Find(composeHit + 1, base + size - composeHit - 1, kComposeBonesSig) &&
            !sig::Find(specialHit + 1, base + size - specialHit - 1, kComposeSpecialBonesSig))
        {
            // Both compositor callers and the allocator use the engine TLS
            // index global at halo3.dll+0xA39F9C. Resolve it through the proven
            // first-person update signature instead of baking that RVA.
            uintptr_t fp =
                sig::Find(base, size, "48 8B C4 44 88 40 18 89 50 10 89 48 08 55 53 56 57 41 54 41 55 41 56 41 57");
            if (fp)
            {
                const int32_t tlsDisp = *reinterpret_cast<const int32_t*>(fp + 0x62);
                g_engineTlsIndex = reinterpret_cast<uint32_t*>(fp + 0x66 + tlsDisp);
            }
            // composeHit+0x50 is mov rax,[rip+tag-data-base]; its displacement
            // resolves the node-record storage used for names and parents.
            const int32_t tagDataDisp = *reinterpret_cast<const int32_t*>(composeHit + 0x53);
            g_animationTagData = reinterpret_cast<unsigned char**>(composeHit + 0x57 + tagDataDisp);
            if (g_engineTlsIndex && g_animationTagData &&
                MCCVR_CreateHookChecked(reinterpret_cast<void*>(composeHit),
                    reinterpret_cast<void*>(&GuardedComposeBonesHook),
                    reinterpret_cast<void**>(&g_origComposeBones)) == MH_OK &&
                MCCVR_CreateHookChecked(reinterpret_cast<void*>(specialHit),
                    reinterpret_cast<void*>(&GuardedComposeSpecialBonesHook),
                    reinterpret_cast<void**>(&g_origComposeSpecialBones)) == MH_OK &&
                MH_QueueEnableHook(reinterpret_cast<void*>(composeHit)) == MH_OK &&
                MH_QueueEnableHook(reinterpret_cast<void*>(specialHit)) == MH_OK)
                LOG("first-person marker/muzzle matrix hooks installed at halo3.dll+0x%llX/+0x%llX",
                    (unsigned long long)(composeHit - base), (unsigned long long)(specialHit - base));
            else
                LOG("FAILED to hook first-person hand/weapon matrix composers");
        }
        else
            LOG("first-person hand/weapon matrix signatures missing or ambiguous");

        RememberInstalledGameHook(reinterpret_cast<void*>(composeHit));
        RememberInstalledGameHook(reinterpret_cast<void*>(specialHit));
        uintptr_t interpolateHit = sig::Find(base, size, kFpInterpolateSig);
        uintptr_t visiblePaletteHit = sig::Find(base, size, kFpVisiblePaletteSig);
        const bool uniqueInterpolate =
            interpolateHit && !sig::Find(interpolateHit + 1, base + size - interpolateHit - 1, kFpInterpolateSig);
        const bool uniqueVisiblePalette =
            visiblePaletteHit &&
            !sig::Find(visiblePaletteHit + 1, base + size - visiblePaletteHit - 1, kFpVisiblePaletteSig);
        bool visiblePathOk = false;
        if (uniqueInterpolate && uniqueVisiblePalette)
        {
            const MH_STATUS createInterpolate = MCCVR_CreateHookChecked(reinterpret_cast<void*>(interpolateHit),
                reinterpret_cast<void*>(&GuardedFpInterpolateHook), reinterpret_cast<void**>(&g_origFpInterpolate));
            const MH_STATUS createPalette = MCCVR_CreateHookChecked(reinterpret_cast<void*>(visiblePaletteHit),
                reinterpret_cast<void*>(&GuardedFpVisiblePaletteHook),
                reinterpret_cast<void**>(&g_origFpVisiblePalette));
            if (createInterpolate == MH_OK && createPalette == MH_OK)
                visiblePathOk = MH_QueueEnableHook(reinterpret_cast<void*>(interpolateHit)) == MH_OK &&
                                MH_QueueEnableHook(reinterpret_cast<void*>(visiblePaletteHit)) == MH_OK;
            if (!visiblePathOk)
            {
                MH_DisableHook(reinterpret_cast<void*>(interpolateHit));
                MH_DisableHook(reinterpret_cast<void*>(visiblePaletteHit));
                MCCVR_RemoveHookChecked(reinterpret_cast<void*>(interpolateHit));
                MCCVR_RemoveHookChecked(reinterpret_cast<void*>(visiblePaletteHit));
                g_origFpInterpolate = nullptr;
                g_origFpVisiblePalette = nullptr;
            }
        }
        RememberInstalledGameHook(reinterpret_cast<void*>(interpolateHit));
        RememberInstalledGameHook(reinterpret_cast<void*>(visiblePaletteHit));
        if (visiblePathOk)
        {
            g_fpInterpolatorHooked.store(true, std::memory_order_release);
            LOG("visible FP reconstruction hooked at halo3.dll+0x%llX -> +0x%llX "
                "(interpolation identity + final palette root; gun/arms only)",
                (unsigned long long)(interpolateHit - base), (unsigned long long)(visiblePaletteHit - base));
        }
        else
            LOG("complete visible FP path missing/ambiguous or hook failed; using sim fallback");

        // Native first-person weapon IK is a flat-screen support-hand system:
        // the animation graph can attach the arm's marker to a marker on the
        // weapon. H3EK proves the shotgun enables exactly
        // left_hand -> left_hand, with the target marker parented to pump node
        // 4; the AR's corresponding weapon-IK block is empty. Our controller
        // solver must own that arm instead. Redirect the one unique decision
        // to Halo's EXISTING no-weapon-IK state (mode 1/3), before any level is
        // evaluated. This is a two-byte startup patch, not a per-frame detour,
        // and preserves all authored fire/reload/melee animation channels.
        {
            const char* kFpNativeWeaponIkDecisionSig = "40 84 ED 74 05 45 84 FF 75 04 84 DB 74 0F BA 03 00 00 00 "
                                                       "41 0F 28 D8 44 8D 42 FF EB 11";
            uintptr_t decision = sig::Find(base, size, kFpNativeWeaponIkDecisionSig);
            const bool uniqueDecision =
                decision && !sig::Find(decision + 1, base + size - decision - 1, kFpNativeWeaponIkDecisionSig);
            if (uniqueDecision)
            {
                unsigned char* branch = reinterpret_cast<unsigned char*>(decision + 3);
                if (branch[0] == 0x74 && branch[1] == 0x05)
                {
                    DWORD oldProtect = 0;
                    if (VirtualProtect(branch, 2, PAGE_EXECUTE_READWRITE, &oldProtect))
                    {
                        // JE +5 (conditional native-IK decision) -> JMP +0x18
                        // (the stock no-weapon-IK branch at halo3+0x2C3959).
                        branch[0] = 0xEB;
                        branch[1] = 0x18;
                        FlushInstructionCache(GetCurrentProcess(), branch, 2);
                        DWORD ignored = 0;
                        VirtualProtect(branch, 2, oldProtect, &ignored);
                        LOG("arm IK: native first-person weapon IK disabled at "
                            "halo3.dll+0x%llX; controller solver owns support arms",
                            (unsigned long long)(decision - base));
                    }
                    else
                        LOG("arm IK: native weapon-IK branch protection failed; "
                            "shotgun support hand may stay attached to pump");
                }
                else
                    LOG("arm IK: native weapon-IK branch bytes changed; patch skipped");
            }
            else
                LOG("arm IK: native weapon-IK decision missing/ambiguous; patch skipped");
        }

        // The CHUD steal-and-requad machinery is GONE (2026-07-18): its three
        // CHUD hooks + draw-call classifier removed the native HUD from both
        // eyes, never displayed the hand quad, and its retry loop cost ~30 fps.
        // The native HUD renders untouched again; only the stock crosshair is
        // suppressed (our floating reticle replaces it).
        uintptr_t fpCamHit = sig::Find(base, size, kFpCameraRebuildSig);
        uintptr_t fpUploadHit = sig::Find(base, size, kFpCameraUploadSig);
        const bool uniqueFpCam = fpCamHit && !sig::Find(fpCamHit + 1, base + size - fpCamHit - 1, kFpCameraRebuildSig);
        const bool uniqueFpUpload =
            fpUploadHit && !sig::Find(fpUploadHit + 1, base + size - fpUploadHit - 1, kFpCameraUploadSig);
        bool fpCameraOk = false;
        if (uniqueFpCam && uniqueFpUpload)
        {
            fpCameraOk = QueueHalo3Hook(fpCamHit, reinterpret_cast<void*>(&GuardedFpCameraRebuildHook),
                reinterpret_cast<void**>(&g_origFpCameraRebuild));
        }
        RememberInstalledGameHook(reinterpret_cast<void*>(fpCamHit));
        if (fpCameraOk)
        {
            g_fpCameraUpload = reinterpret_cast<FpCameraUploadFn>(fpUploadHit);
            LOG("FP camera rebuild hooked at halo3.dll+0x%llX (uploader +0x%llX); "
                "gun/HUD layer renders per-eye",
                (unsigned long long)(fpCamHit - base), (unsigned long long)(fpUploadHit - base));
        }
        else
            LOG("FP camera rebuild signature missing/ambiguous; gun/HUD stay a mono flat layer");

        // GAME BRIGHTNESS: hook 0x278EE0 (once thought to size the HUD; the
        // headset proved it drives brightness). See HudXformHook - it scales the
        // two screen color/gamma floats by game_brightness. MinHook on the
        // function installs reliably. Prologue verified unique on disk (push rbp;
        // mov rbp,rsp; sub rsp,0x50; save xmm6/xmm7; the movaps arg shuffle).
        {
            const char* kHudXformSig = "40 55 48 8B EC 48 83 EC 50 0F 29 74 24 40 0F 28 F1 "
                                       "0F 29 7C 24 30 0F 28 CA 0F 28 F8";
            uintptr_t hudXform = sig::Find(base, size, kHudXformSig);
            RememberInstalledGameHook(reinterpret_cast<void*>(hudXform));
            if (hudXform && !sig::Find(hudXform + 1, base + size - hudXform - 1, kHudXformSig))
            {
                if (MCCVR_CreateHookChecked(reinterpret_cast<void*>(hudXform),
                        reinterpret_cast<void*>(&GuardedHudXformHook),
                        reinterpret_cast<void**>(&g_realHudXform)) == MH_OK &&
                    MH_QueueEnableHook(reinterpret_cast<void*>(hudXform)) == MH_OK)
                    LOG("brightness hook installed at halo3.dll+0x%llX "
                        "(game_brightness)",
                        (unsigned long long)(hudXform - base));
                else
                    LOG("brightness MH_CreateHook failed; brightness fixed at 1.0");
            }
            else
                LOG("brightness signature missing/ambiguous; brightness fixed at 1.0");
        }

        // HUD CROSSHAIR CLASS HIDER. H3EK and ManagedDonkey agree that class 2
        // is the authoritative crosshair marker. chud_draw_widget already checks
        // that class, but game_is_playback short-circuits the check during normal
        // play. NOP only that short-circuit and hook the existing class-gated
        // predicate; no tag-table reads are added to the render hook.
        {
            // True HUD height: translate Halo's computed CHUD anchor basis. The
            // prologue is unique in the current halo3.dll and fails open if MCC
            // changes it. The authored crosshair capture is excluded in the hook.
            const char* kHudAnchorBasisSig = "48 8B C4 48 89 58 08 48 89 70 10 48 89 78 20 55 "
                                             "41 54 41 55 41 56 41 57 48 8D 68 88 48 81 EC 50 01 00 00";
            uintptr_t anchorBasis = sig::Find(base, size, kHudAnchorBasisSig);
            const bool uniqueAnchorBasis =
                anchorBasis && !sig::Find(anchorBasis + 1, base + size - anchorBasis - 1, kHudAnchorBasisSig);
            bool anchorHookReady = false;
            if (uniqueAnchorBasis)
            {
                anchorHookReady = QueueHalo3Hook(anchorBasis, reinterpret_cast<void*>(&GuardedHudAnchorBasisHook),
                    reinterpret_cast<void**>(&g_realHudAnchorBasis));
            }
            if (anchorHookReady)
                LOG("HUD height anchor hook installed at halo3.dll+0x%llX", (unsigned long long)(anchorBasis - base));
            else
                LOG("HUD height anchor signature missing/ambiguous or hook failed; "
                    "height remains stock");

            // Halo 3's placement output is the exact boundary that writes the
            // computed authored colour vector at output+0x88. Its prologue is
            // unique in the pinned module; failure leaves the visible reticle
            // working and only its state-triggered colour refresh unavailable.
            const char* kHudPlaceSig = "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 "
                                       "41 54 41 56 41 57 48 81 EC 80 00 00 00 "
                                       "4C 8B 3D ?? ?? ?? ?? 49 8B F1 0F 29 70 D8 4D 8B E0 "
                                       "0F 29 78 C8 48 8B E9";
            uintptr_t hudPlace = sig::Find(base, size, kHudPlaceSig);
            const bool uniqueHudPlace = hudPlace && !sig::Find(hudPlace + 1, base + size - hudPlace - 1, kHudPlaceSig);
            bool hudPlaceHookReady = false;
            if (uniqueHudPlace)
            {
                hudPlaceHookReady = QueueHalo3Hook(
                    hudPlace, reinterpret_cast<void*>(&GuardedHudPlaceHook), reinterpret_cast<void**>(&g_realHudPlace));
            }
            if (hudPlaceHookReady)
                LOG("authored CHUD colour-state output hooked at "
                    "halo3.dll+0x%llX",
                    (unsigned long long)(hudPlace - base));
            else
                LOG("authored CHUD colour-state signature missing/ambiguous "
                    "or hook failed; reticle remains visible with held colour");

            const char* kHudElemSig = "48 89 5C 24 10 57 48 83 EC 50 48 8B 05 ?? ?? ?? ?? 41 8A F9 45 0F B7 C0";
            uintptr_t hudElem = sig::Find(base, size, kHudElemSig);
            bool uniqueHudElem = hudElem != 0;
            if (uniqueHudElem)
                uniqueHudElem = sig::Find(hudElem + 1, base + size - hudElem - 1, kHudElemSig) == 0;
            if (uniqueHudElem)
            {
                // clang-format off
                const unsigned char expectedClassGate[] = {
                    0x74, 0x17, 0xB8, 0x02, 0x00, 0x00, 0x00,
                    0x66, 0x41, 0x3B, 0x42, 0x04, 0x75, 0x0B,
                    0x8B, 0xCB, 0xE8
                    // clang-format on
                };
                unsigned char* classGate = reinterpret_cast<unsigned char*>(hudElem + 0x84);
                const bool alreadyOwned =
                    classGate == g_halo3OwnedClassGate && classGate[0] == 0x90 && classGate[1] == 0x90 &&
                    memcmp(classGate + 2, expectedClassGate + 2, sizeof(expectedClassGate) - 2) == 0;
                bool layoutMatches =
                    alreadyOwned || memcmp(classGate, expectedClassGate, sizeof(expectedClassGate)) == 0;
                if (reinterpret_cast<unsigned char*>(hudElem)[0x7D] != 0xE8)
                    layoutMatches = false;
                if (layoutMatches)
                {
                    const uintptr_t playbackTarget = sig::RipTarget(hudElem + 0x7E, hudElem + 0x82);
                    const uintptr_t visibleTarget = sig::RipTarget(hudElem + 0x95, hudElem + 0x99);
                    bool targetsInModule = playbackTarget >= base;
                    if (playbackTarget >= base + size)
                        targetsInModule = false;
                    if (visibleTarget < base)
                        targetsInModule = false;
                    if (visibleTarget >= base + size)
                        targetsInModule = false;
                    if (targetsInModule)
                    {
                        RememberInstalledGameHook(reinterpret_cast<void*>(visibleTarget));
                        RememberInstalledGameHook(reinterpret_cast<void*>(hudElem));
                        g_gameIsPlayback = reinterpret_cast<GameIsPlaybackFn>(playbackTarget);
                        const MH_STATUS hookCreateStatus =
                            MCCVR_CreateHookChecked(reinterpret_cast<void*>(visibleTarget),
                                reinterpret_cast<void*>(&GuardedHudCrosshairVisibleHook),
                                reinterpret_cast<void**>(&g_realHudCrosshairVisible));
                        bool hookReady = hookCreateStatus == MH_OK;
                        if (hookReady)
                            hookReady = MH_QueueEnableHook(reinterpret_cast<void*>(visibleTarget)) == MH_OK;
                        bool drawHookReady = false;
                        if (hookReady)
                        {
                            const MH_STATUS drawCreate = MCCVR_CreateHookChecked(reinterpret_cast<void*>(hudElem),
                                reinterpret_cast<void*>(&GuardedHudDrawWidgetHook),
                                reinterpret_cast<void**>(&g_realHudDrawWidget));
                            drawHookReady = drawCreate == MH_OK;
                            if (drawHookReady)
                                drawHookReady = MH_QueueEnableHook(reinterpret_cast<void*>(hudElem)) == MH_OK;
                            if (!drawHookReady && drawCreate == MH_OK)
                                MCCVR_RemoveHookChecked(reinterpret_cast<void*>(hudElem));
                        }
                        if (hookReady)
                        {
                            DWORD oldProtect = 0;
                            if (VirtualProtect(classGate, 2, PAGE_EXECUTE_READWRITE, &oldProtect))
                            {
                                classGate[0] = 0x90;
                                classGate[1] = 0x90;
                                g_halo3OwnedClassGate = classGate;
                                FlushInstructionCache(GetCurrentProcess(), classGate, 2);
                                DWORD ignored = 0;
                                VirtualProtect(classGate, 2, oldProtect, &ignored);
                                if (drawHookReady)
                                    LOG("authored CHUD crosshair redirect active "
                                        "at halo3.dll+0x%llX",
                                        (unsigned long long)(hudElem - base));
                                else
                                    LOG("CHUD class hider active but authored "
                                        "draw wrapper unavailable; using VR fallback");
                            }
                            else
                            {
                                if (drawHookReady)
                                {
                                    MH_DisableHook(reinterpret_cast<void*>(hudElem));
                                    MCCVR_RemoveHookChecked(reinterpret_cast<void*>(hudElem));
                                }
                                MH_DisableHook(reinterpret_cast<void*>(visibleTarget));
                                MCCVR_RemoveHookChecked(reinterpret_cast<void*>(visibleTarget));
                                LOG("CHUD class gate protection failed; "
                                    "game reticle stays visible");
                            }
                        }
                        else
                        {
                            if (hookCreateStatus == MH_OK)
                                MCCVR_RemoveHookChecked(reinterpret_cast<void*>(visibleTarget));
                            LOG("CHUD crosshair predicate hook failed; "
                                "game reticle stays visible");
                        }
                    }
                    else
                        LOG("CHUD class targets outside halo3.dll; "
                            "game reticle stays visible");
                }
                else
                    LOG("CHUD class-gate layout mismatch; "
                        "game reticle stays visible");
            }
            else
                LOG("HUD element signature missing/ambiguous; "
                    "game reticle stays visible");
        }

        // (0x2EEFC8 placement hook removed - measured: no coordinates there.)

        // DO NOT HOOK halo3+0x120DF8. Tried 2026-07-15: it crashes the game on
        // level load, on contact, even as a pure pass-through (proven - the
        // skip range was never armed, the unconditional probe log never
        // printed, and it still died). Surviving the menus proves nothing:
        // halo3.dll's model pipeline does not run there, so the first real call
        // IS the level load. The weapon-lag diagnosis in RE-notes stands; the
        // mechanism for acting on it must not be a detour on this function.

        // FP mesh re-anchor: patch the single root-fetch call inside the object
        // node recomposer (see FpRootShim). lea rdx,[rsp+20]; mov ecx,ebx;
        // call <root>; then the 0x1205AC multiply - unique on disk, verified.
        const char* kFpRootCallSig = "48 8D 54 24 20 8B CB E8 ?? ?? ?? ?? 4D 8B C4 48 8D 4C 24 20 49 8B D7 E8";
        uintptr_t callSite = sig::Find(base, size, kFpRootCallSig);
        if (callSite && !sig::Find(callSite + 1, base + size - callSite - 1, kFpRootCallSig))
        {
            const uintptr_t callInstr = callSite + 7; // the E8
            const uintptr_t relAt = callInstr + 1;    // 4-byte aligned disp32
            const int32_t origRel = *reinterpret_cast<const int32_t*>(relAt);
            const uintptr_t nativeTarget = callInstr + 5 + origRel;
            if (nativeTarget < base || nativeTarget - base >= size)
                LOG("Halo 3 call-site already redirected; retaining native callee instead of chaining a relay");
            else
            {
                g_realFpRoot = reinterpret_cast<FpRootFn>(callInstr + 5 + origRel);
                // 12-byte trampoline (mov rax, imm64; jmp rax) within rel32 range
                // of the call site, since our DLL may sit >2GB away.
                unsigned char* tramp = nullptr;
                for (uintptr_t probe = callInstr & ~0xFFFFull; probe > callInstr - 0x40000000ull && !tramp;
                    probe -= 0x100000)
                    tramp = static_cast<unsigned char*>(VirtualAlloc(
                        reinterpret_cast<void*>(probe), 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
                const intptr_t newRel =
                    tramp ? reinterpret_cast<intptr_t>(tramp) - static_cast<intptr_t>(callInstr + 5) : INT64_MAX;
                if (tramp && newRel >= INT32_MIN && newRel <= INT32_MAX && (relAt & 3) == 0)
                {
                    tramp[0] = 0x48;
                    tramp[1] = 0xB8; // mov rax, imm64
                    *reinterpret_cast<void**>(tramp + 2) = reinterpret_cast<void*>(&FpRootShim);
                    tramp[10] = 0xFF;
                    tramp[11] = 0xE0; // jmp rax
                    DWORD old;
                    if (VirtualProtect(reinterpret_cast<void*>(relAt), 4, PAGE_EXECUTE_READWRITE, &old))
                    {
                        InterlockedExchange(reinterpret_cast<volatile LONG*>(relAt), static_cast<LONG>(newRel));
                        VirtualProtect(reinterpret_cast<void*>(relAt), 4, old, &old);
                        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(callInstr), 5);
                        LOG("FP mesh root call-site patched at halo3.dll+0x%llX "
                            "(real getter halo3.dll+0x%llX; atomic disp32 swap)",
                            (unsigned long long)(callInstr - base),
                            (unsigned long long)(reinterpret_cast<uintptr_t>(g_realFpRoot) - base));
                    }
                    else
                        LOG("FP mesh call-site VirtualProtect failed; gun stays camera-glued");
                }
                else
                    LOG("FP mesh trampoline allocation failed (rel %lld); gun stays camera-glued", (long long)newRel);
            }
        }
        else
            LOG("VRIK: object-root call-site signature missing/ambiguous; object-root probe unavailable");

        // Second call-site patch: the camera pitch/turn rotation applied to
        // every FP bone but camera_control (the head-glue; see the emitted
        // rax-only shim below and the LTCG note at g_fpSkipBounds).
        const char* kSwayCallSig = "44 3B 8F A4 11 00 00 74 0C 49 8B D0 48 8D 4D C8 E8";
        uintptr_t swaySite = sig::Find(base, size, kSwayCallSig);
        if (!g_fpInterpolatorHooked.load() && swaySite &&
            !sig::Find(swaySite + 1, base + size - swaySite - 1, kSwayCallSig))
        {
            const uintptr_t callInstr = swaySite + 16; // the E8
            const uintptr_t relAt = callInstr + 1;     // 4-byte aligned disp32
            const int32_t origRel = *reinterpret_cast<const int32_t*>(relAt);
            const uintptr_t nativeTarget = callInstr + 5 + origRel;
            if (nativeTarget < base || nativeTarget - base >= size)
                LOG("Halo 3 call-site already redirected; retaining native callee instead of chaining a relay");
            else
            {
                g_realSwayApply = reinterpret_cast<SwayApplyFn>(callInstr + 5 + origRel);
                unsigned char* tramp = nullptr;
                for (uintptr_t probe = callInstr & ~0xFFFFull; probe > callInstr - 0x40000000ull && !tramp;
                    probe -= 0x100000)
                    tramp = static_cast<unsigned char*>(VirtualAlloc(
                        reinterpret_cast<void*>(probe), 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
                const intptr_t newRel =
                    tramp ? reinterpret_cast<intptr_t>(tramp) - static_cast<intptr_t>(callInstr + 5) : INT64_MAX;
                if (tramp && newRel >= INT32_MIN && newRel <= INT32_MAX && (relAt & 3) == 0)
                {
                    // Hand-assembled shim, clobbers ONLY rax (the caller keeps its
                    // loop state in r8/r9/r10 across this call - LTCG contract; a
                    // compiled C++ shim here IS the fatal-error bug). Layout:
                    //   [0x00] mov rax,[lo0]; cmp rdx,rax; jb +0x0F (-> lo1 test)
                    //   [0x0F] mov rax,[hi0]; cmp rdx,rax; jb +0x2A (-> ret)
                    //   [0x1E] mov rax,[lo1]; cmp rdx,rax; jb +0x0F (-> tail)
                    //   [0x2D] mov rax,[hi1]; cmp rdx,rax; jb +0x0C (-> ret)
                    //   [0x3C] mov rax, real; jmp rax
                    //   [0x48] ret
                    unsigned char shim[0x49];
                    int o = 0;
                    auto movRaxAbs = [&](volatile uintptr_t* a)
                    {
                        shim[o++] = 0x48;
                        shim[o++] = 0xA1; // mov rax, moffs64
                        const void* p = const_cast<uintptr_t*>(a);
                        memcpy(shim + o, &p, 8);
                        o += 8;
                    };
                    auto cmpJb = [&](unsigned char disp)
                    {
                        shim[o++] = 0x48;
                        shim[o++] = 0x39;
                        shim[o++] = 0xC2; // cmp rdx, rax
                        shim[o++] = 0x72;
                        shim[o++] = disp; // jb rel8
                    };
                    movRaxAbs(&g_fpSkipBounds[0]);
                    cmpJb(0x0F);
                    movRaxAbs(&g_fpSkipBounds[1]);
                    cmpJb(0x2A);
                    movRaxAbs(&g_fpSkipBounds[2]);
                    cmpJb(0x0F);
                    movRaxAbs(&g_fpSkipBounds[3]);
                    cmpJb(0x0C);
                    shim[o++] = 0x48;
                    shim[o++] = 0xB8; // mov rax, imm64
                    const void* real = reinterpret_cast<const void*>(g_realSwayApply);
                    memcpy(shim + o, &real, 8);
                    o += 8;
                    shim[o++] = 0xFF;
                    shim[o++] = 0xE0; // jmp rax
                    shim[o++] = 0xC3; // ret (skip path)
                    memcpy(tramp, shim, o);
                    DWORD old;
                    if (VirtualProtect(reinterpret_cast<void*>(relAt), 4, PAGE_EXECUTE_READWRITE, &old))
                    {
                        InterlockedExchange(reinterpret_cast<volatile LONG*>(relAt), static_cast<LONG>(newRel));
                        VirtualProtect(reinterpret_cast<void*>(relAt), 4, old, &old);
                        FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<void*>(callInstr), 5);
                        LOG("camera pitch/turn call-site patched at halo3.dll+0x%llX "
                            "(rotator halo3.dll+0x%llX)",
                            (unsigned long long)(callInstr - base),
                            (unsigned long long)(reinterpret_cast<uintptr_t>(g_realSwayApply) - base));
                    }
                    else
                        LOG("pitch/turn call-site VirtualProtect failed; gun stays head-rotated");
                }
                else
                    LOG("pitch/turn trampoline allocation failed; gun stays head-rotated");
            }
        }
        else if (!g_fpInterpolatorHooked.load())
            LOG("pitch/turn call-site signature missing/ambiguous; gun stays head-rotated");

        ResolveMotionBlurVars(base, size);
        ResolveCinematicFovVar(base, size);
        ResolveBodyVars(base, size);
        DumpHudDebugVars(base, size);

        // (CHUD visibility-snapshot hook removed 2026-07-19 evening - its forced
        // byte writes used a disproven offset map and suppressed the HUD. The
        // reticle kill uses Halo's class-gated path inside 0x2EDF24 only.)

        // Hook the engine's FP render driver and
        // resolve the guard global that gates its first in-window call site.
        // Driver prologue (0x2835D4 in 1.3528); guard cmp site (0x28599D).
        const char* kFpDriverSig = "48 8B C4 48 89 58 08 48 89 68 10 48 89 70 18 48 89 78 20 41 55 41 56 41 57 "
                                   "48 83 EC 20 48 8B D9 40 8A F2 8B 89 F4 27 00 00";
        const char* kFpDriverGuardSig = "44 39 2D ?? ?? ?? ?? 75 0A 33 D2 48 8B CF E8";
        uintptr_t driverHit = sig::Find(base, size, kFpDriverSig);
        uintptr_t guardHit = sig::Find(base, size, kFpDriverGuardSig);
        if (guardHit && !sig::Find(guardHit + 1, base + size - guardHit - 1, kFpDriverGuardSig))
        {
            const int32_t disp = *reinterpret_cast<const int32_t*>(guardHit + 3);
            g_fpDriverGuard = reinterpret_cast<int32_t*>(guardHit + 7 + disp);
            LOG("FP driver guard global at halo3.dll+0x%llX (value now %d)",
                (unsigned long long)(reinterpret_cast<uintptr_t>(g_fpDriverGuard) - base), *g_fpDriverGuard);
        }
        else
            LOG("FP driver guard site missing/ambiguous");
        if (driverHit && !sig::Find(driverHit + 1, base + size - driverHit - 1, kFpDriverSig))
        {
            if (QueueHalo3Hook(driverHit, reinterpret_cast<void*>(&GuardedFpDriverHook),
                    reinterpret_cast<void**>(&g_origFpDriver)))
                LOG("FP driver hooked at halo3.dll+0x%llX", (unsigned long long)(driverHit - base));
            else
                LOG("FP driver hook FAILED");
        }
        else
            LOG("FP driver signature missing/ambiguous");

        RememberInstalledGameHook(reinterpret_cast<void*>(driverHit));
        uintptr_t gunRef = sig::Find(base, size, kGunCamRefSig);
        if (gunRef && !sig::Find(gunRef + 1, base + size - gunRef - 1, kGunCamRefSig))
        {
            const int32_t disp = *reinterpret_cast<const int32_t*>(gunRef + 13);
            g_gunCamera = gunRef + 17 + disp;
            LOG("gun/overlay camera at halo3.dll+0x%llX (expected 0x%llX for build 1.3528)",
                (unsigned long long)(g_gunCamera.load() - base), (unsigned long long)kGunCamRva);
        }
        else
        {
            LOG("gun-camera signature missing/ambiguous; weapon/HUD will stay oversized in stereo");
        }

        if (InstallLegacyWorldCollision(g_halo3WorldCollision, GameTitle::Halo3, base, size, runtimeGeneration,
                reinterpret_cast<void*>(&Halo3CollisionResolveDetour), 0x001FFD18, nullptr, false, 0x001FD748,
                kHalo3CollisionVectorSignature))
            RememberInstalledGameHook(g_halo3WorldCollision.target);
        (void)InstallHalo3ContactMelee(base, size, runtimeGeneration);
        (void)InstallHalo3WeaponHaptics(base, size, runtimeGeneration);
        (void)InstallHalo3Avatar(base, size, runtimeGeneration);
        // The rejected early-helper-only detours remain inert. The new optional
        // path owns acquisition and downstream assist together, with its own
        // default-off setting and feature-only failure isolation.
        (void)InstallHalo3DualAim(base, size, runtimeGeneration);

        uintptr_t renderHit = sig::Find(base, size, kRenderViewSig);
        uintptr_t prepareHit = sig::Find(base, size, kPrepareViewSig);
        uintptr_t viewportHit = sig::Find(base, size, kBuildViewportSig);
        uintptr_t matricesHit = sig::Find(base, size, kBuildMatricesSig);
        if (!renderHit || sig::Find(renderHit + 1, base + size - renderHit - 1, kRenderViewSig))
        {
            LOG("render-frame signature missing or ambiguous; raw stereo unavailable");
            return true;
        }
        if (!prepareHit || !viewportHit || !matricesHit)
        {
            LOG("derived camera matrix signatures missing; raw stereo unavailable");
            return true;
        }
        g_prepareView = reinterpret_cast<PrepareViewFn>(prepareHit);
        g_buildViewport = reinterpret_cast<BuildViewportFn>(viewportHit);
        g_buildMatrices = reinterpret_cast<BuildMatricesFn>(matricesHit);
        if (!QueueHalo3Hook(renderHit, reinterpret_cast<void*>(&GuardedRenderViewHook),
                reinterpret_cast<void**>(&g_origRenderView)))
        {
            LOG("FAILED to hook render-frame entry");
            return true;
        }
        RememberInstalledGameHook(reinterpret_cast<void*>(renderHit));
        g_renderHooked = true;
        LOG("inner per-view double-render hook installed at halo3.dll+0x%llX", (unsigned long long)(renderHit - base));
        return true;
    }
} // namespace game_core
namespace
{
#include "halo3_physical_crouch.inl"
} // namespace
