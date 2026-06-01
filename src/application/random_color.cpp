#include "autobbox/application/random_color.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/creo/parameter_api.h"

#include <ProAsmcomp.h>
#include <ProAsmcomppath.h>
#include <ProArray.h>
#include <ProMdl.h>
#include <ProObjects.h>
#include <ProParameter.h>
#include <ProSolid.h>
#include <ProSurface.h>
#include <ProToolkit.h>
#include <ProUtil.h>
#include <ProWindows.h>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <numeric>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace autobbox::application {

namespace {

constexpr wchar_t kSystemAppearanceDirectory[] =
    L"D:\\Program Files\\PTC\\Creo 10.0.8.0\\Common Files\\graphic-library\\appearances";

bool PathIsDmtFile(const std::filesystem::path &path)
{
    return std::filesystem::exists(path) &&
           std::filesystem::is_regular_file(path) &&
           path.extension() == ".dmt";
}

std::string TrimAscii(const std::string &value)
{
    size_t begin = 0;
    while (begin < value.size() &&
           std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin &&
           std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool StartsWith(const std::string &value, const char *prefix)
{
    if (prefix == nullptr) {
        return false;
    }
    const std::string prefix_text(prefix);
    return value.size() >= prefix_text.size() &&
           value.compare(0, prefix_text.size(), prefix_text) == 0;
}

std::string ParseQuotedValue(const std::string &line)
{
    const size_t first_quote = line.find('"');
    if (first_quote == std::string::npos) {
        return std::string();
    }
    const size_t second_quote = line.find('"', first_quote + 1);
    if (second_quote == std::string::npos || second_quote <= first_quote + 1) {
        return std::string();
    }
    return line.substr(first_quote + 1, second_quote - first_quote - 1);
}

bool ParseDiffuseColor(const std::string &line, double &red, double &green, double &blue)
{
    if (!StartsWith(line, "diffuse_color")) {
        return false;
    }

    double alpha = 1.0;
    return sscanf_s(
               line.c_str(),
               "diffuse_color %lf %lf %lf %lf",
               &red,
               &green,
               &blue,
               &alpha) >= 3;
}

bool ParseRgbLine(const std::string &line,
                  const char *prefix,
                  double &red,
                  double &green,
                  double &blue)
{
    if (!StartsWith(line, prefix)) {
        return false;
    }

    double alpha = 1.0;
    std::string format(prefix);
    format += " %lf %lf %lf %lf";
    return sscanf_s(line.c_str(), format.c_str(), &red, &green, &blue, &alpha) >= 3;
}

bool ParseFloatLine(const std::string &line, const char *prefix, double &value)
{
    if (!StartsWith(line, prefix)) {
        return false;
    }

    std::string format(prefix);
    format += " %lf";
    return sscanf_s(line.c_str(), format.c_str(), &value) == 1;
}

std::wstring TrimWide(const std::wstring &value)
{
    size_t begin = 0;
    while (begin < value.size() && std::iswspace(value[begin]) != 0) {
        ++begin;
    }

    size_t end = value.size();
    while (end > begin && std::iswspace(value[end - 1]) != 0) {
        --end;
    }
    std::wstring trimmed = value.substr(begin, end - begin);
    if (trimmed.size() >= 2 &&
        ((trimmed.front() == L'"' && trimmed.back() == L'"') ||
         (trimmed.front() == L'\'' && trimmed.back() == L'\''))) {
        trimmed = trimmed.substr(1, trimmed.size() - 2);
    }
    return trimmed;
}

std::wstring NormalizeColorMatchKey(const std::wstring &value)
{
    std::wstring key = TrimWide(value);
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towupper(ch));
    });
    return key;
}

core::RandomColorEntry MakeColorEntry(const char *id,
                                      const wchar_t *display_name,
                                      double red,
                                      double green,
                                      double blue)
{
    core::RandomColorEntry color;
    color.id = id == nullptr ? std::string() : std::string(id);
    color.material_name = display_name == nullptr ? std::wstring() : std::wstring(display_name);
    color.display_name = color.material_name;
    color.red = red;
    color.green = green;
    color.blue = blue;
    color.highlight_red = std::min(1.0, red * 0.8 + 0.2);
    color.highlight_green = std::min(1.0, green * 0.8 + 0.2);
    color.highlight_blue = std::min(1.0, blue * 0.8 + 0.2);
    color.has_highlight_color = true;
    return color;
}

std::vector<std::pair<std::vector<std::wstring>, core::RandomColorEntry>> PresetColorMap()
{
    return {
        {{L"\u7ea2", L"\u7ea2\u8272", L"RED"}, MakeColorEntry("preset::red", L"\u7ea2\u8272", 1.0, 0.0, 0.0)},
        {{L"\u9ec4", L"\u9ec4\u8272", L"YELLOW"}, MakeColorEntry("preset::yellow", L"\u9ec4\u8272", 1.0, 0.85, 0.0)},
        {{L"\u84dd", L"\u84dd\u8272", L"BLUE"}, MakeColorEntry("preset::blue", L"\u84dd\u8272", 0.0, 0.25, 1.0)},
        {{L"\u7eff", L"\u7eff\u8272", L"GREEN"}, MakeColorEntry("preset::green", L"\u7eff\u8272", 0.0, 0.65, 0.0)},
        {{L"\u9ed1", L"\u9ed1\u8272", L"BLACK"}, MakeColorEntry("preset::black", L"\u9ed1\u8272", 0.02, 0.02, 0.02)},
        {{L"\u767d", L"\u767d\u8272", L"WHITE"}, MakeColorEntry("preset::white", L"\u767d\u8272", 1.0, 1.0, 1.0)},
        {{L"\u7070", L"\u7070\u8272", L"GRAY", L"GREY"}, MakeColorEntry("preset::gray", L"\u7070\u8272", 0.5, 0.5, 0.5)},
        {{L"\u6a59", L"\u6a59\u8272", L"ORANGE"}, MakeColorEntry("preset::orange", L"\u6a59\u8272", 1.0, 0.45, 0.0)},
        {{L"\u7d2b", L"\u7d2b\u8272", L"PURPLE", L"VIOLET"}, MakeColorEntry("preset::purple", L"\u7d2b\u8272", 0.55, 0.15, 0.85)},
    };
}

bool TryFindPresetColor(const std::wstring &parameter_value, core::RandomColorEntry &color_out)
{
    const std::wstring key = NormalizeColorMatchKey(parameter_value);
    if (key.empty()) {
        return false;
    }

    for (const auto &preset : PresetColorMap()) {
        for (const std::wstring &alias : preset.first) {
            if (NormalizeColorMatchKey(alias) == key) {
                color_out = preset.second;
                return true;
            }
        }
    }
    return false;
}

bool TryFindLibraryColor(const std::wstring &parameter_value,
                         const std::vector<core::RandomColorEntry> &library_colors,
                         core::RandomColorEntry &color_out)
{
    const std::wstring key = NormalizeColorMatchKey(parameter_value);
    if (key.empty()) {
        return false;
    }

    for (const core::RandomColorEntry &color : library_colors) {
        if (!color.material_name.empty() && NormalizeColorMatchKey(color.material_name) == key) {
            color_out = color;
            return true;
        }
        if (!color.display_name.empty() && NormalizeColorMatchKey(color.display_name) == key) {
            color_out = color;
            return true;
        }
    }
    return false;
}

bool TryReadModelParameterValue(ProMdl mdl,
                                const std::wstring &parameter_name,
                                std::wstring &value_out,
                                core::RandomColorSkipReason &reason_out)
{
    value_out.clear();
    reason_out = core::RandomColorSkipReason::None;
    if (mdl == nullptr || TrimWide(parameter_name).empty()) {
        reason_out = core::RandomColorSkipReason::ReadError;
        return false;
    }

    ProModelitem owner = {};
    if (ProMdlToModelitem(mdl, &owner) != PRO_TK_NO_ERROR) {
        reason_out = core::RandomColorSkipReason::ReadError;
        return false;
    }

    ProName pname = {0};
    const std::wstring normalized = autobbox::creo::NormalizeParameterName(parameter_name);
    wcsncpy_s(pname, normalized.c_str(), _TRUNCATE);

    ProParameter param = {};
    if (ProParameterInit(&owner, pname, &param) != PRO_TK_NO_ERROR) {
        reason_out = core::RandomColorSkipReason::MissingParameter;
        return false;
    }

    ProParamvalueType type = PRO_PARAM_NOT_SET;
    if (!autobbox::creo::ReadParameterDisplayValue(&param, type, value_out)) {
        reason_out = core::RandomColorSkipReason::ReadError;
        return false;
    }
    value_out = TrimWide(value_out);
    if (value_out.empty() || value_out == L"\"\"") {
        reason_out = core::RandomColorSkipReason::EmptyParameter;
        return false;
    }
    return true;
}

const wchar_t *SkipReasonText(core::RandomColorSkipReason reason)
{
    switch (reason) {
    case core::RandomColorSkipReason::MissingParameter:
        return L"\u7f3a\u5c11\u53c2\u6570";
    case core::RandomColorSkipReason::EmptyParameter:
        return L"\u53c2\u6570\u503c\u4e3a\u7a7a";
    case core::RandomColorSkipReason::NoMatch:
        return L"\u672a\u5339\u914d\u989c\u8272";
    case core::RandomColorSkipReason::ReadError:
        return L"\u53c2\u6570\u8bfb\u53d6\u5931\u8d25";
    case core::RandomColorSkipReason::None:
    default:
        return L"";
    }
}

bool DirectoryContainsDmtFiles(const std::filesystem::path &path)
{
    if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) {
        return false;
    }

    for (const auto &entry : std::filesystem::directory_iterator(path)) {
        if (entry.is_regular_file() && entry.path().extension() == ".dmt") {
            return true;
        }
    }
    return false;
}

std::wstring FindFirstAppearanceDirectory(const std::wstring &base_directory)
{
    if (base_directory.empty()) {
        return std::wstring();
    }

    const std::filesystem::path root(base_directory);
    if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
        return std::wstring();
    }
    if (DirectoryContainsDmtFiles(root)) {
        return base_directory;
    }

    std::error_code ec;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec) {
            break;
        }
        if (!entry.is_directory()) {
            continue;
        }
        const std::wstring folder_name = entry.path().filename().wstring();
        if (folder_name.find(L"appearance") != std::wstring::npos ||
            folder_name.find(L"Appearance") != std::wstring::npos ||
            folder_name.find(L"config") != std::wstring::npos) {
            if (DirectoryContainsDmtFiles(entry.path())) {
                return entry.path().wstring();
            }
        }
    }

    for (const auto &entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_directory() && DirectoryContainsDmtFiles(entry.path())) {
            return entry.path().wstring();
        }
    }

    return std::wstring();
}

std::wstring ResolveConfigDirectoryOption(const wchar_t *option_name)
{
    if (option_name == nullptr || option_name[0] == L'\0') {
        return std::wstring();
    }

    ProName option = {0};
    wcsncpy_s(option, option_name, _TRUNCATE);
    ProPath value = {0};
    if (ProConfigoptionGet(option, value) != PRO_TK_NO_ERROR || value[0] == L'\0') {
        return std::wstring();
    }

    return FindFirstAppearanceDirectory(value);
}

std::wstring ResolveConfigLibraryPathOption(const wchar_t *option_name)
{
    if (option_name == nullptr || option_name[0] == L'\0') {
        return std::wstring();
    }

    ProName option = {0};
    wcsncpy_s(option, option_name, _TRUNCATE);
    ProPath value = {0};
    if (ProConfigoptionGet(option, value) != PRO_TK_NO_ERROR || value[0] == L'\0') {
        return std::wstring();
    }

    const std::filesystem::path config_path(value);
    if (PathIsDmtFile(config_path)) {
        return config_path.wstring();
    }

    if (!std::filesystem::exists(config_path) || !std::filesystem::is_directory(config_path)) {
        return std::wstring();
    }

    std::vector<std::filesystem::path> files;
    std::error_code ec;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(config_path, ec)) {
        if (ec) {
            break;
        }
        if (PathIsDmtFile(entry.path())) {
            files.push_back(entry.path());
        }
    }

    std::sort(files.begin(), files.end());
    return files.empty() ? std::wstring() : files.front().wstring();
}

const wchar_t *ModelTypeShortLabel(ProMdlType type)
{
    switch (type) {
    case PRO_MDL_PART:
        return L"PRT";
    case PRO_MDL_ASSEMBLY:
        return L"ASM";
    default:
        return L"MDL";
    }
}

std::wstring MakeCandidateLabel(const core::RandomColorCandidate &candidate)
{
    std::wstring label = candidate.model_name;
    label += L" (";
    label += ModelTypeShortLabel(candidate.type);
    label += L", x";
    label += std::to_wstring(candidate.occurrences.size());
    label += L")";
    return label;
}

bool ParseCreoAppearanceFile(const std::filesystem::path &path,
                             std::vector<core::RandomColorEntry> &colors_out)
{
    std::ifstream input(path);
    if (!input.is_open()) {
        return false;
    }

    struct CurrentEntry {
        std::string id;
        std::wstring material_name;
        std::wstring display_name;
        std::wstring description;
        std::wstring keywords;
        double red = 0.0;
        double green = 0.0;
        double blue = 0.0;
        double highlight_red = 1.0;
        double highlight_green = 1.0;
        double highlight_blue = 1.0;
        double ambient = 0.6;
        double diffuse = 0.9;
        double highlite = 0.6;
        double shininess = 0.6;
        double transparency = 0.0;
        double reflection = 0.3;
        bool has_color = false;
        bool has_highlight_color = false;
    };

    auto flush_entry = [&](CurrentEntry &entry) {
        if (entry.id.empty() || !entry.has_color) {
            entry = CurrentEntry();
            return;
        }

        core::RandomColorEntry color;
        color.id = path.stem().string() + "::" + entry.id;
        color.material_name = entry.material_name.empty()
                                  ? autobbox::common::AToW(entry.id.c_str())
                                  : entry.material_name;
        color.display_name = entry.display_name.empty()
                                 ? autobbox::common::AToW(entry.id.c_str())
                                 : entry.display_name;
        color.description = entry.description;
        color.keywords = entry.keywords;
        color.red = entry.red;
        color.green = entry.green;
        color.blue = entry.blue;
        color.highlight_red = entry.highlight_red;
        color.highlight_green = entry.highlight_green;
        color.highlight_blue = entry.highlight_blue;
        color.ambient = entry.ambient;
        color.diffuse = entry.diffuse;
        color.highlite = entry.highlite;
        color.shininess = entry.shininess;
        color.transparency = entry.transparency;
        color.reflection = entry.reflection;
        color.has_highlight_color = entry.has_highlight_color;
        colors_out.push_back(color);
        entry = CurrentEntry();
    };

    CurrentEntry current;
    std::string line;
    while (std::getline(input, line)) {
        const std::string trimmed = TrimAscii(line);
        if (StartsWith(trimmed, "material_name")) {
            flush_entry(current);
            current.id = ParseQuotedValue(trimmed);
            current.material_name = autobbox::common::AToW(current.id.c_str());
            continue;
        }
        if (StartsWith(trimmed, "material_label")) {
            current.display_name = autobbox::common::AToW(ParseQuotedValue(trimmed).c_str());
            continue;
        }
        if (StartsWith(trimmed, "material_description")) {
            current.description = autobbox::common::AToW(ParseQuotedValue(trimmed).c_str());
            continue;
        }
        if (StartsWith(trimmed, "material_keywords")) {
            current.keywords = autobbox::common::AToW(ParseQuotedValue(trimmed).c_str());
            continue;
        }

        double red = 0.0;
        double green = 0.0;
        double blue = 0.0;
        if (ParseDiffuseColor(trimmed, red, green, blue)) {
            current.red = red;
            current.green = green;
            current.blue = blue;
            current.has_color = true;
        }
        if (ParseRgbLine(trimmed, "specular_color", red, green, blue)) {
            current.highlight_red = red;
            current.highlight_green = green;
            current.highlight_blue = blue;
            current.has_highlight_color = true;
            continue;
        }
        double value = 0.0;
        if (ParseFloatLine(trimmed, "ambient", value)) {
            current.ambient = value;
            continue;
        }
        if (ParseFloatLine(trimmed, "diffuse", value)) {
            current.diffuse = value;
            continue;
        }
        if (ParseFloatLine(trimmed, "intensity", value)) {
            current.highlite = value;
            continue;
        }
        if (ParseFloatLine(trimmed, "shine", value)) {
            current.shininess = value;
            continue;
        }
        if (ParseFloatLine(trimmed, "transparency", value)) {
            current.transparency = value;
            continue;
        }
        if (ParseFloatLine(trimmed, "reflection", value)) {
            current.reflection = value;
            continue;
        }
    }
    flush_entry(current);
    return !colors_out.empty();
}

std::vector<std::filesystem::path> CollectAppearanceFiles(const std::wstring &directory)
{
    std::vector<std::filesystem::path> files;
    if (directory.empty()) {
        return files;
    }

    const std::filesystem::path root(directory);
    if (PathIsDmtFile(root)) {
        files.push_back(root);
        return files;
    }
    if (!std::filesystem::exists(root) || !std::filesystem::is_directory(root)) {
        return files;
    }

    std::error_code ec;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root, ec)) {
        if (ec) {
            break;
        }
        if (entry.is_regular_file() && entry.path().extension() == ".dmt") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());
    return files;
}

struct RandomColorTraverseContext {
    std::vector<core::RandomColorCandidate> candidates;
    std::unordered_map<std::uintptr_t, size_t> index_by_model;
    bool want_parts = true;
    bool want_assemblies = true;
    bool top_level_only = false;
};

bool AcceptCandidateType(ProMdl mdl, const RandomColorTraverseContext &ctx)
{
    const ProMdlType type = autobbox::creo::ModelType(mdl);
    return (type == PRO_MDL_PART && ctx.want_parts) ||
           (type == PRO_MDL_ASSEMBLY && ctx.want_assemblies);
}

ProError CollectRandomColorVisitAction(ProAsmcomppath *path,
                                       ProSolid handle,
                                       ProBoolean down,
                                       ProAppData app_data)
{
    if (down != PRO_B_TRUE || path == nullptr || app_data == nullptr) {
        return PRO_TK_NO_ERROR;
    }

    auto *ctx = reinterpret_cast<RandomColorTraverseContext *>(app_data);
    const int depth = path->table_num;
    if (depth <= 0) {
        return PRO_TK_NO_ERROR;
    }
    if (ctx->top_level_only && depth != 1) {
        return PRO_TK_NO_ERROR;
    }

    ProMdl mdl = reinterpret_cast<ProMdl>(handle);
    if (!AcceptCandidateType(mdl, *ctx)) {
        return PRO_TK_NO_ERROR;
    }

    const std::uintptr_t key = reinterpret_cast<std::uintptr_t>(mdl);
    size_t index = 0;
    const auto it = ctx->index_by_model.find(key);
    if (it == ctx->index_by_model.end()) {
        core::RandomColorCandidate candidate;
        candidate.mdl = mdl;
        candidate.type = autobbox::creo::ModelType(mdl);
        candidate.model_name = autobbox::creo::ModelName(mdl, L"");
        candidate.item_name = "random_color_" + std::to_string(ctx->candidates.size());
        ctx->candidates.push_back(std::move(candidate));
        index = ctx->candidates.size() - 1;
        ctx->index_by_model.emplace(key, index);
    } else {
        index = it->second;
    }

    core::RandomColorOccurrence occurrence;
    occurrence.path = *path;
    occurrence.feat_id = path->comp_id_table[depth - 1];
    ctx->candidates[index].occurrences.push_back(occurrence);
    return PRO_TK_NO_ERROR;
}

ProSurfaceAppearanceProps BuildAppearanceProps(const core::RandomColorEntry &color)
{
    ProSurfaceAppearanceProps props = {};
    if (ProSurfaceAppearanceDefaultPropsGet(PRO_DEF_APPEARANCE_SOLID, &props) != PRO_TK_NO_ERROR) {
        props.ambient = 0.6;
        props.diffuse = 0.9;
        props.highlite = 0.6;
        props.shininess = 0.6;
        props.transparency = 0.0;
        props.reflection = 0.3;
    }

    props.color_rgb[0] = color.red;
    props.color_rgb[1] = color.green;
    props.color_rgb[2] = color.blue;
    props.highlight_color[0] = color.has_highlight_color ? color.highlight_red : std::min(1.0, color.red * 0.8 + 0.2);
    props.highlight_color[1] = color.has_highlight_color ? color.highlight_green : std::min(1.0, color.green * 0.8 + 0.2);
    props.highlight_color[2] = color.has_highlight_color ? color.highlight_blue : std::min(1.0, color.blue * 0.8 + 0.2);
    props.ambient = color.ambient;
    props.diffuse = color.diffuse;
    props.highlite = color.highlite;
    props.shininess = color.shininess;
    props.transparency = color.transparency;
    props.reflection = color.reflection;

    const std::wstring entry_id = autobbox::common::AToW(color.id.c_str());
    wcsncpy_s(props.name, entry_id.c_str(), _TRUNCATE);
    wcsncpy_s(props.label, color.display_name.c_str(), _TRUNCATE);
    wcsncpy_s(props.description, color.description.c_str(), _TRUNCATE);
    wcsncpy_s(props.keywords, color.keywords.c_str(), _TRUNCATE);
    return props;
}

std::wstring FirstNonEmptyAppearanceText(const ProSurfaceAppearanceProps &props)
{
    const std::vector<std::wstring> values = {
        TrimWide(props.label),
        TrimWide(props.name),
        TrimWide(props.description),
    };
    for (const std::wstring &value : values) {
        if (!value.empty()) {
            return value;
        }
    }
    return std::wstring();
}

std::wstring MakeRgbAppearanceLabel(const ProSurfaceAppearanceProps &props)
{
    const int red = static_cast<int>(std::clamp(props.color_rgb[0] * 255.0, 0.0, 255.0));
    const int green = static_cast<int>(std::clamp(props.color_rgb[1] * 255.0, 0.0, 255.0));
    const int blue = static_cast<int>(std::clamp(props.color_rgb[2] * 255.0, 0.0, 255.0));

    wchar_t buffer[64] = {0};
    swprintf_s(buffer, L"RGB(%d,%d,%d)", red, green, blue);
    return buffer;
}

core::RandomColorEntry MakeColorEntryFromAppearanceProps(const ProSurfaceAppearanceProps &props)
{
    core::RandomColorEntry color;
    const std::wstring appearance_name = TrimWide(props.name);
    const std::wstring appearance_label = FirstNonEmptyAppearanceText(props);
    color.id = appearance_name.empty()
                   ? "model-appearance::rgb"
                   : "model-appearance::" + autobbox::common::WToA(appearance_name.c_str());
    color.material_name = appearance_name;
    color.display_name = appearance_label.empty() ? MakeRgbAppearanceLabel(props) : appearance_label;
    color.description = TrimWide(props.description);
    color.keywords = TrimWide(props.keywords);
    color.red = props.color_rgb[0];
    color.green = props.color_rgb[1];
    color.blue = props.color_rgb[2];
    color.highlight_red = props.highlight_color[0];
    color.highlight_green = props.highlight_color[1];
    color.highlight_blue = props.highlight_color[2];
    color.ambient = props.ambient;
    color.diffuse = props.diffuse;
    color.highlite = props.highlite;
    color.shininess = props.shininess;
    color.transparency = props.transparency;
    color.reflection = props.reflection;
    color.has_highlight_color = true;
    return color;
}

bool NearlyEqual(double lhs, double rhs)
{
    return std::fabs(lhs - rhs) <= 0.0005;
}

bool AppearancePropsEquivalent(const ProSurfaceAppearanceProps &lhs,
                               const ProSurfaceAppearanceProps &rhs)
{
    return NearlyEqual(lhs.ambient, rhs.ambient) &&
           NearlyEqual(lhs.diffuse, rhs.diffuse) &&
           NearlyEqual(lhs.highlite, rhs.highlite) &&
           NearlyEqual(lhs.shininess, rhs.shininess) &&
           NearlyEqual(lhs.transparency, rhs.transparency) &&
           NearlyEqual(lhs.reflection, rhs.reflection) &&
           NearlyEqual(lhs.color_rgb[0], rhs.color_rgb[0]) &&
           NearlyEqual(lhs.color_rgb[1], rhs.color_rgb[1]) &&
           NearlyEqual(lhs.color_rgb[2], rhs.color_rgb[2]) &&
           NearlyEqual(lhs.highlight_color[0], rhs.highlight_color[0]) &&
           NearlyEqual(lhs.highlight_color[1], rhs.highlight_color[1]) &&
           NearlyEqual(lhs.highlight_color[2], rhs.highlight_color[2]) &&
           TrimWide(lhs.name) == TrimWide(rhs.name) &&
           TrimWide(lhs.label) == TrimWide(rhs.label);
}

bool TryReadOccurrenceAppearance(ProMdl owner,
                                 const core::RandomColorCandidate &candidate,
                                 const core::RandomColorOccurrence &occurrence,
                                 ProSurfaceAppearanceProps &props_out)
{
    if (owner == nullptr) {
        return false;
    }

    const ProType item_type = (candidate.type == PRO_MDL_ASSEMBLY) ? PRO_ASSEMBLY : PRO_PART;
    ProAsmitem asm_item = {};
    ProName item_name = {0};
    ProAsmcomppath path = occurrence.path;
    if (ProAsmcompAsmitemInit(owner,
                              occurrence.feat_id,
                              item_type,
                              item_name,
                              &path,
                              &asm_item) != PRO_TK_NO_ERROR) {
        return false;
    }

    props_out = {};
    ProError status = ProMdlVisibleSideAppearancepropsGet(&asm_item, 0, &props_out);
    if (status == PRO_TK_NO_ERROR) {
        return true;
    }

    props_out = {};
    status = ProMdlVisibleSideAppearancepropsGet(&asm_item, 1, &props_out);
    return status == PRO_TK_NO_ERROR;
}

void PopulateCurrentAppearance(ProMdl owner, core::RandomColorCandidate &candidate)
{
    candidate.has_current_appearance = false;
    candidate.has_mixed_current_appearance = false;
    candidate.current_appearance = core::RandomColorEntry();
    candidate.current_appearance_label.clear();

    bool have_first = false;
    ProSurfaceAppearanceProps first_props = {};
    for (const core::RandomColorOccurrence &occurrence : candidate.occurrences) {
        ProSurfaceAppearanceProps props = {};
        if (!TryReadOccurrenceAppearance(owner, candidate, occurrence, props)) {
            continue;
        }

        if (!have_first) {
            first_props = props;
            candidate.current_appearance = MakeColorEntryFromAppearanceProps(props);
            candidate.current_appearance_label = candidate.current_appearance.display_name;
            candidate.has_current_appearance = true;
            have_first = true;
            continue;
        }

        if (!AppearancePropsEquivalent(first_props, props)) {
            candidate.has_mixed_current_appearance = true;
        }
    }

    if (candidate.has_mixed_current_appearance) {
        candidate.current_appearance_label = L"\u591a\u79cd\u5916\u89c2";
    }
}

std::string ToDmtText(const std::wstring &value)
{
    return autobbox::common::WToA(value.c_str());
}

std::string EscapeDmtQuotedString(const std::wstring &value)
{
    std::string text = ToDmtText(value);
    text.erase(
        std::remove_if(text.begin(), text.end(), [](unsigned char ch) {
            return ch == '\r' || ch == '\n' || ch == '\0';
        }),
        text.end());
    for (char &ch : text) {
        if (ch == '"') {
            ch = '\'';
        }
    }
    return text;
}

std::string SanitizeDmtIdentifier(const std::wstring &value)
{
    std::string text = ToDmtText(value);
    std::string out;
    out.reserve(text.size());
    bool last_was_separator = false;
    for (unsigned char raw_ch : text) {
        char ch = static_cast<char>(raw_ch);
        if (std::isalnum(raw_ch) != 0) {
            out.push_back(static_cast<char>(std::tolower(raw_ch)));
            last_was_separator = false;
        } else if (!last_was_separator) {
            out.push_back('_');
            last_was_separator = true;
        }
    }
    while (!out.empty() && out.front() == '_') {
        out.erase(out.begin());
    }
    while (!out.empty() && out.back() == '_') {
        out.pop_back();
    }
    return out;
}

std::string RgbSuffix(const core::RandomColorEntry &color)
{
    const int red = static_cast<int>(std::clamp(color.red * 255.0, 0.0, 255.0));
    const int green = static_cast<int>(std::clamp(color.green * 255.0, 0.0, 255.0));
    const int blue = static_cast<int>(std::clamp(color.blue * 255.0, 0.0, 255.0));

    char buffer[32] = {0};
    std::snprintf(buffer, sizeof(buffer), "%02x%02x%02x", red, green, blue);
    return buffer;
}

std::wstring RgbParameterValue(const core::RandomColorEntry &color)
{
    const int red = static_cast<int>(std::clamp(color.red * 255.0, 0.0, 255.0));
    const int green = static_cast<int>(std::clamp(color.green * 255.0, 0.0, 255.0));
    const int blue = static_cast<int>(std::clamp(color.blue * 255.0, 0.0, 255.0));

    wchar_t buffer[64] = {0};
    swprintf_s(buffer, L"RGB(%d,%d,%d)", red, green, blue);
    return buffer;
}

std::wstring StripAppearanceLibraryPrefix(const std::wstring &name)
{
    std::wstring value = TrimWide(name);
    if (value.empty()) {
        return value;
    }

    const size_t double_colon = value.rfind(L"::");
    if (double_colon != std::wstring::npos && double_colon + 2 < value.size()) {
        value = value.substr(double_colon + 2);
    }

    const size_t slash = value.find_last_of(L"\\/");
    if (slash != std::wstring::npos && slash + 1 < value.size()) {
        value = value.substr(slash + 1);
    }

    const size_t colon = value.rfind(L':');
    if (colon != std::wstring::npos && colon + 1 < value.size()) {
        const std::wstring prefix = value.substr(0, colon);
        const bool prefix_looks_like_library =
            prefix.find(L".dmt") != std::wstring::npos ||
            prefix.find(L".DMT") != std::wstring::npos ||
            prefix.find(L'\\') != std::wstring::npos ||
            prefix.find(L'/') != std::wstring::npos ||
            prefix.find(L' ') == std::wstring::npos;
        if (prefix_looks_like_library) {
            value = value.substr(colon + 1);
        }
    }

    return TrimWide(value);
}

std::wstring ColorParameterValueFromAppearance(const core::RandomColorCandidate &candidate)
{
    if (!candidate.current_appearance.material_name.empty()) {
        const std::wstring value = StripAppearanceLibraryPrefix(candidate.current_appearance.material_name);
        if (!value.empty()) {
            return value;
        }
    }
    if (!candidate.current_appearance.display_name.empty()) {
        const std::wstring value = StripAppearanceLibraryPrefix(candidate.current_appearance.display_name);
        if (!value.empty()) {
            return value;
        }
    }
    if (!candidate.current_appearance_label.empty()) {
        const std::wstring value = StripAppearanceLibraryPrefix(candidate.current_appearance_label);
        if (!value.empty()) {
            return value;
        }
    }
    return RgbParameterValue(candidate.current_appearance);
}

bool EntryAppearanceEquivalent(const core::RandomColorEntry &lhs,
                               const core::RandomColorEntry &rhs)
{
    return NearlyEqual(lhs.ambient, rhs.ambient) &&
           NearlyEqual(lhs.diffuse, rhs.diffuse) &&
           NearlyEqual(lhs.highlite, rhs.highlite) &&
           NearlyEqual(lhs.shininess, rhs.shininess) &&
           NearlyEqual(lhs.transparency, rhs.transparency) &&
           NearlyEqual(lhs.reflection, rhs.reflection) &&
           NearlyEqual(lhs.red, rhs.red) &&
           NearlyEqual(lhs.green, rhs.green) &&
           NearlyEqual(lhs.blue, rhs.blue) &&
           NearlyEqual(lhs.highlight_red, rhs.highlight_red) &&
           NearlyEqual(lhs.highlight_green, rhs.highlight_green) &&
           NearlyEqual(lhs.highlight_blue, rhs.highlight_blue);
}

bool EntryDuplicateExists(const core::RandomColorEntry &candidate,
                          const std::vector<core::RandomColorEntry> &entries)
{
    const std::wstring material_key = NormalizeColorMatchKey(candidate.material_name);
    const std::wstring display_key = NormalizeColorMatchKey(candidate.display_name);
    for (const core::RandomColorEntry &entry : entries) {
        if (!material_key.empty() && NormalizeColorMatchKey(entry.material_name) == material_key) {
            return true;
        }
        if (!display_key.empty() && NormalizeColorMatchKey(entry.display_name) == display_key) {
            return true;
        }
        if (EntryAppearanceEquivalent(candidate, entry)) {
            return true;
        }
    }
    return false;
}

const core::RandomColorEntry *FindDuplicateEntry(const core::RandomColorEntry &candidate,
                                                 const std::vector<core::RandomColorEntry> &entries)
{
    const std::wstring material_key = NormalizeColorMatchKey(candidate.material_name);
    const std::wstring display_key = NormalizeColorMatchKey(candidate.display_name);
    for (const core::RandomColorEntry &entry : entries) {
        if (!material_key.empty() && NormalizeColorMatchKey(entry.material_name) == material_key) {
            return &entry;
        }
        if (!display_key.empty() && NormalizeColorMatchKey(entry.display_name) == display_key) {
            return &entry;
        }
        if (EntryAppearanceEquivalent(candidate, entry)) {
            return &entry;
        }
    }
    return nullptr;
}

std::set<std::string> ExistingDmtIdentifiers(const std::vector<core::RandomColorEntry> &entries)
{
    std::set<std::string> ids;
    for (const core::RandomColorEntry &entry : entries) {
        const std::string id = SanitizeDmtIdentifier(entry.material_name);
        if (!id.empty()) {
            ids.insert(id);
        }
    }
    return ids;
}

std::string MakeUniqueDmtIdentifier(const core::RandomColorCandidate &candidate,
                                    const core::RandomColorEntry &color,
                                    std::set<std::string> &used_ids)
{
    std::string base = SanitizeDmtIdentifier(color.material_name);
    if (base.empty()) {
        base = SanitizeDmtIdentifier(color.display_name);
    }
    if (base.empty()) {
        base = "user_color_" + SanitizeDmtIdentifier(candidate.model_name) + "_" + RgbSuffix(color);
    }
    if (base.empty()) {
        base = "user_color_" + RgbSuffix(color);
    }
    if (base.size() > 96) {
        base.resize(96);
        while (!base.empty() && base.back() == '_') {
            base.pop_back();
        }
        if (base.empty()) {
            base = "user_color_" + RgbSuffix(color);
        }
    }

    std::string id = base;
    int suffix = 2;
    while (used_ids.find(id) != used_ids.end()) {
        id = base + "_" + std::to_string(suffix++);
    }
    used_ids.insert(id);
    return id;
}

core::RandomColorEntry MakeLibraryEntryForCandidate(const core::RandomColorCandidate &candidate,
                                                    std::set<std::string> &used_ids)
{
    core::RandomColorEntry entry = candidate.current_appearance;
    const std::string id = MakeUniqueDmtIdentifier(candidate, entry, used_ids);
    entry.id = id;
    entry.material_name = autobbox::common::AToW(id.c_str());
    if (TrimWide(entry.display_name).empty()) {
        entry.display_name = candidate.current_appearance_label.empty()
                                 ? candidate.model_name
                                 : candidate.current_appearance_label;
    }
    if (TrimWide(entry.description).empty()) {
        entry.description = L"AutoBBoxToolkit user appearance";
    }
    return entry;
}

bool IsAppendableDmtFile(const std::filesystem::path &path)
{
    if (!PathIsDmtFile(path)) {
        return false;
    }
    std::ofstream output(path, std::ios::app);
    return output.is_open();
}

std::wstring ResolveUserAppearanceLibraryPath()
{
    const std::wstring state_dir = autobbox::common::ResolveUserStateDirectoryW();
    if (state_dir.empty() || !autobbox::common::EnsureDirectoryW(state_dir)) {
        return std::wstring();
    }
    return autobbox::common::JoinPath(state_dir, L"user_appearances.dmt");
}

std::filesystem::path ResolveAppendLibraryPath(const std::wstring &preferred_library_path,
                                               bool &used_fallback)
{
    used_fallback = false;
    const std::filesystem::path preferred(preferred_library_path);
    if (!preferred_library_path.empty() && IsAppendableDmtFile(preferred)) {
        return preferred;
    }

    used_fallback = true;
    const std::wstring fallback = ResolveUserAppearanceLibraryPath();
    return std::filesystem::path(fallback);
}

bool WriteDmtHeaderIfNeeded(std::ofstream &output, const std::filesystem::path &path)
{
    std::error_code ec;
    const bool needs_header = !std::filesystem::exists(path, ec) ||
                              std::filesystem::file_size(path, ec) == 0;
    if (!needs_header) {
        return true;
    }

    output << "V7.0\n"
           << "#\n"
           << "#\n"
           << "# PGL Version 7.0\n"
           << "#\n"
           << "#\n";
    return output.good();
}

void WriteDmtAppearanceEntry(std::ofstream &output, const core::RandomColorEntry &color)
{
    output << std::fixed << std::setprecision(6);
    output << "\n# material name\n";
    output << "material_name \"" << EscapeDmtQuotedString(color.material_name) << "\"\n";
    output << "# material label\n";
    output << "material_label \"" << EscapeDmtQuotedString(color.display_name) << "\"\n";
    output << "# material description\n";
    output << "material_description \"" << EscapeDmtQuotedString(color.description) << "\"\n";
    output << "# material keywords\n";
    output << "material_keywords \"" << EscapeDmtQuotedString(color.keywords) << "\"\n";
    output << "# ambient color\n";
    output << "ambient_color " << color.red << ' ' << color.green << ' ' << color.blue << " 1.000000\n";
    output << "# diffuse color\n";
    output << "diffuse_color " << color.red << ' ' << color.green << ' ' << color.blue << " 1.000000\n";
    output << "# specular color\n";
    output << "specular_color " << color.highlight_red << ' ' << color.highlight_green << ' ' << color.highlight_blue << " 1.000000\n";
    output << "# diffuse\n";
    output << "diffuse " << color.diffuse << "\n";
    output << "# ambient\n";
    output << "ambient " << color.ambient << "\n";
    output << "# shine\n";
    output << "shine " << color.shininess << "\n";
    output << "# shine intensity\n";
    output << "intensity " << color.highlite << "\n";
    output << "# reflection\n";
    output << "reflection " << color.reflection << "\n";
    output << "# bump_type\n";
    output << "bump_type 0\n";
    output << "# amplitude\n";
    output << "amplitude 1.000000\n";
    output << "# scale\n";
    output << "scale 0.100000\n";
    output << "# exponent\n";
    output << "exponent 3.000000\n";
    output << "# rotation\n";
    output << "rotation 0.000000\n";
    output << "# color_tex_type\n";
    output << "color_tex_type 0\n";
    output << "# color_tex_scale\n";
    output << "color_tex_scale 0.100000\n";
    output << "# color_tex_rotation\n";
    output << "color_tex_rotation 0.000000\n";
    output << "# decal_type\n";
    output << "decal_type 0\n";
    output << "# transparency\n";
    output << "transparency " << color.transparency << "\n";
    output << "# fresnel index of refraction\n";
    output << "index_of_refraction 1.000000\n";
    output << "# fresnel reflections\n";
    output << "fresnel_reflections 0\n";
}

bool ApplyAppearanceToOccurrences(ProMdl owner,
                                  const core::RandomColorCandidate &candidate,
                                  const core::RandomColorEntry *color,
                                  const std::function<void(const std::string &line)> &log_sink,
                                  int &success_count,
                                  int &failure_count)
{
    if (owner == nullptr) {
        return false;
    }

    const ProType item_type = (candidate.type == PRO_MDL_ASSEMBLY) ? PRO_ASSEMBLY : PRO_PART;
    ProSurfaceAppearanceProps props = {};
    if (color != nullptr) {
        props = BuildAppearanceProps(*color);
    }

    bool changed = false;
    for (const core::RandomColorOccurrence &occurrence : candidate.occurrences) {
        ProAsmitem asm_item = {};
        ProName item_name = {0};
        ProAsmcomppath path = occurrence.path;
        const ProError init_status = ProAsmcompAsmitemInit(
            owner,
            occurrence.feat_id,
            item_type,
            item_name,
            &path,
            &asm_item);
        if (init_status != PRO_TK_NO_ERROR) {
            ++failure_count;
            continue;
        }

        ProSurfaceAppearanceProps *props_ptr = (color != nullptr) ? &props : nullptr;
        ProError apply_status = ProMdlVisibleSideAppearancepropsSet(&asm_item, 0, props_ptr);
        if (apply_status == PRO_TK_NO_ERROR) {
            apply_status = ProMdlVisibleSideAppearancepropsSet(&asm_item, 1, props_ptr);
        }
        if (apply_status != PRO_TK_NO_ERROR) {
            ++failure_count;
            continue;
        }

        ++success_count;
        changed = true;
    }

    if (log_sink) {
        std::string line = (color != nullptr ? "random-color apply model=" : "random-color clear model=");
        line += autobbox::common::WToA(candidate.model_name.c_str());
        line += " occurrences=" + std::to_string(static_cast<int>(candidate.occurrences.size()));
        if (color != nullptr) {
            line += " color=" + autobbox::common::WToA(color->display_name.c_str());
        }
        log_sink(line);
    }
    return changed;
}

void RefreshCurrentWindow()
{
    int window_id = -1;
    if (ProWindowCurrentGet(&window_id) == PRO_TK_NO_ERROR && window_id >= 0) {
        ProWindowRefresh(window_id);
        ProWindowRepaint(window_id);
    }
}

} // namespace

std::wstring ResolveDefaultRandomColorLibraryPath()
{
    const std::vector<std::wstring> config_options = {
        L"pro_colormap_path",
        L"appearance_library_dir",
        L"pro_material_dir",
        L"pro_library_dir",
    };

    for (const std::wstring &option_name : config_options) {
        const std::wstring resolved = ResolveConfigLibraryPathOption(option_name.c_str());
        if (!resolved.empty()) {
            return resolved;
        }
    }

    std::vector<std::filesystem::path> fallback_files =
        CollectAppearanceFiles(std::wstring(kSystemAppearanceDirectory));
    return fallback_files.empty() ? std::wstring(kSystemAppearanceDirectory)
                                  : fallback_files.front().wstring();
}

std::vector<core::RandomColorEntry> LoadRandomColorEntriesFromLibraryPath(
    const std::wstring &library_path,
    std::wstring &error_text)
{
    error_text.clear();
    std::vector<core::RandomColorEntry> colors;
    const std::vector<std::filesystem::path> files = CollectAppearanceFiles(library_path);
    for (const std::filesystem::path &path : files) {
        ParseCreoAppearanceFile(path, colors);
    }

    if (colors.empty()) {
        const std::filesystem::path requested_path(library_path);
        if (library_path.empty()) {
            error_text = L"请选择一个 .dmt 颜色库文件。";
        } else if (PathIsDmtFile(requested_path)) {
            error_text = L"选中的 .dmt 文件未加载到任何颜色。";
        } else {
            error_text = L"所选路径中未找到 .dmt 颜色库文件。";
        }
    }
    return colors;
}

RandomColorLibraryAppendResult AppendCurrentAppearancesToLibrary(
    const std::vector<core::RandomColorCandidate> &candidates,
    const std::wstring &preferred_library_path,
    const std::function<void(const std::string &line)> &log_sink)
{
    RandomColorLibraryAppendResult result;
    bool used_fallback = false;
    const std::filesystem::path target_path =
        ResolveAppendLibraryPath(preferred_library_path, used_fallback);
    result.used_fallback_library = used_fallback;
    result.actual_library_path = target_path.wstring();

    if (target_path.empty()) {
        result.write_failed = true;
        return result;
    }

    std::wstring load_error;
    std::vector<core::RandomColorEntry> known_entries =
        LoadRandomColorEntriesFromLibraryPath(result.actual_library_path, load_error);
    std::set<std::string> used_ids = ExistingDmtIdentifiers(known_entries);
    std::vector<core::RandomColorEntry> entries_to_append;

    for (const core::RandomColorCandidate &candidate : candidates) {
        if (!candidate.has_current_appearance) {
            ++result.no_current_appearance_count;
            continue;
        }
        if (candidate.has_mixed_current_appearance) {
            ++result.mixed_appearance_count;
            continue;
        }

        if (EntryDuplicateExists(candidate.current_appearance, known_entries) ||
            EntryDuplicateExists(candidate.current_appearance, entries_to_append)) {
            ++result.duplicate_count;
            continue;
        }

        core::RandomColorEntry entry = MakeLibraryEntryForCandidate(candidate, used_ids);
        entries_to_append.push_back(entry);
        known_entries.push_back(entry);
    }

    if (entries_to_append.empty()) {
        if (log_sink) {
            std::string line = "random-color add-library no-op path=";
            line += autobbox::common::WToA(result.actual_library_path.c_str());
            line += " duplicates=" + std::to_string(result.duplicate_count);
            line += " no-current=" + std::to_string(result.no_current_appearance_count);
            line += " mixed=" + std::to_string(result.mixed_appearance_count);
            log_sink(line);
        }
        return result;
    }

    std::ofstream output(target_path, std::ios::app);
    if (!output.is_open() || !WriteDmtHeaderIfNeeded(output, target_path)) {
        result.write_failed = true;
        return result;
    }

    for (const core::RandomColorEntry &entry : entries_to_append) {
        WriteDmtAppearanceEntry(output, entry);
        ++result.added_count;
    }
    output.flush();
    if (!output.good()) {
        result.write_failed = true;
    }

    if (log_sink) {
        std::string line = "random-color add-library path=";
        line += autobbox::common::WToA(result.actual_library_path.c_str());
        line += " added=" + std::to_string(result.added_count);
        line += " duplicates=" + std::to_string(result.duplicate_count);
        line += " no-current=" + std::to_string(result.no_current_appearance_count);
        line += " mixed=" + std::to_string(result.mixed_appearance_count);
        line += " fallback=" + std::to_string(result.used_fallback_library ? 1 : 0);
        line += " write-failed=" + std::to_string(result.write_failed ? 1 : 0);
        log_sink(line);
    }
    return result;
}

RandomColorParameterWriteResult WriteCurrentAppearanceColorParameters(
    const std::vector<core::RandomColorCandidate> &candidates,
    const std::function<void(const std::string &line)> &log_sink)
{
    RandomColorParameterWriteResult result;
    constexpr wchar_t kColorParameterName[] = L"\u989c\u8272";

    for (const core::RandomColorCandidate &candidate : candidates) {
        if (!candidate.has_current_appearance) {
            ++result.no_current_appearance_count;
            continue;
        }
        if (candidate.has_mixed_current_appearance) {
            ++result.mixed_appearance_count;
            continue;
        }

        const std::wstring value = ColorParameterValueFromAppearance(candidate);
        const bool parameter_existed = autobbox::creo::ParameterExistsOnModel(
            candidate.mdl,
            kColorParameterName);
        const ProError st = autobbox::creo::SetStringParamOnModel(
            candidate.mdl,
            kColorParameterName,
            value);
        if (st == PRO_TK_NO_ERROR || st == PRO_TK_NO_CHANGE) {
            ++result.success_count;
            if (parameter_existed) {
                ++result.updated_count;
            } else {
                ++result.created_count;
            }
        } else {
            ++result.failure_count;
        }
    }

    if (log_sink) {
        std::string line = "random-color write-color-param param=";
        line += autobbox::common::WToA(kColorParameterName);
        line += " success=" + std::to_string(result.success_count);
        line += " created=" + std::to_string(result.created_count);
        line += " updated=" + std::to_string(result.updated_count);
        line += " failures=" + std::to_string(result.failure_count);
        line += " no-current=" + std::to_string(result.no_current_appearance_count);
        line += " mixed=" + std::to_string(result.mixed_appearance_count);
        log_sink(line);
    }
    return result;
}

void RefreshRandomColorCandidateAppearances(std::vector<core::RandomColorCandidate> &candidates)
{
    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR || current == nullptr) {
        for (core::RandomColorCandidate &candidate : candidates) {
            candidate.has_current_appearance = false;
            candidate.has_mixed_current_appearance = false;
            candidate.current_appearance = core::RandomColorEntry();
            candidate.current_appearance_label.clear();
        }
        return;
    }

    for (core::RandomColorCandidate &candidate : candidates) {
        PopulateCurrentAppearance(current, candidate);
    }
}

std::vector<core::RandomColorCandidate> CollectRandomColorCandidates(ProBoolean parts,
                                                                     ProBoolean assemblies,
                                                                     ProBoolean top_level_only)
{
    std::vector<core::RandomColorCandidate> candidates;

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        autobbox::creo::ModelType(current) != PRO_MDL_ASSEMBLY) {
        return candidates;
    }

    RandomColorTraverseContext ctx;
    ctx.want_parts = (parts == PRO_B_TRUE);
    ctx.want_assemblies = (assemblies == PRO_B_TRUE);
    ctx.top_level_only = (top_level_only == PRO_B_TRUE);
    ProSolidDispCompVisit(
        reinterpret_cast<ProSolid>(current),
        CollectRandomColorVisitAction,
        nullptr,
        &ctx);

    for (core::RandomColorCandidate &candidate : ctx.candidates) {
        candidate.label = MakeCandidateLabel(candidate);
        PopulateCurrentAppearance(current, candidate);
    }
    return ctx.candidates;
}

std::vector<core::RandomColorAssignment> BuildRandomColorAssignments(
    const std::vector<core::RandomColorCandidate> &candidates,
    const std::vector<core::RandomColorEntry> &colors)
{
    std::vector<core::RandomColorAssignment> assignments;
    if (candidates.empty() || colors.empty()) {
        return assignments;
    }

    assignments.resize(candidates.size());
    for (size_t i = 0; i < candidates.size(); ++i) {
        assignments[i].candidate = candidates[i];
    }

    std::vector<size_t> order(candidates.size());
    std::iota(order.begin(), order.end(), static_cast<size_t>(0));
    std::vector<size_t> color_order(colors.size());
    std::iota(color_order.begin(), color_order.end(), static_cast<size_t>(0));
    std::mt19937 rng(static_cast<unsigned int>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::shuffle(order.begin(), order.end(), rng);
    std::shuffle(color_order.begin(), color_order.end(), rng);

    for (size_t i = 0; i < order.size(); ++i) {
        assignments[order[i]].color = colors[color_order[i % color_order.size()]];
    }
    return assignments;
}

std::vector<core::RandomColorParameterPreview> BuildParameterColorPreview(
    const std::vector<core::RandomColorCandidate> &candidates,
    const std::vector<core::RandomColorEntry> &library_colors,
    const std::wstring &parameter_name)
{
    std::vector<core::RandomColorParameterPreview> previews;
    previews.reserve(candidates.size());

    for (const core::RandomColorCandidate &candidate : candidates) {
        core::RandomColorParameterPreview preview;
        preview.candidate = candidate;

        core::RandomColorSkipReason read_reason = core::RandomColorSkipReason::None;
        if (!TryReadModelParameterValue(candidate.mdl, parameter_name, preview.parameter_value, read_reason)) {
            preview.skip_reason = read_reason;
            preview.status_text = SkipReasonText(read_reason);
            previews.push_back(std::move(preview));
            continue;
        }

        if (TryFindPresetColor(preview.parameter_value, preview.color)) {
            preview.match_source = core::RandomColorMatchSource::Preset;
            preview.status_text = preview.color.display_name;
        } else if (TryFindLibraryColor(preview.parameter_value, library_colors, preview.color)) {
            preview.match_source = core::RandomColorMatchSource::Library;
            preview.status_text = preview.color.display_name;
        } else {
            preview.skip_reason = core::RandomColorSkipReason::NoMatch;
            preview.status_text = SkipReasonText(preview.skip_reason);
        }
        previews.push_back(std::move(preview));
    }

    return previews;
}

bool ApplyRandomColors(const std::vector<core::RandomColorAssignment> &assignments,
                       std::wstring &summary_text,
                       const std::function<void(const std::string &line)> &log_sink)
{
    summary_text.clear();
    if (assignments.empty()) {
        summary_text = L"未选择任何模型。";
        return false;
    }

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        autobbox::creo::ModelType(current) != PRO_MDL_ASSEMBLY) {
        summary_text = L"当前模型不是装配。";
        return false;
    }

    int success_count = 0;
    int failure_count = 0;
    bool changed = false;
    for (const core::RandomColorAssignment &assignment : assignments) {
        changed |= ApplyAppearanceToOccurrences(
            current,
            assignment.candidate,
            &assignment.color,
            log_sink,
            success_count,
            failure_count);
    }

    RefreshCurrentWindow();
    summary_text = L"唯一模型数：" + std::to_wstring(assignments.size()) +
                   L"\n已更新实例数：" + std::to_wstring(success_count);
    if (failure_count > 0) {
        summary_text += L"\n失败实例数：" + std::to_wstring(failure_count);
    }
    return changed;
}

bool ApplyParameterColors(const std::vector<core::RandomColorParameterPreview> &previews,
                          std::wstring &summary_text,
                          const std::function<void(const std::string &line)> &log_sink)
{
    summary_text.clear();
    if (previews.empty()) {
        summary_text = L"\u672a\u9009\u62e9\u4efb\u4f55\u6a21\u578b\u3002";
        return false;
    }

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        autobbox::creo::ModelType(current) != PRO_MDL_ASSEMBLY) {
        summary_text = L"\u5f53\u524d\u6a21\u578b\u4e0d\u662f\u88c5\u914d\u3002";
        return false;
    }

    int matched_count = 0;
    int missing_count = 0;
    int empty_count = 0;
    int unmatched_count = 0;
    int read_error_count = 0;
    int success_count = 0;
    int failure_count = 0;
    bool changed = false;

    for (const core::RandomColorParameterPreview &preview : previews) {
        if (preview.match_source == core::RandomColorMatchSource::None) {
            switch (preview.skip_reason) {
            case core::RandomColorSkipReason::MissingParameter:
                ++missing_count;
                break;
            case core::RandomColorSkipReason::EmptyParameter:
                ++empty_count;
                break;
            case core::RandomColorSkipReason::NoMatch:
                ++unmatched_count;
                break;
            case core::RandomColorSkipReason::ReadError:
                ++read_error_count;
                break;
            case core::RandomColorSkipReason::None:
            default:
                ++unmatched_count;
                break;
            }
            if (log_sink) {
                std::string line = "parameter-color skip model=";
                line += autobbox::common::WToA(preview.candidate.model_name.c_str());
                line += " value=" + autobbox::common::WToA(preview.parameter_value.c_str());
                line += " reason=" + autobbox::common::WToA(SkipReasonText(preview.skip_reason));
                log_sink(line);
            }
            continue;
        }

        ++matched_count;
        changed |= ApplyAppearanceToOccurrences(
            current,
            preview.candidate,
            &preview.color,
            log_sink,
            success_count,
            failure_count);
        if (log_sink) {
            std::string line = "parameter-color match model=";
            line += autobbox::common::WToA(preview.candidate.model_name.c_str());
            line += " value=" + autobbox::common::WToA(preview.parameter_value.c_str());
            line += " color=" + autobbox::common::WToA(preview.color.display_name.c_str());
            line += " source=";
            line += (preview.match_source == core::RandomColorMatchSource::Preset ? "preset" : "library");
            log_sink(line);
        }
    }

    RefreshCurrentWindow();
    summary_text = L"\u9009\u4e2d\u6a21\u578b\u6570\uff1a" + std::to_wstring(previews.size()) +
                   L"\n\u5339\u914d\u6a21\u578b\u6570\uff1a" + std::to_wstring(matched_count) +
                   L"\n\u5df2\u66f4\u65b0\u5b9e\u4f8b\u6570\uff1a" + std::to_wstring(success_count);
    if (missing_count > 0) {
        summary_text += L"\n\u7f3a\u5c11\u53c2\u6570\uff1a" + std::to_wstring(missing_count);
    }
    if (empty_count > 0) {
        summary_text += L"\n\u53c2\u6570\u503c\u4e3a\u7a7a\uff1a" + std::to_wstring(empty_count);
    }
    if (unmatched_count > 0) {
        summary_text += L"\n\u672a\u5339\u914d\u989c\u8272\uff1a" + std::to_wstring(unmatched_count);
    }
    if (read_error_count > 0) {
        summary_text += L"\n\u8bfb\u53d6\u5931\u8d25\uff1a" + std::to_wstring(read_error_count);
    }
    if (failure_count > 0) {
        summary_text += L"\n\u5e94\u7528\u5931\u8d25\u5b9e\u4f8b\u6570\uff1a" + std::to_wstring(failure_count);
    }
    return changed;
}

bool ClearRandomColors(const std::vector<core::RandomColorCandidate> &targets,
                       std::wstring &summary_text,
                       const std::function<void(const std::string &line)> &log_sink)
{
    summary_text.clear();
    if (targets.empty()) {
        summary_text = L"没有可清除颜色的模型。";
        return false;
    }

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR ||
        current == nullptr ||
        autobbox::creo::ModelType(current) != PRO_MDL_ASSEMBLY) {
        summary_text = L"当前模型不是装配。";
        return false;
    }

    int success_count = 0;
    int failure_count = 0;
    bool changed = false;
    for (const core::RandomColorCandidate &candidate : targets) {
        changed |= ApplyAppearanceToOccurrences(
            current,
            candidate,
            nullptr,
            log_sink,
            success_count,
            failure_count);
    }

    RefreshCurrentWindow();
    summary_text = L"已清除模型数：" + std::to_wstring(targets.size()) +
                   L"\n已更新实例数：" + std::to_wstring(success_count);
    if (failure_count > 0) {
        summary_text += L"\n失败实例数：" + std::to_wstring(failure_count);
    }
    return changed;
}

} // namespace autobbox::application
