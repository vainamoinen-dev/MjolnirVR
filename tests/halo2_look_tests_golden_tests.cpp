// Golden tests for the Halo 2 look-test feed (src/dll/titles/h2/halo2_look_tests.cpp, included here).
// Built as halomccvr_halo2_look_tests_golden_tests with src/dll/core/sigscan.cpp and run by ctest.
// MinHook and the native reader/builder are in-process fakes; no game process.
//
// Pins the CURRENT Halo 2 look-test
// feed before its feed/axis/report logic moves into
// shared head_look_test helpers. A scripted sequence of reader polls, head
// poses and builder inputs is driven through the real ReaderHook/BuilderHook;
// the trace of what the builder saw, what was restored, and the report line
// must stay byte-identical.
#include <windows.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <MinHook.h>
#include "hook_checked_forwarders.h" // DLL hook sites call MinHook through the checked names
#include "../src/dll/core/sigscan.h"

uint64_t g_now = 10000;
uint64_t MockNow()
{
    return g_now;
}
#define GetTickCount64 MockNow

#include "../src/dll/titles/h2/halo2_look_tests.cpp"

#undef GetTickCount64

// ---- seams the look-test file calls ----
std::string g_logs;
void Logf(const char* format, ...)
{
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    g_logs += buffer;
    g_logs += '\n';
}
float g_headQ[4]{0, 0, 0, 1};
bool g_paused = false;
bool VR_GetHeadPose(float outQuat[4], float outPos[3])
{
    for (int i = 0; i < 4; ++i)
        outQuat[i] = g_headQ[i];
    for (int i = 0; i < 3; ++i)
        outPos[i] = 0;
    return true;
}
bool VR_IsPausePresentation()
{
    return g_paused;
}
MH_STATUS WINAPI MH_CreateHook(LPVOID, LPVOID, LPVOID*)
{
    return MH_OK;
}
MH_STATUS WINAPI MH_EnableHook(LPVOID)
{
    return MH_OK;
}
MH_STATUS WINAPI MH_RemoveHook(LPVOID)
{
    return MH_OK;
}
MH_STATUS WINAPI MCCVR_DisableHookForRetirement(LPVOID)
{
    return MH_OK;
}
bool WaitForNativeDetourQuiescence(const void* const*, const void* const*, size_t, const std::atomic<uint32_t>&)
{
    return true;
}

namespace
{
    std::string g_trace;
    float g_seenYaw = 0, g_seenPitch = 0;
    bool g_seenNull = false;
    void __fastcall FakeBuilder(void*, unsigned char* input)
    {
        if (!input)
        {
            g_seenNull = true;
            return;
        }
        std::memcpy(&g_seenYaw, input + 0x10, 4);
        std::memcpy(&g_seenPitch, input + 0x14, 4);
    }
    bool __fastcall FakeReader()
    {
        return false;
    }
    uint32_t g_gameWord = 0;
    unsigned char g_globalsBlock[16]{};
    unsigned char* g_globalsPointer = g_globalsBlock;

    void Head(float yawRightDeg, float pitchUpDeg)
    {
        // OpenXR: yaw right is a negative rotation about +Y; pitch up positive about +X.
        const float yaw = -yawRightDeg / 57.2957795f, pitch = pitchUpDeg / 57.2957795f;
        const float cy = std::cos(yaw / 2), sy = std::sin(yaw / 2), cp = std::cos(pitch / 2), sp = std::sin(pitch / 2);
        // q = yaw(Y) * pitch(X)
        g_headQ[0] = cy * sp;
        g_headQ[1] = sy * cp;
        g_headQ[2] = -sy * sp;
        g_headQ[3] = cy * cp;
    }
    template <int Poll>
    void PollReader()
    {
        (void)ReaderHook<Poll>();
    }
    void Frame(const char* name, unsigned polls, float inputYaw = 0, float inputPitch = 0, bool nullInput = false)
    {
        g_now += 16;
        if (polls & 1)
            PollReader<0>();
        if (polls & 2)
            PollReader<1>();
        if (polls & 4)
            PollReader<2>();
        if (polls & 8)
            PollReader<3>();
        if (polls & 16)
            PollReader<4>();
        std::memcpy(g_globalsBlock + 4, &g_gameWord, 4);
        alignas(16) unsigned char input[0x40]{};
        std::memcpy(input + 0x10, &inputYaw, 4);
        std::memcpy(input + 0x14, &inputPitch, 4);
        g_seenYaw = g_seenPitch = 99;
        g_seenNull = false;
        BuilderHook(nullptr, nullInput ? nullptr : input);
        float afterYaw = 0, afterPitch = 0;
        std::memcpy(&afterYaw, input + 0x10, 4);
        std::memcpy(&afterPitch, input + 0x14, 4);
        char line[160];
        snprintf(line, sizeof(line), "%s seen=%.4f,%.4f after=%.4f,%.4f null=%d\n", name, g_seenYaw, g_seenPitch,
            afterYaw, afterPitch, g_seenNull ? 1 : 0);
        g_trace += line;
    }
}

int main()
{
    g_installed = true;
    g_enabled = true;
    g_builder.original = reinterpret_cast<void*>(&FakeBuilder);
    for (auto& reader : g_readers)
        reader.original = reinterpret_cast<void*>(&FakeReader);
    g_globals = reinterpret_cast<uintptr_t>(&g_globalsPointer);

    Head(0, 0);
    Frame("idle-no-poll", 0);
    Frame("all-arms", 16);
    Head(10, 0);
    Frame("all-right", 16);
    Head(10, 10);
    Frame("all-up", 16);
    Head(10, 0);
    g_gameWord = 0x400;
    Frame("updown-exclusive", 1 | 2);
    g_gameWord = 0;
    Head(-5, 0);
    Frame("left-real-yaw-masked", 4, 0.5f, 0);
    Head(-15, 0);
    Frame("left-idle", 4);
    g_enabled = false;
    Head(5, 0);
    Frame("disabled", 16);
    g_enabled = true;
    g_paused = true;
    Head(20, 0);
    Frame("paused", 16);
    g_paused = false;
    Head(30, 0);
    Frame("null-input", 16, 0, 0, true);
    g_now += 3000;
    Head(40, 0);
    Frame("poll-expired", 0);
    Report();
    g_trace += "LOG " + g_logs;

    // Recorded from the code before the look-test feed moved into shared helpers.
    const char* expected = "idle-no-poll seen=0.0000,0.0000 after=0.0000,0.0000 null=0\n"
                           "all-arms seen=0.0000,0.0000 after=0.0000,0.0000 null=0\n"
                           "all-right seen=-0.0873,0.0000 after=0.0000,0.0000 null=0\n"
                           "all-up seen=0.0000,0.0873 after=0.0000,0.0000 null=0\n"
                           "updown-exclusive seen=0.0000,-0.0873 after=0.0000,0.0000 null=0\n"
                           "left-real-yaw-masked seen=0.5000,0.0000 after=0.5000,0.0000 null=0\n"
                           "left-idle seen=0.0873,0.0000 after=0.0000,0.0000 null=0\n"
                           "disabled seen=0.0000,0.0000 after=0.0000,0.0000 null=0\n"
                           "paused seen=0.0000,0.0000 after=0.0000,0.0000 null=0\n"
                           "null-input seen=99.0000,99.0000 after=0.0000,0.0000 null=1\n"
                           "poll-expired seen=0.0000,0.0000 after=0.0000,0.0000 null=0\n"
                           "LOG Halo 2 look tests: head look fed UDLR on 4 builder tick(s)\n";
    if (g_trace != expected)
    {
        std::printf("TRACE MISMATCH\n--- got\n%s--- expected\n%s", g_trace.c_str(), expected);
        return 1;
    }
    std::printf("Halo 2 look tests golden: trace identical (%zu bytes)\n", g_trace.size());
    return 0;
}
