// Halo Reach title adapter: the adapter stage, the pinned retail evidence identity and the hook-proof checks.
// Used by src/dll/core/title_adapter.cpp and tests/core_tests.cpp.
#pragma once

#include <cstdint>

enum class ReachAdapterStage : uint8_t
{
    Disabled = 0,
    ControllerInputOnly,
};

struct ReachEvidenceIdentity
{
    const wchar_t* moduleName;
    const char* moduleSha256;
    uint32_t peTimestamp;
    uint32_t sizeOfImage;
    const char* hrekBuild;
};

struct ReachHookProof
{
    bool retailIdentity;
    uint32_t loadedImageMatchCount;
    bool executableRange;
    bool abi;
    bool callers;
    bool dataFlow;
    bool hrekSemantics;
    bool consumedLayoutFields;
};

ReachAdapterStage ReachAdapter_GetStage();
const ReachEvidenceIdentity& ReachAdapter_GetEvidenceIdentity();
bool ReachAdapter_HookProofComplete(const ReachHookProof& proof);
bool ReachAdapter_RuntimeHooksPermitted();
