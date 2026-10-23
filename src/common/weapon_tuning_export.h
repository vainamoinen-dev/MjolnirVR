#pragma once
// F1 Weapon tuning export: one paste-ready line per tuned weapon, appended to
// HaloMCCVR-weapon-tuning.txt beside the config, for folding into
// weapon_profiles.h (Profile::gripOffsetM / grabZone). Pure formatting.
// Used by src/dll/core/menu.cpp, which writes the file; tested by tests/weapon_profile_tests.cpp.
#include <cstdio>
#include <filesystem>
#include <string>

namespace weapon_tuning
{
    struct Row
    {
        const char* title = "";                           // GameTitle enumerator name, e.g. "Halo3"
        const char* weapon = "";                          // weapon_model catalog name
        float gripForward = 0, gripRight = 0, gripUp = 0; // gun_forward_m/right_m/up_m
        bool zoneTuned = false;
        float alongMin = 0, alongMax = 0, lateralMax = 0; // tuned grab zone
        float scale = 1, pitch = 0, yaw = 0, roll = 0;    // context only (not a profile field)
        // The scope lens placement in force for this weapon (aim-frame metres); exported
        // as a paste-ready weapon_profile::kScopeLensRows row.
        bool lensExport = false;
        float lensRight = 0, lensUp = 0, lensForward = 0, lensWidth = 0;
    };

    inline std::string Format(const Row& row)
    {
        char text[640];
        if (row.zoneTuned)
            std::snprintf(text, sizeof(text),
                "// %s %s: scale %.2f, rotation %.1f/%.1f/%.1f deg\n"
                "{GameTitle::%s, \"%s\"}: hasGripOffset=true, gripOffsetM={%.3ff, %.3ff, %.3ff}, "
                "hasGrabZone=true, grabZone={%.3ff, %.3ff, %.3ff}\n",
                row.title, row.weapon, row.scale, row.pitch, row.yaw, row.roll, row.title, row.weapon, row.gripForward,
                row.gripRight, row.gripUp, row.alongMin, row.alongMax, row.lateralMax);
        else
            std::snprintf(text, sizeof(text),
                "// %s %s: scale %.2f, rotation %.1f/%.1f/%.1f deg (grab zone not tuned)\n"
                "{GameTitle::%s, \"%s\"}: hasGripOffset=true, gripOffsetM={%.3ff, %.3ff, %.3ff}\n",
                row.title, row.weapon, row.scale, row.pitch, row.yaw, row.roll, row.title, row.weapon, row.gripForward,
                row.gripRight, row.gripUp);
        std::string result = text;
        if (row.lensExport)
        {
            char lens[256];
            std::snprintf(lens, sizeof(lens),
                "{GameTitle::%s, \"%s\", %.3ff, %.3ff, %.3ff, %.3ff}, // scope lens row for kScopeLensRows (right, up, forward, width)\n",
                row.title, row.weapon, row.lensRight, row.lensUp, row.lensForward, row.lensWidth);
            result += lens;
        }
        return result;
    }

    // The export file beside the config. Empty (refuse) when no config is loaded
    // or it has no parent folder, so nothing lands in the game's working folder.
    inline std::filesystem::path ExportPath(const std::wstring& configPath)
    {
        if (configPath.empty())
            return {};
        const std::filesystem::path folder = std::filesystem::path(configPath).parent_path();
        if (folder.empty())
            return {};
        return folder / L"HaloMCCVR-weapon-tuning.txt";
    }

    inline const char* TitleEnumName(int title) noexcept
    {
        // GameTitle: None, Halo3, Halo3ODST, HaloReach, Halo4, HaloCE, Halo2.
        static constexpr const char* names[]{"None", "Halo3", "Halo3ODST", "HaloReach", "Halo4", "HaloCE", "Halo2"};
        return title >= 0 && title < 7 ? names[title] : "Unknown";
    }
} // namespace weapon_tuning
