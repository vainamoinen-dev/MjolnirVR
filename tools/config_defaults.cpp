// Tiny build helper: writes the current source tree's default config to a path that does not exist yet.
// tools/package-candidate.ps1 runs it (halomccvr-config-defaults.exe) to stage and hash the shipped default config.
// Exit codes: 0 written, 2 bad arguments, 3 the path already exists, 4 no file appeared.
// It never overwrites an existing file, and CMake does not install it.
#include <windows.h>

#include "../src/common/config.h"

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2 || !argv[1] || !argv[1][0])
        return 2;
    if (GetFileAttributesW(argv[1]) != INVALID_FILE_ATTRIBUTES)
        return 3;

    ConfigLoad(argv[1]);
    const DWORD attributes = GetFileAttributesW(argv[1]);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY) ? 0 : 4;
}
