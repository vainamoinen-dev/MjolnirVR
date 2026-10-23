// Installer rules: config key migration and the other decisions the launcher's installer makes.
// Used by launcher/installer.cpp, launcher/menu.cpp and the core and installer tests.
#pragma once

// Pure installer policy: no registry, process, filesystem or game access.
#include <algorithm>
#include <cctype>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace mcc_installer
{
    inline std::string Trim(std::string_view text)
    {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())))
            text.remove_prefix(1);
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())))
            text.remove_suffix(1);
        return std::string(text);
    }
    inline std::string ConfigKey(std::string_view line)
    {
        if (line.starts_with("\xEF\xBB\xBF"))
            line.remove_prefix(3);
        line = line.substr(0, line.find('#'));
        const auto equals = line.find('=');
        return equals == std::string_view::npos ? std::string{} : Trim(line.substr(0, equals));
    }
    inline std::vector<std::string> Lines(std::string_view text)
    {
        std::vector<std::string> lines;
        while (!text.empty())
        {
            const auto end = text.find('\n');
            lines.emplace_back(text.substr(0, end));
            if (end == std::string_view::npos)
                break;
            text.remove_prefix(end + 1);
        }
        return lines;
    }
    struct ConfigMerge
    {
        std::string text;
        size_t addedKeys = 0;
    };
    inline ConfigMerge MergeConfig(const std::string& existing, const std::string& defaults)
    {
        if (existing.empty())
            return {defaults, 0};
        ConfigMerge result{existing, 0};
        std::set<std::string> keys;
        int version = 1;
        for (const auto& line : Lines(existing))
        {
            auto key = ConfigKey(line);
            if (key.empty())
                continue;
            keys.insert(key);
            if (key == "config_version")
            {
                const auto value = Trim(std::string_view(line).substr(line.find('=') + 1));
                // Whole decimal number ("10" is ten, not one); junk keeps version 1.
                int parsed = 0;
                size_t digits = 0;
                while (digits < value.size() && digits < 6 && value[digits] >= '0' && value[digits] <= '9')
                    parsed = parsed * 10 + (value[digits++] - '0');
                if (digits && digits == value.size() && parsed >= 1)
                    version = parsed;
            }
        }
        for (const auto& line : Lines(defaults))
        {
            const auto key = ConfigKey(line);
            if (key.empty() || keys.contains(key))
                continue;
            // ConfigSave emits a title-specific copy of every tunable. Existing
            // profiles that predate this feature inherit the global field unless
            // an explicit profile key is present. Do not append seed profile
            // values over a user's saved global calibration during cfg migration.
            bool inheritedProfile = false;
            for (const auto prefix : {"halo3_", "odst_", "reach_", "halo4_", "halo2a_", "halo2c_", "halo1_"})
            {
                const std::string_view profilePrefix(prefix);
                if (!key.starts_with(profilePrefix))
                    continue;
                const auto suffix = key.substr(profilePrefix.size());
                if (keys.contains(suffix) || (suffix == "hud_curvature" && keys.contains("hud_height")))
                {
                    inheritedProfile = true;
                    break;
                }
            }
            if (inheritedProfile)
                continue;
            // Do not suppress ConfigLoad's migrations or cause a current default to
            // be interpreted as an older, differently calibrated setting.
            // Previous candidates stored one stock-strength key for the selected
            // mode, and a boolean smoothing key. Leave missing replacement keys
            // absent while those legacy values exist so ConfigLoad can migrate
            // them; once the game saves, the explicit per-mode/numeric keys win.
            if (key == "config_version" || key == "flashlight_mapping_version" ||
                (key == "scope_zoom" && version < 4) ||
                // Before v25 a missing visor_zoom_scale meant 1.5, which
                // v25 folds into the edges; the new default (1.00) would shrink the window.
                (key == "visor_zoom_scale" && version < 25) ||
                (key == "hud_curvature" && (version < 2 || keys.contains("hud_height"))) ||
                (key == "weapon_holster_radius_m" && keys.contains("weapon_body_zone_radius_m")) ||
                ((key == "virtual_stock_standard_strength" || key == "virtual_stock_plus_strength") &&
                    keys.contains("virtual_stock_strength")) ||
                (key == "two_hand_smoothing_strength" && keys.contains("two_hand_smoothing")) ||
                // The Immersive layout (v7) is for new installs only: never append
                // its bindings or swing-melee default to an older config, so
                // ConfigLoad keeps that player's previous layout.
                ((key.starts_with("vr_bind_") || key == "physical_melee" || key == "melee_fist" ||
                     key == "melee_support_free" || key == "flashlight_head_gesture" ||
                     key == "halo4_visor_follow_hud" || key == "physical_crouch" || key == "weapon_grenade_type_slot" ||
                     key == "weapon_equipment_slot" || key == "y_b_start_chord" || key == "scope_activation" ||
                     key == "vehicle_steer_car" || key == "vehicle_steer_bike" || key == "vehicle_vignette" ||
                     key == "vehicle_bounce" || key == "vehicle_cabin_fade_m" || key == "vehicle_horizon_hold_deg" ||
                     key == "turn_vignette" || key == "cutscene_fade_ms" || key == "vehicle_horizon_lock" ||
                     key == "vehicle_dash_cue" || key == "vehicle_turret_handles" || key == "turret_handle_seat_axis" ||
                     key == "two_hand_grab_extra_m" || key == "two_hand_grip_press" || key == "two_hand_grip_release" ||
                     key == "two_hand_fresh_press" || key == "two_hand_exact_line" ||
                     key == "pistol_two_hand_visual_only" || key == "dual_reticle_scale" ||
                     key == "scope_sight_distance_m" || key == "scope_look_defaults" ||
                     key == "scope_sight_angle_deg" || key == "ce_visor_zoom_max" || key == "vehicle_cockpit_eye_up" ||
                     key == "vehicle_cockpit_screen" || key == "cockpit_screen_width_m" ||
                     key == "cockpit_screen_distance_m" || key == "cockpit_screen_offset_y_m" ||
                     key == "cockpit_screen_fov_deg" || key == "cockpit_screen_ahead_m" ||
                     key == "cockpit_screen_refresh_divisor" || key == "vehicle_flight_stick" ||
                     key == "game_menu_pointer" || key == "pause_menu_world_lock" || key == "visor_zoom_scale" ||
                     key == "ce_visor_fullres" || key == "physical_melee_swing_speed" ||
                     key == "physical_melee_cooldown_s" || key == "weapon_holster_haptic_rearm_m" ||
                     key == "virtual_stock_standard_strength" || key == "virtual_stock_plus_strength" ||
                     key == "virtual_stock_proximity_release_m") &&
                    version < 7))
                continue;
            if (!result.addedKeys)
            {
                if (result.text.back() != '\n')
                    result.text += "\r\n";
                result.text += "\r\n# New MjolnirVR settings added by the installer.\r\n";
            }
            result.text += Trim(line) + "\r\n";
            keys.insert(key);
            ++result.addedKeys;
        }
        return result;
    }

    inline bool SafeRelativePath(std::string_view path)
    {
        if (path.empty() || path.size() > 240 || path.front() == '/' || path.front() == '\\')
            return false;
        std::string part;
        auto validPart = [](std::string value)
        {
            if (value.empty() || value == "." || value == ".." || value.back() == '.' || value.back() == ' ')
                return false;
            std::transform(value.begin(), value.end(), value.begin(),
                [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
            value.resize(value.find('.') == std::string::npos ? value.size() : value.find('.'));
            return value != "CON" && value != "PRN" && value != "AUX" && value != "NUL" &&
                   !(value.size() == 4 && (value.starts_with("COM") || value.starts_with("LPT")) && value[3] >= '0' &&
                       value[3] <= '9');
        };
        for (const unsigned char c : path)
        {
            if (c < 32 || c >= 127 || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
                return false;
            if (c == '/' || c == '\\')
            {
                if (!validPart(part))
                    return false;
                part.clear();
            }
            else
                part += static_cast<char>(c);
        }
        return validPart(part);
    }
    struct PayloadFile
    {
        std::string relative;
        std::string sha256;
    };
    inline bool ParseManifest(const std::string& text, std::vector<PayloadFile>& files, std::string& error)
    {
        files.clear();
        std::set<std::string> seen;
        bool dll = false, config = false;
        for (auto line : Lines(text))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            if (line.size() < 67 || line[64] != ' ' || line[65] != ' ')
            {
                error = "Invalid payload manifest line.";
                return false;
            }
            const std::string hash = line.substr(0, 64), path = line.substr(66);
            if (!std::all_of(hash.begin(), hash.end(), [](unsigned char c) { return std::isxdigit(c) != 0; }) ||
                !SafeRelativePath(path))
            {
                error = "Unsafe path or invalid SHA-256 in payload manifest.";
                return false;
            }
            std::string canonical = path;
            std::transform(canonical.begin(), canonical.end(), canonical.begin(),
                [](unsigned char c) { return c == '/' ? '\\' : static_cast<char>(std::tolower(c)); });
            if (!seen.insert(canonical).second || canonical == "install-manifest.sha256" ||
                canonical.starts_with(".install-") || canonical == "backups" || canonical.starts_with("backups\\"))
            {
                error = "Duplicate or reserved payload path.";
                return false;
            }
            dll |= canonical == "halomccvr.dll";
            config |= canonical == "halomccvr.cfg";
            files.push_back({path, hash});
            if (files.size() > 4096)
            {
                error = "Payload manifest has too many files.";
                return false;
            }
        }
        if (!(dll && config))
        {
            error = "Payload must include the DLL and default configuration.";
            return false;
        }
        return true;
    }

    // Valve KeyValues quoted tokens; enough for library paths and installdir.
    inline std::vector<std::string> VdfValues(const std::string& text, const std::string& key)
    {
        std::vector<std::string> tokens, values;
        for (size_t i = 0; i < text.size(); ++i)
        {
            if (text[i] == '/' && i + 1 < text.size() && text[i + 1] == '/')
            {
                i = text.find('\n', i);
                if (i == std::string::npos)
                    break;
                continue;
            }
            if (text[i] != '"')
                continue;
            std::string token;
            while (++i < text.size() && text[i] != '"')
            {
                if (text[i] == '\\' && i + 1 < text.size() && (text[i + 1] == '\\' || text[i + 1] == '"'))
                    ++i;
                token += text[i];
            }
            tokens.push_back(token);
        }
        for (size_t i = 0; i + 1 < tokens.size(); ++i)
            if (tokens[i] == key)
                values.push_back(tokens[i + 1]);
        return values;
    }
} // namespace mcc_installer
