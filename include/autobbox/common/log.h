#pragma once

#include <cstdio>
#include <cstdarg>
#include <string>

namespace autobbox::common {

struct BufferedLogSession {
    std::FILE *file = nullptr;
};

bool AppendFormattedLine(const std::string &path,
                         const char *prefix,
                         const char *fmt,
                         va_list args);
bool AppendFormattedLine(const std::string &path,
                         std::FILE *session_file,
                         const char *prefix,
                         const char *fmt,
                         va_list args);
void BeginBufferedLogSession(BufferedLogSession &session, const std::string &path);
void EndBufferedLogSession(BufferedLogSession &session);
bool OpenFileInNotepad(const std::string &path, unsigned long *error_code_out = nullptr);

} // namespace autobbox::common
