#pragma once
// XInput packet numbering for the merged virtual gamepad MCC reads.
// Used by src/dll/core/input.cpp; tested by tests/xinput_packet_tests.cpp.
// Uses XInput types only: no Windows calls, no globals.
//
// XInput packet numbering for the pad the mod hands to MCC.
//
// A real controller's dwPacketNumber changes only when its state changes. MCC
// treats a changed packet number as controller activity and switches its menus
// to controller mode, which ignores the mouse - and the VR game-menu laser
// drives MCC through the mouse. Bumping the number on every poll (the old
// behaviour) therefore kept flipping MCC back to controller mode while the
// player was pointing, even with every button and axis at zero.
//
// PacketNormalizer gives the final, merged pad a number that advances exactly
// when the reported gamepad differs from the previous report.
#include <windows.h>
#include <xinput.h>
#include <cstring>

class PacketNormalizer
{
public:
    DWORD Next(const XINPUT_GAMEPAD& pad)
    {
        if (!primed_ || std::memcmp(&pad, &last_, sizeof(pad)) != 0)
        {
            last_ = pad;
            primed_ = true;
            ++packet_;
        }
        return packet_;
    }

private:
    XINPUT_GAMEPAD last_{};
    DWORD packet_ = 0;
    bool primed_ = false;
};
