#include "autobbox/common/files.h"

#include "autobbox/common/strings.h"

#include <ProToolkit.h>
#include <ProUtil.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cwchar>
#include <vector>

namespace autobbox::common {

namespace {

constexpr wchar_t kStateDirectoryName[] = L"AutoBBoxToolkit";
constexpr wchar_t kSettingsFileName[] = L"settings.ini";
constexpr wchar_t kSettingsSection[] = L"session";
constexpr wchar_t kLastWorkingDirectoryKey[] = L"last_working_directory";

std::wstring ResolveAppDataDirectoryW()
{
    const DWORD required = GetEnvironmentVariableW(L"APPDATA", nullptr, 0);
    if (required == 0) {
        return std::wstring();
    }

    std::wstring path(static_cast<size_t>(required), L'\0');
    const DWORD written = GetEnvironmentVariableW(L"APPDATA", path.data(), required);
    if (written == 0 || written >= required) {
        return std::wstring();
    }

    path.resize(written);
    return path;
}

std::wstring ResolveSettingsFilePathW()
{
    const std::wstring state_dir = ResolveUserStateDirectoryW();
    if (state_dir.empty()) {
        return std::wstring();
    }
    return JoinPath(state_dir, kSettingsFileName);
}

} // namespace

std::FILE *OpenFile(const std::string &path, const char *mode)
{
    if (path.empty() || mode == nullptr) {
        return nullptr;
    }
#if defined(_MSC_VER)
    std::FILE *fp = nullptr;
    fopen_s(&fp, path.c_str(), mode);
    return fp;
#else
    return std::fopen(path.c_str(), mode);
#endif
}

bool FileExistsA(const std::string &path)
{
    if (path.empty()) {
        return false;
    }
    std::FILE *fp = OpenFile(path, "r");
    if (fp == nullptr) {
        return false;
    }
    std::fclose(fp);
    return true;
}

bool FileExistsW(const std::wstring &path)
{
    if (path.empty()) {
        return false;
    }
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && ((attrs & FILE_ATTRIBUTE_DIRECTORY) == 0);
}

bool DirectoryExistsW(const std::wstring &path)
{
    if (path.empty()) {
        return false;
    }
    const DWORD attrs = GetFileAttributesW(path.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0);
}

bool EnsureDirectoryW(const std::wstring &path)
{
    if (path.empty()) {
        return false;
    }
    if (DirectoryExistsW(path)) {
        return true;
    }
    if (CreateDirectoryW(path.c_str(), nullptr) != FALSE) {
        return true;
    }
    return DirectoryExistsW(path);
}

std::wstring JoinPath(const std::wstring &base, const wchar_t *name)
{
    if (name == nullptr || name[0] == L'\0') {
        return base;
    }
    if (base.empty()) {
        return std::wstring(name);
    }

    std::wstring out(base);
    const wchar_t last = out.back();
    if (last != L'\\' && last != L'/') {
        out.push_back(L'\\');
    }
    out += name;
    return out;
}

std::wstring CurrentWorkingDirectoryW()
{
    ProPath wdir = {0};
    if (ProDirectoryCurrentGet(wdir) == PRO_TK_NO_ERROR) {
        return std::wstring(wdir);
    }
    return std::wstring();
}

std::wstring ResolveUserStateDirectoryW()
{
    const std::wstring appdata_dir = ResolveAppDataDirectoryW();
    if (appdata_dir.empty()) {
        return std::wstring();
    }
    return JoinPath(appdata_dir, kStateDirectoryName);
}

bool LoadLastWorkingDirectoryW(std::wstring &path_out)
{
    path_out.clear();

    const std::wstring settings_path = ResolveSettingsFilePathW();
    if (settings_path.empty() || !FileExistsW(settings_path)) {
        return false;
    }

    std::vector<wchar_t> buffer(32768, L'\0');
    const DWORD len = GetPrivateProfileStringW(
        kSettingsSection,
        kLastWorkingDirectoryKey,
        L"",
        buffer.data(),
        static_cast<DWORD>(buffer.size()),
        settings_path.c_str());
    if (len == 0) {
        return false;
    }

    path_out.assign(buffer.data(), len);
    return !path_out.empty();
}

bool SaveLastWorkingDirectoryW(const std::wstring &path)
{
    if (path.empty()) {
        return false;
    }

    const std::wstring state_dir = ResolveUserStateDirectoryW();
    if (state_dir.empty() || !EnsureDirectoryW(state_dir)) {
        return false;
    }

    const std::wstring settings_path = JoinPath(state_dir, kSettingsFileName);
    return WritePrivateProfileStringW(
               kSettingsSection,
               kLastWorkingDirectoryKey,
               path.c_str(),
               settings_path.c_str()) != FALSE;
}

bool SaveCurrentWorkingDirectoryW()
{
    const std::wstring path = CurrentWorkingDirectoryW();
    return !path.empty() && SaveLastWorkingDirectoryW(path);
}

bool BuildLogPaths(std::string &startup_log_out, std::string &report_log_out)
{
    startup_log_out.clear();
    report_log_out.clear();

    ProPath wdir = {0};
    const ProError st = ProDirectoryCurrentGet(wdir);
    if (st != PRO_TK_NO_ERROR) {
        return false;
    }

    const std::string dir = WToA(wdir);
    if (dir.empty()) {
        return false;
    }

    startup_log_out = dir + "\\autobbox_startup.log";
    report_log_out = dir + "\\autobbox_report.txt";
    return true;
}

std::wstring ResolveToolkitTextRoot()
{
    ProPath wtext = {0};
    if (ProToolkitApplTextPathGet(wtext) == PRO_TK_NO_ERROR) {
        return std::wstring(wtext);
    }
    return std::wstring();
}

std::wstring ResolveMsgFile(const wchar_t *message_file_name)
{
    const std::wstring text_root = ResolveToolkitTextRoot();
    if (!text_root.empty()) {
        const std::wstring direct = JoinPath(text_root, message_file_name);
        if (FileExistsW(direct)) {
            return direct;
        }

        const std::wstring nested = JoinPath(JoinPath(text_root, L"text"), message_file_name);
        if (FileExistsW(nested)) {
            return nested;
        }
    }

    return message_file_name == nullptr ? std::wstring() : std::wstring(message_file_name);
}

std::wstring ResolveRibbonFile(const wchar_t *ribbon_file_name)
{
    const std::wstring text_root = ResolveToolkitTextRoot();
    if (!text_root.empty()) {
        const std::wstring nested = JoinPath(JoinPath(text_root, L"text"), ribbon_file_name);
        if (FileExistsW(nested)) {
            return nested;
        }

        const std::wstring direct = JoinPath(text_root, ribbon_file_name);
        if (FileExistsW(direct)) {
            return direct;
        }

        const std::wstring standard = JoinPath(JoinPath(text_root, L"ribbon"), ribbon_file_name);
        if (FileExistsW(standard)) {
            return standard;
        }
    }

    return ribbon_file_name == nullptr ? std::wstring() : std::wstring(ribbon_file_name);
}

} // namespace autobbox::common
