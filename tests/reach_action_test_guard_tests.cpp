// Golden: Reach action-test builder guard (reach_action_test_guard_logic.h).
// The fixture is the retail haloreach.dll prologue at +0x657DC and the caller
// edge at +0x61D43, copied byte for byte.
#include "../src/common/titles/reach/reach_action_test_guard_logic.h"
#include <cstdio>
#include <vector>

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
    using namespace reach_action_test_guard;
    // clang-format off
    const std::vector<uint8_t> prologue = {
        0x48, 0x8b, 0xc4, 0x48, 0x89, 0x58, 0x08, 0x48, 0x89, 0x68, 0x10, 0x48, 0x89, 0x70, 0x18, 0x48,
        0x89, 0x78, 0x20, 0x41, 0x56, 0x48, 0x83, 0xec, 0x20, 0x65, 0x48, 0x8b, 0x04, 0x25, 0x58, 0x00,
        0x00, 0x00, 0x41, 0xbe, 0x01, 0x00, 0x00, 0x00, 0x44, 0x8b, 0x0d, 0x0d, 0x23, 0xbb, 0x00, 0x49,
        0x8b, 0xd8, 0x48, 0x63, 0xf1, 0x48, 0x8b, 0xfa, 0xb9, 0xd8, 0x00, 0x00, 0x00, 0x4c, 0x8b, 0xde,
        0x49, 0xc1, 0xe3, 0x05, 0x4e, 0x8b, 0x14, 0xc8, 0x4d, 0x8b, 0x14, 0x0a};
    // clang-format on
    Check(prologue.size() == kPrologueSize, "fixture covers the whole proven prologue");
    Check(DecodeTlsIndexRva(prologue.data(), kBuilderRva) == 0xC17B18,
        "TLS index decodes to +0xC17B18 (the index every Reach binding uses)");
    auto changed = prologue;
    changed[57] = 0xE0; // mov ecx, 0xE0: different TLS member
    Check(DecodeTlsIndexRva(changed.data(), kBuilderRva) == 0, "different TLS member: not proven");
    changed = prologue;
    changed[0] = 0xE9; // already detoured / patched
    Check(DecodeTlsIndexRva(changed.data(), kBuilderRva) == 0, "patched entry: not proven");
    // e8 at +0x61D43 -> +0x657DC (rel32 = 0x3A94).
    const uint8_t call[] = {0xE8, 0x94, 0x3A, 0x00, 0x00};
    Check(CallEdgeMatches(call, kCallerCallRva, kBuilderRva), "caller edge lands on the builder");
    const uint8_t wrong[] = {0xE8, 0x95, 0x3A, 0x00, 0x00};
    Check(!CallEdgeMatches(wrong, kCallerCallRva, kBuilderRva), "off-by-one edge refused");

    int calls = 0;
    int member = 1;
    Check(!CallIfStateExists([] { return static_cast<void*>(nullptr); }, [&] { ++calls; }) && calls == 0,
        "no action-test state: the builder is not called (the +0x65BA9 crash)");
    Check(CallIfStateExists([&] { return static_cast<void*>(&member); }, [&] { ++calls; }) && calls == 1,
        "state present: the builder runs exactly once, unchanged");
    if (g_failures)
    {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("reach_action_test_guard_tests: all passed\n");
    return 0;
}
