#pragma once

#include <cstdio>
#include <string>

namespace autobbox::common {

std::FILE *OpenFile(const std::string &path, const char *mode);
bool FileExistsA(const std::string &path);
bool FileExistsW(const std::wstring &path);
bool DirectoryExistsW(const std::wstring &path);
bool EnsureDirectoryW(const std::wstring &path);
std::wstring JoinPath(const std::wstring &base, const wchar_t *name);
std::wstring CurrentWorkingDirectoryW();
std::wstring ResolveUserStateDirectoryW();
bool LoadLastWorkingDirectoryW(std::wstring &path_out);
bool SaveLastWorkingDirectoryW(const std::wstring &path);
bool SaveCurrentWorkingDirectoryW();
bool BuildLogPaths(std::string &startup_log_out, std::string &report_log_out);
std::wstring ResolveToolkitTextRoot();
std::wstring ResolveMsgFile(const wchar_t *message_file_name);
std::wstring ResolveRibbonFile(const wchar_t *ribbon_file_name);

} // namespace autobbox::common
