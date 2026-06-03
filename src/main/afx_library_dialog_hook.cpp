#include "autobbox/main/afx_library_dialog_hook.h"

#include "autobbox/application/afx_library_search.h"
#include "autobbox/common/log.h"
#include "autobbox/common/strings.h"

#include <ProArray.h>
#include <ProUILabel.h>
#include <ProUIInputpanel.h>
#include <ProUIList.h>
#include <ProUIPushbutton.h>
#include <ProUITree.h>
#include <ProUIDialog.h>
#include <ProToolkit.h>
#include <ProUtil.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cwchar>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace autobbox::main {

namespace {

constexpr char kDialog[] = "ProTKDialogLibrarySelection";
constexpr char kTree[] = "TreeLibraryFolders";
constexpr char kList[] = "ListLibraryFolderContent";
constexpr char kSearchInput[] = "InputPanelLibrarySearch";
constexpr char kSearchStatus[] = "LabelLibrarySearchStatus";
constexpr char kSearchNext[] = "Pushbutton_SearchNext";
constexpr int kPollMs = 1000;
constexpr int kSelectRetryMs = 200;

struct HookState {
    bool running = false;
    bool timer_created = false;
    bool deferred_start_scheduled = false;
    bool bound = false;
    ProUITimerID timer_id = nullptr;
    int pending_select_retries = 0;
    int bind_attempts = 0;
    int bind_fail_logs = 0;
    bool dialog_seen = false;
    std::string startup_log;
    std::wstring last_query;
    autobbox::application::AfxLibrarySearchResult result;
    size_t active_index = 0;
    std::vector<std::wstring> pending_directory_labels;
    std::string pending_list_item;
    std::wstring pending_list_label;
};

HookState g_state;

void TimerAction(char *dialog, ProUITimerID timer_id, ProAppData appdata);
void DeferredStartAction(char *dialog, char *component, ProAppData appdata);
void OnSearchInput(char *dialog, char *component, ProAppData appdata);
void OnSearchActivate(char *dialog, char *component, ProAppData appdata);
void OnSearchNext(char *dialog, char *component, ProAppData appdata);
bool SchedulePoll();
bool ScheduleSelectionRetry();

void LogHook(const char *fmt, ...)
{
    if (g_state.startup_log.empty() || fmt == nullptr) {
        return;
    }
    va_list args;
    va_start(args, fmt);
    autobbox::common::AppendFormattedLine(g_state.startup_log, "AFX ", fmt, args);
    va_end(args);
}

void SetStatus(char *dialog, const std::wstring &text)
{
    const ProError st = ProUILabelTextSet(dialog,
                                          const_cast<char *>(kSearchStatus),
                                          const_cast<wchar_t *>(text.c_str()));
    if (st != PRO_TK_NO_ERROR && g_state.bind_fail_logs < 10) {
        ++g_state.bind_fail_logs;
        LogHook("status-set failed status=%d", static_cast<int>(st));
    }
}

std::wstring GetSearchValue(char *dialog)
{
    wchar_t *raw = nullptr;
    std::wstring value;
    if (ProUIInputpanelValueGet(dialog, const_cast<char *>(kSearchInput), &raw) == PRO_TK_NO_ERROR && raw != nullptr) {
        value.assign(raw);
        ProWstringFree(raw);
    }
    return value;
}

void FreeStringArray(char **values, int count)
{
    if (values != nullptr) {
        ProStringarrayFree(values, count);
    }
}

void FreeWstringArray(wchar_t **values, int count)
{
    if (values != nullptr) {
        ProWstringarrayFree(values, count);
    }
}

std::wstring LowerWide(std::wstring value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return value;
}

std::wstring TrimWide(const std::wstring &value)
{
    size_t first = 0;
    while (first < value.size() && std::iswspace(value[first])) {
        ++first;
    }
    size_t last = value.size();
    while (last > first && std::iswspace(value[last - 1])) {
        --last;
    }
    return value.substr(first, last - first);
}

std::string TrimNarrow(const std::string &value)
{
    size_t first = 0;
    while (first < value.size() && std::isspace(static_cast<unsigned char>(value[first]))) {
        ++first;
    }
    size_t last = value.size();
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1]))) {
        --last;
    }
    return value.substr(first, last - first);
}

std::wstring NormalizeKey(std::wstring value)
{
    value = LowerWide(TrimWide(value));
    std::wstring out;
    out.reserve(value.size());
    for (wchar_t ch : value) {
        if (std::iswspace(ch) || ch == L'_' || ch == L'-') {
            continue;
        }
        out.push_back(ch);
    }
    return out;
}

std::wstring BytesToWide(const std::string &bytes, UINT code_page)
{
    if (bytes.empty()) {
        return {};
    }

    int size = MultiByteToWideChar(code_page,
                                   MB_ERR_INVALID_CHARS,
                                   bytes.data(),
                                   static_cast<int>(bytes.size()),
                                   nullptr,
                                   0);
    if (size <= 0 && code_page == CP_UTF8) {
        size = MultiByteToWideChar(code_page,
                                   0,
                                   bytes.data(),
                                   static_cast<int>(bytes.size()),
                                   nullptr,
                                   0);
    }
    if (size <= 0) {
        return {};
    }

    std::wstring out(static_cast<size_t>(size), L'\0');
    MultiByteToWideChar(code_page,
                        code_page == CP_UTF8 ? 0 : MB_ERR_INVALID_CHARS,
                        bytes.data(),
                        static_cast<int>(bytes.size()),
                        &out[0],
                        size);
    return out;
}

std::wstring ReadTextFileWide(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.size() >= 3 &&
        static_cast<unsigned char>(bytes[0]) == 0xEF &&
        static_cast<unsigned char>(bytes[1]) == 0xBB &&
        static_cast<unsigned char>(bytes[2]) == 0xBF) {
        bytes.erase(0, 3);
    }

    std::wstring utf8 = BytesToWide(bytes, CP_UTF8);
    if (!utf8.empty()) {
        return utf8;
    }
    return BytesToWide(bytes, CP_ACP);
}

std::vector<std::wstring> SplitWideLines(const std::wstring &text)
{
    std::vector<std::wstring> lines;
    std::wstring current;
    for (wchar_t ch : text) {
        if (ch == L'\n') {
            if (!current.empty() && current.back() == L'\r') {
                current.pop_back();
            }
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        if (!current.empty() && current.back() == L'\r') {
            current.pop_back();
        }
        lines.push_back(current);
    }
    return lines;
}

std::filesystem::path AfxInstallRoot()
{
    std::filesystem::path equipment(autobbox::application::DefaultAfxEquipmentRoot());
    if (equipment.filename().wstring() == L"equipment") {
        equipment = equipment.parent_path();
    }
    if (equipment.filename().wstring() == L"parts") {
        equipment = equipment.parent_path();
    }
    return equipment;
}

struct TranslationEntry {
    std::wstring key;
    std::wstring value;
};

const std::vector<TranslationEntry> &AfxTranslations()
{
    static std::vector<TranslationEntry> entries;
    static bool loaded = false;
    if (loaded) {
        return entries;
    }
    loaded = true;

    const std::filesystem::path text_root = AfxInstallRoot() / L"text";
    std::error_code ec;
    if (!std::filesystem::exists(text_root, ec)) {
        return entries;
    }

    for (std::filesystem::directory_iterator it(text_root, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end;
         it.increment(ec)) {
        if (ec || !it->is_directory(ec)) {
            continue;
        }
        const std::filesystem::path file = it->path() / L"afx_translation.txt";
        const std::wstring text = ReadTextFileWide(file);
        if (text.empty()) {
            continue;
        }

        const std::vector<std::wstring> lines = SplitWideLines(text);
        for (size_t i = 0; i + 2 < lines.size();) {
            const std::wstring key = TrimWide(lines[i]);
            const std::wstring english = TrimWide(lines[i + 1]);
            const std::wstring localized = TrimWide(lines[i + 2]);
            if (!key.empty()) {
                entries.push_back({NormalizeKey(key), key});
                if (!english.empty()) {
                    entries.push_back({NormalizeKey(key), english});
                }
                if (!localized.empty()) {
                    entries.push_back({NormalizeKey(key), localized});
                }
            }
            i += 3;
            if (i < lines.size() && TrimWide(lines[i]).empty()) {
                ++i;
            }
        }
    }
    return entries;
}

std::vector<std::wstring> TranslationCandidates(const std::wstring &target)
{
    std::vector<std::wstring> candidates;
    candidates.push_back(target);
    const std::wstring key = NormalizeKey(target);
    for (const TranslationEntry &entry : AfxTranslations()) {
        if (entry.key == key &&
            std::find(candidates.begin(), candidates.end(), entry.value) == candidates.end()) {
            candidates.push_back(entry.value);
        }
    }
    return candidates;
}

std::string JoinWideForLog(const std::vector<std::wstring> &values, const wchar_t *sep = L"\\")
{
    std::wstring joined;
    for (const std::wstring &value : values) {
        if (!joined.empty()) {
            joined += sep;
        }
        joined += value;
    }
    return autobbox::common::WToA(joined.c_str());
}

std::wstring NodeLabel(char *dialog, const std::string &node)
{
    wchar_t *label = nullptr;
    std::wstring value;
    if (ProUITreeNodeLabelGet(dialog, const_cast<char *>(kTree), const_cast<char *>(node.c_str()), &label) == PRO_TK_NO_ERROR &&
        label != nullptr) {
        value.assign(label);
        ProWstringFree(label);
    }
    return value;
}

bool NodeMatchesLabel(char *dialog, const std::string &node, const std::wstring &target)
{
    if (target.empty()) {
        return false;
    }
    const std::wstring label = TrimWide(NodeLabel(dialog, node));
    const std::wstring label_lower = LowerWide(label);
    const std::vector<std::wstring> candidates = TranslationCandidates(target);
    for (const std::wstring &candidate : candidates) {
        const std::wstring candidate_lower = LowerWide(TrimWide(candidate));
        if (!label_lower.empty() &&
            (label_lower == candidate_lower ||
             NormalizeKey(label_lower) == NormalizeKey(candidate_lower))) {
            return true;
        }
    }
    const std::wstring node_as_wide = autobbox::common::AToW(node.c_str());
    return LowerWide(TrimWide(node_as_wide)) == LowerWide(TrimWide(target));
}

std::vector<std::string> TreeChildren(char *dialog, const std::string &parent)
{
    int count = 0;
    char **children = nullptr;
    ProError st = PRO_TK_GENERAL_ERROR;
    if (parent.empty()) {
        st = ProUITreeChildnamesGet(dialog, const_cast<char *>(kTree), &count, &children);
    } else {
        ProUITreeNodeExpand(dialog, const_cast<char *>(kTree), const_cast<char *>(parent.c_str()), PRO_B_FALSE);
        st = ProUITreeNodeChildrenGet(dialog, const_cast<char *>(kTree), const_cast<char *>(parent.c_str()), &count, &children);
    }

    std::vector<std::string> out;
    if (st == PRO_TK_NO_ERROR && children != nullptr) {
        out.reserve(static_cast<size_t>(count));
        for (int i = 0; i < count; ++i) {
            if (children[i] != nullptr) {
                out.emplace_back(children[i]);
            }
        }
    }
    FreeStringArray(children, count);
    return out;
}

void LogTreeChildren(char *dialog, const std::string &parent, const std::vector<std::string> &children)
{
    std::ostringstream oss;
    oss << "tree children parent=" << (parent.empty() ? "<top>" : parent)
        << " count=" << static_cast<int>(children.size()) << " first=";
    const size_t limit = std::min<size_t>(children.size(), 12);
    for (size_t i = 0; i < limit; ++i) {
        if (i != 0) {
            oss << " | ";
        }
        const std::wstring label = NodeLabel(dialog, children[i]);
        oss << children[i] << ":" << autobbox::common::WToA(label.c_str());
    }
    LogHook("%s", oss.str().c_str());
}

std::vector<std::wstring> SelListDirEntries(const std::filesystem::path &folder)
{
    std::vector<std::wstring> entries;
    const std::filesystem::path file = folder / L"sel_list.txt";
    std::ifstream in(file);
    if (!in) {
        return entries;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::istringstream iss(line);
        std::string name;
        std::string type;
        if (!(iss >> name >> type)) {
            continue;
        }
        if (name == "dir_up" || type != "dir") {
            continue;
        }
        entries.push_back(autobbox::common::AToW(name.c_str()));
    }
    return entries;
}

int SelListDirIndex(const std::filesystem::path &folder, const std::wstring &child)
{
    const std::vector<std::wstring> entries = SelListDirEntries(folder);
    const std::wstring child_key = NormalizeKey(child);
    for (size_t i = 0; i < entries.size(); ++i) {
        if (NormalizeKey(entries[i]) == child_key) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void AddUniqueCandidate(std::vector<std::string> &candidates, const std::string &node)
{
    if (node.empty()) {
        return;
    }
    if (std::find(candidates.begin(), candidates.end(), node) == candidates.end()) {
        candidates.push_back(node);
    }
}

std::vector<std::string> CandidateChildren(char *dialog,
                                           const std::string &parent,
                                           const std::filesystem::path &folder,
                                           const std::vector<std::string> &children,
                                           const std::wstring &target)
{
    std::vector<std::string> candidates;

    for (const std::string &child : children) {
        if (NodeMatchesLabel(dialog, child, target)) {
            AddUniqueCandidate(candidates, child);
        }
    }

    const int sel_index = SelListDirIndex(folder, target);
    if (sel_index >= 0 && sel_index < static_cast<int>(children.size())) {
        AddUniqueCandidate(candidates, children[sel_index]);
    }

    LogHook("tree candidates parent=%s folder=%s target=%s children=%d sel_index=%d candidates=%d",
            parent.empty() ? "<top>" : parent.c_str(),
            autobbox::common::WToA(folder.wstring().c_str()).c_str(),
            autobbox::common::WToA(target.c_str()).c_str(),
            static_cast<int>(children.size()),
            sel_index,
            static_cast<int>(candidates.size()));
    return candidates;
}

bool ResolveTreePathFromParent(char *dialog,
                               const std::string &parent,
                               const std::filesystem::path &folder,
                               const std::vector<std::wstring> &labels,
                               size_t label_index,
                               std::vector<std::string> &resolved)
{
    if (label_index >= labels.size()) {
        return true;
    }

    const std::vector<std::string> children = TreeChildren(dialog, parent);
    if (label_index == 0 || children.size() <= 20) {
        LogTreeChildren(dialog, parent, children);
    }

    const std::vector<std::string> candidates =
        CandidateChildren(dialog, parent, folder, children, labels[label_index]);
    for (const std::string &child : candidates) {
        resolved.push_back(child);
        if (ResolveTreePathFromParent(dialog, child, folder / labels[label_index], labels, label_index + 1, resolved)) {
            return true;
        }
        resolved.pop_back();
    }
    return false;
}

std::vector<std::string> ResolveTreePathByLabels(char *dialog, const std::vector<std::wstring> &labels)
{
    if (labels.empty()) {
        return {};
    }

    std::vector<std::string> resolved;
    const std::filesystem::path equipment_root(autobbox::application::DefaultAfxEquipmentRoot());
    char *root_raw = nullptr;
    const ProError root_st = ProUITreeTreerootnodeGet(dialog, const_cast<char *>(kTree), &root_raw);
    const std::string root = root_raw != nullptr ? root_raw : "";
    if (root_raw != nullptr) {
        ProStringFree(root_raw);
    }
    LogHook("tree resolve labels=%s root_st=%d root=%s root_label=%s",
            JoinWideForLog(labels).c_str(),
            static_cast<int>(root_st),
            root.c_str(),
            root.empty() ? "" : autobbox::common::WToA(NodeLabel(dialog, root).c_str()).c_str());

    if (!root.empty()) {
        ProUITreeNodeExpand(dialog, const_cast<char *>(kTree), const_cast<char *>(root.c_str()), PRO_B_FALSE);
        if (NodeMatchesLabel(dialog, root, labels[0])) {
            resolved.push_back(root);
            if (ResolveTreePathFromParent(dialog, root, equipment_root / labels[0], labels, 1, resolved)) {
                return resolved;
            }
            resolved.clear();
        }
        if (ResolveTreePathFromParent(dialog, root, equipment_root, labels, 0, resolved)) {
            return resolved;
        }
    }

    resolved.clear();
    if (ResolveTreePathFromParent(dialog, "", equipment_root, labels, 0, resolved)) {
        return resolved;
    }
    return {};
}

bool ExpandAndSelectDirectory(char *dialog,
                              const std::vector<std::string> &fallback_nodes,
                              const std::vector<std::wstring> &labels)
{
    std::vector<std::string> nodes = ResolveTreePathByLabels(dialog, labels);
    if (nodes.empty()) {
        LogHook("tree resolve by-label failed labels=%s fallback_nodes=%d",
                JoinWideForLog(labels).c_str(),
                static_cast<int>(fallback_nodes.size()));
        nodes = fallback_nodes;
    }
    if (nodes.empty()) {
        LogHook("tree select skipped no nodes");
        return false;
    }

    for (const std::string &node : nodes) {
        const ProError expand_st =
            ProUITreeNodeExpand(dialog, const_cast<char *>(kTree), const_cast<char *>(node.c_str()), PRO_B_FALSE);
        LogHook("tree expand node=%s label=%s st=%d",
                node.c_str(),
                autobbox::common::WToA(NodeLabel(dialog, node).c_str()).c_str(),
                static_cast<int>(expand_st));
    }

    const std::string &leaf = nodes.back();
    char *selected = const_cast<char *>(leaf.c_str());
    const ProError deselect_st = ProUITreeAllnodesDeselect(dialog, const_cast<char *>(kTree));
    const ProError node_select_st = ProUITreeNodeSelect(dialog, const_cast<char *>(kTree), selected);
    const ProError names_set_st = ProUITreeSelectednamesSet(dialog, const_cast<char *>(kTree), 1, &selected);
    const ProError current_st = ProUITreeTreecurrentnodeSet(dialog, const_cast<char *>(kTree), selected);
    ProBoolean is_selected = PRO_B_FALSE;
    const ProError selected_get_st =
        ProUITreeNodeIsSelected(dialog, const_cast<char *>(kTree), selected, &is_selected);
    LogHook("tree select leaf=%s label=%s deselect=%d node_select=%d names_set=%d current=%d is_selected_st=%d is_selected=%d",
            leaf.c_str(),
            autobbox::common::WToA(NodeLabel(dialog, leaf).c_str()).c_str(),
            static_cast<int>(deselect_st),
            static_cast<int>(node_select_st),
            static_cast<int>(names_set_st),
            static_cast<int>(current_st),
            static_cast<int>(selected_get_st),
            is_selected == PRO_B_TRUE ? 1 : 0);
    return names_set_st == PRO_TK_NO_ERROR || node_select_st == PRO_TK_NO_ERROR;
}

bool TrySelectPendingListItem(char *dialog)
{
    if (g_state.pending_list_item.empty() && g_state.pending_list_label.empty()) {
        return true;
    }

    auto select_by_name = [dialog](const std::string &name) {
        char *selection = const_cast<char *>(name.c_str());
        const ProError st = ProUIListSelectednamesSet(dialog, const_cast<char *>(kList), 1, &selection);
        LogHook("list select name=%s st=%d", name.c_str(), static_cast<int>(st));
        return st == PRO_TK_NO_ERROR;
    };

    int count = 0;
    char **names = nullptr;
    const ProError names_st = ProUIListNamesGet(dialog, const_cast<char *>(kList), &count, &names);
    if (names_st == PRO_TK_NO_ERROR && names != nullptr) {
        for (int i = 0; i < count; ++i) {
            if (names[i] != nullptr && g_state.pending_list_item == names[i]) {
                select_by_name(g_state.pending_list_item);
                FreeStringArray(names, count);
                g_state.pending_list_item.clear();
                g_state.pending_list_label.clear();
                g_state.pending_select_retries = 0;
                return true;
            }
        }
    }

    int label_count = 0;
    wchar_t **labels = nullptr;
    const ProError labels_st = ProUIListLabelsGet(dialog, const_cast<char *>(kList), &label_count, &labels);
    if (labels_st == PRO_TK_NO_ERROR && labels != nullptr) {
        const std::wstring pending_name_w(g_state.pending_list_item.begin(), g_state.pending_list_item.end());
        const int pair_count = std::min(count, label_count);
        for (int i = 0; i < pair_count; ++i) {
            if (names != nullptr && names[i] != nullptr && labels[i] != nullptr &&
                (g_state.pending_list_label == labels[i] || pending_name_w == labels[i])) {
                const std::string selected_name = names[i];
                FreeWstringArray(labels, label_count);
                FreeStringArray(names, count);
                select_by_name(selected_name);
                g_state.pending_list_item.clear();
                g_state.pending_list_label.clear();
                g_state.pending_select_retries = 0;
                return true;
            }
        }
    }

    if (g_state.pending_select_retries == 0 ||
        g_state.pending_select_retries == 5 ||
        g_state.pending_select_retries == 9) {
        std::ostringstream oss;
        oss << "list pending not found retries=" << g_state.pending_select_retries
            << " names_st=" << static_cast<int>(names_st)
            << " labels_st=" << static_cast<int>(labels_st)
            << " names=" << count
            << " labels=" << label_count
            << " pending_name=" << g_state.pending_list_item
            << " pending_label=" << autobbox::common::WToA(g_state.pending_list_label.c_str())
            << " pending_dir=" << JoinWideForLog(g_state.pending_directory_labels)
            << " first=";
        const int pair_count = std::min(count, label_count);
        const int limit = std::min(pair_count, 12);
        for (int i = 0; i < limit; ++i) {
            if (i != 0) {
                oss << " | ";
            }
            oss << (names != nullptr && names[i] != nullptr ? names[i] : "")
                << ":"
                << (labels != nullptr && labels[i] != nullptr ? autobbox::common::WToA(labels[i]) : "");
        }
        LogHook("%s", oss.str().c_str());
    }

    FreeWstringArray(labels, label_count);
    FreeStringArray(names, count);
    return false;
}

void LocateActiveMatch(char *dialog)
{
    if (g_state.result.matches.empty()) {
        g_state.pending_directory_labels.clear();
        g_state.pending_list_item.clear();
        g_state.pending_list_label.clear();
        SetStatus(dialog, L"\x672a\x627e\x5230");
        return;
    }

    if (g_state.active_index >= g_state.result.matches.size()) {
        g_state.active_index = 0;
    }

    const auto &item = g_state.result.matches[g_state.active_index];
    LogHook("locate index=%d/%d file=%s dir=%s",
            static_cast<int>(g_state.active_index + 1),
            static_cast<int>(g_state.result.matches.size()),
            autobbox::common::WToA(item.file_name.c_str()).c_str(),
            autobbox::common::WToA(item.relative_directory.c_str()).c_str());
    ExpandAndSelectDirectory(dialog, item.directory_nodes, item.directory_labels);
    g_state.pending_directory_labels = item.directory_labels;
    g_state.pending_list_item = item.list_item_name;
    g_state.pending_list_label = item.display_name;
    if (!TrySelectPendingListItem(dialog)) {
        ScheduleSelectionRetry();
    }

    std::wstring status = std::to_wstring(g_state.active_index + 1);
    status += L"/";
    status += std::to_wstring(g_state.result.matches.size());
    SetStatus(dialog, status);
}

void RunSearch(char *dialog, bool next)
{
    const std::wstring query = GetSearchValue(dialog);
    if (query.empty()) {
        g_state.last_query.clear();
        g_state.result.matches.clear();
        g_state.active_index = 0;
        g_state.pending_directory_labels.clear();
        g_state.pending_list_item.clear();
        g_state.pending_list_label.clear();
        SetStatus(dialog, L"");
        LogHook("search empty");
        return;
    }

    if (query != g_state.last_query) {
        g_state.last_query = query;
        g_state.result = autobbox::application::SearchAfxEquipmentLibrary(query);
        g_state.active_index = 0;
        LogHook("search query=%s matches=%d root=%s",
                autobbox::common::WToA(query.c_str()).c_str(),
                static_cast<int>(g_state.result.matches.size()),
                autobbox::common::WToA(autobbox::application::DefaultAfxEquipmentRoot().c_str()).c_str());
    } else if (next && !g_state.result.matches.empty()) {
        g_state.active_index = (g_state.active_index + 1) % g_state.result.matches.size();
    }

    LocateActiveMatch(dialog);
}

void PollSearchValue(char *dialog)
{
    const std::wstring query = GetSearchValue(dialog);
    if (query != g_state.last_query ||
        (query.empty() && (!g_state.result.matches.empty() ||
                           !g_state.pending_list_item.empty() ||
                           !g_state.pending_list_label.empty()))) {
        RunSearch(dialog, false);
    }
}

bool TryBind(char *dialog)
{
    ++g_state.bind_attempts;
    wchar_t *raw = nullptr;
    const ProError exists = ProUIInputpanelValueGet(dialog, const_cast<char *>(kSearchInput), &raw);
    std::wstring current_value;
    if (raw != nullptr) {
        current_value.assign(raw);
    }
    if (raw != nullptr) {
        ProWstringFree(raw);
    }
    if (exists != PRO_TK_NO_ERROR) {
        g_state.bound = false;
        if (g_state.bind_fail_logs < 20) {
            ++g_state.bind_fail_logs;
            LogHook("bind attempt=%d dialog/input not ready status=%d",
                    g_state.bind_attempts,
                    static_cast<int>(exists));
        }
        return false;
    }

    if (!g_state.dialog_seen) {
        g_state.dialog_seen = true;
        LogHook("dialog seen input value=%s", autobbox::common::WToA(current_value.c_str()).c_str());
    }

    const bool was_bound = g_state.bound;
    const ProError st_input =
        ProUIInputpanelInputActionSet(dialog, const_cast<char *>(kSearchInput), OnSearchInput, nullptr);
    const ProError st_activate =
        ProUIInputpanelActivateActionSet(dialog, const_cast<char *>(kSearchInput), OnSearchActivate, nullptr);
    const ProError st_next =
        ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(kSearchNext), OnSearchNext, nullptr);
    const ProError st_default =
        ProUIDialogDefaultbuttonSet(dialog, const_cast<char *>(kSearchNext));

    if (st_input != PRO_TK_NO_ERROR ||
        st_activate != PRO_TK_NO_ERROR ||
        st_next != PRO_TK_NO_ERROR ||
        st_default != PRO_TK_NO_ERROR) {
        g_state.bound = false;
        wchar_t status[128] = {0};
        std::swprintf(status,
                      sizeof(status) / sizeof(status[0]),
                      L"AB bind failed i=%d a=%d n=%d d=%d",
                      static_cast<int>(st_input),
                      static_cast<int>(st_activate),
                      static_cast<int>(st_next),
                      static_cast<int>(st_default));
        SetStatus(dialog, status);
        LogHook("bind failed input=%d activate=%d next=%d default=%d",
                static_cast<int>(st_input),
                static_cast<int>(st_activate),
                static_cast<int>(st_next),
                static_cast<int>(st_default));
        return false;
    }

    g_state.bound = true;
    if (!was_bound) {
        LogHook("bind ok input=%d activate=%d next=%d default=%d",
                static_cast<int>(st_input),
                static_cast<int>(st_activate),
                static_cast<int>(st_next),
                static_cast<int>(st_default));
        const std::wstring query = GetSearchValue(dialog);
        if (query.empty()) {
            SetStatus(dialog, L"AB ready");
        } else {
            RunSearch(dialog, false);
        }
    }
    TrySelectPendingListItem(dialog);
    return true;
}

bool EnsureTimer()
{
    if (g_state.timer_created && g_state.timer_id != nullptr) {
        return true;
    }

    ProName timer_name = {0};
    ProStringToWstring(timer_name, const_cast<char *>("ABAfxLibSearchHook"));
    const ProError st = ProUITimerCreate(TimerAction, nullptr, timer_name, &g_state.timer_id);
    if (st == PRO_TK_NO_ERROR) {
        g_state.timer_created = true;
        return true;
    }
    return false;
}

bool ScheduleSelectionRetry()
{
    if (!g_state.running ||
        (g_state.pending_list_item.empty() && g_state.pending_list_label.empty()) ||
        !EnsureTimer()) {
        return false;
    }
    const ProError st =
        ProUIDialogTimerStart(const_cast<char *>(kDialog), g_state.timer_id, kSelectRetryMs, PRO_B_FALSE);
    return st == PRO_TK_NO_ERROR;
}

bool SchedulePoll()
{
    if (!g_state.running || !EnsureTimer()) {
        return false;
    }
    const ProError st =
        ProUIDialogTimerStart(const_cast<char *>("main_dlg_cur"), g_state.timer_id, kPollMs, PRO_B_FALSE);
    if (st == PRO_TK_NO_ERROR) {
        return true;
    }
    if (g_state.bind_fail_logs < 20) {
        ++g_state.bind_fail_logs;
        LogHook("poll timer start failed status=%d", static_cast<int>(st));
    }
    return false;
}

bool ScheduleDeferredStart()
{
    if (!g_state.running || g_state.deferred_start_scheduled) {
        return false;
    }
    const ProError st = ProUIDialogAppActionSet(nullptr, DeferredStartAction, nullptr);
    if (st == PRO_TK_NO_ERROR) {
        g_state.deferred_start_scheduled = true;
        return true;
    }
    LogHook("deferred start schedule failed status=%d", static_cast<int>(st));
    return false;
}

void PollAndBind(char *dialog)
{
    TryBind(dialog);
    if (g_state.bound &&
        !(g_state.pending_list_item.empty() && g_state.pending_list_label.empty())) {
        TrySelectPendingListItem(dialog);
    }
}

void TimerAction(char *, ProUITimerID, ProAppData)
{
    if (!g_state.running) {
        return;
    }
    if (!(g_state.pending_list_item.empty() && g_state.pending_list_label.empty()) &&
        !TrySelectPendingListItem(const_cast<char *>(kDialog)) &&
        g_state.pending_select_retries < 10) {
        ++g_state.pending_select_retries;
        ScheduleSelectionRetry();
        return;
    }
    PollAndBind(const_cast<char *>(kDialog));
    if (g_state.running) {
        SchedulePoll();
    }
}

void DeferredStartAction(char *, char *, ProAppData)
{
    g_state.deferred_start_scheduled = false;
    if (!g_state.running) {
        return;
    }
    const bool scheduled = SchedulePoll();
    LogHook("deferred start poll scheduled=%d", scheduled ? 1 : 0);
}

void OnSearchInput(char *dialog, char *, ProAppData)
{
    RunSearch(dialog, false);
}

void OnSearchActivate(char *dialog, char *, ProAppData)
{
    SetStatus(dialog, L"AB enter");
    LogHook("enter action");
    RunSearch(dialog, true);
}

void OnSearchNext(char *dialog, char *, ProAppData)
{
    SetStatus(dialog, L"AB next");
    LogHook("next action");
    RunSearch(dialog, true);
}

} // namespace

void StartAfxLibraryDialogHook(const std::string &startup_log)
{
    g_state.running = true;
    g_state.startup_log = startup_log;
    g_state.bind_attempts = 0;
    g_state.bind_fail_logs = 0;
    g_state.dialog_seen = false;
    LogHook("hook start equipment_root=%s", autobbox::common::WToA(autobbox::application::DefaultAfxEquipmentRoot().c_str()).c_str());
    const bool scheduled = SchedulePoll();
    LogHook("hook poll scheduled=%d timer_created=%d", scheduled ? 1 : 0, g_state.timer_created ? 1 : 0);
    if (!scheduled) {
        ScheduleDeferredStart();
    }
}

void StopAfxLibraryDialogHook()
{
    g_state.running = false;
    g_state.bound = false;
    g_state.deferred_start_scheduled = false;
    g_state.pending_select_retries = 0;
    g_state.pending_list_item.clear();
    g_state.pending_list_label.clear();
    if (g_state.timer_id != nullptr) {
        ProUIDialogTimerStop(g_state.timer_id);
        ProUITimerDestroy(g_state.timer_id);
    }
    LogHook("hook stop");
    g_state.timer_id = nullptr;
    g_state.timer_created = false;
}

} // namespace autobbox::main
