#include "autobbox/application/afx_library_search.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <string>
#include <vector>

namespace autobbox::application {

namespace {

constexpr wchar_t kBuwAfxInstallRoot[] = L"D:\\Program Files\\buw\\AFX 10.0.8.0";
constexpr wchar_t kCreoBundledAfxInstallRoot[] = L"D:\\Program Files\\PTC\\Creo 10.0.8.0\\Common Files\\afx";
constexpr wchar_t kEquipmentRelativePath[] = L"parts\\equipment";

std::wstring Lower(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::vector<std::wstring> SplitTerms(const std::wstring &query)
{
    std::vector<std::wstring> terms;
    std::wstring term;
    for (wchar_t ch : query) {
        if (std::iswspace(ch)) {
            if (!term.empty()) {
                terms.push_back(Lower(term));
                term.clear();
            }
        } else {
            term.push_back(ch);
        }
    }
    if (!term.empty()) {
        terms.push_back(Lower(term));
    }
    return terms;
}

bool IsModelFile(const std::filesystem::path &path)
{
    const std::wstring ext = Lower(path.extension().wstring());
    return ext == L".prt" || ext == L".asm";
}

bool IsDirectory(const std::filesystem::path &path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec) && std::filesystem::is_directory(path, ec);
}

std::filesystem::path EquipmentRootFromInstallRoot(const std::filesystem::path &install_root)
{
    const std::filesystem::path equipment = install_root / kEquipmentRelativePath;
    if (IsDirectory(equipment)) {
        return equipment;
    }

    // Also allow callers to pass the equipment directory directly.
    if (Lower(install_root.filename().wstring()) == L"equipment" && IsDirectory(install_root)) {
        return install_root;
    }
    return {};
}

std::filesystem::path EnvPath(const wchar_t *name)
{
    wchar_t buffer[MAX_PATH * 4] = {0};
    const DWORD capacity = static_cast<DWORD>(sizeof(buffer) / sizeof(buffer[0]));
    const DWORD written = GetEnvironmentVariableW(name, buffer, capacity);
    if (written == 0 || written >= capacity) {
        return {};
    }
    return std::filesystem::path(buffer);
}

std::filesystem::path ResolveAfxEquipmentRootPath()
{
    const std::filesystem::path env_equipment = EnvPath(L"AUTO_BBOX_AFX_EQUIPMENT_ROOT");
    if (IsDirectory(env_equipment)) {
        return env_equipment;
    }

    const std::filesystem::path env_install = EnvPath(L"AUTO_BBOX_AFX_ROOT");
    if (!env_install.empty()) {
        if (const std::filesystem::path env_root = EquipmentRootFromInstallRoot(env_install); !env_root.empty()) {
            return env_root;
        }
    }

    const std::filesystem::path buw_root = EquipmentRootFromInstallRoot(kBuwAfxInstallRoot);
    if (!buw_root.empty()) {
        return buw_root;
    }

    const std::filesystem::path bundled_root = EquipmentRootFromInstallRoot(kCreoBundledAfxInstallRoot);
    if (!bundled_root.empty()) {
        return bundled_root;
    }

    return {};
}

std::string NarrowAscii(const std::wstring &value)
{
    std::string out;
    out.reserve(value.size());
    for (wchar_t ch : value) {
        out.push_back(ch >= 0 && ch <= 0x7f ? static_cast<char>(ch) : '_');
    }
    return out;
}

std::vector<std::string> DirectoryNodesFromRelative(const std::filesystem::path &relative_dir)
{
    std::vector<std::string> nodes;
    for (const auto &part : relative_dir) {
        const std::wstring text = part.wstring();
        if (!text.empty() && text != L"." && text != L"..") {
            nodes.push_back(NarrowAscii(text));
        }
    }
    return nodes;
}

std::vector<std::wstring> DirectoryLabelsFromRelative(const std::filesystem::path &relative_dir)
{
    std::vector<std::wstring> labels;
    for (const auto &part : relative_dir) {
        const std::wstring text = part.wstring();
        if (!text.empty() && text != L"." && text != L"..") {
            labels.push_back(text);
        }
    }
    return labels;
}

bool MatchesAllTerms(const std::wstring &searchable, const std::vector<std::wstring> &terms)
{
    for (const std::wstring &term : terms) {
        if (searchable.find(term) == std::wstring::npos) {
            return false;
        }
    }
    return true;
}

int ScoreItem(const AfxLibrarySearchItem &item, const std::vector<std::wstring> &terms)
{
    int score = 0;
    const std::wstring display = Lower(item.display_name);
    const std::wstring file = Lower(item.file_name);
    const std::wstring rel = Lower(item.relative_directory);
    for (const std::wstring &term : terms) {
        if (display == term) score += 1000;
        if (file == term || file.rfind(term + L".", 0) == 0) score += 800;
        if (display.find(term) != std::wstring::npos) score += 200;
        if (file.find(term) != std::wstring::npos) score += 150;
        if (rel.find(term) != std::wstring::npos) score += 80;
    }
    return score;
}

} // namespace

std::wstring DefaultAfxEquipmentRoot()
{
    return ResolveAfxEquipmentRootPath().wstring();
}

AfxLibrarySearchResult SearchAfxEquipmentLibrary(const std::wstring &query)
{
    AfxLibrarySearchResult result;
    const std::vector<std::wstring> terms = SplitTerms(query);
    if (terms.empty()) {
        return result;
    }

    const std::filesystem::path root(DefaultAfxEquipmentRoot());
    std::error_code ec;
    if (!std::filesystem::exists(root, ec) || !std::filesystem::is_directory(root, ec)) {
        return result;
    }

    struct ScoredItem {
        int score = 0;
        AfxLibrarySearchItem item;
    };
    std::vector<ScoredItem> scored;

    for (std::filesystem::recursive_directory_iterator it(root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end;
         it.increment(ec)) {
        if (ec || !it->is_regular_file(ec) || !IsModelFile(it->path())) {
            continue;
        }

        const std::filesystem::path absolute = it->path();
        const std::filesystem::path relative = std::filesystem::relative(absolute, root, ec);
        if (ec) {
            ec.clear();
            continue;
        }
        const std::filesystem::path relative_dir = relative.parent_path();
        const std::wstring stem = absolute.stem().wstring();
        const std::wstring file_name = absolute.filename().wstring();
        const std::wstring rel_dir_text = relative_dir.wstring();

        AfxLibrarySearchItem item;
        item.display_name = stem;
        item.file_name = file_name;
        item.relative_directory = rel_dir_text;
        item.absolute_path = absolute.wstring();
        item.directory_nodes = DirectoryNodesFromRelative(relative_dir);
        item.directory_labels = DirectoryLabelsFromRelative(relative_dir);
        item.list_item_name = NarrowAscii(stem);
        item.searchable_text = Lower(stem + L" " + file_name + L" " + rel_dir_text);

        if (MatchesAllTerms(item.searchable_text, terms)) {
            scored.push_back({ScoreItem(item, terms), item});
        }
    }

    std::sort(scored.begin(), scored.end(), [](const ScoredItem &a, const ScoredItem &b) {
        if (a.score != b.score) return a.score > b.score;
        if (a.item.relative_directory != b.item.relative_directory) return a.item.relative_directory < b.item.relative_directory;
        return a.item.file_name < b.item.file_name;
    });

    constexpr size_t kMaxMatches = 200;
    for (const ScoredItem &entry : scored) {
        if (result.matches.size() >= kMaxMatches) break;
        result.matches.push_back(entry.item);
    }
    return result;
}

} // namespace autobbox::application
