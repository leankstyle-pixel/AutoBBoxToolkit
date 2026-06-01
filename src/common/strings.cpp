#include "autobbox/common/strings.h"

#include <ProToolkit.h>
#include <ProUtil.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace autobbox::common {

std::string WToA(const wchar_t *value)
{
    if (value == nullptr) {
        return std::string();
    }
    char buffer[PRO_PATH_SIZE] = {0};
    ProWstringToString(buffer, const_cast<wchar_t *>(value));
    return std::string(buffer);
}

std::wstring AToW(const char *value)
{
    if (value == nullptr) {
        return std::wstring();
    }
    wchar_t buffer[PRO_PATH_SIZE] = {0};
    ProStringToWstring(buffer, const_cast<char *>(value));
    return std::wstring(buffer);
}

std::string WideToUtf8(const std::wstring &value)
{
    if (value.empty()) {
        return std::string();
    }

    const int size = WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        nullptr,
        0,
        nullptr,
        nullptr);
    if (size <= 0) {
        return std::string();
    }

    std::string out(static_cast<size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        value.c_str(),
        static_cast<int>(value.size()),
        &out[0],
        size,
        nullptr,
        nullptr);
    return out;
}

} // namespace autobbox::common
