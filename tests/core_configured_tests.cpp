// core_configured_tests.cpp: everything that runs on the loaded config (config load, migration and the tests that read it).
// Units 102, 103, 104, 105, 106, 107, 109, 110, 111, 112, 113, 114, 118, 119, 120, 122, 123, 124, 125, 126, 127, 128, 129, 130, 131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146, 147, 148, 149, 150, 151, 152, 153, 154, 155, 156, 157, 158, 159, 160, 161, 162, 163, 164, 165, 166, 168, 169, 170, 171, 172, 173, 174, 175, 176, 177, 178, 179, 180, 181, 182, 183, 184, 185, 186, 187, 188, 189, 190, 191, 192, 194, 197, 198, 199, 200, 201, 202, 203, 204, 205, 206, 207, 208, 209, 210, 211, 212, 213, 214, 215, 216, 217, 218, 219, 220, 221, 222, 223, 224 of the former tests/core_tests.cpp main(), moved unchanged and in
// their order (E3). Built as halomccvr_core_configured_tests and run by ctest; each unit prints its check count.
#include "core_test_support.h"

int main()
{
    CheckUnit(102);
    wchar_t tempPath[MAX_PATH]{};
    GetTempPathW(MAX_PATH, tempPath);
    const std::filesystem::path configDir =
        std::filesystem::path(tempPath) / (L"halomccvr-config-tests-" + std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(configDir);
    const std::filesystem::path primary = configDir / L"halomccvr.cfg";
    const std::filesystem::path legacy = configDir / L"halo3xr.cfg"; // legacy name: the migration under test
    CheckUnit(103);
    {
        std::ofstream file(legacy);
        file << "screen_width_m = 6.25\n";
        file << "haptic_intensity = malformed\n";
    }
    CheckUnit(104);
    ConfigLoadMigrating(primary.c_str(), legacy.c_str());
    Check(std::filesystem::exists(primary), "Legacy config migration creates halomccvr.cfg");
    Check(std::filesystem::exists(legacy), "Legacy config migration retains halo3xr.cfg");
    Check(g_config.screen_width_m == 6.25f, "Legacy values survive migration");
    Check(!g_config.hud_reveal_near_head && g_config.hud_reveal_radius_m == .22f,
        "HUD reveal is opt-in and cannot change existing hide behavior during upgrade");
    g_config.hud_reveal_near_head = true;
    g_config.hud_reveal_radius_m = .31f;
    ConfigSave();
    ConfigLoad(primary.c_str());
    Check(g_config.hud_reveal_near_head && std::fabs(g_config.hud_reveal_radius_m - .31f) < .0001f,
        "HUD reveal choice and activation distance survive a real config roundtrip");
    g_config.hud_reveal_near_head = false;
    g_config.hud_reveal_radius_m = .22f;
    ConfigSave();
    g_config.dlss_preset = 6;
    ConfigSave();
    ConfigLoad(primary.c_str());
    Check(g_config.dlss_preset == 6, "DLSS E survives real config roundtrip instead of silently changing to M");
    g_config.dlss_preset = 0;
    ConfigSave();
    Check(g_config.haptic_intensity == 0.86f, "Malformed new values retain their individual default");
    Check(g_config.dpad_head_radius == 0.30f && !g_config.quest_thumbrest_dpad,
        "Legacy configs retain the existing D-pad gesture radius and keep the new thumb-rest mode off");
    const std::string organizedConfig = ReadTextFile(primary);
    Check(!g_config.independent_dual_aim && CountText(organizedConfig, "\nindependent_dual_aim = 0") == 1,
        "independent dual aim defaults off in legacy configurations");
    Check(g_config.two_hand_coherent_aim && CountText(organizedConfig, "\ntwo_hand_coherent_aim = 1") == 1,
        "Coherent committed-sample aim defaults on and is persisted on for legacy configurations");
    g_config.two_hand_coherent_aim = false;
    ConfigSave();
    g_config.two_hand_coherent_aim = true;
    ConfigLoad(primary.c_str());
    Check(!g_config.two_hand_coherent_aim,
        "Hidden coherent-aim key preserves OFF through a config round trip as a rollback/debug escape");
    CheckUnit(105);
    {
        // Floor test: a real user's v12 config backup loads to exactly the values
        // the loader produced before pre-v12 migration was dropped. The golden was captured by
        // running that older ConfigLoad + ConfigSave on the same file (the v12 base golden in
        // tests/data); only the key the newer loader saves may differ, plus the v24 keys below,
        // each with its exact value, key by key.
        const auto data = std::filesystem::path(__FILE__).parent_path() / "data";
        const auto copy = configDir / L"z51-v12-floor.cfg";
        // The log lines below are checked, so the log goes to a file from here.
        // Outside configDir: the log stays open, and configDir is removed later.
        const auto logPath = std::filesystem::temp_directory_path() / L"halomccvr-core-tests.log";
        LogInit(logPath.c_str());
        std::filesystem::copy_file(data / "z51-v12-input.cfg", copy, std::filesystem::copy_options::overwrite_existing);
        std::filesystem::remove(configDir / L"z51-v12-floor.cfg.pre12");
        ConfigLoad(copy.c_str());
        ConfigSave();
        const auto values = [](const std::string& text)
        {
            std::map<std::string, std::string> out;
            size_t at = 0;
            while (at < text.size())
            {
                size_t end = text.find('\n', at);
                if (end == std::string::npos)
                    end = text.size();
                std::string line = text.substr(at, end - at);
                at = end + 1;
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                const size_t eq = line.find(" = ");
                if (line.empty() || line[0] == '#' || eq == std::string::npos)
                    continue;
                out[line.substr(0, eq)] = line.substr(eq + 3);
            }
            return out;
        };
        const auto golden = values(ReadTextFile(data / "z51-v12-base-golden.cfg"));
        const auto now = values(ReadTextFile(copy));
        // v24: Reach gets FXAA at 1.30; every other game's new copy is
        // the shared 4 / 1.00; the shared keys are unchanged.
        // v25: the stated window (v16's default: 0.133 x 1.5 at 0.40 m,
        // centre 0.032, -0.020) becomes four edges at size 1; the old keys go.
        const visor_zoom::MigratedWindow window = visor_zoom::EdgesFromOldWindow(0.133f, 1.5f, 0.032f, -0.02f);
        const auto five = [](float v)
        {
            char text[32];
            std::snprintf(text, sizeof text, "%.5f", static_cast<double>(v));
            return std::string(text);
        };
        Check(std::fabs(window.edges.left + 0.06775f) < 1e-6f && std::fabs(window.edges.right - 0.13175f) < 1e-6f &&
                  std::fabs(window.edges.top - 0.0548125f) < 1e-6f &&
                  std::fabs(window.edges.bottom + 0.0948125f) < 1e-6f,
            "F1 v25: the owner's window as edges is F1-PLAN's table (-0.06775, 0.13175, 0.05481, -0.09481)");
        std::map<std::string, std::string> v24{{"config_version", "25"}, {"visor_zoom_scale", "1.00"},
            {"visor_zoom_left_m", five(window.edges.left)}, {"visor_zoom_right_m", five(window.edges.right)},
            {"visor_zoom_top_m", five(window.edges.top)}, {"visor_zoom_bottom_m", five(window.edges.bottom)},
            {"visor_zoom_anchor_x_m", five(window.anchorX)}, {"visor_zoom_anchor_y_m", five(window.anchorY)}};
        const std::vector<std::string> removed{"visor_zoom_width_m", "visor_zoom_offset_x_m", "visor_zoom_offset_y_m"};
        for (const char* prefix : {"halo3_", "odst_", "reach_", "halo4_", "halo2a_", "halo2c_", "halo1_"})
        {
            const bool reach = !std::strcmp(prefix, "reach_");
            v24[std::string(prefix) + "aa_mode"] = reach ? "1" : "4";
            v24[std::string(prefix) + "resolution_scale"] = reach ? "1.3000" : "1.0000";
            v24[std::string(prefix) + "visor_zoom_left_m"] = five(window.edges.left);
            v24[std::string(prefix) + "visor_zoom_right_m"] = five(window.edges.right);
            v24[std::string(prefix) + "visor_zoom_top_m"] = five(window.edges.top);
            v24[std::string(prefix) + "visor_zoom_bottom_m"] = five(window.edges.bottom);
            v24[std::string(prefix) + "visor_zoom_scale"] = "1.0000";
            v24[std::string(prefix) + "visor_zoom_distance_m"] = "0.4000";
        }
        std::string differ;
        for (const auto& [key, value] : golden)
        {
            const auto it = now.find(key);
            const auto expected = v24.find(key);
            if (std::find(removed.begin(), removed.end(), key) != removed.end())
            {
                if (it != now.end())
                    differ += " still:" + key;
                continue;
            }
            if (it == now.end() || it->second != (expected != v24.end() ? expected->second : value))
                differ += " " + key;
        }
        for (const auto& [key, value] : now)
        {
            if (golden.count(key) || key == "menu_pointer_setcursorpos")
                continue;
            const auto expected = v24.find(key);
            if (expected == v24.end() || expected->second != value)
                differ += " +" + key;
        }
        for (const auto& [key, value] : v24)
            if (!now.count(key))
                differ += " -" + key;
        if (!differ.empty())
            std::printf("v12 floor differences:%s\n", differ.c_str());
        Check(golden.size() > 500 && differ.empty() && now.count("menu_pointer_setcursorpos") &&
                  now.at("aa_mode") == "4" && now.at("resolution_scale") == "1.00",
            "z67 A5-7 + F1 v24/v25: the owner's v12 backup loads to the pre-z67 values plus exactly v24's and v25's keys");
        Check(
            CountText(ReadTextFile(logPath), "config: v25 turns the helmet-zoom window into four edges (left ") == 1 &&
                CountText(
                    ReadTextFile(logPath), "size 1.00, picture centre 0.032, -0.020); the window looks the same") == 1,
            "F1 F-L2: v25 logs once, with the edges and the picture centre");
        Check(
            CountText(ReadTextFile(logPath),
                "config: v24 sets Reach to FXAA at resolution scale 1.30 "
                "(reach_aa_mode was 4, reach_resolution_scale was 1.00); F1 > Picture has the old SMAA + FXAA as one click") ==
                1,
            "F1 F-L1: v24 logs once, with the values Reach had (the owner's shared 4 / 1.00)");
        // A second load of the saved (v24) file migrates nothing again: an F1 choice survives.
        {
            std::string text = ReadTextFile(copy);
            const size_t at = text.find("reach_aa_mode = 1");
            if (at != std::string::npos)
                text.replace(at, 17, "reach_aa_mode = 4");
            std::ofstream(copy, std::ios::binary) << text;
        }
        ConfigLoad(copy.c_str());
        Check(g_config.title_profiles[kReachTitleProfile].aa_mode == 4 &&
                  g_config.title_profiles[kReachTitleProfile].resolution_scale == 1.30f &&
                  CountText(ReadTextFile(logPath), "config: v24 ") == 1 &&
                  CountText(ReadTextFile(logPath), "config: v25 ") == 1,
            "F1 v24/v25: a current file is not migrated again (Reach's SMAA + FXAA choice is kept, no second F-L1 or F-L2)");
        Check(!std::filesystem::exists(configDir / L"z51-v12-floor.cfg.pre12"),
            "z67 A5-7: a v12 file is at the floor: no .pre12 copy");
        std::filesystem::remove(copy);
        ConfigLoad(primary.c_str());
    }
    CheckUnit(106);
    {
        // v25: an old window other than the default migrates exactly;
        // a pre-v25 file without visor_zoom_scale meant the old default 1.5.
        const auto cfg = configDir / L"f1c-visor.cfg";
        const auto load = [&](const char* text)
        {
            std::ofstream(cfg, std::ios::binary) << text;
            ConfigLoad(cfg.c_str());
        };
        load("config_version = 23\nvisor_zoom_width_m = 0.300\nvisor_zoom_scale = 2.00\nvisor_zoom_distance_m = 0.750\n"
             "visor_zoom_offset_x_m = 0.270\nvisor_zoom_offset_y_m = -0.020\nconfig_complete = 1\n");
        const visor_zoom::Layout before = visor_zoom::WindowLayout(0.60f, 0.75f, 0.27f, -0.02f);
        const visor_zoom::EdgeWindow after =
            visor_zoom::WindowFromEdges({g_config.visor_zoom_left_m, g_config.visor_zoom_right_m,
                                            g_config.visor_zoom_top_m, g_config.visor_zoom_bottom_m},
                g_config.visor_zoom_scale, g_config.visor_zoom_distance_m, g_config.visor_zoom_anchor_x_m,
                g_config.visor_zoom_anchor_y_m);
        Check(std::fabs(after.quad.width - before.width) < 1e-6f &&
                  std::fabs(after.quad.height - before.height) < 1e-6f && std::fabs(after.quad.x - before.x) < 1e-6f &&
                  std::fabs(after.quad.y - before.y) < 1e-6f && std::fabs(after.quad.z - before.z) < 1e-6f &&
                  g_config.visor_zoom_scale == 1.0f && std::fabs(after.virtualWidth - 0.60f) < 1e-6f &&
                  g_config.title_profiles[0].visor_zoom_left_m == g_config.visor_zoom_left_m,
            "F1 v25: a 0.30 x 2 window at 0.75 m, 0.27 to the right, is the same window as edges (and every game starts from it)");
        load("config_version = 23\nvisor_zoom_width_m = 0.200\nconfig_complete = 1\n");
        Check(std::fabs((g_config.visor_zoom_right_m - g_config.visor_zoom_left_m) - 0.30f) < 1e-6f,
            "F1 v25: a pre-v25 file without visor_zoom_scale migrates with the old default 1.5");
        load("config_version = 25\nhalo4_visor_zoom_left_m = 0.10\nhalo4_visor_zoom_right_m = 0.05\n"
             "visor_zoom_distance_m = 0.10\nconfig_complete = 1\n");
        Check(std::fabs(g_config.title_profiles[3].visor_zoom_right_m - g_config.title_profiles[3].visor_zoom_left_m -
                        visor_zoom::kEdgeMinGap) < 1e-6f &&
                  g_config.visor_zoom_distance_m == visor_zoom::kDistanceMin,
            "F1 C: a game's crossed edges become the narrowest window; distance floors at 0.25 m");
        std::filesystem::remove(cfg);
        ConfigLoad(primary.c_str());
    }
    CheckUnit(107);
    {
        // Per-game section: aa_mode and resolution_scale are per game.
        Check(!std::strcmp(Config_TitleProfileName(kReachTitleProfile), "Halo: Reach"),
            "F1 0: kReachTitleProfile is Reach's profile");
        const Config fresh{};
        Check(fresh.title_profiles[kReachTitleProfile].aa_mode == 1 &&
                  fresh.title_profiles[kReachTitleProfile].resolution_scale == 1.30f &&
                  fresh.title_profiles[0].aa_mode == fresh.aa_mode && fresh.title_profiles[0].resolution_scale == 1.0f,
            "F1 A: a fresh config gives Reach FXAA at 1.30; other games the shared values");
        const auto cfg = configDir / L"f1-per-game.cfg";
        const auto write = [&](const char* text)
        {
            std::ofstream(cfg, std::ios::binary) << text;
            ConfigLoad(cfg.c_str());
        };
        write(
            "config_version = 24\naa_mode = 2\nresolution_scale = 0.90\nreach_aa_mode = 9\nreach_resolution_scale = 1.50\n"
            "halo4_aa_mode = 4x\nconfig_complete = 1\n");
        Check(g_config.title_profiles[0].aa_mode == 2 && g_config.title_profiles[0].resolution_scale == 0.90f,
            "F1 0: a game without its own key starts from the shared value");
        const std::string f1Log =
            ReadTextFile(std::filesystem::temp_directory_path() /
                         L"halomccvr-core-tests.log"); // LogInit in the v12 floor-test block above
        Check(g_config.title_profiles[kReachTitleProfile].aa_mode == 1 && g_config.title_profiles[3].aa_mode == 0 &&
                  g_config.title_profiles[kReachTitleProfile].resolution_scale == 1.50f &&
                  CountText(f1Log, "config: malformed value for 'reach_aa_mode' ignored; keeping 1") == 1 &&
                  CountText(f1Log, "config: malformed value for 'halo4_aa_mode' ignored; keeping 0") == 1,
            "F1 0: an out-of-range or malformed per-game int keeps its value and logs (ParseIntSetting, 0-4)");
        Config_ApplyTitleProfile(kReachTitleProfile);
        Check(g_config.aa_mode == 1 && g_config.resolution_scale == 1.50f, "F1 0: Reach's own values go live in Reach");
        Config_ApplyTitleProfile(0);
        Check(g_config.aa_mode == 2 && g_config.resolution_scale == 0.90f, "F1 0: switching games swaps them");
        Config_ApplyTitleProfile(-1);
        Check(g_config.aa_mode == 2 && g_config.resolution_scale == 0.90f && !Config_UseSharedValue("aa_mode"),
            "F1 0: no game: the shared values; nothing to reset");
        Config_ApplyTitleProfile(kReachTitleProfile);
        Check(Config_UseSharedValue("aa_mode") && g_config.aa_mode == 2 &&
                  g_config.title_profiles[kReachTitleProfile].aa_mode == 2 && g_config.resolution_scale == 1.50f &&
                  !Config_UseSharedValue("no_such_key"),
            "F1 0: 'use the shared value' copies one key into this game only");
        g_config.resolution_scale = 1.20f;
        g_config.aa_mode = 3;
        ConfigSave();
        const std::string saved = ReadTextFile(cfg);
        Config_ApplyTitleProfile(-1);
        ConfigLoad(cfg.c_str());
        Check(g_config.title_profiles[kReachTitleProfile].resolution_scale == 1.20f &&
                  g_config.title_profiles[kReachTitleProfile].aa_mode == 3 && g_config.resolution_scale == 0.90f &&
                  g_config.aa_mode == 2 && CountText(saved, "\naa_mode = 2") == 1 &&
                  CountText(saved, "\nresolution_scale = 0.90") == 1,
            "F1 0: a Reach F1 change saves into Reach's copy; the shared lines keep the shared values");
        std::filesystem::remove(cfg);
        ConfigLoad(primary.c_str());
    }
    CheckUnit(109);
    {
        // An old file is migrated once and saved; a value written later survives.
        const std::filesystem::path oldCfg = configDir / L"halomccvr-v12.cfg";
        {
            std::ofstream file(oldCfg);
            file << "config_version = 12\nreach_scope_cull_mode = 0\nreach_scope_order = 0\n";
        }
        ConfigLoadMigrating(oldCfg.c_str(), nullptr);
        std::string saved = ReadTextFile(oldCfg);
        Check(g_config.reach_scope_cull_mode == 1 &&
                  saved.find("config_version = " + std::to_string(kConfigVersion)) != std::string::npos &&
                  saved.find("reach_scope_cull_mode = 1") != std::string::npos,
            "#13 z49: a v12 file is migrated once and saved as the current version");
        {
            std::string edited = saved;
            const auto at = edited.find("reach_scope_cull_mode = 1");
            edited.replace(at, 25, "reach_scope_cull_mode = 0");
            std::ofstream file(oldCfg);
            file << edited;
        }
        ConfigLoadMigrating(oldCfg.c_str(), nullptr);
        Check(g_config.reach_scope_cull_mode == 0, "#13 z49: a migrated key set after the migration is not reverted");
        std::filesystem::remove(oldCfg);
        ConfigLoad(primary.c_str()); // back to the shared test file
    }
    CheckUnit(110);
    {
        // Config safety. Every key round-trips, keeps a changed value, and
        // has a range; locked, failed and cut-short files never lose the player's file.
        const auto parse = [](const std::string& text)
        {
            std::vector<std::pair<std::string, std::string>> out;
            size_t at = 0;
            while (at < text.size())
            {
                size_t end = text.find('\n', at);
                if (end == std::string::npos)
                    end = text.size();
                std::string line = text.substr(at, end - at);
                at = end + 1;
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                if (line.empty() || line[0] == '#')
                    continue;
                const size_t eq = line.find(" = ");
                if (eq == std::string::npos)
                    continue;
                out.emplace_back(line.substr(0, eq), line.substr(eq + 3));
            }
            return out;
        };
        const auto numeric = [](const std::string& v, double& out)
        {
            char* end = nullptr;
            out = std::strtod(v.c_str(), &end);
            return end && end != v.c_str() && *end == 0 && std::isfinite(out);
        };
        const auto valueOf = [&](const std::string& text, const std::string& key, std::string& out)
        {
            bool found = false;
            for (const auto& [k, v] : parse(text))
                if (k == key)
                {
                    out = v;
                    found = true;
                }
            return found;
        };
        const auto EndsWithComplete = [](std::string text)
        {
            while (!text.empty() && (text.back() == '\n' || text.back() == '\r'))
                text.pop_back();
            const std::string last = "config_complete = 1";
            return text.size() > last.size() && text.compare(text.size() - last.size(), last.size(), last) == 0;
        };
        const std::filesystem::path rt = configDir / L"roundtrip.cfg";
        std::filesystem::remove(rt);
        ConfigLoad(rt.c_str()); // no file: defaults written
        const std::string base = ReadTextFile(rt);
        ConfigLoad(rt.c_str());
        ConfigSave();
        Check(ReadTextFile(rt) == base, "R1 z54: defaults save -> load -> save is byte-identical");
        Check(base.find("config_version = " + std::to_string(kConfigVersion)) != std::string::npos &&
                  EndsWithComplete(base) && !std::filesystem::exists(configDir / L"roundtrip.cfg.tmp"),
            "R1 z54: one version source, the save ends with config_complete, no temp file left");
        // Every saved key: a changed value survives load -> save; 1e9 / -1e9 are pulled into range.
        std::vector<std::string> keys;
        for (const auto& [k, v] : parse(base))
            if (std::find(keys.begin(), keys.end(), k) == keys.end())
                keys.push_back(k);
        std::string ignored, unbounded;
        const auto writeWith = [&](const std::string& key, const std::string& value)
        {
            std::string text;
            size_t at = 0;
            size_t last = std::string::npos;
            while ((at = base.find("\n" + key + " = ", at)) != std::string::npos)
            {
                last = at;
                ++at;
            }
            if (last == std::string::npos)
                return false;
            const size_t start = last + 1 + key.size() + 3, end = base.find('\n', start);
            text = base.substr(0, start) + value + base.substr(end);
            std::ofstream file(rt, std::ios::binary | std::ios::trunc);
            file << text;
            return true;
        };
        for (const std::string& key : keys)
        {
            // Version and migration markers, not settings: the save always writes the current marker.
            if (key == "config_version" || key == "config_complete" || key == "flashlight_mapping_version" ||
                key == "scope_look_defaults")
                continue;
            std::string original;
            double v = 0.0;
            if (!valueOf(base, key, original) || !numeric(original, v))
                continue;
            const bool integer = original.find('.') == std::string::npos;
            bool kept = false;
            for (const double candidate :
                integer ? std::vector<double>{v + 1, v - 1} : std::vector<double>{v * 1.1 + 0.25, v * 0.9 - 0.25})
            {
                char text[64];
                std::snprintf(text, sizeof text, integer ? "%.0f" : "%.4f", candidate);
                if (!writeWith(key, text))
                    break;
                ConfigLoad(rt.c_str());
                ConfigSave();
                std::string now;
                double after = 0.0;
                if (valueOf(ReadTextFile(rt), key, now) && numeric(now, after) && std::fabs(after - v) > 1e-9)
                {
                    kept = true;
                    break;
                }
            }
            if (!kept)
                ignored += " " + key;
            for (const char* huge : {"1000000000", "-1000000000"})
            {
                if (!writeWith(key, huge))
                    break;
                ConfigLoad(rt.c_str());
                ConfigSave();
                std::string now;
                double after = 0.0;
                if (valueOf(ReadTextFile(rt), key, now) && numeric(now, after) && std::fabs(after) >= 1.0e8)
                {
                    unbounded += " " + key + "=" + huge;
                    break;
                }
            }
        }
        if (!ignored.empty())
            std::printf("keys that drop a changed value:%s\n", ignored.c_str());
        if (!unbounded.empty())
            std::printf("keys with no range:%s\n", unbounded.c_str());
        Check(ignored.empty(), "R1 z54: every saved key keeps a changed value through load and save");
        Check(unbounded.empty(), "R1 z54: every saved number is pulled into a range (no 1e9 survives)");
        // The CE visor zoom cap is saved.
        ConfigLoad(rt.c_str());
        g_config.ce_visor_zoom_max = 4.0f;
        ConfigSave();
        ConfigLoad(rt.c_str());
        Check(g_config.ce_visor_zoom_max == 4.0f, "R1 z54: ce_visor_zoom_max survives save and load");
        // A failed replace leaves the old file intact and no temp file.
        const std::string before = ReadTextFile(rt);
        SetFileAttributesW(rt.c_str(), FILE_ATTRIBUTE_READONLY);
        g_config.ce_visor_zoom_max = 5.0f;
        ConfigSave();
        SetFileAttributesW(rt.c_str(), FILE_ATTRIBUTE_NORMAL);
        Check(ReadTextFile(rt) == before && !std::filesystem::exists(configDir / L"roundtrip.cfg.tmp"),
            "R1 z54: a save that cannot replace the file leaves the old file and no temp file");
        // A current-version file cut short is reported and kept as .short.
        {
            std::ofstream file(rt, std::ios::binary | std::ios::trunc);
            file << before.substr(0, before.size() / 2) << "\n";
        }
        std::filesystem::remove(configDir / L"roundtrip.cfg.short");
        ConfigLoad(rt.c_str());
        Check(std::filesystem::exists(configDir / L"roundtrip.cfg.short"),
            "R1 z54: a file without config_complete is kept as .short");
        ConfigSave();
        // Reset ALL keeps a backup.
        const std::string current = ReadTextFile(rt);
        Check(Config_BackupForReset() && ReadTextFile(configDir / L"roundtrip.cfg.bak") == current,
            "R1 z54: the settings file is copied to .bak before Reset ALL");
        // A file that exists but cannot be opened is neither replaced nor saved over.
        HANDLE held = CreateFileW(rt.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        ConfigLoadMigrating(rt.c_str(), nullptr);
        g_config.ce_visor_zoom_max = 7.0f;
        ConfigSave();
        if (held != INVALID_HANDLE_VALUE)
            CloseHandle(held);
        Check(held != INVALID_HANDLE_VALUE && ReadTextFile(rt) == current,
            "R1 z54: a locked settings file is not replaced with defaults or saved over");
        for (const wchar_t* suffix : {L"", L".bak", L".short", L".tmp"})
            std::filesystem::remove(configDir / (std::wstring(L"roundtrip.cfg") + suffix));
        ConfigLoad(primary.c_str()); // back to the shared test file (also clears the save block)
    }
    CheckUnit(111);
    g_config.two_hand_coherent_aim = true;
    ConfigSave();
    CheckUnit(112);
    {
        const std::filesystem::path oldAimConfig = configDir / L"coherent-aim-v5.cfg";
        const std::filesystem::path rollbackAimConfig = configDir / L"coherent-aim-v6-rollback.cfg";
        {
            std::ofstream file(oldAimConfig);
            file << "config_version = 5\n";
            file << "two_hand_coherent_aim = 0\n";
        }
        // Files before v12 are no longer migrated. The stated value
        // loads as written, the file as found is kept as .pre12, and the next
        // save writes the current version.
        const std::string asFound = ReadTextFile(oldAimConfig);
        std::filesystem::remove(configDir / L"coherent-aim-v5.cfg.pre12");
        ConfigLoad(oldAimConfig.c_str());
        Check(!g_config.two_hand_coherent_aim,
            "z67 A5-7: a pre-v12 file's stated OFF loads as written (the v6 migration is retired)");
        Check(ReadTextFile(configDir / L"coherent-aim-v5.cfg.pre12") == asFound,
            "z67 A5-7: a pre-v12 file is kept as found in a .pre12 copy");
        ConfigSave();
        ConfigLoad(oldAimConfig.c_str());
        Check(g_config.config_version == kConfigVersion && !g_config.two_hand_coherent_aim,
            "z67 A5-7: the saved pre-v12 file carries the current version and keeps its value");
        std::filesystem::remove(configDir / L"coherent-aim-v5.cfg.pre12");

        {
            std::ofstream file(rollbackAimConfig);
            file << "config_version = 6\n";
            file << "two_hand_coherent_aim = 0\n";
        }
        ConfigLoad(rollbackAimConfig.c_str());
        Check(!g_config.two_hand_coherent_aim, "A v6 config's explicit hidden OFF rollback remains honored");

        // v13: a saved grab Wheel (1) returns to Stick (2) once; Point stays;
        // choosing Wheel again in a v13 file is kept.
        const std::filesystem::path wheelConfig = configDir / L"wheel-v12.cfg";
        {
            std::ofstream file(wheelConfig);
            file << "config_version = 12\nvehicle_steer_car = 1\nvehicle_steer_bike = 1\n";
        }
        ConfigLoad(wheelConfig.c_str());
        Check(g_config.vehicle_steer_car == 2 && g_config.vehicle_steer_bike == 2,
            "v13 moves a saved grab Wheel back to Stick");
        Check(g_config.vehicle_motion && !g_config.vehicle_double_grip_wheel,
            "v13 keeps vehicle_motion (H2/CE vehicle control) and the double-grip wheel is off");
        {
            std::ofstream file(wheelConfig);
            file << "config_version = 12\nvehicle_steer_car = 0\nvehicle_steer_bike = 1\n";
        }
        ConfigLoad(wheelConfig.c_str());
        Check(g_config.vehicle_steer_car == 0 && g_config.vehicle_steer_bike == 2,
            "v13 keeps Point and moves only Wheel");
        {
            std::ofstream file(wheelConfig);
            file
                << "config_version = 13\nvehicle_steer_car = 1\nvehicle_steer_bike = 1\nvehicle_double_grip_wheel = 1\n";
        }
        ConfigLoad(wheelConfig.c_str());
        Check(g_config.vehicle_steer_car == 1 && g_config.vehicle_steer_bike == 1,
            "a Wheel chosen again in a v13 config is kept");
        Check(g_config.vehicle_double_grip_wheel, "the double-grip wheel turned on again is kept");
        std::filesystem::remove(wheelConfig);
        {
            // v16: the visor zoom window moves to the right-eye axis once; later F1 choices are kept.
            const std::filesystem::path visorConfig = configDir / L"visor-v15.cfg";
            {
                std::ofstream file(visorConfig);
                file
                    << "config_version = 15\nvisor_zoom_distance_m = 0.75\nvisor_zoom_offset_x_m = 0.27\nvisor_zoom_width_m = 0.30\n";
            }
            ConfigLoad(visorConfig.c_str());
            // v25 then turns that window into edges (same window).
            Check(std::fabs(g_config.visor_zoom_distance_m - 0.40f) < 1e-4f &&
                      std::fabs((g_config.visor_zoom_left_m + g_config.visor_zoom_right_m) * 0.5f - 0.032f) < 1e-4f &&
                      std::fabs(g_config.visor_zoom_right_m - g_config.visor_zoom_left_m - 0.1995f) < 1e-3f,
                "a v15 visor window resets to the right-eye pose once");
            {
                std::ofstream file(visorConfig);
                file << "config_version = 16\nvisor_zoom_distance_m = 0.60\nvisor_zoom_offset_x_m = 0.10\n";
            }
            ConfigLoad(visorConfig.c_str());
            Check(std::fabs(g_config.visor_zoom_distance_m - 0.60f) < 1e-4f &&
                      std::fabs((g_config.visor_zoom_left_m + g_config.visor_zoom_right_m) * 0.5f - 0.10f) < 1e-4f,
                "a v16 visor window choice is kept");
            std::filesystem::remove(visorConfig);
        }
        {
            // sharpness_noise_gate parses, clamps at 0.25 and a non-finite value goes to 0.
            const std::filesystem::path gateConfig = configDir / L"gate.cfg";
            const auto gateFrom = [&](const char* value)
            {
                {
                    std::ofstream file(gateConfig);
                    file << "sharpness_noise_gate = " << value << "\n";
                }
                ConfigLoad(gateConfig.c_str());
                return g_config.sharpness_noise_gate;
            };
            Check(std::fabs(gateFrom("0.05") - 0.05f) < 1e-6f, "sharpness_noise_gate parses");
            Check(std::fabs(gateFrom("9") - 0.25f) < 1e-6f, "sharpness_noise_gate clamps at 0.25");
            Check(gateFrom("nan") == 0.0f, "a non-finite sharpness_noise_gate goes to 0");
            std::filesystem::remove(gateConfig);
        }
        {
            // v15: the removed cockpit layer/controls keys are read and dropped on save;
            // the floating cockpit screen and the eye-up opt-in survive.
            const std::filesystem::path cockpitConfig = configDir / L"cockpit-v14.cfg";
            {
                std::ofstream file(cockpitConfig);
                file << "config_version = 14\nvehicle_cockpit = 1\nvehicle_cockpit_scorpion_offset = 0.1 0 0\n"
                        "cockpit_stick_expo = 0.5\ncockpit_grip_offset_m = 0.1\nvehicle_cockpit_screen = 0\n"
                        "vehicle_cockpit_eye_up = 1\n";
            }
            ConfigLoad(cockpitConfig.c_str());
            Check(!g_config.vehicle_cockpit_screen && g_config.vehicle_cockpit_eye_up,
                "v15 keeps the cockpit screen and eye-up keys");
            ConfigSave();
            const std::string saved = ReadTextFile(cockpitConfig);
            Check(saved.find("vehicle_cockpit =") == std::string::npos &&
                      saved.find("vehicle_cockpit_scorpion") == std::string::npos &&
                      saved.find("cockpit_stick_") == std::string::npos &&
                      saved.find("cockpit_grip_offset_m") == std::string::npos &&
                      saved.find("vehicle_cockpit_screen = 0") != std::string::npos,
                "v15 drops the cockpit layer/controls keys on save");
            std::filesystem::remove(cockpitConfig);
        }
        {
            // Vehicle monitor keys: defaults on, parse, save, clamp (NaN included).
            const Config fresh{};
            Check(fresh.vehicle_monitors && fresh.vehicle_monitor_forward && fresh.vehicle_monitor_left &&
                      fresh.vehicle_monitor_right && fresh.vehicle_monitor_width_m == 0.42f &&
                      fresh.vehicle_monitor_distance_m == 0.60f,
                "B2: vehicle monitors default on, 0.42 m wide at 0.60 m");
            const std::filesystem::path monitorConfig = configDir / L"monitors-b2.cfg";
            {
                std::ofstream file(monitorConfig);
                file
                    << "vehicle_monitors = 0\nvehicle_monitor_forward = 1\nvehicle_monitor_left = 0\nvehicle_monitor_right = 1\n"
                       "vehicle_monitor_width_m = 9\nvehicle_monitor_distance_m = nan\n";
            }
            ConfigLoad(monitorConfig.c_str());
            Check(!g_config.vehicle_monitors && g_config.vehicle_monitor_forward && !g_config.vehicle_monitor_left &&
                      g_config.vehicle_monitor_right,
                "B2: monitor switches parse");
            Check(g_config.vehicle_monitor_width_m == 0.80f && g_config.vehicle_monitor_distance_m == 0.60f,
                "B2: width clamps to 0.80 m, a NaN distance falls back to 0.60 m");
            ConfigSave();
            const std::string saved = ReadTextFile(monitorConfig);
            Check(saved.find("vehicle_monitors = 0") != std::string::npos &&
                      saved.find("vehicle_monitor_left = 0") != std::string::npos &&
                      saved.find("vehicle_monitor_width_m = 0.800") != std::string::npos,
                "B2: monitor keys save");
            std::filesystem::remove(monitorConfig);
        }
        {
            // CE vehicle switches default on, parse and save.
            Check(Config{}.ce_vehicle_eye_provisional && Config{}.ce_vehicle_eye_every_frame &&
                      Config{}.ce_turret_aim_frozen,
                "B1: the three CE vehicle switches default on");
            const std::filesystem::path ceConfig = configDir / L"ce-b1.cfg";
            {
                std::ofstream file(ceConfig);
                file << "ce_vehicle_eye_provisional = 0\nce_vehicle_eye_every_frame = 0\nce_turret_aim_frozen = 0\n";
            }
            ConfigLoad(ceConfig.c_str());
            Check(!g_config.ce_vehicle_eye_provisional && !g_config.ce_vehicle_eye_every_frame &&
                      !g_config.ce_turret_aim_frozen,
                "B1: the three CE vehicle switches parse");
            ConfigSave();
            const std::string saved = ReadTextFile(ceConfig);
            Check(saved.find("ce_vehicle_eye_provisional = 0") != std::string::npos &&
                      saved.find("ce_vehicle_eye_every_frame = 0") != std::string::npos &&
                      saved.find("ce_turret_aim_frozen = 0") != std::string::npos,
                "B1: the three CE vehicle switches save");
            std::filesystem::remove(ceConfig);
        }
        {
            // CE Anniversary monitor frames: defaults, parse, save, clamps (NaN included).
            const Config fresh{};
            Check(fresh.ce_anniv_hull_frames_moving_every == 9, "B4: vehicle screens while driving default one in 9");
            Check(fresh.ce_anniv_zoom_boost_frames == 90 && fresh.ce_anniv_zoom_boost_every == 2,
                "CE anniv zoom boost defaults");
            Check(fresh.ce_anniv_monitor_frames && fresh.ce_anniv_monitor_every == 3 &&
                      fresh.ce_anniv_monitor_still_deg == 0.5f,
                "B4: monitor frames on, one in 3, head still under 0.5 deg");
            const std::filesystem::path annivConfig = configDir / L"ce-b4.cfg";
            {
                std::ofstream file(annivConfig);
                file << "ce_anniv_monitor_frames = 0\nce_anniv_monitor_every = 40\nce_anniv_monitor_still_deg = nan\n"
                        "ce_anniv_hull_frames_moving_every = 0\nce_anniv_glass_raised_every = 1\n";
            }
            ConfigLoad(annivConfig.c_str());
            Check(!g_config.ce_anniv_monitor_frames && g_config.ce_anniv_monitor_every == 12 &&
                      g_config.ce_anniv_monitor_still_deg == 0.5f,
                "B4: parse, every clamps to 12, NaN still falls back to 0.5");
            ConfigSave();
            const std::string saved = ReadTextFile(annivConfig);
            Check(saved.find("ce_anniv_monitor_frames = 0") != std::string::npos &&
                      saved.find("ce_anniv_monitor_every = 12") != std::string::npos &&
                      saved.find("ce_anniv_hull_frames_moving_every = 0") != std::string::npos,
                "B4: monitor-frame keys save");
            Check(fresh.ce_anniv_glass_raised_every == 6 && g_config.ce_anniv_glass_raised_every == 2 &&
                      saved.find("ce_anniv_glass_raised_every = 2") != std::string::npos,
                "#12 z47: raised glass borrows 1 in 6 by default; parses, clamps to 2, saves");
            std::filesystem::remove(annivConfig);
        }
        {
            // v17: vehicles start in third person; a v17 choice of first person is kept.
            Check(!Config{}.vehicle_first_person && !Config{}.vehicle_first_person_cfg,
                "v17: a fresh config starts vehicles in third person");
            const std::filesystem::path seatConfig = configDir / L"seat-v16.cfg";
            {
                std::ofstream file(seatConfig);
                file << "config_version = 16\nvehicle_first_person = 1\n";
            }
            ConfigLoad(seatConfig.c_str());
            Check(!g_config.vehicle_first_person && !g_config.vehicle_first_person_cfg,
                "v17: an older cfg is reset to third person once");
            {
                std::ofstream file(seatConfig);
                file << "config_version = 17\nvehicle_first_person = 1\n";
            }
            ConfigLoad(seatConfig.c_str());
            Check(g_config.vehicle_first_person && g_config.vehicle_first_person_cfg,
                "v17: first person chosen after the reset is kept");
            std::filesystem::remove(seatConfig);
        }

        // Restore the shared test config and ConfigSave destination after these
        // isolated version-migration cases.
        ConfigLoad(primary.c_str());
        std::filesystem::remove(oldAimConfig);
        std::filesystem::remove(rollbackAimConfig);
    }
    CheckUnit(113);
    {
        // Exercise the real installer merge followed by runtime parsing and
        // saving. Text-only installer assertions cannot catch new defaults
        // suppressing a ConfigLoad migration or inherited title calibration.
        const auto mergedPath = configDir / L"installer-runtime-roundtrip.cfg";
        const auto verifyMerged = [&](const std::string& previous, auto&& valid, const char* message)
        {
            {
                std::ofstream file(mergedPath);
                file << mcc_installer::MergeConfig(previous, organizedConfig).text;
            }
            ConfigLoad(mergedPath.c_str());
            Check(valid(), message);
            ConfigSave();
            ConfigLoad(mergedPath.c_str());
            Check(valid(), message);
        };
        verifyMerged(
            "config_version=5\nvirtual_stock_rear_reference=1\n"
            "virtual_stock_strength=0.61\ntwo_hand_smoothing=1\n"
            "two_hand_coherent_aim=0\n",
            []
            {
                return g_config.virtual_stock_rear_reference == 1 && g_config.virtual_stock_standard_strength == .61f &&
                       g_config.virtual_stock_plus_strength == kVirtualStockV7StrengthDefault &&
                       g_config.two_hand_smoothing_strength == 25.0f && !g_config.two_hand_coherent_aim &&
                       g_config.config_version == kConfigVersion;
            },
            "installer/runtime roundtrip migrates legacy Standard strength and smoothing (z67: a v5 coherent-aim OFF is kept)");
        Check(mcc_installer::MergeConfig("config_version = 10\n", "vr_bind_halo3_jump = 7\n").addedKeys == 1 &&
                  mcc_installer::MergeConfig("config_version = 6\n", "vr_bind_halo3_jump = 7\n").addedKeys == 0 &&
                  mcc_installer::MergeConfig("config_version = 6\n", "game_menu_pointer = 1\n").addedKeys == 0 &&
                  mcc_installer::MergeConfig("config_version = 7x\n", "vr_bind_halo3_jump = 7\n").addedKeys == 0,
            "installer reads config_version as a whole number (10 is newer than 7; junk is version 1)");
        verifyMerged(
            "config_version=5\nvirtual_stock_rear_reference=3\n"
            "virtual_stock_strength=0.44\ntwo_hand_smoothing=0\n",
            []
            {
                return g_config.virtual_stock_rear_reference == 3 && g_config.virtual_stock_plus_strength == .44f &&
                       g_config.virtual_stock_standard_strength == kVirtualStockV7StrengthDefault &&
                       g_config.two_hand_smoothing_strength == 0.0f;
            },
            "installer/runtime roundtrip migrates legacy Plus strength and disabled smoothing");
        verifyMerged(
            "config_version=6\nvirtual_stock_rear_reference=3\n"
            "virtual_stock_strength=0.44\nvirtual_stock_plus_strength=0.77\n"
            "virtual_stock_standard_strength=0.91\ntwo_hand_smoothing=1\n"
            "two_hand_smoothing_strength=12.5\ntwo_hand_coherent_aim=0\n",
            []
            {
                return g_config.virtual_stock_plus_strength == .77f &&
                       g_config.virtual_stock_standard_strength == .91f &&
                       g_config.two_hand_smoothing_strength == 12.5f && !g_config.two_hand_coherent_aim;
            },
            "installer/runtime roundtrip preserves explicit new strengths and v6 coherent-aim rollback");
        verifyMerged(
            "config_version=6\ngun_scale=0.8\nhud_size=0.7\n"
            "barrel_pitch_deg=12\nhalo3_gun_scale=0.65\n",
            []
            {
                bool valid = g_config.base_tunables.gun_scale == .8f;
                for (int i = 0; i < kTitleProfileCount; ++i)
                {
                    const auto& profile = g_config.title_profiles[i];
                    valid = valid && profile.gun_scale == (i == 0 ? .65f : .8f) && profile.hud_size == .7f &&
                            profile.barrel_pitch_deg == 12.0f;
                }
                return valid;
            },
            "installer/runtime roundtrip inherits missing title calibrations and preserves explicit title override");
        ConfigLoad(primary.c_str());
        std::filesystem::remove(mergedPath);
    }
    CheckUnit(114);
    g_config.independent_dual_aim = true;
    ConfigSave();
    g_config.independent_dual_aim = false;
    ConfigLoad(primary.c_str());
    Check(g_config.independent_dual_aim, "independent dual aim persists through a config round trip");
    g_config.independent_dual_aim = false;
    ConfigSave();
    Check(CountText(organizedConfig, "\ntwo_hand_switch_inherit = 0") == 1,
        "switched-weapon two-hand inheritance is written with its default off");
    Check(!g_config.two_hand_switch_inherit, "switched-weapon two-hand inheritance defaults off");
    g_config.two_hand_switch_inherit = true;
    ConfigSave();
    g_config.two_hand_switch_inherit = false;
    ConfigLoad(primary.c_str());
    Check(
        g_config.two_hand_switch_inherit, "switched-weapon two-hand inheritance persists through a config round trip");
    g_config.two_hand_switch_inherit = false;
    ConfigSave();
    Check(g_config.gun_barrel_aim && CountText(organizedConfig, "\ngun_barrel_aim = 1") == 1,
        "barrel aim is on after the v8 migration (shots leave the visible gun)");
    g_config.gun_barrel_aim = true;
    ConfigSave();
    g_config.gun_barrel_aim = false;
    ConfigLoad(primary.c_str());
    Check(g_config.gun_barrel_aim && !g_config.independent_dual_aim,
        "barrel trajectory persists independently of dual trajectories");
    g_config.gun_barrel_aim = false;
    ConfigSave();
    CheckUnit(118);
    Check(Config{}.virtual_stock == kVirtualStockEnabledDefault && !kVirtualStockEnabledDefault,
        "virtual stock defaults off for backwards compatibility");
    // v7 defaults are blend 0.5 (prior art); 95/80 stay the legacy values.
    Check(Config{}.virtual_stock_standard_strength == kVirtualStockV7StrengthDefault &&
              Config{}.virtual_stock_plus_strength == kVirtualStockV7StrengthDefault &&
              kVirtualStockV7StrengthDefault == 0.50f &&
              Config{}.virtual_stock_rear_height_m == kVirtualStockRearHeightDefaultM &&
              kVirtualStockStandardStrengthDefault == 0.95f && kVirtualStockPlusStrengthDefault == 0.80f &&
              kVirtualStockRearHeightDefaultM == -0.220f,
        "Standard and Plus default to 0.5 (legacy 95/80 percent)");
    Check(Config{}.virtual_stock_proximity_release == kVirtualStockProximityReleaseDefault &&
              Config{}.virtual_stock_proximity_full_m == kVirtualStockProximityFullDefaultM &&
              Config{}.virtual_stock_proximity_release_m == kVirtualStockV7ProximityReleaseDefaultM &&
              kVirtualStockProximityFullDefaultM == 0.270f && kVirtualStockV7ProximityReleaseDefaultM == 0.40f &&
              kVirtualStockProximityReleaseDefaultM == 0.425f,
        "proximity release defaults on with the 0.270/0.40 metre pair (legacy 0.425)");
    Check(Config{}.virtual_stock_rear_reference == kVirtualStockProductRearReferenceDefault &&
              Config{}.virtual_stock_shoulder_back_m == kVirtualStockShoulderBackDefaultM &&
              Config{}.virtual_stock_shoulder_side_m == kVirtualStockShoulderSideDefaultM &&
              kVirtualStockShoulderBackDefaultM == 0.005f && kVirtualStockShoulderSideDefaultM == 0.015f &&
              kVirtualStockProductRearReferenceDefault == 3,
        "Virtual Stock defaults to Plus Centre with the accepted Shoulder offsets");
    Check(Config{}.virtual_stock_chest_height_m == kVirtualStockChestHeightDefaultM &&
              Config{}.virtual_stock_chest_back_m == kVirtualStockChestBackDefaultM &&
              Config{}.virtual_stock_chest_side_m == kVirtualStockChestSideDefaultM &&
              kVirtualStockChestHeightDefaultM == -0.320f && kVirtualStockChestBackDefaultM == 0.000f &&
              kVirtualStockChestSideDefaultM == 0.015f,
        "Chest defaults use the lower centered test seeds");
    Check(Config{}.virtual_stock_adaptive_top_height_m == kVirtualStockAdaptiveTopHeightDefaultM &&
              Config{}.virtual_stock_adaptive_bottom_height_m == kVirtualStockAdaptiveBottomHeightDefaultM &&
              Config{}.virtual_stock_adaptive_top_half_width_m == kVirtualStockAdaptiveTopHalfWidthDefaultM &&
              Config{}.virtual_stock_adaptive_bottom_half_width_m == kVirtualStockAdaptiveBottomHalfWidthDefaultM &&
              kVirtualStockAdaptiveTopHeightDefaultM == -0.180f &&
              kVirtualStockAdaptiveBottomHeightDefaultM == -0.450f &&
              kVirtualStockAdaptiveTopHalfWidthDefaultM == 0.080f &&
              kVirtualStockAdaptiveBottomHalfWidthDefaultM == 0.140f,
        "Adaptive defaults use the broad coplanar coverage seed");
    Check(Config{}.virtual_stock_hybrid_offhand_influence == kVirtualStockHybridOffhandInfluenceDefault &&
              Config{}.virtual_stock_hybrid_ads_reference == kVirtualStockHybridAdsReferenceDefault &&
              Config{}.virtual_stock_hybrid_seat_full_m == kVirtualStockProductHybridSeatFullDefaultM &&
              Config{}.virtual_stock_hybrid_seat_release_m == kVirtualStockProductHybridSeatReleaseDefaultM &&
              kVirtualStockHybridOffhandInfluenceDefault == 0.50f && kVirtualStockHybridAdsReferenceDefault == 0 &&
              Config{}.virtual_stock_hybrid_horizontal_release &&
              Config{}.virtual_stock_hybrid_horizontal_full_m == 0.330f &&
              Config{}.virtual_stock_hybrid_horizontal_release_m == 0.475f &&
              kVirtualStockProductHybridSeatFullDefaultM == 0.464f &&
              kVirtualStockProductHybridSeatReleaseDefaultM == 0.474f,
        "Plus defaults use accepted offhand, Centre, horizontal unloading, and broad safety values");
    Check(!kVirtualStockHybridInverseNeckEnabledDefault && kVirtualStockHybridInverseNeckStrengthDefault == 0.0f &&
              Config{}.two_hand_support_grip_pose,
        "inverse-neck stays off while support-hand rotation reduction defaults on");
    CheckUnit(119);
    {
        Config mode{};
        Check(!mode.virtual_stock && VirtualStockUsesPlusMode(mode) && !VirtualStockUsesShoulderReference(mode),
            "enabling a fresh Virtual Stock configuration starts in Plus Centre");
        mode.virtual_stock = true;
        Check(VirtualStockUsesPlusMode(mode), "enabling Virtual Stock preserves the default Plus mode");
        mode.virtual_stock_rear_reference = 1;
        SetVirtualStockPlusMode(mode, true);
        Check(VirtualStockUsesPlusMode(mode) && VirtualStockUsesShoulderReference(mode) &&
                  mode.virtual_stock_hybrid_ads_reference == 1,
            "Standard Shoulder switches to Plus while preserving Shoulder");
        SetVirtualStockPlusMode(mode, false);
        Check(!VirtualStockUsesPlusMode(mode) && mode.virtual_stock_rear_reference == 1,
            "Plus Shoulder switches back to Standard Shoulder");
        SetVirtualStockShoulderReference(mode, false);
        SetVirtualStockPlusMode(mode, true);
        Check(VirtualStockUsesPlusMode(mode) && !VirtualStockUsesShoulderReference(mode),
            "Standard Centre switches to Plus Centre");
        mode.virtual_stock = false;
        SetVirtualStockShoulderReference(mode, true);
        mode.virtual_stock = true;
        Check(VirtualStockUsesPlusMode(mode) && VirtualStockUsesShoulderReference(mode),
            "turning Virtual Stock off and on preserves mode and rear reference");
        mode.virtual_stock_plus_strength = 0.81f;
        SetVirtualStockPlusMode(mode, false);
        Check(VirtualStockActiveStrength(mode) == kVirtualStockV7StrengthDefault,
            "switching to Standard restores its independent strength");
        VirtualStockActiveStrength(mode) = 0.93f;
        Check(mode.virtual_stock_standard_strength == 0.93f && mode.virtual_stock_plus_strength == 0.81f,
            "editing Standard strength does not change Plus strength");
        SetVirtualStockPlusMode(mode, true);
        Check(VirtualStockActiveStrength(mode) == 0.81f, "switching to Plus restores its independent strength");
        VirtualStockActiveStrength(mode) = 0.79f;
        Check(mode.virtual_stock_standard_strength == 0.93f && mode.virtual_stock_plus_strength == 0.79f,
            "editing Plus strength does not change Standard strength");
    }
    CheckUnit(120);
    {
        Config reset{};
        reset.virtual_stock = true;
        reset.virtual_stock_standard_strength = 0.12f;
        reset.virtual_stock_plus_strength = 0.13f;
        reset.virtual_stock_rear_reference = 1;
        reset.virtual_stock_hybrid_ads_reference = 1;
        reset.virtual_stock_hybrid_horizontal_release = false;
        reset.virtual_stock_hybrid_horizontal_full_m = 0.20f;
        reset.virtual_stock_hybrid_horizontal_release_m = 0.30f;
        reset.virtual_stock_hybrid_seat_full_m = 0.10f;
        reset.virtual_stock_hybrid_seat_release_m = 0.20f;
        reset.virtual_stock_proximity_release = false;
        reset.two_hand_support_grip_pose = false;
        reset.gun_pitch_deg = 17.0f;
        ResetVirtualStockSettings(reset);
        const VirtualStockAimSettings settings =
            VirtualStockAimSettingsFromConfig(reset, HybridDiagnosticOverride::Normal);
        Check(reset.virtual_stock && settings.virtualStockEnabled && settings.virtualStockRearReference == 3 &&
                  settings.virtualStockHybridAdsReference == 0 && settings.virtualStockStrength == 0.50f &&
                  reset.virtual_stock_standard_strength == 0.50f && reset.virtual_stock_plus_strength == 0.50f &&
                  settings.virtualStockRearHeightM == -0.220f && settings.virtualStockHybridOffhandInfluence == 0.50f &&
                  settings.hybridHorizontalRearReleaseEnabled && settings.hybridHorizontalRearReleaseFullM == 0.330f &&
                  settings.hybridHorizontalRearReleaseReleaseM == 0.475f &&
                  settings.virtualStockHybridSeatFullM == 0.464f && settings.virtualStockHybridSeatReleaseM == 0.474f &&
                  settings.virtualStockProximityRelease && settings.virtualStockProximityFullM == 0.270f &&
                  settings.virtualStockProximityReleaseM == 0.40f && !settings.hybridInverseNeckEnabled &&
                  settings.hybridInverseNeckStrength == kVirtualStockHybridInverseNeckStrengthDefault &&
                  settings.hybridInverseNeckForwardM == kVirtualStockHybridInverseNeckForwardDefaultM &&
                  settings.hybridInverseNeckUpM == kVirtualStockHybridInverseNeckUpDefaultM &&
                  settings.hybridInverseNeckLateralM == kVirtualStockHybridInverseNeckLateralDefaultM,
            "Virtual Stock reset restores the accepted Plus Centre composition with inverse-neck disabled");
        Check(!reset.two_hand_support_grip_pose && reset.gun_pitch_deg == 17.0f,
            "Virtual Stock reset preserves support-hand rotation reduction and unrelated aim settings");
        reset.virtual_stock = false;
        reset.virtual_stock_standard_strength = 0.12f;
        reset.virtual_stock_plus_strength = 0.13f;
        ResetVirtualStockSettings(reset);
        Check(!reset.virtual_stock && reset.virtual_stock_standard_strength == kVirtualStockV7StrengthDefault &&
                  reset.virtual_stock_plus_strength == kVirtualStockV7StrengthDefault &&
                  !reset.two_hand_support_grip_pose && reset.gun_pitch_deg == 17.0f,
            "Virtual Stock reset also preserves the disabled state and unrelated settings");
    }
    CheckUnit(122);
    Check(kVirtualStockStrengthMinimum == 0.0f && kVirtualStockStrengthMaximum == 1.0f &&
              kVirtualStockRearHeightMinimumM == -0.30f && kVirtualStockRearHeightMaximumM == 0.10f &&
              kVirtualStockShoulderBackMinimumM == 0.0f && kVirtualStockShoulderBackMaximumM == 0.25f &&
              kVirtualStockShoulderSideMinimumM == 0.0f && kVirtualStockShoulderSideMaximumM == 0.20f &&
              kVirtualStockHybridSeatFullMinimumM == 0.010f && kVirtualStockHybridSeatFullMaximumM == 0.600f &&
              kVirtualStockHybridSeatReleaseMinimumM == 0.020f && kVirtualStockHybridSeatReleaseMaximumM == 0.800f &&
              kVirtualStockHybridSeatMinimumSeparationM == 0.010f && kVirtualStockProximityFullMinimumM == 0.10f &&
              kVirtualStockProximityFullMaximumM == 0.60f && kVirtualStockProximityReleaseMinimumM == 0.15f &&
              kVirtualStockProximityReleaseMaximumM == 0.80f,
        "neutral Virtual Stock bounds match the production parser and UI policy");
    Check(CountText(organizedConfig, "\nvirtual_stock = 0") == 1,
        "virtual stock default is written to legacy configurations");
    Check(CountText(organizedConfig, "\nvirtual_stock_standard_strength = 0.50") == 1 &&
              CountText(organizedConfig, "\nvirtual_stock_plus_strength = 0.50") == 1 &&
              CountText(organizedConfig, "\nvirtual_stock_strength = ") == 0 &&
              CountText(organizedConfig, "\nvirtual_stock_rear_height_m = -0.220") == 1,
        "independent strength defaults replace the legacy shared key");
    Check(CountText(organizedConfig, "\nvirtual_stock_proximity_release = 1") == 1 &&
              CountText(organizedConfig, "\nvirtual_stock_proximity_full_m = 0.270") == 1 &&
              CountText(organizedConfig, "\nvirtual_stock_proximity_release_m = 0.400") == 1,
        "proximity release defaults (V7) are written exactly once to an upgraded old file");
    Check(CountText(organizedConfig, "virtual_stock_head_turn_sway_correction") == 0,
        "the retired sway correction key is never written to generated configurations");
    Check(CountText(organizedConfig, "\nvirtual_stock_rear_reference = 3") == 1 &&
              CountText(organizedConfig, "\nvirtual_stock_shoulder_back_m = 0.005") == 1 &&
              CountText(organizedConfig, "\nvirtual_stock_shoulder_side_m = 0.015") == 1,
        "Shoulder A/B defaults are written exactly once to legacy configurations");
    Check(CountText(organizedConfig, "\nvirtual_stock_chest_height_m = -0.320") == 1 &&
              CountText(organizedConfig, "\nvirtual_stock_chest_back_m = 0.000") == 1 &&
              CountText(organizedConfig, "\nvirtual_stock_chest_side_m = 0.015") == 1,
        "Chest defaults are written exactly once to legacy configurations");
    Check(CountText(organizedConfig, "\nvirtual_stock_adaptive_top_height_m = -0.180") == 1 &&
              CountText(organizedConfig, "\nvirtual_stock_adaptive_bottom_height_m = -0.450") == 1 &&
              CountText(organizedConfig, "\nvirtual_stock_adaptive_top_half_width_m = 0.080") == 1 &&
              CountText(organizedConfig, "\nvirtual_stock_adaptive_bottom_half_width_m = 0.140") == 1,
        "Adaptive defaults are written exactly once to legacy configurations");
    Check(CountText(organizedConfig, "\nvirtual_stock_hybrid_offhand_influence = 0.50") == 1 &&
              CountText(organizedConfig, "\nvirtual_stock_hybrid_ads_reference = 0") == 1 &&
              CountText(organizedConfig, "\nvirtual_stock_hybrid_seat_full_m = 0.464") == 1 &&
              CountText(organizedConfig, "\nvirtual_stock_hybrid_seat_release_m = 0.474") == 1 &&
              CountText(organizedConfig, "\nvirtual_stock_hybrid_horizontal_release = 1") == 1 &&
              CountText(organizedConfig, "\nvirtual_stock_hybrid_horizontal_full_m = 0.330") == 1 &&
              CountText(organizedConfig, "\nvirtual_stock_hybrid_horizontal_release_m = 0.475") == 1,
        "Plus canonical keys are emitted exactly once with their defaults");
    g_config.virtual_stock = true;
    g_config.virtual_stock_standard_strength = 0.93f;
    g_config.virtual_stock_plus_strength = 0.75f;
    g_config.virtual_stock_rear_height_m = -0.05f;
    ConfigSave();
    g_config.virtual_stock = false;
    g_config.virtual_stock_standard_strength = kVirtualStockStandardStrengthDefault;
    g_config.virtual_stock_plus_strength = kVirtualStockPlusStrengthDefault;
    g_config.virtual_stock_rear_height_m = -0.220f;
    ConfigLoad(primary.c_str());
    Check(g_config.virtual_stock && g_config.virtual_stock_standard_strength == 0.93f &&
              g_config.virtual_stock_plus_strength == 0.75f && g_config.virtual_stock_rear_height_m == -0.05f,
        "both mode strengths persist independently through a config round trip");
    g_config.virtual_stock = false;
    g_config.virtual_stock_standard_strength = kVirtualStockStandardStrengthDefault;
    g_config.virtual_stock_plus_strength = kVirtualStockPlusStrengthDefault;
    g_config.virtual_stock_rear_height_m = -0.220f;
    ConfigSave();
    g_config.virtual_stock_rear_reference = 1;
    g_config.virtual_stock_shoulder_back_m = 0.135f;
    g_config.virtual_stock_shoulder_side_m = 0.175f;
    ConfigSave();
    g_config.virtual_stock_rear_reference = 0;
    g_config.virtual_stock_shoulder_back_m = 0.005f;
    g_config.virtual_stock_shoulder_side_m = 0.015f;
    ConfigLoad(primary.c_str());
    Check(g_config.virtual_stock_rear_reference == 1 && g_config.virtual_stock_shoulder_back_m == 0.135f &&
              g_config.virtual_stock_shoulder_side_m == 0.175f,
        "Shoulder selector and offsets persist through a config round trip");
    g_config.virtual_stock_rear_reference = 0;
    g_config.virtual_stock_shoulder_back_m = 0.005f;
    g_config.virtual_stock_shoulder_side_m = 0.015f;
    ConfigSave();
    CheckUnit(123);
    {
        std::ofstream legacyChest(primary);
        legacyChest << "virtual_stock_rear_reference = 2\n";
        legacyChest << "virtual_stock_chest_height_m = -0.410\n";
        legacyChest << "virtual_stock_chest_back_m = 0.135\n";
        legacyChest << "virtual_stock_chest_side_m = 0.175\n";
    }
    CheckUnit(124);
    ConfigLoad(primary.c_str());
    Check(g_config.virtual_stock_rear_reference == 0 && g_config.virtual_stock_chest_height_m == -0.410f &&
              g_config.virtual_stock_chest_back_m == 0.135f && g_config.virtual_stock_chest_side_m == 0.175f,
        "legacy Chest sanitises to Standard Centre while retaining dormant settings");
    g_config.virtual_stock_rear_reference = 0;
    g_config.virtual_stock_chest_height_m = -0.320f;
    g_config.virtual_stock_chest_back_m = 0.000f;
    g_config.virtual_stock_chest_side_m = 0.015f;
    ConfigSave();
    g_config.virtual_stock_rear_reference = 3;
    ConfigSave();
    g_config.virtual_stock_rear_reference = 0;
    ConfigLoad(primary.c_str());
    Check(g_config.virtual_stock_rear_reference == 3,
        "Hybrid selector (numeric mode 3) persists through a config round trip");
    CheckUnit(125);
    for (const int reference : {0, 1, 3})
    {
        std::ofstream explicitReference(primary);
        explicitReference << "virtual_stock_rear_reference = " << reference << "\n";
        explicitReference.close();
        ConfigLoad(primary.c_str());
        Check(g_config.virtual_stock_rear_reference == reference,
            "explicit product rear-reference selections load unchanged");
    }
    CheckUnit(126);
    {
        std::ofstream malformedProduct(primary);
        malformedProduct << "virtual_stock_rear_reference = garbage\n";
        malformedProduct << "virtual_stock_hybrid_horizontal_release = garbage\n";
        malformedProduct << "two_hand_support_grip_pose = garbage\n";
    }
    CheckUnit(127);
    ConfigLoad(primary.c_str());
    Check(g_config.virtual_stock_rear_reference == kVirtualStockProductRearReferenceDefault &&
              g_config.virtual_stock_hybrid_horizontal_release && g_config.two_hand_support_grip_pose,
        "malformed product selectors retain their new-config defaults");
    g_config.virtual_stock_rear_reference = 0;
    ConfigSave();
    g_config.virtual_stock_adaptive_top_height_m = -0.210f;
    g_config.virtual_stock_adaptive_bottom_height_m = -0.510f;
    g_config.virtual_stock_adaptive_top_half_width_m = 0.095f;
    g_config.virtual_stock_adaptive_bottom_half_width_m = 0.185f;
    ConfigSave();
    g_config.virtual_stock_adaptive_top_height_m = -0.180f;
    g_config.virtual_stock_adaptive_bottom_height_m = -0.450f;
    g_config.virtual_stock_adaptive_top_half_width_m = 0.080f;
    g_config.virtual_stock_adaptive_bottom_half_width_m = 0.140f;
    ConfigLoad(primary.c_str());
    Check(g_config.virtual_stock_adaptive_top_height_m == -0.210f &&
              g_config.virtual_stock_adaptive_bottom_height_m == -0.510f &&
              g_config.virtual_stock_adaptive_top_half_width_m == 0.095f &&
              g_config.virtual_stock_adaptive_bottom_half_width_m == 0.185f,
        "Adaptive coverage settings persist through a config round trip");
    g_config.virtual_stock_adaptive_top_height_m = -0.180f;
    g_config.virtual_stock_adaptive_bottom_height_m = -0.450f;
    g_config.virtual_stock_adaptive_top_half_width_m = 0.080f;
    g_config.virtual_stock_adaptive_bottom_half_width_m = 0.140f;
    ConfigSave();
    g_config.virtual_stock_hybrid_offhand_influence = 0.75f;
    g_config.virtual_stock_hybrid_ads_reference = 1;
    g_config.virtual_stock_hybrid_seat_full_m = 0.080f;
    g_config.virtual_stock_hybrid_seat_release_m = 0.220f;
    ConfigSave();
    g_config.virtual_stock_hybrid_offhand_influence = 0.50f;
    g_config.virtual_stock_hybrid_ads_reference = 0;
    g_config.virtual_stock_hybrid_seat_full_m = kVirtualStockProductHybridSeatFullDefaultM;
    g_config.virtual_stock_hybrid_seat_release_m = kVirtualStockProductHybridSeatReleaseDefaultM;
    ConfigLoad(primary.c_str());
    Check(g_config.virtual_stock_hybrid_offhand_influence == 0.75f &&
              g_config.virtual_stock_hybrid_ads_reference == 1 && g_config.virtual_stock_hybrid_seat_full_m == 0.080f &&
              g_config.virtual_stock_hybrid_seat_release_m == 0.220f,
        "Hybrid authority, ADS reference, and seating thresholds persist through a config round trip");
    g_config.virtual_stock_hybrid_offhand_influence = 0.50f;
    g_config.virtual_stock_hybrid_ads_reference = 0;
    g_config.virtual_stock_hybrid_seat_full_m = kVirtualStockProductHybridSeatFullDefaultM;
    g_config.virtual_stock_hybrid_seat_release_m = kVirtualStockProductHybridSeatReleaseDefaultM;
    ConfigSave();
    CheckUnit(128);
    {
        std::ofstream malformed(primary);
        malformed << "virtual_stock_hybrid_offhand_influence = nan\n";
        malformed << "virtual_stock_hybrid_ads_reference = 7\n";
        malformed << "virtual_stock_hybrid_seat_full_m = inf\n";
        malformed << "virtual_stock_hybrid_seat_release_m = 0.055\n";
    }
    CheckUnit(129);
    ConfigLoad(primary.c_str());
    Check(g_config.virtual_stock_hybrid_offhand_influence == 0.50f &&
              g_config.virtual_stock_hybrid_ads_reference == 0 &&
              g_config.virtual_stock_hybrid_seat_full_m == kVirtualStockProductHybridSeatFullDefaultM &&
              g_config.virtual_stock_hybrid_seat_release_m == kVirtualStockProductHybridSeatReleaseDefaultM,
        "Malformed Hybrid values and an invalid threshold pair normalize to safe defaults");
    CheckUnit(130);
    {
        std::ofstream boundary(primary);
        boundary << "virtual_stock_hybrid_seat_full_m = 0.050\n";
        boundary << "virtual_stock_hybrid_seat_release_m = 0.060\n";
    }
    CheckUnit(131);
    ConfigLoad(primary.c_str());
    Check(g_config.virtual_stock_hybrid_seat_full_m == 0.050f && g_config.virtual_stock_hybrid_seat_release_m == 0.060f,
        "Hybrid accepts the exact 0.010 metre seating-threshold separation");
    ConfigSave();
    const std::string boundaryConfig = ReadTextFile(primary);
    Check(CountText(boundaryConfig, "\nvirtual_stock_hybrid_seat_full_m = 0.050") == 1 &&
              CountText(boundaryConfig, "\nvirtual_stock_hybrid_seat_release_m = 0.060") == 1,
        "Hybrid preserves the exact 0.050/0.060 seating pair when persisted");
    ConfigLoad(primary.c_str());
    Check(g_config.virtual_stock_hybrid_seat_full_m == 0.050f && g_config.virtual_stock_hybrid_seat_release_m == 0.060f,
        "Hybrid retains the exact 0.050/0.060 seating pair after reload");
    g_config.virtual_stock_hybrid_offhand_influence = 2.0f;
    g_config.virtual_stock_hybrid_ads_reference = -1;
    g_config.virtual_stock_hybrid_seat_full_m = 0.0f;
    g_config.virtual_stock_hybrid_seat_release_m = 1.0f;
    ConfigSave();
    ConfigLoad(primary.c_str());
    Check(g_config.virtual_stock_hybrid_offhand_influence == 1.0f && g_config.virtual_stock_hybrid_ads_reference == 0 &&
              g_config.virtual_stock_hybrid_seat_full_m == 0.010f &&
              g_config.virtual_stock_hybrid_seat_release_m == 0.800f,
        "Hybrid values clamp to the documented 0.010 metre minimum and conservative maxima");
    g_config.virtual_stock_hybrid_offhand_influence = 0.50f;
    g_config.virtual_stock_hybrid_ads_reference = 0;
    g_config.virtual_stock_hybrid_seat_full_m = kVirtualStockProductHybridSeatFullDefaultM;
    g_config.virtual_stock_hybrid_seat_release_m = kVirtualStockProductHybridSeatReleaseDefaultM;
    ConfigSave();
    g_config.virtual_stock_hybrid_horizontal_release = false;
    g_config.virtual_stock_hybrid_horizontal_full_m = 0.31f;
    g_config.virtual_stock_hybrid_horizontal_release_m = 0.57f;
    ConfigSave();
    g_config.virtual_stock_hybrid_horizontal_release = true;
    g_config.virtual_stock_hybrid_horizontal_full_m = 0.330f;
    g_config.virtual_stock_hybrid_horizontal_release_m = 0.475f;
    ConfigLoad(primary.c_str());
    Check(!g_config.virtual_stock_hybrid_horizontal_release &&
              g_config.virtual_stock_hybrid_horizontal_full_m == 0.31f &&
              g_config.virtual_stock_hybrid_horizontal_release_m == 0.57f,
        "Plus horizontal unloading persists through the normal config path");
    CheckUnit(132);
    {
        std::ofstream invalidHorizontal(primary);
        invalidHorizontal << "virtual_stock_hybrid_horizontal_full_m = 0.50\n";
        invalidHorizontal << "virtual_stock_hybrid_horizontal_release_m = 0.40\n";
    }
    CheckUnit(133);
    ConfigLoad(primary.c_str());
    Check(g_config.virtual_stock_hybrid_horizontal_full_m == 0.330f &&
              g_config.virtual_stock_hybrid_horizontal_release_m == 0.475f,
        "invalid Plus horizontal unloading pairs normalise to product defaults");
    ConfigSave();
    g_config.virtual_stock_proximity_release = true;
    g_config.virtual_stock_proximity_full_m = 0.31f;
    g_config.virtual_stock_proximity_release_m = 0.57f;
    ConfigSave();
    g_config.virtual_stock_proximity_release = true;
    g_config.virtual_stock_proximity_full_m = 0.270f;
    g_config.virtual_stock_proximity_release_m = 0.425f;
    ConfigLoad(primary.c_str());
    Check(g_config.virtual_stock_proximity_release && g_config.virtual_stock_proximity_full_m == 0.31f &&
              g_config.virtual_stock_proximity_release_m == 0.57f,
        "proximity release and both distances persist through a config round trip");
    CheckUnit(134);
    {
        // Retired key inertness: an existing cfg carrying the old key must
        // load without re-enabling inverse-neck, and must never be re-emitted.
        std::ofstream retiredSway(primary);
        retiredSway << "virtual_stock_head_turn_sway_correction = 1\n";
    }
    CheckUnit(135);
    ConfigLoad(primary.c_str());
    CheckUnit(136);
    {
        const VirtualStockAimSettings retiredKeySettings =
            VirtualStockAimSettingsFromConfig(g_config, HybridDiagnosticOverride::Normal);
        Check(!retiredKeySettings.hybridInverseNeckEnabled &&
                  !VirtualStockHeadTurnCorrectionFamilyActive(retiredKeySettings),
            "a legacy sway correction config key cannot re-enable inverse-neck");
    }
    CheckUnit(137);
    ConfigSave();
    Check(ReadTextFile(primary).find("virtual_stock_head_turn_sway_correction") == std::string::npos,
        "the retired sway correction key is never re-emitted by ConfigSave");
    g_config.virtual_stock_proximity_release = true;
    g_config.virtual_stock_proximity_full_m = 0.270f;
    g_config.virtual_stock_proximity_release_m = 0.425f;
    ConfigSave();
    CheckUnit(138);
    {
        const std::string profileConfigBefore = ReadTextFile(primary);
        const VirtualStockAimSettings userSettingsBefore =
            VirtualStockAimSettingsFromConfig(g_config, HybridDiagnosticOverride::Normal);
        const float headsetSmoothingBefore = g_config.headset_smoothing;
        VirtualStockTestProfileState profileState;
        profileState.Store(static_cast<VirtualStockTestProfile>(kVirtualStockExperimentProfileFirstId + 1));
        const VirtualStockAimSettings effective =
            ResolveVirtualStockTestProfile(profileState.Load(), userSettingsBefore);
        Check(effective.virtualStockEnabled && effective.virtualStockRearReference == 3 &&
                  effective.virtualStockStrength == 0.80f && effective.hybridInverseNeckEnabled &&
                  effective.hybridInverseNeckStrength == 1.0f,
            "Runtime profile resolution produces effective settings without assigning Config fields");
        const VirtualStockAimSettings userSettingsAfterResolve =
            VirtualStockAimSettingsFromConfig(g_config, HybridDiagnosticOverride::Normal);
        Check(
            userSettingsAfterResolve.virtualStockEnabled == userSettingsBefore.virtualStockEnabled &&
                userSettingsAfterResolve.virtualStockStrength == userSettingsBefore.virtualStockStrength &&
                userSettingsAfterResolve.virtualStockRearHeightM == userSettingsBefore.virtualStockRearHeightM &&
                userSettingsAfterResolve.virtualStockRearReference == userSettingsBefore.virtualStockRearReference &&
                userSettingsAfterResolve.virtualStockHybridOffhandInfluence ==
                    userSettingsBefore.virtualStockHybridOffhandInfluence &&
                userSettingsAfterResolve.virtualStockHybridSeatFullM ==
                    userSettingsBefore.virtualStockHybridSeatFullM &&
                userSettingsAfterResolve.virtualStockHybridSeatReleaseM ==
                    userSettingsBefore.virtualStockHybridSeatReleaseM &&
                userSettingsAfterResolve.hybridHorizontalRearReleaseEnabled ==
                    userSettingsBefore.hybridHorizontalRearReleaseEnabled &&
                userSettingsAfterResolve.hybridHorizontalRearReleaseFullM ==
                    userSettingsBefore.hybridHorizontalRearReleaseFullM &&
                userSettingsAfterResolve.hybridHorizontalRearReleaseReleaseM ==
                    userSettingsBefore.hybridHorizontalRearReleaseReleaseM &&
                userSettingsAfterResolve.virtualStockProximityRelease ==
                    userSettingsBefore.virtualStockProximityRelease &&
                userSettingsAfterResolve.virtualStockProximityFullM == userSettingsBefore.virtualStockProximityFullM &&
                userSettingsAfterResolve.virtualStockProximityReleaseM ==
                    userSettingsBefore.virtualStockProximityReleaseM &&
                g_config.headset_smoothing == headsetSmoothingBefore,
            "Selecting a runtime profile leaves the live Config values untouched");
        ConfigSave();
        Check(ReadTextFile(primary) == profileConfigBefore,
            "Runtime profile selection leaves ConfigSave output unchanged");
    }
    CheckUnit(139);
    Check(Config{}.two_hand_support_grip_pose && CountText(organizedConfig, "\ntwo_hand_support_grip_pose = 1") == 1,
        "support-hand rotation reduction defaults on in generated configurations");
    Check(Config{}.persistent_support_grip &&
              Config{}.two_hand_smoothing_strength == kTwoHandSmoothingStrengthDefault &&
              Config{}.two_hand_offhand_influence == kTwoHandOffhandInfluenceDefault &&
              kTwoHandOffhandInfluenceDefault == 0.50f && kTwoHandOffhandInfluenceMinimum == 0.0f &&
              kTwoHandOffhandInfluenceMaximum == 1.0f &&
              CountText(organizedConfig, "\npersistent_support_grip = 1") == 1 &&
              CountText(organizedConfig, "\ntwo_hand_smoothing_strength = 0.00") == 1 &&
              CountText(organizedConfig, "\ntwo_hand_offhand_influence = 0.50") == 1,
        "PG defaults on, the Two-Hand Smoothing strength slider defaults off at 0, and the free two-hand offhand influence defaults to half authority");
    Check(TwoHandTransitionContinuityEnabled() && TwoHandTransitionContinuityAppliesToFrame(true) &&
              !TwoHandTransitionContinuityAppliesToFrame(false) &&
              CountText(organizedConfig, "\ntwo_hand_transition_smoothing =") == 0,
        "the fixed 200 ms VS-OFF latch continuity always resolves enabled with no control, so the retired key is never written");
    Check(TwoHandLabTemporalEngagedFor(true, false, false) && !TwoHandLabTemporalEngagedFor(true, false, true) &&
              !TwoHandLabTemporalEngagedFor(true, true, false) && !TwoHandLabTemporalEngagedFor(false, false, false),
        "the Lab temporal correction engages only when Virtual Stock is off and the fixed product continuity does not own the frame, so the two 200 ms corrections can never stack");
    CheckUnit(140);
    {
        std::ofstream settings(primary);
        settings << "persistent_support_grip = 0\n"
                    "two_hand_smoothing = 1\n"
                    "two_hand_transition_smoothing = 0\n";
    }
    CheckUnit(141);
    ConfigLoad(primary.c_str());
    Check(!g_config.persistent_support_grip &&
              g_config.two_hand_smoothing_strength == kTwoHandSmoothingStrengthMaximum &&
              !g_config.two_hand_transition_smoothing && TwoHandTransitionContinuityEnabled(),
        "the legacy boolean two_hand_smoothing = 1 migrates to the full 25 strength, never slider value 1; a historical two_hand_transition_smoothing = 0 is parsed into the dormant field and can never disable the fixed 200 ms continuity");
    ConfigSave();
    const std::string twoHandConfig = ReadTextFile(primary);
    Check(CountText(twoHandConfig, "\npersistent_support_grip = 0") == 1 &&
              CountText(twoHandConfig, "\ntwo_hand_smoothing_strength = 25.00") == 1 &&
              CountText(twoHandConfig, "\ntwo_hand_transition_smoothing =") == 0 &&
              CountText(twoHandConfig, "\ntwo_hand_smoothing =") == 0,
        "the numeric strength is the only smoothing key saved; the legacy boolean and the retired transition key are not re-saved");
    CheckUnit(142);
    {
        std::ofstream settings(primary);
        settings << "two_hand_smoothing = 0\n";
    }
    CheckUnit(143);
    ConfigLoad(primary.c_str());
    Check(g_config.two_hand_smoothing_strength == 0.0f,
        "the legacy boolean two_hand_smoothing = 0 migrates to strength 0");
    CheckUnit(144);
    {
        std::ofstream settings(primary);
        settings << "two_hand_smoothing = 1\n"
                    "two_hand_smoothing_strength = 12.50\n";
    }
    CheckUnit(145);
    ConfigLoad(primary.c_str());
    Check(
        g_config.two_hand_smoothing_strength == 12.5f, "an explicit numeric strength wins over the legacy boolean key");
    ConfigSave();
    const std::string strengthConfig = ReadTextFile(primary);
    Check(CountText(strengthConfig, "\ntwo_hand_smoothing_strength = 12.50") == 1,
        "an intermediate strength round-trips through the canonical numeric key");
    CheckUnit(146);
    {
        std::ofstream settings(primary);
        settings << "two_hand_smoothing_strength = 100.00\n";
    }
    CheckUnit(147);
    ConfigLoad(primary.c_str());
    Check(g_config.two_hand_smoothing_strength == kTwoHandSmoothingStrengthMaximum,
        "an out-of-range high strength clamps to 25");
    CheckUnit(148);
    {
        std::ofstream settings(primary);
        settings << "two_hand_smoothing_strength = -4\n";
    }
    CheckUnit(149);
    ConfigLoad(primary.c_str());
    Check(g_config.two_hand_smoothing_strength == kTwoHandSmoothingStrengthMinimum,
        "an out-of-range negative strength clamps to 0");
    CheckUnit(150);
    {
        std::ofstream settings(primary);
        settings << "two_hand_smoothing_strength = not-a-number\n"
                    "persistent_support_grip = 0\n";
    }
    CheckUnit(151);
    ConfigLoad(primary.c_str());
    Check(g_config.two_hand_smoothing_strength == kTwoHandSmoothingStrengthDefault,
        "a malformed strength is ignored and keeps the 0 default");
    CheckUnit(152);
    {
        std::ofstream settings(primary);
        settings << "two_hand_smoothing_strength = 7.25\n";
    }
    CheckUnit(153);
    ConfigLoad(primary.c_str());
    Check(g_config.two_hand_smoothing_strength == 7.25f, "an in-range fractional strength loads unchanged");
    CheckUnit(154);
    {
        std::ofstream settings(primary);
        settings << "two_hand_offhand_influence = 0.75\n";
    }
    CheckUnit(155);
    ConfigLoad(primary.c_str());
    Check(g_config.two_hand_offhand_influence == 0.75f, "an in-range free two-hand offhand influence loads unchanged");
    ConfigSave();
    Check(CountText(ReadTextFile(primary), "\ntwo_hand_offhand_influence = 0.75") == 1,
        "the offhand influence round-trips through the canonical two-decimal key");
    CheckUnit(156);
    {
        std::ofstream settings(primary);
        settings << "two_hand_offhand_influence = 1.75\n";
    }
    CheckUnit(157);
    ConfigLoad(primary.c_str());
    Check(g_config.two_hand_offhand_influence == kTwoHandOffhandInfluenceMaximum,
        "an out-of-range high offhand influence clamps to 1");
    CheckUnit(158);
    {
        std::ofstream settings(primary);
        settings << "two_hand_offhand_influence = -0.4\n";
    }
    CheckUnit(159);
    ConfigLoad(primary.c_str());
    Check(g_config.two_hand_offhand_influence == kTwoHandOffhandInfluenceMinimum,
        "an out-of-range negative offhand influence clamps to 0");
    CheckUnit(160);
    {
        std::ofstream settings(primary);
        settings << "two_hand_offhand_influence = not-a-number\n";
    }
    CheckUnit(161);
    ConfigLoad(primary.c_str());
    Check(g_config.two_hand_offhand_influence == kTwoHandOffhandInfluenceDefault,
        "a malformed offhand influence is ignored and keeps the 0.5 default");
    ConfigSave();
    Check(CountText(ReadTextFile(primary), "\ntwo_hand_offhand_influence = 0.50") == 1,
        "the clamped offhand influence default round-trips through ConfigSave");
    CheckUnit(162);
    {
        std::ofstream legacySettings(primary);
        legacySettings << "two_hand_support_grip_pose = 1\n";
    }
    CheckUnit(163);
    ConfigLoad(primary.c_str());
    Check(g_config.persistent_support_grip &&
              g_config.two_hand_smoothing_strength == kTwoHandSmoothingStrengthDefault &&
              TwoHandTransitionContinuityEnabled(),
        "older configs with no new keys use defaults and still resolve the fixed continuity without migrating saved settings");
    g_config.two_hand_smoothing_strength = 0.0f;
    ConfigSave();
    ConfigSave();
    g_config.two_hand_support_grip_pose = false;
    ConfigSave();
    g_config.two_hand_support_grip_pose = true;
    ConfigLoad(primary.c_str());
    Check(!g_config.two_hand_support_grip_pose,
        "support grip-pose aiming experiment persists through a config round trip");
    g_config.two_hand_support_grip_pose = true;
    ConfigSave();
    Check(!Config{}.ce_anniversary_disable_lens_flares, "CE flare suppression is opt-in");
    g_config.ce_anniversary_disable_lens_flares = true;
    ConfigSave();
    g_config.ce_anniversary_disable_lens_flares = false;
    ConfigLoad(primary.c_str());
    Check(g_config.ce_anniversary_disable_lens_flares, "CE flare toggle persists enabled");
    g_config.ce_anniversary_disable_lens_flares = false;
    ConfigSave();
    g_config.ce_anniversary_disable_lens_flares = true;
    ConfigLoad(primary.c_str());
    Check(!g_config.ce_anniversary_disable_lens_flares, "CE flare toggle persists disabled");
    const size_t openXrSection = organizedConfig.find("#  OPENXR & COMFORT");
    const size_t controlsSection = organizedConfig.find("#  CONTROLS & TURNING");
    const size_t aimingSection = organizedConfig.find("#  RETICLE & AIMING");
    const size_t weaponSection = organizedConfig.find("#  WEAPON CALIBRATION");
    const size_t scopeSection = organizedConfig.find("#  EXPERIMENTAL SCOPE");
    const size_t displaySection = organizedConfig.find("#  HUD, PRESENTATION & PERFORMANCE");
    const size_t handsSection = organizedConfig.find("#  GAMEPLAY, HANDS & IK");
    const size_t diagnosticsSection = organizedConfig.find("#  DEVELOPMENT DIAGNOSTICS");
    Check(openXrSection < controlsSection && controlsSection < aimingSection && aimingSection < weaponSection &&
              weaponSection < scopeSection && scopeSection < displaySection && displaySection < handsSection &&
              handsSection < diagnosticsSection,
        "The generated universal config has stable, readable section ordering");
    Check(organizedConfig.find("This ONE file is shared by every supported MCC game") != std::string::npos,
        "The generated config explains that preferences are shared across titles");
    Check(CountText(organizedConfig, "\nhalo2_classic_gun_pitch_deg = ") == 1 &&
              CountText(organizedConfig, "\nhalo2_classic_gun_yaw_deg = ") == 1,
        "The generated config persists the two Halo 2 Classic alignment sliders");

    CheckUnit(164);
    {
        std::ofstream file(primary);
        file << "config_version = 5\n";
        file << "h2_classic_gun_yaw_deg = 7.5\n";
        file << "h2_classic_gun_pitch_deg = -4.25\n";
    }
    CheckUnit(165);
    ConfigLoad(primary.c_str());
    Check(g_config.halo2_classic_gun_yaw_deg == 7.5f && g_config.halo2_classic_gun_pitch_deg == -4.25f,
        "Stage 3N/V5 Halo 2 Classic alignment aliases load into the current fields");
    ConfigSave();
    const std::string halo2AlignmentConfig = ReadTextFile(primary);
    Check(CountText(halo2AlignmentConfig, "\nhalo2_classic_gun_yaw_deg = 7.50") == 1 &&
              CountText(halo2AlignmentConfig, "\nhalo2_classic_gun_pitch_deg = -4.25") == 1 &&
              halo2AlignmentConfig.find("\nh2_classic_gun_yaw_deg = ") == std::string::npos &&
              halo2AlignmentConfig.find("\nh2_classic_gun_pitch_deg = ") == std::string::npos,
        "Saving migrates the two V5 aliases to one canonical assignment each");
    CheckUnit(166);
    // The one universal file
    // carries a per-title profile section for the thirteen weapon/hand/HUD
    // tunables - every game, Halo 2's two renderers separately, and a
    // reserved Halo 1 slot. This replaces the older no-title-prefix rule
    // for exactly these keys; everything else stays shared.
    for (const char* profilePrefix : {"\nhalo3_gun_scale = ", "\nodst_gun_scale = ", "\nreach_gun_scale = ",
             "\nhalo4_gun_scale = ", "\nhalo2a_gun_scale = ", "\nhalo2c_gun_scale = ", "\nhalo1_gun_scale = ",
             "\nhalo2a_hud_size = ", "\nhalo2c_hud_size = ", "\nhalo3_left_hand_forward_m = ",
             "\nhalo2c_barrel_pitch_deg = ", "\nhalo3_barrel_roll_deg = "})
    {
        Check(organizedConfig.find(profilePrefix) != std::string::npos,
            "The universal config carries every per-title tunable profile");
    }
    CheckUnit(168);
    {
        // Round trip: a Halo 2 Classic value persists independently and
        // switching profiles swaps the live tunables.
        g_config.title_profiles[5].gun_pitch_deg = -21.0f;
        g_config.title_profiles[4].gun_pitch_deg = 7.0f;
        ConfigSave();
        g_config.title_profiles[5].gun_pitch_deg = 0.0f;
        g_config.title_profiles[4].gun_pitch_deg = 0.0f;
        ConfigLoad(primary.c_str());
        Check(g_config.title_profiles[5].gun_pitch_deg == -21.0f && g_config.title_profiles[4].gun_pitch_deg == 7.0f,
            "Per-title tunables survive a save/load round trip "
            "independently for Halo 2's two renderers");
        const float sharedPitch = g_config.base_tunables.gun_pitch_deg;
        Config_ApplyTitleProfile(5);
        Check(g_config.gun_pitch_deg == -21.0f, "Applying the Halo 2 Classic profile swaps the live weapon "
                                                "tunables");
        Config_ApplyTitleProfile(4);
        Check(g_config.gun_pitch_deg == 7.0f, "Applying the Halo 2 Anniversary profile swaps them again");
        g_config.gun_pitch_deg = -33.0f;
        Config_StoreLiveTunables();
        Check(g_config.title_profiles[4].gun_pitch_deg == -33.0f && g_config.title_profiles[5].gun_pitch_deg == -21.0f,
            "A live edit lands in the active profile only");
        Config_ApplyTitleProfile(-1);
        Check(g_config.gun_pitch_deg == sharedPitch, "Leaving every title restores the shared defaults");
    }
    CheckUnit(169);
    {
        // Real save/load, swap, disable and graphics-mode transitions. A gun
        // overlay must never leak into the defaults or carry HUD edits with it.
        constexpr GameTitle titles[]{GameTitle::Halo3, GameTitle::Halo3ODST, GameTitle::HaloReach, GameTitle::Halo4,
            GameTitle::Halo2, GameTitle::Halo2, GameTitle::HaloCE};
        for (int title = 0; title < kTitleProfileCount; ++title)
        {
            uint64_t guns[2]{};
            int count = 0;
            for (const auto& model : weapon_model::kModels)
                if (model.title == titles[title] && count < 2)
                    guns[count++] = model.identity;
            Check(count == 2, "each title has independently identified alignment fixtures");
            Config_ApplyTitleProfile(title);
            g_config.per_gun_alignment = false;
            Config_ApplyWeaponProfile(guns[0]);
            g_config.gun_forward_m = -.123f;
            g_config.barrel_roll_deg = 3.f;
            g_config.halo2_classic_gun_pitch_deg = -9.5f;
            g_config.muzzle_height_m = .03f;
            Config_StoreLiveTunables();
            g_config.per_gun_alignment = true;
            Config_RefreshWeaponProfile();
            Check(Config_ActiveWeaponProfileName() != nullptr && g_config.gun_forward_m == -.123f,
                "first gun inherits title calibration when enabled");
            g_config.gun_forward_m = .231234f;
            g_config.barrel_roll_deg = 37.f;
            g_config.halo2_classic_gun_pitch_deg = 12.f;
            g_config.muzzle_height_m = .12f;
            g_config.hud_size = .71f;
            ConfigSave();
            Check(g_config.title_profiles[title].gun_forward_m == -.123f &&
                      g_config.title_profiles[title].hud_size == .71f,
                "saving gun alignment isolates title defaults while preserving HUD edits");
            Config_ApplyWeaponProfile(guns[1]);
            Check(g_config.gun_forward_m == -.123f && g_config.barrel_roll_deg == 3.f &&
                      g_config.halo2_classic_gun_pitch_deg == -9.5f && g_config.muzzle_height_m == .03f,
                "second gun starts from fallback including Classic and muzzle trims");
            g_config.gun_forward_m = -.211f;
            Config_ApplyWeaponProfile(guns[0]);
            Check(std::abs(g_config.gun_forward_m - .231234f) < 1e-6f && g_config.barrel_roll_deg == 37.f &&
                      g_config.halo2_classic_gun_pitch_deg == 12.f,
                "switching back restores the first gun exactly");
            ConfigSave();
            ConfigLoad(primary.c_str());
            Config_ApplyWeaponProfile(guns[0]);
            Check(g_config.per_gun_alignment && std::abs(g_config.gun_forward_m - .231234f) < 1e-6f &&
                      g_config.halo2_classic_gun_pitch_deg == 12.f && g_config.muzzle_height_m == .12f,
                "stable title and weapon identity survives a full config round trip");
            Config_ApplyWeaponProfile(guns[1]);
            Check(std::abs(g_config.gun_forward_m + .211f) < 1e-6f, "second gun survives the same round trip");
            Config_ApplyWeaponProfile(0x8000000000000042ull);
            Check(!Config_ActiveWeaponProfileName() && g_config.gun_forward_m == -.123f,
                "unfamiliar or transient native handles never acquire another gun's profile");
            Config_ApplyWeaponProfile(guns[0]);
            g_config.per_gun_alignment = false;
            Config_RefreshWeaponProfile();
            Check(!Config_ActiveWeaponProfileName() && g_config.gun_forward_m == -.123f &&
                      g_config.halo2_classic_gun_pitch_deg == -9.5f && g_config.muzzle_height_m == .03f,
                "disabling restores all fallback alignment fields without losing gun overrides");
        }
        Config_ApplyTitleProfile(-1);
        ConfigLoad(primary.c_str());
        g_config.per_gun_alignment = false;
        Config_RefreshWeaponProfile();
    }
    CheckUnit(170);
    {
        // The render thread applies the held gun's profile before the
        // frame that gates the scope glass on it (VR_UpdateWeaponAlignment just
        // before SubmitPreparedFrame; tools/check_weapon_profile_thread.py pins
        // that order). So a swap passes the lens gate on the same frame: before
        // the apply the gate waits, after it the gate opens.
        uint64_t guns[2]{};
        int count = 0;
        for (const auto& model : weapon_model::kModels)
            if (model.title == GameTitle::Halo3 && count < 2)
                guns[count++] = model.identity;
        Check(count == 2, "z69 #21: two Halo 3 guns to swap between");
        Config_ApplyTitleProfile(0);
        for (const bool perGun : {true, false})
        {
            g_config.per_gun_alignment = perGun;
            Config_ApplyWeaponProfile(guns[0]);
            Check(scope_window::LensProfileReady(guns[0], Config_AppliedWeaponIdentity()) &&
                      !scope_window::LensProfileReady(guns[1], Config_AppliedWeaponIdentity()),
                "z69 #21: a swapped-in gun waits at the lens gate until its profile is applied");
            uint64_t waited = 99;
            Config_ApplyWeaponProfileTimed(guns[1], waited);
            Check(waited == 0 && Config_AppliedWeaponIdentity() == guns[1] &&
                      scope_window::LensProfileReady(guns[1], Config_AppliedWeaponIdentity()),
                "z69 #21: the same frame's lens gate opens after the render-thread apply; an uncontended apply reports no wait");
        }
        {
            // A contended apply reports a non-zero wait: another thread holds the
            // profile lock (here, a second render-thread apply racing this one).
            std::atomic<bool> go{false};
            uint64_t a = 0, b = 0;
            std::thread first(
                [&]
                {
                    while (!go.load())
                    {
                    }
                    for (int i = 0; i < 2000; ++i)
                    {
                        uint64_t w = 0;
                        Config_ApplyWeaponProfileTimed(guns[i & 1], w);
                        a = std::max(a, w);
                    }
                });
            std::thread second(
                [&]
                {
                    while (!go.load())
                    {
                    }
                    for (int i = 0; i < 2000; ++i)
                    {
                        uint64_t w = 0;
                        Config_ApplyWeaponProfileTimed(guns[(i + 1) & 1], w);
                        b = std::max(b, w);
                    }
                });
            go = true;
            first.join();
            second.join();
            Check(Config_AppliedWeaponIdentity() == guns[0] || Config_AppliedWeaponIdentity() == guns[1],
                "z69 #21: racing applies leave one of the two guns applied");
            std::printf("z69 #21: contended apply, longest measured wait %llu us\n",
                static_cast<unsigned long long>(std::max(a, b)));
        }
        Config_ApplyTitleProfile(-1);
        g_config.per_gun_alignment = false;
        Config_RefreshWeaponProfile();
    }
    CheckUnit(171);
    {
        // Lens rows apply to the held weapon with per-gun
        // tuning off; the player's own scope_screen_* values stay global.
        Config_ApplyTitleProfile(0);
        g_config.per_gun_alignment = false;
        const weapon_model::Model* lensGun = nullptr;
        weapon_profile::ScopeLensRow row{};
        bool cls = false;
        for (const auto& m : weapon_model::kModels)
            if (m.title == GameTitle::Halo3 && weapon_profile::ScopeLensPlacement(&m, row, cls))
            {
                lensGun = &m;
                break;
            }
        Check(lensGun != nullptr, "an H3 weapon has a scope-glass lens row");
        if (lensGun)
        {
            Config_ApplyWeaponProfile(0);
            const float globalWidth = g_config.scope_screen_width_m, globalUp = g_config.scope_screen_up_m;
            Config_ApplyWeaponProfile(lensGun->identity);
            Check(std::abs(g_config.scope_screen_width_m - std::clamp(row.width, .02f, .25f)) < 1e-6f &&
                      !Config_ActiveWeaponProfileName(),
                "per-gun off: the held weapon's lens row still applies");
            ConfigSave();
            Check(std::abs(g_config.scope_screen_width_m - std::clamp(row.width, .02f, .25f)) < 1e-6f,
                "saving keeps the row on the held weapon");
            ConfigLoad(primary.c_str());
            Check(g_config.scope_screen_width_m == globalWidth && g_config.scope_screen_up_m == globalUp,
                "the config file keeps the player's global lens values, not the row");
            Config_ApplyTitleProfile(0);
            g_config.per_gun_alignment = false;
            Config_ApplyWeaponProfile(lensGun->identity);
            g_config.scope_screen_up_m = globalUp + .013f; // an F1 edit while the gun is held
            Config_ApplyWeaponProfile(0);
            Check(g_config.scope_screen_width_m == globalWidth &&
                      std::abs(g_config.scope_screen_up_m - (globalUp + .013f)) < 1e-6f,
                "dropping the weapon restores the global lens values and keeps a live edit");
        }
        Config_ApplyTitleProfile(-1);
        ConfigLoad(primary.c_str());
    }
    CheckUnit(172);
    {
        // Per-gun on: an untouched lens row is not saved as a per-weapon value,
        // so it survives swaps and a config round trip; a moved field is saved.
        Config_ApplyTitleProfile(0);
        g_config.per_gun_alignment = true;
        const weapon_model::Model* lensGun = nullptr;
        const weapon_model::Model* other = nullptr;
        weapon_profile::ScopeLensRow row{};
        bool cls = false;
        for (const auto& m : weapon_model::kModels)
            if (m.title == GameTitle::Halo3)
            {
                weapon_profile::ScopeLensRow r{};
                if (!lensGun && weapon_profile::ScopeLensPlacement(&m, r, cls))
                {
                    lensGun = &m;
                    row = r;
                }
                else if (!other)
                    other = &m;
            }
        Check(lensGun && other, "two H3 fixtures for the per-gun lens swap");
        if (lensGun && other)
        {
            const float rowWidth = std::clamp(row.width, .02f, .25f);
            Config_ApplyWeaponProfile(lensGun->identity);
            g_config.gun_forward_m = .077f; // the player moves one field
            Config_ApplyWeaponProfile(other->identity);
            Config_ApplyWeaponProfile(lensGun->identity);
            Check(std::abs(g_config.scope_screen_width_m - rowWidth) < 1e-6f &&
                      std::abs(g_config.gun_forward_m - .077f) < 1e-6f,
                "per-gun on: the kit lens row survives a swap; the moved field is kept");
            ConfigSave();
            std::ifstream in(primary);
            std::string text((std::istreambuf_iterator<char>(in)), {});
            const std::string key = [&]
            {
                char b[96];
                snprintf(
                    b, sizeof b, "weapon_alignment_0_%016llX_", static_cast<unsigned long long>(lensGun->identity));
                return std::string(b);
            }();
            Check(text.find(key + "scope_screen_width_m") == std::string::npos &&
                      text.find(key + "gun_forward_m") != std::string::npos,
                "only the moved field is saved per weapon; the row is not frozen as tuned");
            ConfigLoad(primary.c_str());
            Config_ApplyTitleProfile(0);
            Config_ApplyWeaponProfile(lensGun->identity);
            Check(
                std::abs(g_config.scope_screen_width_m - rowWidth) < 1e-6f, "the row still applies after a round trip");
        }
        g_config.per_gun_alignment = false;
        Config_RefreshWeaponProfile();
        Config_ApplyTitleProfile(-1);
        ConfigLoad(primary.c_str());
    }
    CheckUnit(173);
    constexpr const char* universalKeys[] = {"config_version", "haptic_intensity", "headset_smoothing",
        "aim_stabilization", "screen_width_m", "screen_distance_m", "cutscene_theater_enabled",
        "cutscene_theater_depth", "cutscene_theater_flip_depth", "cutscene_theater_width_m",
        "cutscene_theater_distance_m", "turn_smooth", "vehicle_smooth_turn", "turn_snap_deg", "turn_smooth_deg_s",
        "y_b_start_chord", "dpad_hand", "dpad_head_radius", "quest_thumbrest_dpad", "vehicle_first_person",
        "vehicle_cam_forward_m", "vehicle_cam_up_m", "vehicle_cam_right_m", "vehicle_view_follow",
        "vehicle_cam_smoothing", "vehicle_motion", "vehicle_wheel_max_deg", "vehicle_wheel_deadzone_deg",
        "halo4_helmet", "crosshair", "crosshair_distance_m", "crosshair_size_deg", "reticle_r", "reticle_g",
        "reticle_b", "kill_reticle", "gun_scale", "left_hand_scale", "gun_pitch_deg", "gun_yaw_deg", "gun_roll_deg",
        "gun_forward_m", "muzzle_height_m", "scope_enabled", "scope_zoom", "scope_screen_width_m",
        "scope_screen_right_m", "scope_screen_up_m", "scope_screen_forward_m", "scope_refresh_divisor",
        "game_brightness", "resolution_scale", "upscale_filter", "sharpness", "aa_mode", "rain", "atmospheric_fog",
        "hud_size", "hud_aspect", "hud_curvature", "hud_vertical_offset", "motion_blur", "auto_vr", "left_handed",
        "two_handed_aim", "virtual_stock", "virtual_stock_standard_strength", "virtual_stock_plus_strength",
        "virtual_stock_rear_height_m", "virtual_stock_rear_reference", "virtual_stock_shoulder_back_m",
        "virtual_stock_shoulder_side_m", "virtual_stock_chest_height_m", "virtual_stock_chest_back_m",
        "virtual_stock_chest_side_m", "virtual_stock_adaptive_top_height_m", "virtual_stock_adaptive_bottom_height_m",
        "virtual_stock_adaptive_top_half_width_m", "virtual_stock_adaptive_bottom_half_width_m",
        "virtual_stock_hybrid_offhand_influence", "virtual_stock_hybrid_ads_reference",
        "virtual_stock_hybrid_seat_full_m", "virtual_stock_hybrid_seat_release_m",
        "virtual_stock_hybrid_horizontal_release", "virtual_stock_hybrid_horizontal_full_m",
        "virtual_stock_hybrid_horizontal_release_m", "virtual_stock_proximity_release",
        "virtual_stock_proximity_full_m", "virtual_stock_proximity_release_m", "two_hand_support_grip_pose",
        "two_hand_toggle", "two_hand_offhand_influence", "left_hand_forward_m", "two_hand_zone_right_m",
        "left_grip_forward_m", "arm_ik", "floating_hands", "world_collision", "physical_melee", "gesture_melee",
        "physical_melee_swing_speed", "right_shoulder_drop", "shoulder_level", "body_wip", "weapon_probe", "hud_probe",
        "fsr_probe", "bullet_probe", "right_eye_first"};
    CheckUnit(174);
    for (const char* key : universalKeys)
    {
        const std::string assignment = std::string("\n") + key + " = ";
        const std::string message = std::string("Generated config writes exactly one '") + key + "' assignment";
        Check(CountText(organizedConfig, assignment) == 1, message.c_str());
    }
    CheckUnit(175);
    {
        std::ofstream file(primary);
        file << "config_version = 1\n";
        file << "haptic_intensity = 2.0\n";
        file << "headset_smoothing = 1.0\n";
        file << "aim_stabilization = -1.0\n";
        file << "vehicle_cam_forward_m = -9.0\n";
        file << "vehicle_cam_up_m = 9.0\n";
        file << "vehicle_cam_right_m = 9.0\n";
        file << "aa_mode = 4\n";
    }
    CheckUnit(176);
    ConfigLoad(primary.c_str());
    Check(g_config.haptic_intensity == 1.0f, "Haptic intensity is safely clamped");
    Check(g_config.headset_smoothing == 0.10f, "Headset smoothing is capped at the low-latency maximum");
    Check(g_config.aim_stabilization == 0.0f, "Aim stabilization is safely clamped");
    Check(g_config.vehicle_cam_forward_m == kVehicleCamForwardMin && g_config.vehicle_cam_up_m == kVehicleCamUpMax &&
              g_config.vehicle_cam_right_m == kVehicleCamRightMax,
        "Vehicle forward, height and lateral trims use the expanded safe ranges");
    Check(g_config.aa_mode == 4, "SMAA 1x plus FXAA Strong survives config loading");
    CheckUnit(177);
    {
        {
            std::ofstream file(primary);
            file << "virtual_stock_strength = 0.5\n";
            file << "virtual_stock_rear_height_m = -0.30\n";
            file << "virtual_stock_rear_reference = 1\n";
            file << "virtual_stock_shoulder_back_m = 0.08\n";
            file << "virtual_stock_shoulder_side_m = 0.10\n";
            file << "virtual_stock_chest_height_m = -0.32\n";
            file << "virtual_stock_chest_back_m = 0.00\n";
            file << "virtual_stock_chest_side_m = 0.015\n";
        }
        ConfigLoad(primary.c_str());
        Check(g_config.virtual_stock_standard_strength == 0.5f &&
                  g_config.virtual_stock_plus_strength == kVirtualStockV7StrengthDefault &&
                  g_config.virtual_stock_rear_height_m == -0.30f && g_config.virtual_stock_rear_reference == 1 &&
                  g_config.virtual_stock_shoulder_back_m == 0.08f && g_config.virtual_stock_shoulder_side_m == 0.10f &&
                  g_config.virtual_stock_chest_height_m == -0.32f && g_config.virtual_stock_chest_back_m == 0.0f &&
                  g_config.virtual_stock_chest_side_m == 0.015f,
            "legacy shared strength migrates to active Standard and preserves the Plus default");
        {
            std::ofstream file(primary);
            file << "virtual_stock_strength = 0.61\n";
            file << "virtual_stock_rear_reference = 3\n";
        }
        ConfigLoad(primary.c_str());
        Check(g_config.virtual_stock_standard_strength == kVirtualStockV7StrengthDefault &&
                  g_config.virtual_stock_plus_strength == 0.61f,
            "legacy shared strength migrates to active Plus and preserves the Standard default");
        {
            std::ofstream file(primary);
            file << "virtual_stock_strength = 0.44\n";
            file << "virtual_stock_standard_strength = 0.91\n";
            file << "virtual_stock_plus_strength = 0.77\n";
            file << "virtual_stock_rear_reference = 3\n";
        }
        ConfigLoad(primary.c_str());
        Check(g_config.virtual_stock_standard_strength == 0.91f && g_config.virtual_stock_plus_strength == 0.77f,
            "explicit per-mode strengths take precedence over the legacy shared key");
        {
            std::ofstream file(primary);
            file << "virtual_stock_standard_strength = 9.0\n";
            file << "virtual_stock_plus_strength = -9.0\n";
            file << "virtual_stock_rear_height_m = -9.0\n";
            file << "virtual_stock_rear_reference = 1\n";
            file << "virtual_stock_shoulder_back_m = 9.0\n";
            file << "virtual_stock_shoulder_side_m = -9.0\n";
            file << "virtual_stock_chest_height_m = 9.0\n";
            file << "virtual_stock_chest_back_m = 9.0\n";
            file << "virtual_stock_chest_side_m = -9.0\n";
        }
        ConfigLoad(primary.c_str());
        Check(g_config.virtual_stock_standard_strength == 1.0f && g_config.virtual_stock_plus_strength == 0.0f &&
                  g_config.virtual_stock_rear_height_m == -0.30f && g_config.virtual_stock_rear_reference == 1 &&
                  g_config.virtual_stock_shoulder_back_m == 0.25f && g_config.virtual_stock_shoulder_side_m == 0.0f &&
                  g_config.virtual_stock_chest_height_m == -0.220f && g_config.virtual_stock_chest_back_m == 0.25f &&
                  g_config.virtual_stock_chest_side_m == 0.0f,
            "finite virtual-stock controls clamp to their configured ranges");
        {
            std::ofstream file(primary);
            file << "virtual_stock_standard_strength = -9.0\n";
            file << "virtual_stock_plus_strength = 9.0\n";
            file << "virtual_stock_rear_height_m = 9.0\n";
            file << "virtual_stock_rear_reference = 7\n";
            file << "virtual_stock_shoulder_back_m = -9.0\n";
            file << "virtual_stock_shoulder_side_m = 9.0\n";
            file << "virtual_stock_chest_height_m = -9.0\n";
            file << "virtual_stock_chest_back_m = -9.0\n";
            file << "virtual_stock_chest_side_m = 9.0\n";
        }
        ConfigLoad(primary.c_str());
        Check(g_config.virtual_stock_standard_strength == 0.0f && g_config.virtual_stock_plus_strength == 1.0f &&
                  g_config.virtual_stock_rear_height_m == 0.10f && g_config.virtual_stock_rear_reference == 3 &&
                  g_config.virtual_stock_shoulder_back_m == 0.0f && g_config.virtual_stock_shoulder_side_m == 0.20f &&
                  g_config.virtual_stock_chest_height_m == -0.50f && g_config.virtual_stock_chest_back_m == 0.0f &&
                  g_config.virtual_stock_chest_side_m == 0.20f,
            "opposite virtual-stock range endpoints clamp independently");
        {
            std::ofstream file(primary);
            file << "virtual_stock_standard_strength = nan\n";
            file << "virtual_stock_plus_strength = inf\n";
            file << "virtual_stock_rear_height_m = inf\n";
            file << "virtual_stock_rear_reference = 1\n";
            file << "virtual_stock_shoulder_back_m = nan\n";
            file << "virtual_stock_shoulder_side_m = inf\n";
            file << "virtual_stock_chest_height_m = nan\n";
            file << "virtual_stock_chest_back_m = inf\n";
            file << "virtual_stock_chest_side_m = nan\n";
        }
        ConfigLoad(primary.c_str());
        Check(g_config.virtual_stock_standard_strength == kVirtualStockV7StrengthDefault &&
                  g_config.virtual_stock_plus_strength == kVirtualStockV7StrengthDefault &&
                  g_config.virtual_stock_rear_height_m == -0.220f && g_config.virtual_stock_rear_reference == 1 &&
                  g_config.virtual_stock_shoulder_back_m == 0.005f &&
                  g_config.virtual_stock_shoulder_side_m == 0.015f &&
                  g_config.virtual_stock_chest_height_m == -0.320f && g_config.virtual_stock_chest_back_m == 0.000f &&
                  g_config.virtual_stock_chest_side_m == 0.015f,
            "non-finite virtual-stock controls are rejected and retain normal defaults");
        {
            std::ofstream file(primary);
            file << "virtual_stock_adaptive_top_height_m = 0.0\n";
            file << "virtual_stock_adaptive_bottom_height_m = -0.20\n";
            file << "virtual_stock_adaptive_top_half_width_m = 0.020\n";
            file << "virtual_stock_adaptive_bottom_half_width_m = 0.300\n";
        }
        ConfigLoad(primary.c_str());
        Check(g_config.virtual_stock_adaptive_top_height_m == 0.0f &&
                  g_config.virtual_stock_adaptive_bottom_height_m == -0.20f &&
                  g_config.virtual_stock_adaptive_top_half_width_m == 0.020f &&
                  g_config.virtual_stock_adaptive_bottom_half_width_m == 0.300f,
            "Adaptive coverage accepts its exact configured range endpoints");
        {
            std::ofstream file(primary);
            file << "virtual_stock_adaptive_top_height_m = 9.0\n";
            file << "virtual_stock_adaptive_bottom_height_m = 9.0\n";
            file << "virtual_stock_adaptive_top_half_width_m = -9.0\n";
            file << "virtual_stock_adaptive_bottom_half_width_m = 9.0\n";
        }
        ConfigLoad(primary.c_str());
        Check(g_config.virtual_stock_adaptive_top_height_m == 0.0f &&
                  g_config.virtual_stock_adaptive_bottom_height_m == -0.20f &&
                  g_config.virtual_stock_adaptive_top_half_width_m == 0.020f &&
                  g_config.virtual_stock_adaptive_bottom_half_width_m == 0.300f,
            "Adaptive coverage values clamp independently to their safe ranges");
        {
            std::ofstream file(primary);
            file << "virtual_stock_adaptive_top_height_m = nan\n";
            file << "virtual_stock_adaptive_bottom_height_m = inf\n";
            file << "virtual_stock_adaptive_top_half_width_m = nan\n";
            file << "virtual_stock_adaptive_bottom_half_width_m = inf\n";
        }
        ConfigLoad(primary.c_str());
        Check(g_config.virtual_stock_adaptive_top_height_m == -0.180f &&
                  g_config.virtual_stock_adaptive_bottom_height_m == -0.450f &&
                  g_config.virtual_stock_adaptive_top_half_width_m == 0.080f &&
                  g_config.virtual_stock_adaptive_bottom_half_width_m == 0.140f,
            "non-finite Adaptive coverage values retain their defaults");
        {
            std::ofstream file(primary);
            file << "virtual_stock_proximity_full_m = -9.0\n";
            file << "virtual_stock_proximity_release_m = 9.0\n";
        }
        ConfigLoad(primary.c_str());
        Check(g_config.virtual_stock_proximity_full_m == 0.10f && g_config.virtual_stock_proximity_release_m == 0.80f,
            "finite proximity distances clamp independently to their safe ranges");
        {
            std::ofstream file(primary);
            file << "virtual_stock_proximity_release_m = 0.20\n";
            file << "virtual_stock_proximity_full_m = 0.60\n";
        }
        ConfigLoad(primary.c_str());
        Check(g_config.virtual_stock_proximity_full_m == 0.270f && g_config.virtual_stock_proximity_release_m == 0.425f,
            "invalid proximity pair resets to defaults after complete parsing");
        {
            std::ofstream file(primary);
            file << "virtual_stock_proximity_full_m = 0.60\n";
            file << "virtual_stock_proximity_release_m = 0.20\n";
        }
        ConfigLoad(primary.c_str());
        Check(g_config.virtual_stock_proximity_full_m == 0.270f && g_config.virtual_stock_proximity_release_m == 0.425f,
            "proximity pair normalization is independent of config key order");
        {
            std::ofstream file(primary);
            file << "virtual_stock_proximity_full_m = nan\n";
            file << "virtual_stock_proximity_release_m = inf\n";
        }
        ConfigLoad(primary.c_str());
        Check(g_config.virtual_stock_proximity_full_m == 0.270f &&
                  g_config.virtual_stock_proximity_release_m == kVirtualStockV7ProximityReleaseDefaultM,
            "non-finite proximity distances retain deterministic defaults");
    }
    CheckUnit(178);
    Check(g_config.cutscene_theater_enabled && g_config.cutscene_theater_depth == 1.0f &&
              !g_config.cutscene_theater_flip_depth && g_config.cutscene_theater_width_m == 6.0f &&
              g_config.cutscene_theater_distance_m == 4.0f,
        "legacy configs inherit the enabled cutscene-theatre defaults");
    Check(!g_config.roomscale_movement, "legacy config leaves roomscale off");
    Check(!g_config.experimental_hand_alignment, "legacy config preserves released hand positioning");
    // Holster v2: holsters (tap-to-swap, body-anchored) are on by default; manual reload stays opt-in.
    Check(!g_config.manual_reload && g_config.weapon_holsters && g_config.weapon_holster_click &&
              g_config.weapon_zones_body_anchor && g_config.weapon_holster_haptic == 0.12f &&
              g_config.weapon_holster_by_class && !g_config.weapon_reload_take_grip &&
              g_config.weapon_insert_radius_m == 0.13f,
        "legacy config gets default-on holsters and leaves manual reload off");
    g_config.game_menu_pointer = false;
    ConfigSave();
    g_config.game_menu_pointer = true;
    ConfigLoad(primary.c_str());
    Check(!g_config.game_menu_pointer, "native menu pointer toggle survives save/load");
    Check(!g_config.world_collision, "legacy configs inherit the opt-in world-collision default");

    CheckUnit(179);
    {
        {
            std::ofstream file(primary);
            file << "config_version = 5\n";
            file << "dpad_hand = 1\n";
            file << "dpad_head_radius = 0.225\n";
            file << "quest_thumbrest_dpad = 1\n";
        }
        ConfigLoad(primary.c_str());
        Check(g_config.dpad_hand == 1 && g_config.dpad_head_radius == 0.225f && g_config.quest_thumbrest_dpad,
            "D-pad head radius and Quest thumb-rest mode load independently of the selected gesture hand");
        ConfigSave();
        const std::string dpadConfig = ReadTextFile(primary);
        Check(CountText(dpadConfig, "\ndpad_head_radius = 0.225") == 1 &&
                  CountText(dpadConfig, "\nquest_thumbrest_dpad = 1") == 1,
            "Saving writes one canonical D-pad radius and thumb-rest assignment");
        g_config.dpad_head_radius = 0.50f;
        g_config.quest_thumbrest_dpad = false;
        ConfigLoad(primary.c_str());
        Check(g_config.dpad_head_radius == 0.225f && g_config.quest_thumbrest_dpad,
            "Both D-pad preferences survive a save/load round trip");
        g_config.quest_thumbrest_dpad = false;
        ConfigSave();
        ConfigLoad(primary.c_str());
        Check(!g_config.quest_thumbrest_dpad && g_config.dpad_head_radius == 0.225f,
            "Disabling Quest thumb-rest mode persists without changing head-gesture reach");
        struct RadiusCase
        {
            const char* value;
            float expected;
        };
        for (const RadiusCase radius : {RadiusCase{"0.01", 0.10f}, RadiusCase{"0.99", 0.50f}, RadiusCase{"nan", 0.30f},
                 RadiusCase{"inf", 0.30f}, RadiusCase{"malformed", 0.30f}})
        {
            {
                std::ofstream file(primary);
                file << "config_version = 5\n";
                file << "dpad_head_radius = " << radius.value << '\n';
            }
            ConfigLoad(primary.c_str());
            Check(g_config.dpad_head_radius == radius.expected && !g_config.quest_thumbrest_dpad,
                "D-pad radius clamps finite values and rejects malformed or nonfinite values without enabling thumb-rest mode");
        }
    }

    CheckUnit(180);
    {
        {
            std::ofstream file(primary);
            file
                << "manual_reload = 1\nweapon_holsters = 1\nweapon_pouch_down_m = 0.625\n"
                   "weapon_body_zone_radius_m = 0.225\nweapon_holster_location = 1\n"
                   "weapon_reload_button_ce = 1\nweapon_switch_button_halo2 = 2\n"
                   "disable_flashlight_input = 1\nflashlight_button_halo3 = 2\nflashlight_button_odst = 1\n"
                   "flashlight_button_reach = 13\nflashlight_button_halo4 = 14\nflashlight_button_ce = 0\nflashlight_button_halo2 = 6\n"
                   "weapon_reload_button_reach = invalid\nweapon_switch_button_odst = 999\n";
        }
        ConfigLoad(primary.c_str());
        Check(g_config.disable_flashlight_input && g_config.flashlight_button[0] == 2 &&
                  g_config.flashlight_button[1] == 1 && g_config.flashlight_button[2] == 13 &&
                  g_config.flashlight_button[3] == 14 && g_config.flashlight_button[4] == 15 &&
                  g_config.flashlight_button[5] == 6,
            "flashlight layouts and toggle load independently for all six titles");
        Check(g_config.manual_reload && g_config.weapon_holsters && g_config.weapon_pouch_down_m == 0.625f &&
                  g_config.weapon_body_zone_radius_m == 0.225f && g_config.weapon_holster_location == 1 &&
                  g_config.weapon_reload_button[4] == 1 && g_config.weapon_switch_button[5] == 2 &&
                  g_config.weapon_reload_button[2] == 0 && g_config.weapon_switch_button[1] == 4,
            "weapon gestures load independently with per-title layouts and reject malformed buttons");
        ConfigSave();
        g_config = Config{};
        ConfigLoad(primary.c_str());
        Check(g_config.disable_flashlight_input && g_config.flashlight_button[2] == 13 &&
                  g_config.flashlight_button[5] == 6,
            "flashlight input control persists independently of weapon gesture mappings");
        g_config.flashlight_button[4] = 0;
        ConfigSave();
        g_config = Config{};
        ConfigLoad(primary.c_str());
        Check(g_config.flashlight_button[4] == 0, "versioned explicit right-grip selection is not migrated again");
        Check(g_config.manual_reload && g_config.weapon_holsters && g_config.weapon_reload_button[4] == 1 &&
                  g_config.weapon_switch_button[5] == 2 && g_config.weapon_pouch_down_m == 0.625f,
            "weapon gesture toggles, ergonomics and distinct title layouts survive save/load");
        {
            std::ofstream file(primary);
            file << "weapon_pouch_down_m = nan\nweapon_body_zone_radius_m = 99\n";
        }
        ConfigLoad(primary.c_str());
        Check(!g_config.manual_reload && g_config.weapon_holsters && g_config.weapon_pouch_down_m == 0.50f &&
                  g_config.weapon_body_zone_radius_m == 0.40f,
            "invalid geometry stays finite and bounded; reload opt-in");
        Check(g_config.weapon_holster_radius_m == 0.40f && g_config.weapon_holster_slide &&
                  g_config.weapon_holster_click && !g_config.weapon_needler_shake &&
                  g_config.weapon_unknown_reload_visual,
            "old shared radius migrates to both zones while preserving draw, default-on click and default-off shake");
        {
            std::ofstream file(primary);
            file << "weapon_holster_radius_m = 0.33\nweapon_body_zone_radius_m = 0.12\n"
                    "weapon_insert_radius_m = 0.09\nweapon_holster_draw_m = 0.41\n"
                    "weapon_holster_slide = 0\nweapon_holster_click = 1\nweapon_needler_shake = 1\n"
                    "weapon_shake_travel_m = 0.14\nweapon_unknown_reload_visual = 0\n";
        }
        ConfigLoad(primary.c_str());
        ConfigSave();
        g_config = Config{};
        ConfigLoad(primary.c_str());
        Check(g_config.weapon_holster_radius_m == 0.33f && g_config.weapon_body_zone_radius_m == 0.12f &&
                  g_config.weapon_insert_radius_m == 0.09f && g_config.weapon_holster_draw_m == 0.41f &&
                  !g_config.weapon_holster_slide && g_config.weapon_holster_click && g_config.weapon_needler_shake &&
                  g_config.weapon_shake_travel_m == 0.14f && !g_config.weapon_unknown_reload_visual,
            "independent radii and gesture options persist regardless of legacy-key order");
        {
            std::ofstream file(primary);
            file
                << "weapon_zones_body_anchor = 0\nweapon_holster_haptic = 0.3\nweapon_holster_by_class = 0\nweapon_reload_take_grip = 1\n";
        }
        ConfigLoad(primary.c_str());
        ConfigSave();
        g_config = Config{};
        ConfigLoad(primary.c_str());
        Check(!g_config.weapon_zones_body_anchor && g_config.weapon_holster_haptic == 0.30f &&
                  !g_config.weapon_holster_by_class && g_config.weapon_reload_take_grip,
            "body anchor, holster tick strength and holster-by-class persist");
        {
            std::ofstream file(primary);
            file << "weapon_holster_haptic = 7\n";
        }
        ConfigLoad(primary.c_str());
        Check(g_config.weapon_holster_haptic == 0.50f, "holster tick strength is clamped");
        {
            std::ofstream file(primary);
            file << "weapon_holster_radius_m = nan\nweapon_insert_radius_m = -10\n"
                    "weapon_holster_draw_m = inf\nweapon_shake_travel_m = 999\n";
        }
        ConfigLoad(primary.c_str());
        Check(g_config.weapon_holster_radius_m == 0.20f && g_config.weapon_insert_radius_m == 0.06f &&
                  g_config.weapon_holster_draw_m == 0.25f && g_config.weapon_shake_travel_m == 0.20f,
            "new interaction geometry rejects nonfinite data and clamps finite bounds");
    }
    CheckUnit(181);
    {
        std::ofstream file(primary);
        file << "config_version = 5\n";
        file << "world_collision = 1\n";
        file << "physical_melee = 1\n";
        file << "left_handed = 1\n";
        file << "roomscale_movement = 1\n";
        file << "physical_running = 1\nphysical_running_speed = 0.6\nphysical_running_sensitivity = 2\n";
        file << "experimental_body_ik = 1\nbody_ik_hide_lower = 1\n";
        file << "physical_melee_swing_speed = 0.10\n";
    }
    CheckUnit(182);
    ConfigLoad(primary.c_str());
    Check(g_config.world_collision, "the shared world-collision option can be enabled from config");
    Check(g_config.physical_melee && g_config.physical_melee_swing_speed == 0.3f,
        "physical melee loads independently and clamps its swing threshold");
    ConfigSave();
    ConfigLoad(primary.c_str());
    Check(g_config.world_collision, "the shared world-collision option survives a save/load round trip");
    Check(g_config.roomscale_movement, "roomscale survives config save/load");
    Check(g_config.physical_running && g_config.physical_running_speed == .6f &&
              g_config.physical_running_sensitivity == 2 && g_config.experimental_body_ik &&
              g_config.body_ik_hide_lower,
        "physical running controls and separate avatar/lower-body toggles survive config save/load");
    Check(g_config.left_handed, "left-handed main-weapon mode survives a save/load round trip");
    Check(!g_config.experimental_hand_alignment,
        "left-handed updates keep released positioning until explicitly opted in");
    g_config.experimental_hand_alignment = true;
    ConfigSave();
    ConfigLoad(primary.c_str());
    Check(g_config.experimental_hand_alignment && g_config.left_handed && g_config.world_collision &&
              g_config.physical_melee,
        "experimental hand alignment persists without replacing handedness or collision/melee options");
    Check(g_config.physical_melee && g_config.physical_melee_swing_speed == 0.3f,
        "physical-melee enable and swing threshold survive a save/load round trip");

    CheckUnit(183);
    for (int modes = 0; modes < 4; ++modes)
    {
        {
            std::ofstream file(primary);
            file << "world_collision = 0\n";
            file << "physical_melee = " << (modes & 1) << "\n";
            file << "gesture_melee = " << ((modes >> 1) & 1) << "\n";
        }
        ConfigLoad(primary.c_str());
        ConfigSave();
        ConfigLoad(primary.c_str());
        Check(g_config.physical_melee == bool(modes & 1) && g_config.gesture_melee == bool(modes & 2) &&
                  !g_config.world_collision,
            "Both melee options persist independently with world collision off");
    }

    CheckUnit(184);
    {
        std::ofstream file(primary);
        file << "physical_melee_swing_speed = 9.0\n";
    }
    CheckUnit(185);
    ConfigLoad(primary.c_str());
    Check(g_config.physical_melee_swing_speed == 9.0f,
        "physical melee accepts less-sensitive thresholds above five metres per second");
    ConfigSave();
    ConfigLoad(primary.c_str());
    Check(
        g_config.physical_melee_swing_speed == 9.0f, "less-sensitive melee threshold survives a save/load round trip");
    CheckUnit(186);
    {
        std::ofstream file(primary);
        file << "physical_melee_swing_speed = 11.0\n";
    }
    CheckUnit(187);
    ConfigLoad(primary.c_str());
    Check(g_config.physical_melee_swing_speed == 10.0f && Config{}.physical_melee_swing_speed == 2.2f,
        "physical melee clamps at ten while retaining the 2.2 m/s default");
    CheckUnit(188);
    // Immersive layout (v7). A fresh config gets it; a v6-or-older file keeps its
    // stated bindings and old defaults, but the retired Automatic layout
    // becomes Immersive (melee on the grip while swing melee is off).
    {
        std::filesystem::remove(primary);
        ConfigLoad(primary.c_str());
        const int reach = 2; // TitleIndex(HaloReach)
        const int ce = 4;    // TitleIndex(HaloCE)
        Check(g_config.config_version == kConfigVersion && g_config.physical_melee && g_config.melee_fist &&
                  g_config.flashlight_head_gesture &&
                  g_config.vr_bindings[reach][vr_mapping::Interact] == vr_mapping::B &&
                  g_config.vr_bindings[reach][vr_mapping::Reload] == vr_mapping::B &&
                  g_config.vr_bindings[ce][vr_mapping::Sprint] == vr_mapping::X &&
                  g_config.vr_bindings[reach][vr_mapping::Melee] == vr_mapping::Unbound &&
                  g_config.vr_bindings[reach][vr_mapping::Equipment] == vr_mapping::LeftClick &&
                  g_config.vr_bindings[reach][vr_mapping::Crouch] == vr_mapping::Unbound &&
                  g_config.vr_bindings[ce][vr_mapping::Melee] == vr_mapping::Unbound &&
                  g_config.vr_bindings[ce][vr_mapping::SwitchGrenade] == vr_mapping::Unbound &&
                  g_config.physical_crouch && g_config.weapon_grenade_type_slot && g_config.weapon_equipment_slot &&
                  !g_config.y_b_start_chord && g_config.scope_activation == 2 && g_config.vehicle_steer_car == 2 &&
                  g_config.vehicle_steer_bike == 2 && g_config.scope_sight_distance_m == 0.30f &&
                  g_config.scope_sight_angle_deg == 15.0f && g_config.ce_visor_zoom_max == 2.0f &&
                  g_config.vehicle_bounce == 0.0f && g_config.vehicle_cabin_fade_m == 0.5f &&
                  g_config.vehicle_vignette == 0.6f && g_config.vehicle_horizon_lock && g_config.vehicle_dash_cue &&
                  g_config.vehicle_horizon_hold_deg == 6.0f && g_config.turn_vignette == 0.5f &&
                  g_config.turn_snap_deg == 30.0f && g_config.vehicle_turret_handles && g_config.turret_handle_lever &&
                  g_config.two_hand_grab_extra_m == 0.06f && g_config.two_hand_grip_press == 0.80f &&
                  g_config.two_hand_grip_release == 0.65f && g_config.two_hand_fresh_press &&
                  g_config.two_hand_exact_line && g_config.pistol_two_hand_visual_only &&
                  !g_config.vehicle_cockpit_eye_up && g_config.vehicle_cockpit_screen &&
                  g_config.dual_reticle_scale == 0.25f && g_config.cockpit_screen_width_m == 0.60f &&
                  g_config.cockpit_screen_refresh_divisor == 2 && g_config.vehicle_flight_stick &&
                  !g_config.flight_stick_invert && g_config.flight_stick_tilt_deg == 25.0f &&
                  g_config.game_menu_pointer && g_config.vehicle_wheel_feel == 0 &&
                  g_config.vehicle_wheel_ratio_deg == 45.0f && g_config.physical_melee_swing_speed == 2.2f &&
                  g_config.physical_melee_cooldown_s == 0.30f && g_config.weapon_holster_haptic_rearm_m == 0.05f &&
                  g_config.virtual_stock_standard_strength == 0.50f && g_config.virtual_stock_plus_strength == 0.50f &&
                  g_config.virtual_stock_proximity_release && g_config.virtual_stock_proximity_release_m == 0.40f,
            "a fresh config starts with the Immersive layout and swing melee");
        ConfigLoad(primary.c_str());
        Check(g_config.physical_melee && g_config.vr_bindings[reach][vr_mapping::Equipment] == vr_mapping::LeftClick,
            "the fresh Immersive config survives its own save and reload");
    }

    CheckUnit(189);
    {
        std::ofstream file(primary);
        file << "y_b_start_chord = 0\n";
    }
    CheckUnit(190);
    ConfigLoad(primary.c_str());
    Check(!g_config.y_b_start_chord, "The Y+B Start chord can be disabled in the config");
    ConfigSave();
    ConfigLoad(primary.c_str());
    Check(!g_config.y_b_start_chord, "The Y+B Start chord setting survives a save/load round trip");

    CheckUnit(191);
    // Per-SEAT trim. An override exists only for the axis and SEAT
    // actually written; every other axis, seat, vehicle and on-foot read
    // follows the universal pair live. A driver adjustment must never move
    // the passenger or the gunner of the same vehicle. Malformed values and
    // unknown names never invent an override, and the retired
    // vehicle_cam_lead knob is accepted quietly and never rewritten.
    {
        std::ofstream file(primary);
        file << "config_version = 5\n";
        file << "vehicle_cam_forward_m = 0.20\n";
        file << "vehicle_cam_up_m = -0.10\n";
        file << "vehicle_cam_right_m = 0.12\n";
        file << "vehicle_cam_forward_m_warthog_driver = 0.44\n";
        file << "vehicle_cam_up_m_warthog_gunner = 0.31\n";
        file << "vehicle_cam_right_m_warthog_passenger = -0.27\n";
        file << "vehicle_cam_up_m_hornet_passenger2 = 0.33\n";
        file << "vehicle_cam_forward_m_gondola_driver = 9.0\n";
        file << "vehicle_cam_forward_m_ghost_navigator = 9.0\n";
        file << "vehicle_cam_up_m_ghost_driver = broken\n";
        file << "vehicle_view_follow = 0.82\n";
        file << "vehicle_cam_lead = 0.50\n";
    }
    CheckUnit(192);
    ConfigLoad(primary.c_str());
    // Warthog is id 2, Ghost 4, Hornet 8, mirroring Halo3VehicleId.
    const int hogDriver = ConfigSeatTrimSlot(2, 0, false);
    const int hogPassenger = ConfigSeatTrimSlot(2, 1, false);
    const int hogGunner = ConfigSeatTrimSlot(2, 0, true);
    const int hornetSeat2 = ConfigSeatTrimSlot(8, 2, false);
    const int ghostDriver = ConfigSeatTrimSlot(4, 0, false);
    Check(hogDriver >= 0 && hogGunner >= 0 && hogDriver != hogGunner && hogPassenger != hogDriver &&
              ConfigSeatTrimSlot(0, 0, false) == -1 && ConfigSeatTrimSlot(99, 0, false) == -1 &&
              ConfigSeatTrimSlot(2, 7, false) == -1 &&
              // The gunner slot is reserved: a rider seat may never land on
              // it, or a fourth passenger would share the turret's trim.
              ConfigSeatTrimSlot(2, kVehicleGunnerSlot, false) == -1 &&
              ConfigSeatTrimSlot(2, 0, true) == ConfigSeatTrimSlot(2, 99, true),
        "Every seat of a vehicle keys a distinct trim slot, an unidentified "
        "vehicle or unauthored seat keys none, and the gunner slot is "
        "reserved for mounted turrets whatever seat index they report");
    // The mongoose is the control group: no shipped seat trim touches it, so
    // it still shows the bare universal fallback. Seats the maintainer tuned
    // are asserted separately below, against the shipped table itself.
    const int mongooseDriver = ConfigSeatTrimSlot(3, 0, false);
    Check(
        ConfigSeatCamForward(g_config, hogDriver) == 0.44f && ConfigSeatCamForward(g_config, mongooseDriver) == 0.20f &&
            ConfigSeatCamForward(g_config, hogGunner) == 0.20f && ConfigSeatCamUp(g_config, hogGunner) == 0.31f &&
            ConfigSeatCamUp(g_config, hogDriver) == -0.10f && ConfigSeatCamUp(g_config, hornetSeat2) == 0.33f &&
            ConfigSeatCamUp(g_config, ghostDriver) == -0.10f && ConfigSeatCamForward(g_config, -1) == 0.20f &&
            // The file wins over a shipped seat default: the warthog
            // passenger's lateral trim ships at -0.10 and this config sets
            // it to -0.27.
            ConfigSeatCamRight(g_config, hogPassenger) == -0.27f && ConfigSeatCamRight(g_config, hogDriver) == 0.12f &&
            ConfigSeatCamRight(g_config, -1) == 0.12f && g_config.vehicle_view_follow,
        "A seat overrides exactly the axis written for it; an untuned "
        "vehicle's seats and every unset axis follow the universal trim; a "
        "config value beats the shipped seat default; a legacy non-zero "
        "follow fraction migrates to ON");

    CheckUnit(194);
    ConfigSave();
    const std::string perSeatConfig = ReadTextFile(primary);
    Check(CountText(perSeatConfig, "\nvehicle_cam_forward_m_warthog_driver = ") == 1 &&
              CountText(perSeatConfig, "\nvehicle_cam_up_m_warthog_gunner = ") == 1 &&
              CountText(perSeatConfig, "\nvehicle_cam_up_m_hornet_passenger2 = ") == 1 &&
              CountText(perSeatConfig, "\nvehicle_cam_right_m_warthog_passenger = ") == 1 &&
              CountText(perSeatConfig, "vehicle_cam_up_m_warthog_driver") == 0 &&
              CountText(perSeatConfig, "vehicle_cam_right_m_warthog_driver") == 0 &&
              CountText(perSeatConfig, "vehicle_cam_forward_m_warthog_gunner") == 0 &&
              CountText(perSeatConfig, "\nvehicle_cam_up_m_ghost_driver") == 0 &&
              CountText(perSeatConfig, "gondola") == 0 && CountText(perSeatConfig, "navigator") == 0 &&
              CountText(perSeatConfig, "vehicle_cam_lead") == 0 &&
              CountText(perSeatConfig, "\nvehicle_view_follow = 1") == 1,
        "Saving writes a per-seat line only for overrides that exist, drops "
        "unknown vehicle and seat names and malformed values, and retires "
        "vehicle_cam_lead");
    ConfigLoad(primary.c_str());
    Check(ConfigSeatCamForward(g_config, hogDriver) == 0.44f && ConfigSeatCamUp(g_config, hogGunner) == 0.31f &&
              ConfigSeatCamRight(g_config, hogPassenger) == -0.27f && ConfigSeatCamUp(g_config, hornetSeat2) == 0.33f &&
              !g_config.vehicle_cam_up_set[hogDriver] && !g_config.vehicle_cam_forward_set[hogGunner] &&
              !g_config.vehicle_cam_right_set[hogDriver] && g_config.vehicle_view_follow,
        "Per-seat trim overrides survive a save/load round trip");

    // Reach has its own role-neutral bank. These endpoints pin the runtime
    // ReachVehicleId order and prove all sixteen stable seat indices are
    // collision-free without claiming title-specific driver/gunner roles.
    const int reachBanshee0 = ConfigReachSeatTrimSlot(1, 0);
    const int reachFalcon10 = ConfigReachSeatTrimSlot(12, 10);
    const int reachMachinegun15 = ConfigReachSeatTrimSlot(20, 15);
    const int reachDropPod15 = ConfigReachSeatTrimSlot(34, 15);
    const int reachUnmatched0 = ConfigReachSeatTrimSlot(kReachUnmatchedVehicleTrimId, 0);
    const int reachUnmatched15 = ConfigReachSeatTrimSlot(kReachUnmatchedVehicleTrimId, 15);
    Check(reachBanshee0 == 0 && reachDropPod15 == 34 * kReachVehicleSeatSlots - 1 &&
              reachUnmatched0 == 34 * kReachVehicleSeatSlots && reachUnmatched15 == kReachVehicleTrimSlots - 1 &&
              reachMachinegun15 == 20 * kReachVehicleSeatSlots - 1 && reachFalcon10 != reachBanshee0 &&
              !strcmp(kReachVehicleTrimNames[0], "banshee") && !strcmp(kReachVehicleTrimNames[19], "machinegun") &&
              !strcmp(kReachVehicleTrimNames[20], "mac_cannon") &&
              !strcmp(kReachVehicleTrimNames[33], "squad_drop_pod") &&
              !strcmp(kReachVehicleTrimNames[34], "unmatched") && ConfigReachSeatTrimSlot(0, 0) == -1 &&
              ConfigReachSeatTrimSlot(36, 0) == -1 && ConfigReachSeatTrimSlot(12, -1) == -1 &&
              ConfigReachSeatTrimSlot(12, 16) == -1,
        "Reach vehicle names and raw seat indices map to a bounded independent bank");
    CheckUnit(197);
    // The completed Blender scene is the standalone product default, not a
    // Steam-only side effect of whichever config happened to be edited.
    {
        const Config fresh;
        bool allReachShippedRowsApplied = kConfigReachShippedSeatTrimCount == 25;
        for (const ConfigReachShippedSeatTrim& trim : kConfigReachShippedSeatTrims)
        {
            const int slot = ConfigReachSeatTrimSlot(trim.vehicleId, trim.seatIndex);
            allReachShippedRowsApplied =
                allReachShippedRowsApplied && slot >= 0 && fresh.reach_vehicle_cam_forward_set[slot] &&
                fresh.reach_vehicle_cam_up_set[slot] && fresh.reach_vehicle_cam_right_set[slot] &&
                ConfigReachSeatCamForward(fresh, slot) == trim.forward &&
                ConfigReachSeatCamUp(fresh, slot) == trim.up && ConfigReachSeatCamRight(fresh, slot) == trim.right;
        }
        const int scorpion0 = ConfigReachSeatTrimSlot(14, 0);
        const int shadePlasma0 = ConfigReachSeatTrimSlot(17, 0);
        const int shadeFlak0 = ConfigReachSeatTrimSlot(18, 0);
        const int plasmaTurret0 = ConfigReachSeatTrimSlot(19, 0);
        const int sabre0 = ConfigReachSeatTrimSlot(13, 0);
        Check(allReachShippedRowsApplied && ConfigReachSeatCamUp(fresh, scorpion0) == 2.69f &&
                  ConfigReachSeatCamUp(fresh, shadePlasma0) == 2.41f &&
                  ConfigReachSeatCamForward(fresh, shadeFlak0) == 0.11f &&
                  ConfigReachSeatCamUp(fresh, shadeFlak0) == 2.27f &&
                  ConfigReachSeatCamUp(fresh, plasmaTurret0) == 0.24f &&
                  ConfigReachSeatCamForward(fresh, sabre0) == 42.43f && ConfigReachSeatCamUp(fresh, sabre0) == -8.18f,
            "All 25 user-authored Reach Blender placements ship as built-in defaults");

        {
            std::ofstream file(primary);
            file << "config_version = 5\n";
        }
        ConfigLoad(primary.c_str());
        ConfigSave();
        const std::string shippedReachConfig = ReadTextFile(primary);
        Check(CountText(shippedReachConfig, "\nvehicle_cam_up_m_reach_scorpion_seat0 = 2.69") == 1 &&
                  CountText(shippedReachConfig, "\nvehicle_cam_up_m_reach_shade_plasma_seat0 = 2.41") == 1 &&
                  CountText(shippedReachConfig, "\nvehicle_cam_forward_m_reach_shade_flak_seat0 = 0.11") == 1 &&
                  CountText(shippedReachConfig, "\nvehicle_cam_up_m_reach_plasma_turret_seat0 = 0.24") == 1 &&
                  CountText(shippedReachConfig, "\nvehicle_cam_forward_m_reach_sabre_seat0 = 42.43") == 1,
            "Saving a bare config materializes the Reach Blender camera defaults");
        ConfigLoad(primary.c_str());
        Check(ConfigReachSeatCamUp(g_config, scorpion0) == 2.69f &&
                  ConfigReachSeatCamUp(g_config, shadePlasma0) == 2.41f &&
                  ConfigReachSeatCamForward(g_config, shadeFlak0) == 0.11f &&
                  ConfigReachSeatCamUp(g_config, shadeFlak0) == 2.27f &&
                  ConfigReachSeatCamUp(g_config, plasmaTurret0) == 0.24f &&
                  ConfigReachSeatCamForward(g_config, sabre0) == 42.43f &&
                  ConfigReachSeatCamUp(g_config, sabre0) == -8.18f,
            "Reach shipped camera defaults survive a save/load round trip");

        // F1's seat reset is not representable by deleting numeric keys now
        // that absence selects the shipped Blender row. The explicit
        // tombstone must survive save/load and suppress all three numeric axes.
        // The tombstone returns the seat to its AUTHORED row, not to
        // the shared universal trim. Headset testing proved why - a
        // polluted universal reached the Warthog driver through its tombstone
        // and moved the seat by nearly a metre. Only a seat with no authored
        // row (the unmatched row) still follows the universal.
        g_config.vehicle_cam_forward_m = 0.34f;
        g_config.vehicle_cam_up_m = -0.27f;
        g_config.vehicle_cam_right_m = 0.19f;
        ConfigReachSeatUseUniversalTrim(g_config, scorpion0);
        const int reachUnmatchedSeat0 = ConfigReachSeatTrimSlot(kReachUnmatchedVehicleTrimId, 0);
        Check(g_config.reach_vehicle_cam_use_universal[scorpion0] &&
                  !g_config.reach_vehicle_cam_forward_set[scorpion0] && !g_config.reach_vehicle_cam_up_set[scorpion0] &&
                  !g_config.reach_vehicle_cam_right_set[scorpion0] &&
                  ConfigReachSeatCamForward(g_config, scorpion0) == 0.00f &&
                  ConfigReachSeatCamUp(g_config, scorpion0) == 2.69f &&
                  ConfigReachSeatCamRight(g_config, scorpion0) == 0.00f &&
                  ConfigReachSeatCamForward(g_config, reachUnmatchedSeat0) == 0.34f &&
                  ConfigReachSeatCamUp(g_config, reachUnmatchedSeat0) == -0.27f &&
                  ConfigReachSeatCamRight(g_config, reachUnmatchedSeat0) == 0.19f,
            "Reach Back to authored point restores the Blender row, never the universal trim");
        ConfigSave();
        const std::string universalReachConfig = ReadTextFile(primary);
        Check(CountText(universalReachConfig, "\nvehicle_cam_use_universal_reach_scorpion_seat0 = 1") == 1 &&
                  CountText(universalReachConfig, "\nvehicle_cam_forward_m_reach_scorpion_seat0") == 0 &&
                  CountText(universalReachConfig, "\nvehicle_cam_up_m_reach_scorpion_seat0") == 0 &&
                  CountText(universalReachConfig, "\nvehicle_cam_right_m_reach_scorpion_seat0") == 0,
            "Saving a Reach universal reset writes one tombstone and no numeric seat rows");
        ConfigLoad(primary.c_str());
        Check(g_config.reach_vehicle_cam_use_universal[scorpion0] &&
                  !g_config.reach_vehicle_cam_forward_set[scorpion0] && !g_config.reach_vehicle_cam_up_set[scorpion0] &&
                  !g_config.reach_vehicle_cam_right_set[scorpion0] &&
                  ConfigReachSeatCamForward(g_config, scorpion0) == 0.00f &&
                  ConfigReachSeatCamUp(g_config, scorpion0) == 2.69f &&
                  ConfigReachSeatCamRight(g_config, scorpion0) == 0.00f,
            "Reach Back to authored point survives a config reload");

        // This is the pure state transition used immediately before any of the
        // three F1 sliders writes its changed axis. It must capture exactly
        // what the sliders were displaying - the authored row.
        ConfigReachSeatBeginTrimEdit(g_config, scorpion0);
        g_config.reach_vehicle_cam_forward_v[scorpion0] = 0.88f;
        Check(!g_config.reach_vehicle_cam_use_universal[scorpion0] &&
                  g_config.reach_vehicle_cam_forward_set[scorpion0] && g_config.reach_vehicle_cam_up_set[scorpion0] &&
                  g_config.reach_vehicle_cam_right_set[scorpion0] &&
                  ConfigReachSeatCamForward(g_config, scorpion0) == 0.88f &&
                  ConfigReachSeatCamUp(g_config, scorpion0) == 2.69f &&
                  ConfigReachSeatCamRight(g_config, scorpion0) == 0.00f,
            "The first F1 move after reset preserves the other displayed authored axes");
        ConfigSave();
        ConfigLoad(primary.c_str());
        Check(!g_config.reach_vehicle_cam_use_universal[scorpion0] &&
                  ConfigReachSeatCamForward(g_config, scorpion0) == 0.88f &&
                  ConfigReachSeatCamUp(g_config, scorpion0) == 2.69f &&
                  ConfigReachSeatCamRight(g_config, scorpion0) == 0.00f,
            "The first post-reset Reach slider triplet survives save/load");

        // A hand-edited or stale file can contain both shapes. Any valid
        // numeric row is the explicit override regardless of line ordering.
        {
            std::ofstream file(primary);
            file << "config_version = 5\n";
            file << "vehicle_cam_forward_m_reach_scorpion_seat0 = 0.44\n";
            file << "vehicle_cam_use_universal_reach_scorpion_seat0 = 1\n";
        }
        ConfigLoad(primary.c_str());
        Check(!g_config.reach_vehicle_cam_use_universal[scorpion0] &&
                  ConfigReachSeatCamForward(g_config, scorpion0) == 0.44f &&
                  ConfigReachSeatCamUp(g_config, scorpion0) == 2.69f &&
                  ConfigReachSeatCamRight(g_config, scorpion0) == 0.00f,
            "A valid Reach numeric seat key overrides a tombstone independent of file order");
    }
    CheckUnit(198);
    {
        std::ofstream file(primary);
        file << "config_version = 5\n";
        file << "vehicle_cam_forward_m = 0.20\n";
        file << "vehicle_cam_forward_m_reach_falcon_seat10 = 0.42\n";
        file << "vehicle_cam_up_m_reach_space_banshee_seat0 = -0.23\n";
        file << "vehicle_cam_right_m_reach_machinegun_seat15 = 0.31\n";
        file << "vehicle_cam_up_m_reach_wraith_gunner_seat0 = 0.17\n";
        file << "vehicle_cam_right_m_reach_warthog_chaingun_seat0 = -0.19\n";
        file << "vehicle_cam_forward_m_reach_cart_seat15 = 99.0\n";
        file << "vehicle_cam_up_m_reach_cart_seat15 = -99.0\n";
        file << "vehicle_cam_right_m_reach_cart_seat15 = 99.0\n";
        file << "vehicle_cam_forward_m_reach_falcon_seat16 = 0.99\n";
        file << "vehicle_cam_forward_m_reach_prowler_seat0 = 0.99\n";
        file << "vehicle_cam_up_m_reach_ghost_seat0 = broken\n";
    }
    CheckUnit(199);
    ConfigLoad(primary.c_str());
    const int reachSpaceBanshee0 = ConfigReachSeatTrimSlot(2, 0);
    const int reachGhost0 = ConfigReachSeatTrimSlot(3, 0);
    const int reachWraithGunner0 = ConfigReachSeatTrimSlot(6, 0);
    const int reachHogChaingun0 = ConfigReachSeatTrimSlot(9, 0);
    const int reachMacCannon0 = ConfigReachSeatTrimSlot(21, 0);
    const int reachCart15 = ConfigReachSeatTrimSlot(16, 15);
    Check(ConfigReachSeatCamForward(g_config, reachFalcon10) == 0.42f &&
              ConfigReachSeatCamUp(g_config, reachSpaceBanshee0) == -0.23f &&
              ConfigReachSeatCamRight(g_config, reachMachinegun15) == 0.31f &&
              ConfigReachSeatCamUp(g_config, reachWraithGunner0) == 0.17f &&
              ConfigReachSeatCamRight(g_config, reachHogChaingun0) == -0.19f &&
              ConfigReachSeatCamForward(g_config, reachCart15) == kReachVehicleCamForwardMax &&
              ConfigReachSeatCamUp(g_config, reachCart15) == kReachVehicleCamUpMin &&
              ConfigReachSeatCamRight(g_config, reachCart15) == kReachVehicleCamRightMax &&
              ConfigReachSeatCamForward(g_config, reachMacCannon0) == 0.20f &&
              g_config.reach_vehicle_cam_up_set[reachGhost0] && ConfigReachSeatCamUp(g_config, reachGhost0) == 1.13f &&
              ConfigSeatCamForward(g_config, hogDriver) != 0.42f &&
              ConfigOdstSeatCamForward(g_config, ConfigOdstSeatTrimSlot(2, 0, false)) != 0.42f,
        "Reach trim keys load per axis and never modify Halo 3 or ODST storage");
    ConfigSave();
    const std::string reachSeatConfig = ReadTextFile(primary);
    Check(CountText(reachSeatConfig, "\nvehicle_cam_forward_m_reach_falcon_seat10 = 0.42") == 1 &&
              CountText(reachSeatConfig, "\nvehicle_cam_up_m_reach_space_banshee_seat0 = -0.23") == 1 &&
              CountText(reachSeatConfig, "\nvehicle_cam_right_m_reach_machinegun_seat15 = 0.31") == 1 &&
              CountText(reachSeatConfig, "\nvehicle_cam_up_m_reach_wraith_gunner_seat0 = 0.17") == 1 &&
              CountText(reachSeatConfig, "\nvehicle_cam_right_m_reach_warthog_chaingun_seat0 = -0.19") == 1 &&
              CountText(reachSeatConfig, "reach_falcon_seat16") == 0 &&
              CountText(reachSeatConfig, "reach_prowler") == 0,
        "Saving writes only valid Reach seat overrides with stable Reach prefixes");
    ConfigLoad(primary.c_str());
    Check(ConfigReachSeatCamForward(g_config, reachFalcon10) == 0.42f &&
              ConfigReachSeatCamUp(g_config, reachSpaceBanshee0) == -0.23f &&
              ConfigReachSeatCamRight(g_config, reachMachinegun15) == 0.31f &&
              ConfigReachSeatCamUp(g_config, reachWraithGunner0) == 0.17f &&
              ConfigReachSeatCamRight(g_config, reachHogChaingun0) == -0.19f,
        "Reach per-seat trims survive a save/load round trip");
    CheckUnit(200);
    {
        std::ofstream file(primary);
        file << "config_version = 5\n";
        file << "vehicle_view_follow = 0\n";
    }
    CheckUnit(201);
    ConfigLoad(primary.c_str());
    Check(!g_config.vehicle_view_follow, "The vehicle-follow toggle can still select the world-locked view");
    CheckUnit(202);
    // An older whole-vehicle key means every seat of that vehicle. These are
    // a real user's own tuned lines, copied from a live halomccvr.cfg:
    // six vehicles trimmed by hand before the
    // per-seat rework. Losing any of them would be a real regression, so the
    // exact file is pinned here.
    {
        std::ofstream file(primary);
        file << "config_version = 5\n";
        file << "vehicle_cam_forward_m = -0.03\n";
        file << "vehicle_cam_up_m = 0.10\n";
        file << "vehicle_cam_forward_m_scorpion = 0.21\n";
        file << "vehicle_cam_up_m_scorpion = 1.00\n";
        file << "vehicle_cam_forward_m_warthog = 0.02\n";
        file << "vehicle_cam_up_m_warthog = 0.51\n";
        file << "vehicle_cam_forward_m_mongoose = 0.38\n";
        file << "vehicle_cam_up_m_mongoose = 0.04\n";
        file << "vehicle_cam_forward_m_prowler = 0.18\n";
        file << "vehicle_cam_up_m_prowler = -0.02\n";
        file << "vehicle_cam_up_m_banshee = -0.06\n";
        file << "vehicle_cam_forward_m_hornet = 0.06\n";
        file << "vehicle_cam_up_m_hornet = -0.07\n";
    }
    CheckUnit(203);
    ConfigLoad(primary.c_str());
    bool migrated = true;
    // scorpion=1, warthog=2, mongoose=3, prowler=6, banshee=7, hornet=8.
    // clang-format off
    const struct { int id; float fwd; float up; bool fwdSet; } kTuned[] = {
        {1, 0.21f, 1.00f, true},  {2, 0.02f, 0.51f, true},
        {3, 0.38f, 0.04f, true},  {6, 0.18f, -0.02f, true},
        // clang-format on
        {7, -0.03f, -0.06f, false},
        {8, 0.06f, -0.07f, true},
    };
    CheckUnit(204);
    for (const auto& t : kTuned)
        for (int s = 0; s < kVehicleSeatSlots; ++s)
        {
            const int slot = ConfigSeatTrimSlot(t.id, s == kVehicleGunnerSlot ? 0 : s, s == kVehicleGunnerSlot);
            if (ConfigSeatCamForward(g_config, slot) != t.fwd || ConfigSeatCamUp(g_config, slot) != t.up)
                migrated = false;
            // The banshee line set only the height; its forward must still
            // follow the universal trim rather than being invented.
            if (!t.fwdSet && g_config.vehicle_cam_forward_set[slot])
                migrated = false;
        }
    CheckUnit(205);
    // A vehicle the player never touched still follows the universal pair.
    const int ghostSlot = ConfigSeatTrimSlot(4, 0, false);
    Check(migrated && ConfigSeatCamForward(g_config, ghostSlot) == -0.03f &&
              ConfigSeatCamUp(g_config, ghostSlot) == 0.10f,
        "Every per-vehicle trim the user had already tuned migrates onto all "
        "four seats of that vehicle, sets no axis they never set, and leaves "
        "untouched vehicles on the universal trim");

    CheckUnit(206);
    {
        std::ofstream file(primary);
        file << "config_version = 5\n";
        file << "cutscene_theater_enabled = 0\n";
        file << "cutscene_theater_depth = 9\n";
        file << "cutscene_theater_flip_depth = 1\n";
        file << "cutscene_theater_width_m = 0.1\n";
        file << "cutscene_theater_distance_m = 99\n";
    }
    CheckUnit(207);
    ConfigLoad(primary.c_str());
    Check(!g_config.cutscene_theater_enabled && g_config.cutscene_theater_depth == 2.0f &&
              g_config.cutscene_theater_flip_depth && g_config.cutscene_theater_width_m == 0.5f &&
              g_config.cutscene_theater_distance_m == 20.0f,
        "cutscene-theatre settings load and clamp independently");
    g_config.cutscene_theater_enabled = true;
    g_config.cutscene_theater_depth = 1.25f;
    g_config.cutscene_theater_flip_depth = false;
    g_config.cutscene_theater_width_m = 6.5f;
    g_config.cutscene_theater_distance_m = 4.5f;
    ConfigSave();
    ConfigLoad(primary.c_str());
    Check(g_config.cutscene_theater_enabled && g_config.cutscene_theater_depth == 1.25f &&
              !g_config.cutscene_theater_flip_depth && g_config.cutscene_theater_width_m == 6.5f &&
              g_config.cutscene_theater_distance_m == 4.5f,
        "cutscene-theatre settings survive a save/reload round trip");

    CheckUnit(208);
    // resolution_scale is free-form: a hand-typed value must survive exactly,
    // not snap to one of the six installer tiers (the pre-2026-07-20 behavior).
    {
        std::ofstream file(primary);
        file << "resolution_scale = 0.90\n";
    }
    CheckUnit(209);
    ConfigLoad(primary.c_str());
    Check(g_config.resolution_scale == 0.90f, "A custom resolution scale is honored exactly, not snapped to a preset");
    ConfigSave();
    const std::string savedResolutionConfig = ReadTextFile(primary);
    // Per-game lines (halo3_resolution_scale = ...) also contain the text;
    // the launcher reads only the line that starts with the key.
    Check(CountText(savedResolutionConfig, "\nresolution_scale = 0.90") == 1,
        "Organized config keeps the launcher's resolution_scale line compatible");
    ConfigLoad(primary.c_str());
    Check(g_config.resolution_scale == 0.90f, "A custom resolution scale survives a save/reload round trip");
    CheckUnit(210);
    {
        std::ofstream file(primary);
        file << "resolution_scale = 0.05\n";
    }
    CheckUnit(211);
    ConfigLoad(primary.c_str());
    Check(g_config.resolution_scale == kResolutionScaleMin, "A too-small resolution scale is pulled up to the minimum");
    CheckUnit(212);
    {
        std::ofstream file(primary);
        file << "resolution_scale = 5.0\n";
    }
    CheckUnit(213);
    ConfigLoad(primary.c_str());
    Check(!UpdateTwoHandHold(false, true, false) && UpdateTwoHandHold(false, true, true) &&
              UpdateTwoHandHold(true, true, false) && !UpdateTwoHandHold(true, false, true),
        __func__);

    Check(
        g_config.resolution_scale == kResolutionScaleMax, "A too-large resolution scale is pulled down to the maximum");

    CheckUnit(214);
    {
        std::ofstream file(primary);
        file << "config_version = 2\n";
        file << "hud_curvature = 99\n";
        file << "hud_aspect = 99\n";
        file << "hud_vertical_offset = -999\n";
    }
    CheckUnit(215);
    ConfigLoad(primary.c_str());
    Check(g_config.hud_curvature == kHudCurvatureMax && g_config.hud_aspect == kHudAspectMax &&
              g_config.hud_vertical_offset == kHudHeightMin,
        "Excessive HUD curvature, aspect, and height values are safely clamped");

    CheckUnit(216);
    {
        std::ofstream file(primary);
        file << "scope_enabled = 1\n";
        file << "scope_zoom = 99\n";
        file << "scope_screen_width_m = 1\n";
        file << "scope_screen_right_m = -1\n";
        file << "scope_screen_up_m = 1\n";
        file << "scope_screen_forward_m = -1\n";
        file << "scope_refresh_divisor = 99\n";
    }
    CheckUnit(217);
    ConfigLoad(primary.c_str());
    Check(g_config.scope_enabled && g_config.scope_zoom == 24.0f && g_config.scope_screen_width_m == 0.25f &&
              g_config.scope_screen_right_m == -0.30f && g_config.scope_screen_up_m == 0.30f &&
              g_config.scope_screen_forward_m == -0.30f && g_config.scope_refresh_divisor == 4,
        "Universal scope settings are safely clamped");
    g_config.scope_zoom = 8.25f;
    g_config.scope_screen_width_m = 0.12f;
    g_config.scope_screen_right_m = 0.03f;
    g_config.scope_screen_up_m = 0.09f;
    g_config.scope_screen_forward_m = 0.35f;
    g_config.scope_refresh_divisor = 3;
    ConfigSave();
    ConfigLoad(primary.c_str());
    Check(g_config.scope_zoom == 8.25f && g_config.scope_screen_width_m == 0.12f &&
              g_config.scope_screen_right_m == 0.03f && g_config.scope_screen_up_m == 0.09f &&
              g_config.scope_screen_forward_m == 0.35f && g_config.scope_refresh_divisor == 3,
        "Universal scope settings survive a save/reload round trip");

    CheckUnit(218);
    // Deleting the file is the documented "put everything back" escape hatch.
    {
        std::ofstream file(primary);
        file << "gun_scale = 2.5\n";
    }
    CheckUnit(219);
    ConfigLoad(primary.c_str());
    Check(g_config.gun_scale == 2.5f, "A tuned value loads before the reset test");
    std::filesystem::remove(primary);
    ConfigLoad(primary.c_str());
    Check(std::filesystem::exists(primary), "Deleting the config file recreates it on the next load");
    const Config defaults{};
    Check(!defaults.two_hand_toggle && defaults.two_handed_aim,
        "two-hand grip defaults to hold (a real grab), not a toggle click");
    Check(g_config.gun_scale == defaults.gun_scale && g_config.resolution_scale == defaults.resolution_scale &&
              g_config.hud_size == defaults.hud_size && g_config.hud_aspect == defaults.hud_aspect &&
              g_config.hud_curvature == defaults.hud_curvature &&
              g_config.hud_vertical_offset == defaults.hud_vertical_offset &&
              g_config.halo4_helmet == defaults.halo4_helmet && g_config.scope_enabled &&
              g_config.scope_zoom == defaults.scope_zoom &&
              g_config.scope_screen_width_m == defaults.scope_screen_width_m &&
              g_config.scope_screen_right_m == defaults.scope_screen_right_m &&
              g_config.scope_screen_up_m == defaults.scope_screen_up_m &&
              g_config.scope_screen_forward_m == defaults.scope_screen_forward_m &&
              g_config.scope_refresh_divisor == defaults.scope_refresh_divisor &&
              g_config.cutscene_theater_enabled == defaults.cutscene_theater_enabled &&
              g_config.cutscene_theater_depth == defaults.cutscene_theater_depth &&
              g_config.cutscene_theater_flip_depth == defaults.cutscene_theater_flip_depth &&
              g_config.cutscene_theater_width_m == defaults.cutscene_theater_width_m &&
              g_config.cutscene_theater_distance_m == defaults.cutscene_theater_distance_m,
        "The recreated config file carries the struct defaults");
    std::filesystem::remove_all(configDir);

    MenuChordDetector chord;
    MenuChordResult chordResult = chord.Update(1000, true, false);
    Check(!chordResult.toggled, "One stick click does not toggle the menu");
    chordResult = chord.Update(1249, true, true);
    Check(chordResult.toggled && chordResult.consumeClicks,
        "L3+R3 emits one shared menu-toggle/recenter edge and consumes both clicks");
    Check(!chord.Update(1300, true, true).toggled, "A held chord cannot repeat the shared menu-toggle/recenter action");
    Check(chord.Update(1350, false, true).consumeClicks, "Chord clicks stay consumed until both are released");
    chord.Update(1400, false, false);
    chord.Update(2000, true, false);
    Check(!chord.Update(2251, true, true).toggled, "A chord outside the 250 ms window does not toggle");
    chord.Update(2300, false, false);
    Check(chord.Update(2400, true, true).toggled, "A simultaneous chord works after release");

    ScopeToggleDetector scopeToggle;
    Check(!scopeToggle.Update(true, true, false).changed, "R3 press arms the scope without toggling early");
    ScopeToggleUpdate scopeResult = scopeToggle.Update(true, false, false);
    Check(scopeResult.changed && scopeResult.active, "R3 release opens the universal scope");
    scopeToggle.Update(true, true, false);
    scopeResult = scopeToggle.Update(true, false, false);
    Check(scopeResult.changed && !scopeResult.active, "A second R3 click closes the universal scope");

    scopeToggle.Reset();
    scopeToggle.Update(true, true, false); // R3 begins first
    scopeToggle.Update(true, true, true);  // L3 joins; menu consumes chord
    scopeResult = scopeToggle.Update(true, false, true);
    Check(!scopeResult.changed && !scopeResult.active, "A staggered L3+R3 menu chord cancels the pending scope toggle");
    scopeToggle.Reset();
    scopeToggle.Update(true, true, true); // simultaneous menu chord
    scopeResult = scopeToggle.Update(true, false, true);
    Check(!scopeResult.changed && !scopeResult.active, "A simultaneous L3+R3 menu chord never toggles the scope");

    scopeToggle.Update(true, true, false);
    scopeToggle.Update(true, false, false);
    scopeResult = scopeToggle.Update(false, false, false);
    Check(
        scopeResult.changed && !scopeResult.active, "Losing gameplay or disabling the feature resets an active scope");
    scopeToggle.Update(false, true, false);
    scopeToggle.Update(true, true, false);
    scopeResult = scopeToggle.Update(true, false, false);
    Check(!scopeResult.changed && !scopeResult.active, "R3 held across gameplay entry cannot cause a surprise toggle");

    const float identity[4] = {0, 0, 0, 1};
    const float scopeOrigin[3] = {1, 2, 3};
    const ScopeQuadTransform quad = ComputeScopeQuadTransform(identity, scopeOrigin, 0.04f, 0.08f, 0.30f, 0.10f);
    Check(std::fabs(quad.position[0] - 1.04f) < 1e-5f && std::fabs(quad.position[1] - 2.08f) < 1e-5f &&
              std::fabs(quad.position[2] - 2.70f) < 1e-5f,
        "Scope offsets follow the gun's local right/up/forward axes");
    Check(std::fabs(quad.width - 0.10f) < 1e-5f && std::fabs(quad.height - 0.10f) < 1e-5f,
        "Circular scope has equal physical width/height independent of headset distance");

    const float gameBasis[9] = {1, 0, 0, // forward
        0, 1, 0,                         // left
        0, 0, 1};                        // up
    const float safeCameraOrigin[3] = {4.0f, 5.0f, 6.0f};
    const float bulletForward[3] = {1.0f, 0.02f, -0.01f};
    ScopeCameraPose scopeCamera{};
    Check(ComputeScopeCameraPose(gameBasis, safeCameraOrigin, bulletForward, scopeCamera),
        "Scope camera accepts valid gameplay-camera and bullet inputs");
    Check(std::fabs(scopeCamera.position[0] - 4.0f) < 1e-5f && std::fabs(scopeCamera.position[1] - 5.0f) < 1e-5f &&
              std::fabs(scopeCamera.position[2] - 6.0f) < 1e-5f,
        "Scope camera keeps Halo's collision-safe gameplay origin");
    CheckUnit(220);
    {
        // With the head 0.6 m behind the muzzle, the the earlier CE VR mod placement
        // (muzzle + forward offset) puts the camera ahead of the muzzle.
        const float head[3]{0, 0, 0}, muzzle[3]{0.6f, 0, 0}, barrel[3]{1, 0, 0};
        float origin[3]{};
        ScopeCameraPose muzzleCamera{};
        Check(ScopeOriginAtMuzzle(muzzle, barrel, 0.05f * (1.0f / 3.048f), origin) &&
                  ComputeScopeCameraPose(gameBasis, origin, barrel, muzzleCamera) &&
                  scope_log::ForwardOfMuzzle(muzzleCamera.position, muzzle, barrel, 1.0f / 3.048f) > 0.049f,
            "Z1(b): the scope camera sits ahead of the muzzle (fwd-of-muzzle positive)");
        ScopeCameraPose headCamera{};
        Check(ComputeScopeCameraPose(gameBasis, head, barrel, headCamera) &&
                  scope_log::ForwardOfMuzzle(headCamera.position, muzzle, barrel, 1.0f / 3.048f) < 0,
            "Z1(b): the old head origin is behind the muzzle (the rifle blocked it)");
        const float nanBarrel[3]{NAN, 0, 0};
        Check(!ScopeOriginAtMuzzle(muzzle, nanBarrel, 0.01f, origin), "Z1(b): a bad barrel keeps the head origin");
    }
    CheckUnit(221);
    {
        // Cap maths (radians): never below the original, never above what is needed.
        const float d = 0.017453292519943295f;
        Check(std::fabs(CapCullVerticalFov(78 * d, 151 * d, 20 * d) - 98 * d) < 1e-5f,
            "Z1(c): a 151 deg widening caps at 78+20");
        Check(CapCullVerticalFov(78 * d, 85 * d, 20 * d) == 85 * d, "Z1(c): a small widening is kept whole");
        Check(CapCullVerticalFov(78 * d, 70 * d, 20 * d) == 70 * d, "Z1(c): no widening: unchanged");
        Check(CapCullVerticalFov(NAN, 120 * d, 20 * d) == 120 * d, "Z1(c): unknown original: needed");
        Check(ReachScopeOrderFrom(0) == ReachScopeOrder::First &&
                  ReachScopeOrderFrom(1) == ReachScopeOrder::AfterEyes &&
                  ReachScopeOrderFrom(7) == ReachScopeOrder::First &&
                  !std::strcmp(ReachScopeOrderName(ReachScopeOrder::AfterEyes), "after eyes") &&
                  !std::strcmp(ReachScopeOrderName(ReachScopeOrder::First), "first"),
            "reach_scope_order: 0 first, 1 after eyes");
        Check(Config{}.reach_scope_order == 1, "reach_scope_order: default 1 (after the eyes, v22)");
        {
            const std::filesystem::path v21 = std::filesystem::temp_directory_path() / L"halomccvr-scope-order-v21.cfg";
            {
                std::ofstream file(v21);
                file << "config_version = 21" << "\n" << "reach_scope_order = 0" << "\n";
            }
            ConfigLoad(v21.c_str());
            Check(g_config.reach_scope_order == 1, "reach_scope_order: v22 moves a saved 0 to 1 once");
            {
                std::ofstream file(v21);
                file << "config_version = 22" << "\n" << "reach_scope_order = 0" << "\n";
            }
            ConfigLoad(v21.c_str());
            Check(g_config.reach_scope_order == 0, "reach_scope_order: a v22 choice of 0 is kept");
            std::filesystem::remove(v21);
        }
        Check(!Config{}.reach_muzzle_trace, "reach_muzzle_trace: diagnostic, off by default");
        {
            const std::filesystem::path orderConfig =
                std::filesystem::temp_directory_path() / L"halomccvr-scope-order.cfg";
            {
                std::ofstream file(orderConfig);
                file << "config_version = 22" << "\n" << "reach_scope_order = 9" << "\n";
            }
            ConfigLoad(orderConfig.c_str());
            Check(g_config.reach_scope_order == 1, "reach_scope_order: clamps to 0..1");
            ConfigSave();
            Check(ReadTextFile(orderConfig).find("reach_scope_order = 1") != std::string::npos,
                "reach_scope_order: saves");
            std::filesystem::remove(orderConfig);
        }
        {
            const Config s4{};
            Check(s4.scope_parallax && s4.scope_eyebox && s4.scope_parallax_depth == 0.5f &&
                      s4.scope_parallax_max_shift == 0.25f && s4.scope_eyebox_floor == 0.35f &&
                      s4.scope_eyebox_tolerance_m == 0.03f && s4.scope_eyebox_fade_m == 0.05f &&
                      s4.scope_eyebox_other_eye == 0.2f,
                "S4: parallax and eyebox on by default");
            const std::filesystem::path s4Config = std::filesystem::temp_directory_path() / L"halomccvr-s4.cfg";
            {
                std::ofstream file(s4Config);
                file
                    << "config_version = 17\nscope_parallax = 0\nscope_eyebox = 0\nscope_parallax_depth = 99\nscope_eyebox_fade_m = nan\nscope_eyebox_other_eye = -1\n";
            }
            ConfigLoad(s4Config.c_str());
            Check(g_config.scope_parallax && g_config.scope_eyebox && g_config.scope_parallax_depth == 0.5f &&
                      g_config.scope_eyebox_fade_m == 0.05f && g_config.scope_eyebox_other_eye == 0.0f,
                "S4: v17 cfg migrates on; keys clamp");
            ConfigSave();
            {
                std::ofstream file(s4Config);
                file << "config_version = 18\nscope_parallax = 0\nscope_eyebox_other_eye = 0.7\n";
            }
            ConfigLoad(s4Config.c_str());
            Check(!g_config.scope_parallax && g_config.scope_eyebox_other_eye == 0.7f &&
                      g_config.scope_parallax_depth == 0.5f,
                "S4: a v18 choice is kept; v19 resets the depth to 0.5");
            ConfigSave();
            const std::string saved = ReadTextFile(s4Config);
            Check(saved.find("scope_parallax = 0") != std::string::npos &&
                      saved.find("scope_eyebox_other_eye = 0.70") != std::string::npos,
                "S4: keys save");
            {
                std::ofstream file(s4Config);
                file
                    << "config_version = 19\nscope_parallax_depth = 1.5\nscope_eyebox_floor = 2\nscope_parallax_max_shift = -1\n";
            }
            ConfigLoad(s4Config.c_str());
            Check(g_config.scope_parallax_depth == 1.5f && g_config.scope_eyebox_floor == 1.0f &&
                      g_config.scope_parallax_max_shift == 0.0f,
                "v19: a v19 choice is kept; cap/floor clamp to 0..1");
            std::filesystem::remove(s4Config);
        }
        Check(CapLensTravel(1.4f, 0.3f) == 0.3f && CapLensTravel(0.1f, 0.3f) == 0.1f,
            "lens cap: long travel capped, short kept");
        Check(Config{}.menu_dialog_keys, "Save & Quit dialog keys on by default");
        {
            Check(Config{}.mirror_scope_inset, "monitor scope inset on by default");
            const std::filesystem::path insetConfig = std::filesystem::temp_directory_path() / L"halomccvr-inset.cfg";
            {
                std::ofstream file(insetConfig);
                file << "mirror_scope_inset = 0" << "\n";
            }
            ConfigLoad(insetConfig.c_str());
            Check(!g_config.mirror_scope_inset, "monitor scope inset: key parses");
            ConfigSave();
            Check(ReadTextFile(insetConfig).find("mirror_scope_inset = 0") != std::string::npos,
                "monitor scope inset: key saves");
            std::filesystem::remove(insetConfig);
            g_config.mirror_scope_inset = true;
        }
        Check(CapLensTravel(NAN, 0.3f) == 0 && CapLensTravel(-1, 0.3f) == 0 && CapLensTravel(1, 0) == 0,
            "lens cap: bad travel or a 0 cap stays at the lens");
        Check(Config{}.scope_lens_forward_max_m == 2.0f, "lens cap: default 2.0 m (v21: the camera clears the barrel)");
        {
            const std::filesystem::path v20 = std::filesystem::temp_directory_path() / L"halomccvr-lens-v20.cfg";
            {
                std::ofstream file(v20);
                file << "config_version = 20" << "\n" << "scope_lens_forward_max_m = 0.300" << "\n";
            }
            ConfigLoad(v20.c_str());
            Check(g_config.scope_lens_forward_max_m == 2.0f, "lens cap: v21 moves a saved 0.30 to 2.0 once");
            {
                std::ofstream file(v20);
                file << "config_version = 21" << "\n" << "scope_lens_forward_max_m = 0.500" << "\n";
            }
            ConfigLoad(v20.c_str());
            Check(g_config.scope_lens_forward_max_m == 0.5f, "lens cap: a v21 choice is kept");
            std::filesystem::remove(v20);
        }
        {
            const std::filesystem::path capConfig = std::filesystem::temp_directory_path() / L"halomccvr-lens-cap.cfg";
            {
                std::ofstream file(capConfig);
                file << "scope_lens_forward_max_m = 9\n";
            }
            ConfigLoad(capConfig.c_str());
            Check(g_config.scope_lens_forward_max_m == 2.0f, "lens cap: key parses and clamps");
            ConfigSave();
            Check(ReadTextFile(capConfig).find("scope_lens_forward_max_m = 2.000") != std::string::npos,
                "lens cap: key saves");
            std::filesystem::remove(capConfig);
        }
        Check(Config{}.scope_live_glass && Config{}.scope_idle_cadence == 4,
            "S1: live glass on, idle cadence 4 by default");
        {
            const std::filesystem::path glassConfig =
                std::filesystem::temp_directory_path() / L"halomccvr-live-glass.cfg";
            {
                std::ofstream file(glassConfig);
                file << "scope_live_glass = 0\nscope_idle_cadence = 99\nscope_adaptive_resolution = 0\n";
            }
            ConfigLoad(glassConfig.c_str());
            Check(!g_config.scope_live_glass && g_config.scope_idle_cadence == 8, "S1: keys parse and clamp");
            Check(Config{}.scope_adaptive_resolution && !g_config.scope_adaptive_resolution,
                "#8 z45: adaptive scope resolution on by default, key parses");
            ConfigSave();
            const std::string saved = ReadTextFile(glassConfig);
            Check(saved.find("scope_live_glass = 0") != std::string::npos &&
                      saved.find("scope_idle_cadence = 8") != std::string::npos,
                "S1: keys save");
            Check(saved.find("scope_adaptive_resolution = 0") != std::string::npos, "#8 z45: adaptive key saves");
            Check(Config{}.titles_uncap_framerate && saved.find("titles_uncap_framerate = 1") != std::string::npos,
                "#13 z49: Halo 2 / Halo 4 uncapped by default, key saves");
            Check(Config{}.scope_cost_diag_ms == 0.0f && saved.find("scope_cost_diag_ms") == std::string::npos,
                "#8 z45: the scope cost diagnostic is off by default and never saved");
            std::filesystem::remove(glassConfig);
        }
        const Config fresh{};
        Check(fresh.reach_scope_cull_mode == 1 && fresh.reach_scope_cull_cap_deg == 20.0f,
            "S3: Reach eyes keep the real head FOV while zoomed by default (cull mode 1)");
        {
            const std::filesystem::path v19 = std::filesystem::temp_directory_path() / L"halomccvr-cull-v19.cfg";
            {
                std::ofstream file(v19);
                file << "config_version = 19" << "\n" << "reach_scope_cull_mode = 0" << "\n";
            }
            ConfigLoad(v19.c_str());
            Check(g_config.reach_scope_cull_mode == 1, "S3: v20 moves a saved cull mode 0 to 1 once");
            {
                std::ofstream file(v19);
                file << "config_version = 20" << "\n" << "reach_scope_cull_mode = 0" << "\n";
            }
            ConfigLoad(v19.c_str());
            Check(g_config.reach_scope_cull_mode == 0, "S3: a v20 choice of mode 0 is kept");
            std::filesystem::remove(v19);
        }
        const std::filesystem::path cullConfig = std::filesystem::temp_directory_path() / L"halomccvr-reach-cull.cfg";
        {
            std::ofstream file(cullConfig);
            file << "config_version = 20\nreach_scope_cull_mode = 7\nreach_scope_cull_cap_deg = nan\n";
        }
        ConfigLoad(cullConfig.c_str());
        Check(g_config.reach_scope_cull_mode == 2 && g_config.reach_scope_cull_cap_deg == 20.0f,
            "Z1(c): keys parse and clamp");
        ConfigSave();
        Check(ReadTextFile(cullConfig).find("reach_scope_cull_mode = 2") != std::string::npos, "Z1(c): keys save");
        std::filesystem::remove(cullConfig);
    }
    CheckUnit(222);
    {
        // Scope-camera keys: defaults, parse, save, clamp.
        const Config fresh{};
        Check(fresh.scope_camera_at_muzzle && fresh.scope_camera_forward_m[2] == 0.05f,
            "Z1(b): the muzzle camera is on, 0.05 m ahead, by default");
        const std::filesystem::path muzzleConfig =
            std::filesystem::temp_directory_path() / L"halomccvr-scope-muzzle.cfg";
        {
            std::ofstream file(muzzleConfig);
            file << "scope_camera_at_muzzle = 0\nscope_camera_forward_m_reach = 9\nscope_camera_forward_m_h3 = 0.12\n";
        }
        ConfigLoad(muzzleConfig.c_str());
        Check(!g_config.scope_camera_at_muzzle && g_config.scope_camera_forward_m[2] == 0.50f &&
                  std::fabs(g_config.scope_camera_forward_m[0] - 0.12f) < 1e-6f,
            "Z1(b): keys parse and clamp");
        ConfigSave();
        const std::string saved = ReadTextFile(muzzleConfig);
        Check(saved.find("scope_camera_at_muzzle = 0") != std::string::npos &&
                  saved.find("scope_camera_forward_m_reach = 0.500") != std::string::npos,
            "Z1(b): keys save");
        std::filesystem::remove(muzzleConfig);
    }
    CheckUnit(223);
    const float bulletLength = std::sqrt(1.0f + 0.02f * 0.02f + 0.01f * 0.01f);
    Check(std::fabs(scopeCamera.forward[0] - bulletForward[0] / bulletLength) < 1e-5f &&
              std::fabs(scopeCamera.forward[1] - bulletForward[1] / bulletLength) < 1e-5f &&
              std::fabs(scopeCamera.forward[2] - bulletForward[2] / bulletLength) < 1e-5f,
        "Remote scope center continues along Halo's actual bullet direction");

    const ScopeProjectionTangents scopeLens = ComputeScopeProjectionTangents(2.5f, 16.0f / 9.0f);
    Check(std::fabs(scopeLens.horizontal / scopeLens.vertical - 16.0f / 9.0f) < 1e-5f,
        "Scope render projection matches its source surface before cropping");
    const float croppedHorizontal = scopeLens.horizontal / (16.0f / 9.0f);
    Check(std::fabs(croppedHorizontal / scopeLens.vertical - 1.f) < 1e-5f &&
              std::fabs(croppedHorizontal - 0.70020754f / 2.5f) < 1e-5f,
        "Center-cropped scope image is an undistorted square 2.5x lens");

    ScopeRefreshScheduler refresh;
    Check(!refresh.Advance(true, 2) && refresh.Advance(true, 2),
        "Scope refresh divisor 2 renders every second active frame");
    Check(!refresh.Advance(false, 2) && !refresh.Advance(true, 2) && refresh.Advance(true, 2),
        "Closing the scope resets its image refresh schedule");
    Check(refresh.Advance(true, 0), "Scope refresh divisor clamps safely to one");
    CheckUnit(224);
    {
        // Idle cadence 8 (key max) is honoured (was silently capped at 4); 9+ caps at 8.
        ScopeRefreshScheduler idle;
        int shown = 0;
        for (int i = 0; i < 16; ++i)
            shown += idle.Advance(true, 8) ? 1 : 0;
        Check(shown == 2, "S1: idle cadence 8 renders every eighth frame");
        ScopeRefreshScheduler capped;
        shown = 0;
        for (int i = 0; i < 16; ++i)
            shown += capped.Advance(true, 50) ? 1 : 0;
        Check(shown == 2, "S1: cadence caps at 8");
        Check(Config{}.ce_scope_lens_clearance_m == 0.12f, "CE lens camera: 0.12 m clearance by default");
        Check(Config{}.ce_sniper_glass_aspect == 3.0f, "#10 CE sniper glass: 3:1 (120 x 40 mm) by default (z39)");
        const std::filesystem::path ceConfig = std::filesystem::temp_directory_path() / L"halomccvr-ce-lens.cfg";
        {
            std::ofstream file(ceConfig);
            file << "ce_scope_lens_clearance_m = 3" << "\n" << "ce_sniper_glass_aspect = 9" << "\n";
        }
        ConfigLoad(ceConfig.c_str());
        Check(g_config.ce_scope_lens_clearance_m == 0.5f, "CE lens camera: key parses and clamps");
        Check(g_config.ce_sniper_glass_aspect == 4.0f, "#10 CE sniper glass: key parses and clamps at 4");
        ConfigSave();
        Check(ReadTextFile(ceConfig).find("ce_scope_lens_clearance_m = 0.500") != std::string::npos,
            "CE lens camera: key saves");
        Check(ReadTextFile(ceConfig).find("ce_sniper_glass_aspect = 4.00") != std::string::npos,
            "#10 CE sniper glass: key saves");
        std::filesystem::remove(ceConfig);
    }

    CheckUnit(-1);
    std::cout << "checks run: " << g_checks << '\n';
    if (g_failures == 0)
        std::cout << "HaloMCCVR core configured tests passed\n";
    return g_failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
