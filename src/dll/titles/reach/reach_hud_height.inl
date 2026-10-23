// Halo Reach HUD height and per-element placement: moves the native CHUD basis and the motion-sensor origin.
// Only the height scalar and the element translation change; a failure leaves the native result.
#pragma once
// Included once, by src/dll/core/game.cpp; also compiled by
//   tests/reach_hud_height_tests.cpp, which supplies its own versions. Not a standalone header:
//   it uses what the host defines above its include line, namely g_enabled, g_reachCamera.
// Included inside game.cpp's Reach implementation and the isolated production
// wrapper fixture. HREK 0x91C490 -> pinned Reach 0x2DB54C proves this SIX-argument
// ABI: the native 0x34-byte basis is argument five, not Halo 3's argument four.
using ReachHudAnchorBasisFn = bool(__fastcall*)(int, int, void*, void*, void*, bool*);
ReachHudAnchorBasisFn g_reachOrigHudAnchorBasis = nullptr;
void* g_reachHudAnchorBasisTarget = nullptr;
std::atomic<bool> g_reachHudHeightEnabled{false};
std::atomic<uint64_t> g_reachHudHeightApplied{0};
std::atomic<uint64_t> g_reachHudHeightRefused{0};
thread_local bool g_reachHudHeightRedirected = false;
thread_local uint32_t g_reachHudAnchorDepth = 0;
// Per-element placement. The draw-widget hook publishes the element of the
// widget being drawn (hud_element::Element, -1 = none). Each anchor chain
// records the screen anchor it resolved to and whether any level projected a
// world position; the outermost call applies one translation per element.
thread_local int g_reachHudDrawElement = -1;
// Set by game.cpp: moves the CHUD sensor origin (the motion-sensor blip layer) by a HUD-pixel shift.
constinit void (*g_reachMotionShiftHook)(float dx, float dy) noexcept = nullptr;
thread_local int g_reachHudChainAnchor = 0;
thread_local bool g_reachHudChainProjected = false;
// Virtual HUD size, learned from the game's own bottom-right anchor output
// (it writes the virtual width/height as X/Y), for the camera generation that
// produced it. Another generation counts as unknown until relearned; a new
// size (resolution change) replaces it. Logged once per generation and size.
std::atomic<float> g_reachHudVirtualWidth{0.0f};
std::atomic<float> g_reachHudVirtualHeight{0.0f};
std::atomic<uint32_t> g_reachHudVirtualGeneration{0};
std::atomic<uint64_t> g_reachHudElementApplied{0};

constexpr uintptr_t kReachHudAnchorBasisRva = 0x2DB54C;
constexpr uintptr_t kReachHudAnchorBasisEndRva = 0x2DC1BE;
constexpr char kReachHudAnchorBasisEntryAob[] = "48 8B C4 48 89 58 08 4C 89 48 20 55 56 57 41 54 41 55 41 56 "
                                                "41 57 48 8D 68 B1 48 81 EC B0 00 00 00 48 8B 7D 77 4D 8B E8 "
                                                "4C 8B 75 7F 41 B4 01 4C 8B 15 ?? ?? ?? ?? 33 DB";
constexpr uintptr_t kReachHudAnchorBasisOutputRva = 0x2DB65B;
constexpr char kReachHudAnchorBasisOutputAob[] = "F3 0F 11 47 28 F3 0F 11 4F 2C E9 80 00 00 00";
constexpr uintptr_t kReachHudAnchorBitmapCallRva = 0x2DF2D5;
constexpr uintptr_t kReachHudAnchorTextCallRva = 0x2DCA43;
constexpr uintptr_t kReachHudAnchorModelCallRva = 0x2DEC72;
constexpr uintptr_t kReachHudAnchorParentCallRva = 0x2DA8BA;

static bool ReachHudHeightOwnsStereo() noexcept
{
    const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
    // Both native CHUD phases use this producer. Unlike authored reticle
    // capture, translating the returned HUD basis also applies to the late
    // compositing phase, so admission must not require per-eye render TLS.
    return g_reachHudHeightEnabled.load(std::memory_order_acquire) && generation != 0 &&
           TitleAdapter_GetActiveTitle() == GameTitle::HaloReach &&
           TitleAdapter_GetGeneration(GameTitle::HaloReach) == generation &&
           g_reachCamera.installed.load(std::memory_order_acquire) &&
           g_reachCamera.armed.load(std::memory_order_acquire) &&
           !g_reachCamera.teardownRequested.load(std::memory_order_acquire) &&
           g_enabled.load(std::memory_order_relaxed) && VR_IsStereoEnabled();
}

static void ReachAdjustHudAnchorHeight(void* basis) noexcept
{
    if (!basis || g_reachHudHeightRedirected || !ReachHudHeightOwnsStereo())
        return;
    const float height = g_config.hud_vertical_offset;
    if (!std::isfinite(height) || height < kHudHeightMin || height > kHudHeightMax)
    {
        g_reachHudHeightRefused.fetch_add(1, std::memory_order_relaxed);
        return;
    }
    __try
    {
        // Both screen and projected-object anchors prove +0x2C is virtual
        // pixel Y. The sixth output is not a world-unit flag: false-output
        // anchors have already projected their object positions. Only this
        // scalar changes; native axes/scale/visibility/anchor/flag are preserved.
        float* const y = reinterpret_cast<float*>(static_cast<uint8_t*>(basis) + 0x2C);
        const float current = *y;
        const float wanted = current - height;
        if (!std::isfinite(current) || !std::isfinite(wanted))
        {
            g_reachHudHeightRefused.fetch_add(1, std::memory_order_relaxed);
            return;
        }
        *y = wanted;
        g_reachHudHeightApplied.fetch_add(1, std::memory_order_relaxed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("adjusting the Reach HUD anchor height faulted");
        // Only height fails. Leave native result and the camera/session alive;
        // the worker reports the refusal outside this hot hook.
        g_reachHudHeightRefused.fetch_add(1, std::memory_order_relaxed);
    }
}

// Called after every anchor level returns. Records the chain's anchor and
// learns the virtual HUD size from a bottom-right (7) result.
static void ReachObserveHudAnchor(int anchorType, const void* basis) noexcept
{
    if (hud_element::IsProjectedReachAnchor(anchorType))
    {
        g_reachHudChainProjected = true;
        return;
    }
    if (anchorType < 1 || anchorType > 7)
        return;
    if (!g_reachHudChainAnchor)
        g_reachHudChainAnchor = anchorType;
    if (anchorType != 7 || !basis)
        return;
    float width = 0, height = 0;
    __try
    {
        const auto* bytes = static_cast<const uint8_t*>(basis);
        width = *reinterpret_cast<const float*>(bytes + 0x28);
        height = *reinterpret_cast<const float*>(bytes + 0x2C);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("reading the Reach HUD anchor faulted");
        return;
    }
    if (!hud_element::PlausibleVirtualSize(width, height))
        return;
    const uint32_t generation = g_reachCamera.generation.load(std::memory_order_acquire);
    if (!generation)
        return;
    const bool changed = g_reachHudVirtualGeneration.load(std::memory_order_relaxed) != generation ||
                         g_reachHudVirtualWidth.load(std::memory_order_relaxed) != width ||
                         g_reachHudVirtualHeight.load(std::memory_order_relaxed) != height;
    if (!changed)
        return;
    g_reachHudVirtualWidth.store(width, std::memory_order_relaxed);
    g_reachHudVirtualHeight.store(height, std::memory_order_relaxed);
    g_reachHudVirtualGeneration.store(generation, std::memory_order_release);
    LOG("Reach HUD elements: virtual HUD size %.0fx%.0f learned for generation %u", width, height, generation);
}

// Per-element translation of the completed outermost basis (X +0x28, Y +0x2C).
// Off unless hud_per_element; never for redirected (captured) widgets,
// projected anchors or widgets outside the five elements.
static void ReachAdjustHudElement(void* basis) noexcept
{
    const int element = g_reachHudDrawElement;
    // The blips are not widgets: the engine places them from the globals' sensor origin. Move that
    // origin with the tracker (zero shift restores it) so the dots stay inside the moved frame.
    if (element == hud_element::Motion && g_reachMotionShiftHook && basis && !g_reachHudHeightRedirected &&
        ReachHudHeightOwnsStereo())
    {
        float sx = 0, sy = 0;
        if (g_config.hud_per_element)
        {
            const bool current = g_reachHudVirtualGeneration.load(std::memory_order_acquire) ==
                                 g_reachCamera.generation.load(std::memory_order_acquire);
            (void)hud_element::Translation(g_config.hud_place[hud_element::SetReach].At(element), g_reachHudChainAnchor,
                g_reachHudChainProjected, current ? g_reachHudVirtualWidth.load(std::memory_order_relaxed) : 0.0f,
                current ? g_reachHudVirtualHeight.load(std::memory_order_relaxed) : 0.0f, sx, sy);
        }
        g_reachMotionShiftHook(sx, sy);
    }
    if (!g_config.hud_per_element || !basis || element < 0 || element >= hud_element::Count ||
        g_reachHudHeightRedirected || !ReachHudHeightOwnsStereo())
        return;
    const hud_element::Placement placement = g_config.hud_place[hud_element::SetReach].At(element);
    // A size learned by another generation is unknown here (manual x/y only).
    const bool sizeCurrent = g_reachHudVirtualGeneration.load(std::memory_order_acquire) ==
                             g_reachCamera.generation.load(std::memory_order_acquire);
    float dx = 0, dy = 0;
    if (!hud_element::Translation(placement, g_reachHudChainAnchor, g_reachHudChainProjected,
            sizeCurrent ? g_reachHudVirtualWidth.load(std::memory_order_relaxed) : 0.0f,
            sizeCurrent ? g_reachHudVirtualHeight.load(std::memory_order_relaxed) : 0.0f, dx, dy))
        return;
    __try
    {
        auto* bytes = static_cast<uint8_t*>(basis);
        float* const x = reinterpret_cast<float*>(bytes + 0x28);
        float* const y = reinterpret_cast<float*>(bytes + 0x2C);
        const float wantedX = *x + dx, wantedY = *y + dy;
        if (!std::isfinite(wantedX) || !std::isfinite(wantedY))
            return;
        *x = wantedX;
        *y = wantedY;
        g_reachHudElementApplied.fetch_add(1, std::memory_order_relaxed);
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        GUARD_COUNT("adjusting a Reach HUD element faulted");
    }
}

__declspec(noinline) bool __fastcall ReachHudAnchorBasisDetour(
    int userIndex, int anchorType, void* placementFlags, void* drawWidgetData, void* basis, bool* nativeAnchorFlag)
{
    g_reachCamera.activeCallbacks.fetch_add(1, std::memory_order_acq_rel);
    const bool outermost = g_reachHudAnchorDepth++ == 0;
    if (outermost)
    {
        g_reachHudChainAnchor = 0;
        g_reachHudChainProjected = false;
    }
    bool result = false;
    __try
    {
        ReachHudAnchorBasisFn original = g_reachOrigHudAnchorBasis;
        if (original)
        {
            result = original(userIndex, anchorType, placementFlags, drawWidgetData, basis, nativeAnchorFlag);
            if (result)
                ReachObserveHudAnchor(anchorType, basis);
            // Anchor 0 (parent) recurses through +0x2DA818 -> +0x2DA8BA.
            // Translate the fully composed outer result once; shifting both
            // parent and child would compound height with authored scale.
            if (result && outermost)
            {
                ReachAdjustHudAnchorHeight(basis);
                ReachAdjustHudElement(basis);
            }
        }
    }
    __finally
    {
        --g_reachHudAnchorDepth;
        g_reachCamera.activeCallbacks.fetch_sub(1, std::memory_order_acq_rel);
    }
    return result;
}
