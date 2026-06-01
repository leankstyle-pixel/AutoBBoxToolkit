#pragma once

#include <string>

namespace autobbox::common {

std::string WToA(const wchar_t *value);
std::wstring AToW(const char *value);
std::string WideToUtf8(const std::wstring &value);

} // namespace autobbox::common
