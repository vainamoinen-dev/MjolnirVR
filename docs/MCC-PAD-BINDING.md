# Why the virtual pad works in MCC menus but not in gameplay

Target: `MCC-Win64-Shipping.exe` (67,179,480 bytes, dated 2026-10-07). All addresses are RVAs (offsets
from the image base 0x140000000). Confidence: **[V]** = read directly in the disassembly, **[I]** =
inferred from code I read, **[U]** = a guess. This was a read-only investigation: nothing was launched
and nothing in the game folder was changed.

## Short answer

MCC has **two separate gamepad readers** that both call the same `XInputGetState`:

1. **Unreal's built-in XInput reader** (`FXInputInterface`, poll at +0xEF32D0, the ~62 Hz call you saw).
   It feeds the Unreal UI layer (Slate/UMG). That's why **menus** respond to the pad.
2. **MCC's own input system**, an older engine layer that is not Unreal. Its global object is at
   `[0x4001B78]`. Its main class vtable (the table of pointers to a class's functions) is at
   0x346C168. It only polls pads that it has already turned into **device objects**. **Gameplay** input
   for the Halo DLLs comes only from this system.

MCC's system **creates a pad device in only two cases**:
- **Once at startup**, at +0x3C1990..0x3C1A05 [V]. It calls `XInputGetState(0..3)`, and every user index
  that answers ERROR_SUCCESS gets a device object (0xDB0 bytes, built by constructor 0x87D0CC).
- **After any `WM_DEVICECHANGE` window message**. The handler at 0x87A178 has a `case 0x219` that sets
  the rescan flag `byte [0x4001B4B] = 1` [V]. The next input update then re-probes the unused XInput
  indices (0x87DC55..0x87DCD4) [V].

Your hook almost certainly went live **after** the startup scan, and no `WM_DEVICECHANGE` has arrived
since. No real device was plugged in, so Windows had no reason to send one. As a result MCC's system
has only the keyboard/mouse device and **no pad device**, so gameplay never sees the pad [I, strong].
With the real ViGEm-style pad, the pad was present at startup and the startup scan found it.

There is direct evidence for this. Once a pad device exists, MCC polls it from 0x87D2B1 (return address
**+0x87D2B6**) [V]. Your log shows only +0xEF32D5, the Unreal reader. That matches "no MCC pad device".

**No new hook is needed for the simplest fix:** once the XInput hook is installed, post
`WM_DEVICECHANGE` to MCC's game window (details under Q4).

## Q1. How XInput indices become "gamepad ids" and get tied to a user

- **No HID or RawInput enumeration in the pad path [V/I].** The only things that decide whether a pad
  exists are an XInput index (0..3) and an `XInputGetState` result of 0.
  - `GetRawInputDeviceList` at 0xEF1023 belongs to Unreal's mouse-counting code [I].
  - `SetupDi*` at 0x259832 lists audio devices only. The GUIDs at 0x3286560/0x3286570 are the
    audio-capture and audio-render device classes [V].
  - `SetupDiGetClassDevsExW` + `CM_Get_Device_IDW` at 0xEE9CFD is Unreal's monitor lookup [I].
  - No "IG_" device-path string is referenced in input code. The hits are in OpenSSL text and
    localisation keys [V].
  - `RegisterDeviceNotificationW` at 0x1F8ABAF registers for **all** device-interface classes (flag 4,
    zero GUID) [V]. Its only purpose is to make Windows send `WM_DEVICECHANGE`.
- **Device slots.** MCC's input system has 5 device slots at `inputSys+8 + slot*8`.
  - Base device constructor 0x87C9DC stores the **slot number at device+0x0C** and the **XInput index at
    device+0x808** [V/I]. A value below 0 there means "not an XInput device".
  - Gamepad devices set `byte device+0x800 = 1` [V]. The keyboard/mouse device is the other kind and is
    tested through `device+0x801` [I].
  - The keyboard/mouse device (0x9B0 bytes, vtable 0x346C418) is always created, at 0x3C1A2D [V].
- **"gamepadId" in the user debug dump is that slot number** [I]. The dump at 0x38D4A0..0x38DA24 prints
  `gamepadId` from `[[userSlot+0x28]]+0x0C` [V]. The user manager lives at `[0x4001578]`.
- **"controller status" values** (0x38D7B4) [V]:
  - 0 = "Empty"
  - 1 = "ControllerDisconnected"
  - 2 = "EstablishActiveUser"
  - anything else = "Unknown {%d}"
- **Linking a device to a user.**
  - `0x87E020(inputSys, playerIdx)` returns the gamepad for a player [V].
    - For player 0 it first tries the **active device**, `devices[[0x4001B84]]`, if that device is a
      gamepad.
    - Otherwise it walks the 5 slots and asks the user manager (0x38BF14) which user owns each slot.
  - The active device changes automatically when a device produces input:
    - 0x8CD31C runs a short countdown, then calls `SetActiveDevice` at 0x8CEF8C [V].
    - That function tells the user manager (0x38BCA8 / 0x2DE1EC) and posts an event
      (id 0xBACDA16A) [V].
    - It then writes `[0x4001B84] = slot` at 0x8CF0C4. This is the only writer [V].
- **A pure XInput slot with no device object gets no id at all.** It never even reaches "disconnected".

## Q2. What decides which device feeds gameplay for the primary user

The chain is [V]:

1. halo1.dll calls the host interface (the object MCC hands each game DLL) through vtable slot 36
   (+0x120): `GetInput(short controllerIdx, buf[0x130])`. See halo1 +0xAD8C48.
2. MCC's host object is at 0x3F7B190 (vtable 0x327F3C0). Slot 36 is **0x1E9B4C**, which:
   - clears the buffer;
   - sets `buf[0] = 1` only when controllerIdx == 0 **and** the player-0 preference == 1;
   - then calls `inputSys->vtbl[1]` = **0x87D540**.
3. 0x87D540 (player 0):
   - runs the update/rescan at 0x87DB58;
   - picks `rbx = devices[activeIdx]`;
   - prefers the player's paired gamepad if it had input this frame (`device+0x802`);
   - otherwise uses keyboard/mouse if it has activity;
   - fills the buffer with `device->vtbl[0x38]`. For a pad, 0x87D39C copies `XINPUT_GAMEPAD` into
     `buf+0x120..0x12D`.
4. Other players use only their paired pad (0x87D862).

The preference value is not the deciding factor here:
- **Preference getter = 0x1E151C** (per-player object vtable 0x327DD60, slot +0x228) [V]. Player 0
  returns the byte at **0x3CB11AC**. Every other player returns 2.
- That byte is saved as JSON key **"PrimaryUserPreferredInputDevice"** (serializer 0x39479F) [V].
- Value meanings, from how 0x87D69B..0x87D6C4 tests them [I]:
  - **1 = mouse & keyboard**
  - **2 = gamepad**
  - **0 = no preference / automatic**
- The preference only matters in two places:
  - the "KBM mode" `buf[0]` flag;
  - a device filter. The filter is active only when `0x881364` returns true, which needs an online game
    (`[0x4000B9F]`), feature check 0x1F, and the game-variant flag 0x400. That looks like matchmaking
    restricted by input device, so **"AllowedInputDevices" is matchmaking-only** [I].
  - In campaign or custom games the filter is off [I].
- So **`PreferredInputDevice=0` in GameUserSettings.ini is not the cause** [I]. I did not trace how
  that Unreal setting maps onto byte 0x3CB11AC [U].
- In gameplay, a pad works as soon as a **pad device exists**. Its first button press makes it the
  active device.

## Q3. Title screen ("press start")

I did not trace the title code itself, so this part is **[I]**:
- The Unreal layer shows the title screen. The user binding ("EstablishActiveUser") is keyed on MCC
  input-system device slots, the same gamepadIds as in Q1.
- Keyboard Enter works because the keyboard/mouse device always exists.
- The pad was ignored at the title because it had no device slot.
- After Enter, the Unreal reader drove the menus, so the pad seemed to work there.
- With the old ViGEm pad, a slot existed from startup, so "press start" on the pad worked too.

## Q4. Ways to make the virtual pad count, ranked from lowest risk

1. **Post `WM_DEVICECHANGE` after the hook is live (no new hook, does not depend on game version).**
   - Call `PostMessageW(mccHwnd, 0x219 /*WM_DEVICECHANGE*/, 7 /*DBT_DEVNODES_CHANGED*/, 0)`.
     `mccHwnd` is the top-level game window (Unreal's "UnrealWindow" class) in this process.
   - What happens:
     1. Unreal passes the message to its message handlers.
     2. MCC's handler at 0xBB4658 calls 0x87A178, which sets `[0x4001B4B]=1` [V].
     3. On the next MCC input update (0x87DB58), every unused XInput index is probed and the first free
        slot gets a pad device [V].
     4. Unreal's own reader also re-checks its pads (harmless).
   - Conditions [V]:
     - The update only rescans while `byte [0x3CAF8D8] != 0`. It is set by WM_SETFOCUS (0x87A3C1) and
       cleared when the window is deactivated (0x87A389). If the window isn't focused, the flag simply
       waits.
     - Only **one** device is created per rescan, and one of the 5 slots must be free.
   - Send it once, after the hook is installed and the window exists. Sending it again is harmless: an
     index that is already in use is skipped.
2. **Install the XInput hook before MCC's input system is built (also no new hook).**
   - The startup scan happens when that object is constructed (+0x3C1990). If the mod DLL loads at
     process start (for example as a proxy DLL) and hooks XInput in its startup code, the scan finds the
     pad.
   - How to check: your hook log should show return address **+0x3C19A8** with status 0.
3. **Write `1` to the byte at exe+0x4001B4B directly (no hook, but tied to this exact build).**
   - Same effect as option 1, with no window lookup.
   - It is a bare data address, so it must be re-found after every MCC patch (the instruction at
     0x87DC55 makes a good byte pattern). Medium risk only because of patches.
4. **Not recommended:**
   - calling device constructor 0x87D0CC yourself, or writing into `inputSys+8` slots or
     `[0x4001B84]`;
   - patching the host's GetInput at 0x1E9B4C.

   These skip MCC's own bookkeeping: user-manager pairing, events and locks (MCC guards the input
   system with a lock at 0x3FA2840). Higher risk of crashes or desyncs.

After options 1, 2 or 3:
- Press any pad button once (in a menu or in game). That makes the pad the active device through
  0x8CD31C → 0x8CEF8C.
- **Expected result:** the hook log shows extra `XInputGetState` calls returning to **+0x87D2B6**
  (MCC's per-device poll), alongside the existing +0xEF32D5.
- If the per-device poll appears but gameplay still ignores the pad, look next at the player-0 pairing
  path (0x87E020 / user manager 0x38BF14).

## Correction (2026-10-09 QA, pn2 runs): the real gate is window focus

- The startup scan (+0x3C19A8) DID create the pad device in QA runs: the slot answered 0, so a device
  object exists with `byte device+8 = 1`.
- MCC's input update 0x87DB58 returns immediately unless `byte [0x3CAF8D8] != 0` (`cmp; je 0x87DCDB`).
  That skips every device poll (0x87D26C, return +0x87D2B6) **and** the WM_DEVICECHANGE rescan.
- The window handler 0x87A178 sets the byte on `WM_ACTIVATE` (active) and `WM_SETFOCUS`, and clears it on
  `WM_ACTIVATE` (inactive) and `WM_KILLFOCUS` [V].
- What actually happened in QA (diag5): the flag went 1 -> 0 exactly when the game window replaced the
  splash window. The game window's activation set it, then the splash's late deactivation cleared it.
  The mod keeps a running title "active" (it rewrites deactivation messages), so the game window never
  re-activates, and even a real mouse click did not set it again. The menus keep working because they use
  Unreal's separate reader. This is a race, which is why some runs worked.
- A WM_DEVICECHANGE rescan nudge and a "bring MCC to the front" keeper were both tried and did nothing:
  the rescan runs inside the same gated update, and MCC was already in front.

## What the mod does (fix/mcc-input-focus)

- At first use it proves the flag twice: the update's test (`cmp [rip+d], r12b`) and the window handler's
  setter (`mov byte [rip+d], 1`) must each be unique and resolve to the same byte. Otherwise there are no
  reads (stock behaviour), and this is logged.
- While the flag reads 0 and the game window exists, the window thread sends `WM_SETFOCUS` to the game
  window through the normal window procedure chain, so MCC's own handler sets the flag. This does not
  change the desktop foreground. Only in VR, and only while MCC's window or no window at all (the headset
  case) is in front: a deliberate switch to another application clears the flag legitimately, and
  re-arming it there would let MCC read the keyboard/pad in the background. Never while the window is
  disabled (a modal dialog), minimized or behind the F1 menu. Checked by the worker and again at delivery.
  At most 3 tries per inactive stretch, 2 s apart.
- QA (mf3 runs): one re-send was enough. MCC's gameplay reader (+0x87D2B6) then polled the pad, Reach
  weapon swaps and holsters worked (reserve class learned), and the ODST pod accepted A.
- Logs: `MCC input focus: ...`, `MCC pad readers: N active (...)`.
- Config `mcc_input_focus` (default 1). No F1 control: an internal compatibility fix with nothing
  player-facing to tune (rule 3 exemption).
- Not fixed, by decision: the title screen still needs one keyboard press or laser click when no real
  gamepad is attached. The laser covers it.

## Key addresses

| RVA | What | Conf. |
|---|---|---|
| 0x1F7FBC0 / 0x1F7FBC6 | XInputGetState / XInputSetState call stubs (IAT 0x286C160/168) | V |
| 0xEF325C (poll 0xEF32D0) | Unreal FXInputInterface::SendControllerEvents (menus) | V/I |
| 0x3C1990..0x3C1A05 | MCC input system: startup scan of XInput 0..3 | V |
| 0x87A178, case 0x219 | MCC window handler: WM_DEVICECHANGE sets rescan byte 0x4001B4B | V |
| 0x87DB58 (0x87DC55..0x87DCD4) | input update + rescan (needs byte 0x3CAF8D8 set) | V |
| 0x87D26C (call 0x87D2B1) | per-device XInput poll for MCC pad devices | V |
| 0x87D540 | inputSys GetInput (picks device, fills game buffer) | V |
| 0x87E020 | gamepad paired with a player (active device / user manager) | V |
| 0x8CD31C → 0x8CEF8C | auto-switch of the active device; writes [0x4001B84] | V |
| 0x1E9B4C | host vtable slot 36: GetInput called by Halo DLLs | V |
| 0x1E151C / byte 0x3CB11AC | PrimaryUserPreferredInputDevice getter / value (1=KBM, 2=pad) | V / I |
| 0x881364 | input-device filter, active only in matchmaking (AllowedInputDevices) | I |
| 0x38D4A0 | user/pairing debug dump ("gamepadId" = device slot) | V/I |
