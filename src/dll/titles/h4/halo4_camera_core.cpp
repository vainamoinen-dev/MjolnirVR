// halo4_camera_core.cpp: Halo 4's camera core and everything built on it (moved from game.cpp, unchanged):
// the camera loop hooks, stereo and first-person rig, VRIK and floating weapon, world collision and contact
// melee, muzzle and haptics, HUD/CUI/reticle and restoration, scope, pause reader, install and retirement.
// Core reaches it through halo4_bridge.h and it reaches core through game_internal.h; the Halo 4 .inl
// files are included at the same points and scopes as before.
#include "../../core/game_internal.h"
#include "halo4_bridge.h"
#include "../../core/guard_count.h"
using namespace game_core;

namespace
{
    // Inside the anonymous namespace, so the forwarders at the end can name the anonymous functions as ::Name.
    using namespace halo4_bridge;
} // namespace

extern "C"
{
    // Read by the two allocation-free MASM bridges. The worker installs and
    // clears them only while halo4.dll is loader-pinned.
    constinit void* g_halo4EffectOriginalHelper = nullptr;
    constinit void* g_halo4EffectTransientContinue = nullptr;
    constinit void* g_halo4CurvatureContinue = nullptr;
    constinit volatile DWORD g_halo4HudGameplayThreadId = 0;
    constinit volatile uint8_t g_halo4HudCurvatureEnabled = 0;
    constinit volatile float g_halo4HudCurvatureValue = 1.0f;
    constinit volatile uint8_t g_halo4EffectsEnabled = 0;
    constinit volatile LONG64 g_halo4EffectsHidden = 0;

    void Halo4EffectTransientWrapper();
    void Halo4CurvatureBridge();
    void __fastcall Halo4EffectHideBridge(void* descriptor, void* matrix);
}
#include "halo4_effect_hide_bridge.inl"
namespace
{
    static_assert(offsetof(BoneMatrix, translation) == kHalo4CuiTransformTranslationOffset);
} // namespace
namespace halo4_bridge
{
    bool DiagnosticHalo4WeaponOwner(uint32_t expectedGeneration, uint32_t candidate, uint32_t& unitOut,
        uint32_t& weaponOut, uint32_t& detailOut) noexcept;
} // namespace halo4_bridge
namespace
{
    // Production-safe H4 semantic-primary read: the shared body of the
    // diagnostic probe. `candidate` is UINT32_MAX for the semantic primary
    // slot; any other value must resolve to primary slot 0 through the
    // equipped-weapon mapping (the FP skinning path, where the observed record
    // is the candidate). `primaryAbsentOut` (optional) reports an explicit raw
    // empty primary slot from the same guarded read and is never set by a
    // validation/lifecycle failure.
    bool Halo4WeaponOwnerEvidence(uint32_t expectedGeneration, uint32_t candidate, uint32_t& unitOut,
        uint32_t& weaponOut, uint32_t& detailOut, bool* primaryAbsentOut = nullptr) noexcept;
} // namespace
namespace halo4_bridge
{
    // SEH-guarded tri-state observation of Halo 4's semantic primary owner;
    // a native fault is Unknown, never absence. Used by the production
    // producer and by Game_ReadPrimaryWeaponEvidence's Halo 4 branch.
    PrimaryWeaponEvidence Halo4ObservePrimaryWeapon(uint32_t generation, uint32_t candidate) noexcept;
} // namespace halo4_bridge
namespace
{
    // One safe-context support-grip invocation read: the tri-state semantic
    // primary evidence for `candidate` published to the durable input
    // (including an explicit Unknown) plus this invocation's relation to the
    // live relationship. Read-only with respect to the durable relationship.
    SupportInvocationResolution Halo4ReadSupportInvocation(uint32_t candidate) noexcept;
} // namespace
namespace halo4_bridge
{
    uint32_t Halo4RetainedRuntimeGeneration();
} // namespace halo4_bridge
namespace
{
    using Halo4SetupFn = void(__fastcall*)(
        uintptr_t view, uint32_t window, uint32_t count, uint32_t mode, uint32_t user, uintptr_t observer);
    using Halo4WrapperFn = void(__fastcall*)(uintptr_t element, uintptr_t view, uint32_t window);
    using Halo4FirstPersonProducerFn = uintptr_t(__fastcall*)(
        uint32_t userIndex, uint64_t unitObject, uint32_t maxRecords, void* outputRecords);
    using Halo4CuiRenderCommandFn = bool(__fastcall*)(
        void* renderer, const void* command, void* openRenderSections, void* renderContext);

    constinit Halo4SetupFn g_halo4OrigSetup = nullptr;
    constinit Halo4WrapperFn g_halo4OrigWrapper = nullptr;
} // namespace
namespace halo4_bridge
{
    constinit Halo4ModelSkinningFn g_halo4OrigModelSkinning = nullptr;
} // namespace halo4_bridge
namespace
{
    constinit Halo4FirstPersonProducerFn g_halo4OrigFirstPersonProducer = nullptr;
    constinit Halo4CuiRenderCommandFn g_halo4OrigCuiRenderCommand = nullptr;
} // namespace
namespace halo4_bridge
{
    constinit Halo4CuiGameplayRenderFn g_halo4OrigCuiGameplayRender = nullptr;
    constinit std::atomic<float> g_halo4RenderHalfFovX[2]{};
    constinit std::atomic<float> g_halo4RenderHalfFovY[2]{};
    constinit std::atomic<uint64_t> g_halo4RenderFovSerial[2]{};
} // namespace halo4_bridge
namespace
{
    // The native HUD consumers are the headset-proven ones. The rejected
    // tag-basis HUD writer remains dormant.
    // Stereo and ControllerInput come from the stereo camera core and the
    // look-pitch ownership below. ControllerAim, Haptics, RuntimeModes and
    // RoomScale join them now that Halo 4 publishes the three things the shared
    // paths need from a title: a runtime mode, a yaw reference pair, and the
    // engine's own aim direction. The H4EK-derived cinematic globals plus
    // title-native authored-shot look constraints also prove the shared theatre
    // contract. The HUD-basis writer is headset-rejected and remains
    // dormant. ArmIk stays out because the floating-hands path
    // deliberately uses rigid floating hands only, so advertising ArmIk here
    // would grant a capability Halo 4 does not implement.
    constexpr uint32_t kHalo4RuntimeCapabilities = TitleCapability_Stereo | TitleCapability_ControllerInput |
                                                   TitleCapability_ControllerAim | TitleCapability_Hud |
                                                   TitleCapability_Haptics | TitleCapability_RuntimeModes |
                                                   TitleCapability_RoomScale | TitleCapability_CutsceneTheater;

    // Log-only parity diagnostic. It is a compile-time switch rather than a
    // config key, so an existing configuration does not need to be edited
    // and packaging still leaves halomccvr.cfg untouched.
    constexpr bool kEnableHalo4ParityTrace = true;

} // namespace
namespace halo4_bridge
{
    constinit Halo4CameraCore g_halo4Camera;
} // namespace halo4_bridge
namespace
{

    using Halo4PauseReasonFn = bool(__fastcall*)(int32_t reason);
    using Halo4HudRootFn = void(__fastcall*)(void* renderer);

    constinit struct Halo4RestorationRuntime
    {
        std::atomic<bool> effectsInstalled{false};
        std::atomic<bool> hudInstalled{false};
        std::atomic<bool> pauseProven{false};
        bool cleanupRequired = false;
        bool effectsTouched = false;
        bool effectsNegPatched = false;
        bool effectsHelperPatched = false;
        bool effectsTransientPatched = false;
        bool effectsModeOnePatched = false;
        bool effectsSettingPendingLogged = false;
        bool curvatureTouched = false;
        bool curvaturePatched = false;
        void* hudTarget = nullptr;
        Halo4HudRootFn hudOriginal = nullptr;
        Halo4PauseReasonFn pauseReason = nullptr;
        bool pauseStateKnown = false;
        bool pauseLast = false;
        uint64_t pauseMismatchSinceMs = 0;
        bool pauseMismatchValue = false;
        std::atomic<bool> helmetControlInstalled{false};
        std::atomic<uint64_t> hudTransforms{0};
    } g_halo4Restoration;

    constexpr uint32_t kHalo4EffectNegRva = 0x1059A2;
    constexpr uint32_t kHalo4EffectHelperRva = 0x100EE8;
    constexpr uint32_t kHalo4EffectOriginalHelperRva = 0x100EBC;
    constexpr uint32_t kHalo4EffectTransientRva = 0x1012D5;
    constexpr uint32_t kHalo4EffectTransientContinueRva = 0x1012E7;
    constexpr uint32_t kHalo4EffectModeOneRva = 0x27BD36;
    constexpr uint32_t kHalo4EffectCaveRva = 0xB79C10;
    constexpr uint32_t kHalo4HudRootRva = 0x3F313C;
    constexpr uint32_t kHalo4CurvatureRva = 0x420D7E;
    constexpr uint32_t kHalo4CurvatureContinueRva = 0x420D98;
    constexpr uint32_t kHalo4PauseReasonRva = 0xA0AE4;

    constexpr std::array<uint8_t, 5> kHalo4EffectNegStock = {0x48, 0x8B, 0x5C, 0x24, 0x48};
    constexpr std::array<uint8_t, 5> kHalo4EffectHelperStock = {0xE8, 0xCB, 0x4A, 0x00, 0x00};
    // clang-format off
    constexpr std::array<uint8_t, 18> kHalo4EffectTransientStock = {
        0x66,0x41,0x39,0x44,0xB6,0x18,0x49,0x8B,0xD7,
        0x41,0x0F,0x95,0xC1,0xE8,0xD5,0xFB,0xFF,0xFF};
    // clang-format on
    constexpr std::array<uint8_t, 3> kHalo4EffectModeOneStock = {0x41, 0x8A, 0xDA};
    constexpr std::array<uint8_t, 3> kHalo4EffectModeOneHidden = {0x30, 0xDB, 0x90};
    // clang-format off
    constexpr std::array<uint8_t, 26> kHalo4CurvatureStock = {
        0xF3,0x0F,0x10,0x8F,0xE8,0x01,0x00,0x00,
        0xF3,0x0F,0x11,0x4D,0xA4,
        0xF3,0x0F,0x10,0x8F,0xEC,0x01,0x00,0x00,
        0xF3,0x0F,0x11,0x4D,0xAC};
    // clang-format on

    int Halo4SafeRead(const void* source, void* destination, size_t bytes);
    int Halo4SafeWrite(void* destination, const void* source, size_t bytes);

    bool Halo4FindUniquePinned(uintptr_t base, size_t size, const char* pattern, uint32_t rva, uintptr_t& result)
    {
        result = sig::Find(base, size, pattern);
        return result && result - base == rva && !sig::Find(result + 1, base + size - result - 1, pattern);
    }

    bool Halo4WriteExecutable(void* destination, const void* expected, const void* replacement, size_t bytes)
    {
        if (!destination || !expected || !replacement || !bytes)
            return false;
        std::vector<uint8_t> current(bytes);
        if (!Halo4SafeRead(destination, current.data(), bytes) || std::memcmp(current.data(), expected, bytes) != 0)
            return false;
        DWORD previous = 0;
        if (!VirtualProtect(destination, bytes, PAGE_EXECUTE_READWRITE, &previous))
            return false;
        const bool wrote = Halo4SafeWrite(destination, replacement, bytes) != 0;
        DWORD ignored = 0;
        const bool protectedAgain = VirtualProtect(destination, bytes, previous, &ignored) != 0;
        FlushInstructionCache(GetCurrentProcess(), destination, bytes);
        return wrote && protectedAgain && Halo4SafeRead(destination, current.data(), bytes) &&
               std::memcmp(current.data(), replacement, bytes) == 0;
    }

    template <size_t N>
    bool Halo4PatchMatches(uintptr_t address, const std::array<uint8_t, N>& bytes)
    {
        std::array<uint8_t, N> current{};
        return Halo4SafeRead(reinterpret_cast<const void*>(address), current.data(), N) && current == bytes;
    }

    template <size_t N>
    bool Halo4RestoreOwnedPatch(
        uintptr_t address, const std::array<uint8_t, N>& owned, const std::array<uint8_t, N>& stock)
    {
        std::array<uint8_t, N> current{};
        if (!Halo4SafeRead(reinterpret_cast<const void*>(address), current.data(), N))
            return false;
        if (current == stock)
            return true;
        if (current != owned)
            return false;
        return Halo4WriteExecutable(reinterpret_cast<void*>(address), owned.data(), stock.data(), N);
    }
} // namespace
namespace halo4_bridge
{
    uint32_t Halo4RetainedRuntimeGeneration()
    {
        // Cross-title pending ownership is keyed to the installed camera-core
        // generation, never raw DLL residency. This slot was accidentally
        // omitted when the Halo 4 camera core became permanent, leaving H4 as
        // the only supported VR title that the shared handoff manager could
        // not retain or retire by generation.
        return g_halo4Camera.generation.load(std::memory_order_acquire);
    }
} // namespace halo4_bridge
namespace
{
    // ~1 second at 120 Hz, two eyes per frame. Long enough that a few frames
    // of start-up discovery are never mistaken for a failure.
    constexpr uint32_t kHalo4UncapturedEyeLimit = 240;

    // 1% of headroom on the solved cover. Float rounding through
    // atan/tan and the engine's own projection build can leave the cover a hair
    // under the native frustum, and being one ulp short trips vr.cpp's
    // whole-slice fallback - the exact failure this headroom exists to prevent.
    // The cost is ~1% of raster area; the alternative is an intermittent,
    // frame-dependent geometry fault.
    constexpr float kHalo4CoverMargin = 1.01f;

    // The single predicate that decides who owns Halo 4's look pitch.
    // The render camera, the XInput hook and the closed loop all sample this
    // one function, so there is no state in which the view has taken pitch and
    // the stick has not, or the loop steers while the view still adds.
    //
    // F2 (g_enabled) is a real off switch: turning it off returns every part of
    // this to plain head-pose behaviour rather than leaving a half-owned camera.
    bool Halo4LookPitchOwned()
    {
        return TitleAdapter_GetActiveTitle() == GameTitle::Halo4 &&
               g_halo4Camera.armed.load(std::memory_order_acquire) &&
               !g_halo4Camera.sceneTargetMissing.load(std::memory_order_acquire) &&
               g_enabled.load(std::memory_order_acquire) && VR_IsStereoEnabled();
    }
} // namespace
namespace halo4_bridge
{
    // ---- first-person hands --------------------------------------------
    // Resolved once per install from the signature-verified anchor, never a
    // bare address.
    constinit uint32_t* g_halo4EngineTlsIndex = nullptr;
} // namespace halo4_bridge
namespace
{
    int Halo4SafeRead(const void* source, void* destination, size_t bytes);
    int Halo4SafeWrite(void* destination, const void* source, size_t bytes);
} // namespace
namespace halo4_bridge
{
    // Every dereference on this path is proven before it is taken. A crash dump
    // showed the engine itself indexing fp_weapons[user] with the block
    // pointer NULL - that crash IS this chain, so a missing link here must
    // return false, never fault. SEH covers the reads that a torn teardown
    // could invalidate between the check and the load.
    bool Halo4ResolveFirstPerson(int user, Halo4FirstPersonAccess& out)
    {
        out = Halo4FirstPersonAccess{};
        if (!g_halo4EngineTlsIndex || user < 0 || user >= static_cast<int>(kHalo4FirstPersonMaxUsers))
        {
            return false;
        }
        __try
        {
            const uint32_t slot = *g_halo4EngineTlsIndex;
            // The engine's own index must be a plausible TLS slot; a torn or
            // pre-init read here is exactly how a wild pointer gets built.
            if (slot >= 1088)
                return false;
            auto** slots = reinterpret_cast<unsigned char**>(__readgsqword(0x58));
            if (!slots)
                return false;
            unsigned char* tls = slots[slot];
            if (!tls)
                return false;
            auto* weapons = *reinterpret_cast<unsigned char**>(tls + kHalo4TlsFirstPersonWeaponsOffset);
            auto* orientations = *reinterpret_cast<unsigned char**>(tls + kHalo4TlsFirstPersonOrientationsOffset);
            if (!weapons || !orientations)
                return false; // the crash-dump state: blocks not present

            unsigned char* record = weapons + static_cast<size_t>(user) * kHalo4FirstPersonWeaponsUserStride;
            const uint32_t flags = *reinterpret_cast<uint32_t*>(record + kHalo4FirstPersonRecordFlagsOffset);
            // Retail gates the whole node path on this bit before it reads a
            // single matrix (0x3B53A3-0x3B53A9). Honour the same gate.
            if ((flags & kHalo4FirstPersonRecordActiveFlag) == 0)
                return false;
            const int32_t unit = *reinterpret_cast<int32_t*>(record + kHalo4FirstPersonRecordUnitOffset);
            if (unit == -1)
                return false;

            out.record = record;
            for (uint32_t weapon = 0; weapon < kHalo4FirstPersonMaxWeapons; ++weapon)
            {
                unsigned char* sub = record + static_cast<size_t>(weapon) * kHalo4FirstPersonWeaponStride;
                // Measured: a COUNT, not an index. A weapon with no composed
                // skeleton reports 0 and must be skipped rather than written.
                const int32_t nodes = *reinterpret_cast<int32_t*>(sub + kHalo4FirstPersonWeaponNodeCountOffset);
                if (nodes <= 0 || nodes > static_cast<int32_t>(kHalo4FirstPersonMaxNodes))
                {
                    continue;
                }
                const size_t index = weapon + static_cast<size_t>(user) * kHalo4FirstPersonMaxWeapons;
                out.nodes[weapon] =
                    orientations + index * kHalo4FirstPersonOrientationStride + kHalo4FirstPersonNodeArrayOffset;
                out.nodeCount[weapon] = nodes;
                ++out.weapons;
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("resolving the Halo 4 first-person state faulted");
            out = Halo4FirstPersonAccess{};
            return false;
        }
        return out.weapons > 0;
    }
} // namespace halo4_bridge
namespace
{
    // The final model-skinning consumer receives absolute
    // 0x34-byte matrices.  Work only on a private copy: unlike a write to the
    // animation producer, this cannot feed our pose back into Halo 4.
    // Sized to the record's fixed bank, which is what actually bounds the copy.
    // One record is processed per detour call, so a single scratch serves both
    // the arms solve and the weapon carry.
    constinit thread_local BoneMatrix g_halo4VrikScratch[kHalo4FirstPersonBankTransforms];
    constinit thread_local BoneMatrix g_halo4ScopeHidden[kHalo4FirstPersonBankTransforms];
    constinit thread_local bool g_halo4ScopeRendering = false, g_halo4ScopePixelsValid = true;
} // namespace
namespace halo4_bridge
{
    constinit std::atomic<uint32_t> g_halo4ScopeFaultGeneration{0}, g_halo4ScopeFaults{0};
} // namespace halo4_bridge
namespace
{
    // Roots published for the Halo 4 first-person path.
    //
    // SUPERSEDED by the producer disassembly below, and the old reasoning is left
    // here because it was wrong in an instructive way. This block used to argue
    // that Halo 4 roots its whole first-person assembly on the render camera,
    // that the record's own node 0 therefore IS that camera, and that anchoring
    // on node 0 would glue the rig to the game camera. Disassembly of the
    // producer 0x3B1B4C shows that is true of the two WEAPON fills only. The
    // BODY fill's root argument is NULL (or, on one gated branch, an
    // identity-basis pure translation), so the arms record never carried a
    // camera to be glued to, and node 0 is simply b_pedestal - the anchor the
    // record actually has.
    //
    // The rule the old text was reaching for still holds and is now enforced in
    // one place: exactly one pose per stereo pair, with the eye entering only
    // at the very end. Halo 3, ODST and Reach all do this, and Halo 4 does it
    // as a single per-eye translation applied to the finished rig.
    //
    constinit Halo4CenterRoot g_halo4CenterRoot;
    // The body/aim frame before Halo 4's HMD pose is composed onto the camera.
    // Controller ownership uses this frame, never the post-HMD camera frame.
    constinit Halo4CenterRoot g_halo4ControllerRoot;
} // namespace
namespace halo4_bridge
{
    // Exact active-eye render roots. The producer composes the two
    // WEAPON records through this camera, so the weapon carry uses it to
    // express a model-frame hand motion in the weapon's own world frame. The
    // body record does not carry it and must not have it removed.
    constinit Halo4CenterRoot g_halo4EyeRoot[2];
} // namespace halo4_bridge
namespace
{
    constinit thread_local Halo4VrRenderSnapshot g_halo4RigTracking{};
    constinit thread_local bool g_halo4RigTrackingActive = false;

    void Halo4PublishCenterRoot(const Halo4CameraBasis& center)
    {
        const float left[3] = {center.up[1] * center.forward[2] - center.up[2] * center.forward[1],
            center.up[2] * center.forward[0] - center.up[0] * center.forward[2],
            center.up[0] * center.forward[1] - center.up[1] * center.forward[0]};
        const float packed[12] = {center.position[0], center.position[1], center.position[2], center.forward[0],
            center.forward[1], center.forward[2], left[0], left[1], left[2], center.up[0], center.up[1], center.up[2]};
        for (float component : packed)
            if (!isfinite(component))
                return;
        g_halo4CenterRoot.sequence.fetch_add(1, std::memory_order_acq_rel);
        for (int i = 0; i < 12; ++i)
            g_halo4CenterRoot.value[i].store(packed[i], std::memory_order_relaxed);
        g_halo4CenterRoot.sequence.fetch_add(1, std::memory_order_release);
    }

    void Halo4PublishControllerRoot(const Halo4CameraBasis& center)
    {
        const float left[3] = {center.up[1] * center.forward[2] - center.up[2] * center.forward[1],
            center.up[2] * center.forward[0] - center.up[0] * center.forward[2],
            center.up[0] * center.forward[1] - center.up[1] * center.forward[0]};
        const float packed[12] = {center.position[0], center.position[1], center.position[2], center.forward[0],
            center.forward[1], center.forward[2], left[0], left[1], left[2], center.up[0], center.up[1], center.up[2]};
        for (float component : packed)
            if (!isfinite(component))
                return;
        g_halo4ControllerRoot.sequence.fetch_add(1, std::memory_order_acq_rel);
        for (int i = 0; i < 12; ++i)
            g_halo4ControllerRoot.value[i].store(packed[i], std::memory_order_relaxed);
        g_halo4ControllerRoot.sequence.fetch_add(1, std::memory_order_release);
    }

    void Halo4PublishEyeRoot(int eye, const Halo4CameraBasis& camera)
    {
        if (eye < 0 || eye > 1)
            return;
        const float left[3] = {camera.up[1] * camera.forward[2] - camera.up[2] * camera.forward[1],
            camera.up[2] * camera.forward[0] - camera.up[0] * camera.forward[2],
            camera.up[0] * camera.forward[1] - camera.up[1] * camera.forward[0]};
        const float packed[12] = {camera.position[0], camera.position[1], camera.position[2], camera.forward[0],
            camera.forward[1], camera.forward[2], left[0], left[1], left[2], camera.up[0], camera.up[1], camera.up[2]};
        for (float component : packed)
            if (!isfinite(component))
                return;
        Halo4CenterRoot& destination = g_halo4EyeRoot[eye];
        destination.sequence.fetch_add(1, std::memory_order_acq_rel);
        for (int i = 0; i < 12; ++i)
            destination.value[i].store(packed[i], std::memory_order_relaxed);
        destination.sequence.fetch_add(1, std::memory_order_release);
    }

    bool Halo4LoadControllerRoot(float scale, BoneMatrix& root)
    {
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const uint32_t before = g_halo4ControllerRoot.sequence.load(std::memory_order_acquire);
            if (!before || (before & 1))
                continue;
            float packed[12];
            for (int i = 0; i < 12; ++i)
                packed[i] = g_halo4ControllerRoot.value[i].load(std::memory_order_relaxed);
            const uint32_t after = g_halo4ControllerRoot.sequence.load(std::memory_order_acquire);
            if (before != after || (after & 1))
                continue;
            root = BoneMatrix{};
            root.scale = isfinite(scale) && fabsf(scale) > 0.001f ? scale : 1.0f;
            for (int i = 0; i < 3; ++i)
            {
                root.translation[i] = packed[i];
                root.rotation[i] = packed[3 + i];
                root.rotation[3 + i] = packed[6 + i];
                root.rotation[6 + i] = packed[9 + i];
            }
            return true;
        }
        return false;
    }

    bool Halo4LoadActiveEyeRoot(float scale, BoneMatrix& root)
    {
        const int eye = g_stereoEye.load(std::memory_order_acquire);
        if (eye < 0 || eye > 1)
            return false;
        const Halo4CenterRoot& source = g_halo4EyeRoot[eye];
        for (int attempt = 0; attempt < 4; ++attempt)
        {
            const uint32_t before = source.sequence.load(std::memory_order_acquire);
            if (!before || (before & 1))
                continue;
            float packed[12];
            for (int i = 0; i < 12; ++i)
                packed[i] = source.value[i].load(std::memory_order_relaxed);
            const uint32_t after = source.sequence.load(std::memory_order_acquire);
            if (before != after || (after & 1))
                continue;
            root = BoneMatrix{};
            root.scale = isfinite(scale) && fabsf(scale) > 0.001f ? scale : 1.0f;
            for (int i = 0; i < 3; ++i)
            {
                root.translation[i] = packed[i];
                root.rotation[i] = packed[3 + i];
                root.rotation[3 + i] = packed[6 + i];
                root.rotation[6 + i] = packed[9 + i];
            }
            return true;
        }
        return false;
    }

    // Dormant relation state from earlier, rejected hand models, retained for
    // reference. The active same-eye path below neither reads nor publishes it.
    // The weapon records precede the body record in the rejected model. Cache the
    // most recent successful BODY hand in root-local space, never a world
    // delta carrying the previous eye/head root. Each weapon uses its own eye
    // root and current controller pose to build the world delta it needs.
    constinit AtomicBoneMatrix g_halo4StockRightHandLocal;
    constinit std::atomic<uint64_t> g_halo4StockRightHandLocalSerial{0};
    constinit std::atomic<uint64_t> g_halo4StockRightHandLocalPublishedAt{0};
    // The rigid motion the solved right hand made, in the first-person
    // MODEL frame. The weapon records live in world space, so each
    // one conjugates this by its own eye camera rather than consuming a cached
    // world transform built from a different eye.
    constinit AtomicBoneMatrix g_halo4RightHandMotion;
    constinit std::atomic<uint64_t> g_halo4RightHandMotionSerial{0};
    constinit std::atomic<uint64_t> g_halo4RightHandMotionPublishedAt{0};
    // Three records reach the first-person site per eye, two eyes per frame.
    // Twelve is a couple of frames' worth, so a delta survives normal record
    // ordering but expires as soon as the arms stop solving.
    constexpr uint64_t kHalo4WeaponDeltaMaxRecordAge = 12;

    constinit std::atomic<bool> g_halo4RuntimeWeaponBoundsProven{false};
    constinit std::atomic<bool> g_halo4VehicleIdentityProven{false};
    constinit std::atomic<uint64_t> g_halo4RuntimeWeaponBoundsReads{0};
    constinit std::atomic<uint64_t> g_halo4RuntimeWeaponBoundsRefusals{0};
    constinit uintptr_t g_halo4RenderModelTagIndexPointerSlot = 0;
    constinit uintptr_t g_halo4RenderModelGroupBaseTable = 0;

    // The active path follows Halo 4's live per-eye producer order, not the rejected
    // previous-pair prediction. The exact 80-node Storm record builds both
    // hands and stages its actual world delta. The immediately following
    // flag-1 held model consumes that same-eye, same-source-frame delta once.
    struct Halo4FloatingRelation
    {
        bool valid = false;
        uint32_t epoch = 0;
        uint32_t generation = 0;
        uint64_t preparedSerial = 0;
        Halo4FloatingTransform rightHandDeltaWorld{};
        uintptr_t expectedHeldSource = 0;
        // Preserved only for the dormant previous-pair path. The active same-eye
        // transaction never reads or publishes a previous-pair relation.
        Halo4FloatingTransform stockRightHandLocal{};
    };
    struct Halo4FloatingPair
    {
        contact_melee::TrackingToWorld reloadTrackingTransform{};
        bool reloadTrackingValid = false;
        // XR-local tracking <-> Halo 4 world for this pair, the same
        // position mapping the visible gun is placed with. The barrel reticle ray
        // is projected with it; the contact frames' transform is body-relative.
        contact_melee::TrackingToWorld xrTrackingTransform{};
        bool xrTrackingValid = false;
        contact_melee::Frame contactFrames[2]{};
        // The raw tracking epoch of those frames (their referenceEpoch is
        // the hashed contact-space epoch); the muzzle/lens receipts are keyed by this one.
        uint64_t contactRawEpoch = 0;
        bool rightContactPublished = false;
        bool active = false;
        bool targetsValid = false;
        bool rightTargetValid = false;
        bool leftTargetValid = false;
        uint32_t epoch = 0;
        uint32_t generation = 0;
        uint64_t preparedSerial = 0;
        float worldScale = 0.0f;
        float gunYawDeg = 0.0f;
        float gunPitchDeg = 0.0f;
        float gunRollDeg = 0.0f;
        bool twoHandAimActive = false;
        // Persistent support grip: the visible rigid support lock follows the
        // frozen pair decision (relationship engaged AND this pair's owner
        // trusted), never the aim-authority flag. Frozen once per pair by the
        // first record that can prove Present/Absent (F04/A003); with the
        // feature off it is the pair-begin twoHandAimActive copy, so the base
        // presentation is byte-identical.
        bool supportGripAttached = false;
        bool supportGripResolved = false;
        uint32_t supportGripOwnerUnit = UINT32_MAX;
        uint32_t supportGripOwnerWeapon = UINT32_MAX;
        // Frozen from the same safe first-person owner proof as the hands
        // record. Used only to observe native flag-0 body identity; it never
        // changes the submitted stock body palette.
        bool localUnitFrozen = false;
        uint32_t localUnit = UINT32_MAX;
        // Owner-safe one-hand fallback carrier, built once per pair from the
        // same prepared sample. The weapon carrier consumes it when this
        // invocation is untrusted/detached and the prepared aim is
        // support-derived, so an untrusted invocation never rides the
        // wrong owner's two-hand solve.
        bool oneHandRightTargetValid = false;
        Halo4FloatingTransform oneHandRightTargetWorld{};
        bool handAlignment = false;
        Halo4ControllerWorldPoseInput bodyWorldFrame{};
        bool bodyWorldFrameValid = false;
        ControllerFingerInput bodySupportFingerInput{};
        bool bodyPairLeftHanded = false;
        Halo4FloatingTransform rightTargetWorld{};
        Halo4FloatingTransform leftTargetWorld{};
        // Collision publication is rebuilt from authored model-node extents.
        // Keep the uncorrected controller roots so last frame's correction is
        // not fed back as this frame's requested target.
        float collisionRoot[2][3]{};
        float rightSolvedWrist[3]{};
        bool rightSolvedWristValid = false;
        float handCollisionSamples[2][kHalo4WorldCollisionExtremaCount][3]{};
        uint32_t handCollisionSampleCount[2]{};
        int32_t collisionIgnoredObjectIndex = -1;
        Halo4FloatingRecordPhase eyePhase[2]{
            Halo4FloatingRecordPhase::AwaitStormHands, Halo4FloatingRecordPhase::AwaitStormHands};
        // Dormant frozen previous-pair state; deliberately never read
        // by the active same-eye transaction.
        Halo4FloatingRelation eyeRelation[2]{};
        // Current-eye Storm motion. It is cleared at eye entry, consumed by
        // the next held record, and never survives the pair.
        Halo4FloatingRelation nextEyeRelation[2]{};
    };
    // Retained dormant for reference. The active path has no previous-pair fallback.
    constinit thread_local Halo4FloatingRelation g_halo4FloatingLatest[2]{};
    constinit thread_local Halo4FloatingPair g_halo4FloatingPair{};
    constinit thread_local bool g_halo4MuzzleOwnedQuery = false;
    struct Halo4PhysicsRayCastInput
    {
        uint32_t profile = 0;
        uint32_t collisionFlags = 0;
        uint32_t additionalFlags = 0;
        float start[3]{};
        float end[3]{};
        int32_t ignoredObjects[64]{};
        uint8_t ignoredObjectCount = 0;
        uint8_t padding[3]{};
        // H4EK's post-cast environment conversion reads three optional
        // result-detail bytes at +0x128..+0x12A. This experiment leaves all
        // optional result conversions disabled.
        uint8_t resultOptions[4]{};
    };

    struct Halo4PhysicsRayCastResult
    {
        uint32_t type = 0;
        float fraction = 0.0f;
        float position[3]{};
        float normal[3]{};
        uint8_t unknown20[8]{};
        int32_t objectIndex = -1;
        uint8_t remaining[0x70 - 0x2C]{};
    };
    static_assert(sizeof(Halo4PhysicsRayCastInput) == 0x12C);
    static_assert(offsetof(Halo4PhysicsRayCastInput, start) == 0x0C);
    static_assert(offsetof(Halo4PhysicsRayCastInput, end) == 0x18);
    static_assert(offsetof(Halo4PhysicsRayCastInput, ignoredObjects) == 0x24);
    static_assert(offsetof(Halo4PhysicsRayCastInput, ignoredObjectCount) == 0x124);
    static_assert(offsetof(Halo4PhysicsRayCastInput, resultOptions) == 0x128);
    static_assert(sizeof(Halo4PhysicsRayCastResult) == 0x70);
    static_assert(offsetof(Halo4PhysicsRayCastResult, fraction) == 0x04);
    static_assert(offsetof(Halo4PhysicsRayCastResult, position) == 0x08);
    static_assert(offsetof(Halo4PhysicsRayCastResult, normal) == 0x14);
    static_assert(offsetof(Halo4PhysicsRayCastResult, objectIndex) == 0x28);

    using Halo4PhysicsRayCastFn = bool(__fastcall*)(Halo4PhysicsRayCastInput*, Halo4PhysicsRayCastResult*);
    using Halo4ObjectSetVelocitiesFn = void(__fastcall*)(int32_t, const float*, const float*);

    struct Halo4WorldCollisionTargetPublication
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint32_t> generation{0};
        std::atomic<uint64_t> publishedAtMs{0};
        std::atomic<uint32_t> sampleCount{0};
        std::atomic<uint32_t> handSampleCount{0};
        std::atomic<uint32_t> shapeId{0};
        std::atomic<int32_t> ignoredObjectIndex{-1};
        std::atomic<float> samples[kHalo4WorldCollisionMaxSamples][3]{};
    };

    struct Halo4WorldCollisionCorrectionPublication
    {
        std::atomic<uint32_t> sequence{0};
        std::atomic<uint32_t> generation{0};
        std::atomic<uint64_t> publishedAtMs{0};
        std::atomic<float> source[3]{};
        std::atomic<float> correction[3]{};
        std::atomic<bool> contact{false};
    };

    struct Halo4WorldCollisionWorkerHand
    {
        ContactReleaseSmoothing smoothing{};
        bool seeded = false;
        uint32_t generation = 0;
        uint64_t publishedAtMs = 0;
        uint32_t sampleCount = 0;
        uint32_t shapeId = 0;
        float accepted[kHalo4WorldCollisionMaxSamples][3]{};
        // Raw authored targets, separate from collision-corrected acceptance.
        // Swing speed must never treat a stationary push into a wall as motion.
        float previousDesired[kHalo4WorldCollisionMaxSamples][3]{};
    };

    struct Halo4WorldCollisionFeature
    {
        std::atomic<bool> installed{false};
        // Cold worker mirrors the shared config into this atomic so render /
        // raycast hot paths never race directly with the F1 menu writer.
        std::atomic<bool> configured{false};
        std::atomic<bool> physicalMeleeConfigured{false};
        std::atomic<float> physicalMeleeRequiredSpeed{5.0f};
        std::atomic<bool> configurationResetPending{true};
        std::atomic<bool> objectPushInstalled{false};
        Halo4PhysicsRayCastFn originalRayCast = nullptr;
        Halo4ObjectSetVelocitiesFn objectSetVelocities = nullptr;
        uint32_t generation = 0;
        Halo4WorldCollisionTargetPublication target[2]{};
        Halo4WorldCollisionCorrectionPublication correction[2]{};
        Halo4WorldCollisionWorkerHand worker[2]{};
        std::atomic<uint64_t> queries{0};
        std::atomic<uint64_t> handContacts[2]{};
        std::atomic<uint64_t> weaponContacts{0};
        std::atomic<uint64_t> weaponBoundsPublished{0};
        std::atomic<uint64_t> weaponBoundsFallbacks{0};
        std::atomic<uint64_t> applied[2]{};
        std::atomic<uint64_t> objectPushAttempts{0};
        std::atomic<uint64_t> objectPushes{0};
        std::atomic<uint64_t> objectPushFailures{0};
        std::atomic<uint64_t> physicalMeleeCandidates{0};
        std::atomic<uint64_t> physicalMeleePulses{0};
        std::atomic<uint64_t> physicalMeleeCooldowns{0};
        std::atomic<uint64_t> physicalMeleeVelocitySamples{0};
        std::atomic<uint32_t> physicalMeleePeakCentimetresPerSecond{0};
        std::atomic<uint64_t> physicalMeleePulseUntilMs{0};
        std::atomic<uint64_t> physicalMeleeCooldownUntilMs{0};
        std::atomic<bool> physicalMeleeVelocityLatched[2]{};
        std::atomic<uint64_t> resets{0};
        std::atomic<uint64_t> failures{0};
        std::atomic<uint64_t> calibrationQueries{0};
        std::atomic<uint64_t> calibrationHits{0};
        std::atomic<bool> environmentValidated{false};
        // This hooks PhysicsRayCast itself because testing proved the small
        // clear-line helper is dormant in retail campaign. The trampoline is
        // always completed first and our batch is leased/rate-limited.
        void* rayCastTarget = nullptr;
        std::atomic<uint64_t> engineContextCalls{0};
        std::atomic<uint64_t> dynamicContextCalls{0};
        std::atomic<uint64_t> fixedOnlyContextCalls{0};
        std::atomic<uint64_t> nextEngineQueryAtMs{0};
        std::atomic<bool> engineQueryActive{false};
        std::atomic<uint32_t> hotFailureReason{0};
    };

    constinit Halo4WorldCollisionFeature g_halo4WorldCollision;
    constexpr bool kEnableHalo4WorldCollisionStage4 = true;
    // The volumes use, instead of animated-node origins, the per-render-model
    // compression bounds authored in H4EK. Runtime checksum selects the exact
    // model; unknown identities fail open to the accepted right-hand volume.
    constexpr bool kEnableHalo4WeaponWorldCollisionStage6 = true;
    constexpr bool kEnableHalo4PhysicalMeleeStage9 = true;
    static_assert(kEnableHalo4WorldCollisionStage4);
    static_assert(kEnableHalo4WeaponWorldCollisionStage6);
    static_assert(kEnableHalo4PhysicalMeleeStage9);
    constexpr uint32_t kHalo4PhysicsRayCastRva = 0x1C1D4C;
    constexpr uint32_t kHalo4PhysicsOutputInitRva = 0x1C12A8;
    constexpr uint32_t kHalo4PhysicsFilterCtorRva = 0x1C0D94;
    constexpr uint32_t kHalo4PhysicsWorldRayRva = 0x2748AC;
    constexpr uint32_t kHalo4PhysicsCollectorResultRva = 0x1C15A8;
    constexpr uint32_t kHalo4WorldLineTestRva = 0x0F4218;
    constexpr uint32_t kHalo4WorldLineFilterGlobalRva = 0x2FFB0B8;
    constexpr uint32_t kHalo4ObjectSetVelocitiesRva = 0x5D1580;
    constexpr uint32_t kHalo4ObjectSetVelocitiesCoreRva = 0x5D14D8;
    constexpr uint32_t kHalo4ObjectVelocityMirrorRva = 0x0F1ADC;
    constexpr uint32_t kHalo4ObjectWakePhysicsRva = 0x5D73BC;
    constexpr uint32_t kHalo4ObjectMotionRefreshRva = 0x5D0F38;
    constexpr uint32_t kHalo4ObjectMotionCommitRva = 0x5DB6D0;
    // Exact mask pair copied by the official H4EK clear-line wrapper at
    // 0x17BF10 and its retail homolog at 0x0F4218. An earlier hand-built
    // structure/fixed-only mask was rejected in-headset after 1,794 clean
    // no-hit calls; it omitted these engine-owned filter categories.
    constexpr uint32_t kHalo4WorldLineCollisionFlags = 0x0002D009;
    constexpr uint32_t kHalo4WorldLineAdditionalFlags = 0x00005185;
    constexpr uint64_t kHalo4WorldLineFilterPair =
        (static_cast<uint64_t>(kHalo4WorldLineAdditionalFlags) << 32) | kHalo4WorldLineCollisionFlags;
    // Observed in multiple ordinary H4EK PhysicsRayCast callers. This first
    // dword selects the engine's raycast profiling bucket, not collision type.
    constexpr uint32_t kHalo4CollisionProfile = 0x1A;
    constexpr uint32_t kHalo4CollisionFixedObjectsOnly = 1u << 27;
    constexpr uint64_t kHalo4CollisionPublicationMaxAgeMs = 150;
    constexpr float kHalo4CollisionHapticAmplitude = feedback::Strength(feedback::Cue::WorldContact);
    constexpr uint64_t kHalo4CollisionEngineQueryIntervalMs = 33;
    constexpr uint64_t kHalo4PhysicalMeleeCooldownMs = 250;
} // namespace
namespace halo4_bridge
{
    // Gesture melee cooldown: the configured one, never under the native 250 ms.
    uint64_t Halo4PhysicalMeleeCooldownMs() noexcept
    {
        return std::max<uint64_t>(kHalo4PhysicalMeleeCooldownMs,
            static_cast<uint64_t>(ClampPhysicalMeleeCooldown(g_config.physical_melee_cooldown_s) * 1000.0f + 0.5f));
    }
} // namespace halo4_bridge
namespace
{
    constexpr uint32_t kHalo4CollisionFailureRayException = 1;
    constexpr uint32_t kHalo4CollisionFailureRayContract = 2;
    constexpr uint32_t kHalo4CollisionFailureObjectPushException = 3;

#include "halo4_contact_melee_runtime.inl"
#include "halo4_vehicle_input.inl"
    void Halo4PublishCollisionTarget(int hand, uint32_t generation, const float samples[][3], uint32_t sampleCount,
        uint32_t handSampleCount, int32_t ignoredObjectIndex, uint32_t shapeId = 0)
    {
        if (hand < 0 || hand > 1 || !generation || !samples || sampleCount == 0 ||
            sampleCount > kHalo4WorldCollisionMaxSamples || handSampleCount == 0 || handSampleCount > sampleCount)
            return;
        for (uint32_t sample = 0; sample < sampleCount; ++sample)
            if (!Halo4WorldCollisionFiniteVector(samples[sample]))
                return;
        if (generation == g_halo4Contact.generation)
            Halo4PublishContactHand(hand, samples, sampleCount, ignoredObjectIndex,
                (uint64_t(shapeId) << 32) ^ sampleCount ^ (uint64_t(handSampleCount) << 16));
        if (!g_halo4WorldCollision.configured.load(std::memory_order_acquire))
            return;
        auto& publication = g_halo4WorldCollision.target[hand];
        const uint32_t writing = publication.sequence.fetch_add(1, std::memory_order_acq_rel) + 1;
        publication.generation.store(generation, std::memory_order_relaxed);
        publication.publishedAtMs.store(GetTickCount64(), std::memory_order_relaxed);
        publication.sampleCount.store(sampleCount, std::memory_order_relaxed);
        publication.handSampleCount.store(handSampleCount, std::memory_order_relaxed);
        publication.shapeId.store(shapeId, std::memory_order_relaxed);
        publication.ignoredObjectIndex.store(ignoredObjectIndex, std::memory_order_relaxed);
        for (uint32_t sample = 0; sample < sampleCount; ++sample)
            for (int axis = 0; axis < 3; ++axis)
                publication.samples[sample][axis].store(samples[sample][axis], std::memory_order_relaxed);
        publication.sequence.store(writing + 1, std::memory_order_release);
    }

    bool Halo4ReadCollisionTarget(int hand, uint32_t& generation, uint64_t& publishedAtMs, float samples[][3],
        uint32_t& sampleCount, uint32_t& handSampleCount, int32_t& ignoredObjectIndex, uint32_t& shapeId)
    {
        if (hand < 0 || hand > 1 || !samples)
            return false;
        const auto& publication = g_halo4WorldCollision.target[hand];
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const uint32_t before = publication.sequence.load(std::memory_order_acquire);
            if (before & 1u)
                continue;
            generation = publication.generation.load(std::memory_order_relaxed);
            publishedAtMs = publication.publishedAtMs.load(std::memory_order_relaxed);
            sampleCount = publication.sampleCount.load(std::memory_order_relaxed);
            handSampleCount = publication.handSampleCount.load(std::memory_order_relaxed);
            shapeId = publication.shapeId.load(std::memory_order_relaxed);
            ignoredObjectIndex = publication.ignoredObjectIndex.load(std::memory_order_relaxed);
            if (sampleCount == 0 || sampleCount > kHalo4WorldCollisionMaxSamples || handSampleCount == 0 ||
                handSampleCount > sampleCount)
                continue;
            for (uint32_t sample = 0; sample < sampleCount; ++sample)
                for (int axis = 0; axis < 3; ++axis)
                    samples[sample][axis] = publication.samples[sample][axis].load(std::memory_order_relaxed);
            const uint32_t after = publication.sequence.load(std::memory_order_acquire);
            if (before == after && !(after & 1u))
            {
                for (uint32_t sample = 0; sample < sampleCount; ++sample)
                    if (!Halo4WorldCollisionFiniteVector(samples[sample]))
                        return false;
                return true;
            }
        }
        return false;
    }

    void Halo4PublishCollisionCorrection(
        int hand, uint32_t generation, const float source[3], const float correction[3], bool contact)
    {
        if (hand < 0 || hand > 1 || !Halo4WorldCollisionFiniteVector(source) ||
            !Halo4WorldCollisionFiniteVector(correction))
            return;
        auto& publication = g_halo4WorldCollision.correction[hand];
        const uint32_t writing = publication.sequence.fetch_add(1, std::memory_order_acq_rel) + 1;
        publication.generation.store(generation, std::memory_order_relaxed);
        publication.publishedAtMs.store(GetTickCount64(), std::memory_order_relaxed);
        for (int axis = 0; axis < 3; ++axis)
        {
            publication.source[axis].store(source[axis], std::memory_order_relaxed);
            publication.correction[axis].store(correction[axis], std::memory_order_relaxed);
        }
        publication.contact.store(contact, std::memory_order_relaxed);
        publication.sequence.store(writing + 1, std::memory_order_release);
    }

    bool Halo4ReadCollisionCorrection(
        int hand, uint32_t& generation, uint64_t& publishedAtMs, float source[3], float correction[3], bool& contact)
    {
        if (hand < 0 || hand > 1 || !source || !correction)
            return false;
        const auto& publication = g_halo4WorldCollision.correction[hand];
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const uint32_t before = publication.sequence.load(std::memory_order_acquire);
            if (before & 1u)
                continue;
            generation = publication.generation.load(std::memory_order_relaxed);
            publishedAtMs = publication.publishedAtMs.load(std::memory_order_relaxed);
            for (int axis = 0; axis < 3; ++axis)
            {
                source[axis] = publication.source[axis].load(std::memory_order_relaxed);
                correction[axis] = publication.correction[axis].load(std::memory_order_relaxed);
            }
            contact = publication.contact.load(std::memory_order_relaxed);
            const uint32_t after = publication.sequence.load(std::memory_order_acquire);
            if (before == after && !(after & 1u))
                return Halo4WorldCollisionFiniteVector(source) && Halo4WorldCollisionFiniteVector(correction);
        }
        return false;
    }

    void Halo4ClearCollisionCorrection(int hand, uint32_t generation)
    {
        const float zero[3]{};
        Halo4PublishCollisionCorrection(hand, generation, zero, zero, false);
    }

    void Halo4ResetWorldCollisionState(uint32_t generation)
    {
        for (int hand = 0; hand < 2; ++hand)
        {
            g_halo4WorldCollision.worker[hand] = Halo4WorldCollisionWorkerHand{};
            // The render thread is the sole target-publication writer. Do not
            // reset that sequence from the worker during teardown; generation,
            // age, and the mandatory first-sample seed make any retained value
            // inert without introducing a second seqlock writer.
            Halo4ClearCollisionCorrection(hand, generation);
        }
        g_halo4WorldCollision.queries.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.handContacts[0].store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.handContacts[1].store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.weaponContacts.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.weaponBoundsPublished.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.weaponBoundsFallbacks.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.applied[0].store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.applied[1].store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.objectPushAttempts.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.objectPushes.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.objectPushFailures.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.physicalMeleeCandidates.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.physicalMeleePulses.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.physicalMeleeCooldowns.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.physicalMeleeVelocitySamples.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.physicalMeleePeakCentimetresPerSecond.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.physicalMeleePulseUntilMs.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.physicalMeleeCooldownUntilMs.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.physicalMeleeVelocityLatched[0].store(false, std::memory_order_relaxed);
        g_halo4WorldCollision.physicalMeleeVelocityLatched[1].store(false, std::memory_order_relaxed);
        g_halo4WorldCollision.resets.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.failures.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.calibrationQueries.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.calibrationHits.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.environmentValidated.store(false, std::memory_order_relaxed);
        g_halo4WorldCollision.engineContextCalls.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.dynamicContextCalls.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.fixedOnlyContextCalls.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.nextEngineQueryAtMs.store(0, std::memory_order_relaxed);
        g_halo4WorldCollision.engineQueryActive.store(false, std::memory_order_relaxed);
        g_halo4WorldCollision.hotFailureReason.store(0, std::memory_order_relaxed);
    }

    void RemoveHalo4WorldCollision()
    {
        g_halo4WorldCollision.configured.store(false, std::memory_order_release);
        g_halo4WorldCollision.physicalMeleeConfigured.store(false, std::memory_order_release);
        g_halo4WorldCollision.physicalMeleePulseUntilMs.store(0, std::memory_order_release);
        g_halo4WorldCollision.physicalMeleeCooldownUntilMs.store(0, std::memory_order_release);
        g_halo4WorldCollision.physicalMeleeVelocityLatched[0].store(false, std::memory_order_release);
        g_halo4WorldCollision.physicalMeleeVelocityLatched[1].store(false, std::memory_order_release);
        g_halo4WorldCollision.configurationResetPending.store(true, std::memory_order_release);
        g_halo4WorldCollision.installed.store(false, std::memory_order_release);
        g_halo4WorldCollision.objectPushInstalled.store(false, std::memory_order_release);
        const uint32_t generation = g_halo4WorldCollision.generation;
        // Hook disable/removal and non-atomic worker-state reset happen in the
        // parent camera teardown after activeCallbacks proves quiescence.
        Halo4ClearCollisionCorrection(0, generation);
        Halo4ClearCollisionCorrection(1, generation);
    }

    uintptr_t Halo4DecodeRel32Call(uintptr_t instruction)
    {
        uint8_t opcode = 0;
        int32_t displacement = 0;
        if (!instruction || !Halo4SafeRead(reinterpret_cast<const void*>(instruction), &opcode, sizeof(opcode)) ||
            opcode != 0xE8 ||
            !Halo4SafeRead(reinterpret_cast<const void*>(instruction + 1), &displacement, sizeof(displacement)))
            return 0;
        return instruction + 5 + static_cast<intptr_t>(displacement);
    }

    uintptr_t Halo4DecodeRipRelativeAddress(uintptr_t instruction, size_t displacementOffset, size_t instructionLength)
    {
        int32_t displacement = 0;
        if (!instruction || displacementOffset + sizeof(displacement) > instructionLength ||
            !Halo4SafeRead(
                reinterpret_cast<const void*>(instruction + displacementOffset), &displacement, sizeof(displacement)))
            return 0;
        return instruction + instructionLength + static_cast<intptr_t>(displacement);
    }

    void Halo4RejectWorldCollisionFromEngineContext(uint32_t reason)
    {
        if (reason == kHalo4CollisionFailureObjectPushException)
        {
            g_halo4WorldCollision.objectPushFailures.fetch_add(1, std::memory_order_relaxed);
            g_halo4WorldCollision.objectPushInstalled.store(false, std::memory_order_release);
        }
        else
        {
            g_halo4WorldCollision.failures.fetch_add(1, std::memory_order_relaxed);
            g_halo4WorldCollision.installed.store(false, std::memory_order_release);
            const uint32_t generation = g_halo4WorldCollision.generation;
            Halo4ClearCollisionCorrection(0, generation);
            Halo4ClearCollisionCorrection(1, generation);
        }
        uint32_t empty = 0;
        g_halo4WorldCollision.hotFailureReason.compare_exchange_strong(
            empty, reason, std::memory_order_acq_rel, std::memory_order_relaxed);
    }

    bool Halo4RunWorldCollisionRayFromEngineContext(
        Halo4PhysicsRayCastInput& input, Halo4PhysicsRayCastResult& output, bool& hit)
    {
        hit = false;
        if (!g_halo4WorldCollision.originalRayCast || !Halo4WorldCollisionFiniteVector(input.start) ||
            !Halo4WorldCollisionFiniteVector(input.end))
        {
            Halo4RejectWorldCollisionFromEngineContext(kHalo4CollisionFailureRayException);
            return false;
        }
        bool completed = false;
        __try
        {
            hit = g_halo4WorldCollision.originalRayCast(&input, &output);
            completed = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("Halo 4's collision ray cast faulted");
            completed = false;
        }
        g_halo4WorldCollision.queries.fetch_add(1, std::memory_order_relaxed);
        if (!completed)
        {
            Halo4RejectWorldCollisionFromEngineContext(kHalo4CollisionFailureRayException);
            return false;
        }
        if ((hit && (!std::isfinite(output.fraction) || output.fraction < 0.0f || output.fraction >= 1.0f)) ||
            (!hit && output.fraction != 1.0f))
        {
            Halo4RejectWorldCollisionFromEngineContext(kHalo4CollisionFailureRayContract);
            return false;
        }
        return true;
    }

    void Halo4TryPushWorldCollisionObject(int32_t objectIndex, int32_t ignoredObjectIndex, const float previous[3],
        const float desired[3], uint64_t elapsedMs, float worldScale)
    {
        if (objectIndex == -1 || objectIndex == ignoredObjectIndex ||
            !g_halo4WorldCollision.objectPushInstalled.load(std::memory_order_acquire) ||
            !g_halo4WorldCollision.objectSetVelocities)
            return;
        float velocity[3]{};
        if (!Halo4BuildWorldCollisionPushVelocity(previous, desired, elapsedMs, worldScale, velocity))
            return;
        g_halo4WorldCollision.objectPushAttempts.fetch_add(1, std::memory_order_relaxed);
        bool completed = false;
        __try
        {
            // H4EK object_set_velocity resolves to this exact native helper.
            // A null angular vector preserves angular velocity while the
            // helper applies the finite linear velocity and wakes the object.
            g_halo4WorldCollision.objectSetVelocities(objectIndex, velocity, nullptr);
            completed = true;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("pushing a Halo 4 collision object faulted");
            completed = false;
        }
        if (completed)
            g_halo4WorldCollision.objectPushes.fetch_add(1, std::memory_order_relaxed);
        else
            Halo4RejectWorldCollisionFromEngineContext(kHalo4CollisionFailureObjectPushException);
    }

    void Halo4WorldCollisionEngineContextTick()
    {
        if (!g_halo4WorldCollision.installed.load(std::memory_order_acquire) ||
            !g_halo4WorldCollision.configured.load(std::memory_order_acquire) ||
            !g_halo4Camera.armed.load(std::memory_order_acquire) ||
            g_halo4Camera.teardownRequested.load(std::memory_order_acquire))
            return;

        bool idle = false;
        if (!g_halo4WorldCollision.engineQueryActive.compare_exchange_strong(
                idle, true, std::memory_order_acq_rel, std::memory_order_relaxed))
            return;
        struct EngineQueryLease
        {
            std::atomic<bool>& active;
            ~EngineQueryLease() { active.store(false, std::memory_order_release); }
        } lease{g_halo4WorldCollision.engineQueryActive};

        if (g_halo4WorldCollision.configurationResetPending.exchange(false, std::memory_order_acq_rel))
        {
            for (int hand = 0; hand < 2; ++hand)
            {
                g_halo4WorldCollision.worker[hand] = Halo4WorldCollisionWorkerHand{};
                Halo4ClearCollisionCorrection(hand, g_halo4WorldCollision.generation);
            }
        }

        const uint64_t now = GetTickCount64();
        uint64_t due = g_halo4WorldCollision.nextEngineQueryAtMs.load(std::memory_order_relaxed);
        if (now < due ||
            !g_halo4WorldCollision.nextEngineQueryAtMs.compare_exchange_strong(
                due, now + kHalo4CollisionEngineQueryIntervalMs, std::memory_order_acq_rel, std::memory_order_relaxed))
            return;

        const uint32_t generation = g_halo4Camera.generation.load(std::memory_order_acquire);
        if (!generation || generation != g_halo4WorldCollision.generation)
            return;

        const float worldScale = g_worldScale.load(std::memory_order_acquire);
        if (!std::isfinite(worldScale) || worldScale <= 0.0f)
            return;

        for (int hand = 0; hand < 2; ++hand)
        {
            uint32_t targetGeneration = 0;
            uint64_t targetAtMs = 0;
            uint32_t sampleCount = 0;
            uint32_t handSampleCount = 0;
            uint32_t shapeId = 0;
            int32_t ignoredObjectIndex = -1;
            float desired[kHalo4WorldCollisionMaxSamples][3]{};
            auto& state = g_halo4WorldCollision.worker[hand];
            if (!Halo4ReadCollisionTarget(hand, targetGeneration, targetAtMs, desired, sampleCount, handSampleCount,
                    ignoredObjectIndex, shapeId) ||
                targetGeneration != generation || targetAtMs > now ||
                now - targetAtMs > kHalo4CollisionPublicationMaxAgeMs)
            {
                state.seeded = false;
                Halo4ClearCollisionCorrection(hand, generation);
                continue;
            }
            if (!state.seeded || state.generation != generation || state.sampleCount != sampleCount ||
                state.shapeId != shapeId ||
                Halo4WorldCollisionMovementIsTeleport(state.accepted[0], desired[0], worldScale))
            {
                state.smoothing.Reset();
                state.seeded = true;
                state.generation = generation;
                state.publishedAtMs = targetAtMs;
                state.sampleCount = sampleCount;
                state.shapeId = shapeId;
                memcpy(state.accepted, desired, sizeof(float) * sampleCount * 3);
                memcpy(state.previousDesired, desired, sizeof(float) * sampleCount * 3);
                Halo4ClearCollisionCorrection(hand, generation);
                g_halo4WorldCollision.resets.fetch_add(1, std::memory_order_relaxed);
                continue;
            }

            bool contact = false;
            uint32_t contactSample = 0;
            int32_t contactObject = -1;
            float strongestCorrection[3]{};
            float strongestCorrectionSquared = 0.0f;
            float fastestMeleeContactSpeed = -1.0f;
            for (uint32_t sample = 0; sample < sampleCount; ++sample)
            {
                const float movement = Halo4WorldCollisionDistanceSquared(state.accepted[sample], desired[sample]);
                if (movement < 1.0e-10f)
                    continue;
                Halo4PhysicsRayCastInput input{};
                Halo4PhysicsRayCastResult output{};
                input.profile = kHalo4CollisionProfile;
                input.collisionFlags = kHalo4WorldLineCollisionFlags;
                input.additionalFlags = kHalo4WorldLineAdditionalFlags;
                memcpy(input.start, state.accepted[sample], sizeof(input.start));
                memcpy(input.end, desired[sample], sizeof(input.end));
                if (ignoredObjectIndex != -1)
                {
                    input.ignoredObjects[0] = ignoredObjectIndex;
                    input.ignoredObjectCount = 1;
                }
                bool hit = false;
                if (!Halo4RunWorldCollisionRayFromEngineContext(input, output, hit))
                    return;
                if (!hit)
                    continue;
                g_halo4WorldCollision.environmentValidated.store(true, std::memory_order_release);
                const Halo4WorldCollisionResolution resolution = Halo4ResolveWorldCollision(
                    state.accepted[sample], desired[sample], true, output.fraction, worldScale);
                if (!resolution.valid)
                {
                    Halo4RejectWorldCollisionFromEngineContext(kHalo4CollisionFailureRayContract);
                    return;
                }
                [[maybe_unused]] const float swingSpeed = // audit finding: computed, never compared
                    Halo4PhysicalMeleeSwingSpeedMetresPerSecond(state.previousDesired[sample], desired[sample],
                        targetAtMs > state.publishedAtMs ? targetAtMs - state.publishedAtMs : 0, worldScale);
                [[maybe_unused]] const float requiredSpeed = // audit finding: loaded, never compared
                    g_halo4WorldCollision.physicalMeleeRequiredSpeed.load(std::memory_order_relaxed);
                float correctionSquared = 0.0f;
                for (float value : resolution.correction)
                    correctionSquared += value * value;
                if (!contact || correctionSquared > strongestCorrectionSquared)
                {
                    contact = true;
                    contactSample = sample;
                    contactObject = output.objectIndex;
                    strongestCorrectionSquared = correctionSquared;
                    memcpy(strongestCorrection, resolution.correction, sizeof(strongestCorrection));
                }
            }

            const uint64_t elapsedMs = targetAtMs > state.publishedAtMs ? targetAtMs - state.publishedAtMs : 0;
            if (contact)
                Halo4TryPushWorldCollisionObject(contactObject, ignoredObjectIndex, state.accepted[contactSample],
                    desired[contactSample], elapsedMs, worldScale);
            if (fastestMeleeContactSpeed >= 0.0f)
            {
                g_halo4WorldCollision.physicalMeleeCandidates.fetch_add(1, std::memory_order_relaxed);
                const uint32_t speedCmPerSecond =
                    static_cast<uint32_t>(std::fmin(fastestMeleeContactSpeed * 100.0f, 100000.0f));
                uint32_t priorPeak =
                    g_halo4WorldCollision.physicalMeleePeakCentimetresPerSecond.load(std::memory_order_relaxed);
                while (priorPeak < speedCmPerSecond &&
                       !g_halo4WorldCollision.physicalMeleePeakCentimetresPerSecond.compare_exchange_weak(
                           priorPeak, speedCmPerSecond, std::memory_order_relaxed, std::memory_order_relaxed))
                {
                }
                const uint64_t cooldownUntil =
                    g_halo4WorldCollision.physicalMeleeCooldownUntilMs.load(std::memory_order_relaxed);
                if (now >= cooldownUntil)
                {
                    g_halo4WorldCollision.physicalMeleePulseUntilMs.store(
                        now + kHalo4PhysicalMeleePulseMs, std::memory_order_release);
                    g_halo4WorldCollision.physicalMeleeCooldownUntilMs.store(
                        now + Halo4PhysicalMeleeCooldownMs(), std::memory_order_release);
                    g_halo4WorldCollision.physicalMeleePulses.fetch_add(1, std::memory_order_relaxed);
                }
                else
                {
                    g_halo4WorldCollision.physicalMeleeCooldowns.fetch_add(1, std::memory_order_relaxed);
                }
            }
            for (uint32_t sample = 0; sample < sampleCount; ++sample)
            {
                for (int axis = 0; axis < 3; ++axis)
                {
                    state.accepted[sample][axis] = desired[sample][axis] + (contact ? strongestCorrection[axis] : 0.0f);
                    state.previousDesired[sample][axis] = desired[sample][axis];
                }
            }
            state.publishedAtMs = targetAtMs;
            const bool visibleCorrection = state.smoothing.Apply(now, contact, worldScale, strongestCorrection);
            Halo4PublishCollisionCorrection(hand, generation, desired[0], strongestCorrection, visibleCorrection);
            if (contact)
            {
                if (contactSample < handSampleCount)
                    g_halo4WorldCollision.handContacts[hand].fetch_add(1, std::memory_order_relaxed);
                else
                    g_halo4WorldCollision.weaponContacts.fetch_add(1, std::memory_order_relaxed);
                VR_PulseContactHaptics(hand == 0, kHalo4CollisionHapticAmplitude);
            }
        }
    }

    bool Halo4PhysicsRayCastDetourBody(Halo4PhysicsRayCastInput* input, Halo4PhysicsRayCastResult* output)
    {
        Halo4PhysicsRayCastFn original = g_halo4WorldCollision.originalRayCast;
        if (!original)
            return false;
        if (g_halo4MuzzleOwnedQuery)
            return original(input, output);
        uint32_t originalFlags = kHalo4CollisionFixedObjectsOnly;
        __try
        {
            if (input)
                originalFlags = input->collisionFlags;
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the Halo 4 ray-cast flags faulted");
            originalFlags = kHalo4CollisionFixedObjectsOnly;
        }
        const bool result = original(input, output);
        g_halo4WorldCollision.engineContextCalls.fetch_add(1, std::memory_order_relaxed);
        if ((originalFlags & kHalo4CollisionFixedObjectsOnly) != 0)
            g_halo4WorldCollision.fixedOnlyContextCalls.fetch_add(1, std::memory_order_relaxed);
        else
        {
            g_halo4WorldCollision.dynamicContextCalls.fetch_add(1, std::memory_order_relaxed);
            Halo4WorldCollisionEngineContextTick();
        }
        return result;
    }

    __declspec(noinline) bool __fastcall Halo4PhysicsRayCastDetour(
        Halo4PhysicsRayCastInput* input, Halo4PhysicsRayCastResult* output)
    {
        bool result = true;
        g_halo4Camera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            if (g_halo4MuzzleOwnedQuery ||
                !Halo4RedirectContactRay(reinterpret_cast<uintptr_t>(_ReturnAddress()), input, output, result))
                result = Halo4PhysicsRayCastDetourBody(input, output);
        }
        __finally
        {
            g_halo4Camera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
        return result;
    }

    bool InstallHalo4WorldCollision(uintptr_t base, size_t size, uint32_t generation)
    {
        RemoveHalo4WorldCollision();
        if (!kEnableHalo4WorldCollisionStage4)
        {
            LOG("Halo 4 experimental world contact: StockFallback; the "
                "replacement transaction is disabled; rejected Stages 1-3 "
                "remain dormant and camera, hands, weapon, HUD, reticle, "
                "effects and OpenXR remain unchanged");
            return false;
        }
        if (g_halo4WorldCollision.rayCastTarget)
        {
            LOG("Halo 4 experimental world contact: StockFallback; a prior "
                "raycast hook has not completed teardown");
            return false;
        }
        if (!base || !generation || size != kHalo4RetailImageSize || !g_halo4Camera.modelSkinningTarget)
        {
            LOG("Halo 4 experimental world contact: StockFallback; the "
                "floating-hand palette path is unavailable");
            return false;
        }
        constexpr char kRayCastSignature[] = "48 8B C4 55 56 57 48 8D A8 ?? ?? ?? ?? 48 81 EC 10 0A 00 00 "
                                             "48 C7 44 24 30 FE FF FF FF 48 89 58 18 48 8B 05 ?? ?? ?? ?? "
                                             "48 33 C4 48 89 85 00 09 00 00 48 8B F2 48 8B F9";
        const uintptr_t hit = sig::Find(base, size, kRayCastSignature);
        const bool unique = hit && !sig::Find(hit + 1, base + size - hit - 1, kRayCastSignature);
        const bool pinned = unique && hit - base == kHalo4PhysicsRayCastRva;
        const bool edges = pinned && Halo4DecodeRel32Call(hit + 0x4B) == base + kHalo4PhysicsOutputInitRva &&
                           Halo4DecodeRel32Call(hit + 0xE4) == base + kHalo4PhysicsFilterCtorRva &&
                           Halo4DecodeRel32Call(hit + 0xFF) == base + kHalo4PhysicsWorldRayRva &&
                           Halo4DecodeRel32Call(hit + 0x10F) == base + kHalo4PhysicsCollectorResultRva;
        constexpr char kWorldLineTestSignature[] = "40 55 48 8D AC 24 30 FF FF FF 48 81 EC D0 01 00 00 "
                                                   "48 8B 05 ?? ?? ?? ?? 48 33 C4 48 89 85 C0 00 00 00 "
                                                   "8B 42 08 41 83 C9 FF F2 0F 10 02 66 83 A5 B8 00 00 00 00";
        const uintptr_t worldLineTest = sig::Find(base, size, kWorldLineTestSignature);
        const bool worldLineUnique =
            worldLineTest && !sig::Find(worldLineTest + 1, base + size - worldLineTest - 1, kWorldLineTestSignature);
        const bool worldLinePinned = worldLineUnique && worldLineTest - base == kHalo4WorldLineTestRva &&
                                     Halo4DecodeRel32Call(worldLineTest + 0x84) == hit;
        const uintptr_t worldLineFilter =
            worldLinePinned ? Halo4DecodeRipRelativeAddress(worldLineTest + 0x3E, 3, 7) : 0;
        uint64_t liveWorldLineFilter = 0;
        const bool worldLineFilterPinned = worldLineFilter == base + kHalo4WorldLineFilterGlobalRva &&
                                           Halo4SafeRead(reinterpret_cast<const void*>(worldLineFilter),
                                               &liveWorldLineFilter, sizeof(liveWorldLineFilter)) &&
                                           liveWorldLineFilter == kHalo4WorldLineFilterPair;
        if (!edges || !worldLineFilterPinned || GetModuleHandleW(L"halo4.dll") != reinterpret_cast<HMODULE>(base))
        {
            LOG("Halo 4 experimental world contact: StockFallback; H4EK-"
                "mapped PhysicsRayCast signature was %s%s and its four "
                "internal call edges were %s; the official clear-line "
                "filter was %s; camera/hands/reticle remain unchanged",
                hit ? "present" : "missing", hit && !pinned ? " but not uniquely pinned" : "",
                edges ? "pinned" : "not proven", worldLineFilterPinned ? "pinned" : "not proven");
            return false;
        }

        constexpr char kObjectSetVelocitiesSignature[] = "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 45 33 C9 "
                                                         "49 8B F8 48 8B F2 8B D9 E8 ?? ?? ?? ?? 41 B1 01 4C 8B C7 "
                                                         "48 8B D6 8B CB E8 ?? ?? ?? ?? F3 0F 10 1D ?? ?? ?? ??";
        const uintptr_t objectSetVelocities = sig::Find(base, size, kObjectSetVelocitiesSignature);
        const bool objectSetVelocitiesUnique =
            objectSetVelocities &&
            !sig::Find(objectSetVelocities + 1, base + size - objectSetVelocities - 1, kObjectSetVelocitiesSignature);
        const bool objectSetVelocitiesPinned =
            objectSetVelocitiesUnique && objectSetVelocities - base == kHalo4ObjectSetVelocitiesRva &&
            Halo4DecodeRel32Call(objectSetVelocities + 0x1A) == base + kHalo4ObjectSetVelocitiesCoreRva &&
            Halo4DecodeRel32Call(objectSetVelocities + 0x2A) == base + kHalo4ObjectVelocityMirrorRva &&
            Halo4DecodeRel32Call(objectSetVelocities + 0x93) == base + kHalo4ObjectWakePhysicsRva &&
            Halo4DecodeRel32Call(objectSetVelocities + 0x9A) == base + kHalo4ObjectMotionRefreshRva &&
            Halo4DecodeRel32Call(objectSetVelocities + 0xA1) == base + kHalo4ObjectMotionCommitRva;

        Halo4PhysicsRayCastFn originalRayCast = nullptr;
        void* const rayCastTarget = reinterpret_cast<void*>(hit);
        if (MCCVR_CreateHookChecked(rayCastTarget, reinterpret_cast<void*>(&Halo4PhysicsRayCastDetour),
                reinterpret_cast<void**>(&originalRayCast)) != MH_OK)
        {
            LOG("Halo 4 experimental world contact: StockFallback; MinHook "
                "rejected the optional live PhysicsRayCast hook");
            return false;
        }

        g_halo4WorldCollision.rayCastTarget = rayCastTarget;
        g_halo4WorldCollision.originalRayCast = originalRayCast;
        g_halo4WorldCollision.objectSetVelocities =
            objectSetVelocitiesPinned ? reinterpret_cast<Halo4ObjectSetVelocitiesFn>(objectSetVelocities) : nullptr;
        g_halo4WorldCollision.objectPushInstalled.store(objectSetVelocitiesPinned, std::memory_order_release);
        g_halo4WorldCollision.generation = generation;
        Halo4ResetWorldCollisionState(generation);
        g_halo4WorldCollision.configured.store(g_config.world_collision, std::memory_order_release);
        g_halo4WorldCollision.physicalMeleeConfigured.store(g_config.physical_melee, std::memory_order_release);
        g_halo4WorldCollision.physicalMeleeRequiredSpeed.store(
            g_config.physical_melee_swing_speed, std::memory_order_release);
        g_halo4WorldCollision.configurationResetPending.store(true, std::memory_order_release);
        if (MH_EnableHook(rayCastTarget) != MH_OK)
        {
            MCCVR_RemoveHookChecked(rayCastTarget);
            g_halo4WorldCollision.rayCastTarget = nullptr;
            g_halo4WorldCollision.originalRayCast = nullptr;
            g_halo4WorldCollision.objectSetVelocities = nullptr;
            g_halo4WorldCollision.objectPushInstalled.store(false, std::memory_order_release);
            g_halo4WorldCollision.generation = 0;
            LOG("Halo 4 experimental world contact: StockFallback; the "
                "optional live PhysicsRayCast hook could not be "
                "enabled");
            return false;
        }
        g_halo4WorldCollision.installed.store(true, std::memory_order_release);
        LOG("Halo 4 experimental world contact capability LIVE/config-%s: official H4EK "
            "PhysicsRayCast is uniquely verified at retail RVA 0x%X with "
            "all four internal call edges pinned; retail clear-line wrapper "
            "RVA 0x%X pins the engine-owned 0x%08X/0x%08X filter pair; "
            "the raycast itself is hooked and its original always runs first; "
            "authored Storm-hand and checksum-selected render-model bounds "
            "may query only after an "
            "engine dynamic-inclusive ray completes in that context; native "
            "object motion RVA 0x%X is %s; the stable combined right-side "
            "hand plus 14-sample weapon bounds are enabled; physical "
            "melee is available as an independently default-off OpenXR-"
            "velocity/native-input transaction",
            g_config.world_collision ? "enabled" : "disabled", kHalo4PhysicsRayCastRva, kHalo4WorldLineTestRva,
            kHalo4WorldLineCollisionFlags, kHalo4WorldLineAdditionalFlags, kHalo4ObjectSetVelocitiesRva,
            objectSetVelocitiesPinned ? "LIVE" : "StockFallback");
        return true;
    }

    void Halo4WorldCollisionWorkerTick()
    {
        // This is a cold lifecycle tick. Mirror the menu/config setting for
        // lock-free consumption by the render and engine raycast callbacks.
        if (g_halo4Contact.faulted.load(std::memory_order_acquire) &&
            g_halo4Contact.enabled.exchange(false, std::memory_order_acq_rel))
        {
            g_halo4Contact.enabled.store(false, std::memory_order_release);
            LOG("Halo 4 contact melee FAILED OPEN: native contact callback exception; camera/world contact remain active");
        }
        const bool requested = g_config.world_collision;
        const bool previous = g_halo4WorldCollision.configured.exchange(requested, std::memory_order_acq_rel);
        if (previous != requested)
            g_halo4WorldCollision.configurationResetPending.store(true, std::memory_order_release);
        const bool meleeRequested = g_config.physical_melee;
        const bool previousMelee =
            g_halo4WorldCollision.physicalMeleeConfigured.exchange(meleeRequested, std::memory_order_acq_rel);
        g_halo4WorldCollision.physicalMeleeRequiredSpeed.store(
            g_config.physical_melee_swing_speed, std::memory_order_release);
        if (previousMelee != meleeRequested)
        {
            g_halo4WorldCollision.physicalMeleePulseUntilMs.store(0, std::memory_order_release);
            g_halo4WorldCollision.physicalMeleeCooldownUntilMs.store(0, std::memory_order_release);
            g_halo4WorldCollision.physicalMeleeVelocityLatched[0].store(false, std::memory_order_release);
            g_halo4WorldCollision.physicalMeleeVelocityLatched[1].store(false, std::memory_order_release);
        }
        const uint32_t failure = g_halo4WorldCollision.hotFailureReason.exchange(0, std::memory_order_acq_rel);
        if (!failure)
            return;
        if (failure == kHalo4CollisionFailureObjectPushException)
            LOG("Halo 4 dynamic-object contact FAILED OPEN: native "
                "object motion raised an exception; hand/weapon world "
                "clamping, camera, HUD and OpenXR continue");
        else
            LOG("Halo 4 world contact FAILED OPEN: engine raycast "
                "failed contract %u; only world clamping/object push is "
                "disabled; camera, hands, weapon, HUD, reticle, effects and "
                "OpenXR continue",
                failure);
    }

    void Halo4ApplyWorldCollision(int hand, uint32_t generation, float worldScale, Halo4FloatingTransform& target)
    {
        if (hand < 0 || hand > 1 || !g_halo4WorldCollision.installed.load(std::memory_order_acquire) ||
            !g_halo4WorldCollision.configured.load(std::memory_order_acquire) ||
            g_halo4WorldCollision.configurationResetPending.load(std::memory_order_acquire) ||
            !Halo4FloatingTransformValid(target))
            return;
        uint32_t correctionGeneration = 0;
        uint64_t correctionAtMs = 0;
        float source[3]{}, correction[3]{};
        bool contact = false;
        if (!Halo4ReadCollisionCorrection(hand, correctionGeneration, correctionAtMs, source, correction, contact) ||
            !contact || correctionGeneration != generation)
            return;
        const uint64_t now = GetTickCount64();
        if (correctionAtMs > now || now - correctionAtMs > kHalo4CollisionPublicationMaxAgeMs ||
            !std::isfinite(worldScale) || worldScale <= 0.0f)
            return;
        const float sourceDriftSquared = Halo4WorldCollisionDistanceSquared(source, target.translation);
        const float sourceDriftLimit = 0.15f * worldScale;
        const float correctionLimit = 0.75f * worldScale;
        float correctionLengthSquared = 0.0f;
        for (float value : correction)
            correctionLengthSquared += value * value;
        if (sourceDriftSquared < 0.0f || sourceDriftSquared > sourceDriftLimit * sourceDriftLimit ||
            !std::isfinite(correctionLengthSquared) || correctionLengthSquared > correctionLimit * correctionLimit)
            return;
        Halo4FloatingTransform corrected = target;
        for (int axis = 0; axis < 3; ++axis)
            corrected.translation[axis] += correction[axis];
        if (Halo4FloatingTransformValid(corrected))
        {
            target = corrected;
            g_halo4WorldCollision.applied[hand].fetch_add(1, std::memory_order_relaxed);
        }
    }

    template <size_t N>
    uint32_t Halo4BuildAuthoredCollisionExtrema(const BoneMatrix* matrices, const int (&nodes)[N], int wristNode,
        const float collisionRoot[3], float output[][3])
    {
        if (!matrices || wristNode < 0 || !Halo4WorldCollisionFiniteVector(collisionRoot) ||
            !Halo4WorldCollisionFiniteVector(matrices[wristNode].translation))
            return 0;
        float authored[N][3]{};
        int authoredCount = 0;
        for (int node : nodes)
        {
            if (node < 0 || !Halo4WorldCollisionFiniteVector(matrices[node].translation))
                continue;
            for (int axis = 0; axis < 3; ++axis)
                authored[authoredCount][axis] =
                    collisionRoot[axis] + matrices[node].translation[axis] - matrices[wristNode].translation[axis];
            ++authoredCount;
        }
        return static_cast<uint32_t>(Halo4SelectWorldCollisionExtrema(
            collisionRoot, &authored[0][0], authoredCount, output, kHalo4WorldCollisionExtremaCount));
    }

    void Halo4PublishAuthoredHandCollisionVolumes(const BoneMatrix* solved, int32_t ignoredObjectIndex)
    {
        if ((!g_halo4WorldCollision.configured.load(std::memory_order_acquire) &&
                !(Halo4ContactMeleeReady() && g_config.physical_melee)) ||
            !solved || !g_halo4FloatingPair.targetsValid || !g_halo4FloatingPair.generation)
            return;
        const bool swapped = g_halo4FloatingPair.handAlignment;
        const int primaryWrist = swapped ? kHalo4LeftHandNode : kHalo4RightHandNode;
        const uint32_t rightCount =
            swapped ? Halo4BuildAuthoredCollisionExtrema(solved, kHalo4LeftHandSubtree, kHalo4LeftHandNode,
                          g_halo4FloatingPair.collisionRoot[1], g_halo4FloatingPair.handCollisionSamples[1])
                    : Halo4BuildAuthoredCollisionExtrema(solved, kHalo4RightHandSubtree, kHalo4RightHandNode,
                          g_halo4FloatingPair.collisionRoot[1], g_halo4FloatingPair.handCollisionSamples[1]);
        const uint32_t leftCount =
            swapped ? Halo4BuildAuthoredCollisionExtrema(solved, kHalo4RightHandSubtree, kHalo4RightHandNode,
                          g_halo4FloatingPair.collisionRoot[0], g_halo4FloatingPair.handCollisionSamples[0])
                    : Halo4BuildAuthoredCollisionExtrema(solved, kHalo4LeftHandSubtree, kHalo4LeftHandNode,
                          g_halo4FloatingPair.collisionRoot[0], g_halo4FloatingPair.handCollisionSamples[0]);
        g_halo4FloatingPair.handCollisionSampleCount[1] = rightCount;
        g_halo4FloatingPair.handCollisionSampleCount[0] = leftCount;
        g_halo4FloatingPair.collisionIgnoredObjectIndex = ignoredObjectIndex;
        if (Halo4WorldCollisionFiniteVector(solved[primaryWrist].translation))
        {
            memcpy(g_halo4FloatingPair.rightSolvedWrist, solved[primaryWrist].translation,
                sizeof(g_halo4FloatingPair.rightSolvedWrist));
            g_halo4FloatingPair.rightSolvedWristValid = true;
        }
        // The right side is deliberately deferred until the immediately
        // following held record can publish one stable hand+weapon volume.
        // Publishing here as well caused 985 reseeds in headset testing
        // and made the gun visually ineffective.
        if (rightCount && !kEnableHalo4WeaponWorldCollisionStage6)
            Halo4PublishCollisionTarget(1, g_halo4FloatingPair.generation, g_halo4FloatingPair.handCollisionSamples[1],
                rightCount, rightCount, ignoredObjectIndex);
        if (leftCount)
            Halo4PublishCollisionTarget(0, g_halo4FloatingPair.generation, g_halo4FloatingPair.handCollisionSamples[0],
                leftCount, leftCount, ignoredObjectIndex);
    }

#pragma warning(push)
#pragma warning(disable : 4702) // audit finding: the code after publishHandOnly(); return; never runs
    void Halo4PublishAuthoredWeaponCollisionVolume(const BoneMatrix* moved, int nodeCount,
        uint32_t runtimeImportChecksum, const Halo4WeaponCollisionBounds* runtimeBounds, uint32_t shapeId)
    {
        if ((!g_halo4WorldCollision.configured.load(std::memory_order_acquire) &&
                !(Halo4ContactMeleeReady() && g_config.physical_melee)))
            return;
        const uint32_t handCount = g_halo4FloatingPair.handCollisionSampleCount[1];
        if (!handCount || !g_halo4FloatingPair.rightSolvedWristValid)
            return;
        const auto publishHandOnly = [&]()
        {
            Halo4PublishCollisionTarget(1, g_halo4FloatingPair.generation, g_halo4FloatingPair.handCollisionSamples[1],
                handCount, handCount, g_halo4FloatingPair.collisionIgnoredObjectIndex, shapeId);
        };
        if (!moved || nodeCount <= 0 || nodeCount > kHalo4FirstPersonBankTransforms)
        {
            publishHandOnly();
            return;
        }
        if (kEnableHalo4WeaponWorldCollisionStage6)
        {
            const Halo4WeaponCollisionBounds* bounds =
                runtimeBounds ? runtimeBounds : Halo4FindWeaponCollisionBounds(runtimeImportChecksum);
            float rootBasis[9]{};
            float weapon[kHalo4WeaponCollisionBoundsSampleCount][3]{};
            if (!bounds || !NormalizedBasis(moved[0], rootBasis) ||
                Halo4BuildWeaponCollisionBoundsSamples(*bounds, moved[0].scale, rootBasis, moved[0].translation,
                    g_halo4FloatingPair.collisionRoot[1], g_halo4FloatingPair.rightSolvedWrist, weapon,
                    kHalo4WeaponCollisionBoundsSampleCount) != kHalo4WeaponCollisionBoundsSampleCount)
            {
                g_halo4WorldCollision.weaponBoundsFallbacks.fetch_add(1, std::memory_order_relaxed);
                publishHandOnly();
                return;
            }
            float combined[kHalo4WorldCollisionMaxSamples][3]{};
            memcpy(combined, g_halo4FloatingPair.handCollisionSamples[1], sizeof(float) * handCount * 3);
            memcpy(combined + handCount, weapon, sizeof(weapon));
            Halo4PublishCollisionTarget(1, g_halo4FloatingPair.generation, combined,
                handCount + kHalo4WeaponCollisionBoundsSampleCount, handCount,
                g_halo4FloatingPair.collisionIgnoredObjectIndex, shapeId);
            g_halo4WorldCollision.weaponBoundsPublished.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        // Preserve the headset-accepted right-hand volume when the combined
        // hand+weapon volume is dormant, without reviving the earlier alternating
        // combined publication.
        publishHandOnly();
        return;
        float authored[kHalo4FirstPersonBankTransforms][3]{};
        int authoredCount = 0;
        for (int node = 0; node < nodeCount; ++node)
        {
            if (!Halo4WorldCollisionFiniteVector(moved[node].translation))
                continue;
            for (int axis = 0; axis < 3; ++axis)
                authored[authoredCount][axis] = g_halo4FloatingPair.collisionRoot[1][axis] +
                                                moved[node].translation[axis] -
                                                g_halo4FloatingPair.rightSolvedWrist[axis];
            ++authoredCount;
        }
        float weapon[kHalo4WorldCollisionExtremaCount][3]{};
        const int weaponCount = Halo4SelectWorldCollisionExtrema(g_halo4FloatingPair.collisionRoot[1], &authored[0][0],
            authoredCount, weapon, kHalo4WorldCollisionExtremaCount);
        float combined[kHalo4WorldCollisionMaxSamples][3]{};
        uint32_t combinedCount = handCount;
        memcpy(combined, g_halo4FloatingPair.handCollisionSamples[1], sizeof(float) * handCount * 3);
        // weapon[0] duplicates the common hand root. Append only the six
        // title-authored held-model extrema, then publish the right side once.
        for (int sample = 1; sample < weaponCount && combinedCount < kHalo4WorldCollisionMaxSamples; ++sample)
        {
            memcpy(combined[combinedCount], weapon[sample], sizeof(combined[combinedCount]));
            ++combinedCount;
        }
        Halo4PublishCollisionTarget(1, g_halo4FloatingPair.generation, combined, combinedCount, handCount,
            g_halo4FloatingPair.collisionIgnoredObjectIndex, runtimeImportChecksum);
    }
#pragma warning(pop)
    // THE 28 BONES BETWEEN THE JOINTS, which the shared solver does not move.
    //
    // ReconstructVisiblePaletteSource writes exactly three things per arm:
    // the shoulder record, the elbow record, and every bone in the hand mask.
    // That is complete for Halo 3/ODST/Reach, whose first-person skeletons put
    // nothing between the joints. storm_fp is not built that way - these are
    // the bands left over once the joints and both hand subtrees are removed
    // from the shoulder/elbow subtrees, and the H4EK tag names them:
    //
    //   b_r/l_shoulder_helper1..4, _shoulder_fixup, _upperarm_fixup,
    //   b_r/l_forearm_helper1..4, _forearm_fixup, _elbow_fixup,
    //   _elbow_armor, _hand_twist
    //
    // They are the deformation helpers the arm mesh is skinned to. Left at
    // stock while the joints move, the arm tears open between shoulder and
    // hand. The deleted private solver never had this problem because it
    // applied its deltas over the WHOLE shoulder and elbow subtrees; carrying
    // these bands restores exactly that reach without touching the shared
    // solver, which the three shipped titles depend on.
    //
    // Each band rides the joint ABOVE it: the upper-arm band follows the
    // shoulder, the forearm band follows the elbow. No joint appears in its
    // own band (applying a joint's delta to the already-solved joint would
    // compose solved*stock^-1*solved), the bands are disjoint from each other
    // and from both hand subtrees, and every index is < 64.
    constexpr int kHalo4RightUpperArmBand[] = {11, 12, 14, 15, 17, 18};
    constexpr int kHalo4RightForearmBand[] = {22, 26, 27, 30, 31, 34, 36, 38};
    constexpr int kHalo4LeftUpperArmBand[] = {7, 9, 10, 13, 19, 20};
    constexpr int kHalo4LeftForearmBand[] = {21, 23, 24, 25, 28, 32, 33, 35};

    float Halo4VrikDistance(const BoneMatrix& a, const BoneMatrix& b)
    {
        const float x = b.translation[0] - a.translation[0];
        const float y = b.translation[1] - a.translation[1];
        const float z = b.translation[2] - a.translation[2];
        return sqrtf(x * x + y * y + z * z);
    }

    // Bin argument 7. Lock-free, bounded to kHalo4VrikCountSlots probes, and
    // deterministic: the same palette traffic always produces the same bins.
    void Halo4RecordSkinningCount(int32_t count)
    {
        // The caller's `jle` path can reach this call site with a non-positive
        // count, so the empty-slot sentinel is the one value that cannot be
        // binned without a slot claiming itself.
        if (count != kHalo4VrikCountSlotEmpty)
        {
            for (int slot = 0; slot < kHalo4VrikCountSlots; ++slot)
            {
                std::atomic<int32_t>& value = g_halo4Camera.vrikCountValue[slot];
                int32_t seen = value.load(std::memory_order_acquire);
                if (seen == kHalo4VrikCountSlotEmpty)
                {
                    // A successful exchange leaves `seen` untouched; a lost
                    // race overwrites it with the winner's value, which is
                    // exactly what the match below needs either way.
                    if (value.compare_exchange_strong(
                            seen, count, std::memory_order_acq_rel, std::memory_order_acquire))
                        seen = count;
                }
                if (seen == count)
                {
                    g_halo4Camera.vrikCountHits[slot].fetch_add(1, std::memory_order_relaxed);
                    return;
                }
            }
        }
        g_halo4Camera.vrikCountOverflow.fetch_add(1, std::memory_order_relaxed);
    }

    // Weapon-order diagnostic tranche: counterless fill-flag peek. Recording
    // must not change the totals of the existing VRIK presentation counters,
    // so the diagnostic path never calls Halo4RecordFillFlag.
    int32_t Halo4DiagnosticPeekFillFlag(const BoneMatrix* input) noexcept
    {
        if (!input)
            return -1;
        int32_t flag = 0;
        const unsigned char* record = reinterpret_cast<const unsigned char*>(input) - kHalo4FirstPersonRecordBankOffset;
        if (!Halo4SafeRead(record + kHalo4FirstPersonRecordFillFlagOffset, &flag, sizeof(flag)))
            return -1;
        return flag;
    }

    // The producer-authored 0x1910 record flag partitions the ordered loop:
    // flag 1 covers both storm_fp hands and the held model, then flag 0 closes
    // the sequence with the native body/legs model. It is not anatomy by
    // itself. Unknown or unreadable values never enter either transform path.
    int32_t Halo4RecordFillFlag(const BoneMatrix* input)
    {
        if (!input)
        {
            g_halo4Camera.vrikUnreadableFillRecords.fetch_add(1, std::memory_order_relaxed);
            return -1;
        }
        int32_t flag = 0;
        const unsigned char* record = reinterpret_cast<const unsigned char*>(input) - kHalo4FirstPersonRecordBankOffset;
        if (!Halo4SafeRead(record + kHalo4FirstPersonRecordFillFlagOffset, &flag, sizeof(flag)))
        {
            g_halo4Camera.vrikUnreadableFillRecords.fetch_add(1, std::memory_order_relaxed);
            return -1;
        }
        if (flag == kHalo4FirstPersonBodyFillFlag)
            g_halo4Camera.vrikBodyFillRecords.fetch_add(1, std::memory_order_relaxed);
        else if (flag == kHalo4FirstPersonWeaponFillFlag)
            g_halo4Camera.vrikWeaponFillRecords.fetch_add(1, std::memory_order_relaxed);
        else
            g_halo4Camera.vrikUnexpectedFillRecords.fetch_add(1, std::memory_order_relaxed);
        return flag;
    }

    // Decide whether this record IS the Storm first-person body, from matrix
    // relationships the H4EK tag proves - never from argument 7, which
    // was measured to be a per-render-model skinning PALETTE SIZE.
    // `publish` is true only for the engine's own body fill. The two weapon
    // records reach this classifier too, and their slots 4/16/29/5/8/37 are
    // unrelated bones - letting them write the probe made the measured arm
    // links an average of the real arms and a weapon, which reads as
    // "bone lengths that change every window" and is deeply misleading.
    Halo4VrikStage Halo4ClassifyStormArms(const BoneMatrix* nodes, const BoneMatrix& vrRoot, bool publish)
    {
        const int indices[] = {kHalo4RightShoulderNode, kHalo4RightElbowNode, kHalo4RightHandNode,
            kHalo4LeftShoulderNode, kHalo4LeftElbowNode, kHalo4LeftHandNode};

        // Publish what the ENGINE holds for the first arm node BEFORE judging
        // any of it. A real animation basis is orthonormal to float noise, so
        // these four numbers separate "we are decoding the element wrongly"
        // from "this is not the model we think it is" without another guess.
        if (publish)
        {
            g_halo4Camera.vrikClassifyAttempts.fetch_add(1, std::memory_order_relaxed);
            const BoneMatrix& probe = nodes[kHalo4RightShoulderNode];
            float column[3][3]{};
            float worstColumn = 0.0f, worstDot = 0.0f, translationMagnitude = 0.0f;
            for (int c = 0; c < 3; ++c)
            {
                float lengthSquared = 0.0f;
                for (int r = 0; r < 3; ++r)
                {
                    column[c][r] = probe.rotation[c * 3 + r];
                    lengthSquared += column[c][r] * column[c][r];
                }
                const float length = sqrtf(lengthSquared);
                const float deviation = fabsf(length - 1.0f);
                if (!(deviation <= worstColumn))
                    worstColumn = deviation;
                if (isfinite(length) && length > 1.0e-6f)
                    for (int r = 0; r < 3; ++r)
                        column[c][r] /= length;
            }
            for (int a = 0; a < 3; ++a)
                for (int b = a + 1; b < 3; ++b)
                {
                    float dot = 0.0f;
                    for (int r = 0; r < 3; ++r)
                        dot += column[a][r] * column[b][r];
                    if (!(fabsf(dot) <= worstDot))
                        worstDot = fabsf(dot);
                }
            for (float v : probe.translation)
                translationMagnitude += v * v;
            translationMagnitude = sqrtf(translationMagnitude);
            g_halo4Camera.vrikProbeScale.store(probe.scale, std::memory_order_relaxed);
            g_halo4Camera.vrikProbeColumnError.store(worstColumn, std::memory_order_relaxed);
            g_halo4Camera.vrikProbeOrthoError.store(worstDot, std::memory_order_relaxed);
            g_halo4Camera.vrikProbeTranslation.store(translationMagnitude, std::memory_order_relaxed);
        }

        for (int index : indices)
        {
            float basis[9];
            if (!NormalizedBasis(nodes[index], basis))
                return Halo4VrikStage::BasisFailed;
            // Only non-finite is a refusal now. An earlier check also demanded every
            // component be within 10 units of the origin, which silently
            // assumed these matrices are object-local; the arm-link lengths
            // this classifier actually depends on are translation-invariant,
            // so that bound could only ever cause a false refusal.
            for (float v : nodes[index].translation)
                if (!isfinite(v))
                    return Halo4VrikStage::RangeFailed;
        }
        const float ru = Halo4VrikDistance(nodes[kHalo4RightShoulderNode], nodes[kHalo4RightElbowNode]);
        const float rl = Halo4VrikDistance(nodes[kHalo4RightElbowNode], nodes[kHalo4RightHandNode]);
        const float lu = Halo4VrikDistance(nodes[kHalo4LeftShoulderNode], nodes[kHalo4LeftElbowNode]);
        const float ll = Halo4VrikDistance(nodes[kHalo4LeftElbowNode], nodes[kHalo4LeftHandNode]);
        // Publish the measurement BEFORE judging it, so a window that admits
        // nothing still reports the four distances the engine actually holds.
        if (publish)
        {
            g_halo4Camera.vrikLastRightUpper.store(ru, std::memory_order_relaxed);
            g_halo4Camera.vrikLastRightLower.store(rl, std::memory_order_relaxed);
            g_halo4Camera.vrikLastLeftUpper.store(lu, std::memory_order_relaxed);
            g_halo4Camera.vrikLastLeftLower.store(ll, std::memory_order_relaxed);
            g_halo4Camera.vrikLinkMeasurements.fetch_add(1, std::memory_order_relaxed);
        }
        if (!Halo4StormLinkLengthsMatch(ru, rl, lu, ll))
            return Halo4VrikStage::LinkFailed;
        // Shoulder separation along the VR camera's own LEFT axis. MEASURED
        // AND PUBLISHED, NEVER REFUSED ON.
        //
        // This test asserted that node 5 sits on the camera's +left of node 4,
        // i.e. that the second basis column really points left. Nothing ever
        // proved that convention, and at first the test never even ran -
        // the bind-derived link gate returned LinkFailed first, so its 0
        // refusals meant "never reached", not "passed". The moment the link
        // gate was corrected, this refused 972/972, 968/968 and 148/172 body
        // records: it is the whole reason the arms went dead.
        //
        // It also cannot change any assignment. The H4EK tag is authoritative
        // about anatomy - node 4 is b_r_upperarm and node 5 is b_l_upperarm -
        // so the left controller drives node 5 whichever way the camera's Y
        // axis happens to point. Refusing the record on an unproven axis
        // convention only ever cost the arms; it never protected anything.
        //
        // Record identity is established before this point from the
        // engine's ordered flag-1/80 Storm slot. The fill flag alone only
        // partitions first-person-loop records from native body/legs and
        // cannot distinguish Storm hands from the held model. This publishes
        // the sign so the convention remains measured evidence.
        float separation[3];
        for (int i = 0; i < 3; ++i)
            separation[i] =
                nodes[kHalo4LeftShoulderNode].translation[i] - nodes[kHalo4RightShoulderNode].translation[i];
        const float towardLeft = vrRoot.rotation[3] * separation[0] + vrRoot.rotation[4] * separation[1] +
                                 vrRoot.rotation[5] * separation[2];
        if (publish)
            g_halo4Camera.vrikShoulderTowardLeft.store(towardLeft, std::memory_order_relaxed);
        return Halo4VrikStage::Solved;
    }

    bool Halo4QuaternionToBoneBasis(const float qIn[4], float out[9])
    {
        float q[4];
        if (!Halo4NormalizeQuaternion(qIn, q))
            return false;
        const float x = q[0], y = q[1], z = q[2], w = q[3];
        const float row[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w), 2 * (x * y + z * w),
            1 - 2 * (x * x + z * z), 2 * (y * z - x * w), 2 * (x * z - y * w), 2 * (y * z + x * w),
            1 - 2 * (x * x + y * y)};
        for (int r = 0; r < 3; ++r)
            for (int c = 0; c < 3; ++c)
                out[c * 3 + r] = row[r * 3 + c];
        return true;
    }

    bool Halo4BuildControllerBodyRoot(BoneMatrix& root)
    {
        if (!g_halo4Camera.headReferenceValid.load(std::memory_order_acquire) ||
            !g_halo4Camera.gameYawReferenceValid.load(std::memory_order_acquire))
            return false;
        BoneMatrix preHmd{};
        if (!Halo4LoadControllerRoot(1.0f, preHmd))
            return false;
        const float gameYaw = g_halo4Camera.gameYawReference.load(std::memory_order_relaxed);
        float forward[3], up[3];
        Halo4ComposeHeadOwnedBasis(gameYaw, 0.0f, 0.0f, forward, up);
        const float left[3] = {up[1] * forward[2] - up[2] * forward[1], up[2] * forward[0] - up[0] * forward[2],
            up[0] * forward[1] - up[1] * forward[0]};
        root = BoneMatrix{};
        root.scale = 1.0f;
        for (int axis = 0; axis < 3; ++axis)
        {
            root.translation[axis] = preHmd.translation[axis];
            root.rotation[axis] = forward[axis];
            root.rotation[3 + axis] = left[axis];
            root.rotation[6 + axis] = up[axis];
        }
        return NormalizedBasis(root, root.rotation);
    }

    // Halo 4's final palette is a different consumer from Halo 3/ODST/Reach.
    // Keep the cross-title strategy (articulate the body first; carry the
    // separate weapon by the gripping hand), but solve this title's authored
    // Storm hierarchy directly.  Applying each joint delta to its complete
    // tag-proven subtree moves the helper, armour and finger nodes together;
    // there is no 64-bit-mask tail or post-solve helper-band repair involved.
    bool Halo4BuildDirectionDelta(const float from[3], const float to[3], const float pivot[3], BoneMatrix& delta)
    {
        float a[3] = {from[0], from[1], from[2]};
        float b[3] = {to[0], to[1], to[2]};
        auto normalize = [](float v[3])
        {
            const float length = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
            if (!isfinite(length) || length < 1.0e-5f)
                return false;
            for (int i = 0; i < 3; ++i)
                v[i] /= length;
            return true;
        };
        if (!normalize(a) || !normalize(b))
            return false;
        const float cross[3] = {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]};
        float dot = Clamp(a[0] * b[0] + a[1] * b[1] + a[2] * b[2], -1.0f, 1.0f);
        float quaternion[4] = {cross[0], cross[1], cross[2], 1.0f + dot};
        if (1.0f + dot < 1.0e-4f)
        {
            const float axis[3] = {fabsf(a[0]) < 0.8f ? 1.0f : 0.0f, fabsf(a[0]) < 0.8f ? 0.0f : 1.0f, 0.0f};
            quaternion[0] = a[1] * axis[2] - a[2] * axis[1];
            quaternion[1] = a[2] * axis[0] - a[0] * axis[2];
            quaternion[2] = a[0] * axis[1] - a[1] * axis[0];
            quaternion[3] = 0.0f;
        }
        delta = BoneMatrix{};
        delta.scale = 1.0f;
        if (!Halo4QuaternionToBoneBasis(quaternion, delta.rotation))
            return false;
        for (int row = 0; row < 3; ++row)
        {
            float rotated = 0.0f;
            for (int column = 0; column < 3; ++column)
                rotated += delta.rotation[column * 3 + row] * pivot[column];
            delta.translation[row] = pivot[row] - rotated;
        }
        return true;
    }

    template <size_t N>
    bool Halo4ApplySubtreeDelta(BoneMatrix* nodes, const int (&indices)[N], const BoneMatrix& delta)
    {
        for (int index : indices)
        {
            BoneMatrix moved{};
            if (!ComposeBoneMatrices(delta, nodes[index], moved))
                return false;
            nodes[index] = moved;
        }
        return true;
    }

    bool Halo4BuildTranslationDelta(const float from[3], const float to[3], BoneMatrix& delta)
    {
        delta = BoneMatrix{};
        delta.scale = 1.0f;
        delta.rotation[0] = delta.rotation[4] = delta.rotation[8] = 1.0f;
        for (int axis = 0; axis < 3; ++axis)
        {
            delta.translation[axis] = to[axis] - from[axis];
            if (!isfinite(delta.translation[axis]))
                return false;
        }
        return true;
    }

    // The controller pose the hands are measured RELATIVE to.
    //
    // Earlier designs computed an ABSOLUTE hand position and needed the
    // bank's geometry to mean what the tag says. It does not. In one window the
    // same record reports the shoulders 0.39x their authored separation, the
    // hand 0.74x its authored distance from the root, and the upper arm 2.1x
    // its authored length. No scale, no unit convention and no node reordering
    // makes those one skeleton - that was checked exhaustively against the tag -
    // so an absolute target can only ever be luck.
    //
    // The way out is to stop needing the geometry at all. Halo 4 already draws
    // the hand in exactly the right place; take THAT as the origin and add only
    // what the player's hand has done since a reference pose. At the reference
    // the hands are byte-identical to stock, which cannot look wrong, and every
    // departure from it is a measured controller movement.
    struct Halo4HandReference
    {
        std::atomic<bool> valid{false};
        std::atomic<float> position[3]{};
        std::atomic<float> orientation[4]{};
    };
    constinit Halo4HandReference g_halo4HandReference[2]; // 0 = right, 1 = left

    // Title-native two-bone application over the actual Storm subtrees.  The
    // generic analytic elbow point is merely geometry; all axes, poles,
    // attachment orientation and every carried node belong to Halo 4 evidence.
    //
    // Optional arms-with-hands solve on the exact H4EK storm_fp chain. The
    // historical 0.2100-0.2137 readings used the wrong indices on the separate
    // 120-node flag-0 body and are not an arm-scale measurement. This routine
    // derives shoulder/elbow/wrist distances from the admitted 80-node
    // storm_fp palette itself. The classifier admits only the exact ordered
    // 80-node Storm record with finite bases, plausible live link ranges and
    // bilateral upper-arm symmetry. It measures shoulder separation but does
    // not refuse on a camera-axis sign (the world-rooted sign changes with
    // heading). A solve failure is rolled back before the rigid-hand fallback.
    bool Halo4SolveStormArm(
        BoneMatrix* nodes, bool left, const BoneMatrix& desired, const BoneMatrix& bodyRoot, BoneMatrix* outHandDelta)
    {
        const int shoulder = left ? kHalo4LeftShoulderNode : kHalo4RightShoulderNode;
        const int elbow = left ? kHalo4LeftElbowNode : kHalo4RightElbowNode;
        const int hand = left ? kHalo4LeftHandNode : kHalo4RightHandNode;
        const float upper = Halo4VrikDistance(nodes[shoulder], nodes[elbow]);
        const float lower = Halo4VrikDistance(nodes[elbow], nodes[hand]);
        const float* authoredPole = left ? kHalo4LeftPoleDirection : kHalo4RightPoleDirection;
        float pole[3];
        for (int row = 0; row < 3; ++row)
            pole[row] = bodyRoot.rotation[row] * authoredPole[0] + bodyRoot.rotation[3 + row] * authoredPole[1] +
                        bodyRoot.rotation[6 + row] * authoredPole[2];
        const float dx = desired.translation[0] - nodes[shoulder].translation[0];
        const float dy = desired.translation[1] - nodes[shoulder].translation[1];
        const float dz = desired.translation[2] - nodes[shoulder].translation[2];
        const float span = sqrtf(dx * dx + dy * dy + dz * dz);
        Halo4ArmReachPlan reach{};
        if (!Halo4PlanArmReach(upper, lower, span, reach))
            return false;
        float solvedElbow[3];
        if (!IK_SolveTwoBone(nodes[shoulder].translation, desired.translation, reach.upperLength, reach.lowerLength,
                pole, solvedElbow))
            return false;
        float oldUpper[3], newUpper[3];
        for (int i = 0; i < 3; ++i)
        {
            oldUpper[i] = nodes[elbow].translation[i] - nodes[shoulder].translation[i];
            newUpper[i] = solvedElbow[i] - nodes[shoulder].translation[i];
        }
        BoneMatrix shoulderDelta{};
        if (!Halo4BuildDirectionDelta(oldUpper, newUpper, nodes[shoulder].translation, shoulderDelta))
            return false;
        if (left ? !Halo4ApplySubtreeDelta(nodes, kHalo4LeftShoulderSubtree, shoulderDelta)
                 : !Halo4ApplySubtreeDelta(nodes, kHalo4RightShoulderSubtree, shoulderDelta))
            return false;

        // A direction delta is deliberately rigid: it cannot change the
        // shoulder-to-elbow distance.  The old path stopped here and later
        // teleported only the hand subtree, concentrating BOTH links' planned
        // extension across the H4EK tag's 222/235 forearm-hand blended
        // vertices.  Move the complete elbow subtree to the analytic elbow
        // endpoint first.  The final hand delta below then contributes only
        // the lower link's proportional extension, so Halo 4's authored
        // weights interpolate both segments instead of opening one seam.
        BoneMatrix upperStretchDelta{};
        if (!Halo4BuildTranslationDelta(nodes[elbow].translation, solvedElbow, upperStretchDelta) ||
            (left ? !Halo4ApplySubtreeDelta(nodes, kHalo4LeftElbowSubtree, upperStretchDelta)
                  : !Halo4ApplySubtreeDelta(nodes, kHalo4RightElbowSubtree, upperStretchDelta)))
            return false;
        float oldLower[3], newLower[3];
        for (int i = 0; i < 3; ++i)
        {
            oldLower[i] = nodes[hand].translation[i] - nodes[elbow].translation[i];
            newLower[i] = desired.translation[i] - nodes[elbow].translation[i];
        }
        BoneMatrix elbowDelta{};
        if (!Halo4BuildDirectionDelta(oldLower, newLower, nodes[elbow].translation, elbowDelta))
            return false;
        if (left ? !Halo4ApplySubtreeDelta(nodes, kHalo4LeftElbowSubtree, elbowDelta)
                 : !Halo4ApplySubtreeDelta(nodes, kHalo4RightElbowSubtree, elbowDelta))
            return false;
        BoneMatrix inverseHand{}, handDelta{};
        if (!InvertBoneMatrix(nodes[hand], inverseHand) || !ComposeBoneMatrices(desired, inverseHand, handDelta) ||
            (left ? !Halo4ApplySubtreeDelta(nodes, kHalo4LeftHandSubtree, handDelta)
                  : !Halo4ApplySubtreeDelta(nodes, kHalo4RightHandSubtree, handDelta)))
            return false;
        if (outHandDelta)
            *outHandDelta = handDelta;
        return true;
    }

    bool Halo4ToFloatingTransform(const BoneMatrix& source, Halo4FloatingTransform& target)
    {
        target.scale = source.scale;
        memcpy(target.rotation, source.rotation, sizeof(target.rotation));
        memcpy(target.translation, source.translation, sizeof(target.translation));
        return Halo4FloatingTransformValid(target);
    }

    bool Halo4ToBoneMatrix(const Halo4FloatingTransform& source, BoneMatrix& target)
    {
        if (!Halo4FloatingTransformValid(source))
            return false;
        target = BoneMatrix{};
        target.scale = source.scale;
        memcpy(target.rotation, source.rotation, sizeof(target.rotation));
        memcpy(target.translation, source.translation, sizeof(target.translation));
        return true;
    }

    float Halo4FloatingDistance(const Halo4FloatingTransform& first, const Halo4FloatingTransform& second)
    {
        float squared = 0.0f;
        for (int axis = 0; axis < 3; ++axis)
        {
            const float delta = first.translation[axis] - second.translation[axis];
            squared += delta * delta;
        }
        return isfinite(squared) ? sqrtf(squared) : INFINITY;
    }

    struct Halo4FloatingTargetFrame
    {
        Halo4ControllerWorldPoseInput common{};
        float rightForwardTrim = 0.0f;
        float leftForwardTrim = 0.0f;
        float gunYawDeg = 0.0f;
        float gunPitchDeg = 0.0f;
        float gunRollDeg = 0.0f;
        float visualPitchDeg = 0.0f, visualYawDeg = 0.0f, visualRollDeg = 0.0f;
        bool twoHandAimActive = false;
    };

    bool Halo4FreezeFloatingTargetFrame(Halo4FloatingTargetFrame& frame)
    {
        if (!g_halo4RigTrackingActive || !g_halo4RigTracking.preparedSerial ||
            !g_halo4Camera.headReferenceValid.load(std::memory_order_acquire) ||
            !g_halo4Camera.gameYawReferenceValid.load(std::memory_order_acquire))
            return false;
        BoneMatrix preHmd{};
        if (!Halo4LoadControllerRoot(1.0f, preHmd))
            return false;
        frame = Halo4FloatingTargetFrame{};
        for (int axis = 0; axis < 3; ++axis)
        {
            frame.common.trackingOriginPosition[axis] =
                g_halo4Camera.headPositionReference[axis].load(std::memory_order_relaxed);
            frame.common.bodyOrigin[axis] = preHmd.translation[axis];
        }
        frame.common.headYawReference = g_halo4Camera.headYawReference.load(std::memory_order_relaxed);
        frame.common.gameYawReference = g_halo4Camera.gameYawReference.load(std::memory_order_relaxed);
        frame.common.yawSign = g_yawSign.load(std::memory_order_relaxed);
        frame.common.pitchSign = g_pitchSign.load(std::memory_order_relaxed);
        frame.common.worldScale = g_worldScale.load(std::memory_order_relaxed);
        frame.common.verticalTrim = g_config.halo4_hand_vertical_m;
        frame.common.lateralTrim = g_config.halo4_hand_lateral_m;
        frame.common.mirrored = g_config.halo4_hands_mirrored;
        frame.rightForwardTrim = g_config.halo4_hand_forward_m + Clamp(g_config.gun_forward_m, -0.3f, 0.5f);
        frame.leftForwardTrim = g_config.halo4_hand_forward_m + Clamp(g_config.left_hand_forward_m, -0.15f, 0.30f);
        frame.gunYawDeg = g_config.gun_yaw_deg;
        frame.gunPitchDeg = g_config.gun_pitch_deg;
        frame.gunRollDeg = g_config.gun_roll_deg;
        frame.visualPitchDeg = g_config.barrel_pitch_deg;
        frame.visualYawDeg = g_config.barrel_yaw_deg;
        frame.visualRollDeg = g_config.barrel_roll_deg;
        frame.twoHandAimActive = g_halo4RigTracking.twoHandAimActive;
        for (float value : frame.common.trackingOriginPosition)
            if (!isfinite(value))
                return false;
        for (float value : frame.common.bodyOrigin)
            if (!isfinite(value))
                return false;
        return isfinite(frame.common.headYawReference) && isfinite(frame.common.gameYawReference) &&
               isfinite(frame.common.yawSign) && isfinite(frame.common.pitchSign) &&
               isfinite(frame.common.worldScale) && frame.common.worldScale > 0.0f &&
               isfinite(frame.common.verticalTrim) && isfinite(frame.common.lateralTrim) &&
               isfinite(frame.rightForwardTrim) && isfinite(frame.leftForwardTrim) && isfinite(frame.gunYawDeg) &&
               isfinite(frame.gunPitchDeg) && isfinite(frame.gunRollDeg) && isfinite(frame.visualPitchDeg) &&
               isfinite(frame.visualYawDeg) && isfinite(frame.visualRollDeg);
    }

    // Build one physical controller carrier from the immutable prepared
    // snapshot. Its position is frozen once before either eye and is never
    // rebuilt from mutable reference atomics in the palette hook. The reroot adds
    // each current eye's live authored wrist relation only after the exact
    // Storm record arrives.
    bool Halo4BuildFloatingWorldTarget(
        bool left, const Halo4FloatingTargetFrame& frame, Halo4FloatingTransform& target, bool oneHandRight = false)
    {
        const bool rightValid =
            oneHandRight ? g_halo4RigTracking.oneHandRightAimValid : g_halo4RigTracking.rightAimValid;
        if (left ? !g_halo4RigTracking.leftControllerValid : !rightValid)
            return false;
        Halo4ControllerWorldPoseInput input = frame.common;
        memcpy(input.controllerOrientation,
            left ? g_halo4RigTracking.leftControllerOrientation
                 : (oneHandRight ? g_halo4RigTracking.oneHandRightAimOrientation
                                 : g_halo4RigTracking.rightAimOrientation),
            sizeof(input.controllerOrientation));
        memcpy(input.controllerPosition,
            left ? g_halo4RigTracking.leftControllerPosition : g_halo4RigTracking.rightAimPosition,
            sizeof(input.controllerPosition));
        input.forwardTrim = left ? frame.leftForwardTrim : frame.rightForwardTrim;

        Halo4ControllerWorldPose controller{};
        if (!Halo4BuildControllerWorldPose(input, controller))
            return false;
        if (!left && (frame.visualPitchDeg != 0.0f || frame.visualYawDeg != 0.0f || frame.visualRollDeg != 0.0f))
        {
            // The accepted floating-palette path does not use the older
            // Halo4PlaceFirstPersonHands trim. Apply visual alignment here,
            // before the authored hand attachment, on this frozen pair only.
            float mount[9], trimmed[9];
            BasisFromAngles(-frame.visualYawDeg * 0.0174533f, frame.visualPitchDeg * 0.0174533f,
                -frame.visualRollDeg * 0.0174533f, mount);
            MultiplyBases(controller.basis, mount, trimmed);
            for (float value : trimmed)
                if (!isfinite(value))
                    return false;
            memcpy(controller.basis, trimmed, sizeof(trimmed));
        }
        // These are Blender hand-control bases, not Halo wrist rotations.  The
        // exported attachment positions are expressed in those control-local
        // axes, so retain the bases only to preserve the working physical
        // placement.  The reroot never uses them as the visible wrist orientation.
        const float attachmentParentRight[4] = {-0.583606601f, 0.000000213f, -0.698711872f, 0.413769424f};
        const float attachmentParentLeft[4] = {-0.265249819f, 0.000000008f, -0.317565471f, 0.910381734f};
        float attachmentParentBasis[9], attachmentWorldBasis[9];
        if (!Halo4QuaternionToBoneBasis(left ? attachmentParentLeft : attachmentParentRight, attachmentParentBasis))
            return false;
        MultiplyBases(controller.basis, attachmentParentBasis, attachmentWorldBasis);

        float targetPosition[3];
        memcpy(targetPosition, controller.position, sizeof(targetPosition));
        const float* attachment = left ? kHalo4LeftAttachmentMetres : kHalo4RightAttachmentMetres;
        for (int row = 0; row < 3; ++row)
        {
            float offset = 0.0f;
            for (int column = 0; column < 3; ++column)
                offset += attachmentWorldBasis[column * 3 + row] * attachment[column];
            targetPosition[row] += offset * input.worldScale;
        }
        // Both carriers start from their prepared raw bases. The
        // left hand no longer inherits the gun's mirrored presentation trim;
        // its exact state-specific rotational parent is selected once below.
        Halo4FloatingTransform carrier{};
        if (!Halo4BuildFloatingControllerCarrier(
                controller.basis, targetPosition, false, frame.gunYawDeg, frame.gunPitchDeg, frame.gunRollDeg, carrier))
            return false;
        if (!left)
        {
            const float rightOff = Clamp(g_config.gun_right_m, -0.3f, 0.3f) * input.worldScale;
            const float upOff = Clamp(g_config.gun_up_m, -0.3f, 0.3f) * input.worldScale;
            for (int axis = 0; axis < 3; ++axis)
                carrier.translation[axis] += carrier.rotation[3 + axis] * rightOff + carrier.rotation[6 + axis] * upOff;
            if (!Halo4FloatingTransformValid(carrier))
                return false;
        }
        target = carrier;
        return true;
    }

    bool Halo4FloatingPairMatchesCurrent()
    {
        return g_halo4FloatingPair.active && g_halo4RigTrackingActive &&
               g_halo4FloatingPair.epoch == g_halo4Camera.floatingHandsEpoch.load(std::memory_order_acquire) &&
               g_halo4FloatingPair.generation == g_halo4Camera.generation.load(std::memory_order_acquire) &&
               g_halo4FloatingPair.preparedSerial == g_halo4RigTracking.preparedSerial;
    }

#include "halo4_muzzle_ownership.inl"
    // Holsters: called where Halo 4 observes the held first-person model.
    void Halo4ObserveReserveWeapon(uint32_t generation, uint64_t heldIdentity)
    {
        if (!g_config.weapon_holsters || !generation)
            return;
        auto& state = g_reserveWeapon[3];
        if (!ReserveWeaponEnter(state, generation))
            return;
        uint32_t primary = reserve_weapon::kNoHandle, reserve = reserve_weapon::kNoHandle;
        bool read = false;
        __try
        {
            Halo4VehicleInputState seat{};
            read = Halo4ReadVehicleInput(seat) && !seat.seated && seat.unit != UINT32_MAX &&
                   Halo4ReadReserveWeapon(seat.unit, primary, reserve);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the Halo 4 reserve weapon faulted");
            read = false;
        }
        ReserveWeaponObserveLocked(state, generation, read, primary, reserve,
            weapon_model::Find(GameTitle::Halo4, heldIdentity) ? heldIdentity : 0);
        state.busy.clear(std::memory_order_release);
    }
#include "halo4_muzzle_publication.inl"
#include "halo4_muzzle_shots.inl"
#include "halo4_muzzle_lifecycle.inl"
#include "halo4_weapon_haptics.inl"

    void Halo4BeginFloatingPair()
    {
        g_halo4FloatingPair = Halo4FloatingPair{};
        g_halo4FloatingPair.active = true;
        g_halo4FloatingPair.handAlignment = g_halo4RigTracking.handAlignment;
        g_halo4FloatingPair.epoch = g_halo4Camera.floatingHandsEpoch.load(std::memory_order_acquire);
        g_halo4FloatingPair.generation = g_halo4Camera.generation.load(std::memory_order_acquire);
        g_halo4FloatingPair.preparedSerial = g_halo4RigTracking.preparedSerial;
        Halo4FloatingTargetFrame targetFrame{};
        const bool frameValid = Halo4FreezeFloatingTargetFrame(targetFrame);
        if (frameValid)
        {
            g_halo4FloatingPair.bodyWorldFrame = targetFrame.common;
            g_halo4FloatingPair.bodyWorldFrameValid = true;
        }
        g_halo4FloatingPair.reloadTrackingValid =
            frameValid && g_config.manual_reload &&
            Halo4BuildReloadTrackingTransform(targetFrame.common, g_halo4FloatingPair.reloadTrackingTransform);
        g_halo4FloatingPair.xrTrackingValid = frameValid && Halo4BuildReloadTrackingTransform(targetFrame.common,
                                                                g_halo4FloatingPair.xrTrackingTransform);
        if (frameValid)
        {
            g_halo4FloatingPair.worldScale = targetFrame.common.worldScale;
            g_halo4FloatingPair.twoHandAimActive = targetFrame.twoHandAimActive;
            // With the feature wired the visible attachment starts
            // unresolved (and therefore detached) and is frozen by this pair's
            // own owner evidence; it is never seeded from the aim-authority
            // flag. Feature off keeps the exact base default.
            g_halo4FloatingPair.supportGripAttached =
                VR_SupportGripWiredForTitle(GameTitle::Halo4) ? false : targetFrame.twoHandAimActive;
            g_halo4FloatingPair.gunYawDeg = targetFrame.gunYawDeg;
            g_halo4FloatingPair.gunPitchDeg = targetFrame.gunPitchDeg;
            g_halo4FloatingPair.gunRollDeg = targetFrame.gunRollDeg;
        }
        VrContactTrackingSnapshot contact{};
        if (frameValid && VR_GetContactTrackingSnapshot(contact) &&
            contact.serial == g_halo4FloatingPair.preparedSerial)
        {
            g_halo4FloatingPair.bodySupportFingerInput = contact.controllerFingers[0];
            g_halo4FloatingPair.bodyPairLeftHanded = contact.leftHanded;
            uint64_t settings = g_halo4FloatingPair.epoch + 1;
            settings = (settings ^ uint64_t(targetFrame.twoHandAimActive)) * 1099511628211ull;
            const float trims[]{targetFrame.gunYawDeg, targetFrame.gunPitchDeg, targetFrame.gunRollDeg,
                targetFrame.rightForwardTrim, targetFrame.leftForwardTrim, targetFrame.visualPitchDeg,
                targetFrame.visualYawDeg, targetFrame.visualRollDeg, targetFrame.common.verticalTrim,
                targetFrame.common.lateralTrim, g_config.gun_right_m, g_config.gun_up_m, g_config.gun_scale,
                g_config.left_hand_scale};
            for (float value : trims)
            {
                uint32_t bits = 0;
                memcpy(&bits, &value, sizeof(bits));
                settings = (settings ^ bits) * 1099511628211ull;
            }
            g_halo4FloatingPair.contactRawEpoch = contact.referenceEpoch;
            for (int hand = 0; hand < 2; ++hand)
            {
                auto physical = targetFrame.common;
                memcpy(physical.controllerOrientation, contact.hands[hand].orientation,
                    sizeof(physical.controllerOrientation));
                memcpy(physical.controllerPosition, contact.hands[hand].position, sizeof(physical.controllerPosition));
                // The exact owned handle and model shape arrive from the
                // admitted palette callback before this frame is queued.
                Halo4PreparePhysicalContactFrame(physical, contact.hands[hand].valid, contact.serial, contact.timeNs,
                    contact.referenceEpoch, g_halo4FloatingPair.generation, 0, 1, settings ? settings : 1,
                    g_halo4FloatingPair.contactFrames[hand]);
            }
        }
        g_halo4FloatingPair.rightTargetValid =
            frameValid && Halo4BuildFloatingWorldTarget(false, targetFrame, g_halo4FloatingPair.rightTargetWorld);
        g_halo4FloatingPair.leftTargetValid =
            frameValid && Halo4BuildFloatingWorldTarget(true, targetFrame, g_halo4FloatingPair.leftTargetWorld);
        // The owner-safe one-hand fallback carrier is frozen here, from the
        // same prepared sample and the same pair frame, before either eye can
        // reach the palette hook. It is built only for a wired title, so
        // PG off adds no carrier state at all.
        if (VR_SupportGripWiredForTitle(GameTitle::Halo4))
            g_halo4FloatingPair.oneHandRightTargetValid =
                frameValid &&
                Halo4BuildFloatingWorldTarget(false, targetFrame, g_halo4FloatingPair.oneHandRightTargetWorld, true);
        if (g_halo4FloatingPair.rightTargetValid && g_halo4FloatingPair.leftTargetValid)
        {
            Halo4FloatingTransform selectedLeft{};
            g_halo4FloatingPair.leftTargetValid =
                Halo4BuildFloatingLeftCarrierForState(g_halo4FloatingPair.twoHandAimActive,
                    g_halo4FloatingPair.leftTargetWorld, g_halo4FloatingPair.rightTargetWorld, selectedLeft);
            if (g_halo4FloatingPair.leftTargetValid)
                g_halo4FloatingPair.leftTargetWorld = selectedLeft;
        }
        if (g_halo4FloatingPair.rightTargetValid)
            memcpy(g_halo4FloatingPair.collisionRoot[1], g_halo4FloatingPair.rightTargetWorld.translation,
                sizeof(g_halo4FloatingPair.collisionRoot[1]));
        if (g_halo4FloatingPair.leftTargetValid)
            memcpy(g_halo4FloatingPair.collisionRoot[0], g_halo4FloatingPair.leftTargetWorld.translation,
                sizeof(g_halo4FloatingPair.collisionRoot[0]));
        // The final visible carriers are the only positions published to the
        // collision transaction. A fresh, same-generation correction changes
        // translation only; the authored hand/weapon volume is published after
        // the exact palettes are built below.
        if (g_halo4FloatingPair.rightTargetValid)
            Halo4ApplyWorldCollision(1, g_halo4FloatingPair.generation, g_halo4FloatingPair.worldScale,
                g_halo4FloatingPair.rightTargetWorld);
        if (g_halo4FloatingPair.leftTargetValid)
            Halo4ApplyWorldCollision(
                0, g_halo4FloatingPair.generation, g_halo4FloatingPair.worldScale, g_halo4FloatingPair.leftTargetWorld);
        g_halo4FloatingPair.targetsValid = g_halo4FloatingPair.rightTargetValid && g_halo4FloatingPair.leftTargetValid;

        // No previous pair is imported. Both current-eye motions begin empty
        // and can be minted only by this pair's exact Storm record.
        g_halo4FloatingPair.nextEyeRelation[0] = Halo4FloatingRelation{};
        g_halo4FloatingPair.nextEyeRelation[1] = Halo4FloatingRelation{};
    }

    void Halo4BeginFloatingEye(int eye)
    {
        if (eye < 0 || eye > 1 || !Halo4FloatingPairMatchesCurrent())
            return;
        g_halo4FloatingPair.eyePhase[eye] = Halo4FloatingRecordPhase::AwaitStormHands;
        g_halo4FloatingPair.nextEyeRelation[eye] = Halo4FloatingRelation{};
    }

    void Halo4EndFloatingPair()
    {
        // The held record normally publishes the combined right hand/gun.
        // A pair with hands but no held record still needs physical hand
        // samples. Keep this private to contact: world-collision publication
        // retains its accepted single hand+weapon ordering.
        const uint32_t count = g_halo4FloatingPair.handCollisionSampleCount[1];
        if (g_halo4FloatingPair.active && !g_halo4FloatingPair.rightContactPublished && count)
            Halo4PublishContactHand(1, g_halo4FloatingPair.handCollisionSamples[1], count,
                g_halo4FloatingPair.collisionIgnoredObjectIndex, uint64_t(count) ^ (uint64_t(count) << 16));
        // Same-eye motions are one-shot and never publish beyond this pair.
        g_halo4FloatingPair.nextEyeRelation[0] = Halo4FloatingRelation{};
        g_halo4FloatingPair.nextEyeRelation[1] = Halo4FloatingRelation{};
        g_halo4FloatingPair.active = false;
    }

    template <size_t N>
    bool Halo4CarryFloatingSubtree(BoneMatrix* nodes, int wrist, const int (&subtree)[N],
        const Halo4FloatingTransform& desiredWorld, Halo4FloatingTransform& appliedDeltaWorld)
    {
        Halo4FloatingTransform stockWorld{}, deltaWorld{};
        BoneMatrix delta{};
        if (!nodes || !Halo4ToFloatingTransform(nodes[wrist], stockWorld))
            return false;
        if (!Halo4BuildFloatingWorldDelta(desiredWorld, stockWorld, deltaWorld) ||
            !Halo4ToBoneMatrix(deltaWorld, delta) || !Halo4ApplySubtreeDelta(nodes, subtree, delta))
            return false;
        appliedDeltaWorld = deltaWorld;
        Halo4FloatingTransform verified{};
        return Halo4ToFloatingTransform(nodes[wrist], verified) &&
               Halo4FloatingDistance(verified, desiredWorld) < 0.01f;
    }

#include "halo4_render_model_identity.inl"
#include "halo4_vehicle_identity.inl"
#include "halo4_body_packet_backend.inl"

#include "halo4_runtime_weapon_bounds.inl"

    // Experimental H4 avatar transaction. The first-person producer finishes
    // writing each packet's region mask before returning; this wrapper then
    // edits only the exact local flag-0 storm_masterchief row. Weapons and the
    // existing tracked-hand palette remain independent and are never gated by
    // this optional feature.
    constexpr uint32_t kHalo4FirstPersonRecordCountLimit = 16;

    Halo4VrikStage Halo4BuildFloatingHandsPalette(
        const BoneMatrix* source, BoneMatrix* solved, int nodeCount, int32_t objectIndex, bool publish = true);

    bool Halo4BuildBodyIkCandidate(const Halo4FloatingTransform (&source)[kHalo4BodyIkNodeCount],
        const BoneMatrix* solvedHands, uint32_t nativeBodyMask, uint32_t nativeHandsMask,
        Halo4BodyIkPacketMutation& result)
    {
        if (!g_halo4FloatingPair.bodyWorldFrameValid || !g_halo4RigTracking.headPoseValid ||
            !g_halo4FloatingPair.leftTargetValid || !g_halo4FloatingPair.rightTargetValid)
            return false;
        Halo4BodyIkRequest request{};
        auto headInput = g_halo4FloatingPair.bodyWorldFrame;
        memcpy(headInput.controllerOrientation, g_halo4RigTracking.headOrientation,
            sizeof(headInput.controllerOrientation));
        memcpy(headInput.controllerPosition, g_halo4RigTracking.headPosition, sizeof(headInput.controllerPosition));
        headInput.forwardTrim = 0.0f;
        headInput.verticalTrim = 0.0f;
        headInput.lateralTrim = 0.0f;
        headInput.mirrored = false;
        Halo4ControllerWorldPose headWorld{};
        if (!Halo4BuildControllerWorldPose(headInput, headWorld))
            return false;
        request.headTargetValid = true;
        request.desiredWorldHead.scale = 1.0f;
        memcpy(request.desiredWorldHead.rotation, headWorld.basis, sizeof(headWorld.basis));
        memcpy(request.desiredWorldHead.translation, headWorld.position, sizeof(headWorld.position));
        if (!Halo4BodyHeadFromHmd(request.desiredWorldHead, request.desiredWorldHead))
            return false;
        // Native foot animation remains the floor reference; do not translate
        // both feet blindly with every headset movement. Hidden legs need no
        // additional solve and cannot veto the upper-body feature.
        if (!g_config.body_ik_hide_lower && !Halo4PrepareBodyFootTargets(source, request))
            return false;

        // The visible H4 controller carriers are already frozen in this same
        // world frame. Each elbow pole uses the source forearm direction,
        // projected by the bounded solver onto the shoulder-to-wrist plane.
        request.wristValid[0] = request.wristValid[1] = true;
        Halo4FloatingTransform firstPersonWrists[2]{};
        if (!solvedHands || !Halo4ToFloatingTransform(solvedHands[kHalo4LeftHandNode], firstPersonWrists[0]) ||
            !Halo4ToFloatingTransform(solvedHands[kHalo4RightHandNode], firstPersonWrists[1]) ||
            !Halo4BodyWristsFromFirstPerson(firstPersonWrists, request.desiredWorldWrist))
            return false;
        const uint32_t uppers[2]{kHalo4BodyIkLeftUpperArm, kHalo4BodyIkRightUpperArm};
        const uint32_t forearms[2]{kHalo4BodyIkLeftForearm, kHalo4BodyIkRightForearm};
        for (unsigned side = 0; side < 2; ++side)
            for (int axis = 0; axis < 3; ++axis)
                request.elbowPoleWorld[side][axis] =
                    source[forearms[side]].translation[axis] - source[uppers[side]].translation[axis];
        // Finger input is frozen from the same prepared display serial. Only
        // the unlatched support hand may flex; the primary and weapon-latched
        // support hand retain Halo's authored grip on the weapon.
        const unsigned supportAnatomical = g_halo4FloatingPair.handAlignment ? 1u : 0u;
        if (!g_halo4FloatingPair.supportGripAttached && !g_halo4FloatingPair.twoHandAimActive &&
            g_halo4FloatingPair.bodySupportFingerInput.valid)
        {
            request.fingerPoseAllowed[supportAnatomical] = true;
            request.fingerInput[supportAnatomical] = g_halo4FloatingPair.bodySupportFingerInput;
        }
        return Halo4BuildBodyIkPacketMutation(kHalo4BodyIkImportChecksum, kHalo4BodyIkNodeCount, source, request,
            nativeBodyMask, nativeHandsMask, g_config.body_ik_hide_lower, result);
    }

    // Hot producer-hook diagnostics are atomics only. The 50 ms worker emits
    // transitions; never perform logging or I/O in the native producer.
    constinit std::atomic<uint64_t> g_halo4BodyIkApplied{0};
    constinit std::atomic<uint64_t> g_halo4BodyIkRefused{0};
    constinit std::atomic<uint64_t> g_halo4BodyIkWriteFailures{0};
    constinit std::atomic<uint64_t> g_halo4BodyIkRollbackFailures{0};
    constinit std::atomic<uint64_t> g_halo4BodyIkSehFailures{0};

    void Halo4ApplyBodyIkToProducerRows(
        uint32_t userIndex, uint64_t unitObject, uint32_t maxRecords, void* outputRecords, uintptr_t emittedRecords)
    {
        if (!g_config.experimental_body_ik || !g_config.halo4_hands || userIndex != 0)
            return;
        if (!g_halo4Camera.armed.load(std::memory_order_acquire) ||
            g_halo4Camera.teardownRequested.load(std::memory_order_acquire) || !Halo4FloatingPairMatchesCurrent() ||
            !outputRecords)
        {
            g_halo4BodyIkRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        auto classify = [](uint16_t model, uint8_t fill, halo4_body_packet::RowKind& kind)
        {
            Halo4RenderModelIdentity identity{};
            if (!Halo4ResolveRenderModelIdentity(model, identity))
                return true;
            if (fill == kHalo4FirstPersonBodyFillFlag &&
                identity.nodeCount == static_cast<int>(kHalo4BodyIkNodeCount) &&
                identity.runtimeImportChecksum == kHalo4BodyIkImportChecksum)
                kind = halo4_body_packet::RowKind::Body;
            else if (fill == kHalo4FirstPersonWeaponFillFlag &&
                     identity.nodeCount == static_cast<int>(kHalo4StormFpBodyNodeCount) &&
                     identity.runtimeImportChecksum == kHalo4StormFpRuntimeImportChecksum)
                kind = halo4_body_packet::RowKind::Hands;
            return true;
        };
        auto admit = [&](uint64_t unit)
        {
            uint32_t ownerUnit = UINT32_MAX, ownerWeapon = UINT32_MAX, ownerDetail = 0;
            if (!DiagnosticHalo4WeaponOwner(
                    g_halo4FloatingPair.generation, UINT32_MAX, ownerUnit, ownerWeapon, ownerDetail) ||
                !(ownerDetail & 4u) || ownerUnit == UINT32_MAX || ownerUnit != unit ||
                (g_halo4FloatingPair.localUnitFrozen && g_halo4FloatingPair.localUnit != ownerUnit))
                return false;
            g_halo4FloatingPair.localUnit = ownerUnit;
            g_halo4FloatingPair.localUnitFrozen = true;
            if (VR_SupportGripWiredForTitle(GameTitle::Halo4) && !g_halo4FloatingPair.supportGripResolved)
            {
                const auto support = Halo4ReadSupportInvocation(UINT32_MAX);
                const auto resolved = halo4_owner_evidence::UpdatePairResolution(
                    halo4_owner_evidence::PairResolutionOf(false, g_halo4FloatingPair.supportGripAttached), true,
                    support.trusted, support.evidenceAbsent);
                if (resolved == halo4_owner_evidence::PairResolution::Unresolved)
                    return false;
                g_halo4FloatingPair.supportGripResolved = true;
                g_halo4FloatingPair.supportGripAttached = resolved == halo4_owner_evidence::PairResolution::Present;
                if (g_halo4FloatingPair.supportGripAttached)
                {
                    g_halo4FloatingPair.supportGripOwnerUnit = support.owner.unit;
                    g_halo4FloatingPair.supportGripOwnerWeapon = support.owner.weapon;
                }
            }
            return true;
        };
        auto solve = [&](const uint8_t* handsBytes, const uint8_t* bodyBytes, uint32_t bodyMask, uint32_t handsMask,
                         uint8_t* candidateBytes, uint32_t& candidateBodyMask, uint32_t& candidateHandsMask)
        {
            BoneMatrix solvedHands[kHalo4FirstPersonBankTransforms]{};
            BoneMatrix sourceHands[kHalo4FirstPersonBankTransforms]{};
            if (!Halo4SafeRead(handsBytes, sourceHands, sizeof(sourceHands)))
                return false;
            if (Halo4BuildFloatingHandsPalette(sourceHands, solvedHands, kHalo4StormFpBodyNodeCount,
                    int32_t(g_halo4FloatingPair.localUnit), false) != Halo4VrikStage::Solved)
                return false;
            Halo4FloatingTransform source[kHalo4BodyIkNodeCount]{};
            Halo4BodyIkPacketMutation solved{};
            static_assert(sizeof(source) == sizeof(Halo4FloatingTransform) * kHalo4BodyIkNodeCount);
            memcpy(source, bodyBytes, sizeof(source));
            if (!Halo4BuildBodyIkCandidate(source, solvedHands, bodyMask, handsMask, solved))
                return false;
            memcpy(candidateBytes, solved.body.transforms, sizeof(solved.body.transforms));
            candidateBodyMask = solved.bodyRegionMask;
            candidateHandsMask = solved.duplicateHandsRegionMask;
            return true;
        };
        auto stillCurrent = [&](uint64_t unit)
        {
            if (!Halo4FloatingPairMatchesCurrent() || g_halo4Camera.teardownRequested.load(std::memory_order_acquire) ||
                !g_halo4Camera.armed.load(std::memory_order_acquire) || !g_halo4FloatingPair.localUnitFrozen ||
                g_halo4FloatingPair.localUnit != unit)
                return false;
            uint32_t owner = UINT32_MAX, weapon = UINT32_MAX, detail = 0;
            return DiagnosticHalo4WeaponOwner(g_halo4FloatingPair.generation, UINT32_MAX, owner, weapon, detail) &&
                   (detail & 4u) && owner == unit;
        };
        auto read = [](const void* source, void* destination, size_t bytes)
        { return Halo4SafeRead(source, destination, bytes) != 0; };
        auto write = [](void* destination, const void* source, size_t bytes)
        { return Halo4SafeWrite(destination, source, bytes) != 0; };
        const auto result = halo4_body_packet::Apply<sizeof(Halo4FloatingTransform) * kHalo4BodyIkNodeCount>(userIndex,
            unitObject, maxRecords, outputRecords, emittedRecords, kHalo4FirstPersonRecordCountLimit,
            kHalo4FirstPersonRecordStride, kHalo4FirstPersonRecordModelIndexOffset,
            kHalo4FirstPersonRecordObjectIndexOffset, kHalo4FirstPersonRecordFillFlagOffset,
            kHalo4FirstPersonRecordRegionMaskOffset, kHalo4FirstPersonRecordMatrixOffset, read, write, classify, admit,
            solve, stillCurrent);
        if (result == halo4_body_packet::Result::Applied)
            g_halo4BodyIkApplied.fetch_add(1, std::memory_order_relaxed);
        else
            g_halo4BodyIkRefused.fetch_add(1, std::memory_order_relaxed);
        if (result == halo4_body_packet::Result::WriteFailure || result == halo4_body_packet::Result::RollbackFailure)
        {
            g_halo4BodyIkWriteFailures.fetch_add(1, std::memory_order_relaxed);
            if (result == halo4_body_packet::Result::RollbackFailure)
                g_halo4BodyIkRollbackFailures.fetch_add(1, std::memory_order_relaxed);
        }
    }

    void Halo4AfterFirstPersonProducer(void*, uint32_t userIndex, uint64_t unitObject, uint32_t maxRecords,
        void* outputRecords, uintptr_t emittedRecords)
    {
        __try
        {
            Halo4ApplyBodyIkToProducerRows(userIndex, unitObject, maxRecords, outputRecords, emittedRecords);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("applying Halo 4 body IK faulted");
            g_halo4BodyIkSehFailures.fetch_add(1, std::memory_order_relaxed);
            // Native output already exists; an optional avatar fault cannot
            // suppress that packet or escape into the working VR path.
        }
    }

    uintptr_t __fastcall Halo4FirstPersonProducerDetour(
        uint32_t userIndex, uint64_t unitObject, uint32_t maxRecords, void* outputRecords)
    {
        g_halo4Camera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        uintptr_t emitted = 0;
        __try
        {
            Halo4FirstPersonProducerFn original = g_halo4OrigFirstPersonProducer;
            emitted = halo4_body_packet::InvokeNativeOnceAndThen(
                original, Halo4AfterFirstPersonProducer, nullptr, userIndex, unitObject, maxRecords, outputRecords);
        }
        __finally
        {
            g_halo4Camera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
        return emitted;
    }

    // One active Storm-hands transaction. The first flag-1 record is the
    // exact 80-node storm_fp graph and is world-rooted by the current eye. Its
    // direct world delta is staged only after both hands and the final mask
    // validate, then consumed by the immediately following held model.
    constinit std::atomic<uint64_t> g_halo4FpFingerApplied{0}, g_halo4FpFingerRefused{0};
    void Halo4PoseFreeFirstPersonFingers(BoneMatrix* solved)
    {
        if (!VR_FingerPoseWanted() || g_halo4FloatingPair.supportGripAttached ||
            !g_halo4FloatingPair.bodySupportFingerInput.valid ||
            (VR_SupportGripWiredForTitle(GameTitle::Halo4) && !g_halo4FloatingPair.supportGripResolved))
            return;
        Halo4FloatingTransform source[80]{}, posed[80]{};
        BoneMatrix staged[80]{};
        bool valid = true;
        for (unsigned i = 0; i < 80 && valid; ++i)
            valid = Halo4ToFloatingTransform(solved[i], source[i]);
        valid = valid && Halo4PoseFreeFpFingers(source, kHalo4StormFpRuntimeImportChecksum, 80,
                             FingerAnatomicalSupportSide(g_halo4FloatingPair.handAlignment),
                             g_halo4FloatingPair.bodySupportFingerInput, posed);
        for (unsigned i = 0; i < 80 && valid; ++i)
            valid = Halo4ToBoneMatrix(posed[i], staged[i]);
        if (valid)
            std::memcpy(solved, staged, sizeof(staged));
        (valid ? g_halo4FpFingerApplied : g_halo4FpFingerRefused).fetch_add(1, std::memory_order_relaxed);
    }

    // Measurement, at most once a second from the left eye's published
    // pass: Halo 4's stock wrists relative to the current eye (the relation the hands
    // reroot through) and the game's aim seen from that eye. Head-independent
    // wrists would read the same with the head straight and turned; the reroot replaces
    // the relation with the head-straight value. Angles: heading and elevation of
    // the wrist's x axis and roll about it, in the eye frame (x forward, y left,
    // z up); quaternion x y z w (Halo4QuaternionToBlamBasis convention).
    void Halo4LogWristFromEye(const Halo4FloatingTransform& eyeRoot, const Halo4FloatingTransform& stockRight,
        const Halo4FloatingTransform& stockLeft) noexcept
    {
        static uint64_t lastMs = 0;
        const uint64_t now = GetTickCount64();
        if (now - lastMs < 1000)
            return;
        float right[9]{}, left[9]{}, rightQ[4]{}, leftQ[4]{}, eye[9]{};
        if (!Halo4EyeLocalWristBasis(eyeRoot.rotation, stockRight.rotation, right) ||
            !Halo4EyeLocalWristBasis(eyeRoot.rotation, stockLeft.rotation, left) ||
            !Halo4BlamBasisToQuaternion(right, rightQ) || !Halo4BlamBasisToQuaternion(left, leftQ) ||
            !Halo4NormalizeFloatingBasis(eyeRoot.rotation, eye))
            return;
        lastMs = now;
        constexpr float deg = 57.2957795f;
        const auto heading = [&](const float* m) { return atan2f(m[1], m[0]) * deg; };
        const auto elevation = [&](const float* m) { return asinf(Clamp(m[2], -1.0f, 1.0f)) * deg; };
        const auto roll = [&](const float* m) { return atan2f(m[5], m[8]) * deg; };
        float aim[3]{}, aimFromEye[3]{};
        for (int axis = 0; axis < 3; ++axis)
            aim[axis] = g_halo4Camera.engineAimForward[axis].load(std::memory_order_relaxed);
        for (int column = 0; column < 3; ++column)
            aimFromEye[column] = eye[column * 3] * aim[0] + eye[column * 3 + 1] * aim[1] + eye[column * 3 + 2] * aim[2];
        LOG("Halo4 wrist from eye: right ypr %+.2f %+.2f %+.2f q %.5f %.5f %.5f %.5f; "
            "left ypr %+.2f %+.2f %+.2f q %.5f %.5f %.5f %.5f; game aim from eye yaw %+.2f pitch %+.2f deg",
            heading(right), elevation(right), roll(right), rightQ[0], rightQ[1], rightQ[2], rightQ[3], heading(left),
            elevation(left), roll(left), leftQ[0], leftQ[1], leftQ[2], leftQ[3],
            atan2f(aimFromEye[1], aimFromEye[0]) * deg, asinf(Clamp(aimFromEye[2], -1.0f, 1.0f)) * deg);
    }

    Halo4VrikStage Halo4BuildFloatingHandsPalette(
        const BoneMatrix* source, BoneMatrix* solved, int nodeCount, int32_t objectIndex, bool publish)
    {
        if (!source || !solved || nodeCount != kHalo4StormFpBodyNodeCount || !Halo4FloatingPairMatchesCurrent())
            return Halo4VrikStage::CountRefused;
        if (!g_halo4FloatingPair.rightTargetValid)
            return Halo4VrikStage::RightPoseFailed;
        if (!g_halo4FloatingPair.leftTargetValid)
            return Halo4VrikStage::LeftPoseFailed;
        if (!Halo4SafeRead(source, solved, sizeof(BoneMatrix) * kHalo4FirstPersonBankTransforms))
            return Halo4VrikStage::CopyFailed;

        const int eye = g_stereoEye.load(std::memory_order_acquire);
        BoneMatrix eyeRootMatrix{};
        Halo4FloatingTransform eyeRoot{}, stockRight{}, stockLeft{};
        if (eye < 0 || eye > 1 || !Halo4LoadActiveEyeRoot(1.0f, eyeRootMatrix) ||
            !Halo4ToFloatingTransform(eyeRootMatrix, eyeRoot) ||
            !Halo4ToFloatingTransform(solved[kHalo4RightHandNode], stockRight) ||
            !Halo4ToFloatingTransform(solved[kHalo4LeftHandNode], stockLeft))
            return Halo4VrikStage::AnchorFailed;
        const Halo4VrikStage stormStage = Halo4ClassifyStormArms(solved, eyeRootMatrix, publish);
        if (stormStage != Halo4VrikStage::Solved)
            return stormStage;
        if (publish && eye == 0)
            Halo4LogWristFromEye(eyeRoot, stockRight, stockLeft);

        Halo4FloatingTransform desiredRight{}, desiredLeft{};
        Halo4FloatingTransform supportRightTarget = g_halo4FloatingPair.rightTargetWorld;
        if (VR_SupportGripWiredForTitle(GameTitle::Halo4) && !g_halo4FloatingPair.supportGripAttached &&
            g_halo4FloatingPair.oneHandRightTargetValid &&
            support_grip::AimSupportDerived(g_halo4FloatingPair.rightTargetValid, g_halo4FloatingPair.twoHandAimActive))
        {
            // Owner-untrusted/detached invocation whose prepared aim is
            // support-derived: the weapon carrier must not ride the two-hand
            // solve of an owner this invocation could not prove. The pair's
            // frozen ordinary one-hand carrier is the owner-safe target. A
            // coherently disengaged frame (the prepared aim is not
            // support-derived) keeps the base carrier unchanged.
            supportRightTarget = g_halo4FloatingPair.oneHandRightTargetWorld;
        }
        if (!Halo4BuildFloatingControllerRerootTarget(supportRightTarget, eyeRoot, stockRight, desiredRight))
            return Halo4VrikStage::RightPoseFailed;
        bool markerParityApplied = false;
        bool rigidSupportLockApplied = false;
        bool markerParityFallback = false;
        if (g_halo4FloatingPair.supportGripAttached)
        {
            // Match Halo 3's accepted support lock. The left controller still
            // owns the two-hand aim solve, but the visible hand takes the same
            // rigid world delta as the right hand and held model so it cannot
            // slide away from Halo 4's authored weapon grip.
            if (!Halo4BuildFloatingRigidSupportTarget(desiredRight, stockRight, stockLeft, desiredLeft))
                return Halo4VrikStage::LeftPoseFailed;
            rigidSupportLockApplied = true;
        }
        else
        {
            if (!Halo4BuildFloatingControllerRerootTarget(
                    g_halo4FloatingPair.leftTargetWorld, eyeRoot, stockLeft, desiredLeft))
                return Halo4VrikStage::LeftPoseFailed;
            // Reach's official left_hand marker is identity on its wrist.
            // Halo 4's marker is this H4EK quaternion on identity child node54.
            // Align the marker frames; never equate the wrist bone axes again.
            const float halo4LeftHandMarkerQ[4] = {-0.706223f, 0.701140f, -0.0353406f, 0.0916652f};
            float markerLocalBasis[9]{};
            Halo4FloatingTransform mountedCarrier{}, markerTarget{};
            if (Halo4QuaternionToBoneBasis(halo4LeftHandMarkerQ, markerLocalBasis) &&
                Halo4BuildFloatingControllerCarrier(g_halo4FloatingPair.leftTargetWorld.rotation,
                    g_halo4FloatingPair.leftTargetWorld.translation, true, g_halo4FloatingPair.gunYawDeg,
                    g_halo4FloatingPair.gunPitchDeg, g_halo4FloatingPair.gunRollDeg, mountedCarrier) &&
                Halo4BuildFloatingFreeLeftMarkerParityTarget(
                    mountedCarrier, markerLocalBasis, desiredLeft, markerTarget))
            {
                desiredLeft = markerTarget;
                markerParityApplied = true;
            }
            else
            {
                // Keep the already-valid raw-controller reroot.
                // Support/right/gun remain fully independent.
                markerParityFallback = true;
            }
        }
        // The gun retains the primary role's original authored delta even
        // when the anatomical right hand becomes the support hand.
        Halo4FloatingTransform primaryWeaponDelta{};
        if (!Halo4BuildFloatingWorldDelta(desiredRight, stockRight, primaryWeaponDelta))
            return Halo4VrikStage::RightPoseFailed;
        if (g_halo4FloatingPair.handAlignment)
        {
            AnatomicalPalmMarkers palms{};
            if (!Halo4AnatomicalPalmMarkers(palms) ||
                !RouteLeftHandedPalmTargets(palms.right, palms.left, desiredRight, desiredLeft))
                return Halo4VrikStage::LeftPoseFailed;
        }
        const float rightDistance = Halo4FloatingDistance(stockRight, desiredRight);
        const float leftDistance = Halo4FloatingDistance(stockLeft, desiredLeft);
        const float worldScale = g_halo4FloatingPair.worldScale;
        if (!Halo4FloatingWristDeltaPlausible(rightDistance, worldScale) ||
            !Halo4FloatingWristDeltaPlausible(leftDistance, worldScale))
            return Halo4VrikStage::RangeFailed;

        Halo4FloatingTransform rightDeltaWorld{}, leftDeltaWorld{};
        bool controllerArmsSolved = false;
        // Keep the optional arm solve atomic. If either side refuses, restore
        // the exact source palette before applying the established rigid hand
        // carry so no partial shoulder/elbow mutation reaches the game.
        BoneMatrix rightHandDelta{}, leftHandDelta{};
        BoneMatrix rightArmTarget{}, leftArmTarget{};
        const bool armTargetsValid =
            Halo4ToBoneMatrix(desiredRight, rightArmTarget) && Halo4ToBoneMatrix(desiredLeft, leftArmTarget);
        BoneMatrix bodyRoot{};
        const bool bodyRootValid = Halo4BuildControllerBodyRoot(bodyRoot);
        Halo4VrikStage armFallbackFailure = Halo4VrikStage::RightIkFailed;
        const auto armTransaction = Halo4RunArmPaletteTransaction(
            Halo4ShouldSolveVisibleArms(!g_config.floating_hands, g_config.arm_ik, bodyRootValid, armTargetsValid),
            [&]()
            {
                return Halo4SolveStormArm(solved, false, rightArmTarget, bodyRoot, &rightHandDelta) &&
                       Halo4ToFloatingTransform(rightHandDelta, rightDeltaWorld);
            },
            [&]()
            {
                return Halo4SolveStormArm(solved, true, leftArmTarget, bodyRoot, &leftHandDelta) &&
                       Halo4ToFloatingTransform(leftHandDelta, leftDeltaWorld);
            },
            [&]() { return Halo4SafeRead(source, solved, sizeof(BoneMatrix) * kHalo4FirstPersonBankTransforms); },
            [&]()
            {
                if (!Halo4CarryFloatingSubtree(
                        solved, kHalo4RightHandNode, kHalo4RightHandSubtree, desiredRight, rightDeltaWorld))
                {
                    armFallbackFailure = Halo4VrikStage::RightIkFailed;
                    return false;
                }
                if (!Halo4CarryFloatingSubtree(
                        solved, kHalo4LeftHandNode, kHalo4LeftHandSubtree, desiredLeft, leftDeltaWorld))
                {
                    armFallbackFailure = Halo4VrikStage::LeftIkFailed;
                    return false;
                }
                return true;
            });
        controllerArmsSolved = armTransaction == Halo4ArmTransactionResult::Committed;
        if (armTransaction == Halo4ArmTransactionResult::RightSolveFailed ||
            armTransaction == Halo4ArmTransactionResult::LeftSolveFailed)
        {
            const auto failedStage = armTransaction == Halo4ArmTransactionResult::LeftSolveFailed
                                         ? Halo4VrikStage::LeftIkFailed
                                         : Halo4VrikStage::RightIkFailed;
            g_halo4Camera.vrikStageRefusals[static_cast<size_t>(failedStage)].fetch_add(1, std::memory_order_relaxed);
        }
        if (armTransaction == Halo4ArmTransactionResult::RollbackFailed)
            return Halo4VrikStage::CopyFailed;
        if (armTransaction == Halo4ArmTransactionResult::HandFallbackFailed)
            return armFallbackFailure;
        if (armTransaction == Halo4ArmTransactionResult::NotRequested)
        {
            if (!Halo4CarryFloatingSubtree(
                    solved, kHalo4RightHandNode, kHalo4RightHandSubtree, desiredRight, rightDeltaWorld))
                return Halo4VrikStage::RightIkFailed;
            if (!Halo4CarryFloatingSubtree(
                    solved, kHalo4LeftHandNode, kHalo4LeftHandSubtree, desiredLeft, leftDeltaWorld))
                return Halo4VrikStage::LeftIkFailed;
        }

        // Visibility is last. A failed optional solve falls back to the
        // established floating-hands view, preserving hands and held weapon.
        BoneMatrix collapsedRight = solved[kHalo4RightHandNode];
        BoneMatrix collapsedLeft = solved[kHalo4LeftHandNode];
        collapsedRight.scale = 0.0001f;
        collapsedLeft.scale = 0.0001f;
        for (int node = 0; node < kHalo4StormFpBodyNodeCount; ++node)
        {
            const auto role = Halo4ClassifyFloatingNode(node);
            switch (role)
            {
            case Halo4FloatingNodeRole::RightHand:
            case Halo4FloatingNodeRole::LeftHand:
                break;
            case Halo4FloatingNodeRole::CollapseAtRightWrist:
            case Halo4FloatingNodeRole::CollapseAtLeftWrist:
            case Halo4FloatingNodeRole::Hidden:
                if (Halo4ShouldCollapseFloatingNode(role, !controllerArmsSolved))
                {
                    if (role == Halo4FloatingNodeRole::CollapseAtRightWrist)
                        solved[node] = collapsedRight;
                    else if (role == Halo4FloatingNodeRole::CollapseAtLeftWrist)
                        solved[node] = collapsedLeft;
                    else
                        solved[node].scale = 0.0001f;
                }
                break;
            case Halo4FloatingNodeRole::OutsideBody:
                return Halo4VrikStage::RangeFailed;
            }
        }
        for (int node = 0; node < kHalo4StormFpBodyNodeCount; ++node)
        {
            Halo4FloatingTransform checked{};
            if (!Halo4ToFloatingTransform(solved[node], checked))
                return Halo4VrikStage::RangeFailed;
        }

        if (!publish)
        {
            Halo4ApplyVisualHandOffsets(
                solved, g_halo4FloatingPair.worldScale, g_config.left_handed, g_halo4FloatingPair.handAlignment);
            return Halo4VrikStage::Solved;
        }
        Halo4PoseFreeFirstPersonFingers(solved);
        g_halo4Camera.vrikTargetMiss.store(rightDistance, std::memory_order_relaxed);
        const uintptr_t sourceAddress = reinterpret_cast<uintptr_t>(source);
        const uintptr_t heldSource = Halo4ExpectedHeldRecordSource(sourceAddress);
        if (!heldSource)
            return Halo4VrikStage::RangeFailed;
        // Count only palettes that reached the final commit boundary. This
        // makes the logged state split directly comparable with committed
        // Storm palettes instead of counting attempts later refused elsewhere.
        if (markerParityApplied)
            g_halo4Camera.vrikMarkerParityApplications.fetch_add(1, std::memory_order_relaxed);
        else if (rigidSupportLockApplied)
            g_halo4Camera.vrikTwoHandRigidSupportLocks.fetch_add(1, std::memory_order_relaxed);
        else if (markerParityFallback)
            g_halo4Camera.vrikMarkerParityFallbacks.fetch_add(1, std::memory_order_relaxed);
        Halo4FloatingRelation& staged = g_halo4FloatingPair.nextEyeRelation[eye];
        staged.valid = true;
        staged.epoch = g_halo4FloatingPair.epoch;
        staged.generation = g_halo4FloatingPair.generation;
        staged.preparedSerial = g_halo4FloatingPair.preparedSerial;
        // This relationship is always measured from the untouched source
        // wrist to the controller target. The arm IK's final wrist correction
        // alone is not the gun delta because shoulder/elbow motion preceded it.
        staged.rightHandDeltaWorld = primaryWeaponDelta;
        staged.expectedHeldSource = heldSource;
        Halo4PublishAuthoredHandCollisionVolumes(solved, objectIndex);
        Halo4ApplyVisualHandOffsets(
            solved, g_halo4FloatingPair.worldScale, g_config.left_handed, g_halo4FloatingPair.handAlignment);
        return Halo4VrikStage::Solved;
    }

    // The held record is world-rooted by the same current-eye root as the Storm
    // record immediately before it. Consume that exact same-frame hand delta
    // once and commit only after every resolved held-model node moves.
    bool Halo4CarryFloatingWeaponRecord(const BoneMatrix* source, BoneMatrix* moved, int nodeCount,
        uint32_t runtimeImportChecksum, uint16_t renderModelIndex, uintptr_t renderModelDescriptor)
    {
        if (!source || !moved || nodeCount <= 0 || nodeCount > kHalo4FirstPersonBankTransforms ||
            !Halo4FloatingPairMatchesCurrent())
            return false;
        const int eye = g_stereoEye.load(std::memory_order_acquire);
        if (eye < 0 || eye > 1)
            return false;
        Halo4FloatingRelation& current = g_halo4FloatingPair.nextEyeRelation[eye];
        if (!current.valid || current.epoch != g_halo4FloatingPair.epoch ||
            current.generation != g_halo4FloatingPair.generation ||
            current.preparedSerial != g_halo4FloatingPair.preparedSerial)
            return false;
        if (current.expectedHeldSource != reinterpret_cast<uintptr_t>(source))
        {
            current = Halo4FloatingRelation{};
            return false;
        }

        // One-shot even on failure: no later record may inherit a delta meant
        // for this exact held model.
        const Halo4FloatingTransform deltaWorld = current.rightHandDeltaWorld;
        current = Halo4FloatingRelation{};
        BoneMatrix deltaMatrix{};
        if (!Halo4ToBoneMatrix(deltaWorld, deltaMatrix) ||
            !Halo4SafeRead(source, moved, sizeof(BoneMatrix) * kHalo4FirstPersonBankTransforms))
            return false;

        for (int node = 0; node < nodeCount; ++node)
        {
            BoneMatrix result{};
            if (!ComposeBoneMatrices(deltaMatrix, moved[node], result))
                return false;
            moved[node] = result;
        }
        if (g_halo4FloatingPair.reloadTrackingValid)
        {
            const auto* model = weapon_model::Find(GameTitle::Halo4, runtimeImportChecksum);
            float basis[9]{}, point[3]{};
            if (NormalizedBasis(moved[0], basis) &&
                weapon_model::ReceiverPoint(model, moved[0].scale, basis, moved[0].translation, point))
                VR_PublishReloadTarget(GameTitle::Halo4, g_halo4FloatingPair.generation, runtimeImportChecksum,
                    g_halo4FloatingPair.preparedSerial, g_halo4FloatingPair.reloadTrackingTransform, point);
        }
        Halo4WeaponCollisionBounds runtimeBounds{};
        const bool needsBounds = g_halo4WorldCollision.configured.load(std::memory_order_acquire) ||
                                 (Halo4ContactMeleeReady() && g_config.physical_melee);
        const bool readBounds =
            needsBounds && Halo4ReadRuntimeWeaponBounds(renderModelDescriptor, runtimeImportChecksum, runtimeBounds);
        if (needsBounds)
            (readBounds ? g_halo4RuntimeWeaponBoundsReads : g_halo4RuntimeWeaponBoundsRefusals)
                .fetch_add(1, std::memory_order_relaxed);
        const auto* selectedBounds =
            readBounds ? &runtimeBounds : Halo4FindWeaponCollisionBounds(runtimeImportChecksum);
        const uint32_t shapeId =
            selectedBounds ? Halo4RuntimeWeaponShapeId(renderModelIndex, *selectedBounds) : runtimeImportChecksum;
        Halo4PublishAuthoredWeaponCollisionVolume(
            moved, nodeCount, runtimeImportChecksum, readBounds ? &runtimeBounds : nullptr, shapeId);
        return true;
    }

    __declspec(noinline) void __fastcall Halo4ModelSkinningDetour(int32_t objectIndex, uint16_t renderModelIndex,
        const BoneMatrix* inputObjectNodeMatrices, const void* nodeMap, bool flagA, bool flagB,
        int32_t totalNodeMatrixCount, void* skinning)
    {
        g_halo4Camera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        const BoneMatrix* selected = inputObjectNodeMatrices;
        Halo4MuzzlePending muzzlePending{};
        __try
        {
            if (g_halo4ScopeRendering && reinterpret_cast<uintptr_t>(_ReturnAddress()) ==
                                             g_halo4Camera.base + kHalo4FirstPersonSkinningReturnRva)
            {
                if (Halo4SafeRead(inputObjectNodeMatrices, g_halo4ScopeHidden, sizeof(g_halo4ScopeHidden)))
                {
                    for (auto& matrix : g_halo4ScopeHidden)
                        matrix.scale = .0001f;
                    selected = g_halo4ScopeHidden;
                }
                else
                    g_halo4ScopePixelsValid = false;
                g_halo4OrigModelSkinning(
                    objectIndex, renderModelIndex, selected, nodeMap, flagA, flagB, totalNodeMatrixCount, skinning);
                return;
            }
            // Persistent support grip: Halo 4's production owner-evidence
            // producer and the pair's ONE owner-trust decision (F04/A003).
            //
            // The safe context is this first-person model-skinning invocation
            // on the game render thread, gated by the same conditions the
            // diagnostic commit uses (armed, no teardown, the proven FP return
            // address). It is deliberately independent of the optional hands
            // presentation: with the feature on the durable relationship must
            // still be fed while `halo4_hands` is off, or Halo 4's ordinary
            // two-hand authority could never bind.
            //
            // The read is the SEMANTIC PRIMARY (`candidate = UINT32_MAX`),
            // never the record's own object index: the storm-hands record's
            // object index is the local unit handle. This block runs at the
            // pair's first safe FP invocation -- by the engine's per-eye
            // record order (storm hands, held weapon, native body) that is the
            // storm-hands decision boundary -- before any palette can consume
            // the decision. A provisional Unknown publishes its explicit
            // Unknown record and leaves the pair open (later records,
            // including the second eye's, may still freeze it); Present and
            // Absent are terminal. Once resolved, no later record re-reads or
            // re-defines the decision.
            // Freeze a semantic local-unit handle from the proven owner
            // reader at the first exact Storm-hands record in this stereo
            // pair. This is an observation only; later flag-0 body packets
            // remain byte-for-byte stock. Requiring the official Storm
            // checksum/count and FP producer agreement prevents a generic
            // model or stale TLS owner from establishing the reference.
            if (!g_halo4FloatingPair.localUnitFrozen &&
                !g_halo4Camera.teardownRequested.load(std::memory_order_acquire) &&
                g_halo4Camera.armed.load(std::memory_order_acquire) && Halo4FloatingPairMatchesCurrent() &&
                reinterpret_cast<uintptr_t>(_ReturnAddress()) ==
                    g_halo4Camera.base + kHalo4FirstPersonSkinningReturnRva &&
                Halo4DiagnosticPeekFillFlag(inputObjectNodeMatrices) == kHalo4FirstPersonWeaponFillFlag)
            {
                Halo4RenderModelIdentity ownerModel{};
                uint32_t ownerUnit = UINT32_MAX, ownerWeapon = UINT32_MAX;
                uint32_t ownerDetail = 0;
                const bool exactStorm = Halo4ResolveRenderModelIdentity(renderModelIndex, ownerModel) &&
                                        ownerModel.nodeCount == kHalo4StormFpBodyNodeCount &&
                                        ownerModel.runtimeImportChecksum == kHalo4StormFpRuntimeImportChecksum;
                if (exactStorm &&
                    DiagnosticHalo4WeaponOwner(TitleAdapter_GetGeneration(GameTitle::Halo4), UINT32_MAX, ownerUnit,
                        ownerWeapon, ownerDetail) &&
                    (ownerDetail & 0x4u) != 0 && ownerUnit != UINT32_MAX)
                {
                    g_halo4FloatingPair.localUnit = ownerUnit;
                    g_halo4FloatingPair.localUnitFrozen = true;
                }
            }
            if (VR_SupportGripWiredForTitle(GameTitle::Halo4) && !g_halo4FloatingPair.supportGripResolved &&
                !g_halo4Camera.teardownRequested.load(std::memory_order_acquire) &&
                g_halo4Camera.armed.load(std::memory_order_acquire) && Halo4FloatingPairMatchesCurrent() &&
                reinterpret_cast<uintptr_t>(_ReturnAddress()) ==
                    g_halo4Camera.base + kHalo4FirstPersonSkinningReturnRva)
            {
                const uint32_t supportBoundaryCandidate = halo4_owner_evidence::PairRecordWeaponCandidate(
                    halo4_owner_evidence::PairRecordAction::StormHandsBoundary, uint32_t(objectIndex));
                const SupportInvocationResolution supportResolution =
                    Halo4ReadSupportInvocation(supportBoundaryCandidate);
                const halo4_owner_evidence::PairResolution pairResolution = halo4_owner_evidence::UpdatePairResolution(
                    halo4_owner_evidence::PairResolutionOf(
                        g_halo4FloatingPair.supportGripResolved, g_halo4FloatingPair.supportGripAttached),
                    true, supportResolution.trusted, supportResolution.evidenceAbsent);
                if (pairResolution == halo4_owner_evidence::PairResolution::Present)
                {
                    // Authoritative: this invocation proved the durable
                    // relationship's exact current owner, so the visible
                    // support attachment is engaged for the whole pair.
                    g_halo4FloatingPair.supportGripAttached = true;
                    g_halo4FloatingPair.supportGripResolved = true;
                    g_halo4FloatingPair.supportGripOwnerUnit = supportResolution.owner.unit;
                    g_halo4FloatingPair.supportGripOwnerWeapon = supportResolution.owner.weapon;
                    static std::atomic<uint32_t> s_supportPairPresentGeneration{0};
                    const uint32_t supportPairGeneration = g_halo4Camera.generation.load(std::memory_order_acquire);
                    if (supportPairGeneration && s_supportPairPresentGeneration.exchange(supportPairGeneration,
                                                     std::memory_order_relaxed) != supportPairGeneration)
                        support_diagnostics::Record(support_diagnostics::Event::H4PairPresent);
                }
                else if (pairResolution == halo4_owner_evidence::PairResolution::Absent)
                {
                    // Authoritative absence: the pair stays detached for its
                    // remaining records and the visible hand is free.
                    g_halo4FloatingPair.supportGripAttached = false;
                    g_halo4FloatingPair.supportGripResolved = true;
                    static std::atomic<uint32_t> s_supportPairAbsentGeneration{0};
                    const uint32_t supportPairGeneration = g_halo4Camera.generation.load(std::memory_order_acquire);
                    if (supportPairGeneration && s_supportPairAbsentGeneration.exchange(supportPairGeneration,
                                                     std::memory_order_relaxed) != supportPairGeneration)
                        support_diagnostics::Record(support_diagnostics::Event::H4PairAbsent);
                }
                // Provisional Unknown wrote nothing here: the shared resolver
                // already published the explicit Unknown record, so a stale
                // KnownPresent cannot survive, and the pair stays open.
            }
            // Weapon-order diagnostic: first-person thread/frame marker AND
            // stable weapon commit for the engine's FP weapon record.
            // Deliberately independent of the optional hands presentation so
            // a capture is never silently empty; read-only, no gameplay
            // effect. The commit requires the same ungated validation the FP
            // commit contract uses (owned semantic primary plus FP producer
            // agreement); if it cannot be proven, no commit is invented and
            // the analyser reports an uncommitted FP invocation instead.
            if (Telemetry_WeaponEventsAccepting() && !g_halo4Camera.teardownRequested.load(std::memory_order_acquire) &&
                g_halo4Camera.armed.load(std::memory_order_acquire) &&
                reinterpret_cast<uintptr_t>(_ReturnAddress()) ==
                    g_halo4Camera.base + kHalo4FirstPersonSkinningReturnRva &&
                Halo4DiagnosticPeekFillFlag(inputObjectNodeMatrices) == kHalo4FirstPersonWeaponFillFlag)
            {
                Game_DiagnosticNoteFpThread(GameTitle::Halo4);
                const uint64_t diagnosticSerial =
                    Halo4FloatingPairMatchesCurrent() ? g_halo4FloatingPair.preparedSerial : 0;
                Telemetry_PublishWeaponEvent(static_cast<uint8_t>(WeaponOrderEventKind::FpEntry),
                    static_cast<uint8_t>(WeaponOrderEventStatus::NoObservation), static_cast<uint8_t>(GameTitle::Halo4),
                    g_halo4Camera.generation.load(std::memory_order_acquire), diagnosticSerial, UINT32_MAX, UINT32_MAX,
                    0, 0);
                uint32_t commitUnit = UINT32_MAX;
                uint32_t commitWeapon = UINT32_MAX;
                uint32_t commitDetail = 0;
                if (DiagnosticHalo4WeaponOwner(TitleAdapter_GetGeneration(GameTitle::Halo4), uint32_t(objectIndex),
                        commitUnit, commitWeapon, commitDetail) &&
                    (commitDetail & 0x4u) != 0)
                    Telemetry_PublishWeaponEvent(static_cast<uint8_t>(WeaponOrderEventKind::FpWeaponCommit),
                        static_cast<uint8_t>(WeaponOrderEventStatus::Success), static_cast<uint8_t>(GameTitle::Halo4),
                        g_halo4Camera.generation.load(std::memory_order_acquire), diagnosticSerial, commitUnit,
                        commitWeapon, 0, commitDetail);
            }
            if (!g_halo4Camera.teardownRequested.load(std::memory_order_acquire) &&
                g_halo4Camera.armed.load(std::memory_order_acquire) &&
                // `arm_ik` is NO LONGER A GATE. There is no arm IK in
                // this path any more - the hands are placed rigidly - so
                // requiring an arm-IK switch to be on before VR hands appear is
                // backwards. It also silently cost a whole headset session: a
                // session log showed the rig running normally at arm_ik=1, then
                // the setting going to 0 and every window afterwards reading
                // `0 solved / 0 first-person calls`, with the hook doing
                // nothing at all while the build looked installed and armed.
                g_config.halo4_hands &&
                reinterpret_cast<uintptr_t>(_ReturnAddress()) ==
                    g_halo4Camera.base + kHalo4FirstPersonSkinningReturnRva)
            {
                // An earlier gate required a second animation TLS record and
                // argument 7 equalling 85. Measurement proves argument 7 is the
                // per-render-model output count, and the TLS record is not
                // evidence about WHICH record this callback is carrying - it
                // may not even be reachable on this thread. Both are gone. The
                // record is classified from the matrices themselves.
                g_halo4Camera.vrikExactReturnHits.fetch_add(1, std::memory_order_relaxed);
                Halo4RecordSkinningCount(totalNodeMatrixCount);
                const int32_t fillFlag = Halo4RecordFillFlag(inputObjectNodeMatrices);
                Halo4RenderModelIdentity identity{};
                const bool identityValid = Halo4ResolveRenderModelIdentity(renderModelIndex, identity);
                if (!identityValid)
                    g_halo4Camera.vrikNodeCountResolveFailures.fetch_add(1, std::memory_order_relaxed);
                else if (fillFlag == kHalo4FirstPersonBodyFillFlag)
                {
                    g_halo4Camera.vrikLastBodyNodeCount.store(identity.nodeCount, std::memory_order_relaxed);
                    g_halo4Camera.vrikLastBodyChecksum.store(identity.runtimeImportChecksum, std::memory_order_relaxed);
                    const bool exactBody =
                        Halo4IsStormMasterchiefBodyIdentity(identity.runtimeImportChecksum, identity.nodeCount);
                    if (exactBody)
                    {
                        g_halo4Camera.vrikBodyIdentityExact.fetch_add(1, std::memory_order_relaxed);
                        const bool sameFrozenPair =
                            Halo4FloatingPairMatchesCurrent() && g_halo4FloatingPair.localUnitFrozen &&
                            g_halo4FloatingPair.generation == TitleAdapter_GetGeneration(GameTitle::Halo4);
                        if (sameFrozenPair)
                        {
                            if (Halo4BodyBelongsToFrozenLocalUnit(
                                    true, static_cast<uint32_t>(objectIndex), true, g_halo4FloatingPair.localUnit))
                                g_halo4Camera.vrikBodyLocalUnitMatches.fetch_add(1, std::memory_order_relaxed);
                            else
                                g_halo4Camera.vrikBodyLocalUnitMismatches.fetch_add(1, std::memory_order_relaxed);
                        }
                    }
                }
                else if (fillFlag == kHalo4FirstPersonWeaponFillFlag)
                    g_halo4Camera.vrikLastWeaponNodeCount.store(identity.nodeCount, std::memory_order_relaxed);

                const int eye = g_stereoEye.load(std::memory_order_acquire);
                const bool sequenceCurrent = identityValid && eye >= 0 && eye < 2 && Halo4FloatingPairMatchesCurrent();
                Halo4FloatingRecordDecision decision{};
                if (sequenceCurrent)
                {
                    Halo4FloatingRecordPhase& phase = g_halo4FloatingPair.eyePhase[eye];
                    if (phase == Halo4FloatingRecordPhase::AwaitStormHands && fillFlag == 1 &&
                        identity.nodeCount != kHalo4StormFpBodyNodeCount)
                        g_unmappedFpRigs.Note(fp_rig_coverage::PackRig(
                            GameTitle::Halo4, identity.nodeCount, identity.runtimeImportChecksum));
                    decision = Halo4SelectFloatingRecord(phase, fillFlag, identity.nodeCount);
                    phase = decision.next;
                }
                else if (eye >= 0 && eye < 2 && g_halo4FloatingPair.active)
                {
                    g_halo4FloatingPair.eyePhase[eye] = fillFlag == kHalo4FirstPersonBodyFillFlag
                                                            ? Halo4FloatingRecordPhase::AwaitStormHands
                                                            : Halo4FloatingRecordPhase::AwaitSequenceBoundary;
                    g_halo4FloatingPair.nextEyeRelation[eye] = Halo4FloatingRelation{};
                }

                if (decision.action == Halo4FloatingRecordAction::BuildStormHands)
                {
                    g_halo4Camera.vrikStormRecordCandidates.fetch_add(1, std::memory_order_relaxed);
                    g_halo4Camera.vrikLastStormChecksum.store(
                        identity.runtimeImportChecksum, std::memory_order_relaxed);
                    const Halo4VrikStage stage = Halo4BuildFloatingHandsPalette(
                        inputObjectNodeMatrices, g_halo4VrikScratch, identity.nodeCount, objectIndex);
                    if (stage == Halo4VrikStage::Solved)
                    {
                        selected = g_halo4VrikScratch;
                        g_halo4Camera.vrikSolvedPalettes.fetch_add(1, std::memory_order_relaxed);
                    }
                    else
                    {
                        g_halo4FloatingPair.eyePhase[eye] = Halo4FloatingRecordPhase::AwaitSequenceBoundary;
                        g_halo4FloatingPair.nextEyeRelation[eye] = Halo4FloatingRelation{};
                        g_halo4Camera.vrikStageRefusals[static_cast<size_t>(stage)].fetch_add(
                            1, std::memory_order_relaxed);
                        g_halo4Camera.vrikAlignmentRefusals.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                else if (decision.action == Halo4FloatingRecordAction::CarryHeldModel)
                {
                    // The held-weapon record validates its own exact
                    // object index against the pair's frozen semantic primary.
                    // It never re-resolves the pair and never re-defines the
                    // hand solve that already ran: an untrusted or outdated
                    // pair stays detached for its remaining records.
                    if (VR_SupportGripWiredForTitle(GameTitle::Halo4) && sequenceCurrent &&
                        g_halo4FloatingPair.supportGripResolved && g_halo4FloatingPair.supportGripAttached &&
                        !halo4_owner_evidence::PairRecordValidatesFrozenPrimary(
                            halo4_owner_evidence::PairRecordAction::HeldWeapon, uint32_t(objectIndex), true,
                            g_halo4FloatingPair.supportGripOwnerWeapon))
                    {
                        static std::atomic<uint32_t> s_supportHeldMismatchGeneration{0};
                        const uint32_t supportPairGeneration = g_halo4Camera.generation.load(std::memory_order_acquire);
                        if (supportPairGeneration && s_supportHeldMismatchGeneration.exchange(supportPairGeneration,
                                                         std::memory_order_relaxed) != supportPairGeneration)
                            support_diagnostics::Record(support_diagnostics::Event::H4HeldMismatch);
                    }
                    muzzlePending = Halo4PrepareMuzzlePalette(uint32_t(objectIndex), renderModelIndex,
                        identity.runtimeImportChecksum, identity.nodeCount, nullptr);
                    g_halo4Camera.vrikLastHeldChecksum.store(identity.runtimeImportChecksum, std::memory_order_relaxed);
                    g_halo4Camera.vrikLastHeldNodeCount.store(identity.nodeCount, std::memory_order_relaxed);
                    if (Halo4CarryFloatingWeaponRecord(inputObjectNodeMatrices, g_halo4VrikScratch, identity.nodeCount,
                            identity.runtimeImportChecksum, renderModelIndex, identity.descriptor))
                    {
                        selected = g_halo4VrikScratch;
                        muzzlePending = Halo4PrepareMuzzlePalette(uint32_t(objectIndex), renderModelIndex,
                            identity.runtimeImportChecksum, identity.nodeCount, selected);
                        VR_ObserveWeaponModel(GameTitle::Halo4,
                            g_halo4Camera.generation.load(std::memory_order_acquire),
                            weapon_model::LiveIdentity(identity.runtimeImportChecksum, renderModelIndex));
                        Halo4ObserveReserveWeapon(
                            TitleAdapter_GetGeneration(GameTitle::Halo4), identity.runtimeImportChecksum);
                        g_halo4Camera.vrikWeaponRecordsCarried.fetch_add(1, std::memory_order_relaxed);
                    }
                    else
                    {
                        g_halo4Camera.vrikWeaponRecordsRefused.fetch_add(1, std::memory_order_relaxed);
                    }
                }
                else
                {
                    // Flag 0 is the independently submitted 120-node native
                    // body/legs model. It closes the sequence and remains
                    // byte-for-byte stock. Unreadable, unresolved, unexpected,
                    // or out-of-order records also stay stock.
                    if (eye >= 0 && eye < 2 && fillFlag == kHalo4FirstPersonBodyFillFlag)
                    {
                        g_halo4FloatingPair.nextEyeRelation[eye] = Halo4FloatingRelation{};
                    }
                    g_halo4Camera.vrikStockPalettes.fetch_add(1, std::memory_order_relaxed);
                }
            }
            else
                g_halo4Camera.vrikStockPalettes.fetch_add(1, std::memory_order_relaxed);
            Halo4ModelSkinningFn original = g_halo4OrigModelSkinning;
            if (original)
            {
                original(
                    objectIndex, renderModelIndex, selected, nodeMap, flagA, flagB, totalNodeMatrixCount, skinning);
                Halo4CommitMuzzlePalette(muzzlePending);
            }
        }
        __finally
        {
            g_halo4Camera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    // Put Halo 4's own first-person weapon assembly on the controller.
    //
    // Order of operations is deliberate and is what makes this safe to ship on
    // an unproven layout: READ the engine's stock node first, prove it really
    // is {quaternion, translation, scale}, publish what was read, and only then
    // write. A build that cannot prove the format places nothing and says so -
    // it never writes a guess into an engine bone array.
    void Halo4PlaceFirstPersonHands()
    {
        if (!g_config.halo4_hands)
            return;
        Halo4FirstPersonAccess access;
        if (!Halo4ResolveFirstPerson(0, access))
        {
            g_halo4Camera.handsBlockedFrames.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        g_halo4Camera.lastWeaponCount.store(access.weapons, std::memory_order_relaxed);

        float controllerQuaternion[4], controllerPosition[3];
        float headQuaternion[4], headPosition[3];
        if (!VR_GetAimPose(controllerQuaternion, controllerPosition) || !VR_GetHeadPose(headQuaternion, headPosition))
        {
            g_halo4Camera.handsBlockedFrames.fetch_add(1, std::memory_order_relaxed);
            return;
        }

        Halo4HandPlacementInput placement{};
        for (int axis = 0; axis < 3; ++axis)
        {
            placement.controllerOffset[axis] = controllerPosition[axis] - headPosition[axis];
        }
        memcpy(placement.controllerOrientation, controllerQuaternion, sizeof(placement.controllerOrientation));
        // Mesh-only barrel trim (per title profile): rotate the hands/gun
        // rig about the controller in its OWN frame (OpenXR: +X right,
        // +Y up, -Z forward). The aim ray and crosshair never read this.
        if (g_config.barrel_pitch_deg != 0.0f || g_config.barrel_yaw_deg != 0.0f || g_config.barrel_roll_deg != 0.0f)
        {
            constexpr float kHalfRadians = 0.5f * 0.0174533f;
            const float hp = g_config.barrel_pitch_deg * kHalfRadians;
            const float hy = g_config.barrel_yaw_deg * kHalfRadians;
            const float hr = g_config.barrel_roll_deg * kHalfRadians;
            const float qPitch[4] = {sinf(hp), 0.0f, 0.0f, cosf(hp)}; // about +X
            const float qYaw[4] = {0.0f, sinf(hy), 0.0f, cosf(hy)};   // about +Y
            const float qRoll[4] = {0.0f, 0.0f, -sinf(hr), cosf(hr)}; // about -Z (forward)
            float qPY[4], qLocal[4], trimmed[4];
            Halo4MultiplyQuaternion(qYaw, qPitch, qPY);
            Halo4MultiplyQuaternion(qPY, qRoll, qLocal);
            Halo4MultiplyQuaternion(placement.controllerOrientation, qLocal, trimmed);
            bool finite = true;
            for (int j = 0; j < 4; ++j)
                if (!isfinite(trimmed[j]))
                {
                    finite = false;
                    break;
                }
            if (finite)
                memcpy(placement.controllerOrientation, trimmed, sizeof(trimmed));
        }
        placement.worldScale = g_worldScale.load(std::memory_order_relaxed);
        placement.forwardTrim = g_config.halo4_hand_forward_m;
        placement.verticalTrim = g_config.halo4_hand_vertical_m;
        placement.lateralTrim = g_config.halo4_hand_lateral_m;
        placement.mirrored = g_config.halo4_hands_mirrored;

        bool placedAny = false;
        for (int weapon = 0; weapon < static_cast<int>(kHalo4FirstPersonMaxWeapons); ++weapon)
        {
            if (!access.nodes[weapon] || access.nodeCount[weapon] <= 0)
                continue;
            // The root is node 0 of the live bank; the rest of the assembly
            // hangs off it, so this moves the whole rig.
            unsigned char* node =
                access.nodes[weapon] + static_cast<size_t>(kHalo4FirstPersonRootNode) * kHalo4FirstPersonNodeStride;

            Halo4FirstPersonNode stock{};
            if (!Halo4SafeRead(node, &stock, sizeof(stock)))
                continue;

            // Publish what the ENGINE holds, every time, before deciding
            // anything. A clean log that only ever showed our own values would
            // prove nothing (clean-diagnostic-means-wrong-mechanism).
            const float quaternionLength =
                sqrtf(stock.rotation[0] * stock.rotation[0] + stock.rotation[1] * stock.rotation[1] +
                      stock.rotation[2] * stock.rotation[2] + stock.rotation[3] * stock.rotation[3]);
            g_halo4Camera.stockNodeQuaternionLength.store(quaternionLength, std::memory_order_relaxed);
            g_halo4Camera.stockNodeScale.store(stock.scale, std::memory_order_relaxed);
            for (int axis = 0; axis < 3; ++axis)
            {
                g_halo4Camera.stockNodeTranslation[axis].store(stock.translation[axis], std::memory_order_relaxed);
            }
            g_halo4Camera.lastRootNode.store(access.nodeCount[weapon], std::memory_order_relaxed);

            if (!Halo4FirstPersonNodeLooksValid(stock))
            {
                // The 0x20 stride is proven; this shape is not. Refuse loudly
                // and permanently for this generation rather than writing a
                // transform the engine may read as something else entirely.
                g_halo4Camera.nodeFormatRejected.store(true, std::memory_order_release);
                g_halo4Camera.handsBlockedFrames.fetch_add(1, std::memory_order_relaxed);
                return;
            }
            g_halo4Camera.nodeFormatProven.store(true, std::memory_order_release);

            // The rigid transform the whole assembly gets. `placed` carries the
            // controller's rotation and the offset in engine units; applying it
            // to EVERY node moves the gun and arms as one body, so nothing here
            // depends on which node is the root.
            Halo4FirstPersonNode placed{};
            if (!Halo4BuildHandNode(placement, stock, placed))
                continue;

            const int count = access.nodeCount[weapon];
            int written = 0;
            for (int index = 0; index < count; ++index)
            {
                unsigned char* target = access.nodes[weapon] + static_cast<size_t>(index) * kHalo4FirstPersonNodeStride;
                Halo4FirstPersonNode current{};
                if (!Halo4SafeRead(target, &current, sizeof(current)))
                    continue;
                Halo4FirstPersonNode moved{};
                if (!Halo4TransformAssemblyNode(current, placed.rotation, placed.translation, moved))
                {
                    continue;
                }
                if (Halo4SafeWrite(target, &moved, sizeof(moved)))
                    ++written;
            }
            if (written == 0)
                continue;

            // Read the root straight back. This reports what the ENGINE holds
            // after the write, not that we issued one - the only form of
            // confirmation worth logging.
            Halo4FirstPersonNode readback{};
            if (Halo4SafeRead(node, &readback, sizeof(readback)))
            {
                float delta = 0.0f;
                for (int axis = 0; axis < 3; ++axis)
                {
                    const float d = readback.translation[axis] - stock.translation[axis];
                    delta += d * d;
                }
                g_halo4Camera.lastWriteSurvived.store(sqrtf(delta) > 1.0e-4f, std::memory_order_relaxed);
                for (int axis = 0; axis < 3; ++axis)
                {
                    g_halo4Camera.lastHandTranslation[axis].store(
                        readback.translation[axis], std::memory_order_relaxed);
                }
            }
            g_halo4Camera.lastNodesWritten.store(written, std::memory_order_relaxed);
            placedAny = true;
        }
        (placedAny ? g_halo4Camera.handsPlacedFrames : g_halo4Camera.handsBlockedFrames)
            .fetch_add(1, std::memory_order_relaxed);
    }
} // namespace
namespace halo4_bridge
{
    // Motion aim on top of that: the hand steers, so it additionally
    // needs VR aim itself (Insert) and a captured heading reference. Turning
    // VR aim off drops cleanly back to pitch ownership only - stick yaw, headset pitch.
    bool Halo4ControllerAimActive()
    {
        return Halo4LookPitchOwned() && g_vrAim.load(std::memory_order_acquire) &&
               g_halo4Camera.gameYawReferenceValid.load(std::memory_order_acquire);
    }
} // namespace halo4_bridge
namespace
{
// Halo 4's ApplyVrTurn. Rotating the yaw reference turns the
// head-locked view instantly, and the hand-steered aim follows because its
// target is expressed against that same reference - the exact relationship
// Halo 3 relies on. Halo 4 needs its own copy rather than sharing Halo 3's
// because its reference lives in the per-level camera core, but the snap
// latch, the smooth rate and the config keys are all shared.
#include "halo4_vr_turn.inl"
} // namespace
namespace halo4_bridge
{
    // The yaw reference pair and the engine's own aim direction, read together
    // so the aim loop cannot mix generations. Mirrors ReachReadYawReferencePair
    // / ReachReadAimFeedback.
    bool Halo4ReadAimReferences(float& gameYaw, float& headYaw, float aimForward[3])
    {
        if (!g_halo4Camera.gameYawReferenceValid.load(std::memory_order_acquire) ||
            !g_halo4Camera.headReferenceValid.load(std::memory_order_acquire) ||
            !g_halo4Camera.enginePitchValid.load(std::memory_order_acquire))
        {
            return false;
        }
        gameYaw = g_halo4Camera.gameYawReference.load(std::memory_order_relaxed);
        headYaw = g_halo4Camera.headYawReference.load(std::memory_order_relaxed);
        for (int axis = 0; axis < 3; ++axis)
        {
            aimForward[axis] = g_halo4Camera.engineAimForward[axis].load(std::memory_order_relaxed);
        }
        const float lengthSquared =
            aimForward[0] * aimForward[0] + aimForward[1] * aimForward[1] + aimForward[2] * aimForward[2];
        return std::isfinite(gameYaw) && std::isfinite(headYaw) && std::isfinite(lengthSquared) &&
               fabsf(lengthSquared - 1.0f) < 0.05f;
    }
} // namespace halo4_bridge
namespace
{
    // Why a frame rendered stock before ownership or was dropped after a
    // claimed eye transaction began. Reported by the worker only.
    enum class Halo4StereoRejection : uint32_t
    {
        None = 0,
        NotArmed,
        ForeignCaller,
        SetupArgsMissing,
        SplitScreenWindow,
        ObserverUnreadable,
        CameraInvalid,
        PreparedFrameUnavailable,
        EyeBuildFailed,
        CameraClaimMismatch,
        ProjectionInvalid,
        EyeCaptureMissing,
        RestoreFailed,
        TransactionException,
        SceneTargetMissing,
    };

    enum class Halo4StereoResult : uint8_t
    {
        NotStarted,
        Complete,
        StartedFrameDropped,
    };

    // Setup's own arguments, captured at the one proven call site so the eye
    // loop can re-invoke the producer without re-deriving the observer's TLS
    // chain. Thread-local: the render thread is the only claimant.
    struct Halo4SetupArgs
    {
        bool valid = false;
        uintptr_t view = 0;
        uint32_t window = 0;
        uint32_t count = 0;
        uint32_t mode = 0;
        uint32_t user = 0;
        uintptr_t observer = 0;
    };
    constinit thread_local Halo4SetupArgs g_halo4SetupArgs;

    int Halo4SafeRead(const void* source, void* destination, size_t bytes)
    {
        __try
        {
            memcpy(destination, source, bytes);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("copying from Halo 4 memory faulted");
            return 0;
        }
        return 1;
    }

    int Halo4SafeWrite(void* destination, const void* source, size_t bytes)
    {
        __try
        {
            memcpy(destination, source, bytes);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("copying into Halo 4 memory faulted");
            return 0;
        }
        return 1;
    }

    // H4EK's cinematic_scripting_set_user_input_constraints writer proves this
    // title's live authored-shot block: cinematic globals live at TLS+0xC8;
    // the camera is TLS+0x58, type 6, with current limits at +0xC8..+0xD4,
    // rates at +0xD8..+0xE4, and interpolation ticks at +0xF0.  This reader
    // consumes only the engine-TLS index already uniquely resolved by the
    // camera-core install proof.  A bad read deliberately produces Unknown;
    // the compositor then stays immersive when the publication goes stale.
    constexpr uint32_t kHalo4CinematicTlsGlobalsOffset = 0xC8;
    constexpr uint32_t kHalo4CinematicTlsCameraOffset = 0x58;
    constexpr uint32_t kHalo4CinematicCameraTypeOffset = 0x02;
    constexpr uint32_t kHalo4CinematicLookLimitsOffset = 0xC8;
    constexpr uint32_t kHalo4CinematicLookRatesOffset = 0xD8;
    constexpr uint32_t kHalo4CinematicLookTicksOffset = 0xF0;
    constexpr uint32_t kHalo4CinematicTlsSlotLimit = 256;
    constexpr uint64_t kHalo4CinematicPublishPeriodMs = 250;

    CinematicControlState ReadHalo4CinematicControl() noexcept
    {
        if (!g_halo4Camera.cinematicTheaterProof.load(std::memory_order_acquire) || !g_halo4EngineTlsIndex ||
            !g_halo4Camera.base ||
            reinterpret_cast<uintptr_t>(g_halo4EngineTlsIndex) != g_halo4Camera.base + kHalo4EngineTlsIndexRva)
        {
            return CinematicControlState::Unknown;
        }

        uint32_t slot = 0;
        if (!Halo4SafeRead(g_halo4EngineTlsIndex, &slot, sizeof(slot)) || slot >= kHalo4CinematicTlsSlotLimit)
        {
            return CinematicControlState::Unknown;
        }

        auto** const slots = reinterpret_cast<uint8_t**>(__readgsqword(0x58));
        uintptr_t tls = 0;
        if (!slots || !Halo4SafeRead(slots + slot, &tls, sizeof(tls)) || !tls)
            return CinematicControlState::Unknown;
        uintptr_t globals = 0;
        if (!Halo4SafeRead(
                reinterpret_cast<const void*>(tls + kHalo4CinematicTlsGlobalsOffset), &globals, sizeof(globals)) ||
            !globals)
        {
            return CinematicControlState::Unknown;
        }

        uint8_t cinematicInProgress = 0;
        if (!Halo4SafeRead(
                reinterpret_cast<const void*>(globals + 5), &cinematicInProgress, sizeof(cinematicInProgress)))
        {
            return CinematicControlState::Unknown;
        }
        if (!cinematicInProgress)
        {
            return ClassifyHalo4CinematicControl(true, false, false, 0, nullptr, nullptr, 0);
        }

        uintptr_t camera = 0;
        uint16_t cameraType = 0;
        if (!Halo4SafeRead(
                reinterpret_cast<const void*>(tls + kHalo4CinematicTlsCameraOffset), &camera, sizeof(camera)) ||
            !camera ||
            !Halo4SafeRead(reinterpret_cast<const void*>(camera + kHalo4CinematicCameraTypeOffset), &cameraType,
                sizeof(cameraType)) ||
            cameraType != kHalo4CinematicCameraType)
        {
            return CinematicControlState::Unknown;
        }

        float limits[4]{};
        if (!Halo4SafeRead(
                reinterpret_cast<const void*>(camera + kHalo4CinematicLookLimitsOffset), limits, sizeof(limits)))
        {
            return CinematicControlState::Unknown;
        }
        const CinematicControlState currentLimits =
            ClassifyHalo4CinematicControl(true, true, true, cameraType, limits, nullptr, 0);
        if (currentLimits == CinematicControlState::PlayerControlled)
            return currentLimits;

        int32_t ticks = 0;
        if (!Halo4SafeRead(
                reinterpret_cast<const void*>(camera + kHalo4CinematicLookTicksOffset), &ticks, sizeof(ticks)) ||
            ticks < 0 || ticks > kHalo4CinematicMaximumTicks)
        {
            return CinematicControlState::Unknown;
        }
        if (ticks == 0)
            return currentLimits;

        float rates[4]{};
        if (!Halo4SafeRead(
                reinterpret_cast<const void*>(camera + kHalo4CinematicLookRatesOffset), rates, sizeof(rates)))
        {
            return CinematicControlState::Unknown;
        }
        return ClassifyHalo4CinematicControl(true, true, true, cameraType, limits, rates, ticks);
    }

    void PublishHalo4CinematicTheater() noexcept
    {
        const uint64_t now = GetTickCount64();
        const uint64_t next = g_halo4Camera.cinematicNextPublishMs.load(std::memory_order_relaxed);
        if (now < next)
            return;
        g_halo4Camera.cinematicNextPublishMs.store(now + kHalo4CinematicPublishPeriodMs, std::memory_order_relaxed);

        const uint32_t generation = g_halo4Camera.generation.load(std::memory_order_acquire);
        if (!generation || g_halo4Camera.teardownRequested.load(std::memory_order_acquire) ||
            TitleAdapter_GetGeneration(GameTitle::Halo4) != generation)
        {
            return;
        }

        const CinematicControlState control = ReadHalo4CinematicControl();
        if (control == CinematicControlState::Unknown)
            return;

        if (!TitleAdapter_PublishCinematicControl(GameTitle::Halo4, generation, control, now))
        {
            return;
        }

        // Publish the control verdict independently of the
        // optional projection sample.  Aspect can be transiently unavailable
        // while a scene target is being learned; withholding PlayerControlled
        // in that window would leave a previous AuthoredLocked sample live
        // until the shared stale timeout and could hold theatre across a real
        // return to gameplay.
        float authoredAspect = 0.0f;
        if (!VR_GetGameRenderAspect(authoredAspect))
            return;
        (void)TitleAdapter_PublishCutsceneTheaterProjection(GameTitle::Halo4, generation, authoredAspect, now);
    }

    // ---- native CUI HUD layout -----------------------------------------
    // H4EK's one ui\hud_globals tag owns a 3x3 screen-transform basis. This is
    // a separate fail-open feature from the camera and authored reticle hooks:
    // an absent or ambiguous payload leaves only HUD layout stock.
    constinit struct Halo4HudLayoutRuntime
    {
        std::atomic<uintptr_t> basis{0};
        std::atomic<uint32_t> generation{0};
        std::atomic<int> matchCount{-1}; // -1 idle, -2 scanning, 1 proven
        std::atomic<bool> scanInFlight{false};
        std::atomic<uint32_t> scanAttempts{0};
        std::atomic<uint64_t> lastAttemptMs{0};
        std::atomic<uint64_t> lastVerifyMs{0};
        std::atomic<uint32_t> appliedHorizontalBits{0};
        std::atomic<uint32_t> appliedVerticalBits{0};
        std::atomic<uint32_t> appliedCurvatureBits{0};
        std::atomic<uint32_t> appliedOffsetBits{0};
        uintptr_t rememberedBasis = 0;
        std::array<Halo4HudBasisPoint, 9> rememberedWritten{};
        bool rememberedWrittenValid = false;
    } g_halo4HudLayout;
    constinit SRWLOCK g_halo4HudLayoutLock = SRWLOCK_INIT;

    bool Halo4HudWriteBasis(uintptr_t address, const Halo4HudBasisPoint* basis);

    bool Halo4HudWriteBasis(uintptr_t address, const Halo4HudBasisPoint* basis)
    {
        MEMORY_BASIC_INFORMATION memory{};
        if (VirtualQuery(reinterpret_cast<void*>(address), &memory, sizeof(memory)) != sizeof(memory))
            return false;
        // A writable mapped view may still be file-backed. Force copy-on-write
        // before the first store so no H4 map/tag file can ever be modified.
        if (memory.Type != MEM_MAPPED && Halo4SafeWrite(reinterpret_cast<void*>(address), basis, kHalo4HudBasisBytes))
            return true;
        const DWORD wanted = memory.Type == MEM_MAPPED ? PAGE_WRITECOPY : PAGE_READWRITE;
        DWORD previous = 0;
        if (!VirtualProtect(reinterpret_cast<void*>(address), kHalo4HudBasisBytes, wanted, &previous))
            return false;
        const bool wrote = Halo4SafeWrite(reinterpret_cast<void*>(address), basis, kHalo4HudBasisBytes) != 0;
        DWORD restored = 0;
        VirtualProtect(reinterpret_cast<void*>(address), kHalo4HudBasisBytes, previous, &restored);
        return wrote;
    }

    struct Halo4CuiCommandHeader
    {
        int16_t command = 0;
        uint16_t payloadSize = 0;
    };

    struct Halo4CuiReticleEyeScope
    {
        bool active = false;
        bool gameplayPassActive = false;
        bool redirectActive = false;
        bool captureAuthored = false;
        bool captureReplay = false;
        bool captureReplayAttempted = false;
        bool captureReplayRedirected = false;
        Halo4CuiCaptureSelectionState captureSelection{};
        int eye = -1;
        uint32_t generation = 0;
        uint32_t depth = 0;
        uint64_t preparedSerial = 0;
        void* renderer = nullptr;
    };
    constinit thread_local Halo4CuiReticleEyeScope g_halo4CuiReticleEyeScope;
    // The dispatcher normally runs synchronously beneath the hooked CUI front
    // end. Its outer callback already pins both trampolines, so avoid a locked
    // atomic increment for every command in the stream. A dispatcher reached
    // without that scope (including a partially-enabled install) still takes
    // its own callback reference below.
    constinit thread_local uint32_t g_halo4CuiFrontendCallbackDepth = 0;

    void Halo4ApplyNativeHudAffine(void* renderer)
    {
        if (!renderer || !g_halo4Restoration.hudInstalled.load(std::memory_order_acquire) ||
            !Halo4NativeHudAdmitsCuiRoot(g_halo4CuiFrontendCallbackDepth, g_halo4CuiReticleEyeScope.captureReplay,
                VR_IsPausePresentationTarget()))
        {
            return;
        }
        int32_t count = 0;
        if (!Halo4SafeRead(static_cast<const uint8_t*>(renderer) + 0x870, &count, sizeof(count)) || count <= 0 ||
            count > 0x60)
            return;
        auto* const entry = static_cast<uint8_t*>(renderer) + 0x878 + static_cast<size_t>(count - 1) * 0x34;
        std::array<float, 13> transform{};
        if (!Halo4SafeRead(entry, transform.data(), 0x34))
            return;
        constexpr size_t kTouched[] = {1, 2, 4, 5, 7, 8, 10, 11};
        for (const size_t index : kTouched)
            if (!std::isfinite(transform[index]))
                return;
        Halo4HudAffine affine{};
        if (!Halo4ComputeNativeHudAffine(g_config.hud_size, g_config.hud_aspect, g_config.hud_vertical_offset, affine))
            return;
        for (const size_t index : {size_t{1}, size_t{4}, size_t{7}, size_t{10}})
            transform[index] *= affine.horizontal;
        for (const size_t index : {size_t{2}, size_t{5}, size_t{8}, size_t{11}})
            transform[index] *= affine.vertical;
        transform[11] -= affine.heightPixels;
        if (Halo4SafeWrite(entry, transform.data(), 0x34))
            g_halo4Restoration.hudTransforms.fetch_add(1, std::memory_order_relaxed);
    }

    void __fastcall Halo4HudRootDetour(void* renderer)
    {
        g_halo4Camera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            Halo4HudRootFn const original = g_halo4Restoration.hudOriginal;
            if (original)
                original(renderer);
            Halo4ApplyNativeHudAffine(renderer);
        }
        __finally
        {
            g_halo4Camera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    bool Halo4CuiReticleTransformLive()
    {
        // This live fact is queried from the compositor as well as from pinned
        // hook callbacks. Publish it only through atomics: the worker clears
        // trampoline/target pointers after the release-store below, and an
        // unpinned compositor read of those plain pointers would be a data
        // race. Hook bodies are protected separately by activeCallbacks.
        return g_halo4Camera.installed.load(std::memory_order_acquire) &&
               g_halo4Camera.cuiReticleInstalled.load(std::memory_order_acquire) &&
               !g_halo4Camera.teardownRequested.load(std::memory_order_acquire);
    }
} // namespace
namespace halo4_bridge
{
    bool Halo4CuiReticleNativeSuppressionLive()
    {
        return Halo4CuiReticleOwnsNativeSuppression(
            Halo4CuiReticleTransformLive(), VR_Halo4AuthoredReticleFeatureHealthy());
    }
} // namespace halo4_bridge
namespace
{
    bool Halo4OwnsCuiReticleEyeTransaction()
    {
        const Halo4CuiReticleEyeScope& scope = g_halo4CuiReticleEyeScope;
        const uint32_t generation = g_halo4Camera.generation.load(std::memory_order_acquire);
        return scope.active && scope.preparedSerial != 0 && generation != 0 && scope.generation == generation &&
               g_halo4RigTrackingActive && g_halo4RigTracking.preparedSerial == scope.preparedSerial &&
               TitleAdapter_GetActiveTitle() == GameTitle::Halo4 &&
               TitleAdapter_GetGeneration(GameTitle::Halo4) == generation &&
               g_halo4Camera.installed.load(std::memory_order_acquire) &&
               g_halo4Camera.armed.load(std::memory_order_acquire) &&
               !g_halo4Camera.teardownRequested.load(std::memory_order_acquire) &&
               g_enabled.load(std::memory_order_acquire) && VR_IsStereoEnabled() && scope.eye >= 0 && scope.eye <= 1 &&
               g_stereoEye.load(std::memory_order_relaxed) == scope.eye;
    }

    bool Halo4OwnsCuiReticleEyeScope()
    {
        return g_halo4CuiReticleEyeScope.gameplayPassActive && Halo4OwnsCuiReticleEyeTransaction();
    }

    bool Halo4EndCuiReticleRedirect(bool forced)
    {
        Halo4CuiReticleEyeScope& scope = g_halo4CuiReticleEyeScope;
        if (!scope.redirectActive)
            return true;

        const bool captured = scope.captureAuthored;
        const bool ended =
            captured ? VR_EndPreparedAuthoredReticleCapture() : VR_EndPreparedAuthoredReticleSuppression();
        scope.redirectActive = false;
        scope.captureAuthored = false;
        scope.captureSelection = {};
        scope.depth = 0;
        scope.renderer = nullptr;

        if (forced)
        {
            // An unmatched marker must not publish a partial subtree. The D3D
            // state was restored above on this same render thread; only the
            // optional art is invalidated and stereo ownership is untouched.
            if (captured)
                VR_InvalidatePreparedAuthoredReticleCapture();
            g_halo4Camera.cuiReticleForcedCleanup.fetch_add(1, std::memory_order_relaxed);
            return ended;
        }
        if (!ended)
        {
            g_halo4Camera.cuiReticleRedirectFailures.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        if (captured)
        {
            // H4EK proves payload+0 is a renderer transform ID, not a stable
            // weapon/art identity. A fixed nonzero sentinel is the truthful
            // contract: BoundedAnimation republishes the pixels on cadence,
            // while the existing coverage probe rejects blank/thin captures.
            g_authoredCrosshairKeyAccum = 1;
            g_authoredCrosshairKey.store(g_authoredCrosshairKeyAccum, std::memory_order_release);
            g_halo4Camera.cuiReticleCaptures.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            g_halo4Camera.cuiReticleSuppressions.fetch_add(1, std::memory_order_relaxed);
        }
        g_halo4Camera.cuiReticleCompleted.fetch_add(1, std::memory_order_relaxed);
        return true;
    }

    void Halo4FinishCuiReticleEye()
    {
        if (g_halo4CuiReticleEyeScope.redirectActive)
            (void)Halo4EndCuiReticleRedirect(true);
        g_halo4CuiReticleEyeScope = Halo4CuiReticleEyeScope{};
    }

    void Halo4BeginCuiReticleEye(int eye, uint64_t preparedSerial)
    {
        Halo4FinishCuiReticleEye();
        Halo4CuiReticleEyeScope& scope = g_halo4CuiReticleEyeScope;
        scope.active = eye >= 0 && eye <= 1 && preparedSerial != 0;
        scope.eye = eye;
        scope.preparedSerial = preparedSerial;
        scope.generation = g_halo4Camera.generation.load(std::memory_order_acquire);
        if (eye >= 0 && eye <= 1)
            g_halo4Camera.cuiReticleEyeSerial[eye].store(0, std::memory_order_release);
    }

    void Halo4ParityRecordCommand(bool headerReadable, const Halo4CuiCommandHeader& header)
    {
        if constexpr (!kEnableHalo4ParityTrace)
            return;
        if (!g_halo4CuiReticleEyeScope.gameplayPassActive)
            return;
        g_halo4Camera.parityCuiTotalCommands.fetch_add(1, std::memory_order_relaxed);
        if (!headerReadable)
        {
            g_halo4Camera.parityCuiUnreadableHeaders.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (Halo4ParityCommandFitsBucket(header.command))
            g_halo4Camera.parityCuiCommands[Halo4ParityCommandBucket(header.command)].fetch_add(
                1, std::memory_order_relaxed);
        else
            g_halo4Camera.parityCuiCommandsOutOfRange.fetch_add(1, std::memory_order_relaxed);
    }

    void Halo4ParityRecordTransform(
        int32_t transformId, uint32_t payloadSize, uint32_t stackCount, const BoneMatrix& transform, bool replay)
    {
        if constexpr (!kEnableHalo4ParityTrace)
            return;
        if (transformId == kHalo4ParityEmptyTransformId)
        {
            g_halo4Camera.parityTransformOverflow.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        int slot = -1;
        for (size_t i = 0; i < kHalo4ParityTransformSlotCount; ++i)
        {
            int32_t seen = g_halo4Camera.parityTransformId[i].load(std::memory_order_acquire);
            if (seen == transformId)
            {
                slot = static_cast<int>(i);
                break;
            }
            if (seen == kHalo4ParityEmptyTransformId)
            {
                int32_t expected = kHalo4ParityEmptyTransformId;
                if (g_halo4Camera.parityTransformId[i].compare_exchange_strong(
                        expected, transformId, std::memory_order_acq_rel, std::memory_order_acquire) ||
                    expected == transformId)
                {
                    slot = static_cast<int>(i);
                    break;
                }
            }
        }
        if (slot < 0)
        {
            g_halo4Camera.parityTransformOverflow.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        const size_t index = static_cast<size_t>(slot);
        g_halo4Camera.parityTransformPayload[index].store(payloadSize, std::memory_order_relaxed);
        g_halo4Camera.parityTransformStackCount[index].store(stackCount, std::memory_order_relaxed);
        g_halo4Camera.parityTransformScale[index].store(transform.scale, std::memory_order_relaxed);
        g_halo4Camera.parityTransformX[index].store(transform.translation[0], std::memory_order_relaxed);
        g_halo4Camera.parityTransformY[index].store(transform.translation[1], std::memory_order_relaxed);
        g_halo4Camera.parityTransformHits[index].fetch_add(1, std::memory_order_relaxed);
        (replay ? g_halo4Camera.parityTransformReplayHits[index] : g_halo4Camera.parityTransformNormalHits[index])
            .fetch_add(1, std::memory_order_relaxed);
    }

    bool Halo4EnforceCuiCaptureSelection(void* renderer, const Halo4CuiCaptureSelectionState& selection)
    {
        if (!renderer)
            return false;
        uint32_t count = 0;
        if (!Halo4SafeRead(
                static_cast<const uint8_t*>(renderer) + kHalo4CuiTransformStackCountOffset, &count, sizeof(count)) ||
            !Halo4CuiTransformStackCountValid(count))
        {
            return false;
        }

        uint8_t* const translationX = static_cast<uint8_t*>(renderer) + kHalo4CuiTransformStackEntriesOffset +
                                      static_cast<size_t>(count - 1) * kHalo4CuiTransformStride +
                                      kHalo4CuiTransformTranslationOffset;
        float currentX = 0.0f;
        if (!Halo4SafeRead(translationX, &currentX, sizeof(currentX)))
            return false;
        float adjustedX = currentX;
        // 3CR fold-in: un-hide by the LIVE hide shift the visible pass baked
        // for this layout (4*|baseX| from the recorded base translation);
        // the helper falls back to the calibrated width when none is live.
        const Halo4CuiAimOffset liveHide =
            Halo4BuildHiddenCuiTranslation(g_halo4Camera.cuiReticleBaseX.load(std::memory_order_relaxed),
                g_halo4Camera.cuiReticleBaseY.load(std::memory_order_relaxed));
        if (!Halo4CuiCaptureAdjustedTranslationX(
                currentX, Halo4CuiCaptureKeepsTopTransform(selection), liveHide.valid ? liveHide.x : 0.0f, adjustedX))
        {
            return false;
        }
        if (adjustedX == currentX)
            return true;
        return Halo4SafeWrite(translationX, &adjustedX, sizeof(adjustedX)) != 0;
    }

    void Halo4CuiGameplayRenderBody(uintptr_t caller, uint32_t windowIndex, uint32_t renderBufferChannel,
        const void* viewportBounds, const void* optionalProfileValue, uint32_t renderMode, bool flag)
    {
        Halo4CuiGameplayRenderFn const original = g_halo4OrigCuiGameplayRender;
        if (!original)
            return;

        Halo4CuiReticleEyeScope& scope = g_halo4CuiReticleEyeScope;
        const uintptr_t expectedCaller = g_halo4Camera.base + kHalo4CuiGameplayCallerReturnRva;
        if (g_halo4ScopeRendering && caller == expectedCaller)
        {
            ++hud_visibility::depth;
            __try
            {
                original(windowIndex, renderBufferChannel, viewportBounds, optionalProfileValue, renderMode, flag);
            }
            __finally
            {
                --hud_visibility::depth;
            }
            return;
        }
        const bool ownsGameplayPass = caller == expectedCaller && !scope.gameplayPassActive &&
                                      Halo4OwnsCuiReticleEyeTransaction() && Halo4CuiReticleNativeSuppressionLive();
        if (!ownsGameplayPass)
        {
            original(windowIndex, renderBufferChannel, viewportBounds, optionalProfileValue, renderMode, flag);
            return;
        }

        const bool hideHud = VR_HudHidden();
        if (hideHud)
            ++hud_visibility::depth;
        scope.gameplayPassActive = true;
        g_halo4HudGameplayThreadId = GetCurrentThreadId();
        if constexpr (kEnableHalo4ParityTrace)
        {
            g_halo4Camera.parityLastWindow.store(windowIndex, std::memory_order_relaxed);
            g_halo4Camera.parityLastChannel.store(renderBufferChannel, std::memory_order_relaxed);
            g_halo4Camera.parityLastRenderMode.store(renderMode, std::memory_order_relaxed);
            g_halo4Camera.parityLastFlag.store(flag ? 1u : 0u, std::memory_order_relaxed);
        }
        g_halo4Camera.cuiGameplayPasses.fetch_add(1, std::memory_order_relaxed);
        __try
        {
            // Halo 3/ODST/Reach render authored pixels into the OpenXR reticle
            // texture and hide the flat copy. On bounded sample frames, replay
            // the gameplay CUI stream while the centred authored target is
            // held through the whole front end. 43j proved Halo 4 batches the
            // descendant draws past the logical 0x29 marker.
            const int captureEye = g_config.right_eye_first ? 1 : 0;
            const bool wantsCaptureReplay = scope.eye == captureEye && g_config.crosshair && g_config.kill_reticle &&
                                            VR_ShouldCaptureAuthoredReticleThisFrame();
            if (wantsCaptureReplay)
            {
                scope.captureReplay = true;
                // Match the last headset-confirmed native-reticle pass: the
                // private replay uses stock CUI affine/curvature. Its own live
                // canvas is published below so 3CX framing never mixes these
                // stock coordinates with the scaled visible HUD coordinates.
                g_halo4HudGameplayThreadId = 0;
                g_halo4Camera.cuiReticleCaptureBaseX.store(0.0f, std::memory_order_relaxed);
                g_halo4Camera.cuiReticleCaptureBaseY.store(0.0f, std::memory_order_relaxed);
                scope.captureReplayAttempted = false;
                scope.captureReplayRedirected = false;
                scope.captureSelection = {};
                original(windowIndex, renderBufferChannel, viewportBounds, optionalProfileValue, renderMode, flag);
                scope.captureReplay = false;
                g_halo4HudGameplayThreadId = GetCurrentThreadId();
                scope.captureSelection = {};
                if (scope.redirectActive)
                    (void)Halo4EndCuiReticleRedirect(false);

                // A failed private redirect means the replay above was the one
                // stock pass in the eye. Do not draw it twice. The native/
                // procedural fallback remains intact for this frame.
                if (!scope.captureReplayRedirected)
                    return;
            }

            // Normal eye pass. The dispatcher moves only the native reticle
            // matrices offscreen; every other Halo 4 HUD command stays stock.
            original(windowIndex, renderBufferChannel, viewportBounds, optionalProfileValue, renderMode, flag);
        }
        __finally
        {
            if (hideHud)
                --hud_visibility::depth;
            // A malformed/missing 0x29 must restore the eye target before the
            // full-size CUI call returns. Auxiliary and later menu calls never
            // enter this phase and therefore remain completely stock.
            if (scope.redirectActive)
                (void)Halo4EndCuiReticleRedirect(true);
            scope.captureReplay = false;
            g_halo4HudGameplayThreadId = 0;
            scope.gameplayPassActive = false;
        }
    }

    __declspec(noinline) void __fastcall Halo4CuiGameplayRenderDetour(uint32_t windowIndex,
        uint32_t renderBufferChannel, const void* viewportBounds, const void* optionalProfileValue, uint32_t renderMode,
        bool flag)
    {
        const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress());
        g_halo4Camera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        ++g_halo4CuiFrontendCallbackDepth;
        // The earlier affine and curvature wrappers used this complete frontend
        // scope. Publish the same scope for the assembly curvature bridge so
        // sibling visor draws receive the same native HUD transform as radar,
        // shields and ammo. The private reticle replay temporarily clears it
        // inside Halo4CuiGameplayRenderBody and therefore remains stock.
        if (g_halo4CuiFrontendCallbackDepth == 1)
            g_halo4HudGameplayThreadId = GetCurrentThreadId();
        __try
        {
            Halo4CuiGameplayRenderBody(
                caller, windowIndex, renderBufferChannel, viewportBounds, optionalProfileValue, renderMode, flag);
        }
        __finally
        {
            --g_halo4CuiFrontendCallbackDepth;
            if (g_halo4CuiFrontendCallbackDepth == 0)
                g_halo4HudGameplayThreadId = 0;
            g_halo4Camera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    bool Halo4CuiRenderCommandBody(void* renderer, const void* command, void* openRenderSections, void* renderContext)
    {
        Halo4CuiRenderCommandFn const original = g_halo4OrigCuiRenderCommand;
        if (!original)
            return false;

        Halo4CuiCommandHeader header{};
        const bool headerReadable = command && Halo4SafeRead(command, &header, sizeof(header));
        int32_t reticleTransformId = 0;
        const bool beginPayloadReadable = headerReadable && header.command == kHalo4CuiCommandBegin &&
                                          header.payloadSize == kHalo4CuiCommandBeginPayloadSize &&
                                          Halo4SafeRead(static_cast<const uint8_t*>(command) + sizeof(header),
                                              &reticleTransformId, sizeof(reticleTransformId));
        Halo4CuiReticleEyeScope& scope = g_halo4CuiReticleEyeScope;
        Halo4ParityRecordCommand(headerReadable, header);

        // Capture replay owns the whole CUI command stream, not the logical
        // reticle subtree. Start before its first command and retain the private
        // target until user_interface_render returns. The replay state machine
        // then keeps only a 0x28/0x0c bitmap reticle container on-target and
        // rejects grenade/damage polyart before its vertices are baked.
        if (scope.captureReplay)
        {
            if (!scope.captureReplayAttempted)
            {
                scope.captureReplayAttempted = true;
                if (VR_BeginPreparedAuthoredReticleCapture())
                {
                    scope.redirectActive = true;
                    scope.captureAuthored = true;
                    scope.captureReplayRedirected = true;
                    scope.renderer = renderer;
                    scope.depth = 1;
                }
                else
                {
                    g_halo4Camera.cuiReticleRedirectFailures.fetch_add(1, std::memory_order_relaxed);
                }
            }
            const uint32_t captureCommand =
                headerReadable ? static_cast<uint32_t>(static_cast<uint16_t>(header.command)) : 0xFFFFFFFFu;
            const bool captureTargetWasActive = scope.redirectActive;
            if (captureTargetWasActive &&
                Halo4CuiCaptureMarkPolyartBeforeDraw(scope.captureSelection, headerReadable, captureCommand))
            {
                // Polyart vertices bake the current transform during this
                // command, so damage/grenade art must be hidden before the
                // original dispatcher executes it.
                (void)Halo4EnforceCuiCaptureSelection(renderer, scope.captureSelection);
            }
            const bool result = original(renderer, command, openRenderSections, renderContext);
            if (captureTargetWasActive)
            {
                Halo4CuiCaptureAdvanceAfterDraw(
                    scope.captureSelection, headerReadable, captureCommand, header.payloadSize);
                (void)Halo4EnforceCuiCaptureSelection(renderer, scope.captureSelection);
            }
            if (result && beginPayloadReadable && renderer)
            {
                uint32_t count = 0;
                if (Halo4SafeRead(static_cast<const uint8_t*>(renderer) + kHalo4CuiTransformStackCountOffset, &count,
                        sizeof(count)) &&
                    Halo4CuiTransformStackCountValid(count))
                {
                    const uint8_t* const entry = static_cast<const uint8_t*>(renderer) +
                                                 kHalo4CuiTransformStackEntriesOffset +
                                                 static_cast<size_t>(count - 1) * kHalo4CuiTransformStride;
                    BoneMatrix transform{};
                    if (Halo4SafeRead(entry, &transform, sizeof(transform)))
                    {
                        if (Halo4CuiCaptureKeepsTopTransform(scope.captureSelection) &&
                            std::isfinite(transform.translation[0]) && std::isfinite(transform.translation[1]))
                        {
                            g_halo4Camera.cuiReticleCaptureBaseX.store(
                                transform.translation[0], std::memory_order_relaxed);
                            g_halo4Camera.cuiReticleCaptureBaseY.store(
                                transform.translation[1], std::memory_order_relaxed);
                        }
                        Halo4ParityRecordTransform(reticleTransformId, header.payloadSize, count, transform, true);
                    }
                }
            }
            return result;
        }

        // This dispatcher executes the entire CUI stream. All non-reticle
        // commands stay a single trampoline call; ownership/config checks are
        // paid only for the exact H4EK-proven 0x28/0x0C marker.
        if (!beginPayloadReadable)
            return original(renderer, command, openRenderSections, renderContext);

        const bool ownsStereo = Halo4OwnsCuiReticleEyeScope();
        const Halo4CuiReticleAction action =
            Halo4DecideCuiReticleAction(ownsStereo, Halo4CuiReticleNativeSuppressionLive(), beginPayloadReadable,
                headerReadable ? static_cast<uint32_t>(static_cast<uint16_t>(header.command)) : 0, g_config.crosshair,
                g_config.kill_reticle, g_stereoEye.load(std::memory_order_relaxed), g_config.right_eye_first);

        // Every command always runs. In the visible eye pass, type 0x28 pushes
        // the reticle-only 0x34 transform. Move only that native flat copy
        // offscreen; the captured pixels are presented by the exact OpenXR
        // reticle quad used by Halo 3/ODST/Reach.
        const bool result = original(renderer, command, openRenderSections, renderContext);
        if (!result || action == Halo4CuiReticleAction::DrawStock)
            return result;

        g_halo4Camera.cuiReticleBegins.fetch_add(1, std::memory_order_relaxed);
        uint32_t count = 0;
        if (!renderer ||
            !Halo4SafeRead(
                static_cast<const uint8_t*>(renderer) + kHalo4CuiTransformStackCountOffset, &count, sizeof(count)) ||
            !Halo4CuiTransformStackCountValid(count))
        {
            g_halo4Camera.cuiReticleRedirectFailures.fetch_add(1, std::memory_order_relaxed);
            return result;
        }

        uint8_t* const entry = static_cast<uint8_t*>(renderer) + kHalo4CuiTransformStackEntriesOffset +
                               static_cast<size_t>(count - 1) * kHalo4CuiTransformStride;
        BoneMatrix transform{};
        if (!Halo4SafeRead(entry, &transform, sizeof(transform)))
        {
            g_halo4Camera.cuiReticleRedirectFailures.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        const float* const values = reinterpret_cast<const float*>(&transform);
        for (size_t value = 0; value < sizeof(transform) / sizeof(float); ++value)
        {
            if (!std::isfinite(values[value]))
            {
                g_halo4Camera.cuiReticleRedirectFailures.fetch_add(1, std::memory_order_relaxed);
                return result;
            }
        }
        if (transform.scale < 0.001f || transform.scale > 128.0f)
        {
            g_halo4Camera.cuiReticleRedirectFailures.fetch_add(1, std::memory_order_relaxed);
            return result;
        }

        const float stockScale = transform.scale;
        const float baseX = transform.translation[0];
        const float baseY = transform.translation[1];
        Halo4ParityRecordTransform(reticleTransformId, header.payloadSize, count, transform, false);
        // This path never receives or computes an aim coordinate. It only
        // removes the duplicate flat copy; the shared reticleQuad owns the
        // exact bullet-ray position for all four titles.
        const Halo4CuiAimOffset cuiDelta = Halo4BuildHiddenCuiTranslation(baseX, baseY);
        if (!cuiDelta.valid)
        {
            g_halo4Camera.cuiReticleRedirectFailures.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        const float writtenScale = stockScale;
        transform.translation[0] += cuiDelta.x;
        transform.translation[1] += cuiDelta.y;
        if (!Halo4SafeWrite(entry, &transform, sizeof(transform)))
        {
            g_halo4Camera.cuiReticleRedirectFailures.fetch_add(1, std::memory_order_relaxed);
            return result;
        }
        g_halo4Camera.cuiReticleBaseX.store(baseX, std::memory_order_relaxed);
        g_halo4Camera.cuiReticleBaseY.store(baseY, std::memory_order_relaxed);
        g_halo4Camera.cuiReticleAimX.store(cuiDelta.x, std::memory_order_relaxed);
        g_halo4Camera.cuiReticleAimY.store(cuiDelta.y, std::memory_order_relaxed);
        g_halo4Camera.cuiReticleStockScale.store(stockScale, std::memory_order_relaxed);
        g_halo4Camera.cuiReticleWrittenScale.store(writtenScale, std::memory_order_relaxed);
        g_halo4Camera.cuiReticleSuppressions.fetch_add(1, std::memory_order_relaxed);
        g_halo4Camera.cuiReticleCompleted.fetch_add(1, std::memory_order_relaxed);
        g_halo4Camera.cuiReticleEyeSerial[scope.eye].store(scope.preparedSerial, std::memory_order_release);
        (void)reticleTransformId;
        return result;
    }

    __declspec(noinline) bool __fastcall Halo4CuiRenderCommandDetour(
        void* renderer, const void* command, void* openRenderSections, void* renderContext)
    {
        bool result = false;
        const bool frontendPinsCallback = g_halo4CuiFrontendCallbackDepth != 0;
        if (!frontendPinsCallback)
            g_halo4Camera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            result = Halo4CuiRenderCommandBody(renderer, command, openRenderSections, renderContext);
        }
        __finally
        {
            if (!frontendPinsCallback)
                g_halo4Camera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
        return result;
    }

    void Halo4NoteRejection(Halo4StereoRejection reason, bool frameStarted)
    {
        g_halo4Camera.lastRejection.store(static_cast<uint32_t>(reason), std::memory_order_relaxed);
        (frameStarted ? g_halo4Camera.droppedFrames : g_halo4Camera.stockFrames)
            .fetch_add(1, std::memory_order_relaxed);
    }

    void Halo4InvalidatePublishedFrame()
    {
        g_halo4RenderFovSerial[0].store(0, std::memory_order_release);
        g_halo4RenderFovSerial[1].store(0, std::memory_order_release);
        VR_InvalidateHalo4PreparedFrame();
    }

    void Halo4ResetTelemetry()
    {
        g_halo4Camera.vrikSolvedPalettes.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikStockPalettes.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikAlignmentRefusals.store(0, std::memory_order_relaxed);
        for (std::atomic<uint64_t>& stage : g_halo4Camera.vrikStageRefusals)
            stage.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikExactReturnHits.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikWeaponRecordsCarried.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikWeaponRecordsRefused.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikMarkerParityFallbacks.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikMarkerParityApplications.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikTwoHandRigidSupportLocks.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikBodyFillRecords.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikWeaponFillRecords.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikUnexpectedFillRecords.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikUnreadableFillRecords.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikNodeCountResolveFailures.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikLastBodyNodeCount.store(-1, std::memory_order_relaxed);
        g_halo4Camera.vrikLastWeaponNodeCount.store(-1, std::memory_order_relaxed);
        g_halo4Camera.vrikStormRecordCandidates.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikLastStormChecksum.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikLastHeldChecksum.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikLastBodyChecksum.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikLastHeldNodeCount.store(-1, std::memory_order_relaxed);
        g_halo4Camera.vrikArmBandBonesCarried.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikArmBandBonesSkipped.store(0, std::memory_order_relaxed);
        g_halo4StockRightHandLocalSerial.store(0, std::memory_order_release);
        g_halo4StockRightHandLocalPublishedAt.store(0, std::memory_order_relaxed);
        // This one is load-bearing, not hygiene: `vrikExactReturnHits`
        // is zeroed just above, so a serial left set across a re-arm would let
        // the age gate admit a hand motion solved on the PREVIOUS level and
        // drag the gun by it - the stale-transform class this project has
        // already paid for once.
        g_halo4RightHandMotionSerial.store(0, std::memory_order_release);
        g_halo4RightHandMotionPublishedAt.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikRigScale.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.vrikPedestalTranslation.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.vrikPedestalScale.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.vrikPedestalTilt.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.vrikShoulderSpan.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.vrikTargetMiss.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.vrikWeaponPredictionTranslation.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.vrikWeaponPredictionAngle.store(0.0f, std::memory_order_relaxed);
        g_halo4CenterRoot.sequence.store(0, std::memory_order_release);
        g_halo4ControllerRoot.sequence.store(0, std::memory_order_release);
        g_halo4EyeRoot[0].sequence.store(0, std::memory_order_release);
        g_halo4EyeRoot[1].sequence.store(0, std::memory_order_release);
        g_halo4Camera.vrikLinkMeasurements.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikClassifyAttempts.store(0, std::memory_order_relaxed);
        g_halo4Camera.vrikProbeScale.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.vrikProbeColumnError.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.vrikProbeOrthoError.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.vrikProbeTranslation.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.vrikLastRightUpper.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.vrikLastRightLower.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.vrikLastLeftUpper.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.vrikLastLeftLower.store(0.0f, std::memory_order_relaxed);
        // Slot 0 would otherwise claim the legitimate count 0, because the
        // caller's `jle` path can reach this call site with a zero count.
        for (int slot = 0; slot < kHalo4VrikCountSlots; ++slot)
        {
            g_halo4Camera.vrikCountHits[slot].store(0, std::memory_order_relaxed);
            g_halo4Camera.vrikCountValue[slot].store(kHalo4VrikCountSlotEmpty, std::memory_order_release);
        }
        g_halo4Camera.vrikCountOverflow.store(0, std::memory_order_relaxed);
        g_halo4Camera.stereoFrames.store(0, std::memory_order_relaxed);
        g_halo4Camera.stockFrames.store(0, std::memory_order_relaxed);
        g_halo4Camera.droppedFrames.store(0, std::memory_order_relaxed);
        g_halo4Camera.cameraClaims.store(0, std::memory_order_relaxed);
        g_halo4Camera.projectionReadbacks.store(0, std::memory_order_relaxed);
        g_halo4Camera.uncapturedEyes.store(0, std::memory_order_relaxed);
        g_halo4Camera.cuiGameplayPasses.store(0, std::memory_order_relaxed);
        g_halo4Camera.cuiReticleBegins.store(0, std::memory_order_relaxed);
        g_halo4Camera.cuiReticleCaptures.store(0, std::memory_order_relaxed);
        g_halo4Camera.cuiReticleSuppressions.store(0, std::memory_order_relaxed);
        g_halo4Camera.cuiReticleRedirectFailures.store(0, std::memory_order_relaxed);
        g_halo4Camera.cuiReticleCompleted.store(0, std::memory_order_relaxed);
        g_halo4Camera.cuiReticleForcedCleanup.store(0, std::memory_order_relaxed);
        for (auto& bucket : g_halo4Camera.parityCuiCommands)
            bucket.store(0, std::memory_order_relaxed);
        g_halo4Camera.parityCuiCommandsOutOfRange.store(0, std::memory_order_relaxed);
        g_halo4Camera.parityCuiUnreadableHeaders.store(0, std::memory_order_relaxed);
        g_halo4Camera.parityCuiTotalCommands.store(0, std::memory_order_relaxed);
        for (size_t slot = 0; slot < kHalo4ParityTransformSlotCount; ++slot)
        {
            g_halo4Camera.parityTransformHits[slot].store(0, std::memory_order_relaxed);
            g_halo4Camera.parityTransformReplayHits[slot].store(0, std::memory_order_relaxed);
            g_halo4Camera.parityTransformNormalHits[slot].store(0, std::memory_order_relaxed);
            g_halo4Camera.parityTransformPayload[slot].store(0, std::memory_order_relaxed);
            g_halo4Camera.parityTransformStackCount[slot].store(0, std::memory_order_relaxed);
            g_halo4Camera.parityTransformScale[slot].store(0.0f, std::memory_order_relaxed);
            g_halo4Camera.parityTransformX[slot].store(0.0f, std::memory_order_relaxed);
            g_halo4Camera.parityTransformY[slot].store(0.0f, std::memory_order_relaxed);
            g_halo4Camera.parityTransformId[slot].store(kHalo4ParityEmptyTransformId, std::memory_order_release);
        }
        g_halo4Camera.parityTransformOverflow.store(0, std::memory_order_relaxed);
        g_halo4Camera.parityLastWindow.store(0, std::memory_order_relaxed);
        g_halo4Camera.parityLastChannel.store(0, std::memory_order_relaxed);
        g_halo4Camera.parityLastRenderMode.store(0, std::memory_order_relaxed);
        g_halo4Camera.parityLastFlag.store(0, std::memory_order_relaxed);
        for (auto& serial : g_halo4Camera.cuiReticleEyeSerial)
            serial.store(0, std::memory_order_relaxed);
        g_halo4Camera.consecutiveUncaptured.store(0, std::memory_order_relaxed);
        g_halo4Camera.lastPositionError.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.lastForwardError.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.lastUpError.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.lastProjectionCenterX.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.lastProjectionCenterY.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.lastHalfFovX.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.lastHalfFovY.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.lastRejection.store(0, std::memory_order_relaxed);
        g_halo4RenderFovSerial[0].store(0, std::memory_order_release);
        g_halo4RenderFovSerial[1].store(0, std::memory_order_release);

        // The recenter reference is deliberately dropped here: the
        // camera core is installed and removed per level, and carrying a stale
        // reference across a load would spawn the player facing a direction
        // chosen during the previous level. The next tracked frame re-captures
        // it against the level's own camera.
        g_halo4Camera.headReferenceValid.store(false, std::memory_order_release);
        g_halo4Camera.headYawReference.store(0.0f, std::memory_order_relaxed);
        for (int axis = 0; axis < 3; ++axis)
        {
            g_halo4Camera.headPositionReference[axis].store(0.0f, std::memory_order_relaxed);
        }
        g_halo4Camera.headTrackedFrames.store(0, std::memory_order_relaxed);
        g_halo4Camera.lastHeadYawDelta.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.lastHeadPitchDelta.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.lastLeanMagnitude.store(0.0f, std::memory_order_relaxed);
        for (int axis = 0; axis < 3; ++axis)
            g_halo4Camera.lastLean[axis].store(0.0f, std::memory_order_relaxed);

        // Dropping the published engine pitch is what makes the input
        // thread's loop fail closed across a level change: its freshness gate
        // sees the serial stop advancing and parks the stick at zero rather
        // than steering the next level's camera with the last one's error.
        g_halo4Camera.enginePitchValid.store(false, std::memory_order_release);
        g_halo4Camera.enginePitch.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.enginePitchSerial.store(0, std::memory_order_release);
        g_halo4Camera.engineAimForward[0].store(1.0f, std::memory_order_relaxed);
        g_halo4Camera.engineAimForward[1].store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.engineAimForward[2].store(0.0f, std::memory_order_relaxed);
        // T-3: the telemetry-only stock camera publication goes with them; an
        // unreadable frame can never retain the previous level's position. The
        // clear runs inside its own seqlock window, bumping the version twice
        // (odd, then even) exactly like a real publication: the counter is
        // never stored, so the even-at-rest parity the reader depends on
        // survives every reset.
        g_halo4Camera.stockCameraVersion.fetch_add(1, std::memory_order_acq_rel); // -> odd (clearing)
        g_halo4Camera.stockCameraValid.store(false, std::memory_order_relaxed);
        g_halo4Camera.stockCameraSerial.store(0, std::memory_order_relaxed);
        for (int axis = 0; axis < 3; ++axis)
            g_halo4Camera.stockCameraPosition[axis].store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.stockCameraVersion.fetch_add(1, std::memory_order_release); // -> even (cleared)
        // The yaw reference goes with it. Carrying a heading chosen
        // during the previous level across a load is exactly the fault
        // Halo4ResetTelemetry already avoids for the head reference.
        g_halo4Camera.gameYawReferenceValid.store(false, std::memory_order_release);
        g_halo4Camera.gameYawReference.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.vrTurns.store(0, std::memory_order_relaxed);
        // The node-format proof is per module generation, for the
        // same reason the FOV calibration is - a different level or model may
        // present a different first-person assembly, and one probe frame is a
        // cheap price for never writing on a stale proof.
        g_halo4Camera.nodeFormatProven.store(false, std::memory_order_release);
        g_halo4Camera.nodeFormatRejected.store(false, std::memory_order_release);
        g_halo4Camera.handsPlacedFrames.store(0, std::memory_order_relaxed);
        g_halo4Camera.handsBlockedFrames.store(0, std::memory_order_relaxed);
        g_halo4Camera.lastRootNode.store(-1, std::memory_order_relaxed);
        g_halo4Camera.lastWeaponCount.store(0, std::memory_order_relaxed);
        g_halo4Camera.stockNodeQuaternionLength.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.stockNodeScale.store(0.0f, std::memory_order_relaxed);
        for (int axis = 0; axis < 3; ++axis)
        {
            g_halo4Camera.stockNodeTranslation[axis].store(0.0f, std::memory_order_relaxed);
            g_halo4Camera.lastHandTranslation[axis].store(0.0f, std::memory_order_relaxed);
        }
        g_halo4Camera.lastWriteSurvived.store(false, std::memory_order_relaxed);
        g_halo4Camera.lastNodesWritten.store(0, std::memory_order_relaxed);
        // The shared aim-liveness flag Halo 4's transaction now sets. Bound it
        // to this core's lifetime: MCC keeps every title's module loaded, so a
        // flag left true by Halo 4 would otherwise tell the shared loop that
        // the NEXT title's camera hook is already running.
        g_aimSeen.store(false, std::memory_order_release);
        g_halo4Camera.lastPitchError.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.lastPitchCommand.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.lastPitchTarget.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.pitchServoDirection.store(1.0f, std::memory_order_relaxed);
        g_halo4Camera.pitchServoMinStep.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.pitchCommandedFrames.store(0, std::memory_order_relaxed);
        g_halo4Camera.pitchParkedFrames.store(0, std::memory_order_relaxed);
        g_halo4Camera.pitchServoStalls.store(0, std::memory_order_relaxed);

        // The FOV calibration is re-learned per generation for the same reason:
        // a different level or window layout may build its projection
        // differently, and one stock frame is a cheap price for never widening
        // on a stale mapping.
        g_halo4Camera.fovGain.store(0.3925f, std::memory_order_relaxed);
        g_halo4Camera.fovRatio.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.fovLearned.store(false, std::memory_order_release);
        g_halo4Camera.fovWidenedFrames.store(0, std::memory_order_relaxed);
        g_halo4Camera.fovStockFovFrames.store(0, std::memory_order_relaxed);
        g_halo4Camera.lastRequestedHalfX.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.lastRequestedHalfY.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.lastCoverContained.store(false, std::memory_order_relaxed);
        g_halo4Camera.lastObserverVerticalFov.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.lastElementVerticalFov.store(0.0f, std::memory_order_relaxed);
        g_halo4Camera.lastConverterScale.store(0.0f, std::memory_order_relaxed);
    }

    int Halo4RestoreMonoCamera(
        const Halo4SetupArgs& args, const unsigned char savedObserver[kHalo4ObserverSnapshotBytes])
    {
        if (!Halo4SafeWrite(reinterpret_cast<void*>(args.observer), savedObserver, kHalo4ObserverSnapshotBytes))
        {
            return 0;
        }
        __try
        {
            g_halo4OrigSetup(args.view, args.window, args.count, args.mode, args.user, args.observer);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("restoring the Halo 4 mono camera faulted");
            return 0;
        }
        return 1;
    }

    void __fastcall Halo4SetupDetour(
        uintptr_t view, uint32_t window, uint32_t count, uint32_t mode, uint32_t user, uintptr_t observer)
    {
        const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
        g_halo4Camera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            Halo4SetupArgs& args = g_halo4SetupArgs;
            // Only the proven call site inside the per-window loop may
            // establish a transaction. Our own re-invocations go through the
            // trampoline and never reach this detour at all.
            if (returnAddress == g_halo4Camera.setupReturnAddress && view && observer)
            {
                args.valid = true;
                args.view = view;
                args.window = window;
                args.count = count;
                args.mode = mode;
                args.user = user;
                args.observer = observer;
            }
            else
            {
                args.valid = false;
            }
            g_halo4OrigSetup(view, window, count, mode, user, observer);
        }
        __finally
        {
            g_halo4Camera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    inline void Halo4ScopeLogCamera(const float* camera, const float* head)
    {
        VR_NoteScopeCamera(GameTitle::Halo4, camera, head, Game_GetWorldScale());
    }
    inline const float* Halo4ScopeOrigin(const float* head, const float* barrel, float out[3])
    {
        return VR_ScopeCameraOrigin(GameTitle::Halo4, Game_GetWorldScale(), barrel, out) ? out : head;
    }
#include "halo4_scope.inl"

    // Stock-projection geometry on the sustained wrapper scope.
    // The snapshot, camera basis, and both eye transforms are validated before
    // the first mutation. Once mutation begins, any failure drops this frame;
    // it never mixes a partial eye with another stock wrapper render.
    // POD-only: this function owns no C++ destructor because SEH must always run
    // the eye/observer cleanup below.
    Halo4StereoResult Halo4StereoTransaction(
        uintptr_t element, uintptr_t view, uint32_t window, const Halo4SetupArgs& args, bool& frameStarted)
    {
        frameStarted = false;
        Halo4VrRenderSnapshot snapshot{};
        if (!VR_Halo4GetRenderSnapshot(snapshot))
        {
            Halo4NoteRejection(Halo4StereoRejection::PreparedFrameUnavailable, false);
            return Halo4StereoResult::NotStarted;
        }

        // Publish only from the proven claimed camera transaction. The shared
        // resolver owns theatre admission; a missing/unknown H4 sample simply
        // expires and never changes this stereo transaction's ownership.
        PublishHalo4CinematicTheater();

        // The shared theatre switches only at full black.  Once it owns the
        // presentation, Halo 4's pristine observer is the authored cutscene
        // camera: keep the stereo eye split below, but do not compose the HMD
        // midpoint pose or replace the authored projection with the gameplay
        // OpenXR cover.  Sampling this once keeps both eyes in one camera mode
        // for the complete transaction and preserves the existing transition.
        const bool authoredTheater = VR_IsCutsceneTheaterActive();

        unsigned char savedObserver[kHalo4ObserverSnapshotBytes];
        if (!Halo4SafeRead(reinterpret_cast<const void*>(args.observer), savedObserver, sizeof(savedObserver)))
        {
            Halo4NoteRejection(Halo4StereoRejection::ObserverUnreadable, false);
            return Halo4StereoResult::NotStarted;
        }

        Halo4CameraBasis stock{};
        memcpy(stock.position, savedObserver + kHalo4ObserverPositionOffset, sizeof(stock.position));
        memcpy(stock.forward, savedObserver + kHalo4ObserverForwardOffset, sizeof(stock.forward));
        memcpy(stock.up, savedObserver + kHalo4ObserverUpOffset, sizeof(stock.up));
        memcpy(&stock.verticalFov, savedObserver + kHalo4ObserverVerticalFovOffset, sizeof(stock.verticalFov));
        memcpy(&stock.fovRatio, savedObserver + kHalo4ObserverFovRatioOffset, sizeof(stock.fovRatio));
        if (!Halo4ValidateCameraBasis(stock))
        {
            Halo4NoteRejection(Halo4StereoRejection::CameraInvalid, false);
            return Halo4StereoResult::NotStarted;
        }
        // Retain the game body/aim camera before HMD composition.  Hands are
        // controller-owned in this frame; `stock` below becomes the HMD-owned
        // render camera and must not be reused for their placement.
        const Halo4CameraBasis controllerRoot = stock;

        // Publish the ENGINE's own look pitch, taken before any of our
        // head transform touches it. This observer forward is the ray Halo
        // spawns first-person shots along, so it is both what the closed loop
        // must steer and what proves it worked. Allocation-free and lock-free:
        // three relaxed stores and one release increment.
        {
            const float z = stock.forward[2] < -1.0f ? -1.0f : (stock.forward[2] > 1.0f ? 1.0f : stock.forward[2]);
            g_halo4Camera.enginePitch.store(asinf(z), std::memory_order_relaxed);
            // The whole direction is published from this one read, so the
            // aim loop can never pair a yaw from one frame with a pitch from
            // the next - the incoherence Reach's own feedback publication was
            // rebuilt to remove. The release on the serial below publishes all
            // four values together.
            for (int axis = 0; axis < 3; ++axis)
            {
                g_halo4Camera.engineAimForward[axis].store(stock.forward[axis], std::memory_order_relaxed);
            }
            g_halo4Camera.enginePitchValid.store(true, std::memory_order_release);
            g_halo4Camera.enginePitchSerial.fetch_add(1, std::memory_order_release);
            // The shared aim loop's "camera hook not running" gate. Reach sets
            // it from its own camera publication for the same reason: an owned
            // Halo 4 render frame IS the proof that a level is running and the
            // camera is live.
            g_aimSeen.store(true, std::memory_order_release);
        }

        // T-3 telemetry-only: publish the pristine observer position already
        // read and validated above, under the stock-camera seqlock. No gameplay
        // reader exists; this cannot affect the frame. The version goes odd
        // BEFORE the first payload store and even again AFTER the last one, so
        // the telemetry reader either sees one whole iteration or fails open;
        // the serial is bumped inside that window and is what the recording
        // reports.
        g_halo4Camera.stockCameraVersion.fetch_add(1, std::memory_order_acq_rel); // -> odd (writing)
        for (int axis = 0; axis < 3; ++axis)
            g_halo4Camera.stockCameraPosition[axis].store(stock.position[axis], std::memory_order_relaxed);
        g_halo4Camera.stockCameraValid.store(true, std::memory_order_relaxed);
        g_halo4Camera.stockCameraSerial.fetch_add(1, std::memory_order_relaxed);
        g_halo4Camera.stockCameraVersion.fetch_add(1, std::memory_order_release); // -> even (published)

        // Put the player inside. The headset's orientation and its
        // room-space movement are applied to the MONO camera here, before the
        // eyes split off it, so both eyes inherit one consistent head pose and
        // the IPD baseline (forward x up, halo4_render_logic.h) rotates with
        // the head instead of staying welded to the engine's heading.
        //
        // This is a DELTA on top of Halo 4's own camera, not Halo 3's outright
        // replacement of forward/up. Halo 3 can replace the basis because it
        // also owns the turn stick; Halo 4's turn/look ownership is handled
        // separately, and replacing the basis without it would leave the player
        // unable to turn at all. Applying the headset on top keeps the accepted
        // gamepad behaviour intact.
        //
        // Failure is feature-local: no head pose this frame means
        // the eyes still render from the engine's camera. It never drops a pair.
        const float worldScale = g_worldScale.load(std::memory_order_acquire);
        bool headTracked = false;
        if (!authoredTheater && snapshot.headPoseValid)
        {
            Halo4HeadPoseInput headInput{};
            memcpy(headInput.quaternion, snapshot.headOrientation, sizeof(headInput.quaternion));
            memcpy(headInput.position, snapshot.headPosition, sizeof(headInput.position));

            // Decode BEFORE consuming either request. Sampling the flags first
            // would silently swallow a recenter the player asked for on a frame
            // whose pose could not be decoded.
            Halo4HeadOrientation head{};
            const bool decoded = Halo4DecodeHeadOrientation(snapshot.headOrientation, head);
            const bool manualRecenter = decoded && g_needRecenter.exchange(false);
            const bool positionOnlyRecenter = decoded && g_needPosRecenter.exchange(false);
            if (decoded && (manualRecenter || !g_halo4Camera.headReferenceValid.load(std::memory_order_acquire)))
            {
                // A recenter aligns the player's physical facing to
                // the heading the ENGINE is currently looking along, then the
                // VR turn stick owns that heading from here. Same pairing Halo
                // 3 captures in ApplyHeadLook (g_gameYawRef / g_headYawRef);
                // capturing them together is what makes the recenter seamless.
                g_halo4Camera.gameYawReference.store(
                    atan2f(stock.forward[1], stock.forward[0]), std::memory_order_relaxed);
                g_halo4Camera.gameYawReferenceValid.store(true, std::memory_order_release);
                g_halo4Camera.headYawReference.store(head.yaw, std::memory_order_relaxed);
                NoteYawReferenceLatched(GameTitle::Halo4, g_halo4Camera.generation.load(std::memory_order_relaxed),
                    manualRecenter ? "recenter" : "first frame", atan2f(stock.forward[1], stock.forward[0]), head.yaw,
                    false);
                for (int axis = 0; axis < 3; ++axis)
                {
                    g_halo4Camera.headPositionReference[axis].store(
                        snapshot.headPosition[axis], std::memory_order_relaxed);
                }
                // Halo 4's reference pair stays private. Earlier
                // writes into the Halo 3/ODST/Reach globals existed only for
                // the rejected shared-hand experiment and are deliberately
                // inactive; target freezing reads the private pair above.
                g_halo4Camera.headReferenceValid.store(true, std::memory_order_release);
            }
            else if (decoded && positionOnlyRecenter)
            {
                // Enabling leaning captures the neutral head position only, so
                // the yaw baseline is untouched and the view does not snap.
                for (int axis = 0; axis < 3; ++axis)
                {
                    g_halo4Camera.headPositionReference[axis].store(
                        snapshot.headPosition[axis], std::memory_order_relaxed);
                }
            }

            if (decoded && g_halo4Camera.headReferenceValid.load(std::memory_order_acquire))
            {
                headInput.headYawReference = g_halo4Camera.headYawReference.load(std::memory_order_relaxed);
                for (int axis = 0; axis < 3; ++axis)
                {
                    headInput.headPositionReference[axis] =
                        g_halo4Camera.headPositionReference[axis].load(std::memory_order_relaxed);
                }
                headInput.yawSign = g_yawSign.load(std::memory_order_relaxed);
                headInput.pitchSign = g_pitchSign.load(std::memory_order_relaxed);
                headInput.pitchTrim = g_pitchTrim.load(std::memory_order_relaxed);
                headInput.worldScale = worldScale;
                headInput.positional = g_positional.load(std::memory_order_relaxed);
                // The same predicate the XInput hook uses to take the
                // stick's vertical axis. Sampling one predicate for both means
                // the view and the stick can never disagree about who owns
                // pitch - and F2 turns the whole behaviour off together,
                // returning exactly the additive head pose.
                headInput.headOwnsPitch = Halo4LookPitchOwned();
                // Once the aim loop is steering the engine's heading
                // toward this same reference, the view MUST stop reading the
                // engine's live heading. Reading it would double the head's
                // yaw - the loop drives engineYaw to (reference + headDelta),
                // and the view would then add headDelta to that again. Halo 3
                // has always composed from the reference for this reason.
                headInput.headOwnsYaw =
                    Halo4ControllerAimActive() && g_halo4Camera.gameYawReferenceValid.load(std::memory_order_acquire);
                headInput.gameYawReference = g_halo4Camera.gameYawReference.load(std::memory_order_relaxed);

                // Derive orientation without translating the native body sample.
                Halo4CameraBasis roomscaleBasis = stock;
                if ((g_config.roomscale_movement || g_config.physical_running) &&
                    Halo4ApplyHeadPose(roomscaleBasis, headInput))
                {
                    Roomscale_Camera(GameTitle::Halo4, Game_RoomscaleCameraAllowed(GameTitle::Halo4), stock.position,
                        snapshot.headPosition, snapshot.headOrientation, roomscaleBasis.forward,
                        headInput.headPositionReference, worldScale);
                    for (int axis = 0; axis < 3; ++axis)
                        g_halo4Camera.headPositionReference[axis].store(
                            headInput.headPositionReference[axis], std::memory_order_relaxed);
                }

                const Halo4CameraBasis beforeHead = stock;
                if (Halo4ApplyHeadPose(stock, headInput))
                {
                    ApplyCachedPhysicalCrouch(GameTitle::Halo4, stock.position,
                        -Clamp(
                            (snapshot.headPosition[1] - headInput.headPositionReference[1]) * worldScale, -1.5f, 1.5f),
                        true);
                    headTracked = true;
                    g_halo4Camera.headTrackedFrames.fetch_add(1, std::memory_order_relaxed);
                    g_halo4Camera.lastHeadYawDelta.store(
                        Halo4WrapPi(headInput.yawSign * Halo4WrapPi(head.yaw - headInput.headYawReference)),
                        std::memory_order_relaxed);
                    g_halo4Camera.lastHeadPitchDelta.store(
                        headInput.pitchSign * head.pitch + headInput.pitchTrim, std::memory_order_relaxed);
                    float lean = 0.0f;
                    for (int axis = 0; axis < 3; ++axis)
                    {
                        const float d = stock.position[axis] - beforeHead.position[axis];
                        lean += d * d;
                        g_halo4Camera.lastLean[axis].store(d, std::memory_order_relaxed);
                    }
                    g_halo4Camera.lastLeanMagnitude.store(sqrtf(lean), std::memory_order_relaxed);
                }
                else
                {
                    // Restore the engine's camera exactly. A refused head
                    // transform must not leave a half-rotated basis behind.
                    stock = beforeHead;
                }
            }
        }
        (void)headTracked;

        // The old hand writes, though verified, land in the animation system's telemetry
        // mirror, not in the render input. Leave the investigation code intact,
        // but do not keep overwriting that dead block while the real producer is
        // being traced. This is feature-local: Halo 4's camera transaction and
        // motion aim remain completely unchanged.

        Halo4CameraBasis eyeCameras[2]{};
        for (int eye = 0; eye < 2; ++eye)
        {
            float eyePosition[3]{};
            float eyeOrientation[4]{};
            memcpy(eyePosition, snapshot.eyes[eye].position, sizeof(eyePosition));
            memcpy(eyeOrientation, snapshot.eyes[eye].orientation, sizeof(eyeOrientation));
            ApplyCutsceneTheaterEyeTransform(
                authoredTheater, g_config.cutscene_theater_depth, eyePosition, eyeOrientation);
            if (!Halo4BuildEyeCamera(stock, eyePosition, eyeOrientation, worldScale, eyeCameras[eye]))
            {
                Halo4NoteRejection(Halo4StereoRejection::EyeBuildFailed, false);
                return Halo4StereoResult::NotStarted;
            }
        }

        // The CENTER camera, built by the exact same function and the exact
        // same snapshot the two eye cameras are built from, published for the
        // first-person palette to root itself on. Halo 4's own first-person
        // root carries no 6DOF, so without this the hands and gun rotate with
        // the head but never translate. Center rather than per-eye, so both
        // eyes solve the same arms.
        //
        // `stock` AT THIS POINT ALREADY CARRIES HEAD-LOOK. The head pose was
        // applied to it above - that is what the lean measurement immediately
        // above this is differencing. So the center root is `stock` ITSELF,
        // with no further offset and no further rotation.
        //
        // This is the same rule Halo 3 and Reach each wrote down after paying
        // for it: "already carries head-look. Touching it would double-apply"
        // (the Reach world render camera), and "writing here would apply the
        // hand twice" (the Halo 3 sim bank). Feeding snapshot.headPosition or
        // snapshot.headOrientation back into Halo4BuildEyeCamera here adds the
        // headset pose a SECOND time, which is exactly the double-tracking the
        // player sees: the hands move with the head once from the camera the
        // eyes render from, and again from the root the palette is placed on.
        //
        // Passing a zero offset instead is equally wrong in the other
        // direction only if `stock` were the raw game camera - it is not, it is
        // the post-head-look camera, the direct analogue of Halo 3's g_camX/Y/Z
        // which is read back AFTER ApplyHeadLook.
        if (Halo4ValidateCameraBasis(stock))
            Halo4PublishCenterRoot(stock);
        if (Halo4ValidateCameraBasis(controllerRoot))
            Halo4PublishControllerRoot(controllerRoot);

        // Solve each eye's raster cover from the runtime's own reported
        // frustum. Nothing here names a headset. Halo 4's stock cover is
        // narrower than several headsets (measured PSVR2: stock 50.46/41.14 deg
        // against a native 61.5/53.0), and when the cover does not CONTAIN the
        // native frustum vr.cpp cannot crop to it and submits the whole slice
        // at the wrong FOV - the "M2 WARNING" line in vr.cpp's log.
        //
        // The calibration is measured from the engine's own finished
        // projection, so the first stereo frame of a generation renders at
        // Halo 4's stock FOV and teaches us the mapping; every later frame
        // widens. A runtime that reports no per-eye FOV simply never widens.
        Halo4FovCalibration calibration{};
        calibration.gain = g_halo4Camera.fovGain.load(std::memory_order_relaxed);
        calibration.ratio = g_halo4Camera.fovRatio.load(std::memory_order_relaxed);
        calibration.learned = g_halo4Camera.fovLearned.load(std::memory_order_acquire);
        float verticalFovWrite[2]{};
        float expectedHalfY[2]{};
        bool widenFov[2] = {false, false};
        for (int eye = 0; eye < 2; ++eye)
        {
            widenFov[eye] = !authoredTheater && snapshot.eyes[eye].fovValid &&
                            Halo4SolveCoverVerticalFov(snapshot.eyes[eye].fov, calibration, kHalo4CoverMargin,
                                verticalFovWrite[eye], expectedHalfY[eye]);
        }

        float halfFovX[2]{};
        float halfFovY[2]{};
        float maximumPositionError = 0.0f;
        float maximumForwardError = 0.0f;
        float maximumUpError = 0.0f;
        float lastCenterX = 0.0f;
        float lastCenterY = 0.0f;
        int completedEyes = 0;
        int uncapturedEyes = 0;
        bool eyeActive = false;
        bool restoreSucceeded = false;
        Halo4StereoRejection rejection = Halo4StereoRejection::None;

        Halo4InvalidatePublishedFrame();
        frameStarted = true;
        // The model-skinning detour runs synchronously inside the two wrapper
        // calls below. Publish this exact prepared-serial tracking sample only
        // for that scope; no palette hook takes the tracking lock or re-reads a
        // controller between the two eyes.
        g_halo4RigTracking = snapshot;
        g_halo4RigTrackingActive = true;
        __try
        {
            // Freeze both physical wrist targets before either eye can reach
            // model_skinning. Hand-to-held motion is captured and consumed
            // inside each eye's current ordered record sequence.
            Halo4BeginFloatingPair();
            if (!authoredTheater)
                Halo4RenderScope(element, view, window, args, savedObserver, controllerRoot, calibration);
            const int firstEye = g_config.right_eye_first ? 1 : 0;
            for (int pass = 0; pass < 2; ++pass)
            {
                const int eye = pass == 0 ? firstEye : 1 - firstEye;
                unsigned char eyeObserver[kHalo4ObserverSnapshotBytes];
                memcpy(eyeObserver, savedObserver, sizeof(eyeObserver));
                // This substitutes pose/basis, and (once the calibration
                // is learned) the vertical FOV input. +0x7C stays stock.
                memcpy(eyeObserver + kHalo4ObserverPositionOffset, eyeCameras[eye].position,
                    sizeof(eyeCameras[eye].position));
                memcpy(eyeObserver + kHalo4ObserverForwardOffset, eyeCameras[eye].forward,
                    sizeof(eyeCameras[eye].forward));
                memcpy(eyeObserver + kHalo4ObserverUpOffset, eyeCameras[eye].up, sizeof(eyeCameras[eye].up));
                // Widen the vertical FOV input so the raster covers the
                // headset's own frustum. +0x7C (the FOV ratio) stays stock:
                // retail converter 0x38F094-0x38F0AC scales both fields by one
                // shared factor, so the vertical write is the whole handle and
                // touching the ratio as well would double-apply.
                if (widenFov[eye])
                {
                    memcpy(eyeObserver + kHalo4ObserverVerticalFovOffset, &verticalFovWrite[eye],
                        sizeof(verticalFovWrite[eye]));
                }
                if (!Halo4SafeWrite(reinterpret_cast<void*>(args.observer), eyeObserver, sizeof(eyeObserver)))
                {
                    rejection = Halo4StereoRejection::ObserverUnreadable;
                    break;
                }

                g_stereoEye = eye;
                VR_BeginRasterEye(eye);
                eyeActive = true;
                g_halo4OrigSetup(args.view, args.window, args.count, args.mode, args.user, args.observer);

                float actualPosition[3];
                float actualForward[3];
                float actualUp[3];
                float projection[16];
                if (!Halo4SafeRead(reinterpret_cast<const void*>(element + kHalo4ElementPositionOffset), actualPosition,
                        sizeof(actualPosition)) ||
                    !Halo4SafeRead(reinterpret_cast<const void*>(element + kHalo4ElementForwardOffset), actualForward,
                        sizeof(actualForward)) ||
                    !Halo4SafeRead(
                        reinterpret_cast<const void*>(element + kHalo4ElementUpOffset), actualUp, sizeof(actualUp)) ||
                    !Halo4SafeRead(reinterpret_cast<const void*>(element + kHalo4ElementProjectionMatrixOffset),
                        projection, sizeof(projection)))
                {
                    rejection = Halo4StereoRejection::ObserverUnreadable;
                    break;
                }

                for (int axis = 0; axis < 3; ++axis)
                {
                    const float positionError = fabsf(actualPosition[axis] - eyeCameras[eye].position[axis]);
                    const float forwardError = fabsf(actualForward[axis] - eyeCameras[eye].forward[axis]);
                    const float upError = fabsf(actualUp[axis] - eyeCameras[eye].up[axis]);
                    if (positionError > maximumPositionError)
                        maximumPositionError = positionError;
                    if (forwardError > maximumForwardError)
                        maximumForwardError = forwardError;
                    if (upError > maximumUpError)
                        maximumUpError = upError;
                }
                g_halo4Camera.lastPositionError.store(maximumPositionError, std::memory_order_relaxed);
                g_halo4Camera.lastForwardError.store(maximumForwardError, std::memory_order_relaxed);
                g_halo4Camera.lastUpError.store(maximumUpError, std::memory_order_relaxed);
                if (!Halo4CameraOutputMatches(eyeCameras[eye], actualPosition, actualForward, actualUp))
                {
                    rejection = Halo4StereoRejection::CameraClaimMismatch;
                    break;
                }
                ++completedEyes;
                g_halo4Camera.cameraClaims.fetch_add(1, std::memory_order_relaxed);

                float centerX = 0.0f;
                float centerY = 0.0f;
                if (!Halo4DecodeSymmetricProjectionHalfFovs(projection, halfFovX[eye], halfFovY[eye], centerX, centerY))
                {
                    g_halo4Camera.lastProjectionCenterX.store(centerX, std::memory_order_relaxed);
                    g_halo4Camera.lastProjectionCenterY.store(centerY, std::memory_order_relaxed);
                    rejection = Halo4StereoRejection::ProjectionInvalid;
                    break;
                }
                lastCenterX = centerX;
                lastCenterY = centerY;
                g_halo4Camera.projectionReadbacks.fetch_add(1, std::memory_order_relaxed);
                // Optional DLSS: the camera and the row-vector projection the
                // engine holds for this eye, exactly as read back above (a
                // proven perspective with zero centre terms). Halo 4 builds
                // its projection inside setup, so no jitter is written into
                // it: DLSS runs unjittered on this title until that write
                // path is proven.
                if (VR_DlssWantsEyeCamera())
                {
                    VrEyeCameraSample sample{};
                    memcpy(sample.position, actualPosition, sizeof(sample.position));
                    memcpy(sample.forward, actualForward, sizeof(sample.forward));
                    memcpy(sample.up, actualUp, sizeof(sample.up));
                    memcpy(sample.projection, projection, sizeof(sample.projection));
                    VR_PublishEyeCamera(eye, sample);
                }

                // Learn from what the engine ACTUALLY built, whether or
                // not we widened. This is what makes the cover self-correcting
                // instead of resting on an assumed field meaning: if the engine
                // answers a write differently than expected, the next frame's
                // solve already uses the corrected gain and ratio.
                const float writtenVerticalFov = widenFov[eye] ? verticalFovWrite[eye] : stock.verticalFov;
                // Measure retail's converter scale from the engine's own two
                // values, rather than trusting the 0.785 constant read out of
                // the image.
                float elementVerticalFov = 0.0f;
                if (Halo4SafeRead(reinterpret_cast<const void*>(element + kHalo4ElementVerticalFovOffset),
                        &elementVerticalFov, sizeof(elementVerticalFov)) &&
                    isfinite(elementVerticalFov) && writtenVerticalFov > 1.0e-4f)
                {
                    g_halo4Camera.lastObserverVerticalFov.store(writtenVerticalFov, std::memory_order_relaxed);
                    g_halo4Camera.lastElementVerticalFov.store(elementVerticalFov, std::memory_order_relaxed);
                    g_halo4Camera.lastConverterScale.store(
                        elementVerticalFov / writtenVerticalFov, std::memory_order_relaxed);
                }
                if (Halo4LearnFovCalibration(writtenVerticalFov, halfFovX[eye], halfFovY[eye], calibration))
                {
                    g_halo4Camera.fovGain.store(calibration.gain, std::memory_order_relaxed);
                    g_halo4Camera.fovRatio.store(calibration.ratio, std::memory_order_relaxed);
                    g_halo4Camera.fovLearned.store(true, std::memory_order_release);
                }
                if (snapshot.eyes[eye].fovValid)
                {
                    const bool contained = Halo4CoverContainsFov(halfFovX[eye], halfFovY[eye], snapshot.eyes[eye].fov);
                    g_halo4Camera.lastCoverContained.store(contained, std::memory_order_relaxed);
                }
                if (widenFov[eye])
                {
                    g_halo4Camera.fovWidenedFrames.fetch_add(1, std::memory_order_relaxed);
                    g_halo4Camera.lastRequestedHalfY.store(expectedHalfY[eye], std::memory_order_relaxed);
                }
                else
                {
                    g_halo4Camera.fovStockFovFrames.fetch_add(1, std::memory_order_relaxed);
                }

                // Testing proved the old animation-mirror write was not a
                // visible consumer, so it remains absent. Publish this eye root
                // before the wrapper; the final-palette path owns the visible hands and held
                // model later at their exact final-palette callbacks.
                Halo4PublishEyeRoot(eye, eyeCameras[eye]);
                Halo4BeginFloatingEye(eye);
                // Placement is owned entirely by the existing OpenXR reticle
                // quad. No CUI screen coordinate or game-space target enters
                // the authored-art transaction.
                Halo4BeginCuiReticleEye(eye, snapshot.preparedSerial);
                __try
                {
                    g_halo4OrigWrapper(element, view, window);
                }
                __finally
                {
                    // The type-0x29 marker normally closes the redirect. This
                    // same-thread scope exit is the last-resort state restore
                    // for malformed/exceptional command streams.
                    Halo4FinishCuiReticleEye();
                }
                if (!VR_CaptureHalo4RenderedEye(eye, snapshot.preparedSerial))
                {
                    ++uncapturedEyes;
                }
                VR_EndRasterEye();
                eyeActive = false;
            }
        }
        __finally
        {
            Halo4FinishCuiReticleEye();
            Halo4EndFloatingPair();
            g_halo4RigTrackingActive = false;
            if (eyeActive)
                VR_EndRasterEye();
            g_stereoEye = -1;
            restoreSucceeded = Halo4RestoreMonoCamera(args, savedObserver) != 0;
        }

        if (!restoreSucceeded)
            rejection = Halo4StereoRejection::RestoreFailed;
        else if (rejection == Halo4StereoRejection::None && completedEyes == 2 && uncapturedEyes != 0)
            rejection = Halo4StereoRejection::EyeCaptureMissing;

        if (restoreSucceeded && completedEyes == 2 && uncapturedEyes == 0 && rejection == Halo4StereoRejection::None)
        {
            for (int eye = 0; eye < 2; ++eye)
            {
                g_halo4RenderHalfFovX[eye].store(halfFovX[eye], std::memory_order_relaxed);
                g_halo4RenderHalfFovY[eye].store(halfFovY[eye], std::memory_order_relaxed);
                g_halo4RenderFovSerial[eye].store(snapshot.preparedSerial, std::memory_order_release);
            }
            g_halo4Camera.consecutiveUncaptured.store(0, std::memory_order_relaxed);
            g_halo4Camera.lastPositionError.store(maximumPositionError, std::memory_order_relaxed);
            g_halo4Camera.lastForwardError.store(maximumForwardError, std::memory_order_relaxed);
            g_halo4Camera.lastUpError.store(maximumUpError, std::memory_order_relaxed);
            g_halo4Camera.lastProjectionCenterX.store(lastCenterX, std::memory_order_relaxed);
            g_halo4Camera.lastProjectionCenterY.store(lastCenterY, std::memory_order_relaxed);
            g_halo4Camera.lastHalfFovX.store(halfFovX[1], std::memory_order_relaxed);
            g_halo4Camera.lastHalfFovY.store(halfFovY[1], std::memory_order_relaxed);
            g_halo4Camera.stereoFrames.fetch_add(1, std::memory_order_relaxed);
            return Halo4StereoResult::Complete;
        }

        if (uncapturedEyes)
        {
            g_halo4Camera.uncapturedEyes.fetch_add(static_cast<uint64_t>(uncapturedEyes), std::memory_order_relaxed);
            if (rejection == Halo4StereoRejection::EyeCaptureMissing)
            {
                g_halo4Camera.consecutiveUncaptured.fetch_add(
                    static_cast<uint32_t>(uncapturedEyes), std::memory_order_relaxed);
            }
        }
        Halo4InvalidatePublishedFrame();
        if (rejection == Halo4StereoRejection::None)
            rejection = Halo4StereoRejection::TransactionException;
        Halo4NoteRejection(rejection, true);
        return Halo4StereoResult::StartedFrameDropped;
    }

    void Halo4WrapperBody(uintptr_t element, uintptr_t view, uint32_t window, uintptr_t returnAddress)
    {
        const Halo4SetupArgs args = g_halo4SetupArgs;
        g_halo4SetupArgs.valid = false;

        // Headset-accepted pause ownership: while the compositor
        // targets the head-locked pause screen, run Halo 4's stock wrapper
        // once. The stereo core stays armed, but it must not consume the
        // native start-menu backbuffer as a per-eye scene transaction.
        if (VR_IsPausePresentationTarget())
        {
            g_halo4Camera.stockFrames.fetch_add(1, std::memory_order_relaxed);
            g_halo4OrigWrapper(element, view, window);
            return;
        }

        if (g_halo4Camera.sceneTargetMissing.load(std::memory_order_acquire))
        {
            // Rendering twice and capturing nothing is pure cost with a black
            // headset at the end of it. Stop claiming the transaction until a
            // new module generation re-earns it.
            Halo4NoteRejection(Halo4StereoRejection::SceneTargetMissing, false);
            g_halo4OrigWrapper(element, view, window);
            return;
        }
        if (!g_halo4Camera.armed.load(std::memory_order_acquire) || !VR_IsStereoEnabled() || !g_enabled.load())
        {
            Halo4NoteRejection(Halo4StereoRejection::NotArmed, false);
            g_halo4OrigWrapper(element, view, window);
            return;
        }
        if (returnAddress != g_halo4Camera.wrapperReturnAddress || element != g_halo4Camera.elementAddress)
        {
            Halo4NoteRejection(Halo4StereoRejection::ForeignCaller, false);
            g_halo4OrigWrapper(element, view, window);
            return;
        }
        // Halo 4 renders up to four windows. Only the single-player window
        // owns the headset; a split-screen guest keeps its stock flat render.
        if (window != 0)
        {
            Halo4NoteRejection(Halo4StereoRejection::SplitScreenWindow, false);
            g_halo4OrigWrapper(element, view, window);
            return;
        }
        // The immediately preceding setup call must be the one for THIS
        // window. Anything else means the loop shape is not what the evidence
        // describes, and the eye loop must not run.
        if (!args.valid || args.view != view || args.window != window || !args.observer)
        {
            Halo4NoteRejection(Halo4StereoRejection::SetupArgsMissing, false);
            g_halo4OrigWrapper(element, view, window);
            return;
        }

        Halo4StereoResult result = Halo4StereoResult::NotStarted;
        bool frameStarted = false;
        bool transactionException = false;
        __try
        {
            result = Halo4StereoTransaction(element, view, window, args, frameStarted);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("the Halo 4 stereo transaction faulted");
            transactionException = true;
            result = frameStarted ? Halo4StereoResult::StartedFrameDropped : Halo4StereoResult::NotStarted;
        }
        if (transactionException)
        {
            Halo4InvalidatePublishedFrame();
            Halo4NoteRejection(Halo4StereoRejection::TransactionException, frameStarted);
        }
        if (result == Halo4StereoResult::NotStarted)
        {
            // Ownership never began, so the untouched stock wrapper is safe.
            g_stereoEye = -1;
            g_halo4OrigWrapper(element, view, window);
        }
        // StartedFrameDropped intentionally does not re-run the wrapper. The
        // next prepared frame retries with the camera core still armed.
    }

    __declspec(noinline) void __fastcall Halo4WrapperDetour(uintptr_t element, uintptr_t view, uint32_t window)
    {
        const uintptr_t returnAddress = reinterpret_cast<uintptr_t>(_ReturnAddress());
        g_halo4Camera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
        __try
        {
            Halo4WrapperBody(element, view, window, returnAddress);
        }
        __finally
        {
            g_halo4Camera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
        }
    }

    bool PublishHalo4Lifecycle()
    {
        const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::Halo4);
        if (!generation)
            return false;
        TitleRuntimeLifecycle lifecycle{};
        lifecycle.installed = g_halo4Camera.installed.load(std::memory_order_acquire);
        lifecycle.armed = g_halo4Camera.armed.load(std::memory_order_acquire);
        lifecycle.teardownRequested = g_halo4Camera.teardownRequested.load(std::memory_order_acquire);
        lifecycle.enabledCapabilities =
            lifecycle.installed && !lifecycle.teardownRequested ? kHalo4RuntimeCapabilities : TitleCapability_None;
        if (!g_halo4Camera.cinematicTheaterProof.load(std::memory_order_acquire))
        {
            lifecycle.enabledCapabilities &= ~TitleCapability_CutsceneTheater;
        }
        if (!g_halo4Restoration.hudInstalled.load(std::memory_order_acquire))
        {
            lifecycle.enabledCapabilities &= ~TitleCapability_Hud;
        }
        // Publish only on a real state change: republishing identical state
        // every poll keeps the shared snapshot permanently "pending", which is
        // the Reach "gameplay -> loading" flap.
        static uint32_t publishedGeneration = 0;
        static uint32_t publishedState = 0xFFFFFFFFu;
        const uint32_t state = (lifecycle.installed ? 1u : 0u) | (lifecycle.armed ? 2u : 0u) |
                               (lifecycle.teardownRequested ? 4u : 0u) | (lifecycle.enabledCapabilities << 3);
        if (generation == publishedGeneration && state == publishedState)
            return true;
        const bool published = TitleAdapter_PublishLifecycle(GameTitle::Halo4, generation, lifecycle);
        if (published)
        {
            publishedGeneration = generation;
            publishedState = state;
        }
        return published;
    }

    bool InstallHalo4Vrik(uintptr_t base, size_t size)
    {
        g_halo4RuntimeWeaponBoundsProven.store(false, std::memory_order_release);
        g_halo4VehicleIdentityProven.store(false, std::memory_order_release);
        g_halo4RenderModelTagIndexPointerSlot = 0;
        g_halo4RenderModelGroupBaseTable = 0;
        const uintptr_t hit = sig::Find(base, size, kHalo4ModelSkinningPattern);
        const bool unique = hit && !sig::Find(hit + 1, base + size - hit - 1, kHalo4ModelSkinningPattern);
        if (!unique || hit - base != kHalo4ModelSkinningRva)
        {
            LOG("Halo 4 floating hands: final model-skinning consumer is %s (RVA 0x%zX, "
                "pinned 0x%X); stock hands remain and camera core stays armed",
                hit ? (unique ? "moved" : "ambiguous") : "unavailable", hit ? hit - base : 0, kHalo4ModelSkinningRva);
            return false;
        }
        const uintptr_t producerHit = sig::Find(base, size, kHalo4FirstPersonProducerPattern);
        const bool producerUnique =
            producerHit && producerHit - base == kHalo4FirstPersonProducerRva &&
            !sig::Find(producerHit + 1, base + size - producerHit - 1, kHalo4FirstPersonProducerPattern);

        const auto findUniqueAt = [&](const char* pattern, uint32_t expectedRva)
        {
            const uintptr_t found = sig::Find(base, size, pattern);
            return found && found - base == expectedRva && !sig::Find(found + 1, base + size - found - 1, pattern)
                       ? found
                       : uintptr_t{0};
        };
        const uintptr_t lookup = findUniqueAt(kHalo4ModelSkinningTagLookupPattern, kHalo4ModelSkinningTagLookupRva);
        const uintptr_t nodeCount = findUniqueAt(kHalo4ModelSkinningNodeCountPattern, kHalo4ModelSkinningNodeCountRva);
        const uintptr_t reload =
            findUniqueAt(kHalo4ModelSkinningTagLookupReloadPattern, kHalo4ModelSkinningTagLookupReloadRva);
        const auto ripTarget = [](uintptr_t instruction) -> uintptr_t
        {
            int32_t displacement = 0;
            if (!instruction ||
                !Halo4SafeRead(reinterpret_cast<const void*>(instruction + 3), &displacement, sizeof(displacement)))
                return 0;
            return instruction + 7 + static_cast<intptr_t>(displacement);
        };
        const uintptr_t groupTableA = lookup ? ripTarget(lookup + 4) : 0;
        const uintptr_t tagPointerA = lookup ? ripTarget(lookup + 11) : 0;
        const uintptr_t tagPointerB = reload ? ripTarget(reload) : 0;
        const uintptr_t groupTableB = reload ? ripTarget(reload + 7) : 0;
        if (!lookup || !nodeCount || !reload || tagPointerA != tagPointerB || groupTableA != groupTableB ||
            tagPointerA != base + kHalo4RenderModelTagIndexPointerRva ||
            groupTableA != base + kHalo4RenderModelGroupBaseTableRva)
        {
            LOG("Halo 4 floating hands: exact render-model nodes.count resolver "
                "did not prove its three pinned windows/globals; hands and gun "
                "stay stock while the camera core remains armed");
            return false;
        }
        g_halo4RenderModelTagIndexPointerSlot = tagPointerA;
        g_halo4RenderModelGroupBaseTable = groupTableA;
        const bool vehicleIdentityProven = Halo4ProveVehicleIdentity(base, size);
        g_halo4VehicleIdentityProven.store(vehicleIdentityProven, std::memory_order_release);
        LOG("Halo 4 vehicle profiles: %s", vehicleIdentityProven ? "native parent model/checksum chain verified"
                                                                 : "identity unavailable; per-game settings retained");
        const bool weaponBoundsProven = Halo4ProveRuntimeWeaponBounds(base, size);
        g_halo4RuntimeWeaponBoundsProven.store(weaponBoundsProven, std::memory_order_release);
        LOG("Halo 4 equipped-model collision bounds: %s; geometry failure stays local to weapon contact",
            weaponBoundsProven ? "native model/compression consumers verified; live bounds preferred over stock catalog"
                               : "native proof unavailable; existing catalog fallback retained");

        const GatedHook hook = InstallGatedHook(g_halo4Camera.modelSkinningTarget, reinterpret_cast<void*>(hit),
            reinterpret_cast<void*>(&Halo4ModelSkinningDetour), reinterpret_cast<void**>(&g_halo4OrigModelSkinning));
        if (!hook.enabled)
        {
            g_halo4RuntimeWeaponBoundsProven.store(false, std::memory_order_release);
            g_halo4VehicleIdentityProven.store(false, std::memory_order_release);
            g_halo4RenderModelTagIndexPointerSlot = 0;
            g_halo4RenderModelGroupBaseTable = 0;
            if (hook.created != MH_OK)
                LOG("Halo 4 floating hands: MinHook rejected the optional final-palette hook; "
                    "stock hands remain and camera core stays armed");
            else
                LOG("Halo 4 floating hands: optional final-palette hook could not be enabled "
                    "(cleanup %s); stock hands remain and camera core stays armed",
                    hook.retained ? "retained for verified teardown" : "complete");
            return false;
        }
        // Optional: a failed enable whose removal MinHook cannot confirm keeps
        // the target and original for the verified teardown retry.
        GatedHook producerHook{};
        if (producerUnique)
            producerHook = InstallGatedHook(g_halo4Camera.firstPersonProducerTarget,
                reinterpret_cast<void*>(producerHit), reinterpret_cast<void*>(&Halo4FirstPersonProducerDetour),
                reinterpret_cast<void**>(&g_halo4OrigFirstPersonProducer));
        if (!producerHook.enabled)
            LOG("Halo 4 experimental avatar: exact post-mask first-person producer is unavailable%s; avatar stays stock, hands/camera remain active",
                producerHook.retained ? " (failed hook retained for verified teardown)" : "");
        else
            LOG("Halo 4 experimental avatar: exact local first-person packet producer hooked; feature defaults off and requires 120-node model identity");
        uint32_t epoch = g_halo4Camera.floatingHandsEpoch.fetch_add(1, std::memory_order_acq_rel) + 1;
        if (!epoch)
        {
            epoch = 1;
            g_halo4Camera.floatingHandsEpoch.store(epoch, std::memory_order_release);
        }
        LOG("Halo 4 marker-parity free / rigid two-hand support: final palette 0x%X hooked; only "
            "return 0x%X is admitted; %d bank transforms are privately copied "
            "and argument 7 is never treated as a node count; H4EK/retail "
            "render-model checksum/nodes.count is read exactly; epoch %u has "
            "one no-IK path: current-eye Storm80 hands, immediately following "
            "held model, then untouched flag-0 native body",
            kHalo4ModelSkinningRva, kHalo4FirstPersonSkinningReturnRva, kHalo4FirstPersonBankTransforms, epoch);
        return true;
    }

    bool Halo4CuiReticleExecutableAddress(uintptr_t address)
    {
        MEMORY_BASIC_INFORMATION info{};
        if (!address || VirtualQuery(reinterpret_cast<const void*>(address), &info, sizeof(info)) != sizeof(info) ||
            info.State != MEM_COMMIT || (info.Protect & PAGE_GUARD))
            return false;
        const DWORD protection = info.Protect & 0xFFu;
        return protection == PAGE_EXECUTE || protection == PAGE_EXECUTE_READ || protection == PAGE_EXECUTE_READWRITE ||
               protection == PAGE_EXECUTE_WRITECOPY;
    }

    bool CleanupHalo4CuiReticleFeature()
    {
        g_halo4Camera.cuiReticleInstalled.store(false, std::memory_order_release);
        for (auto& serial : g_halo4Camera.cuiReticleEyeSerial)
            serial.store(0, std::memory_order_release);
        void* const dispatcherTarget = g_halo4Camera.cuiReticleTarget;
        void* const gameplayTarget = g_halo4Camera.cuiReticleGameplayTarget;
        if (!dispatcherTarget && !gameplayTarget)
        {
            g_halo4Camera.cuiReticleCleanupRequired = false;
            g_halo4OrigCuiRenderCommand = nullptr;
            g_halo4OrigCuiGameplayRender = nullptr;
            return true;
        }
        // Stop new inner command callbacks before closing the outer gameplay
        // scope. Its finally block restores any redirect already in progress.
        if (dispatcherTarget)
            MH_DisableHook(dispatcherTarget);
        if (gameplayTarget)
            MH_DisableHook(gameplayTarget);
        if (g_halo4Camera.activeCallbacks.load(std::memory_order_acquire) != 0)
            return false;

        const MH_STATUS dispatcherRemoved =
            dispatcherTarget ? MCCVR_RemoveHookChecked(dispatcherTarget) : MH_ERROR_NOT_CREATED;
        const MH_STATUS gameplayRemoved =
            gameplayTarget ? MCCVR_RemoveHookChecked(gameplayTarget) : MH_ERROR_NOT_CREATED;
        const bool dispatcherClean = dispatcherRemoved == MH_OK || dispatcherRemoved == MH_ERROR_NOT_CREATED;
        const bool gameplayClean = gameplayRemoved == MH_OK || gameplayRemoved == MH_ERROR_NOT_CREATED;
        if (!dispatcherClean || !gameplayClean)
        {
            g_halo4Camera.cuiReticleCleanupRequired = true;
            LOG("Halo 4 CUI reticle: optional two-hook cleanup "
                "needs retry (dispatcher=%d gameplay=%d); retaining targets, "
                "trampolines, and halo4.dll pin while camera/OpenXR stay "
                "independent",
                static_cast<int>(dispatcherRemoved), static_cast<int>(gameplayRemoved));
            return false;
        }
        g_halo4Camera.cuiReticleTarget = nullptr;
        g_halo4Camera.cuiReticleGameplayTarget = nullptr;
        g_halo4OrigCuiRenderCommand = nullptr;
        g_halo4OrigCuiGameplayRender = nullptr;
        g_halo4Camera.cuiReticleCleanupRequired = false;
        LOG("Halo 4 CUI reticle: optional two-hook cleanup complete; "
            "camera core and OpenXR remained armed");
        return true;
    }

    Halo4CuiReticleOptionalInstallState InstallHalo4CuiReticle(uintptr_t base, size_t size, uint32_t generation)
    {
        if (g_halo4Camera.cuiReticleInstalled.load(std::memory_order_acquire))
        {
            if (Halo4CuiReticleTransformLive())
                return Halo4CuiReticleOptionalInstallState::Installed;
            g_halo4Camera.cuiReticleInstalled.store(false, std::memory_order_release);
            g_halo4Camera.cuiReticleCleanupRequired = true;
            return Halo4CuiReticleOptionalInstallState::CleanupRequired;
        }
        if (!g_halo4Camera.installed.load(std::memory_order_acquire) || !base || !generation ||
            base != g_halo4Camera.base || generation != g_halo4Camera.generation.load(std::memory_order_acquire))
            return Halo4CuiReticleOptionalInstallState::StockFallback;
        if (g_halo4Camera.cuiReticleCleanupRequired)
            return Halo4CuiReticleOptionalInstallState::CleanupRequired;
        if (g_halo4Camera.cuiReticleRejectedGeneration == generation)
            return Halo4CuiReticleOptionalInstallState::StockFallback;

        if (!D3D_Halo4AuthoredReticleDrawPathAvailable())
        {
            static uint32_t drawPathWaitLoggedGeneration = 0;
            if (drawPathWaitLoggedGeneration != generation)
            {
                drawPathWaitLoggedGeneration = generation;
                LOG("Halo 4 authored CUI reticle: draw-time framing "
                    "hooks are unavailable; native/procedural reticle stays "
                    "stock while camera, theatre and OpenXR remain armed");
            }
            return Halo4CuiReticleOptionalInstallState::StockFallback;
        }

        if (!VR_CanPrepareAuthoredReticleResources())
            return Halo4CuiReticleOptionalInstallState::StockFallback;
        constexpr uint64_t kHalo4CuiResourceRetryMs = 500;
        const uint64_t resourceAttemptMs = GetTickCount64();
        if (g_halo4Camera.cuiReticleLastResourceAttemptMs != 0 &&
            resourceAttemptMs - g_halo4Camera.cuiReticleLastResourceAttemptMs < kHalo4CuiResourceRetryMs)
        {
            return Halo4CuiReticleOptionalInstallState::StockFallback;
        }
        g_halo4Camera.cuiReticleLastResourceAttemptMs = resourceAttemptMs;
        const AuthoredReticlePreparationResult prepared = VR_PrepareAuthoredReticleResources();
        const bool capturePrepared =
            prepared == AuthoredReticlePreparationResult::Ready && VR_PrepareAuthoredReticleSuppressionResources();
        if (!capturePrepared)
        {
            if (g_halo4Camera.cuiReticleResourceWaitLoggedGeneration != generation)
            {
                g_halo4Camera.cuiReticleResourceWaitLoggedGeneration = generation;
                LOG("Halo 4 authored CUI reticle: capture resources "
                    "not ready (prepare=%u); keeping stock/procedural fallback "
                    "and retrying without disarming camera/OpenXR",
                    static_cast<unsigned>(prepared));
            }
            return Halo4CuiReticleOptionalInstallState::StockFallback;
        }
        Halo4CuiReticleInstallProof proof{};
        proof.transformLayoutProven = true;

        const auto findAnchor = [&](const char* pattern, uint32_t pinnedRva)
        {
            const uintptr_t first = sig::Find(base, size, pattern);
            const bool unique = first && !sig::Find(first + 1, base + size - first - 1, pattern);
            if (unique)
            {
                ++proof.anchorsMatchedOnce;
                if (first - base == pinnedRva)
                    ++proof.anchorsAtPinnedRva;
            }
            return unique ? first : uintptr_t{0};
        };
        const uintptr_t dispatcher = findAnchor(kHalo4CuiReticleDispatcherEntryAob, kHalo4CuiReticleDispatcherRva);
        const uintptr_t caller = findAnchor(kHalo4CuiReticleCallerAob, kHalo4CuiReticleCallerRva);
        const uintptr_t gameplayRender = findAnchor(kHalo4CuiGameplayRenderEntryAob, kHalo4CuiGameplayRenderRva);
        const uintptr_t gameplayCaller = findAnchor(kHalo4CuiGameplayCallerAob, kHalo4CuiGameplayCallerRva);
        const auto decodeRel32Call =
            [&](uintptr_t callSite, size_t opcodeOffset, size_t displacementOffset, size_t nextOffset)
        {
            uint8_t opcode = 0;
            int32_t displacement = 0;
            if (!callSite ||
                !Halo4SafeRead(reinterpret_cast<const void*>(callSite + opcodeOffset), &opcode, sizeof(opcode)) ||
                opcode != 0xE8 ||
                !Halo4SafeRead(
                    reinterpret_cast<const void*>(callSite + displacementOffset), &displacement, sizeof(displacement)))
            {
                return uintptr_t{0};
            }
            return callSite + nextOffset + static_cast<intptr_t>(displacement);
        };
        const uintptr_t decodedDispatcher = decodeRel32Call(caller, kHalo4CuiReticleCallerCallOpcodeOffset,
            kHalo4CuiReticleCallerCallDisplacementOffset, kHalo4CuiReticleCallerCallNextOffset);
        const uintptr_t decodedGameplayRender = decodeRel32Call(gameplayCaller, kHalo4CuiGameplayCallerCallOpcodeOffset,
            kHalo4CuiGameplayCallerCallDisplacementOffset, kHalo4CuiGameplayCallerCallNextOffset);
        proof.callerDecodesDispatcher =
            dispatcher && decodedDispatcher == dispatcher && decodedDispatcher >= base &&
            decodedDispatcher < base + size &&
            Halo4CuiReticleCallerTargetsDispatcher(static_cast<uint32_t>(decodedDispatcher - base));
        proof.gameplayCallerDecodesRender =
            gameplayRender && decodedGameplayRender == gameplayRender && decodedGameplayRender >= base &&
            decodedGameplayRender < base + size &&
            Halo4CuiGameplayCallerTargetsRender(static_cast<uint32_t>(decodedGameplayRender - base));
        proof.executableRange =
            dispatcher && caller && gameplayRender && gameplayCaller && kHalo4CuiReticleDispatcherRva < size &&
            kHalo4CuiReticleCallerRva < size && kHalo4CuiGameplayRenderRva < size &&
            kHalo4CuiGameplayCallerRva < size && Halo4CuiReticleExecutableAddress(dispatcher) &&
            Halo4CuiReticleExecutableAddress(caller) && Halo4CuiReticleExecutableAddress(gameplayRender) &&
            Halo4CuiReticleExecutableAddress(gameplayCaller);
        proof.mappingStable = GetModuleHandleW(L"halo4.dll") == reinterpret_cast<HMODULE>(base);
        if (!Halo4CuiReticleInstallComplete(proof))
        {
            g_halo4Camera.cuiReticleRejectedGeneration = generation;
            LOG("Halo 4 CUI reticle REFUSED: transform=%d "
                "anchorsOnce=%u/%u anchorsPinned=%u/%u edges=%d/%d range=%d "
                "mapping=%d; reticles stay on the stock/procedural fallback "
                "and camera core stays armed",
                proof.transformLayoutProven ? 1 : 0, proof.anchorsMatchedOnce, kHalo4CuiReticleAnchorCount,
                proof.anchorsAtPinnedRva, kHalo4CuiReticleAnchorCount, proof.callerDecodesDispatcher ? 1 : 0,
                proof.gameplayCallerDecodesRender ? 1 : 0, proof.executableRange ? 1 : 0, proof.mappingStable ? 1 : 0);
            return Halo4CuiReticleOptionalInstallState::StockFallback;
        }

        Halo4CuiGameplayRenderFn originalGameplay = nullptr;
        const MH_STATUS gameplayCreated = MCCVR_CreateHookChecked(reinterpret_cast<void*>(gameplayRender),
            reinterpret_cast<void*>(&Halo4CuiGameplayRenderDetour), reinterpret_cast<void**>(&originalGameplay));
        if (gameplayCreated != MH_OK)
        {
            g_halo4Camera.cuiReticleRejectedGeneration = generation;
            LOG("Halo 4 CUI reticle: optional gameplay-scope hook "
                "creation failed (%d); native/procedural reticles stay stock "
                "and camera core stays armed",
                static_cast<int>(gameplayCreated));
            return Halo4CuiReticleOptionalInstallState::StockFallback;
        }
        g_halo4Camera.cuiReticleGameplayTarget = reinterpret_cast<void*>(gameplayRender);
        g_halo4OrigCuiGameplayRender = originalGameplay;

        Halo4CuiRenderCommandFn originalDispatcher = nullptr;
        const MH_STATUS dispatcherCreated = MCCVR_CreateHookChecked(reinterpret_cast<void*>(dispatcher),
            reinterpret_cast<void*>(&Halo4CuiRenderCommandDetour), reinterpret_cast<void**>(&originalDispatcher));
        if (dispatcherCreated != MH_OK)
        {
            g_halo4Camera.cuiReticleRejectedGeneration = generation;
            const bool cleaned = CleanupHalo4CuiReticleFeature();
            LOG("Halo 4 CUI reticle: optional dispatcher hook "
                "creation failed (%d); %s stock fallback retained and camera "
                "core stays armed",
                static_cast<int>(dispatcherCreated), cleaned ? "clean" : "cleanup-pending");
            return cleaned ? Halo4CuiReticleOptionalInstallState::StockFallback
                           : Halo4CuiReticleOptionalInstallState::CleanupRequired;
        }
        g_halo4Camera.cuiReticleTarget = reinterpret_cast<void*>(dispatcher);
        g_halo4OrigCuiRenderCommand = originalDispatcher;

        const MH_STATUS dispatcherQueued = MH_QueueEnableHook(g_halo4Camera.cuiReticleTarget);
        const MH_STATUS gameplayQueued =
            dispatcherQueued == MH_OK ? MH_QueueEnableHook(g_halo4Camera.cuiReticleGameplayTarget) : MH_UNKNOWN;
        const MH_STATUS applied = dispatcherQueued == MH_OK && gameplayQueued == MH_OK ? MH_ApplyQueued() : MH_UNKNOWN;
        if (dispatcherQueued != MH_OK || gameplayQueued != MH_OK || applied != MH_OK)
        {
            g_halo4Camera.cuiReticleRejectedGeneration = generation;
            const bool cleaned = CleanupHalo4CuiReticleFeature();
            LOG("Halo 4 CUI reticle: atomic optional hook enable "
                "failed (%d/%d/%d); %s stock fallback retained and camera "
                "core stays armed",
                static_cast<int>(dispatcherQueued), static_cast<int>(gameplayQueued), static_cast<int>(applied),
                cleaned ? "clean" : "cleanup-pending");
            return cleaned ? Halo4CuiReticleOptionalInstallState::StockFallback
                           : Halo4CuiReticleOptionalInstallState::CleanupRequired;
        }

        g_halo4Camera.cuiReticleInstalled.store(true, std::memory_order_release);
        LOG("Halo 4 authored CUI reticle installed: gameplay scope "
            "+0x%X (exact caller return +0x%X) and dispatcher +0x%X (sole "
            "caller edge +0x%X) matched uniquely; auxiliary/menu CUI stays "
            "stock; bounded capture replays select the weapon bitmap container, "
            "reject grenade/damage polyart, and retain the draw framing "
            "through the batching boundary; visible passes hide only the flat "
            "type-0x28 copy and the authored pixels ride the existing exact "
            "OpenXR weapon-ray quad",
            kHalo4CuiGameplayRenderRva, kHalo4CuiGameplayCallerReturnRva, kHalo4CuiReticleDispatcherRva,
            kHalo4CuiReticleCallerRva);
        return Halo4CuiReticleOptionalInstallState::Installed;
    }

    std::array<uint8_t, 5> Halo4RelativePatch(uint8_t opcode, uintptr_t source, uintptr_t destination)
    {
        std::array<uint8_t, 5> patch{opcode, 0, 0, 0, 0};
        const int64_t wide = static_cast<int64_t>(destination) - static_cast<int64_t>(source + patch.size());
        const int32_t relative = static_cast<int32_t>(wide);
        if (static_cast<int64_t>(relative) != wide)
            return {};
        std::memcpy(patch.data() + 1, &relative, sizeof(relative));
        return patch;
    }

    std::array<uint8_t, 18> Halo4TransientPatch()
    {
        std::array<uint8_t, 18> patch{};
        patch[0] = 0xFF;
        patch[1] = 0x25;
        const uintptr_t target = reinterpret_cast<uintptr_t>(&Halo4EffectTransientWrapper);
        std::memcpy(patch.data() + 6, &target, sizeof(target));
        std::fill(patch.begin() + 14, patch.end(), uint8_t{0x90});
        return patch;
    }

    std::array<uint8_t, 0xF0> Halo4EffectCavePatch()
    {
        std::array<uint8_t, 0xF0> cave{};
        // clang-format off
        static constexpr uint8_t kNegPayload[] = {
            0x66,0x83,0xFF,0xFE,0x7F,0x12,0x48,0x89,0xE9,0x48,0x89,0xDA,
            0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xD0,0x48,0x8B,0x5C,0x24,
            0x48,0xE9,0x75,0xBD,0x58,0xFF};
        // clang-format on
        // clang-format off
        static constexpr uint8_t kHelperPayload[] = {
            0x48,0x83,0xEC,0x58,0x48,0x89,0x4C,0x24,0x20,0x48,0x89,0x54,
            0x24,0x28,0x4C,0x89,0x44,0x24,0x30,0x4C,0x89,0x4C,0x24,0x38,
            0xE8,0x1B,0xBD,0x58,0xFF,0x4C,0x8B,0x4C,0x24,0x38,0x45,0x84,
            0xC9,0x74,0x30,0x48,0x8B,0x44,0x24,0x28,0x66,0x83,0xF8,0xFE,
            0x7F,0x25,0x48,0x8B,0x4C,0x24,0x20,0xF6,0x41,0x4A,0x0F,0x74,
            0x1A,0x8A,0x41,0x4A,0x24,0xF0,0x3C,0xF0,0x74,0x11,0x48,0x8B,
            0x54,0x24,0x30,0x48,0xB8,0,0,0,0,0,0,0,0,0xFF,0xD0,0x48,
            0x83,0xC4,0x58,0xC3};
        // clang-format on
        std::memcpy(cave.data(), kNegPayload, sizeof(kNegPayload));
        std::memcpy(cave.data() + 0x70, kHelperPayload, sizeof(kHelperPayload));
        const uintptr_t bridge = reinterpret_cast<uintptr_t>(&Halo4EffectHideBridge);
        std::memcpy(cave.data() + 0x0E, &bridge, sizeof(bridge));
        std::memcpy(cave.data() + 0xBD, &bridge, sizeof(bridge));
        return cave;
    }

    std::array<uint8_t, 26> Halo4CurvaturePatch()
    {
        std::array<uint8_t, 26> patch{};
        patch[0] = 0x48;
        patch[1] = 0xB8;
        const uintptr_t target = reinterpret_cast<uintptr_t>(&Halo4CurvatureBridge);
        std::memcpy(patch.data() + 2, &target, sizeof(target));
        patch[10] = 0xFF;
        patch[11] = 0xE0;
        std::fill(patch.begin() + 12, patch.end(), uint8_t{0x90});
        return patch;
    }

    bool CleanupHalo4Effects(uintptr_t base)
    {
        g_halo4EffectsEnabled = 0;
        if (!g_halo4Restoration.effectsTouched)
        {
            g_halo4Restoration.effectsInstalled.store(false, std::memory_order_release);
            g_halo4EffectOriginalHelper = nullptr;
            g_halo4EffectTransientContinue = nullptr;
            return true;
        }
        bool clean = true;
        const auto transient = Halo4TransientPatch();
        const auto cave = Halo4EffectCavePatch();
        const std::array<uint8_t, 0xF0> caveStock{};
        const auto neg = Halo4RelativePatch(0xE9, base + kHalo4EffectNegRva, base + kHalo4EffectCaveRva);
        const auto helper = Halo4RelativePatch(0xE8, base + kHalo4EffectHelperRva, base + kHalo4EffectCaveRva + 0x70);
        if (g_halo4Restoration.effectsTransientPatched)
            clean =
                Halo4RestoreOwnedPatch(base + kHalo4EffectTransientRva, transient, kHalo4EffectTransientStock) && clean;
        if (g_halo4Restoration.effectsHelperPatched)
            clean = Halo4RestoreOwnedPatch(base + kHalo4EffectHelperRva, helper, kHalo4EffectHelperStock) && clean;
        if (g_halo4Restoration.effectsNegPatched)
            clean = Halo4RestoreOwnedPatch(base + kHalo4EffectNegRva, neg, kHalo4EffectNegStock) && clean;
        if (g_halo4Restoration.effectsModeOnePatched)
            clean = Halo4RestoreOwnedPatch(
                        base + kHalo4EffectModeOneRva, kHalo4EffectModeOneHidden, kHalo4EffectModeOneStock) &&
                    clean;
        // The muzzle-effect cave's four entry routes are stock again before the trampoline
        // storage is cleared, so no thread can enter the cave while it is
        // being restored. Previous candidates omitted this final ownership
        // step: the first level worked, but the next install found the cave
        // non-stock and fell back, bringing muzzle effects back after reload.
        clean = Halo4RestoreOwnedPatch(base + kHalo4EffectCaveRva, cave, caveStock) && clean;
        clean = Halo4PatchMatches(base + kHalo4EffectTransientRva, kHalo4EffectTransientStock) &&
                Halo4PatchMatches(base + kHalo4EffectHelperRva, kHalo4EffectHelperStock) &&
                Halo4PatchMatches(base + kHalo4EffectNegRva, kHalo4EffectNegStock) &&
                Halo4PatchMatches(base + kHalo4EffectModeOneRva, kHalo4EffectModeOneStock) &&
                Halo4PatchMatches(base + kHalo4EffectCaveRva, caveStock) && clean;
        if (!clean)
        {
            g_halo4Restoration.cleanupRequired = true;
            return false;
        }
        g_halo4Restoration.effectsTransientPatched = false;
        g_halo4Restoration.effectsHelperPatched = false;
        g_halo4Restoration.effectsNegPatched = false;
        g_halo4Restoration.effectsModeOnePatched = false;
        g_halo4Restoration.effectsTouched = false;
        g_halo4Restoration.effectsInstalled.store(false, std::memory_order_release);
        g_halo4EffectOriginalHelper = nullptr;
        g_halo4EffectTransientContinue = nullptr;
        return true;
    }

    bool InstallHalo4Effects(uintptr_t base, size_t size)
    {
        if (g_halo4Restoration.effectsInstalled.load(std::memory_order_acquire))
            return true;
        uintptr_t negSite = 0, helperSite = 0, transientSite = 0;
        uintptr_t modeOneSite = 0;
        constexpr char kNegSignature[] = "48 8B 5C 24 48 48 8B 6C 24 50 48 83 C4 20 41 5E 5F 5E C3 "
                                         "CC CC CC 48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 40 "
                                         "49 8B F0";
        constexpr char kHelperSignature[] = "E8 CB 4A 00 00 48 8D 53 0C 4C 8B C7 48 8D 4C 24 20 "
                                            "E8 DA C5 01 00 41 B3 01 4C 8B D7";
        constexpr char kTransientSignature[] = "66 41 39 44 B6 18 49 8B D7 41 0F 95 C1 E8 D5 FB FF FF "
                                               "45 8A 0C B6 48 8D 4D D0";
        constexpr char kModeOneSignature[] = "41 8A DA EB 03 41 88 10 E8 ?? ?? ?? ?? 33 D2 41 0F B6 CA";
        if (!Halo4FindUniquePinned(base, size, kNegSignature, kHalo4EffectNegRva, negSite) ||
            !Halo4FindUniquePinned(base, size, kHelperSignature, kHalo4EffectHelperRva, helperSite) ||
            !Halo4FindUniquePinned(base, size, kTransientSignature, kHalo4EffectTransientRva, transientSite) ||
            !Halo4FindUniquePinned(base, size, kModeOneSignature, kHalo4EffectModeOneRva, modeOneSite))
        {
            LOG("Halo 4 effects: StockFallback; signatures were "
                "missing, ambiguous, or moved; camera/HUD stay live");
            return false;
        }

        std::array<uint8_t, 0xF0> caveStock{};
        const auto cave = Halo4EffectCavePatch();
        if (!Halo4WriteExecutable(
                reinterpret_cast<void*>(base + kHalo4EffectCaveRva), caveStock.data(), cave.data(), cave.size()))
        {
            LOG("Halo 4 effects: StockFallback; exact C50 cave was not "
                "untouched/writable; camera/HUD stay live");
            return false;
        }
        g_halo4Restoration.effectsTouched = true;

        g_halo4EffectOriginalHelper = reinterpret_cast<void*>(base + kHalo4EffectOriginalHelperRva);
        g_halo4EffectTransientContinue = reinterpret_cast<void*>(base + kHalo4EffectTransientContinueRva);
        const auto negPatch = Halo4RelativePatch(0xE9, negSite, base + kHalo4EffectCaveRva);
        const auto helperPatch = Halo4RelativePatch(0xE8, helperSite, base + kHalo4EffectCaveRva + 0x70);
        const auto transientPatch = Halo4TransientPatch();
        const bool hideEffects = g_config.hide_muzzle_flash[3];
        bool ok =
            !hideEffects || Halo4WriteExecutable(reinterpret_cast<void*>(modeOneSite), kHalo4EffectModeOneStock.data(),
                                kHalo4EffectModeOneHidden.data(), kHalo4EffectModeOneStock.size());
        g_halo4Restoration.effectsModeOnePatched = Halo4PatchMatches(modeOneSite, kHalo4EffectModeOneHidden);
        if (ok)
        {
            ok = Halo4WriteExecutable(
                reinterpret_cast<void*>(negSite), kHalo4EffectNegStock.data(), negPatch.data(), negPatch.size());
            g_halo4Restoration.effectsNegPatched = Halo4PatchMatches(negSite, negPatch);
        }
        if (ok)
        {
            ok = Halo4WriteExecutable(reinterpret_cast<void*>(helperSite), kHalo4EffectHelperStock.data(),
                helperPatch.data(), helperPatch.size());
            g_halo4Restoration.effectsHelperPatched = Halo4PatchMatches(helperSite, helperPatch);
        }
        if (ok)
        {
            ok = Halo4WriteExecutable(reinterpret_cast<void*>(transientSite), kHalo4EffectTransientStock.data(),
                transientPatch.data(), transientPatch.size());
            g_halo4Restoration.effectsTransientPatched = Halo4PatchMatches(transientSite, transientPatch);
        }
        if (!ok)
        {
            const bool cleaned = CleanupHalo4Effects(base);
            LOG("Halo 4 effects: %s; patch transaction failed and "
                "camera/HUD stay live",
                cleaned ? "StockFallback" : "CleanupRequired");
            return false;
        }
        g_halo4EffectsHidden = 0;
        g_halo4Restoration.effectsInstalled.store(true, std::memory_order_release);
        g_halo4EffectsEnabled = hideEffects ? 1 : 0;
        g_halo4Restoration.effectsSettingPendingLogged = false;
        LOG("Halo 4 effects Installed: exact C50 negative/helper/"
            "transient routes and mode-1 particle fallback; suppression=%s",
            hideEffects ? "enabled" : "disabled");
        return true;
    }

    bool InstallHalo4NativeHud(uintptr_t base, size_t size)
    {
        uintptr_t hudSite = 0, curvatureSite = 0;
        constexpr char kHudSignature[] = "40 55 48 8D 6C 24 A9 48 81 EC 90 00 00 00 4C 63 91 70 08 00 00";
        constexpr char kCurvatureSignature[] = "F3 0F 10 8F E8 01 00 00 F3 0F 11 4D A4 F3 0F 10 8F EC 01 00 00 "
                                               "F3 0F 11 4D AC";
        if (!Halo4FindUniquePinned(base, size, kHudSignature, kHalo4HudRootRva, hudSite))
        {
            LOG("Halo 4 HUD: StockFallback; native CUI root was "
                "missing, ambiguous, or moved; camera/effects stay live");
            return false;
        }
        const GatedHook hook = InstallGatedHook(g_halo4Restoration.hudTarget, reinterpret_cast<void*>(hudSite),
            reinterpret_cast<void*>(&Halo4HudRootDetour), reinterpret_cast<void**>(&g_halo4Restoration.hudOriginal));
        if (!hook.enabled)
        {
            if (hook.created != MH_OK)
                LOG("Halo 4 HUD: StockFallback; MinHook refused the optional "
                    "native CUI root; camera/effects stay live");
            else
                LOG("Halo 4 HUD: StockFallback; optional native CUI root could "
                    "not be enabled (cleanup %s); camera/effects stay live",
                    hook.retained ? "retained for verified teardown" : "complete");
            return false;
        }
        g_halo4Restoration.hudInstalled.store(true, std::memory_order_release);
        if (Halo4FindUniquePinned(base, size, kCurvatureSignature, kHalo4CurvatureRva, curvatureSite))
        {
            const auto patch = Halo4CurvaturePatch();
            g_halo4CurvatureContinue = reinterpret_cast<void*>(base + kHalo4CurvatureContinueRva);
            g_halo4Restoration.curvatureTouched = true;
            const bool curvatureWrite = Halo4WriteExecutable(
                reinterpret_cast<void*>(curvatureSite), kHalo4CurvatureStock.data(), patch.data(), patch.size());
            g_halo4Restoration.curvaturePatched = Halo4PatchMatches(curvatureSite, patch);
            if (curvatureWrite && g_halo4Restoration.curvaturePatched)
            {
                g_halo4HudCurvatureEnabled = 1;
            }
            else
            {
                if (g_halo4Restoration.curvaturePatched)
                    (void)Halo4RestoreOwnedPatch(curvatureSite, patch, kHalo4CurvatureStock);
                g_halo4Restoration.curvaturePatched = false;
                g_halo4CurvatureContinue = nullptr;
            }
        }
        LOG("Halo 4 HUD Installed: gameplay-CUI affine is adjustable; "
            "native curvature=%s; pause/shell CUI stays stock",
            g_halo4Restoration.curvaturePatched ? "Installed" : "StockFallback");
        return true;
    }

    bool CleanupHalo4NativeHud(uintptr_t base)
    {
        g_halo4HudCurvatureEnabled = 0;
        g_halo4HudGameplayThreadId = 0;
        g_halo4Restoration.hudInstalled.store(false, std::memory_order_release);
        if (g_halo4Restoration.hudTarget)
            MH_DisableHook(g_halo4Restoration.hudTarget);
        if (g_halo4Camera.activeCallbacks.load(std::memory_order_acquire) != 0)
            return false;
        bool clean = true;
        if (g_halo4Restoration.curvaturePatched)
        {
            const auto patch = Halo4CurvaturePatch();
            clean = Halo4RestoreOwnedPatch(base + kHalo4CurvatureRva, patch, kHalo4CurvatureStock);
        }
        if (g_halo4Restoration.curvatureTouched)
            clean = Halo4PatchMatches(base + kHalo4CurvatureRva, kHalo4CurvatureStock) && clean;
        if (g_halo4Restoration.hudTarget)
        {
            const MH_STATUS removed = MCCVR_RemoveHookChecked(g_halo4Restoration.hudTarget);
            clean = (removed == MH_OK || removed == MH_ERROR_NOT_CREATED) && clean;
        }
        if (!clean)
        {
            g_halo4Restoration.cleanupRequired = true;
            return false;
        }
        g_halo4Restoration.hudTarget = nullptr;
        g_halo4Restoration.hudOriginal = nullptr;
        g_halo4Restoration.curvaturePatched = false;
        g_halo4Restoration.curvatureTouched = false;
        g_halo4CurvatureContinue = nullptr;
        return true;
    }

    void InstallHalo4PauseAndHelmet(uintptr_t base, size_t size)
    {
        uintptr_t pauseSite = 0;
        constexpr char kPauseSignature[] = "8B 15 ?? ?? ?? ?? 65 48 8B 04 25 58 00 00 00 41 B8 90 00 00 00 "
                                           "48 8B 04 D0 49 8B 14 00 32 C0 38 02 74 0F B8 01 00 00 00 66 D3 "
                                           "E0 66 85 42 02 0F 95 C0 C3";
        if (Halo4FindUniquePinned(base, size, kPauseSignature, kHalo4PauseReasonRva, pauseSite))
        {
            g_halo4Restoration.pauseReason = reinterpret_cast<Halo4PauseReasonFn>(pauseSite);
            g_halo4Restoration.pauseProven.store(true, std::memory_order_release);
            g_halo4NativePauseAvailable.store(true, std::memory_order_release);
            LOG("Halo 4 pause Installed: unique native reason-3 getter proven; "
                "presentation follows the start-menu component");
        }
        else
        {
            LOG("Halo 4 pause: StockFallback; native reason-3 getter was "
                "missing, ambiguous, or moved; other H4 features stay live");
        }

        const bool helmetShaderPath = D3D_Halo4HelmetShaderPathAvailable();
        g_halo4Restoration.helmetControlInstalled.store(helmetShaderPath, std::memory_order_release);
        if (helmetShaderPath)
        {
            LOG("Halo 4 helmet toggle Installed: exact known-good V6 visor "
                "pixel-shader binding is active; authored frame stays visible "
                "by default and no other HUD shader is modified");
        }
        else
        {
            LOG("Halo 4 helmet toggle: StockFallback; exact visor shader "
                "binding is unavailable, so authored helmet art stays stock");
        }
    }

#include "halo4_effect_suppression.inl"

    void Halo4RestorationWorkerTick()
    {
        const float curvature =
            std::clamp(std::isfinite(g_config.hud_curvature) ? g_config.hud_curvature : 0.5f, 0.0f, 1.0f) * 2.0f;
        g_halo4HudCurvatureValue = curvature;
        if (g_halo4Restoration.effectsInstalled.load(std::memory_order_acquire))
        {
            const bool hide = g_config.hide_muzzle_flash[3];
            if (!Halo4SetEffectsSuppressed(g_halo4Camera.base, hide))
            {
                if (!g_halo4Restoration.effectsSettingPendingLogged)
                    LOG("Halo 4 muzzle suppression: setting change pending; "
                        "owned mode-1 bytes or thread snapshot unavailable; "
                        "prior suppression retained, camera/HUD stay live");
                g_halo4Restoration.effectsSettingPendingLogged = true;
            }
            else if (g_halo4Restoration.effectsSettingPendingLogged)
            {
                LOG("Halo 4 muzzle suppression: pending setting applied (%s)", hide ? "enabled" : "disabled");
                g_halo4Restoration.effectsSettingPendingLogged = false;
            }
        }
    }

#include "halo4_pause_reader.inl"

    bool RemoveHalo4Restoration(uintptr_t base)
    {
        g_halo4NativePauseAvailable.store(false, std::memory_order_release);
        g_halo4Restoration.pauseProven.store(false, std::memory_order_release);
        g_halo4Restoration.pauseReason = nullptr;
        g_halo4Restoration.pauseStateKnown = false;
        g_halo4Restoration.pauseMismatchSinceMs = 0;
        g_halo4Restoration.helmetControlInstalled.store(false, std::memory_order_release);
        if (!CleanupHalo4NativeHud(base) || !CleanupHalo4Effects(base))
            return false;
        g_halo4Restoration.cleanupRequired = false;
        return true;
    }

    void InstallHalo4Restoration(uintptr_t base, size_t size)
    {
        (void)InstallHalo4Effects(base, size);
        (void)InstallHalo4NativeHud(base, size);
        InstallHalo4PauseAndHelmet(base, size);
        Halo4RestorationWorkerTick();
    }

    bool RemoveHalo4CameraCore()
    {
        if (!Halo4LookTests_Remove())
        {
            LOG("Halo 4 teardown: look-test hooks need cleanup retry; retaining native dependencies");
            return false;
        }
        if (!RemoveHalo4WeaponHaptics())
        {
            LOG("Halo 4 teardown: optional weapon haptics need cleanup retry; retaining native dependencies");
            return false;
        }
        if (!RemoveHalo4Muzzle())
        {
            LOG("Halo 4 teardown: optional muzzle hooks need cleanup retry; retaining native dependencies");
            return false;
        }
        const uint32_t generation = g_halo4Camera.generation.load(std::memory_order_acquire);
        // The module can stay resident across levels, so capability withdrawal
        // alone is not enough: do not let a new core for the same module
        // generation inherit an old AuthoredLocked/aspect publication.
        TitleAdapter_ClearCinematicControl(GameTitle::Halo4, generation);
        g_halo4Camera.cinematicNextPublishMs.store(0, std::memory_order_release);
        g_halo4Camera.cinematicTheaterProof.store(false, std::memory_order_release);
        g_halo4Camera.teardownRequested.store(true, std::memory_order_release);
        g_halo4Camera.armed.store(false, std::memory_order_release);
        if (!RemoveHalo4VehicleInput())
        {
            LOG("Halo 4 teardown: vehicle input readers need cleanup retry; retaining native module");
            return false;
        }
        if (!RemoveHalo4ContactMelee())
        {
            LOG("Halo 4 teardown: contact hooks need cleanup retry; retaining native module/trampolines");
            return false;
        }
        RemoveHalo4WorldCollision();
        g_halo4Camera.cuiReticleInstalled.store(false, std::memory_order_release);
        g_halo4Restoration.hudInstalled.store(false, std::memory_order_release);
        g_halo4HudCurvatureEnabled = 0;
        if (g_halo4Restoration.hudTarget)
            MH_DisableHook(g_halo4Restoration.hudTarget);
        PublishHalo4Lifecycle();
        for (void* target : {g_halo4Camera.cuiReticleTarget, g_halo4Camera.cuiReticleGameplayTarget,
                 g_halo4Camera.setupTarget, g_halo4Camera.wrapperTarget, g_halo4Camera.modelSkinningTarget,
                 g_halo4Camera.firstPersonProducerTarget, g_halo4WorldCollision.rayCastTarget})
            if (target)
                MH_DisableHook(target);
        // Both detours must have returned before a trampoline is freed or the
        // module reference is released.
        if (g_halo4Camera.activeCallbacks.load(std::memory_order_acquire) != 0)
            return false;
        if (!RemoveHalo4Restoration(g_halo4Camera.base))
            return false;
        // A callback admitted immediately before teardown can have published
        // after the first clear above. Clear again after quiescence so this
        // resident module generation cannot carry theatre into its next level.
        TitleAdapter_ClearCinematicControl(GameTitle::Halo4, generation);
        // F4: every core removal must be confirmed before its target and
        // trampoline are forgotten and halo4.dll is released. A failure keeps
        // all of them (and the module pin) for the next verified retry; a hook
        // already removed reports NOT_CREATED then, which counts as clean.
        bool coreRemoved = true;
        struct
        {
            void* target;
            const char* name;
        } const coreHooks[] = {
            {g_halo4Camera.setupTarget, "setup"},
            {g_halo4Camera.wrapperTarget, "wrapper"},
            {g_halo4Camera.modelSkinningTarget, "model skinning"},
            {g_halo4Camera.firstPersonProducerTarget, "first-person producer"},
            {g_halo4WorldCollision.rayCastTarget, "world-collision ray cast"},
        };
        for (const auto& hook : coreHooks)
        {
            if (!hook.target)
                continue;
            const MH_STATUS status = MCCVR_RemoveHookChecked(hook.target);
            if (status != MH_OK && status != MH_ERROR_NOT_CREATED)
            {
                LOG("Halo 4 teardown: %s hook removal failed (%d); retaining "
                    "targets/trampolines and the halo4.dll pin for a retry",
                    hook.name, static_cast<int>(status));
                coreRemoved = false;
            }
        }
        if (!coreRemoved)
            return false;
        const MH_STATUS dispatcherRemoved = g_halo4Camera.cuiReticleTarget
                                                ? MCCVR_RemoveHookChecked(g_halo4Camera.cuiReticleTarget)
                                                : MH_ERROR_NOT_CREATED;
        const MH_STATUS gameplayRemoved = g_halo4Camera.cuiReticleGameplayTarget
                                              ? MCCVR_RemoveHookChecked(g_halo4Camera.cuiReticleGameplayTarget)
                                              : MH_ERROR_NOT_CREATED;
        const bool dispatcherClean = dispatcherRemoved == MH_OK || dispatcherRemoved == MH_ERROR_NOT_CREATED;
        const bool gameplayClean = gameplayRemoved == MH_OK || gameplayRemoved == MH_ERROR_NOT_CREATED;
        if (!dispatcherClean || !gameplayClean)
        {
            if (!g_halo4Camera.cuiReticleCleanupRequired)
                LOG("Halo 4 teardown: optional CUI two-hook removal "
                    "needs retry (dispatcher=%d gameplay=%d); retaining both "
                    "targets/trampolines and halo4.dll pin until cleanup "
                    "proves complete",
                    static_cast<int>(dispatcherRemoved), static_cast<int>(gameplayRemoved));
            g_halo4Camera.cuiReticleCleanupRequired = true;
            return false;
        }
        // Put Halo 4's own first-person FOV squish back exactly as it was, and
        // only if we were the one who changed it. Done after the hooks are
        // quiescent and before the module reference is released, so the write
        // cannot land in an unloaded image.
        if (g_halo4Camera.fpSquishApplied && g_halo4Camera.fpSquishSlot)
        {
            *g_halo4Camera.fpSquishSlot = g_halo4Camera.fpSquishStock;
            LOG("Halo 4 first-person FOV: enable_first_person_squish "
                "restored to its stock %u on teardown",
                static_cast<unsigned>(g_halo4Camera.fpSquishStock));
        }
        g_halo4Camera.fpSquishApplied = false;
        g_halo4Camera.fpSquishSlot = nullptr;
        g_halo4Camera.fpSquishStock = 0;
        g_halo4Camera.moduleReference = nullptr;
        g_halo4Camera.setupTarget = nullptr;
        g_halo4Camera.wrapperTarget = nullptr;
        g_halo4Camera.modelSkinningTarget = nullptr;
        g_halo4Camera.firstPersonProducerTarget = nullptr;
        g_halo4Camera.cuiReticleTarget = nullptr;
        g_halo4Camera.cuiReticleGameplayTarget = nullptr;
        g_halo4Camera.cuiReticleCleanupRequired = false;
        g_halo4Camera.cuiReticleRejectedGeneration = 0;
        g_halo4Camera.cuiReticleResourceWaitLoggedGeneration = 0;
        g_halo4Camera.cuiReticleLastResourceAttemptMs = 0;
        g_halo4OrigSetup = nullptr;
        g_halo4OrigWrapper = nullptr;
        g_halo4OrigModelSkinning = nullptr;
        g_halo4OrigFirstPersonProducer = nullptr;
        g_halo4OrigCuiRenderCommand = nullptr;
        g_halo4OrigCuiGameplayRender = nullptr;
        g_halo4WorldCollision.rayCastTarget = nullptr;
        g_halo4WorldCollision.originalRayCast = nullptr;
        g_halo4WorldCollision.objectSetVelocities = nullptr;
        g_halo4WorldCollision.objectPushInstalled.store(false, std::memory_order_release);
        g_halo4WorldCollision.generation = 0;
        Halo4ResetWorldCollisionState(0);
        g_halo4RuntimeWeaponBoundsProven.store(false, std::memory_order_release);
        g_halo4VehicleIdentityProven.store(false, std::memory_order_release);
        g_halo4RenderModelTagIndexPointerSlot = 0;
        g_halo4RenderModelGroupBaseTable = 0;
        g_halo4EngineTlsIndex = nullptr;
        g_halo4Camera.base = 0;
        g_halo4Camera.size = 0;
        g_halo4Camera.elementAddress = 0;
        g_halo4Camera.setupReturnAddress = 0;
        g_halo4Camera.wrapperReturnAddress = 0;
        g_halo4Camera.installed.store(false, std::memory_order_release);
        g_halo4Camera.generation.store(0, std::memory_order_release);
        g_halo4Camera.teardownRequested.store(false, std::memory_order_release);
        Halo4ResetTelemetry();
        g_halo4Camera.sceneTargetMissing.store(false, std::memory_order_release);
        g_halo4LevelLoadGate.Rearm();
        LOG("Halo 4 camera core removed (generation %u); Halo 4 is stock "
            "again and the next level must re-earn the install proof",
            generation);
        return true;
    }

    // Resolves and verifies every camera anchor against the LOADED image, then
    // creates both hooks or neither. Worker thread only, and only behind the
    // level-load gate: this scans the whole module.
    bool InstallHalo4CameraCore(uintptr_t base, size_t size, uint32_t generation)
    {
        if (g_halo4Camera.installed.load(std::memory_order_acquire))
            return true;
        if (!base || !generation || size != kHalo4RetailImageSize)
            return false;
        if (!Halo4Adapter_RuntimeHooksPermitted())
            return false;
        // The cold observation's verdict for THIS module instance is the entry condition.
        if (!Halo4ColdObservation_Passed(generation))
            return false;

        g_halo4Camera.cinematicTheaterProof.store(false, std::memory_order_release);

        Halo4CameraInstallProof proof{};
        proof.coldObservationPassed = true;

        uintptr_t loopHit = 0;
        for (const Halo4RetailAnchor& anchor : kHalo4CameraAnchors)
        {
            const uintptr_t hit = sig::Find(base, size, anchor.pattern);
            if (!hit)
            {
                LOG("Halo 4 camera install: anchor %s matched ZERO times "
                    "(pinned RVA 0x%X); no hook is created",
                    anchor.name, anchor.rva);
                continue;
            }
            if (sig::Find(hit + 1, base + size - hit - 1, anchor.pattern))
            {
                LOG("Halo 4 camera install: anchor %s is NOT unique "
                    "(first match RVA 0x%zX); no hook is created",
                    anchor.name, hit - base);
                continue;
            }
            ++proof.anchorsMatchedOnce;
            if (hit - base == anchor.rva)
                ++proof.anchorsAtPinnedRva;
            else
                LOG("Halo 4 camera install: anchor %s moved (RVA 0x%zX, "
                    "pinned 0x%X)",
                    anchor.name, hit - base, anchor.rva);
            if (anchor.ripDispOffset != 0)
            {
                const uintptr_t target = sig::RipTarget(hit + anchor.ripDispOffset, hit + anchor.ripDispOffset + 4);
                if (target - base == anchor.ripTargetRva)
                    ++proof.ripTargetsAtPinnedRva;
                else
                    LOG("Halo 4 camera install: anchor %s rip decode landed "
                        "at RVA 0x%zX, pinned 0x%X",
                        anchor.name, target - base, anchor.ripTargetRva);
            }
            if (anchor.rva == kHalo4PerWindowLoopRva)
                loopHit = hit;
            // The engine TLS index, taken from the accessor's own rip
            // decode rather than shipped as an address. If that decode did not
            // land where it was pinned, the pointer stays null and the hands
            // feature simply never arms.
            if (anchor.ripTargetRva == kHalo4EngineTlsIndexRva && anchor.ripDispOffset != 0)
            {
                const uintptr_t target = sig::RipTarget(hit + anchor.ripDispOffset, hit + anchor.ripDispOffset + 4);
                g_halo4EngineTlsIndex =
                    target - base == kHalo4EngineTlsIndexRva ? reinterpret_cast<uint32_t*>(target) : nullptr;
            }
        }

        // The decisive edge: the loop's own two call instructions must target
        // the two functions we are about to hook. Without this the anchors are
        // three addresses that merely matched patterns; with it they are a
        // proven caller relationship.
        if (loopHit)
        {
            const uintptr_t setupTarget =
                sig::RipTarget(loopHit + kHalo4LoopSetupRel32Offset, loopHit + kHalo4LoopSetupRel32Offset + 4);
            const uintptr_t wrapperTarget =
                sig::RipTarget(loopHit + kHalo4LoopWrapperRel32Offset, loopHit + kHalo4LoopWrapperRel32Offset + 4);
            proof.loopCallTargetsAgree = Halo4CameraLoopTargetsAgree(
                static_cast<uint32_t>(setupTarget - base), static_cast<uint32_t>(wrapperTarget - base));
            if (!proof.loopCallTargetsAgree)
                LOG("Halo 4 camera install: the per-window loop calls RVA "
                    "0x%zX and 0x%zX, not the pinned setup 0x%X and wrapper "
                    "0x%X; no hook is created",
                    setupTarget - base, wrapperTarget - base, kHalo4SetupRva, kHalo4WrapperRva);
        }

        proof.executableRange = kHalo4SetupRva < size && kHalo4WrapperRva < size && kHalo4StackElementRva < size;
        proof.mappingStable = GetModuleHandleW(L"halo4.dll") == reinterpret_cast<HMODULE>(base);

        if (!Halo4CameraInstallComplete(proof))
        {
            LOG("Halo 4 camera install REFUSED: anchorsOnce=%u/%zu "
                "anchorsPinned=%u/%zu ripDecodes=%u/%u callEdge=%d range=%d "
                "mapping=%d; Halo 4 stays completely stock and flat",
                proof.anchorsMatchedOnce, kHalo4CameraAnchorCount, proof.anchorsAtPinnedRva, kHalo4CameraAnchorCount,
                proof.ripTargetsAtPinnedRva, kHalo4CameraAnchorRipTargets, proof.loopCallTargetsAgree ? 1 : 0,
                proof.executableRange ? 1 : 0, proof.mappingStable ? 1 : 0);
            return false;
        }

        // H4EK-first theatre evidence: the cinematic globals registration
        // loads the engine TLS index and registers a 0x28-byte member. Keep
        // this as a cold, unique signature proof rather than a render-time
        // scan or a bare retail address. Its failure withholds only theatre.
        constexpr char kHalo4CinematicRegistrationSignature[] = "8B 15 8D A7 F2 00 41 B9 28 00 00 00";
        constexpr uint32_t kHalo4CinematicRegistrationRva = 0x12CA85;
        const uintptr_t cinematicRegistration = sig::Find(base, size, kHalo4CinematicRegistrationSignature);
        const bool cinematicTheaterProof =
            cinematicRegistration && cinematicRegistration - base == kHalo4CinematicRegistrationRva &&
            !sig::Find(cinematicRegistration + 1, base + size - cinematicRegistration - 1,
                kHalo4CinematicRegistrationSignature);
        if (!cinematicTheaterProof)
        {
            LOG("Halo 4 cutscene theatre: cinematic-globals registration is %s; "
                "theatre remains immersive while camera/reticle/OpenXR stay armed",
                cinematicRegistration
                    ? (cinematicRegistration - base == kHalo4CinematicRegistrationRva ? "ambiguous" : "moved")
                    : "missing");
        }

        HMODULE moduleReference = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(base), &moduleReference) ||
            reinterpret_cast<uintptr_t>(moduleReference) != base)
        {
            LOG("Halo 4 camera install: halo4.dll is no longer mapped at its "
                "observed base; the install retries on a later tick");
            return false;
        }

        void* setupTarget = reinterpret_cast<void*>(base + kHalo4SetupRva);
        void* wrapperTarget = reinterpret_cast<void*>(base + kHalo4WrapperRva);
        Halo4SetupFn originalSetup = nullptr;
        Halo4WrapperFn originalWrapper = nullptr;
        const bool setupCreated = MCCVR_CreateHookChecked(setupTarget, reinterpret_cast<void*>(&Halo4SetupDetour),
                                      reinterpret_cast<void**>(&originalSetup)) == MH_OK;
        const bool wrapperCreated =
            setupCreated && MCCVR_CreateHookChecked(wrapperTarget, reinterpret_cast<void*>(&Halo4WrapperDetour),
                                reinterpret_cast<void**>(&originalWrapper)) == MH_OK;
        if (!setupCreated || !wrapperCreated)
        {
            if (setupCreated)
                MCCVR_RemoveHookChecked(setupTarget);
            LOG("Halo 4 camera install: MinHook refused the %s hook; Halo 4 "
                "stays stock and flat",
                setupCreated ? "wrapper" : "setup");
            return false;
        }

        g_halo4Camera.base = base;
        g_halo4Camera.size = size;
        g_halo4Camera.moduleReference = moduleReference;
        g_halo4Camera.setupTarget = setupTarget;
        g_halo4Camera.wrapperTarget = wrapperTarget;
        g_halo4Camera.elementAddress = base + kHalo4StackElementRva;
        // The instruction after each call in the proven loop.
        g_halo4Camera.setupReturnAddress = base + kHalo4PerWindowLoopRva + kHalo4LoopSetupRel32Offset + 4;
        g_halo4Camera.wrapperReturnAddress = base + kHalo4PerWindowLoopRva + kHalo4LoopWrapperRel32Offset + 4;
        g_halo4OrigSetup = originalSetup;
        g_halo4OrigWrapper = originalWrapper;
        g_halo4Camera.generation.store(generation, std::memory_order_release);
        PrepareCachedPhysicalCrouch(GameTitle::Halo4, base, size, generation);
        g_halo4Camera.teardownRequested.store(false, std::memory_order_release);
        g_halo4Camera.cinematicTheaterProof.store(cinematicTheaterProof, std::memory_order_release);
        g_halo4Camera.cinematicNextPublishMs.store(0, std::memory_order_release);
        Halo4ResetTelemetry();
        g_halo4Camera.sceneTargetMissing.store(false, std::memory_order_release);
        g_halo4Camera.installedAtMs = GetTickCount64();

        if (MH_EnableHook(setupTarget) != MH_OK || MH_EnableHook(wrapperTarget) != MH_OK)
        {
            LOG("Halo 4 camera install: MinHook could not enable both hooks; "
                "backing out and leaving Halo 4 stock");
            MH_DisableHook(setupTarget);
            MH_DisableHook(wrapperTarget);
            MCCVR_RemoveHookChecked(setupTarget);
            MCCVR_RemoveHookChecked(wrapperTarget);
            g_halo4Camera.moduleReference = nullptr;
            g_halo4Camera.setupTarget = nullptr;
            g_halo4Camera.wrapperTarget = nullptr;
            g_halo4OrigSetup = nullptr;
            g_halo4OrigWrapper = nullptr;
            g_halo4Camera.generation.store(0, std::memory_order_release);
            return false;
        }

        g_halo4Camera.installed.store(true, std::memory_order_release);
        // Turn off Halo 4's first-person FOV squish.
        //
        // The first-person camera rebuild 0x34EC44 multiplies the FP
        // layer's vertical FOV by a scale built from the STOCK viewmodel ratio,
        // gated on the boolean debug global `enable_first_person_squish`
        // (retail default 1). The FOV cover widens the world FOV to cover the headset
        // but deliberately leaves that ratio alone, so the gun and hands are
        // drawn through a narrower FOV than the world they sit in - they read as
        // magnified, at the wrong depth, and on their own layer. That is a flat
        // -screen viewmodel trick with no meaning in a headset.
        //
        // Resolved BY NAME through the same proven debug-var path Reach's rain
        // and motion-blur controls use - no hook, no signature, no address. The
        // type is required to be 5 (boolean) and the slot proved committed,
        // writable and non-executable before anything is written, and the stock
        // byte is kept so teardown puts it back.
        {
            auto* const squish = static_cast<uint8_t*>(FindDebugVarSlot(base, size, "enable_first_person_squish", 5));
            uint8_t stock = 0;
            if (squish && SafeReadByte(squish, &stock))
            {
                g_halo4Camera.fpSquishSlot = squish;
                g_halo4Camera.fpSquishStock = stock;
                *squish = 0;
                g_halo4Camera.fpSquishApplied = true;
                LOG("Halo 4 first-person FOV: enable_first_person_squish "
                    "resolved by name and set 0 (stock %u); the gun and hands "
                    "now use the same widened FOV as the world instead of the "
                    "flat-screen viewmodel ratio",
                    static_cast<unsigned>(stock));
            }
            else
            {
                LOG("Halo 4 first-person FOV: "
                    "enable_first_person_squish did not resolve as a boolean "
                    "debug global; the first-person layer stays at Halo 4's "
                    "stock viewmodel FOV and everything else is unaffected");
            }
        }
        // Optional, fail-open record ownership for the hands. It
        // owns only the live
        // Storm80 -> held -> native-body record sequence; any miss leaves that
        // exact feature stock while the working camera/session stays armed.
        InstallHalo4Vrik(base, size);
        // Separate optional transaction: a missing/ambiguous H4EK-mapped
        // query leaves the already-installed floating hands entirely intact.
        (void)InstallHalo4WorldCollision(base, size, generation);
        (void)InstallHalo4ContactMelee(base, size, generation);
        (void)InstallHalo4VehicleInput(base, size, generation);
        (void)InstallHalo4Muzzle(base, size, generation);
        (void)InstallHalo4WeaponHaptics(base, size, generation);
        (void)Halo4LookTests_Install(base, size, generation);
        // Halo 4 supplies authored pixels to the same shared VR
        // reticle chain as Halo 3/ODST/Reach. CUI never owns placement.
        (void)InstallHalo4CuiReticle(base, size, generation);
        // Four optional, independently fail-open restorations: muzzle
        // effects, the native HUD, pause semantics, and the known-good exact
        // visor-shader toggle. Each path fails open independently of camera/OpenXR.
        InstallHalo4Restoration(base, size);
        PublishHalo4Lifecycle();
        LOG("Halo 4 camera core installed (generation %u): setup 0x%X and the "
            "render wrapper 0x%X are hooked at their pinned RVAs, both proven "
            "to be the per-window loop 0x%X's own call targets; per-eye "
            "substitution happens at the observer result, before setup; when "
            "the H4EK cinematic proof is present, locked cinematics publish "
            "the shared theatre while look-free or unknown samples remain immersive",
            generation, kHalo4SetupRva, kHalo4WrapperRva, kHalo4PerWindowLoopRva);
        return true;
    }

    void Halo4CameraCore_Poll(uintptr_t base, size_t size, uint32_t generation, bool soleHalo4Title, bool levelRunning)
    {
        const bool installed = g_halo4Camera.installed.load(std::memory_order_acquire);
        if (installed && g_halo4Camera.teardownRequested.load(std::memory_order_acquire))
        {
            RemoveHalo4CameraCore();
            return;
        }
        if (!soleHalo4Title || !levelRunning || !base || size != kHalo4RetailImageSize)
        {
            if (installed)
                RemoveHalo4CameraCore();
            return;
        }
        if (installed &&
            (base != g_halo4Camera.base || generation != g_halo4Camera.generation.load(std::memory_order_acquire)))
        {
            RemoveHalo4CameraCore();
            return;
        }
        if (installed && g_halo4Camera.cuiReticleCleanupRequired)
            (void)CleanupHalo4CuiReticleFeature();
        else if (installed && levelRunning && !g_halo4Camera.cuiReticleInstalled.load(std::memory_order_acquire))
            (void)InstallHalo4CuiReticle(base, size, generation);
        if (installed)
            Halo4RestorationWorkerTick();
        if (g_vrRuntimeFailureLatched.load(std::memory_order_acquire))
        {
            g_halo4Camera.armed.store(false, std::memory_order_release);
            TitleAdapter_ClearCinematicControl(
                GameTitle::Halo4, g_halo4Camera.generation.load(std::memory_order_acquire));
            PublishHalo4Lifecycle();
            return;
        }
        if (!installed)
        {
            if (levelRunning)
                InstallHalo4CameraCore(base, size, generation);
            return;
        }
        // Same settling interval the other cores use: give the display and the
        // engine a moment after the install before the first owned eye render.
        if (!g_halo4Camera.armed.load(std::memory_order_acquire) &&
            GetTickCount64() - g_halo4Camera.installedAtMs >= kReachRenderSafetyIntervalMs)
        {
            g_halo4Camera.armed.store(true, std::memory_order_release);
            LOG("Halo 4 camera core armed: complete authored CUI weapon-reticle capture on the shared bullet-ray VR crosshair, current-eye controller-rerooted "
                "Storm hands, H3/ODST/Reach left_hand-marker parity free pose, exact shared-right-aim support pose, and "
                "same-frame held-model carry (no arm IK) on motion aim, VR "
                "turn and rumble on the headset-owned look, 6DOF and "
                "native headset-FOV coverage. The hand steers Halo 4's own aim "
                "through the shared closed loop the other three titles use, "
                "closing on the observer camera - the ray Halo 4 spawns "
                "first-person shots along. The stick becomes the VR turn "
                "(snap or smooth, per config) and moves Halo 4's own yaw "
                "reference, which the view now composes from: reading the "
                "engine's live heading while the loop steers it would apply "
                "the head's yaw twice. Halo 4 also publishes a Gameplay "
                "runtime mode for the first time, which is what the shared "
                "paths gate rumble and head-relative movement on. Insert "
                "returns aim to stick yaw + headset pitch; F2 returns "
                "everything to head tracking only. The weapon-only 0x28/0x0C selector, "
                "polyart rejection, framing, RGB/alpha repair, typeless "
                "XR view fallback, held-art policy and scene write-back are live; "
                "the H4EK look-constraint classifier publishes the retained"
                " 3D cutscene theatre without changing reticle ownership. "
                "The rejected HUD-basis writer remains dormant. A floating-hand transaction "
                "refusal submits no alternate hand algorithm and never "
                "disarms stereo or OpenXR");
        }
        Halo4WorldCollisionWorkerTick();
        PublishHalo4Lifecycle();
    }

    void Halo4CameraLogTick()
    {
        static uint64_t reportedFpFingerApplied = 0, reportedFpFingerRefused = 0;
        static ULONGLONG lastFpFingerReport = 0, lastBodyIkReport = 0;
        const ULONGLONG avatarReportNow = GetTickCount64();
        const auto fpFingerApplied = g_halo4FpFingerApplied.load(std::memory_order_relaxed);
        const auto fpFingerRefused = g_halo4FpFingerRefused.load(std::memory_order_relaxed);
        if ((fpFingerApplied != reportedFpFingerApplied || fpFingerRefused != reportedFpFingerRefused) &&
            avatarReportNow - lastFpFingerReport >= 5000)
        {
            lastFpFingerReport = avatarReportNow;
            LOG("Halo 4 optional FP fingers: applied=%llu stock_fallback=%llu; held grips and camera retained",
                static_cast<unsigned long long>(fpFingerApplied), static_cast<unsigned long long>(fpFingerRefused));
            reportedFpFingerApplied = fpFingerApplied;
            reportedFpFingerRefused = fpFingerRefused;
        }
        static uint64_t reportedBodyIkApplied = 0;
        static uint64_t reportedBodyIkRefused = 0;
        static uint64_t reportedBodyIkWriteFailures = 0;
        static uint64_t reportedBodyIkRollbackFailures = 0;
        static uint64_t reportedBodyIkSehFailures = 0;
        const uint64_t bodyIkApplied = g_halo4BodyIkApplied.load(std::memory_order_relaxed);
        const uint64_t bodyIkRefused = g_halo4BodyIkRefused.load(std::memory_order_relaxed);
        const uint64_t bodyIkWriteFailures = g_halo4BodyIkWriteFailures.load(std::memory_order_relaxed);
        const uint64_t bodyIkRollbackFailures = g_halo4BodyIkRollbackFailures.load(std::memory_order_relaxed);
        const uint64_t bodyIkSehFailures = g_halo4BodyIkSehFailures.load(std::memory_order_relaxed);
        if ((bodyIkApplied != reportedBodyIkApplied || bodyIkRefused != reportedBodyIkRefused ||
                bodyIkWriteFailures != reportedBodyIkWriteFailures ||
                bodyIkRollbackFailures != reportedBodyIkRollbackFailures ||
                bodyIkSehFailures != reportedBodyIkSehFailures) &&
            (avatarReportNow - lastBodyIkReport >= 5000 || bodyIkWriteFailures != reportedBodyIkWriteFailures ||
                bodyIkRollbackFailures != reportedBodyIkRollbackFailures ||
                bodyIkSehFailures != reportedBodyIkSehFailures))
        {
            lastBodyIkReport = avatarReportNow;
            LOG("Halo 4 experimental body IK: applied=%llu refused=%llu write_failures=%llu rollback_failures=%llu seh_failures=%llu; optional feature only",
                static_cast<unsigned long long>(bodyIkApplied), static_cast<unsigned long long>(bodyIkRefused),
                static_cast<unsigned long long>(bodyIkWriteFailures),
                static_cast<unsigned long long>(bodyIkRollbackFailures),
                static_cast<unsigned long long>(bodyIkSehFailures));
            reportedBodyIkApplied = bodyIkApplied;
            reportedBodyIkRefused = bodyIkRefused;
            reportedBodyIkWriteFailures = bodyIkWriteFailures;
            reportedBodyIkRollbackFailures = bodyIkRollbackFailures;
            reportedBodyIkSehFailures = bodyIkSehFailures;
        }
        static uint32_t reportedScopeFaults = 0;
        const auto scopeFaults = g_halo4ScopeFaults.load();
        if (scopeFaults != reportedScopeFaults)
        {
            reportedScopeFaults = scopeFaults;
            LOG("Halo 4 zoom stock fallback: optional scope faults=%u generation=%u; stereo core retained", scopeFaults,
                g_halo4ScopeFaultGeneration.load());
        }
        if (!g_halo4Camera.installed.load(std::memory_order_acquire))
            return;
        // Retire on a stale camera heartbeat, exactly the rule every
        // other title lives by ("once its heartbeat is absent for 500 ms,
        // disarm per-eye rendering and release the retained scene target").
        // The camera setup hook advances enginePitchSerial on every owned
        // frame, in gameplay and cinematics alike; at the main menu it stops.
        // A Steam session once showed an armed core with zero
        // owned frames for two minutes after a level exit: presentation stayed
        // head-locked and the stick stayed owned in the shell. This puts the
        // level-exit teardown back: armed + no heartbeat for 2.5 s + not the
        // pause screen -> request teardown; the next tick removes the core,
        // the title is re-detected, the mode falls to unsupported and the
        // menu/theatre presentation returns. Loading hitches shorter than
        // 2.5 s are untouched; a longer one re-earns the install through the
        // normal level-load gate, as every consecutive level already does.
        {
            static uint32_t heartbeatSerial = 0;
            static uint64_t heartbeatMs = 0;
            static uint32_t heartbeatGeneration = 0;
            const uint32_t generation = g_halo4Camera.generation.load(std::memory_order_acquire);
            const uint32_t serial =
                static_cast<uint32_t>(g_halo4Camera.enginePitchSerial.load(std::memory_order_acquire));
            const uint64_t nowMs = GetTickCount64();
            if (generation != heartbeatGeneration)
            {
                heartbeatGeneration = generation;
                heartbeatSerial = serial;
                heartbeatMs = nowMs;
            }
            else if (serial != heartbeatSerial)
            {
                heartbeatSerial = serial;
                heartbeatMs = nowMs;
            }
            else if (serial != 0 && heartbeatMs != 0 && g_halo4Camera.armed.load(std::memory_order_acquire) &&
                     !g_halo4Camera.teardownRequested.load(std::memory_order_acquire) && !VR_IsPausePresentation() &&
                     nowMs - heartbeatMs > 2500)
            {
                LOG("Halo 4 camera core: no camera heartbeat for %llu ms while "
                    "armed (the level closed) - retiring so presentation "
                    "returns to the menu screen, exactly like the other titles",
                    static_cast<unsigned long long>(nowMs - heartbeatMs));
                g_halo4Camera.armed.store(false, std::memory_order_release);
                g_halo4Camera.teardownRequested.store(true, std::memory_order_release);
                heartbeatMs = nowMs;
            }
        }
        // Trip the flat-screen fallback from the worker, never from the hot
        // detour: the detour only counts.
        if (!g_halo4Camera.sceneTargetMissing.load(std::memory_order_acquire) &&
            g_halo4Camera.consecutiveUncaptured.load(std::memory_order_acquire) >= kHalo4UncapturedEyeLimit)
        {
            g_halo4Camera.sceneTargetMissing.store(true, std::memory_order_release);
            LOG("Halo 4 stereo DISABLED: %u consecutive eyes rendered and NOT "
                "captured, so no eye image existed and no OpenXR layer could "
                "be submitted - that is a black headset, which is worse than "
                "flat. Halo 4 keeps rendering once, flat, for the rest of this "
                "level. The scene-colour render target was never identified; "
                "the SCENEPROBE lines above list every scene-scale target the "
                "title bound during an owned eye",
                kHalo4UncapturedEyeLimit);
        }
        static uint64_t lastLogMs = 0;
        const uint64_t now = GetTickCount64();
        if (now - lastLogMs < 2000)
            return;
        lastLogMs = now;
        static const char* const kRejectionNames[] = {"none", "not armed", "foreign caller", "setup args missing",
            "split-screen window", "observer unreadable", "camera invalid", "prepared frame unavailable",
            "eye build failed", "engine camera readback mismatch", "projection invalid/off-axis", "eye capture missing",
            "mono restore failed", "transaction exception", "scene target never identified"};
        const uint64_t stereo = g_halo4Camera.stereoFrames.exchange(0, std::memory_order_relaxed);
        const uint64_t stock = g_halo4Camera.stockFrames.exchange(0, std::memory_order_relaxed);
        const uint64_t dropped = g_halo4Camera.droppedFrames.exchange(0, std::memory_order_relaxed);
        const uint64_t claims = g_halo4Camera.cameraClaims.exchange(0, std::memory_order_relaxed);
        const uint64_t projections = g_halo4Camera.projectionReadbacks.exchange(0, std::memory_order_relaxed);
        const uint64_t uncaptured = g_halo4Camera.uncapturedEyes.exchange(0, std::memory_order_relaxed);
        uint32_t reason = g_halo4Camera.lastRejection.exchange(0, std::memory_order_relaxed);
        if (reason >= sizeof(kRejectionNames) / sizeof(kRejectionNames[0]))
            reason = 0;
        LOG("Halo 4 stereo pairs: %llu completed pairs, %llu stock windows, %llu "
            "dropped frames, %llu/%llu camera/projection readbacks, %llu "
            "uncaptured eyes in 2s; geometry %s; last readback max error "
            "pos %.6f fwd %.6f up %.6f; last projection half "
            "%.2f/%.2f deg center %.6f/%.6f; last rejection: %s; armed=%d",
            static_cast<unsigned long long>(stereo), static_cast<unsigned long long>(stock),
            static_cast<unsigned long long>(dropped), static_cast<unsigned long long>(claims),
            static_cast<unsigned long long>(projections), static_cast<unsigned long long>(uncaptured),
            stereo != 0 ? "TAKING" : "UNPROVEN", g_halo4Camera.lastPositionError.load(std::memory_order_relaxed),
            g_halo4Camera.lastForwardError.load(std::memory_order_relaxed),
            g_halo4Camera.lastUpError.load(std::memory_order_relaxed),
            g_halo4Camera.lastHalfFovX.load(std::memory_order_relaxed) * 57.2958f,
            g_halo4Camera.lastHalfFovY.load(std::memory_order_relaxed) * 57.2958f,
            g_halo4Camera.lastProjectionCenterX.load(std::memory_order_relaxed),
            g_halo4Camera.lastProjectionCenterY.load(std::memory_order_relaxed), kRejectionNames[reason],
            g_halo4Camera.armed.load(std::memory_order_acquire) ? 1 : 0);

        const LONG64 hiddenEffects = InterlockedExchange64(&g_halo4EffectsHidden, 0);
        const uint64_t hudTransforms = g_halo4Restoration.hudTransforms.exchange(0, std::memory_order_relaxed);
        uint64_t helmetShaders = 0;
        uint64_t helmetSuppressions = 0;
        D3D_GetHalo4HelmetTelemetry(helmetShaders, helmetSuppressions);
        uint64_t motionSuckShaders = 0;
        uint64_t motionSuckSuppressions = 0;
        D3D_GetHalo4ScreenEffectTelemetry(motionSuckShaders, motionSuckSuppressions);
        LOG("Halo 4 restorations: effects=%s (%lld local FP hides), "
            "HUD=%s (%llu native affine writes, curvature=%s), pause=%s, "
            "helmet=%s/config-%s (%llu exact shaders, %llu suppressed binds; "
            "full frontend root), screen-fx=%s (%llu motion-suck shaders, "
            "%llu suppressed binds); "
            "every unavailable feature stays stock",
            g_halo4Restoration.effectsInstalled.load(std::memory_order_acquire)
                ? (g_halo4EffectsEnabled ? "LIVE" : "config-disabled")
                : "StockFallback",
            static_cast<long long>(hiddenEffects),
            g_halo4Restoration.hudInstalled.load(std::memory_order_acquire) ? "LIVE" : "StockFallback",
            static_cast<unsigned long long>(hudTransforms),
            g_halo4Restoration.curvaturePatched ? "LIVE" : "StockFallback",
            g_halo4Restoration.pauseProven.load(std::memory_order_acquire) ? "native-reason-3" : "raw-edge-fallback",
            g_halo4Restoration.helmetControlInstalled.load(std::memory_order_acquire) ? "shader-LIVE" : "StockFallback",
            g_config.halo4_helmet ? "visible" : "hidden", static_cast<unsigned long long>(helmetShaders),
            static_cast<unsigned long long>(helmetSuppressions),
            D3D_Halo4ScreenEffectShaderPathAvailable() ? "exact-bridge-LIVE" : "StockFallback",
            static_cast<unsigned long long>(motionSuckShaders),
            static_cast<unsigned long long>(motionSuckSuppressions));

        const uint64_t collisionQueries = g_halo4WorldCollision.queries.exchange(0, std::memory_order_relaxed);
        const uint64_t collisionLeft = g_halo4WorldCollision.handContacts[0].exchange(0, std::memory_order_relaxed);
        const uint64_t collisionRight = g_halo4WorldCollision.handContacts[1].exchange(0, std::memory_order_relaxed);
        const uint64_t collisionWeapon = g_halo4WorldCollision.weaponContacts.exchange(0, std::memory_order_relaxed);
        const uint64_t collisionWeaponBounds =
            g_halo4WorldCollision.weaponBoundsPublished.exchange(0, std::memory_order_relaxed);
        const uint64_t collisionWeaponFallbacks =
            g_halo4WorldCollision.weaponBoundsFallbacks.exchange(0, std::memory_order_relaxed);
        const auto liveBoundsReads = g_halo4RuntimeWeaponBoundsReads.exchange(0, std::memory_order_relaxed);
        const auto liveBoundsRefusals = g_halo4RuntimeWeaponBoundsRefusals.exchange(0, std::memory_order_relaxed);
        if (liveBoundsReads || liveBoundsRefusals)
            LOG("Halo 4 equipped-model bounds in 2s: %llu runtime reads / %llu refusals; "
                "refusals retain catalog geometry when recognized, otherwise hand-only contact",
                static_cast<unsigned long long>(liveBoundsReads), static_cast<unsigned long long>(liveBoundsRefusals));
        const uint64_t collisionAppliedLeft = g_halo4WorldCollision.applied[0].exchange(0, std::memory_order_relaxed);
        const uint64_t collisionAppliedRight = g_halo4WorldCollision.applied[1].exchange(0, std::memory_order_relaxed);
        const uint64_t collisionResets = g_halo4WorldCollision.resets.exchange(0, std::memory_order_relaxed);
        const uint64_t collisionFailures = g_halo4WorldCollision.failures.load(std::memory_order_relaxed);
        ReportHalo4ContactMelee();
        ReportHalo4Muzzle();
        ReportHalo4WeaponHaptics();
        if (g_halo4VehicleInput.ready.load(std::memory_order_acquire))
            LOG("Halo 4 vehicle input in 2s: seated=%llu unknown/stock=%llu guardedFaults=%llu",
                static_cast<unsigned long long>(g_halo4VehicleInput.seated.exchange(0)),
                static_cast<unsigned long long>(g_halo4VehicleInput.unknown.exchange(0)),
                static_cast<unsigned long long>(g_halo4VehicleInput.faults.exchange(0)));
        const uint64_t collisionEngineContextCalls =
            g_halo4WorldCollision.engineContextCalls.exchange(0, std::memory_order_relaxed);
        const uint64_t collisionDynamicContexts =
            g_halo4WorldCollision.dynamicContextCalls.exchange(0, std::memory_order_relaxed);
        const uint64_t collisionFixedContexts =
            g_halo4WorldCollision.fixedOnlyContextCalls.exchange(0, std::memory_order_relaxed);
        const uint64_t collisionPushAttempts =
            g_halo4WorldCollision.objectPushAttempts.exchange(0, std::memory_order_relaxed);
        const uint64_t collisionPushes = g_halo4WorldCollision.objectPushes.exchange(0, std::memory_order_relaxed);
        const uint64_t collisionPushFailures = g_halo4WorldCollision.objectPushFailures.load(std::memory_order_relaxed);
        const uint64_t meleeCandidates =
            g_halo4WorldCollision.physicalMeleeCandidates.exchange(0, std::memory_order_relaxed);
        const uint64_t meleePulses = g_halo4WorldCollision.physicalMeleePulses.exchange(0, std::memory_order_relaxed);
        const uint64_t meleeCooldowns =
            g_halo4WorldCollision.physicalMeleeCooldowns.exchange(0, std::memory_order_relaxed);
        const uint64_t meleeVelocitySamples =
            g_halo4WorldCollision.physicalMeleeVelocitySamples.exchange(0, std::memory_order_relaxed);
        const uint32_t meleePeakCmPerSecond =
            g_halo4WorldCollision.physicalMeleePeakCentimetresPerSecond.exchange(0, std::memory_order_relaxed);
        LOG("Halo 4 experimental world contact + physical melee: capability-%s/config-%s; %llu engine "
            "raycasts (%llu dynamic-safe / %llu fixed-only), %llu authored-"
            "volume probes, %llu/%llu left/right hand contacts, %llu weapon "
            "contacts, bounds %llu published / %llu hand-only fallback, "
            "%llu/%llu visible corrections, %llu seed/teleport/model "
            "resets, environment %s, %llu ray failures in 2s; object push=%s "
            "(%llu attempts / %llu completed / %llu failures); physical "
            "melee=%s/config-%s/threshold %.2f m/s (%llu valid OpenXR "
            "velocity samples / %llu swing crossings / %llu native-input "
            "pulses / %llu cooldown suppressions / %.2f m/s peak)",
            g_halo4WorldCollision.installed.load(std::memory_order_acquire) ? "LIVE" : "StockFallback",
            g_halo4WorldCollision.configured.load(std::memory_order_acquire) ? "enabled" : "disabled",
            static_cast<unsigned long long>(collisionEngineContextCalls),
            static_cast<unsigned long long>(collisionDynamicContexts),
            static_cast<unsigned long long>(collisionFixedContexts), static_cast<unsigned long long>(collisionQueries),
            static_cast<unsigned long long>(collisionLeft), static_cast<unsigned long long>(collisionRight),
            static_cast<unsigned long long>(collisionWeapon), static_cast<unsigned long long>(collisionWeaponBounds),
            static_cast<unsigned long long>(collisionWeaponFallbacks),
            static_cast<unsigned long long>(collisionAppliedLeft),
            static_cast<unsigned long long>(collisionAppliedRight), static_cast<unsigned long long>(collisionResets),
            g_halo4WorldCollision.environmentValidated.load(std::memory_order_acquire) ? "VALIDATED" : "unproven",
            static_cast<unsigned long long>(collisionFailures),
            g_halo4WorldCollision.objectPushInstalled.load(std::memory_order_acquire) ? "LIVE" : "StockFallback",
            static_cast<unsigned long long>(collisionPushAttempts), static_cast<unsigned long long>(collisionPushes),
            static_cast<unsigned long long>(collisionPushFailures),
            kEnableHalo4PhysicalMeleeStage9 ? "available" : "StockFallback",
            g_halo4WorldCollision.physicalMeleeConfigured.load(std::memory_order_acquire) ? "enabled" : "disabled",
            g_halo4WorldCollision.physicalMeleeRequiredSpeed.load(std::memory_order_relaxed),
            static_cast<unsigned long long>(meleeVelocitySamples), static_cast<unsigned long long>(meleeCandidates),
            static_cast<unsigned long long>(meleePulses), static_cast<unsigned long long>(meleeCooldowns),
            static_cast<double>(meleePeakCmPerSecond) / 100.0);

        const uint64_t cuiGameplayPasses = g_halo4Camera.cuiGameplayPasses.exchange(0, std::memory_order_relaxed);
        const uint64_t cuiBegins = g_halo4Camera.cuiReticleBegins.exchange(0, std::memory_order_relaxed);
        const uint64_t cuiCaptures = g_halo4Camera.cuiReticleCaptures.exchange(0, std::memory_order_relaxed);
        const uint64_t cuiSuppressions = g_halo4Camera.cuiReticleSuppressions.exchange(0, std::memory_order_relaxed);
        const uint64_t cuiFailures = g_halo4Camera.cuiReticleRedirectFailures.exchange(0, std::memory_order_relaxed);
        const uint64_t cuiCompleted = g_halo4Camera.cuiReticleCompleted.exchange(0, std::memory_order_relaxed);
        const uint64_t cuiForced = g_halo4Camera.cuiReticleForcedCleanup.exchange(0, std::memory_order_relaxed);
        const uint64_t authoredOmReroutes = VR_TakeAuthoredReticleOmReroutes();
        const uint64_t authoredFramingReasserts = VR_TakeAuthoredReticleFramingReasserts();
        LOG("Halo 4 shared authored-reticle path: hook=%s, %llu main gameplay CUI "
            "passes, %llu begin markers, "
            "%llu completed actions (%llu authored captures / %llu native hides), %llu "
            "write failures, %llu forced restores, %llu exact capture OM reroutes "
            "(%llu framing reasserts) in 2s; last native base %.3f/%.3f, "
            "capture base %.3f/%.3f "
            "+ offscreen hide %.3f/%.3f, scale %.4f -> %.4f; camera "
            "and OpenXR remain independently armed; the existing VR quad alone owns placement",
            Halo4CuiReticleTransformLive() ? "LIVE" : "stock fallback",
            static_cast<unsigned long long>(cuiGameplayPasses), static_cast<unsigned long long>(cuiBegins),
            static_cast<unsigned long long>(cuiCompleted), static_cast<unsigned long long>(cuiCaptures),
            static_cast<unsigned long long>(cuiSuppressions), static_cast<unsigned long long>(cuiFailures),
            static_cast<unsigned long long>(cuiForced), static_cast<unsigned long long>(authoredOmReroutes),
            static_cast<unsigned long long>(authoredFramingReasserts),
            g_halo4Camera.cuiReticleBaseX.load(std::memory_order_relaxed),
            g_halo4Camera.cuiReticleBaseY.load(std::memory_order_relaxed),
            g_halo4Camera.cuiReticleCaptureBaseX.load(std::memory_order_relaxed),
            g_halo4Camera.cuiReticleCaptureBaseY.load(std::memory_order_relaxed),
            g_halo4Camera.cuiReticleAimX.load(std::memory_order_relaxed),
            g_halo4Camera.cuiReticleAimY.load(std::memory_order_relaxed),
            g_halo4Camera.cuiReticleStockScale.load(std::memory_order_relaxed),
            g_halo4Camera.cuiReticleWrittenScale.load(std::memory_order_relaxed));

        if constexpr (kEnableHalo4ParityTrace)
        {
            char commands[1536]{};
            size_t written = 0;
            uint32_t distinctCommands = 0;
            for (size_t command = 0; command < kHalo4ParityCommandBucketCount; ++command)
            {
                const uint64_t hits = g_halo4Camera.parityCuiCommands[command].exchange(0, std::memory_order_relaxed);
                if (!hits)
                    continue;
                ++distinctCommands;
                const size_t room = sizeof(commands) - written;
                const int step = snprintf(commands + written, room, "%s%02zXx%llu", written ? " " : "", command,
                    static_cast<unsigned long long>(hits));
                if (step <= 0 || static_cast<size_t>(step) >= room)
                    break;
                written += static_cast<size_t>(step);
            }
            if (!written)
                snprintf(commands, sizeof(commands), "none");
            const uint64_t totalCommands = g_halo4Camera.parityCuiTotalCommands.exchange(0, std::memory_order_relaxed);
            const uint64_t unreadable = g_halo4Camera.parityCuiUnreadableHeaders.exchange(0, std::memory_order_relaxed);
            const uint64_t outOfRange =
                g_halo4Camera.parityCuiCommandsOutOfRange.exchange(0, std::memory_order_relaxed);
            LOG("H4DIAG CUI COMMANDS: %llu total, %u distinct byte-range types, "
                "%llu unreadable, %llu outside 00-FF in 2s; ABI window=%u "
                "channel=%u mode=%u flag=%u; histogram %s",
                static_cast<unsigned long long>(totalCommands), distinctCommands,
                static_cast<unsigned long long>(unreadable), static_cast<unsigned long long>(outOfRange),
                g_halo4Camera.parityLastWindow.load(std::memory_order_relaxed),
                g_halo4Camera.parityLastChannel.load(std::memory_order_relaxed),
                g_halo4Camera.parityLastRenderMode.load(std::memory_order_relaxed),
                g_halo4Camera.parityLastFlag.load(std::memory_order_relaxed), commands);

            uint32_t liveTransformIds = 0;
            for (size_t slot = 0; slot < kHalo4ParityTransformSlotCount; ++slot)
            {
                const int32_t transformId = g_halo4Camera.parityTransformId[slot].load(std::memory_order_acquire);
                if (transformId == kHalo4ParityEmptyTransformId)
                    continue;
                ++liveTransformIds;
                const uint64_t hits = g_halo4Camera.parityTransformHits[slot].exchange(0, std::memory_order_relaxed);
                const uint64_t replayHits =
                    g_halo4Camera.parityTransformReplayHits[slot].exchange(0, std::memory_order_relaxed);
                const uint64_t normalHits =
                    g_halo4Camera.parityTransformNormalHits[slot].exchange(0, std::memory_order_relaxed);
                if (!hits)
                    continue;
                LOG("H4DIAG CUI TRANSFORM: slot=%zu id=%d/0x%08X hits=%llu "
                    "replay=%llu normal=%llu payload=0x%X stack=%u last "
                    "scale=%.6f xy=%+.3f/%+.3f",
                    slot, transformId, static_cast<uint32_t>(transformId), static_cast<unsigned long long>(hits),
                    static_cast<unsigned long long>(replayHits), static_cast<unsigned long long>(normalHits),
                    g_halo4Camera.parityTransformPayload[slot].load(std::memory_order_relaxed),
                    g_halo4Camera.parityTransformStackCount[slot].load(std::memory_order_relaxed),
                    g_halo4Camera.parityTransformScale[slot].load(std::memory_order_relaxed),
                    g_halo4Camera.parityTransformX[slot].load(std::memory_order_relaxed),
                    g_halo4Camera.parityTransformY[slot].load(std::memory_order_relaxed));
            }
            const uint64_t transformOverflow =
                g_halo4Camera.parityTransformOverflow.exchange(0, std::memory_order_relaxed);
            LOG("H4DIAG CUI IDENTITY COVERAGE: %u/%zu distinct type-28 "
                "transform IDs retained, %llu observations overflowed; zero "
                "overflow is required before using this trace as a complete "
                "reticle census",
                liveTransformIds, kHalo4ParityTransformSlotCount, static_cast<unsigned long long>(transformOverflow));
        }

        // The two head-pose behaviours report on their own lines so one headset
        // session can accept or reject each independently. A pair count alone
        // proves nothing; these report what the ENGINE held.
        const uint64_t headFrames = g_halo4Camera.headTrackedFrames.exchange(0, std::memory_order_relaxed);
        LOG("Halo 4 head tracking: %llu tracked frames in 2s, "
            "reference %s; last yaw delta %.1f deg, head pitch %.1f deg "
            "(%s), lean %.3f world units = %+.3f/%+.3f/%+.3f xyz "
            "(6DOF %s, world scale %.2f units/m)",
            static_cast<unsigned long long>(headFrames),
            g_halo4Camera.headReferenceValid.load(std::memory_order_acquire) ? "captured" : "NOT captured",
            g_halo4Camera.lastHeadYawDelta.load(std::memory_order_relaxed) * 57.2958f,
            g_halo4Camera.lastHeadPitchDelta.load(std::memory_order_relaxed) * 57.2958f,
            Halo4LookPitchOwned() ? "ABSOLUTE, headset owns pitch" : "added to the engine's pitch",
            g_halo4Camera.lastLeanMagnitude.load(std::memory_order_relaxed),
            g_halo4Camera.lastLean[0].load(std::memory_order_relaxed),
            g_halo4Camera.lastLean[1].load(std::memory_order_relaxed),
            g_halo4Camera.lastLean[2].load(std::memory_order_relaxed),
            g_positional.load(std::memory_order_relaxed) ? "ON" : "OFF", g_worldScale.load(std::memory_order_relaxed));

        // Pitch ownership's own line, so the pitch claim can be accepted or rejected on
        // its own. `commanded` falling to near zero while `error` stays small
        // is the loop working; a large steady error with the stick pinned is
        // the engine refusing to be steered, which is a different fault
        // entirely and must not be reported as head tracking.
        const uint64_t pitchCommanded = g_halo4Camera.pitchCommandedFrames.exchange(0, std::memory_order_relaxed);
        const uint64_t pitchParked = g_halo4Camera.pitchParkedFrames.exchange(0, std::memory_order_relaxed);
        const uint64_t pitchStalls = g_halo4Camera.pitchServoStalls.exchange(0, std::memory_order_relaxed);
        LOG("Halo 4 look/aim: %s; engine pitch %.1f deg vs head "
            "%.1f deg (error %.2f deg), stick %+.2f, learned direction %+.0f, "
            "min step %.3f deg; %llu commanded / %llu parked frames, %llu "
            "stale-publication stalls in 2s",
            Halo4ControllerAimActive() ? "MOTION AIM: the hand steers, the stick turns"
                                       : (Halo4LookPitchOwned() ? "headset pitch only (VR aim off - press Insert)"
                                                                : "stock stick pitch (not owned)"),
            g_halo4Camera.enginePitch.load(std::memory_order_relaxed) * 57.2958f,
            g_halo4Camera.lastPitchTarget.load(std::memory_order_relaxed) * 57.2958f,
            g_halo4Camera.lastPitchError.load(std::memory_order_relaxed) * 57.2958f,
            g_halo4Camera.lastPitchCommand.load(std::memory_order_relaxed),
            g_halo4Camera.pitchServoDirection.load(std::memory_order_relaxed),
            g_halo4Camera.pitchServoMinStep.load(std::memory_order_relaxed) * 57.2958f,
            static_cast<unsigned long long>(pitchCommanded), static_cast<unsigned long long>(pitchParked),
            static_cast<unsigned long long>(pitchStalls));

        // Motion aim's own line. The three shared systems Halo 4 had never been
        // wired into report what the SHARED code decided, not what we asked
        // for: a capability mask that is missing a bit, or a runtime mode that
        // is not Gameplay, silently disables rumble and head-relative movement
        // - which is exactly how Reach lost its rumble for weeks.
        const uint64_t turns = g_halo4Camera.vrTurns.exchange(0, std::memory_order_relaxed);
        LOG("Halo 4 shared systems: runtime mode %d, aim %s, "
            "haptics %s, room scale %s, locomotion %s; yaw reference %s at "
            "%.1f deg, %llu VR turn steps in 2s (%s %s)",
            static_cast<int>(TitleAdapter_GetRuntimeMode()),
            Game_HasTitleCapability(TitleCapability_ControllerAim) ? "GRANTED" : "denied",
            Game_HasTitleCapability(TitleCapability_Haptics) ? "GRANTED" : "denied",
            Game_HasTitleCapability(TitleCapability_RoomScale) ? "GRANTED" : "denied",
            Game_MoveStickIsLocomotion() ? "head-relative" : "raw stick",
            g_halo4Camera.gameYawReferenceValid.load(std::memory_order_acquire) ? "captured" : "NOT captured",
            g_halo4Camera.gameYawReference.load(std::memory_order_relaxed) * 57.2958f,
            static_cast<unsigned long long>(turns), g_config.turn_smooth ? "smooth" : "snap", "turn");

        const uint64_t solved = g_halo4Camera.vrikSolvedPalettes.exchange(0, std::memory_order_relaxed);
        const uint64_t stockPalettes = g_halo4Camera.vrikStockPalettes.exchange(0, std::memory_order_relaxed);
        const uint64_t refused = g_halo4Camera.vrikAlignmentRefusals.exchange(0, std::memory_order_relaxed);
        const uint64_t exactHits = g_halo4Camera.vrikExactReturnHits.exchange(0, std::memory_order_relaxed);
        uint64_t stageRefusals[static_cast<size_t>(Halo4VrikStage::Count)] = {};
        for (size_t stage = 0; stage < static_cast<size_t>(Halo4VrikStage::Count); ++stage)
            stageRefusals[stage] = g_halo4Camera.vrikStageRefusals[stage].exchange(0, std::memory_order_relaxed);
        const uint64_t weaponCarried = g_halo4Camera.vrikWeaponRecordsCarried.exchange(0, std::memory_order_relaxed);
        const uint64_t weaponRefused = g_halo4Camera.vrikWeaponRecordsRefused.exchange(0, std::memory_order_relaxed);
        const uint64_t markerParityFallbacks =
            g_halo4Camera.vrikMarkerParityFallbacks.exchange(0, std::memory_order_relaxed);
        const uint64_t markerParityApplications =
            g_halo4Camera.vrikMarkerParityApplications.exchange(0, std::memory_order_relaxed);
        const uint64_t twoHandRigidSupportLocks =
            g_halo4Camera.vrikTwoHandRigidSupportLocks.exchange(0, std::memory_order_relaxed);
        const uint64_t stormCandidates = g_halo4Camera.vrikStormRecordCandidates.exchange(0, std::memory_order_relaxed);
        LOG("Halo 4 marker-parity free / rigid two-hand support: palette %s, halo4_hands=%d; "
            "%llu Storm hand palettes committed / %llu refused, %llu held records committed / %llu "
            "refused, %llu exact first-person calls "
            "in 2s; no IK, forced floaty mask, world scale %.3f, current stock-"
            "to-controller right-wrist distance %.4f; current-eye same-frame "
            "Storm candidates %llu; left modes: marker-parity free %llu / rigid support lock "
            "%llu / free fallback %llu",
            g_halo4Camera.modelSkinningTarget ? "hooked" : "UNAVAILABLE - stock", g_config.halo4_hands ? 1 : 0,
            static_cast<unsigned long long>(solved), static_cast<unsigned long long>(refused),
            static_cast<unsigned long long>(weaponCarried), static_cast<unsigned long long>(weaponRefused),
            static_cast<unsigned long long>(exactHits), g_worldScale.load(std::memory_order_relaxed),
            g_halo4Camera.vrikTargetMiss.load(std::memory_order_relaxed),
            static_cast<unsigned long long>(stormCandidates), static_cast<unsigned long long>(markerParityApplications),
            static_cast<unsigned long long>(twoHandRigidSupportLocks),
            static_cast<unsigned long long>(markerParityFallbacks));
        LOG("Halo 4 floating-hand refusals in 2s: count=%llu copy=%llu basis=%llu "
            "range=%llu eye/root=%llu link=%llu side=%llu right-pose=%llu left-pose=%llu "
            "right-rigid=%llu left-rigid=%llu; %llu stock/non-owned palettes",
            static_cast<unsigned long long>(stageRefusals[static_cast<size_t>(Halo4VrikStage::CountRefused)]),
            static_cast<unsigned long long>(stageRefusals[static_cast<size_t>(Halo4VrikStage::CopyFailed)]),
            static_cast<unsigned long long>(stageRefusals[static_cast<size_t>(Halo4VrikStage::BasisFailed)]),
            static_cast<unsigned long long>(stageRefusals[static_cast<size_t>(Halo4VrikStage::RangeFailed)]),
            static_cast<unsigned long long>(stageRefusals[static_cast<size_t>(Halo4VrikStage::AnchorFailed)]),
            static_cast<unsigned long long>(stageRefusals[static_cast<size_t>(Halo4VrikStage::LinkFailed)]),
            static_cast<unsigned long long>(stageRefusals[static_cast<size_t>(Halo4VrikStage::SideFailed)]),
            static_cast<unsigned long long>(stageRefusals[static_cast<size_t>(Halo4VrikStage::RightPoseFailed)]),
            static_cast<unsigned long long>(stageRefusals[static_cast<size_t>(Halo4VrikStage::LeftPoseFailed)]),
            static_cast<unsigned long long>(stageRefusals[static_cast<size_t>(Halo4VrikStage::RightIkFailed)]),
            static_cast<unsigned long long>(stageRefusals[static_cast<size_t>(Halo4VrikStage::LeftIkFailed)]),
            static_cast<unsigned long long>(stockPalettes));
        // The producer flag partitions the sequence but does not identify
        // anatomy: flag 1 contains both Storm hands and the held model.
        LOG("Halo 4 record sequence in 2s: %llu flag0 native-body / %llu "
            "flag1 first-person-loop / "
            "%llu unexpected / %llu header unreadable; nodes.count resolver "
            "%llu failures; official body identity %llu, frozen-local-unit "
            "matches %llu / mismatches %llu; last flag0 nodes %d checksum 0x%08X, last flag1 "
            "nodes %d, Storm checksum 0x%08X (H4EK 0x%08X telemetry-only), "
            "held nodes %d checksum 0x%08X",
            static_cast<unsigned long long>(g_halo4Camera.vrikBodyFillRecords.exchange(0, std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_halo4Camera.vrikWeaponFillRecords.exchange(0, std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_halo4Camera.vrikUnexpectedFillRecords.exchange(0, std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_halo4Camera.vrikUnreadableFillRecords.exchange(0, std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_halo4Camera.vrikNodeCountResolveFailures.exchange(0, std::memory_order_relaxed)),
            static_cast<unsigned long long>(g_halo4Camera.vrikBodyIdentityExact.exchange(0, std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_halo4Camera.vrikBodyLocalUnitMatches.exchange(0, std::memory_order_relaxed)),
            static_cast<unsigned long long>(
                g_halo4Camera.vrikBodyLocalUnitMismatches.exchange(0, std::memory_order_relaxed)),
            g_halo4Camera.vrikLastBodyNodeCount.load(std::memory_order_relaxed),
            g_halo4Camera.vrikLastBodyChecksum.load(std::memory_order_relaxed),
            g_halo4Camera.vrikLastWeaponNodeCount.load(std::memory_order_relaxed),
            g_halo4Camera.vrikLastStormChecksum.load(std::memory_order_relaxed), kHalo4StormFpRuntimeImportChecksum,
            g_halo4Camera.vrikLastHeldNodeCount.load(std::memory_order_relaxed),
            g_halo4Camera.vrikLastHeldChecksum.load(std::memory_order_relaxed));
        // Which per-render-model counts actually arrive. This is the fact
        // an earlier gate assumed instead of measuring.
        {
            char counts[256];
            size_t written = 0;
            for (int slot = 0; slot < kHalo4VrikCountSlots; ++slot)
            {
                const int32_t value = g_halo4Camera.vrikCountValue[slot].load(std::memory_order_acquire);
                const uint64_t hits = g_halo4Camera.vrikCountHits[slot].exchange(0, std::memory_order_relaxed);
                if (value == kHalo4VrikCountSlotEmpty || hits == 0)
                    continue;
                const size_t room = sizeof(counts) - written;
                const int step = snprintf(counts + written, room, "%s%dx%llu", written ? " " : "", value,
                    static_cast<unsigned long long>(hits));
                if (step <= 0 || static_cast<size_t>(step) >= room)
                    break;
                written += static_cast<size_t>(step);
            }
            if (!written)
                snprintf(counts, sizeof(counts), "none");
            LOG("Halo 4 argument-7 histogram in 2s: %s (%llu past "
                "%d slots)",
                counts,
                static_cast<unsigned long long>(g_halo4Camera.vrikCountOverflow.exchange(0, std::memory_order_relaxed)),
                kHalo4VrikCountSlots);
        }

        const uint64_t widened = g_halo4Camera.fovWidenedFrames.exchange(0, std::memory_order_relaxed);
        const uint64_t stockFov = g_halo4Camera.fovStockFovFrames.exchange(0, std::memory_order_relaxed);
        LOG("Halo 4 FOV cover: %llu widened eyes, %llu stock-FOV eyes "
            "in 2s; calibration %s (gain %.4f, ratio %.4f); requested half-Y "
            "%.2f deg, engine built %.2f/%.2f deg; contains headset frustum: "
            "%s",
            static_cast<unsigned long long>(widened), static_cast<unsigned long long>(stockFov),
            g_halo4Camera.fovLearned.load(std::memory_order_acquire) ? "learned" : "NOT learned",
            g_halo4Camera.fovGain.load(std::memory_order_relaxed),
            g_halo4Camera.fovRatio.load(std::memory_order_relaxed),
            g_halo4Camera.lastRequestedHalfY.load(std::memory_order_relaxed) * 57.2958f,
            g_halo4Camera.lastHalfFovX.load(std::memory_order_relaxed) * 57.2958f,
            g_halo4Camera.lastHalfFovY.load(std::memory_order_relaxed) * 57.2958f,
            g_halo4Camera.lastCoverContained.load(std::memory_order_relaxed) ? "YES" : "NO");
        // Retail's converter multiplies both observer FOV fields by a scale
        // chosen at halo4.dll 0x38F01A-0x38F05E: 0.785, or 0.168214291 only
        // when the engine's FOV global is exactly zero. Logging the live pair
        // makes any future branch flip visible instead of silent.
        LOG("Halo 4 FOV converter: observer +0x78 %.5f rad -> element "
            "+0x28 %.5f rad, measured scale K %.5f (static candidates 0.78500 "
            "/ 0.16821)",
            g_halo4Camera.lastObserverVerticalFov.load(std::memory_order_relaxed),
            g_halo4Camera.lastElementVerticalFov.load(std::memory_order_relaxed),
            g_halo4Camera.lastConverterScale.load(std::memory_order_relaxed));
        if constexpr (kEnableHalo4ParityTrace)
        {
            LOG("H4DIAG PARITY COVERAGE: generation=%u camera=%s fov=%s "
                "head6dof=%s aim=%s gameplayCUI=%s authoredCapture=%s "
                "stormHands=%s heldWeapon=%s; NOT OBSERVABLE FROM CURRENT "
                "PROVEN HOOKS: vehicle seat/camera/projectile ownership, "
                "cutscene/theater state, native HUD-layout consumer. Those "
                "three remain stock and require H4EK-first bindings; this "
                "diagnostic does not guess them",
                g_halo4Camera.generation.load(std::memory_order_acquire),
                stereo ? "OBSERVED" : "not seen in this 2s window", widened ? "OBSERVED" : "not seen in this 2s window",
                headFrames ? "OBSERVED" : "not seen in this 2s window",
                (pitchCommanded || pitchParked) ? "OBSERVED" : "not seen in this 2s window",
                cuiGameplayPasses ? "OBSERVED" : "not seen in this 2s window",
                cuiCaptures ? "OBSERVED" : "not seen in this 2s window",
                solved ? "OBSERVED" : "not seen in this 2s window",
                weaponCarried ? "OBSERVED" : "not seen in this 2s window");
        }
    }
} // namespace
namespace halo4_bridge
{
    void TickHalo4(const TitleTickContext& c)
    {
        const TitleDescriptor* activeTitle = c.activeTitle;
        const bool activeLevelRunning = c.activeLevelRunning;
        const uint64_t pollNow = c.now;
        (void)pollNow;
        const uintptr_t& halo4GateBase = g_supervisorTitles.halo4Gate.base;
        const size_t& halo4GateSize = g_supervisorTitles.halo4Gate.size;
        const bool& halo4GateSampled = g_supervisorTitles.halo4Gate.sampled;
        uint32_t& halo4ManualRecoveryPending = g_supervisorTitles.halo4ManualRecoveryPending;
        (void)halo4ManualRecoveryPending;
        {
            // Look prompts (halo4_look_tests.h): head look reaches the
            // action-test builder while a script polls a look test.
            // Installed/removed with the camera core.
            Halo4LookTests_SetEnabled(g_config.halo4_tutorial_head_look);
            Halo4LookTests_Report();
        }
        {
            const bool halo4Active = activeTitle && activeTitle->title == GameTitle::Halo4;
            // Gate lifecycle first, independent of the observation below
            // (the camera core may replace the observation; this re-arm must
            // outlive it). Same title-exit re-arm as the other three
            // titles: stillness observed while Halo 4 is not the active
            // title must never satisfy the frozen half of the NEXT
            // level's proof. The condition is the exact complement of
            // the gate-sample condition in the switch above, so a
            // sampled tick is never also a re-armed tick.
            if (!halo4Active)
                g_halo4LevelLoadGate.Rearm();
            // Halo 4 cold observation. Verifies the pinned
            // identity and the code anchors against the loaded image,
            // once per module instance, only on a tick the gate sampled
            // AND proved the level running (the preflight keeps a
            // non-owning exact-base identity and scans the whole image -
            // touches that must never hit a loading module, and that a
            // fail-open gate cannot vouch for). Read-only either way.
            else if (halo4GateSampled && activeLevelRunning)
            {
                const uint32_t halo4Generation = TitleAdapter_GetGeneration(GameTitle::Halo4);
                if (halo4ManualRecoveryPending == halo4Generation &&
                    !g_halo4Camera.installed.load(std::memory_order_acquire))
                {
                    (void)Halo4ColdObservation_RetryFailed(halo4GateBase, halo4Generation);
                    halo4ManualRecoveryPending = 0;
                }
                if (Halo4ColdObservation_Pending(halo4Generation))
                    Halo4ColdObservation_Poll(
                        halo4GateBase, halo4GateSize, halo4Generation, true, g_halo4LevelLoadGate.ArrayProven());
            }
            // The camera core. It scans the module, so it takes
            // the same gate the observation does, and it additionally
            // requires that observation's PASS for this exact generation.
            // Teardown must still run on a tick where Halo 4 is NOT the
            // active title, which is why the call sits outside the
            // active/inactive branch above.
            {
                const uint32_t halo4Generation = TitleAdapter_GetGeneration(GameTitle::Halo4);
                // The frame cap (Reach's binding, Halo 4 sites).
                ReachFrameCapTick(halo4GateBase, halo4GateSize, halo4Generation,
                    halo4Active && halo4GateSampled && activeLevelRunning, g_halo4FrameCap, ResolveHalo4FrameCap,
                    g_config.titles_uncap_framerate);
                Halo4CameraCore_Poll(
                    halo4GateBase, halo4GateSize, halo4Generation, halo4Active && halo4GateSampled, activeLevelRunning);
                // The HUD-basis writer is headset-rejected. Keep its evidence-backed
                // implementation dormant until it is isolated from the
                // rejected crosshair candidate and tested independently.
                Halo4CameraLogTick();
            }
        }
    }
} // namespace halo4_bridge
bool Game_Halo4OwnsLookPitch()
{
    return Halo4LookPitchOwned();
}

bool Game_Halo4LiveCuiCanvas(float& baseY, float& hideShift)
{
    return Halo4SelectCuiCaptureCanvas(g_halo4Camera.cuiReticleCaptureBaseX.load(std::memory_order_relaxed),
        g_halo4Camera.cuiReticleCaptureBaseY.load(std::memory_order_relaxed),
        g_halo4Camera.cuiReticleBaseX.load(std::memory_order_relaxed),
        g_halo4Camera.cuiReticleBaseY.load(std::memory_order_relaxed), baseY, hideShift);
}

void Game_Halo4UpdateVrTurn(const VrPadState& pad)
{
    Halo4ApplyVrTurn(pad);
}

bool Game_Halo4PhysicalMeleePulseActive(uint64_t nowMs)
{
    if (!kEnableHalo4PhysicalMeleeStage9 || TitleAdapter_GetActiveTitle() != GameTitle::Halo4 ||
        !g_halo4Camera.armed.load(std::memory_order_acquire) ||
        g_halo4Camera.teardownRequested.load(std::memory_order_acquire) || !g_enabled.load(std::memory_order_acquire) ||
        !VR_IsStereoEnabled() || !g_halo4WorldCollision.physicalMeleeConfigured.load(std::memory_order_acquire))
        return false;

    const float requiredSpeed = g_halo4WorldCollision.physicalMeleeRequiredSpeed.load(std::memory_order_relaxed);
    for (int hand = 0; hand < 2; ++hand)
    {
        float speed = -1.0f;
        const bool velocityValid = VR_GetPhysicalMeleeSpeed(hand == 0, speed);
        if (velocityValid && speed >= 0.0f)
        {
            g_halo4WorldCollision.physicalMeleeVelocitySamples.fetch_add(1, std::memory_order_relaxed);
            const uint32_t speedCmPerSecond = static_cast<uint32_t>(std::fmin(speed * 100.0f, 100000.0f));
            uint32_t priorPeak =
                g_halo4WorldCollision.physicalMeleePeakCentimetresPerSecond.load(std::memory_order_relaxed);
            while (priorPeak < speedCmPerSecond &&
                   !g_halo4WorldCollision.physicalMeleePeakCentimetresPerSecond.compare_exchange_weak(
                       priorPeak, speedCmPerSecond, std::memory_order_relaxed, std::memory_order_relaxed))
            {
            }
        }

        std::atomic<bool>& latch = g_halo4WorldCollision.physicalMeleeVelocityLatched[hand];
        const bool wasLatched = latch.load(std::memory_order_acquire);
        const Halo4PhysicalMeleeVelocityDecision decision =
            Halo4UpdatePhysicalMeleeVelocityLatch(velocityValid, speed, requiredSpeed, wasLatched);
        if (!decision.latched)
        {
            latch.store(false, std::memory_order_release);
            continue;
        }
        if (!decision.trigger)
            continue;
        bool expected = false;
        if (!latch.compare_exchange_strong(expected, true, std::memory_order_acq_rel, std::memory_order_relaxed))
            continue;

        g_halo4WorldCollision.physicalMeleeCandidates.fetch_add(1, std::memory_order_relaxed);
        const uint64_t cooldownUntil =
            g_halo4WorldCollision.physicalMeleeCooldownUntilMs.load(std::memory_order_relaxed);
        if (nowMs >= cooldownUntil)
        {
            g_halo4WorldCollision.physicalMeleePulseUntilMs.store(
                nowMs + kHalo4PhysicalMeleePulseMs, std::memory_order_release);
            g_halo4WorldCollision.physicalMeleeCooldownUntilMs.store(
                nowMs + Halo4PhysicalMeleeCooldownMs(), std::memory_order_release);
            g_halo4WorldCollision.physicalMeleePulses.fetch_add(1, std::memory_order_relaxed);
        }
        else
        {
            g_halo4WorldCollision.physicalMeleeCooldowns.fetch_add(1, std::memory_order_relaxed);
        }
    }
    const uint64_t pulseUntil = g_halo4WorldCollision.physicalMeleePulseUntilMs.load(std::memory_order_acquire);
    return nowMs != 0 && nowMs < pulseUntil;
}
// Halo 4's answer to "the HMD owns pitch": a different implementation for the
// same player experience as Halo 3, because
// this engine's look ownership sits at a different rung than Halo 3's.
//
// Halo 3 zeroes BOTH stick axes because ApplyVrTurn owns yaw and a controller
// aim loop keeps the gun on the VR sight. Halo 4 has neither yet, so zeroing
// yaw as well would leave the player unable to turn and unable to shoot where
// they turned. The horizontal axis therefore stays with the engine - it turns
// the body, the aim and the view together, exactly as it always has - and only
// the VERTICAL axis changes hands. That axis is the one headset testing flagged:
// artificial pitch fights the inner ear in a way artificial yaw does not.
//
// Returns false when the loop declines this poll, which the caller reads as
// "hold the stick at zero": we still own the axis, we simply have nothing to
// command. It never falls back to the raw stick, because that would put the
// artificial pitch back with no warning.
bool Game_ComputeHalo4PitchStick(float& outRy)
{
    outRy = 0.0f;
    static Halo4PitchServo servo;
    static uint64_t lastSerial = 0;
    static uint64_t lastSerialChangeMs = 0;
    static float heldCommand = 0.0f;
    static std::atomic<int> lastBlock{-1};
    auto blocked = [](int reason, const char* what)
    {
        if (lastBlock.exchange(reason) != reason)
            LOG("Halo 4 look pitch: loop idle: %s", what);
        return false;
    };

    if (!Halo4LookPitchOwned())
    {
        Halo4ResetPitchServo(servo);
        lastSerial = 0;
        lastSerialChangeMs = 0;
        heldCommand = 0.0f;
        return false;
    }
    if (!g_halo4Camera.enginePitchValid.load(std::memory_order_acquire))
        return blocked(1, "the engine's own camera pitch has not been read yet");

    // Freshness, not liveness-by-assumption. The render thread republishes the
    // serial once per owned frame; if it stops advancing, no owned render is
    // happening (a load, a menu, a teardown) and the last pitch we saw is
    // stale. Commanding a stick against a stale error is exactly the runaway
    // this gate exists to prevent.
    const uint64_t serial = g_halo4Camera.enginePitchSerial.load(std::memory_order_acquire);
    const uint64_t now = GetTickCount64();
    bool freshFrame = false;
    if (serial != lastSerial)
    {
        lastSerial = serial;
        lastSerialChangeMs = now;
        freshFrame = true;
    }
    else if (!lastSerialChangeMs || now - lastSerialChangeMs > 250)
    {
        Halo4ResetPitchServo(servo);
        heldCommand = 0.0f;
        g_halo4Camera.pitchServoStalls.fetch_add(1, std::memory_order_relaxed);
        return blocked(2, "no owned Halo 4 frame in the last 250 ms");
    }

    // Measured in a headset session: MCC polls XInput about SIX
    // times per rendered frame (1354 commanded + 96 parked polls in 2 s against
    // a 120 Hz publication). Stepping the loop per poll fed AimServoObserve
    // five zero-steps and one whole-frame step, and its deliberate
    // rise-immediately/decay-slowly rule latched that lump: `min step` sat at
    // 2.758 deg, which widened the rest band to 1.65/4.14 deg and left the gun
    // parked up to ~1.7 deg off the view. Step once per NEW publication and
    // hold the command across the polls that share a frame, so one observation
    // corresponds to one issued command exactly as the shared servo assumes.
    if (!freshFrame)
    {
        outRy = heldCommand;
        return true;
    }

    float q[4], p[3];
    if (!VR_GetHeadPose(q, p))
    {
        Halo4ResetPitchServo(servo);
        heldCommand = 0.0f;
        return blocked(3, "headset not tracked");
    }
    Halo4HeadOrientation head{};
    if (!Halo4DecodeHeadOrientation(q, head))
    {
        Halo4ResetPitchServo(servo);
        heldCommand = 0.0f;
        return blocked(4, "head orientation could not be decoded");
    }
    lastBlock.store(0, std::memory_order_relaxed);

    // The same target the render camera is already pointing at, built from the
    // same head sample, the same signs and the same trim - so the gun converges
    // on the middle of the view rather than somewhere near it.
    const float target =
        Clamp(g_pitchSign.load(std::memory_order_relaxed) * head.pitch + g_pitchTrim.load(std::memory_order_relaxed),
            -1.5f, 1.5f);
    const float enginePitch = g_halo4Camera.enginePitch.load(std::memory_order_relaxed);
    outRy = Halo4PitchServoStep(servo, enginePitch, target, kHalo4PitchServoGain);
    heldCommand = outRy;

    g_halo4Camera.lastPitchTarget.store(target, std::memory_order_relaxed);
    g_halo4Camera.lastPitchError.store(target - enginePitch, std::memory_order_relaxed);
    g_halo4Camera.lastPitchCommand.store(outRy, std::memory_order_relaxed);
    g_halo4Camera.pitchServoDirection.store(servo.direction, std::memory_order_relaxed);
    g_halo4Camera.pitchServoMinStep.store(servo.axis.minStep, std::memory_order_relaxed);
    (outRy == 0.0f ? g_halo4Camera.pitchParkedFrames : g_halo4Camera.pitchCommandedFrames)
        .fetch_add(1, std::memory_order_relaxed);
    return true;
}
namespace halo4_bridge
{
    // Halo 4 presentation.
    // Returns true when this title owned the tick (Game_AutoVrTick returns immediately).
    bool AutoVrTickHalo4()
    {
        if (TitleAdapter_GetActiveTitle() == GameTitle::Halo4)
        {
            bool halo4Paused = false;
            const bool halo4PauseKnown = Halo4ReadNativePaused(halo4Paused);
            const uint64_t halo4Now = GetTickCount64();
            if (halo4PauseKnown)
            {
                if (!g_halo4Restoration.pauseStateKnown || g_halo4Restoration.pauseLast != halo4Paused)
                {
                    g_halo4Restoration.pauseStateKnown = true;
                    g_halo4Restoration.pauseLast = halo4Paused;
                    LOG("Halo 4 pause state: native reason 3=%d, presentation "
                        "target=%d",
                        halo4Paused ? 1 : 0, VR_IsPausePresentationTarget() ? 1 : 0);
                }
                const bool targetPaused = VR_IsPausePresentationTarget();
                if (targetPaused != halo4Paused)
                {
                    if (!g_halo4Restoration.pauseMismatchSinceMs ||
                        g_halo4Restoration.pauseMismatchValue != halo4Paused)
                    {
                        g_halo4Restoration.pauseMismatchSinceMs = halo4Now;
                        g_halo4Restoration.pauseMismatchValue = halo4Paused;
                    }
                    else if (halo4Now - g_halo4Restoration.pauseMismatchSinceMs >= 50)
                    {
                        VR_RequestPausePresentation(halo4Paused);
                        LOG("Halo 4 pause state: authoritative reason 3 restored "
                            "presentation to %s",
                            halo4Paused ? "head-locked 2D" : "stereo 3D");
                        g_halo4Restoration.pauseMismatchSinceMs = 0;
                    }
                }
                else
                    g_halo4Restoration.pauseMismatchSinceMs = 0;
            }
            // Halo 4 owns stereo presentation exactly while its camera core is
            // armed. g_enabled is also the shared presentation gate used by the
            // wrapper detour; the camera core applies the HMD head pose and 6DOF. The
            // heartbeat is the shared resolver's liveness signal: without it Halo 4
            // never qualifies as runtime owner and its Stereo capability is denied.
            const bool halo4StereoUsable = g_halo4Camera.armed.load(std::memory_order_acquire) &&
                                           !g_halo4Camera.sceneTargetMissing.load(std::memory_order_acquire);
            if (halo4StereoUsable)
            {
                if (!g_enabled.load(std::memory_order_relaxed) || !VR_IsStereoEnabled())
                {
                    g_enabled.store(true, std::memory_order_release);
                    Game_ForcePositional();
                    if (!VR_IsStereoEnabled())
                        VR_ToggleStereo();
                    LOG("Halo 4 immersive VR ON: stereo geometry, head "
                        "tracking, 6DOF and headset-owned look pitch are live. "
                        "Halo 4's CUI arrives inside the captured scene target, so "
                        "it needs no separate HUD redirect (user-confirmed "
                        "2026-08-08); controller aim is live and the optional "
                        "current-eye floating-hand/gun transaction is no-IK; the "
                        "free hand aligns Halo 4's official left_hand marker to the "
                        "H3/ODST/Reach controller frame, while support preserves the "
                        "exact accepted frozen right-aim parent");
                }
                const uint32_t halo4Generation = TitleAdapter_GetGeneration(GameTitle::Halo4);
                if (halo4Generation)
                {
                    TitleAdapter_PublishHeartbeat(GameTitle::Halo4, halo4Generation, GetTickCount64());
                    // Without this, nothing publishes a runtime mode for Halo 4,
                    // and two shared features are gated on it, which is
                    // exactly how Reach lost its rumble: ApplyControllerHaptics
                    // requires Gameplay/Vehicle/Turret, and
                    // Game_MoveStickIsLocomotion decides there whether the left
                    // stick walks head-relative. An armed Halo 4 core with stereo
                    // live is this title's gameplay proof; the disarmed branch
                    // below stops the heartbeat, which expires ownership and drops
                    // the arm-gated capabilities with it.
                    TitleAdapter_PublishMode(GameTitle::Halo4, halo4Generation,
                        halo4PauseKnown && halo4Paused ? RuntimeMode::Paused : RuntimeMode::Gameplay);
                    // HUD-basis writes are disabled after headset rejection.
                }
            }
            else if (g_enabled.load(std::memory_order_relaxed) || VR_IsStereoEnabled())
            {
                // Disarmed: stop compositing before anything stale reaches a
                // headset, and release Halo 4's retained scene target.
                g_enabled.store(false, std::memory_order_release);
                VR_DetachGamePresentation();
            }
            return true;
        }
        return false;
    }
} // namespace halo4_bridge
bool Game_TelemetryReadHalo4EngineAim(
    float forwardOut[3], bool& pitchValidOut, float& pitchRadiansOut, uint64_t& pitchSerialOut) noexcept
{
    pitchValidOut = false;
    pitchRadiansOut = 0.0f;
    pitchSerialOut = 0;
    if (!forwardOut)
        return false;
    if (!g_halo4Camera.armed.load(std::memory_order_acquire) ||
        g_halo4Camera.teardownRequested.load(std::memory_order_acquire))
        return false;
    const uint32_t generation = g_halo4Camera.generation.load(std::memory_order_acquire);
    if (!generation)
        return false;
    // The stereo transaction publishes pitch, then the whole forward, then
    // valid (release) and bumps the serial (release). The serial is read first
    // and re-checked after, so the recorded vector is always a genuine
    // published sample, never a fabricated one. It is NOT a version-bracketed
    // pair: the payload is stored BEFORE the serial is bumped and the two
    // serial reads are not fenced around it, so the returned serial may differ
    // from its payload by one writer iteration - treat serial +/-1 as the
    // tolerance and never assert an exact serial-to-payload pairing. This
    // publication is pre-existing and feeds the gameplay aim loop, so the
    // recorder documents the tolerance instead of changing it.
    const uint64_t serial = g_halo4Camera.enginePitchSerial.load(std::memory_order_acquire);
    const bool pitchValid = g_halo4Camera.enginePitchValid.load(std::memory_order_acquire);
    const float pitch = g_halo4Camera.enginePitch.load(std::memory_order_relaxed);
    const float sample[3] = {g_halo4Camera.engineAimForward[0].load(std::memory_order_relaxed),
        g_halo4Camera.engineAimForward[1].load(std::memory_order_relaxed),
        g_halo4Camera.engineAimForward[2].load(std::memory_order_relaxed)};
    if (g_halo4Camera.enginePitchSerial.load(std::memory_order_acquire) != serial ||
        g_halo4Camera.generation.load(std::memory_order_acquire) != generation)
        return false;
    if (!pitchValid || !serial || !std::isfinite(pitch) || !std::isfinite(sample[0]) || !std::isfinite(sample[1]) ||
        !std::isfinite(sample[2]))
        return false;
    memcpy(forwardOut, sample, sizeof(sample));
    pitchValidOut = true;
    pitchRadiansOut = pitch;
    pitchSerialOut = serial;
    return true;
}

bool Game_TelemetryReadHalo4Camera(float eyeOut[3], uint64_t& serialOut) noexcept
{
    serialOut = 0;
    if (!eyeOut)
        return false;
    // The recorded field must read exact zeros whenever this fails; zero the
    // caller's buffer here so the invalid/absent label never depends on its
    // prior contents.
    eyeOut[0] = 0.0f;
    eyeOut[1] = 0.0f;
    eyeOut[2] = 0.0f;
    if (!g_halo4Camera.armed.load(std::memory_order_acquire) ||
        g_halo4Camera.teardownRequested.load(std::memory_order_acquire))
        return false;
    const uint32_t generation = g_halo4Camera.generation.load(std::memory_order_acquire);
    if (!generation)
        return false;
    // stockCameraVersion is a version-counter seqlock (same discipline as
    // ReachReadCompletedEye and Halo2Observer6Dof_ReadPublishedPose): the
    // stereo transaction bumps it odd BEFORE it touches the payload and even
    // again AFTER, so an unchanged even version proves the position, the valid
    // flag and the serial all belong to one writer iteration. Re-reading the
    // serial alone could not prove that - the writer overwrites the position
    // before it bumps the serial, so a serial read back unchanged can still
    // cover a position stored by the next iteration. An odd or unstable
    // version is retried a bounded number of times; if it never settles, the
    // read fails open to invalid with zeros and serial 0 and the caller labels
    // the source absent, never a torn or cross-iteration position.
    for (int attempt = 0; attempt < 4; ++attempt)
    {
        const uint32_t before = g_halo4Camera.stockCameraVersion.load(std::memory_order_acquire);
        if (before & 1u)
            continue;
        const float sample[3] = {g_halo4Camera.stockCameraPosition[0].load(std::memory_order_relaxed),
            g_halo4Camera.stockCameraPosition[1].load(std::memory_order_relaxed),
            g_halo4Camera.stockCameraPosition[2].load(std::memory_order_relaxed)};
        const bool valid = g_halo4Camera.stockCameraValid.load(std::memory_order_relaxed);
        const uint64_t serial = g_halo4Camera.stockCameraSerial.load(std::memory_order_relaxed);
        // Order the payload loads (and the two flags that describe them) before
        // the version re-read, exactly as the Halo 2 observer reader does.
        std::atomic_thread_fence(std::memory_order_acquire);
        if (g_halo4Camera.stockCameraVersion.load(std::memory_order_acquire) != before)
            continue;
        if (g_halo4Camera.generation.load(std::memory_order_acquire) != generation)
            return false;
        if (!valid || !serial || !std::isfinite(sample[0]) || !std::isfinite(sample[1]) || !std::isfinite(sample[2]))
            return false;
        memcpy(eyeOut, sample, sizeof(sample));
        serialOut = serial;
        return true;
    }
    return false;
}
namespace
{
    // candidate == UINT32_MAX selects the primary slot (Capture path); any
    // other value must resolve to primary slot 0 through the equipped-weapon
    // mapping (the FP skinning path, where the observed record is the
    // candidate). `primaryAbsentOut` (optional) reports an explicit raw empty
    // primary slot from the same guarded read (F03/F17); it is never set by a
    // validation/lifecycle failure. This is the shared production body: the
    // diagnostic probe below and the persistent support-grip producer both
    // read through it, so the native read logic exists once.
    bool Halo4WeaponOwnerEvidence(uint32_t expectedGeneration, uint32_t candidate, uint32_t& unitOut,
        uint32_t& weaponOut, uint32_t& detailOut, bool* primaryAbsentOut) noexcept
    {
        if (primaryAbsentOut)
            *primaryAbsentOut = false;
        if (g_halo4Camera.generation.load(std::memory_order_acquire) != expectedGeneration)
        {
            detailOut = kDiagnosticGuard;
            return false;
        }
        Halo4VehicleInputState seat{};
        const char* vehicleReason = nullptr;
        const bool vehicleUsable = Halo4ReadVehicleInputEx(seat, &vehicleReason) && !seat.seated &&
                                   seat.unit != UINT32_MAX && (seat.unit >> 16) &&
                                   Halo4MuzzleTargetStorage(seat.unit) != nullptr;
        uint32_t owner = seat.unit;
        if (!vehicleUsable)
        {
            // The optional vehicle-input helper is an admission shortcut, not
            // a prerequisite for on-foot owner proof. When its runtime reader
            // cannot establish the local output unit, the engine's own
            // first-person weapon record carries the same evidence. Fail
            // closed: every stage below must pass or the owner stays Unknown.
            if (!g_halo4Camera.armed.load(std::memory_order_acquire) ||
                g_halo4Camera.teardownRequested.load(std::memory_order_acquire))
                return false;
            const auto* tls = Halo4MuzzleTls();
            const auto* fpRecord = tls ? Halo4VehicleRead<const uint8_t*>(tls, 0x6A0) : nullptr;
            const uint32_t fpFlags = fpRecord ? Halo4VehicleRead<uint32_t>(fpRecord, 0) : 0u;
            const uint32_t fpUnit = fpRecord ? Halo4VehicleRead<uint32_t>(fpRecord, 4) : UINT32_MAX;
            const auto* fpUnitObject =
                (fpRecord && fpUnit != UINT32_MAX && (fpUnit >> 16)) ? Halo4MuzzleObject(fpUnit, 1) : nullptr;
            // H4EK E80C00/E977E0: on foot means no parent (+0x24) and no
            // controlling parent (+0x694), mirroring Halo4MuzzleTargetStorage.
            uint32_t fallbackUnit = UINT32_MAX;
            const auto stage = halo4_owner_evidence::ResolveFromFpRecord(fpRecord != nullptr, fpFlags, fpUnit,
                fpUnitObject != nullptr, fpUnitObject ? Halo4VehicleRead<uint32_t>(fpUnitObject, 0x24) : UINT32_MAX,
                fpUnitObject ? Halo4VehicleRead<uint32_t>(fpUnitObject, 0x694) : UINT32_MAX, fallbackUnit);
            static std::atomic<uint32_t> s_vehicleFallbackGeneration{0};
            if (expectedGeneration && s_vehicleFallbackGeneration.exchange(
                                          expectedGeneration, std::memory_order_relaxed) != expectedGeneration)
                // Statement of fact: entry into the fallback does not imply the
                // later stages resolved. The outcome is logged at the return
                // site that actually decided it.
                support_diagnostics::Record(support_diagnostics::Event::H4OwnerFallback);
            if (stage != halo4_owner_evidence::Stage::Ok)
                return false;
            owner = fallbackUnit;
        }
        uint32_t weapons[2]{};
        if (!Halo4ReadMuzzleWeapons(owner, weapons, primaryAbsentOut))
        {
            static std::atomic<uint32_t> s_notResolvedMuzzleWeapons{0};
            static std::atomic<uint32_t> s_notResolvedPrimaryAbsent{0};
            const bool absent = primaryAbsentOut && *primaryAbsentOut;
            auto& latch = absent ? s_notResolvedPrimaryAbsent : s_notResolvedMuzzleWeapons;
            if (expectedGeneration &&
                latch.exchange(expectedGeneration, std::memory_order_relaxed) != expectedGeneration)
                support_diagnostics::Record(support_diagnostics::Event::H4OwnerWeaponsUnavailable);
            return false;
        }
        if (weapons[0] == UINT32_MAX)
        {
            // Explicit empty primary slot (role byte 0xFF): a real KnownAbsent
            // state, not an unexplained refusal.
            static std::atomic<uint32_t> s_notResolvedPrimaryAbsent{0};
            if (expectedGeneration && s_notResolvedPrimaryAbsent.exchange(
                                          expectedGeneration, std::memory_order_relaxed) != expectedGeneration)
                support_diagnostics::Record(support_diagnostics::Event::H4OwnerAbsent);
            return false;
        }
        if (candidate != UINT32_MAX &&
            ResolveEquippedWeaponSlot(candidate, weapons[0], weapons[1], true, weapons[1] != UINT32_MAX) != 0)
        {
            static std::atomic<uint32_t> s_notResolvedCandidate{0};
            if (expectedGeneration &&
                s_notResolvedCandidate.exchange(expectedGeneration, std::memory_order_relaxed) != expectedGeneration)
                support_diagnostics::Record(support_diagnostics::Event::H4OwnerCandidateMismatch);
            return false;
        }
        // A validated local on-foot owner, always carrying the semantic
        // primary slot handle -- never the observed candidate.
        unitOut = owner;
        weaponOut = weapons[0];
        detailOut = ((weapons[1] != UINT32_MAX) ? 1u : 0u) | 2u;
        // Primary-slot FP producer agreement, mirroring the gated muzzle
        // capture without its option/bindings gate. Recorded raw here; the FP
        // commit requires the bit so the Capture probe measures the
        // inventory/ownership view rather than the FP assembly view.
        const auto* tls = Halo4MuzzleTls();
        const auto* fp = tls ? Halo4VehicleRead<const uint8_t*>(tls, 0x6A0) : nullptr;
        if (fp && (Halo4VehicleRead<uint32_t>(fp, 0) & 2) && Halo4VehicleRead<uint32_t>(fp, 4) == owner &&
            Halo4VehicleRead<uint32_t>(fp, 0x6C) == weapons[0])
            detailOut |= kDiagnosticFpAgreement;
        if (g_halo4Camera.generation.load(std::memory_order_acquire) != expectedGeneration)
        {
            detailOut = kDiagnosticGuard;
            static std::atomic<uint32_t> s_notResolvedGeneration{0};
            if (expectedGeneration &&
                s_notResolvedGeneration.exchange(expectedGeneration, std::memory_order_relaxed) != expectedGeneration)
                support_diagnostics::Record(support_diagnostics::Event::H4OwnerGenerationMismatch);
            return false;
        }
        static std::atomic<uint32_t> s_resolvedGeneration{0};
        if (expectedGeneration &&
            s_resolvedGeneration.exchange(expectedGeneration, std::memory_order_relaxed) != expectedGeneration)
            support_diagnostics::Record(support_diagnostics::Event::H4OwnerResolved);
        return true;
    }
} // namespace
namespace halo4_bridge
{
    // Weapon-order diagnostic entry: byte-for-byte the production body's
    // result, kept as the diagnostic probe's own name (the same body is what
    // the persistent support-grip producer reads).
    bool DiagnosticHalo4WeaponOwner(uint32_t expectedGeneration, uint32_t candidate, uint32_t& unitOut,
        uint32_t& weaponOut, uint32_t& detailOut) noexcept
    {
        return Halo4WeaponOwnerEvidence(expectedGeneration, candidate, unitOut, weaponOut, detailOut, nullptr);
    }
    // SEH-guarded tri-state observation of Halo 4's semantic primary owner.
    // A native fault is Unknown, never absence; the observation still names
    // the lifecycle generation so a coherent Unknown record can be published
    // for the current lifecycle (mirroring Game_ReadPrimaryWeaponEvidence's
    // per-title branches).
    PrimaryWeaponEvidence Halo4ObservePrimaryWeapon(uint32_t generation, uint32_t candidate) noexcept
    {
        PrimaryWeaponEvidence out{};
        out.generation = generation;
        uint32_t unit = UINT32_MAX;
        uint32_t weapon = UINT32_MAX;
        uint32_t detail = 0;
        bool absent = false;
        bool ok = false;
        __try
        {
            ok = Halo4WeaponOwnerEvidence(generation, candidate, unit, weapon, detail, &absent);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            GUARD_COUNT("reading the Halo 4 primary weapon faulted");
            ok = false;
            absent = false;
        }
        out.detail = detail;
        if (ok)
        {
            out.state = static_cast<uint8_t>(support_grip::OwnerEvidence::KnownPresent);
            out.unit = unit;
            out.weapon = weapon;
        }
        else
            out.state = static_cast<uint8_t>(
                absent ? support_grip::OwnerEvidence::KnownAbsent : support_grip::OwnerEvidence::Unknown);
        return out;
    }
} // namespace halo4_bridge
namespace
{
    // One safe-context support-grip invocation read. `candidate` is the
    // semantic primary (UINT32_MAX) at the pair's decision boundary; a unit
    // handle must never be offered here (F04). The shared resolver publishes
    // the tri-state evidence -- including an explicit Unknown, so no stale
    // KnownPresent survives -- and returns this invocation's relation to the
    // live relationship. No diagnostic-session dependency.
    SupportInvocationResolution Halo4ReadSupportInvocation(uint32_t candidate) noexcept
    {
        const uint32_t generation = TitleAdapter_GetGeneration(GameTitle::Halo4);
        const PrimaryWeaponEvidence evidence = (generation && VR_SupportGripWiredForTitle(GameTitle::Halo4))
                                                   ? Halo4ObservePrimaryWeapon(generation, candidate)
                                                   : PrimaryWeaponEvidence{};
        return Game_ResolveSupportInvocation(GameTitle::Halo4, evidence, true, evidence, true, true);
    }
} // namespace

namespace halo4_bridge
{
    // Core calls these. Their definitions (or an .inl forward declaration) stay in the anonymous namespace
    // above, beside the Halo 4 code that calls them; each forwarder only forwards.
    bool Halo4ContactMeleeReady()
    {
        return ::Halo4ContactMeleeReady();
    }
    const uint8_t* Halo4ContactObject(uint32_t handle, bool requireBiped)
    {
        return ::Halo4ContactObject(handle, requireBiped);
    }
    const uint8_t* Halo4ContactBiped(uint32_t handle)
    {
        return ::Halo4ContactBiped(handle);
    }
    bool Halo4ReadVehicleInput(Halo4VehicleInputState& output) noexcept
    {
        return ::Halo4ReadVehicleInput(output);
    }
    uint64_t Halo4ReadVehicleIdentity(const uint8_t* parent) noexcept
    {
        return ::Halo4ReadVehicleIdentity(parent);
    }
    CinematicControlState ReadHalo4CinematicControl() noexcept
    {
        return ::ReadHalo4CinematicControl();
    }
    // Core reads two fields of the contact-melee runtime, whose type is defined inside
    // halo4_contact_melee_runtime.inl (anonymous namespace), so it reads them through these.
    uint32_t Halo4ContactGeneration() noexcept
    {
        return g_halo4Contact.generation;
    }
    const uint8_t* Halo4ContactObjectOfKinds(uint32_t handle, uint32_t kinds)
    {
        return g_halo4Contact.object(handle, kinds);
    }
} // namespace halo4_bridge
