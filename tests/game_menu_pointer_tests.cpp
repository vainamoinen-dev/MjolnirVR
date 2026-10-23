// Tests for the in-game menu laser pointer, the pause world lock and the Menu-resume rule
// (src/dll/core/native_menu_pointer.cpp, game_menu_pointer.h, pause_world_lock.h).
// Built as halomccvr_game_menu_pointer_tests with src/common/config.cpp and run by ctest.
// Executes the shipping window-thread transport against fake Windows endpoints.
// No real cursor movement, input injection, timers, windows or game access.
#include <windows.h>
#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>
#include "config.h"
#include "pause_world_lock.h"
#include "game_menu_pointer.h"
#include "exclusive_input.h"
#include "game_menu_pointer_xr.h"

static HWND fakeWindow = reinterpret_cast<HWND>(uintptr_t{1});
static bool focused = true, minimized = false, f1 = false, pauseScreen = false;
static bool physicalDown = false, sendOk = true, postOk = true, moveOk = true;
static bool timerOk = true, covered = false, stereo = true, foreignForeground = false;
static DWORD fixtureNow = 1000;
static POINT cursor{};
static int fixtureWidth = 1000, fixtureHeight = 500, posts = 0, moves = 0, logs = 0;
static RuntimeMode fixtureMode = RuntimeMode::Shell;
static std::vector<DWORD> buttons;
static HWND otherApp = reinterpret_cast<HWND>(uintptr_t{2}), ownedDialog = reinterpret_cast<HWND>(uintptr_t{3});
static bool dialogForeground = false, setForegroundOk = false, hung = false;
static int setForegrounds = 0, attaches = 0;
static HWND FakeForeground()
{
    return focused ? fakeWindow : dialogForeground ? ownedDialog : foreignForeground ? otherApp : nullptr;
}
static BOOL FakeIconic(HWND)
{
    return minimized;
}
static DWORD FakeThreadPid(HWND h, DWORD* pid)
{
    if (pid)
        *pid = h == otherApp ? 4242u : GetCurrentProcessId();
    return 7;
}
static HWND FakeGetWindow(HWND h, UINT)
{
    return h == ownedDialog ? fakeWindow : nullptr;
}
static BOOL FakeHung(HWND)
{
    return hung;
}
static BOOL FakeAttach(DWORD, DWORD, BOOL attach)
{
    if (attach)
        ++attaches;
    return TRUE;
}
static BOOL FakeBringToTop(HWND)
{
    return TRUE;
}
static BOOL FakeSetForeground(HWND)
{
    ++setForegrounds;
    if (setForegroundOk)
    {
        focused = true;
        foreignForeground = dialogForeground = false;
    }
    return setForegroundOk;
}
static bool gameEnabled = true;
static int setFocusSends = 0;
static BOOL FakeEnabled(HWND)
{
    return gameEnabled;
}
static LRESULT FakeSend(HWND, UINT msg, WPARAM, LPARAM)
{
    if (msg == WM_SETFOCUS)
        ++setFocusSends;
    return 0;
}
static BOOL FakeRect(HWND, RECT* r)
{
    *r = {0, 0, fixtureWidth, fixtureHeight};
    return TRUE;
}
static BOOL FakeClient(HWND, POINT* p)
{
    p->x += 100;
    p->y += 50;
    return TRUE;
}
static HWND FakeUnder(POINT)
{
    return covered ? nullptr : fakeWindow;
}
static BOOL FakeChild(HWND, HWND)
{
    return FALSE;
}
static SHORT FakeKey(int)
{
    return physicalDown ? SHORT(-0x8000) : 0;
}
static BOOL FakeMove(int x, int y)
{
    ++moves;
    cursor = {x, y};
    return moveOk;
}
static BOOL FakeCursor(POINT* p)
{
    *p = cursor;
    return TRUE;
}
static int absoluteMoves = 0;
static UINT FakeSend(UINT count, INPUT* input, int)
{
    if (!sendOk)
        return 0;
    for (UINT i = 0; i < count; ++i)
    {
        if (input[i].mi.dwFlags & MOUSEEVENTF_MOVE)
        { // virtual desktop 0,0 3840x2160
            if (!moveOk)
                return 0;
            ++absoluteMoves;
            ++moves;
            cursor = {LONG((input[i].mi.dx * 3839LL + 32767) / 65535), LONG((input[i].mi.dy * 2159LL + 32767) / 65535)};
            continue;
        }
        buttons.push_back(input[i].mi.dwFlags);
    }
    return count;
}
static int FakeMetrics(int index)
{
    return index == SM_CXVIRTUALSCREEN ? 3840 : index == SM_CYVIRTUALSCREEN ? 2160 : 0;
}
static UINT_PTR FakeTimer(HWND, UINT_PTR, UINT, TIMERPROC)
{
    return timerOk ? 42 : 0;
}
static BOOL FakeKill(HWND, UINT_PTR)
{
    return TRUE;
}
static UINT FakeRegister(const wchar_t*)
{
    static UINT next = 0xC123;
    return next++;
} // pointer 0xC123, focus 0xC124
static BOOL FakePost(HWND, UINT, WPARAM, LPARAM)
{
    ++posts;
    return postOk;
}
static DWORD FakeTick()
{
    return fixtureNow;
}
#define GetForegroundWindow FakeForeground
#define GetWindowThreadProcessId FakeThreadPid
#define GetWindow FakeGetWindow
#define IsHungAppWindow FakeHung
#define SetForegroundWindow FakeSetForeground
#define AttachThreadInput FakeAttach
#define BringWindowToTop FakeBringToTop
#define IsIconic FakeIconic
#define GetClientRect FakeRect
#define ClientToScreen FakeClient
#define WindowFromPoint FakeUnder
#define IsChild FakeChild
#define GetAsyncKeyState FakeKey
#define SetCursorPos FakeMove
#define GetCursorPos FakeCursor
#define SendInput FakeSend
#define GetSystemMetrics FakeMetrics
#define SetTimer FakeTimer
#define KillTimer FakeKill
#define RegisterWindowMessageW FakeRegister
#define PostMessageW FakePost
#define GetTickCount FakeTick
#define IsWindowEnabled FakeEnabled
#define SendMessageW FakeSend
#include "../src/dll/core/native_menu_pointer.cpp"

void Logf(const char*, ...)
{
    ++logs;
}
const wchar_t* LogDirectory()
{
    return L"";
}
bool Menu_IsOpen()
{
    return f1;
}
RuntimeMode TitleAdapter_GetRuntimeMode()
{
    return fixtureMode;
}
GameTitle TitleAdapter_GetActiveTitle()
{
    return GameTitle::HaloReach;
}
bool VR_IsPausePresentation()
{
    return pauseScreen;
}
bool VR_IsPausePresentationTarget()
{
    return pauseScreen;
}
bool VR_IsCutsceneTheaterActive()
{
    return false;
}
bool VR_IsStereoEnabled()
{
    return stereo;
}

static int checks = 0, failures = 0;
static void Check(bool ok, const char* name)
{
    ++checks;
    if (!ok)
    {
        ++failures;
        std::printf("FAIL: %s\n", name);
    }
}
static void Frame(bool active, bool trigger = false, float u = .5f, float v = .5f)
{
    ++fixtureNow;
    NativeMenuPointer_Publish(active, u, v, trigger);
    NativeMenuPointer_Message(0xC123);
}
int main()
{
    using namespace game_menu_pointer;
    for (int m = 0; m <= int(RuntimeMode::Unsupported); ++m)
        Check(MenuMode(RuntimeMode(m), false) ==
                  (m == int(RuntimeMode::Shell) || m == int(RuntimeMode::Paused) || m == int(RuntimeMode::Unsupported)),
            "preloaded shell and pause admit pointing; live gameplay/loading do not");
    // The Menu button resumes an in-game pause while the laser owns it.
    // The laser owns input when MenuMode(runtime mode, in-game pause screen) holds.
    {
        // ODST after its pause-boundary teardown: the runtime mode reads Loading,
        // the in-game pause is still showing, and the ODST camera context is gone.
        // (Seen on ODST: the second press logged mode=1 pause view=1/1 exclusive=1.)
        const bool localPointer = MenuMode(RuntimeMode::Loading, true);
        Check(localPointer && MenuResumesPause(localPointer, true, true),
            "z70: after ODST's pause teardown a Menu press resumes (the camera context plays no part)");
        Check(MenuResumesPause(MenuMode(RuntimeMode::Paused, true), true, true),
            "z70: a Menu press on any title's in-game pause menu resumes");
        Check(!MenuResumesPause(MenuMode(RuntimeMode::Paused, true), true, false),
            "z70: holding Menu does not repeat (one press, one Start); the press that paused is not a new press");
        const bool shell = MenuMode(RuntimeMode::Shell, false);
        Check(shell && !MenuResumesPause(shell, false, true),
            "z70: MCC's shell menus (laser on, no in-game pause) get no Start");
        const bool gameplay = MenuMode(RuntimeMode::Gameplay, false);
        Check(!gameplay && !MenuResumesPause(gameplay, false, true),
            "z70: in gameplay the laser does not own input and the normal Menu path is untouched");
        // An earlier version ran the decision on the pad after the exclusive filter,
        // which clears Menu while the laser owns input. It must use the raw edge.
        struct
        {
            bool a, b, x, y, clickL, clickR, menu;
            float moveX, moveY, turnX, turnY, trigL, trigR, gripL, gripR;
            uint32_t weaponButtons;
        } filtered{};
        filtered.menu = true;
        ExclusiveInputHolds holds;
        holds.ApplyVr(filtered, localPointer, true);
        Check(!filtered.menu && MenuResumesPause(localPointer, true, true),
            "z70: the exclusive filter clears Menu, so the resume uses the raw edge from before it");
    }
    XrCompositionLayerQuad quad{XR_TYPE_COMPOSITION_LAYER_QUAD};
    quad.pose = {{0, 0, 0, 1}, {0, 0, -2.4f}};
    quad.size = {4, 4 * 2100.0f / 2912};
    XrPosef head{{0, 0, 0, 1}, {0, 0, 0}}, aim = head;
    float hitU = 0, hitV = 0;
    Check(
        RayHit(quad, aim, head, false, hitU, hitV) && std::fabs(hitU - .5f) < .0001f && std::fabs(hitV - .5f) < .0001f,
        "main-menu ray hits actual 2912x2100 stock quad center");
    for (bool view : {false, true})
        for (float yaw : {0.0f, 2.474877f})
            for (float u : {.1f, .5f, .9f})
                for (float v : {.1f, .5f, .9f})
                {
                    const XrQuaternionf rotation{0, std::sin(yaw / 2), 0, std::cos(yaw / 2)};
                    const XrVector3f anchor{-3, -.2f, -1.3f};
                    head = {rotation, anchor};
                    aim.orientation = rotation;
                    const auto hand = RotatePoint(rotation, {(u - .5f) * 4, (.5f - v) * quad.size.height, 0});
                    aim.position = {anchor.x + hand.x, anchor.y + hand.y, anchor.z + hand.z};
                    quad.pose = {{0, 0, 0, 1}, {0, 0, -2.4f}};
                    if (!view)
                    {
                        quad.pose.orientation = rotation;
                        const auto center = RotatePoint(rotation, {0, 0, -2.4f});
                        quad.pose.position = {anchor.x + center.x, anchor.y + center.y, anchor.z + center.z};
                    }
                    Check(RayHit(quad, aim, head, view, hitU, hitV) && std::fabs(hitU - u) < .0001f &&
                              std::fabs(hitV - v) < .0001f,
                        "rotated/recentered local shell and view-space pause map full menu");
                    const auto cursorQuad = CursorQuad(quad, XR_NULL_HANDLE, u, v, false);
                    auto cursorLocal = InversePoint(quad.pose, cursorQuad.pose.position);
                    Check(std::fabs(cursorLocal.x - (u - .5f) * 4) < .0001f &&
                              std::fabs(cursorLocal.y - (.5f - v) * quad.size.height) < .0001f,
                        "visible cursor overlays exact delivered hover coordinates");
                    Check(cursorQuad.space == quad.space && cursorQuad.subImage.imageRect.extent.width == 32 &&
                              cursorQuad.size.width > CursorQuad(quad, XR_NULL_HANDLE, u, v, true).size.width,
                        "cursor keeps screen space and contracts on click");
                }
    quad.pose = {{0, 0, 0, 1}, {0, 0, -2.4f}};
    head = {{0, 0, 0, 1}, {0, 0, 0}};
    aim = head;
    aim.orientation = {0, 1, 0, 0};
    Check(!RayHit(quad, aim, head, false, hitU, hitV), "backward ray rejects");
    aim = head;
    aim.position.x = 3;
    Check(!RayHit(quad, aim, head, false, hitU, hitV), "off-menu ray rejects");
    aim = head;
    aim.position.z = -3;
    Check(!RayHit(quad, aim, head, false, hitU, hitV), "behind-menu ray rejects");
    aim = head;
    aim.orientation.w = 0;
    Check(!RayHit(quad, aim, head, false, hitU, hitV), "invalid pose rejects");
    aim = head;
    quad.size.width = std::numeric_limits<float>::quiet_NaN();
    Check(!RayHit(quad, aim, head, false, hitU, hitV), "nonfinite quad rejects");
    Check(CursorPixel(0, 0) == 0 && CursorPixel(16, 16) == 0 && CursorPixel(16, 5) == 0xFFFFFFFFu &&
              CursorPixel(16, 1) == 0xFF101010u,
        "cursor has transparent surround/center and contrasting ring");
    Pointer p;
    Check(!FreshActive(Pack(fixtureNow, true, std::numeric_limits<float>::quiet_NaN(), .5f, false), fixtureNow),
        "NaN rejects");
    Check(!FreshActive(Pack(fixtureNow, true, 1.01f, .5f, false), fixtureNow), "outside rejects");
    Check(FreshActive(Pack(0xfffffff0, true, .5f, .5f, false), 10), "tick wrap stays fresh");
    Check(!FreshActive(Pack(fixtureNow, true, .5f, .5f, false), fixtureNow + 201), "stale rejects");
    auto edge = p.Update(Pack(fixtureNow, true, 1, 1, false), fixtureNow, true, 1000, 500);
    Check(edge.x == 999 && edge.y == 499, "last pixel clamps inside client");
    Check(!p.Update(Pack(fixtureNow, true, .5f, .5f, true), fixtureNow, true, 0, 500).move, "zero client rejects");

    NativeMenuPointer_Init(fakeWindow);
    Check(Config{}.game_menu_pointer, "new configs point at game menus by default");
    g_config.game_menu_pointer = false;
    Frame(true);
    Check(moves == 0 && buttons.empty(), "disabled pointer never moves or clicks");
    g_config.game_menu_pointer = true;
    Frame(true, true);
    Check(buttons.empty(), "held trigger at activation never clicks");
    Frame(true);
    Check(cursor.x >= 599 && cursor.x <= 600 && cursor.y >= 299 && cursor.y <= 300,
        "UV maps physical client plus screen origin");
    Check(absoluteMoves > 0, "default delivery is a real absolute mouse move (SendInput)");
    {
        int abs0 = absoluteMoves, set0 = moves - absoluteMoves;
        g_config.menu_pointer_setcursorpos = true;
        Frame(true, false, .25f, .5f);
        Check(absoluteMoves == abs0 && moves - absoluteMoves > set0, "legacy SetCursorPos delivery on request");
        g_config.menu_pointer_setcursorpos = false;
    }
    Frame(true, true);
    Check(mouseDown && buttons.back() == MOUSEEVENTF_LEFTDOWN, "trigger presses native mouse");
    Frame(true, true);
    Check(buttons.size() == 1, "held trigger does not repeat presses");
    Frame(false, true);
    Check(!mouseDown && buttons.back() == MOUSEEVENTF_LEFTUP, "miss releases");
    Frame(true, true);
    Check(!mouseDown, "reenter while held requires release");
    Frame(true);
    Frame(true, true);
    focused = false;
    NativeMenuPointer_Message(WM_KILLFOCUS);
    Check(!mouseDown, "focus loss releases");
    const int beforeMoves = moves;
    Frame(true, true);
    Check(moves == beforeMoves, "background cannot move pointer");
    focused = true;
    Frame(true);
    Frame(true, true);
    float visualU = 0, visualV = 0;
    bool visualPressed = false;
    Check(NativeMenuPointer_ReadVisual(visualU, visualV, visualPressed) && visualPressed,
        "visible cursor receives successfully delivered mouse press");
    f1 = true;
    Watchdog(nullptr, 0, 0, 0);
    Check(!mouseDown, "F1 releases");
    f1 = false;
    Check(!NativeMenuPointer_ReadVisual(visualU, visualV, visualPressed), "F1 cancellation hides native cursor");
    Frame(true);
    Frame(true, true);
    fixtureNow += 201;
    Watchdog(nullptr, 0, 0, 0);
    Check(!mouseDown && !timer, "stalled render watchdog releases and stops timer");
    Frame(true);
    Frame(true, true);
    fixtureMode = RuntimeMode::Gameplay;
    Watchdog(nullptr, 0, 0, 0);
    Check(!mouseDown, "gameplay transition releases");
    Frame(true);
    Check(!NativeMenuPointer_ConsumesTrigger(), "gameplay never consumes trigger");
    fixtureMode = RuntimeMode::Paused;
    Frame(true);
    Check(NativeMenuPointer_ConsumesTrigger(), "pause hover consumes VR trigger only");
    fixtureMode = RuntimeMode::Unsupported;
    Frame(true);
    Frame(true, true);
    Check(mouseDown && NativeMenuPointer_ConsumesTrigger(), "reported six-module shell now delivers trigger click");
    Frame(true);
    physicalDown = true;
    Frame(true, true);
    Check(!mouseDown, "physical drag remains native");
    physicalDown = false;
    covered = true;
    Frame(true);
    Check(!mouseDown, "covered window cannot click");
    covered = false;
    Frame(true);
    sendOk = false;
    Frame(true, true);
    Check(!mouseDown && logs > 0, "send failure stays optional and logs");
    sendOk = true;
    Frame(true);
    Frame(true, true);
    sendOk = false;
    Frame(false);
    Check(mouseDown && timer, "failed release retains retry ownership");
    sendOk = true;
    Watchdog(nullptr, 0, 0, 0);
    Check(!mouseDown, "failed release retries");
    timerOk = false;
    Frame(true);
    Check(!mouseDown, "no watchdog means no press");
    timerOk = true;
    Frame(true);
    moveOk = false;
    Frame(true, true);
    Check(!mouseDown, "failed positioning cannot click wrong location");
    moveOk = true;
    Frame(false);
    const int oldPosts = posts;
    NativeMenuPointer_Publish(true, .2f, .3f, false);
    NativeMenuPointer_Publish(true, .8f, .7f, false);
    Check(posts == oldPosts + 1, "render messages coalesce");
    NativeMenuPointer_Message(0xC123);
    Frame(false);
    postOk = false;
    NativeMenuPointer_Publish(true, .5f, .5f, false);
    Check(!queued.load(), "failed post permits next frame retry");
    postOk = true;
    NativeMenuPointer_Publish(true, .5f, .5f, false);
    NativeMenuPointer_Message(0xC123);
    Check(!queued.load(), "retry drains normally");
    Frame(true, true);
    g_config.game_menu_pointer = false;
    Watchdog(nullptr, 0, 0, 0);
    Check(!mouseDown, "toggle off releases");

    // MCC input focus re-send: WM_SETFOCUS through the window procedure chain,
    // coalesced, never into a disabled (modal dialog), minimized or F1 state.
    int before = posts;
    setFocusSends = 0;
    NativeMenuPointer_ResendInputFocus();
    NativeMenuPointer_ResendInputFocus();
    Check(posts == before + 1, "re-send requests coalesce into one message");
    NativeMenuPointer_Message(0xC124);
    Check(setFocusSends == 1 && NativeMenuPointer_FocusResent() == 1, "window thread sends WM_SETFOCUS once");
    NativeMenuPointer_Message(0xC124);
    Check(setFocusSends == 2, "each delivered message sends once (worker bounds the attempts)");
    gameEnabled = false;
    NativeMenuPointer_ResendInputFocus();
    NativeMenuPointer_Message(0xC124);
    Check(setFocusSends == 2, "never while MCC's window is disabled (modal dialog up)");
    gameEnabled = true;
    minimized = true;
    NativeMenuPointer_ResendInputFocus();
    NativeMenuPointer_Message(0xC124);
    Check(setFocusSends == 2, "never while minimized");
    minimized = false;
    f1 = true;
    NativeMenuPointer_ResendInputFocus();
    NativeMenuPointer_Message(0xC124);
    Check(setFocusSends == 2, "never with the F1 menu open");
    f1 = false;
    focused = false;
    NativeMenuPointer_ResendInputFocus();
    NativeMenuPointer_Message(0xC124);
    Check(setFocusSends == 3, "works with no window in front (headset): focus message only, no foreground change");
    foreignForeground = true;
    NativeMenuPointer_ResendInputFocus();
    NativeMenuPointer_Message(0xC124);
    Check(setFocusSends == 3, "never while another application is in front (no background input)");
    foreignForeground = false;
    stereo = false;
    NativeMenuPointer_ResendInputFocus();
    NativeMenuPointer_Message(0xC124);
    Check(setFocusSends == 3, "VR only");
    stereo = true;
    focused = true;
    NativeMenuPointer_ResendInputFocus();
    NativeMenuPointer_Message(0xC124);
    Check(setFocusSends == 4, "MCC in front: sent");
    // Reclaim etiquette: the laser on a menu takes MCC back only from no window or
    // MCC's own windows; another application only on fresh VR input; never from
    // an MCC-owned dialog. SetForegroundWindow alone first; attach only on failure.
    {
        g_config.game_menu_pointer = true;
        fixtureMode = RuntimeMode::Shell;
        f1 = false;
        physicalDown = false;
        auto attempt = [&](bool vrInput = false)
        {
            fixtureNow += 1500;
            if (vrInput)
                NativeMenuPointer_NoteVrInput();
            const int before = setForegrounds;
            Frame(true);
            return setForegrounds > before;
        };
        focused = false;
        foreignForeground = true;
        setForegroundOk = true;
        Check(!attempt() && !focused, "another application in front, no VR input: left alone");
        Check(attempt(true) && focused, "fresh VR input: MCC taken back from the other application");
        focused = false;
        foreignForeground = false;
        Check(attempt() && focused, "no window in front: taken back freely");
        focused = false;
        dialogForeground = true;
        Check(!attempt(), "never from a dialog owned by MCC's window");
        dialogForeground = false;
        focused = false;
        foreignForeground = true;
        setForegroundOk = false;
        attaches = 0;
        attempt(true);
        Check(attaches == 1, "attach only after a plain SetForegroundWindow failed");
        hung = true;
        attaches = 0;
        attempt(true);
        Check(attaches == 0, "never attach to a hung window");
        hung = false;
        setForegroundOk = true;
        attaches = 0;
        attempt(true);
        Check(attaches == 0 && focused, "no attach when SetForegroundWindow works alone");
        foreignForeground = false;
    }
    {
        // Pause screen world lock: placed where the head looked at pause start; looking around never moves it.
        pause_world_lock::Anchor a;
        pause_world_lock::Pose pInner, q;
        const float fwd0[3]{0, 0, -1}, pos[3]{0.1f, 1.6f, 0.2f}, turned[3]{-0.5f, 0, -0.866f},
            pos2[3]{0.5f, 1.5f, 0.0f};
        Check(!a.Update(false, true, true, fwd0, pos, 1.5f, pInner), "not paused: head-locked quad kept");
        Check(!a.Update(true, false, true, fwd0, pos, 1.5f, pInner), "switch off: head-locked quad kept");
        Check(!a.Update(true, true, false, fwd0, pos, 1.5f, pInner), "no head pose yet: keep the head-locked quad");
        Check(a.Update(true, true, true, fwd0, pos, 1.5f, pInner) && std::fabs(pInner.position.x - 0.1f) < 1e-4f &&
                  std::fabs(pInner.position.y - 1.6f) < 1e-4f && std::fabs(pInner.position.z - (0.2f - 1.5f)) < 1e-4f,
            "placed 1.5 m ahead of the head at eye height");
        Check(a.Update(true, true, true, turned, pos2, 1.5f, q) &&
                  std::fabs(q.position.x - pInner.position.x) < 1e-5f &&
                  std::fabs(q.position.z - pInner.position.z) < 1e-5f &&
                  std::fabs(q.orientation.y - pInner.orientation.y) < 1e-6f,
            "turning or moving the head does not move the screen");
        Check(
            a.Update(true, true, true, turned, pos2, 1.5f, q, 5) && std::fabs(q.position.x - pInner.position.x) > 0.3f,
            "a recenter while paused takes a fresh anchor");
        a.Update(false, true, true, fwd0, pos, 1.5f, q);
        Check(a.Update(true, true, true, turned, pos2, 1.5f, q) && std::fabs(q.position.x - pInner.position.x) > 0.3f,
            "a new pause takes a fresh anchor");
    }
    {
        // A pause blip (Reach load end) must not re-anchor where the head looks now.
        const float fwd0[3]{0, 0, -1}, turned[3]{-1, 0, 0}, pos[3]{0.1f, 1.6f, 0.2f};
        pause_world_lock::Anchor a;
        pause_world_lock::Pose pInner, q;
        Check(a.Update(true, true, true, fwd0, pos, 1.5f, pInner, 0, 1000) &&
                  a.LastMode() == pause_world_lock::Mode::Anchored,
            "anchored mode");
        a.Update(false, true, true, fwd0, pos, 1.5f, q, 0, 1100);
        Check(a.LastMode() == pause_world_lock::Mode::NotPaused, "not-paused mode while unpaused");
        Check(a.Update(true, true, true, turned, pos, 1.5f, q, 0, 1200) &&
                  std::fabs(q.position.z - pInner.position.z) < 1e-5f,
            "a 100 ms unpause keeps the anchor (no re-anchor at the new gaze)");
        a.Update(false, true, true, fwd0, pos, 1.5f, q, 0, 2000);
        a.Update(false, true, true, fwd0, pos, 1.5f, q, 0, 3100);
        Check(a.Update(true, true, true, turned, pos, 1.5f, q, 0, 3200) &&
                  std::fabs(q.position.x - pInner.position.x) > 0.3f,
            "an unpause over 1 s takes a fresh anchor");
        // Expiry must not need an unpaused call (stereo resumes, nothing calls Update).
        pause_world_lock::Anchor c;
        pause_world_lock::Pose r;
        c.Update(true, true, true, fwd0, pos, 1.5f, r, 0, 1000);
        Check(
            c.Update(true, true, true, turned, pos, 1.5f, q, 0, 6000) && std::fabs(q.position.x - r.position.x) > 0.3f,
            "pause, no calls for 5 s, pause again: fresh anchor");
        pause_world_lock::Anchor b;
        Check(!b.Update(true, true, false, fwd0, pos, 1.5f, q, 0, 10) &&
                  b.LastMode() == pause_world_lock::Mode::NoHeadPose,
            "no head pose: following-head mode is reported");
        Check(!b.Update(true, false, true, fwd0, pos, 1.5f, q, 0, 20) &&
                  b.LastMode() == pause_world_lock::Mode::SwitchOff,
            "switch off: following-head mode is reported");
    }
    {
        using namespace game_menu_pointer;
        Check(PauseEndedByGame(10000, 0), "pause ended with no resume press ever: the game ended it");
        Check(!PauseEndedByGame(10000, 9000), "Menu/Start or B 1 s before: the player resumed");
        Check(PauseEndedByGame(10000, 8000), "a press 2 s before does not count as the resume");
        Check(PauseEndedByGame(10000, 10500), "a press after the end does not count");
        Check(!PauseEndedByGame(0, 0), "no pause end: nothing");
        Check(DialogKeysWindow(25000, 10000) && !DialogKeysWindow(30001, 10000) && !DialogKeysWindow(5000, 0),
            "dialog keys for 20 s after the game ended the pause");
        Check(StickDir(-0.7f) == -1 && StickDir(0.7f) == 1 && StickDir(0.5f) == 0, "stick directions past 0.6");
        Check(!PauseEndedByGame(10000, 0, 9500) && PauseEndedByGame(10000, 0, 9000),
            "a laser click 0.5 s before the end resumed; 1 s before it did not (it opened the dialog)");
        Check(GameplayInput(0.8f, 0) && GameplayInput(0, -0.7f) && !GameplayInput(0.2f, 0.3f),
            "a trigger pull or walking ends the dialog keys");
    }
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
