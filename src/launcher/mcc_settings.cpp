// MCC flat-play settings: the VR preset, snapshot and restore (mcc_settings.h).
#include "mcc_settings.h"
#include <windows.h>
#include <cstdio>

namespace mcc_settings
{
    namespace
    {
        bool ReadWholeFile(const std::wstring& path, std::string& out)
        {
            FILE* f = nullptr;
            _wfopen_s(&f, path.c_str(), L"rb");
            if (!f)
                return false;
            out.clear();
            char buf[65536];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
                out.append(buf, n);
            fclose(f);
            return true;
        }

        // Write via a temp file + replace so MCC never sees a half-written file.
        bool WriteWholeFile(const std::wstring& path, const std::string& data)
        {
            const std::wstring tmp = path + L".vrtmp";
            FILE* f = nullptr;
            _wfopen_s(&f, tmp.c_str(), L"wb");
            if (!f)
                return false;
            const bool ok = fwrite(data.data(), 1, data.size(), f) == data.size();
            fclose(f);
            if (!ok || !MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
            {
                DeleteFileW(tmp.c_str());
                return false;
            }
            return true;
        }

        bool FileExists(const std::wstring& path)
        {
            const DWORD attr = GetFileAttributesW(path.c_str());
            return attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY);
        }

        // Calls visit(key, value) for each key=value line of a snapshot.
        template <typename Visit>
        void ForEachLine(const std::string& snapshot, Visit visit)
        {
            size_t lineStart = 0;
            while (lineStart < snapshot.size())
            {
                size_t lineEnd = snapshot.find('\n', lineStart);
                if (lineEnd == std::string::npos)
                    lineEnd = snapshot.size();
                std::string line = snapshot.substr(lineStart, lineEnd - lineStart);
                lineStart = lineEnd + 1;
                if (!line.empty() && line.back() == '\r')
                    line.pop_back();
                const size_t eq = line.find('=');
                if (eq != std::string::npos)
                    visit(line.substr(0, eq), line.substr(eq + 1));
            }
        }
    } // namespace

    bool FindIniValue(const std::string& text, const char* key, size_t& valueBegin, size_t& valueEnd)
    {
        const std::string needle = std::string(key) + "=";
        size_t pos = 0;
        while ((pos = text.find(needle, pos)) != std::string::npos)
        {
            if (pos == 0 || text[pos - 1] == '\n')
            {
                valueBegin = pos + needle.size();
                valueEnd = text.find_first_of("\r\n", valueBegin);
                if (valueEnd == std::string::npos)
                    valueEnd = text.size();
                return true;
            }
            pos += needle.size();
        }
        return false;
    }

    std::string BuildSnapshot(const std::string& ini)
    {
        std::string snapshot;
        for (const Key& k : kSnapshotKeys)
        {
            size_t b, e;
            if (FindIniValue(ini, k.key, b, e))
                snapshot += std::string(k.key) + "=" + ini.substr(b, e - b) + "\r\n";
        }
        return snapshot;
    }

    int ApplyPreset(std::string& ini)
    {
        int changed = 0;
        for (const Key& k : kSnapshotKeys)
        {
            size_t b, e;
            if (!k.vrValue || !FindIniValue(ini, k.key, b, e))
                continue;
            if (ini.compare(b, e - b, k.vrValue) != 0)
            {
                ini.replace(b, e - b, k.vrValue);
                ++changed;
            }
        }
        return changed;
    }

    int RestoreSnapshot(std::string& ini, const std::string& snapshot)
    {
        int restored = 0;
        ForEachLine(snapshot,
            [&](const std::string& key, const std::string& value)
            {
                size_t b, e;
                if (FindIniValue(ini, key.c_str(), b, e))
                {
                    ini.replace(b, e - b, value);
                    ++restored;
                }
            });
        return restored;
    }

    std::string FirstMismatch(const std::string& ini, const std::string& snapshot)
    {
        std::string mismatch;
        ForEachLine(snapshot,
            [&](const std::string& key, const std::string& value)
            {
                size_t b, e;
                if (mismatch.empty() && FindIniValue(ini, key.c_str(), b, e) && ini.compare(b, e - b, value) != 0)
                    mismatch = key + " is " + ini.substr(b, e - b) + ", snapshot " + value;
            });
        return mismatch;
    }

    std::string DisplaySummary(const std::string& snapshot)
    {
        std::string summary;
        for (const Key& k : kSnapshotKeys)
        {
            if (k.vrValue)
                continue;
            size_t b, e;
            summary += (summary.empty() ? "" : ", ") + std::string(k.key) + "=" +
                       (FindIniValue(snapshot, k.key, b, e) ? snapshot.substr(b, e - b) : "-");
        }
        return summary;
    }

    void ApplyVrSettings(const std::wstring& iniPath, const std::wstring& snapshotPath, bool saveSnapshot, Log log)
    {
        std::string ini;
        if (!ReadWholeFile(iniPath, ini))
        {
            log("MCC settings: %ls not found (first MCC run?); preset skipped", iniPath.c_str());
            return;
        }
        const bool keepOldSnapshot = !saveSnapshot || FileExists(snapshotPath);
        std::string snapshot;
        if (!keepOldSnapshot)
        {
            // Saved before the preset is written, so a launcher killed before
            // MCC exits restores on the next launch.
            snapshot = BuildSnapshot(ini);
            if (!WriteWholeFile(snapshotPath, snapshot))
            {
                log("MCC settings: cannot save flat snapshot; preset skipped");
                return;
            }
        }
        else if (saveSnapshot)
            ReadWholeFile(snapshotPath, snapshot);
        const int changed = ApplyPreset(ini);
        if (changed && !WriteWholeFile(iniPath, ini))
        {
            log("MCC settings: preset write FAILED");
            return;
        }
        log("MCC settings: VR preset applied (%d values changed; FOV 120 in every title, "
            "V-Sync off, no frame cap)%s",
            changed, keepOldSnapshot ? "; earlier unrestored snapshot kept" : "");
        if (saveSnapshot)
            log("MCC settings: flat snapshot display values: %s", DisplaySummary(snapshot).c_str());
    }

    bool RestoreFlatSettings(const std::wstring& iniPath, const std::wstring& snapshotPath, Log log)
    {
        std::string snapshot, ini;
        if (!ReadWholeFile(snapshotPath, snapshot))
            return true;
        if (!ReadWholeFile(iniPath, ini))
        {
            log("MCC settings: cannot read %ls to restore; keeping snapshot", iniPath.c_str());
            return false;
        }
        const int restored = RestoreSnapshot(ini, snapshot);
        if (!WriteWholeFile(iniPath, ini))
        {
            log("MCC settings: restore write FAILED; keeping snapshot");
            return false;
        }
        log("MCC settings: restored %d flat-play values (display: %s)", restored, DisplaySummary(snapshot).c_str());
        std::string written;
        if (!ReadWholeFile(iniPath, written))
        {
            log("MCC settings: restore check cannot read the file back; keeping snapshot");
            return false;
        }
        const std::string mismatch = FirstMismatch(written, snapshot);
        if (!mismatch.empty())
        {
            log("MCC settings: restore check FAILED (%s); keeping snapshot", mismatch.c_str());
            return false;
        }
        DeleteFileW(snapshotPath.c_str());
        log("MCC settings: restore verified");
        return true;
    }
} // namespace mcc_settings
