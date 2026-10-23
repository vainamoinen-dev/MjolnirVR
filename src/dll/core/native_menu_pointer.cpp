// The VR laser pointer on MCC's own menus: the render thread publishes a ray hit, and MCC's
// window thread turns it into mouse messages and focus requests. Called from vr.cpp, input.cpp
// and menu.cpp through native_menu_pointer.h. Never writes game memory; Windows messages only.
#include "native_menu_pointer.h"
#include "../../common/game_menu_pointer.h"
#include "../../common/config.h"
#include "../../common/log.h"
#include "menu.h"
#include "title_adapter.h"
#include "vr.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cstdlib>

namespace
{
    HWND window = nullptr;
    UINT messageId = 0, focusMessageId = 0, keyMessageId = 0;
    std::atomic<WORD> pendingKey{0};
    std::atomic<unsigned long long> lastClickMs{0};
    // MCC input focus re-send (see NativeMenuPointer_ResendInputFocus).
    std::atomic<bool> focusQueued{false};
    std::atomic<unsigned> focusResent{0};
    std::atomic<uint64_t> sample{0};
    std::atomic<bool> queued{false};
    std::atomic<bool> delivered{false};
    std::atomic<unsigned> postFailures{0};
    std::atomic<uint64_t> visual{0};
    std::atomic<unsigned> moveCount{0}, clickCount{0};
    std::atomic<unsigned> cursorFailures{0};
    // Diagnostics: who holds the foreground while the laser is on the game screen,
    // and how often something other than the laser moved the cursor.
    std::atomic<unsigned> foregroundLost{0}, cursorStolen{0};
    char foregroundOwner[160] = "";
    POINT lastSet{-100000, -100000};
    std::atomic<const char*> frameStatus{"no-screen"}, deliveryStatus{"idle"}, currentTarget{"none"};
    // All remaining state belongs exclusively to MCC's window thread.
    game_menu_pointer::Pointer pointer;
    UINT_PTR timer = 0;
    bool mouseDown = false;
    bool faultLogged = false;

    // Tick of the last ray sample the render thread admitted (it passed the same menu-mode gate there and the
    // ray hit the screen). The runtime-mode / pause-target flags flicker for a frame or two when a game popup
    // opens (test runs logged the menu-mode gate refusing between hits), so the window thread trusts a fresh admission.
    std::atomic<DWORD> lastRenderAdmit{0};
    bool MenuModeNow()
    {
        if (game_menu_pointer::MenuMode(
                TitleAdapter_GetRuntimeMode(), VR_IsPausePresentation() && VR_IsPausePresentationTarget()))
            return true;
        const DWORD admit = lastRenderAdmit.load(std::memory_order_acquire);
        return admit && DWORD(GetTickCount() - admit) <= 400 &&
               VR_IsPausePresentation(); // never outside a pause screen
    }
    bool GatesExceptFocus()
    {
        return g_config.game_menu_pointer && !Menu_IsOpen() && MenuModeNow() && !VR_IsCutsceneTheaterActive() &&
               window && !IsIconic(window);
    }
    // Which gate refuses the laser right now (nullptr = none refuse). Names go in the log.
    const char* GateName()
    {
        if (!g_config.game_menu_pointer)
            return "gate-config-off";
        if (Menu_IsOpen())
            return "gate-f1-menu-open";
        if (!MenuModeNow())
            return "gate-not-menu-mode";
        if (VR_IsCutsceneTheaterActive())
            return "gate-cutscene-theater";
        if (!window)
            return "gate-no-window";
        if (IsIconic(window))
            return "gate-minimized";
        if (GetForegroundWindow() != window)
            return "gate-foreground-not-mcc";
        return nullptr;
    }
    bool Allowed()
    {
        return GatesExceptFocus() && GetForegroundWindow() == window;
    }
    // In the headset nothing on the desktop usually has focus (one headset session
    // logged MCC losing the foreground 1,762 times, mostly to no window at all), so
    // the pointer stayed gated and MCC fell back to controller mode. While the laser
    // is on a game menu, take the foreground back - at most once a second, borrowing
    // the current foreground thread's input state as Windows requires. Window
    // thread only.
    //
    // Etiquette (as the MCC input-focus fix): reclaim freely only when the
    // foreground is no window at all or a window of MCC's own process (never a
    // dialog owned by MCC's window, never while it is disabled). From ANOTHER
    // application only on deliberate VR controller input within the last second:
    // the player may have switched there on purpose. SetForegroundWindow is tried
    // first on its own; only if that fails is the foreground thread's input state
    // borrowed (AttachThreadInput), and never from a hung window.
    std::atomic<unsigned> focusTakes{0}, focusDeferred{0};
    std::atomic<DWORD> lastVrInput{0}; // tick of the last deliberate VR controller input (0 = none)
    DWORD lastFocusTry = 0;
    bool ForegroundOwnedByGame(HWND fg)
    {
        for (int depth = 0; fg && depth < 8; ++depth)
        {
            fg = GetWindow(fg, GW_OWNER);
            if (fg == window)
                return true;
        }
        return false;
    }
    bool ForegroundIsOtherApplication(HWND fg)
    {
        if (!fg)
            return false;
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        return pid && pid != GetCurrentProcessId();
    }
    void ReclaimForeground()
    {
        const DWORD now = GetTickCount();
        if (!window || GetForegroundWindow() == window || !GatesExceptFocus() || !IsWindowEnabled(window) ||
            !game_menu_pointer::FreshActive(sample.load(std::memory_order_acquire), now) ||
            DWORD(now - lastFocusTry) < 1000)
            return;
        const HWND fg = GetForegroundWindow();
        // Deferrals (an MCC-owned dialog, or another application without fresh VR
        // input) are counted at most once a second, like the attempts.
        static DWORD lastDeferral = 0;
        auto defer = [&]
        {
            if (!lastDeferral || DWORD(now - lastDeferral) >= 1000)
            {
                lastDeferral = now;
                focusDeferred.fetch_add(1, std::memory_order_relaxed);
            }
        };
        if (ForegroundOwnedByGame(fg))
        {
            defer();
            return;
        }
        if (ForegroundIsOtherApplication(fg))
        {
            const DWORD input = lastVrInput.load(std::memory_order_acquire);
            if (!input || DWORD(now - input) > 1000)
            {
                defer();
                return;
            }
        }
        lastFocusTry = now;
        BringWindowToTop(window);
        if (!SetForegroundWindow(window) && fg && !IsHungAppWindow(fg))
        {
            const DWORD me = GetCurrentThreadId();
            const DWORD fgThread = GetWindowThreadProcessId(fg, nullptr);
            const bool attached = fgThread && fgThread != me && AttachThreadInput(me, fgThread, TRUE);
            SetForegroundWindow(window);
            if (attached)
                AttachThreadInput(me, fgThread, FALSE);
        }
        if (GetForegroundWindow() == window)
            focusTakes.fetch_add(1, std::memory_order_relaxed);
    }
    // Moves the cursor to a screen point. Default: a real absolute mouse move via
    // SendInput, which MCC sees as mouse activity (raw input), so its menus follow
    // the laser in both directions; SetCursorPos produced no input event and MCC's
    // device switch left a stale highlight. menu_pointer_setcursorpos = 1 restores
    // the old SetCursorPos delivery. Window thread only.
    bool MoveCursor(POINT target)
    {
        if (g_config.menu_pointer_setcursorpos)
            return SetCursorPos(target.x, target.y) != FALSE;
        const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        if (vw <= 1 || vh <= 1)
            return SetCursorPos(target.x, target.y) != FALSE;
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dx = LONG(((long long)(target.x - vx) * 65535 + (vw - 1) / 2) / (vw - 1));
        input.mi.dy = LONG(((long long)(target.y - vy) * 65535 + (vh - 1) / 2) / (vh - 1));
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE | MOUSEEVENTF_VIRTUALDESK;
        if (SendInput(1, &input, sizeof(input)) != 1)
            return SetCursorPos(target.x, target.y) != FALSE;
        return true;
    }
    bool Button(bool down)
    {
        if (mouseDown == down)
            return true;
        INPUT input{};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = down ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
        if (SendInput(1, &input, sizeof(input)) != 1)
            return false;
        mouseDown = down;
        if (down)
        {
            clickCount.fetch_add(1, std::memory_order_relaxed);
            lastClickMs.store(GetTickCount64(), std::memory_order_release);
        }
        // Diagnostic: a laser click while paused (the Save and Quit prompt), with the
        // pointer target, so the harness sees the prompt take the laser per title.
        if (down)
        {
            // Where the click landed, in MCC's client pixels (to compare with a capture of
            // the dialog's buttons), for every laser click (paused or a shell menu).
            POINT at{};
            RECT client{};
            const bool haveAt =
                window && GetCursorPos(&at) && ScreenToClient(window, &at) && GetClientRect(window, &client);
            LOG("pause pointer: laser click title=%u mode=%u target=%s at client (%ld,%ld) of %ldx%ld",
                unsigned(TitleAdapter_GetActiveTitle()), unsigned(TitleAdapter_GetRuntimeMode()),
                currentTarget.load(std::memory_order_relaxed), haveAt ? at.x : -1L, haveAt ? at.y : -1L,
                haveAt ? client.right - client.left : 0L, haveAt ? client.bottom - client.top : 0L);
        }
        return true;
    }
    void Fault()
    {
        deliveryStatus.store("Windows-input-failed");
        if (!faultLogged)
        {
            LOG("game menu pointer: Windows mouse delivery unavailable; native menu controls retained");
            faultLogged = true;
        }
    }
    void Cancel()
    {
        lastSet = POINT{-100000, -100000};
        delivered.store(false, std::memory_order_release);
        visual.store(0, std::memory_order_release);
        pointer.Reset();
        if (!Button(false))
            Fault();
        // Retry a failed release on the existing timer. Never forget a sent down.
        if (timer && !mouseDown)
        {
            KillTimer(nullptr, timer);
            timer = 0;
        }
    }
    void CALLBACK Watchdog(HWND, UINT, UINT_PTR, DWORD)
    {
        if (!Allowed() || !game_menu_pointer::FreshActive(sample.load(), GetTickCount()))
            Cancel();
    }
    // Records the process/title that owns the foreground while MCC's window does
    // not; the owner lookup is limited to once a second. Window thread only.
    void NoteForeground()
    {
        const HWND fg = GetForegroundWindow();
        if (fg == window)
            return;
        foregroundLost.fetch_add(1, std::memory_order_relaxed);
        static DWORD lastLookup = 0;
        const DWORD now = GetTickCount();
        if (lastLookup && DWORD(now - lastLookup) < 1000)
            return;
        lastLookup = now;
        DWORD pid = 0;
        GetWindowThreadProcessId(fg, &pid);
        char title[96] = "";
        if (fg)
            GetWindowTextA(fg, title, sizeof(title));
        char exe[MAX_PATH] = "";
        if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid))
        {
            DWORD n = sizeof(exe);
            QueryFullProcessImageNameA(h, 0, exe, &n);
            CloseHandle(h);
        }
        const char* base = strrchr(exe, '\\');
        base = base ? base + 1 : exe;
        snprintf(foregroundOwner, sizeof(foregroundOwner), "%s \"%s\"", fg ? base : "(none)", title);
    }
    void Apply()
    {
        ReclaimForeground();
        RECT client{};
        const bool allowed = Allowed() && GetClientRect(window, &client);
        const auto result = pointer.Update(sample.load(std::memory_order_acquire), GetTickCount(), allowed,
            client.right - client.left, client.bottom - client.top);
        if (!result.move)
        {
            if (!allowed && window && GetForegroundWindow() != window)
                NoteForeground();
            {
                const char* gate = allowed ? nullptr : GateName();
                deliveryStatus.store(allowed ? "inactive-ray" : gate ? gate : "focus-or-menu-gate");
            }
            Cancel();
            return;
        }
        // Do not take over a physical mouse drag. An injected down is tracked
        // separately and always balanced on miss, focus loss, F1 or stale input.
        if (!mouseDown && (GetAsyncKeyState(VK_LBUTTON) & 0x8000))
        {
            deliveryStatus.store("physical-drag");
            Cancel();
            return;
        }
        POINT target{result.x, result.y};
        if (!ClientToScreen(window, &target))
        {
            Cancel();
            Fault();
            return;
        }
        const HWND under = WindowFromPoint(target);
        if (under != window && !IsChild(window, under))
        {
            deliveryStatus.store("window-covered");
            Cancel();
            return;
        }
        if (!timer)
            timer = SetTimer(nullptr, 0, 50, Watchdog);
        if (!timer)
        {
            Cancel();
            Fault();
            return;
        }
        // Our DLL caller bypasses MCC's fitted-window coordinate remap. Send
        // physical client coordinates once; the existing MCC read hook scales them.
        POINT actual{};
        // Something other than the laser (MCC's controller-mode focus snap, another
        // app, the physical mouse) moved the cursor since the last delivery.
        if (lastSet.x != -100000)
        {
            POINT now{};
            if (GetCursorPos(&now) && (std::abs(now.x - lastSet.x) > 2 || std::abs(now.y - lastSet.y) > 2))
                cursorStolen.fetch_add(1, std::memory_order_relaxed);
        }
        lastSet = target;
        if (!MoveCursor(target) || !GetCursorPos(&actual) || std::abs(actual.x - target.x) > 2 ||
            std::abs(actual.y - target.y) > 2)
        {
            Cancel();
            Fault();
            return;
        }
        if (!Button(result.pressed))
        {
            Cancel();
            Fault();
        }
        else
        {
            moveCount.fetch_add(1, std::memory_order_relaxed);
            deliveryStatus.store("delivered");
            visual.store(game_menu_pointer::Pack(GetTickCount(), true, float(result.x) / (client.right - client.left),
                             float(result.y) / (client.bottom - client.top), mouseDown),
                std::memory_order_release);
            delivered.store(true, std::memory_order_release);
        }
    }
}

void NativeMenuPointer_Init(HWND hwnd)
{
    window = hwnd;
    messageId = RegisterWindowMessageW(L"HaloMCCVR.GameMenuPointer.20260916");
    if (!messageId)
        LOG("game menu pointer: message registration failed; native controls retained");
    focusMessageId = RegisterWindowMessageW(L"HaloMCCVR.InputFocus.20261009");
    keyMessageId = RegisterWindowMessageW(L"HaloMCCVR.DialogKey.20261012");
    if (keyMessageId == messageId || keyMessageId == focusMessageId)
        keyMessageId = 0;
    if (!focusMessageId || focusMessageId == messageId)
    {
        focusMessageId = 0;
        LOG("MCC input focus: message registration failed; focus re-send disabled");
    }
}
void NativeMenuPointer_Publish(bool active, float u, float v, bool pressed)
{
    lastRenderAdmit.store(
        active ? GetTickCount() : 0, std::memory_order_release); // an inactive publish revokes it at once
    const uint64_t next = game_menu_pointer::Pack(GetTickCount(), active, u, v, pressed);
    const uint64_t previous = sample.exchange(next, std::memory_order_acq_rel);
    // Default-off gameplay performs no Windows input or window messaging.
    if (!(next & (1ull << 31)) && !(previous & (1ull << 31)))
        return;
    if (messageId && window && !queued.exchange(true, std::memory_order_acq_rel))
    {
        if (!PostMessageW(window, messageId, 0, 0))
        {
            queued.store(false);
            delivered.store(false);
            postFailures.fetch_add(1, std::memory_order_relaxed);
        }
    }
}
void NativeMenuPointer_ResendInputFocus()
{
    // Any thread: one coalesced message; the window thread re-checks the gates.
    if (!focusMessageId || !window || focusQueued.exchange(true, std::memory_order_acq_rel))
        return;
    if (!PostMessageW(window, focusMessageId, 0, 0))
        focusQueued.store(false, std::memory_order_release);
}
unsigned NativeMenuPointer_FocusResent()
{
    return focusResent.load(std::memory_order_relaxed);
}
void NativeMenuPointer_NoteVrInput()
{
    lastVrInput.store(GetTickCount() | 1u, std::memory_order_release);
}
bool NativeMenuPointer_ConsumesTrigger()
{
    return delivered.load(std::memory_order_acquire) && Allowed() &&
           game_menu_pointer::FreshActive(sample.load(), GetTickCount());
}
void NativeMenuPointer_ReportFailures()
{
    if (cursorFailures.exchange(0))
        LOG("game menu cursor: frame submission rejected; optional ring disabled for this session, native input and core VR retained");
    const unsigned failures = postFailures.exchange(0, std::memory_order_relaxed);
    if (failures)
        LOG("game menu pointer: %u window-message delivery failures; native controls retained", failures);
    static DWORD lastReport = 0;
    static int lastEnabled = -1;
    const int enabled = g_config.game_menu_pointer ? 1 : 0;
    const DWORD now = GetTickCount();
    if (lastEnabled != enabled || (enabled && DWORD(now - lastReport) >= 2000))
    {
        lastReport = now;
        lastEnabled = enabled;
        LOG("game menu pointer: enabled=%d hand=%s mode=%u frame=%s delivery=%s active=%d moves=%u clicks=%u "
            "cursorMovedByOther=%u foregroundLost=%u focusReclaimed=%u focusDeferred=%u lastForeground=%s",
            enabled, g_config.left_handed ? "left" : "right", unsigned(TitleAdapter_GetRuntimeMode()),
            frameStatus.load(), deliveryStatus.load(), int(game_menu_pointer::FreshActive(visual.load(), now)),
            moveCount.load(), clickCount.load(), cursorStolen.load(), foregroundLost.load(), focusTakes.load(),
            focusDeferred.load(), foregroundOwner[0] ? foregroundOwner : "-");
    }
}
namespace
{
    void KeyTap(WORD vk)
    {
        INPUT in[2]{};
        in[0].type = in[1].type = INPUT_KEYBOARD;
        in[0].ki.wVk = in[1].ki.wVk = vk;
        in[0].ki.wScan = in[1].ki.wScan = static_cast<WORD>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        in[1].ki.dwFlags = KEYEVENTF_KEYUP;
        if (vk == VK_LEFT || vk == VK_RIGHT)
            in[0].ki.dwFlags = in[1].ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
        SendInput(2, in, sizeof(INPUT));
    }
}
void NativeMenuPointer_MenuKey(unsigned short vk, const char* why)
{
    if (!keyMessageId || !window)
        return;
    pendingKey.store(vk, std::memory_order_release); // one key per message; a newer press replaces an unsent one
    if (!PostMessageW(window, keyMessageId, 0, 0))
        return;
    LOG("menu keys: %s -> %s to MCC (%s)", why,
        vk == VK_RETURN   ? "Enter"
        : vk == VK_ESCAPE ? "Esc"
        : vk == VK_LEFT   ? "Left"
        : vk == VK_RIGHT  ? "Right"
        : vk == VK_UP     ? "Up"
        : vk == VK_DOWN   ? "Down"
                          : "key",
        "the game ended the pause without a resume press");
}
void NativeMenuPointer_Status(const char*& frame, const char*& delivery)
{
    frame = frameStatus.load(std::memory_order_relaxed);
    delivery = deliveryStatus.load(std::memory_order_relaxed);
}
unsigned long long NativeMenuPointer_LastClickMs()
{
    return lastClickMs.load(std::memory_order_acquire);
}
void NativeMenuPointer_DialogKeys(bool a, bool b, float stickX)
{
    static const char* lastTarget = "";
    static bool wasA = false, wasB = false;
    static int wasDir = 0;
    const HWND fg = GetForegroundWindow();
    const char* target = "none";
    if (!g_config.game_menu_pointer || !window)
        target = "off";
    else if (!GatesExceptFocus())
        target = "gated";
    else if (fg == window)
        target = "game-screen";
    else if (fg && ForegroundOwnedByGame(fg) && IsWindowVisible(fg))
        target = "mcc-dialog";
    else if (ForegroundIsOtherApplication(fg))
        target = "other-app";
    if (strcmp(target, lastTarget) != 0)
    {
        lastTarget = target;
        char title[96] = "";
        if (fg)
            GetWindowTextA(fg, title, sizeof(title));
        LOG("pause pointer: target=%s (foreground \"%s\")", target, title);
    }
    currentTarget.store(target, std::memory_order_relaxed);
    const bool dialog = strcmp(target, "mcc-dialog") == 0;
    const int dir = stickX <= -0.6f ? -1 : stickX >= 0.6f ? 1 : 0;
    if (dialog)
    {
        if (a && !wasA)
        {
            KeyTap(VK_RETURN);
            LOG("pause pointer: A -> Enter to the MCC dialog title=%u", unsigned(TitleAdapter_GetActiveTitle()));
        }
        if (b && !wasB)
            KeyTap(VK_ESCAPE);
        if (dir && dir != wasDir)
            KeyTap(dir < 0 ? VK_LEFT : VK_RIGHT);
    }
    wasA = a;
    wasB = b;
    wasDir = dir;
}
void NativeMenuPointer_FrameStatus(const char* status)
{
    frameStatus.store(status, std::memory_order_relaxed);
    if (strcmp(status, "no-native-screen") == 0)
        return; // the per-frame reset, not a result
    // Final per-frame status (set after DialogKeys): log pointer state changes once.
    static const char* lastFrame = "";
    static const char* lastDelivery = "";
    static const char* lastTarget = "";
    const char* delivery = deliveryStatus.load(std::memory_order_relaxed);
    const char* target = currentTarget.load(std::memory_order_relaxed);
    if (strcmp(status, lastFrame) != 0 || strcmp(delivery, lastDelivery) != 0 || strcmp(target, lastTarget) != 0)
    {
        lastFrame = status;
        lastDelivery = delivery;
        lastTarget = target;
        LOG("pause pointer: frame=%s delivery=%s target=%s", status, delivery, target);
    }
}
void NativeMenuPointer_CursorFailed()
{
    cursorFailures.fetch_add(1, std::memory_order_relaxed);
}
bool NativeMenuPointer_ReadVisual(float& u, float& v, bool& pressed)
{
    const auto packet = visual.load(std::memory_order_acquire);
    if (!delivered.load(std::memory_order_acquire) || !game_menu_pointer::FreshActive(packet, GetTickCount()))
        return false;
    u = float(packet & 32767) / 32767.0f;
    v = float((packet >> 15) & 32767) / 32767.0f;
    pressed = (packet & (1ull << 30)) != 0;
    return true;
}
bool NativeMenuPointer_Message(UINT msg)
{
    if (focusMessageId && msg == focusMessageId)
    {
        // MCC's input system missed its window's activation: deliver
        // WM_SETFOCUS through the normal window procedure chain so MCC's own
        // handler sets its flag. It does not change the desktop foreground.
        // Only while MCC's window is in front, or no window is (the headset
        // case): never while the player is in another application, or MCC
        // would read the keyboard/pad in the background.
        focusQueued.store(false, std::memory_order_release);
        const HWND fg = GetForegroundWindow();
        if (window && (fg == window || !fg) && VR_IsStereoEnabled() && IsWindowEnabled(window) && !IsIconic(window) &&
            !Menu_IsOpen())
        {
            SendMessageW(window, WM_SETFOCUS, 0, 0);
            focusResent.fetch_add(1, std::memory_order_relaxed);
        }
        return true;
    }
    if (messageId && msg == messageId)
    {
        queued.store(false, std::memory_order_release);
        Apply();
        return true;
    }
    if (keyMessageId && msg == keyMessageId)
    {
        // Keyboard input goes to the foreground window: bring MCC forward first
        // (the press is deliberate VR input), never from a hung window.
        NativeMenuPointer_NoteVrInput();
        const HWND fg = GetForegroundWindow();
        // Only when nothing or an MCC window is in front (the headset case): never
        // take the foreground from another application (Link, runtime desktop).
        if (window && fg != window && !ForegroundIsOtherApplication(fg) && IsWindowEnabled(window) && !IsIconic(window))
        {
            BringWindowToTop(window);
            if (!SetForegroundWindow(window) && fg && !IsHungAppWindow(fg))
            {
                const DWORD me = GetCurrentThreadId(), fgThread = GetWindowThreadProcessId(fg, nullptr);
                const bool attached = fgThread && fgThread != me && AttachThreadInput(me, fgThread, TRUE);
                SetForegroundWindow(window);
                if (attached)
                    AttachThreadInput(me, fgThread, FALSE);
            }
        }
        const HWND now = GetForegroundWindow();
        const WORD vk = pendingKey.exchange(0, std::memory_order_acq_rel);
        if (vk && (now == window || (now && ForegroundOwnedByGame(now))))
            KeyTap(vk);
        else if (vk)
            LOG("menu keys: MCC is not in the foreground; key dropped");
        return true;
    }
    if (msg == WM_KILLFOCUS || msg == WM_DESTROY)
    {
        sample.store(0, std::memory_order_release);
        Cancel();
    }
    return false;
}
