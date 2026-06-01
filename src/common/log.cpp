#include "autobbox/common/log.h"

#include "autobbox/common/files.h"

#include <cstring>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace autobbox::common {

namespace {

bool WriteFormattedLine(std::FILE *fp,
                        const char *prefix,
                        const char *fmt,
                        va_list args)
{
    if (fp == nullptr || fmt == nullptr) {
        return false;
    }

    if (prefix != nullptr && prefix[0] != '\0') {
        std::fputs(prefix, fp);
    }
    std::vfprintf(fp, fmt, args);
    std::fputc('\n', fp);
    return true;
}

} // namespace

bool AppendFormattedLine(const std::string &path,
                         const char *prefix,
                         const char *fmt,
                         va_list args)
{
    if (path.empty() || fmt == nullptr) {
        return false;
    }

    std::FILE *fp = OpenFile(path, "a");
    if (fp == nullptr) {
        return false;
    }

    const bool ok = WriteFormattedLine(fp, prefix, fmt, args);
    std::fclose(fp);
    return ok;
}

bool AppendFormattedLine(const std::string &path,
                         std::FILE *session_file,
                         const char *prefix,
                         const char *fmt,
                         va_list args)
{
    if (session_file != nullptr) {
        const bool ok = WriteFormattedLine(session_file, prefix, fmt, args);
        std::fflush(session_file);
        return ok;
    }
    return AppendFormattedLine(path, prefix, fmt, args);
}

void BeginBufferedLogSession(BufferedLogSession &session, const std::string &path)
{
    EndBufferedLogSession(session);
    if (path.empty()) {
        return;
    }

    session.file = OpenFile(path, "a");
    if (session.file != nullptr) {
        static char session_buffer[64 * 1024];
        std::setvbuf(session.file, session_buffer, _IOFBF, sizeof(session_buffer));
    }
}

void EndBufferedLogSession(BufferedLogSession &session)
{
    if (session.file != nullptr) {
        std::fflush(session.file);
        std::fclose(session.file);
        session.file = nullptr;
    }
}

bool OpenFileInNotepad(const std::string &path, unsigned long *error_code_out)
{
    if (error_code_out != nullptr) {
        *error_code_out = 0;
    }
    if (path.empty()) {
        if (error_code_out != nullptr) {
            *error_code_out = ERROR_INVALID_PARAMETER;
        }
        return false;
    }

    std::string cmd = "notepad.exe \"" + path + "\"";
    std::vector<char> cmdline(cmd.begin(), cmd.end());
    cmdline.push_back('\0');

    STARTUPINFOA si;
    std::memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi;
    std::memset(&pi, 0, sizeof(pi));

    const BOOL ok = CreateProcessA(
        nullptr,
        cmdline.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_NEW_PROCESS_GROUP,
        nullptr,
        nullptr,
        &si,
        &pi);
    if (!ok) {
        if (error_code_out != nullptr) {
            *error_code_out = static_cast<unsigned long>(GetLastError());
        }
        return false;
    }

    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

} // namespace autobbox::common
