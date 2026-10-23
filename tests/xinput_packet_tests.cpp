// PacketNormalizer: the virtual pad's packet number advances only on a real
// state change, so an idle pad never reads as controller activity to MCC.
#include "../src/common/xinput_packet.h"
#include <cstdio>

static int g_failures = 0;
static void Check(bool ok, const char* what)
{
    if (!ok)
    {
        ++g_failures;
        std::printf("FAIL: %s\n", what);
    }
}

int main()
{
    PacketNormalizer n;
    XINPUT_GAMEPAD idle{};
    const DWORD first = n.Next(idle);
    Check(first != 0, "first report gets a packet number");
    bool steady = true;
    for (int i = 0; i < 1000; ++i)
        steady = steady && n.Next(idle) == first;
    Check(steady, "an unchanged idle pad keeps the same packet number across polls");

    XINPUT_GAMEPAD a = idle;
    a.wButtons = XINPUT_GAMEPAD_A;
    const DWORD pressed = n.Next(a);
    Check(pressed != first, "a button press advances the packet number");
    Check(n.Next(a) == pressed, "holding the button does not advance it again");
    const DWORD released = n.Next(idle);
    Check(released != pressed, "releasing advances it");

    XINPUT_GAMEPAD stick = idle;
    stick.sThumbLX = 1;
    Check(n.Next(stick) != released, "any axis change counts, however small");

    if (g_failures == 0)
        std::printf("xinput packet tests passed\n");
    return g_failures == 0 ? 0 : 1;
}
