// Interface to the VR laser pointer on MCC's native menus (native_menu_pointer.cpp).
// vr.cpp publishes ray samples, input.cpp reports VR input, menu.cpp asks for the trigger state.
#pragma once
#include <windows.h>

void NativeMenuPointer_Init(HWND window);
// Render thread publishes only a bounded atomic sample and a coalesced message.
void NativeMenuPointer_Publish(bool active, float u = 0, float v = 0, bool pressed = false);
bool NativeMenuPointer_ConsumesTrigger();
// Input thread: deliberate VR controller input just happened (a press, a
// pulled trigger/grip, a pushed stick). Lets the menu laser take MCC back from
// another application the player had switched to.
void NativeMenuPointer_NoteVrInput();
// Worker: MCC's own input system is inactive (it missed its window's
// activation). The window thread sends WM_SETFOCUS to
// the game window so MCC's handler sees it; only in VR while MCC's window or no
// window is in front (never another application), and never while the window
// is disabled (a modal dialog), minimized or behind the F1 menu. Delivery count.
void NativeMenuPointer_ResendInputFocus();
unsigned NativeMenuPointer_FocusResent();
void NativeMenuPointer_ReportFailures();                // existing diagnostics worker only
void NativeMenuPointer_FrameStatus(const char* status); // static literal, atomic only
bool NativeMenuPointer_ReadVisual(float& u, float& v, bool& pressed);
void NativeMenuPointer_CursorFailed(); // atomic report; retires optional layer only
// Menu mode only, any thread, once per frame. When an MCC-owned dialog (the "Save and Quit" confirmation)
// holds the foreground, the laser cannot reach it (it is not on the game screen), so the VR
// controls act as keys on it: A = Enter, B = Esc, stick/dpad left/right = Left/Right. Logs
// "pause pointer: target=<name>" whenever the target changes.
void NativeMenuPointer_DialogKeys(bool a, bool b, float stickX);
// Any thread: one key tap (Enter/Esc/arrows) to MCC's window, delivered on the window
// thread after bringing MCC to the foreground (deliberate VR input). For the
// Save & Quit dialog that the title's pause flag no longer covers.
void NativeMenuPointer_MenuKey(unsigned short vk, const char* why);
// Diagnostic: the last per-frame pointer status and delivery result (static literals).
void NativeMenuPointer_Status(const char*& frame, const char*& delivery);
// GetTickCount64 of the last laser button-down (0 = none), for "Resume" picked with the laser.
unsigned long long NativeMenuPointer_LastClickMs();
// Called before ImGui/native processing, on the window thread.
bool NativeMenuPointer_Message(UINT message);
