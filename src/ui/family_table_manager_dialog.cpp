
#include "autobbox/ui/family_table_manager_dialog.h"

#include "autobbox/application/ft_column_ops.h"
#include "autobbox/application/ft_diff_engine.h"
#include "autobbox/application/ft_discovery.h"
#include "autobbox/application/ft_excel_exporter.h"
#include "autobbox/application/ft_excel_importer.h"
#include "autobbox/application/ft_logger.h"
#include "autobbox/application/ft_native_bridge.h"
#include "autobbox/application/ft_reader.h"
#include "autobbox/application/ft_replica_view.h"
#include "autobbox/application/ft_row_ops.h"
#include "autobbox/application/ft_validator.h"
#include "autobbox/application/ft_writer.h"
#include "autobbox/common/strings.h"
#include "autobbox/ui/family_table_add_column_dialog.h"
#include "autobbox/ui/family_table_grid.h"
#include "autobbox/ui/message_dialog.h"

#include <ProArray.h>
#include <ProFamtable.h>
#include <ProFaminstance.h>
#include <ProMdl.h>
#include <ProUI.h>
#include <ProUIDialog.h>
#include <ProUIDrawingarea.h>
#include <ProUIInputpanel.h>
#include <ProUILabel.h>
#include <ProUIList.h>
#include <ProUIMessage.h>
#include <ProUIPushbutton.h>
#include <ProUITable.h>
#include <ProUtil.h>

#include <algorithm>
#include <cwctype>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>
#endif

namespace autobbox::ui {
namespace {

struct DialogConfig {
    const char *dialog = "autobbox_family_table_manager";
    const char *status = "StatusLabel";
    const char *quick_input = "QuickInput";
    const char *level_list = "LevelList";
    const char *level_splitter = "LevelSplitter";
    const char *grid = "FtGrid";
    const char *cell_input_base = "CellInputBase";

    const char *file_refresh = "FileRefresh";
    const char *file_import = "FileImport";
    const char *file_export_current = "FileExportCurrent";
    const char *file_export_all = "FileExportAll";
    const char *file_edit_outside = "FileEditOutside";
    const char *file_edit_excel = "FileEditExcel";
    const char *file_apply = "FileApply";
    const char *file_close = "FileClose";

    const char *edit_add_col = "EditAddColumn";
    const char *edit_del_col = "EditDeleteColumn";
    const char *edit_move_left = "EditMoveLeft";
    const char *edit_move_right = "EditMoveRight";
    const char *edit_hide_col = "EditHideColumn";
    const char *edit_show_all_cols = "EditShowAllColumns";
    const char *edit_add_row = "EditAddRow";
    const char *edit_del_row = "EditDeleteRow";
    const char *edit_delete_level = "EditDeleteLevel";
    const char *edit_clone_instance = "EditCloneInstance";
    const char *edit_clone_tree = "EditCloneTree";
    const char *edit_lock = "EditLock";
    const char *edit_unlock = "EditUnlock";
    const char *edit_comment = "EditComment";

    const char *view_search = "ViewSearch";
    const char *view_open = "ViewOpenInstance";
    const char *view_preview = "ViewPreviewInstance";
    const char *view_log = "ViewLog";

    const char *format_narrow = "FormatNarrow";
    const char *format_wider = "FormatWider";
    const char *format_reset = "FormatReset";

    const char *tools_validate = "ToolsValidate";
    const char *tools_native = "ToolsNative";
    const char *tools_enhanced = "ToolsEnhanced";

    const char *help_about = "HelpAbout";
};

struct DialogState {
    core::FtWorkspace *workspace = nullptr;
    DialogConfig config;
    application::FtReplicaSession replica;
    FtGridRenderState grid_state;
    std::wstring last_file_path;
    std::wstring last_focused_instance_name;
    std::wstring filter_text;
    std::wstring status_text;
    std::set<std::wstring> hidden_columns;
    std::set<std::wstring> expanded_level_paths;
    std::vector<std::string> level_item_names;
    std::vector<std::wstring> level_item_paths;
    int level_list_min_columns = 20;
    bool splitter_dragging = false;
    int splitter_drag_start_screen_x = 0;
    int splitter_drag_start_min_columns = 20;
    bool rendering = false;
};

core::FtLevelNode *ActiveLevel(DialogState &state)
{
    if (state.workspace == nullptr) return nullptr;
    if (state.workspace->active_level_path.empty() && !state.workspace->level_nodes.empty()) {
        state.workspace->active_level_path = state.workspace->level_nodes.front().level_path;
    }
    for (auto &level : state.workspace->level_nodes) {
        if (level.level_path == state.workspace->active_level_path) return &level;
    }
    if (!state.workspace->level_nodes.empty()) return &state.workspace->level_nodes.front();
    return nullptr;
}

const core::FtColumn *FindColumn(const core::FtLevelNode &level, const std::wstring &key)
{
    for (const auto &column : level.columns) if (column.column_key == key) return &column;
    return nullptr;
}

core::FtCell *FindCell(core::FtRow &row, const std::wstring &key)
{
    for (auto &cell : row.cells) if (cell.column_key == key) return &cell;
    return nullptr;
}

core::FtLevelNode *FindLevelByPath(core::FtWorkspace &workspace, const std::wstring &path)
{
    for (auto &level : workspace.level_nodes) {
        if (level.level_path == path) return &level;
    }
    return nullptr;
}

core::FtRow *FindRow(core::FtLevelNode &level, const std::wstring &name)
{
    for (auto &row : level.rows) {
        if (row.instance_name == name || row.original_instance_name == name) return &row;
    }
    return nullptr;
}

std::wstring GetInputValue(char *dialog, const char *comp)
{
    wchar_t *value = nullptr;
    std::wstring out;
    if (ProUIInputpanelValueGet(dialog, const_cast<char *>(comp), &value) == PRO_TK_NO_ERROR && value != nullptr) {
        out = value;
        ProWstringFree(value);
    }
    return out;
}

void SetInputValue(char *dialog, const char *comp, const std::wstring &value)
{
    ProUIInputpanelValueSet(dialog, const_cast<char *>(comp), const_cast<wchar_t *>(value.c_str()));
}

void Message(ProUIMessageType type, const std::wstring &title, const std::wstring &message)
{
    ShowSimpleMessageDialog(type, title.c_str(), message.c_str());
}

bool Confirm(const std::wstring &title, const std::wstring &msg)
{
    ProUIMessageButton *buttons = nullptr;
    if (ProArrayAlloc(0, sizeof(ProUIMessageButton), 1, reinterpret_cast<ProArray *>(&buttons)) != PRO_TK_NO_ERROR) return false;
    ProUIMessageButton yes = PRO_UI_MESSAGE_YES;
    ProUIMessageButton no = PRO_UI_MESSAGE_NO;
    ProArrayObjectAdd(reinterpret_cast<ProArray *>(&buttons), PRO_VALUE_UNUSED, 1, &yes);
    ProArrayObjectAdd(reinterpret_cast<ProArray *>(&buttons), PRO_VALUE_UNUSED, 1, &no);
    ProUIMessageButton choice = PRO_UI_MESSAGE_NO;
    ProUIMessageDialogDisplay(PROUIMESSAGE_QUESTION,
                              const_cast<wchar_t *>(title.c_str()),
                              const_cast<wchar_t *>(msg.c_str()),
                              buttons,
                              PRO_UI_MESSAGE_NO,
                              &choice);
    ProArrayFree(reinterpret_cast<ProArray *>(&buttons));
    return choice == PRO_UI_MESSAGE_YES;
}

void SetStatus(DialogState &state, const std::wstring &text)
{
    state.status_text = text;
}

void DrawLevelSplitter(char *dialog, const DialogState &state)
{
    if (dialog == nullptr) return;
    const char *component = state.config.level_splitter;
    if (component == nullptr) return;

    int width = 0;
    int height = 0;
    if (ProUIDrawingareaSizeGet(dialog, const_cast<char *>(component), &width, &height) != PRO_TK_NO_ERROR ||
        width <= 0 ||
        height <= 0) {
        return;
    }

    ProUIDrawingareaBgcolorSet(dialog, const_cast<char *>(component), PRO_UI_COLOR_WINDOW_BACKGROUND);
    ProUIDrawingareaFgcolorSet(dialog, const_cast<char *>(component), PRO_UI_COLOR_2D_LIGHT_SHADOW);
    ProUIDrawingareaClear(dialog, const_cast<char *>(component));

    ProUIPoint start = {};
    ProUIPoint end = {};
    start.x = width / 2;
    start.y = 0;
    end.x = width / 2;
    end.y = std::max(0, height - 1);
    ProUIDrawingareaLineDraw(dialog, const_cast<char *>(component), &start, &end);
}

std::wstring LeafName(const std::wstring &path)
{
    const size_t pos = path.rfind(L'/');
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

std::wstring ParentPath(const std::wstring &path)
{
    const size_t pos = path.rfind(L'/');
    if (pos == std::wstring::npos) return L"";
    return path.substr(0, pos);
}

core::FtLevelNode *FindLevel(core::FtWorkspace &workspace, const std::wstring &path)
{
    return FindLevelByPath(workspace, path);
}

bool HasLevelPath(const core::FtWorkspace &workspace, const std::wstring &path)
{
    for (const auto &level : workspace.level_nodes) if (level.level_path == path) return true;
    return false;
}

bool IsKnownLeafLevelPath(const core::FtWorkspace &workspace, const std::wstring &path)
{
    return std::find(workspace.known_leaf_level_paths.begin(),
                     workspace.known_leaf_level_paths.end(),
                     path) != workspace.known_leaf_level_paths.end();
}

void MarkKnownLeafLevelPath(core::FtWorkspace &workspace, const std::wstring &path)
{
    if (path.empty() || IsKnownLeafLevelPath(workspace, path)) return;
    workspace.known_leaf_level_paths.push_back(path);
}

void UnmarkKnownLeafLevelPath(core::FtWorkspace &workspace, const std::wstring &path)
{
    workspace.known_leaf_level_paths.erase(
        std::remove(workspace.known_leaf_level_paths.begin(), workspace.known_leaf_level_paths.end(), path),
        workspace.known_leaf_level_paths.end());
}

void RemoveLevelNodesBySubtree(core::FtWorkspace &workspace, const std::wstring &root_level_path)
{
    workspace.level_nodes.erase(
        std::remove_if(workspace.level_nodes.begin(),
                       workspace.level_nodes.end(),
                       [&](const core::FtLevelNode &candidate) {
                           return candidate.level_path == root_level_path ||
                                  (candidate.level_path.size() > root_level_path.size() &&
                                   candidate.level_path.compare(0, root_level_path.size(), root_level_path) == 0 &&
                                   candidate.level_path[root_level_path.size()] == L'/');
                       }),
        workspace.level_nodes.end());
}

void EnsureChildLevelPlaceholders(core::FtWorkspace &workspace)
{
    const size_t original_count = workspace.level_nodes.size();
    for (size_t i = 0; i < original_count; ++i) {
        const std::wstring level_path = workspace.level_nodes[i].level_path;
        const std::wstring level_generic_name = workspace.level_nodes[i].generic_name;
        const int level_depth = workspace.level_nodes[i].level_depth;
        const ProMdlType level_model_type = workspace.level_nodes[i].model_type;
        const bool level_pending = workspace.level_nodes[i].pending_resolve;
        const bool level_has_family_table = workspace.level_nodes[i].has_family_table;
        const auto rows = workspace.level_nodes[i].rows;
        if (level_pending || !level_has_family_table) continue;
        for (const auto &row : rows) {
            if (row.row_kind != core::FtRowKind::Instance || row.instance_name.empty()) continue;
            if (row.action == core::FtRowAction::Delete || row.change_kind == core::FtChangeKind::Delete) continue;
            if (row.suppress_child_placeholder) continue;
            const std::wstring child_path = level_path + L"/" + row.instance_name;
            if (IsKnownLeafLevelPath(workspace, child_path)) continue;
            if (HasLevelPath(workspace, child_path)) continue;

            core::FtLevelNode child;
            child.level_path = child_path;
            child.level_depth = level_depth + 1;
            child.generic_name = row.instance_name;
            child.parent_generic_name = level_generic_name;
            child.parent_instance_name = row.instance_name;
            child.pending_parent_level_path = level_path;
            child.pending_resolve = true;
            child.model_type = level_model_type;
            workspace.level_nodes.push_back(child);
        }
    }
}

void RebuildLevelChildren(core::FtWorkspace &workspace)
{
    for (auto &level : workspace.level_nodes) {
        level.children.clear();
    }

    std::map<std::wstring, size_t> index_by_path;
    for (size_t i = 0; i < workspace.level_nodes.size(); ++i) {
        index_by_path[workspace.level_nodes[i].level_path] = i;
    }

    for (size_t i = 0; i < workspace.level_nodes.size(); ++i) {
        const auto &level = workspace.level_nodes[i];
        if (level.level_path == L"TOP") continue;

        const std::wstring parent_path = level.pending_parent_level_path.empty()
            ? ParentPath(level.level_path)
            : level.pending_parent_level_path;
        if (parent_path.empty()) continue;

        const auto parent_it = index_by_path.find(parent_path);
        if (parent_it == index_by_path.end()) continue;
        workspace.level_nodes[parent_it->second].children.push_back(i);
    }
}

int ChildOrderKey(const core::FtLevelNode &parent, const core::FtLevelNode &child)
{
    for (size_t i = 0; i < parent.rows.size(); ++i) {
        const auto &row = parent.rows[i];
        if (row.row_kind == core::FtRowKind::Instance && row.instance_name == child.parent_instance_name) {
            return static_cast<int>(i);
        }
    }
    return 1000000;
}

std::vector<size_t> BuildLevelDisplayOrder(core::FtWorkspace &workspace)
{
    RebuildLevelChildren(workspace);

    auto sort_children = [&](size_t parent_index) {
        auto &children = workspace.level_nodes[parent_index].children;
        std::sort(children.begin(), children.end(), [&](size_t a, size_t b) {
            const auto &parent = workspace.level_nodes[parent_index];
            const auto &la = workspace.level_nodes[a];
            const auto &lb = workspace.level_nodes[b];
            const int ka = ChildOrderKey(parent, la);
            const int kb = ChildOrderKey(parent, lb);
            if (ka != kb) return ka < kb;
            if (la.level_depth != lb.level_depth) return la.level_depth < lb.level_depth;
            return la.level_path < lb.level_path;
        });
    };

    for (size_t i = 0; i < workspace.level_nodes.size(); ++i) {
        sort_children(i);
    }

    std::vector<size_t> order;
    std::vector<bool> visited(workspace.level_nodes.size(), false);

    std::function<void(size_t)> walk = [&](size_t index) {
        if (index >= workspace.level_nodes.size() || visited[index]) return;
        visited[index] = true;
        order.push_back(index);
        for (size_t child_index : workspace.level_nodes[index].children) {
            walk(child_index);
        }
    };

    size_t top_index = workspace.level_nodes.size();
    for (size_t i = 0; i < workspace.level_nodes.size(); ++i) {
        if (workspace.level_nodes[i].level_path == L"TOP") {
            top_index = i;
            break;
        }
    }
    if (top_index < workspace.level_nodes.size()) {
        walk(top_index);
    }

    std::vector<size_t> remainder;
    for (size_t i = 0; i < workspace.level_nodes.size(); ++i) {
        if (!visited[i]) remainder.push_back(i);
    }
    std::sort(remainder.begin(), remainder.end(), [&](size_t a, size_t b) {
        const auto &la = workspace.level_nodes[a];
        const auto &lb = workspace.level_nodes[b];
        if (la.level_depth != lb.level_depth) return la.level_depth < lb.level_depth;
        return la.level_path < lb.level_path;
    });
    for (size_t i : remainder) {
        walk(i);
    }
    return order;
}

bool HasChildLevels(const core::FtWorkspace &workspace, const core::FtLevelNode &level)
{
    if (!level.children.empty()) return true;
    for (const auto &candidate : workspace.level_nodes) {
        const std::wstring parent_path = candidate.pending_parent_level_path.empty()
            ? ParentPath(candidate.level_path)
            : candidate.pending_parent_level_path;
        if (parent_path == level.level_path) return true;
    }
    return false;
}

void EnsureExpandedAncestors(DialogState &state, const std::wstring &level_path)
{
    if (state.workspace == nullptr || level_path.empty()) return;
    state.expanded_level_paths.insert(L"TOP");
    std::wstring current = ParentPath(level_path);
    while (!current.empty()) {
        state.expanded_level_paths.insert(current);
        current = ParentPath(current);
    }
}

bool IsLevelVisibleByExpansion(const DialogState &state, const core::FtLevelNode &level)
{
    if (level.level_path == L"TOP") return true;
    std::wstring parent = level.pending_parent_level_path.empty()
        ? ParentPath(level.level_path)
        : level.pending_parent_level_path;
    while (!parent.empty()) {
        if (state.expanded_level_paths.find(parent) == state.expanded_level_paths.end()) return false;
        if (parent == L"TOP") return true;
        const core::FtLevelNode *parent_level = state.workspace == nullptr ? nullptr : FindLevel(*state.workspace, parent);
        parent = parent_level == nullptr
            ? ParentPath(parent)
            : (parent_level->pending_parent_level_path.empty() ? ParentPath(parent_level->level_path) : parent_level->pending_parent_level_path);
    }
    return true;
}

bool PathInSubtree(const std::wstring &path, const std::wstring &root_path)
{
    if (path == root_path) return true;
    return path.size() > root_path.size() &&
           path.compare(0, root_path.size(), root_path) == 0 &&
           path[root_path.size()] == L'/';
}

bool ResolvePendingLevelForBrowse(core::FtWorkspace &workspace, core::FtLevelNode &level, std::wstring &error_out);

bool EnsureCloneSubtreeLoaded(core::FtWorkspace &workspace, const std::wstring &root_path, std::wstring &error_out)
{
    error_out.clear();
    std::set<std::wstring> no_famtable_paths;

    for (;;) {
        bool resolved_any = false;
        std::vector<std::wstring> pending_paths;
        for (const auto &level : workspace.level_nodes) {
            if (level.pending_resolve && PathInSubtree(level.level_path, root_path)) {
                pending_paths.push_back(level.level_path);
            }
        }
        if (pending_paths.empty()) {
            return true;
        }

        for (const auto &level_path : pending_paths) {
            if (no_famtable_paths.find(level_path) != no_famtable_paths.end()) continue;
            core::FtLevelNode *level = FindLevel(workspace, level_path);
            if (level == nullptr || !level->pending_resolve) continue;

            std::wstring resolve_error;
            if (ResolvePendingLevelForBrowse(workspace, *level, resolve_error)) {
                UnmarkKnownLeafLevelPath(workspace, level_path);
                application::ReadFamilyTableWorkspace(workspace, false);
                EnsureChildLevelPlaceholders(workspace);
                RebuildLevelChildren(workspace);
                application::RefreshFtWorkspaceDiff(workspace);
                resolved_any = true;
                break;
            }

            if (resolve_error.find(L"has no family table") != std::wstring::npos) {
                no_famtable_paths.insert(level_path);
                MarkKnownLeafLevelPath(workspace, level_path);
                RemoveLevelNodesBySubtree(workspace, level_path);
                RebuildLevelChildren(workspace);
                continue;
            }

            error_out = resolve_error;
            return false;
        }

        if (!resolved_any) {
            return true;
        }
    }
}

bool HasUsableFamtable(ProMdl mdl, ProFamtable *out)
{
    if (mdl == nullptr || out == nullptr) return false;
    std::memset(out, 0, sizeof(*out));
    if (ProFamtableInit(mdl, out) != PRO_TK_NO_ERROR) return false;
    const ProError st = ProFamtableCheck(out);
    return st == PRO_TK_NO_ERROR || st == PRO_TK_EMPTY;
}

bool RefreshLevelFamtableHandle(core::FtLevelNode &level)
{
    if (level.generic_mdl == nullptr) return false;
    ProFamtable refreshed = {};
    if (!HasUsableFamtable(level.generic_mdl, &refreshed)) {
        return false;
    }
    level.famtable = refreshed;
    level.has_family_table = true;
    return true;
}

bool ResolvePendingLevelForBrowse(core::FtWorkspace &workspace, core::FtLevelNode &level, std::wstring &error_out)
{
    error_out.clear();
    if (!level.pending_resolve) return true;

    const std::wstring parent_level_path = level.pending_parent_level_path.empty() ? ParentPath(level.level_path) : level.pending_parent_level_path;
    core::FtLevelNode *parent_level = FindLevel(workspace, parent_level_path);
    if (parent_level == nullptr || !parent_level->has_family_table) {
        error_out = L"Parent level is not ready: " + parent_level_path;
        return false;
    }

    ProName inst_name = {0};
    wcsncpy_s(inst_name, level.parent_instance_name.c_str(), _TRUNCATE);
    ProFaminstance inst = {};
    ProMdl child_model = nullptr;
    ProError st = ProFaminstanceInit(inst_name, &parent_level->famtable, &inst);
    if (st == PRO_TK_NO_ERROR) {
        st = ProFaminstanceMdlGet(&inst, &child_model);
        if (st != PRO_TK_NO_ERROR || child_model == nullptr) {
            st = ProFaminstanceRetrieve(&inst, &child_model);
        }
    }
    if (st != PRO_TK_NO_ERROR || child_model == nullptr) {
        if (RefreshLevelFamtableHandle(*parent_level)) {
            std::memset(&inst, 0, sizeof(inst));
            child_model = nullptr;
            st = ProFaminstanceInit(inst_name, &parent_level->famtable, &inst);
            if (st == PRO_TK_NO_ERROR) {
                st = ProFaminstanceMdlGet(&inst, &child_model);
                if (st != PRO_TK_NO_ERROR || child_model == nullptr) {
                    st = ProFaminstanceRetrieve(&inst, &child_model);
                }
            }
        }
    }
    if (st != PRO_TK_NO_ERROR || child_model == nullptr) {
        error_out = L"Cannot retrieve child model: " + level.parent_instance_name;
        return false;
    }

    ProFamtable child_ft = {};
    if (!HasUsableFamtable(child_model, &child_ft)) {
        error_out = L"Selected level model has no family table: " + level.parent_instance_name;
        return false;
    }

    level.generic_mdl = child_model;
    level.famtable = child_ft;
    level.has_family_table = true;
    level.pending_resolve = false;
    level.generic_name = LeafName(level.level_path);
    for (auto &column : level.columns) {
        if (!column.has_creo_item) continue;
        column.creo_item_owner = child_model;
    }
    return true;
}

std::wstring BuildNextCloneName(const core::FtLevelNode &parent_level, const std::wstring &source_name)
{
    auto exists = [&](const std::wstring &candidate) {
        for (const auto &row : parent_level.rows) {
            if (row.row_kind == core::FtRowKind::Instance && row.instance_name == candidate) return true;
        }
        return false;
    };

    const std::wstring base = source_name + L"_INST";
    if (!exists(base)) return base;

    for (int attempt = 2; attempt < 10000; ++attempt) {
        const std::wstring candidate = base + std::to_wstring(attempt);
        if (!exists(candidate)) return candidate;
    }
    return base + L"_9999";
}

bool CanQuickClone(const DialogState &state, const core::FtLevelNode *level)
{
    if (state.workspace == nullptr || level == nullptr) return false;
    if (level->enhanced_clone) return false;
    if (!level->parent_instance_name.empty()) {
        const std::wstring parent_path = level->pending_parent_level_path.empty() ? ParentPath(level->level_path) : level->pending_parent_level_path;
        const core::FtLevelNode *parent_level = parent_path.empty() ? nullptr : FindLevel(*state.workspace, parent_path);
        return parent_level != nullptr && parent_level->has_family_table && !parent_level->enhanced_clone;
    }
    return level->has_family_table;
}

bool CanDeleteSelectedLevel(const DialogState &state, const core::FtLevelNode *level)
{
    if (state.workspace == nullptr || level == nullptr) return false;
    if (level->level_path == L"TOP") return false;
    if (level->parent_instance_name.empty()) return false;
    const std::wstring parent_path = level->pending_parent_level_path.empty() ? ParentPath(level->level_path) : level->pending_parent_level_path;
    return !parent_path.empty() && FindLevel(*state.workspace, parent_path) != nullptr;
}

std::wstring TrimCopy(std::wstring text)
{
    const auto is_space = [](wchar_t ch) { return std::iswspace(ch) != 0; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), [&](wchar_t ch) { return !is_space(ch); }));
    text.erase(std::find_if(text.rbegin(), text.rend(), [&](wchar_t ch) { return !is_space(ch); }).base(), text.end());
    return text;
}

int ParseCloneCount(const std::wstring &text)
{
    const std::wstring trimmed = TrimCopy(text);
    if (trimmed.empty()) return 1;
    if (!std::all_of(trimmed.begin(), trimmed.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; })) {
        return 1;
    }
    try {
        const int value = std::stoi(trimmed);
        return value > 0 ? value : 1;
    } catch (...) {
        return 1;
    }
}

std::wstring LevelLabel(const core::FtLevelNode &level)
{
    std::wstring label(static_cast<size_t>(std::max(0, level.level_depth)) * 2, L' ');
    label += L"  ";
    if (level.level_path == L"TOP") {
        label += level.generic_name.empty() ? L"TOP" : (L"TOP | " + level.generic_name);
    } else {
        label += LeafName(level.level_path);
    }
    if (level.pending_resolve) label += L" *";
    return label;
}

std::wstring LevelPathByItem(const DialogState &state, const std::string &item_name)
{
    for (size_t i = 0; i < state.level_item_names.size() && i < state.level_item_paths.size(); ++i) {
        if (state.level_item_names[i] == item_name) return state.level_item_paths[i];
    }
    return L"";
}

std::wstring SelectedLevelPath(char *dialog, const DialogState &state)
{
    char **selected_names = nullptr;
    int selected_count = 0;
    if (ProUIListSelectednamesGet(dialog,
                                  const_cast<char *>(state.config.level_list),
                                  &selected_count,
                                  &selected_names) == PRO_TK_NO_ERROR &&
        selected_count > 0 &&
        selected_names != nullptr &&
        selected_names[0] != nullptr) {
        const std::wstring path = LevelPathByItem(state, selected_names[0]);
        ProStringarrayFree(selected_names, selected_count);
        if (!path.empty()) return path;
    } else if (selected_names != nullptr) {
        ProStringarrayFree(selected_names, selected_count);
    }

    char *last_entered = nullptr;
    if (ProUIListLastentereditemGet(dialog,
                                    const_cast<char *>(state.config.level_list),
                                    &last_entered) == PRO_TK_NO_ERROR &&
        last_entered != nullptr) {
        const std::wstring path = LevelPathByItem(state, last_entered);
        ProStringFree(last_entered);
        if (!path.empty()) return path;
    }

    return state.workspace == nullptr ? L"" : state.workspace->active_level_path;
}

bool LooksLikeWorkbookPath(const std::wstring &text)
{
    return text.find(L".xls") != std::wstring::npos || text.find(L".xml") != std::wstring::npos;
}

void SyncReplica(DialogState &state)
{
    if (state.workspace == nullptr) return;
    if (state.workspace->active_level_path.empty() && !state.workspace->level_nodes.empty()) {
        state.workspace->active_level_path = state.workspace->level_nodes.front().level_path;
    }
    state.replica = application::BuildFtReplicaSession(*state.workspace);
}

void ApplyReplicaRules(DialogState &state)
{
    core::FtLevelNode *level = ActiveLevel(state);
    if (level == nullptr) return;
    application::ApplyFtReplicaColumnRules(*level, state.replica);
    for (auto &column : level->columns) {
        if (state.hidden_columns.find(column.column_key) != state.hidden_columns.end()) {
            column.visible = false;
        }
    }
}

void HarvestActiveGrid(char *dialog, DialogState &state)
{
    if (core::FtLevelNode *level = ActiveLevel(state)) {
        HarvestFamilyTableGridEditor(dialog, state.config.grid, *level, state.grid_state);
        for (const auto &rename : state.grid_state.renamed_instances) {
            core::FtRow *row = nullptr;
            for (auto &candidate : level->rows) {
                if (candidate.row_kind == core::FtRowKind::Instance && candidate.instance_name == rename.second) {
                    row = &candidate;
                    break;
                }
            }
            if (row != nullptr && row->enhanced_clone) {
                row->instance_name = rename.first;
                if (core::FtCell *cell = FindCell(*row, L"INSTANCE_NAME")) {
                    cell->value = rename.first;
                    cell->changed = false;
                    cell->change_kind = core::FtChangeKind::None;
                }
                SetStatus(state, L"Enhanced-clone instances cannot be renamed in plugin mode. Use the official family-table editor.");
                continue;
            }
            application::RenameFtInstanceSubtree(*state.workspace, level->level_path, rename.first, rename.second);
        }
        state.grid_state.renamed_instances.clear();
        application::ApplyFtReplicaEditsToWorkspace(*state.workspace, state.replica);
    }
}

void RenderStatus(char *dialog, DialogState &state)
{
    SyncReplica(state);
    std::wstring status = state.status_text.empty() ? state.replica.summary : state.status_text;
    if (!state.filter_text.empty()) status += L" | FILTER=" + state.filter_text;
    ProUILabelTextSet(dialog, const_cast<char *>(state.config.status), const_cast<wchar_t *>(status.c_str()));

    std::wstring title = L"Multi-Level Family Table Excel Manager";
    if (core::FtLevelNode *level = ActiveLevel(state)) {
        title += L" - " + level->generic_name;
    }
    if (state.workspace != nullptr && state.workspace->dirty) title += L" *";
    ProUIDialogTitleSet(dialog, const_cast<wchar_t *>(title.c_str()));
}

void SetEnabled(char *dialog, const char *component, bool enabled)
{
    if (enabled) ProUIPushbuttonEnable(dialog, const_cast<char *>(component));
    else ProUIPushbuttonDisable(dialog, const_cast<char *>(component));
}

void RenderMenus(char *dialog, DialogState &state)
{
    const auto &menu = state.replica.menu_state;
    const core::FtLevelNode *level = ActiveLevel(state);
    SetEnabled(dialog, state.config.edit_add_col, menu.can_add_column);
    SetEnabled(dialog, state.config.edit_del_col, menu.can_delete_column);
    SetEnabled(dialog, state.config.edit_add_row, menu.can_add_row);
    SetEnabled(dialog, state.config.edit_del_row, menu.can_delete_row);
    SetEnabled(dialog, state.config.edit_delete_level, CanDeleteSelectedLevel(state, level));
    SetEnabled(dialog, state.config.edit_clone_instance, CanQuickClone(state, level));
    SetEnabled(dialog, state.config.edit_clone_tree, level != nullptr && level->level_path != L"TOP" && !level->enhanced_clone);
    SetEnabled(dialog, state.config.edit_lock, menu.can_lock);
    SetEnabled(dialog, state.config.edit_unlock, menu.can_unlock);
    SetEnabled(dialog, state.config.view_open, menu.can_open_instance);
    SetEnabled(dialog, state.config.view_preview, menu.can_preview_instance);
    SetEnabled(dialog, state.config.file_apply, menu.can_apply);
    SetEnabled(dialog, state.config.tools_enhanced, menu.can_show_enhanced_mode);
}

void RenderLevelTabs(char *dialog, DialogState &state)
{
    state.level_item_names.clear();
    state.level_item_paths.clear();
    if (state.workspace == nullptr) return;
    if (state.expanded_level_paths.empty()) state.expanded_level_paths.insert(L"TOP");
    EnsureExpandedAncestors(state, state.workspace->active_level_path);

    std::vector<size_t> order = BuildLevelDisplayOrder(*state.workspace);

    std::vector<std::wstring> labels;
    labels.reserve(state.workspace->level_nodes.size());
    state.level_item_names.reserve(state.workspace->level_nodes.size());
    state.level_item_paths.reserve(state.workspace->level_nodes.size());

    int active_index = -1;
    size_t visible_pos = 0;
    for (size_t i : order) {
        const auto &level = state.workspace->level_nodes[i];
        if (!IsLevelVisibleByExpansion(state, level)) continue;

        std::wstring label(static_cast<size_t>(std::max(0, level.level_depth)) * 2, L' ');
        if (HasChildLevels(*state.workspace, level)) {
            label += state.expanded_level_paths.find(level.level_path) != state.expanded_level_paths.end() ? L"[-] " : L"[+] ";
        } else {
            label += L"    ";
        }
        label += (level.level_path == L"TOP")
            ? (level.generic_name.empty() ? L"TOP" : (L"TOP | " + level.generic_name))
            : LeafName(level.level_path);
        if (level.pending_resolve) label += L" *";

        state.level_item_names.push_back("LV" + std::to_string(visible_pos));
        state.level_item_paths.push_back(level.level_path);
        labels.push_back(label);
        if (level.level_path == state.workspace->active_level_path) {
            active_index = static_cast<int>(visible_pos);
        }
        ++visible_pos;
    }
    if (active_index < 0 && !state.workspace->level_nodes.empty()) {
        active_index = 0;
        for (size_t i : order) {
            if (IsLevelVisibleByExpansion(state, state.workspace->level_nodes[i])) {
                state.workspace->active_level_path = state.workspace->level_nodes[i].level_path;
                break;
            }
        }
    }

    std::vector<char *> name_ptrs;
    std::vector<wchar_t *> label_ptrs;
    for (auto &name : state.level_item_names) name_ptrs.push_back(const_cast<char *>(name.c_str()));
    for (auto &label : labels) label_ptrs.push_back(const_cast<wchar_t *>(label.c_str()));

    ProUIListColumnsSet(dialog, const_cast<char *>(state.config.level_list), 1);
    ProUIListMincolumnsSet(dialog, const_cast<char *>(state.config.level_list), state.level_list_min_columns);
    ProUIListSelectionpolicySet(dialog, const_cast<char *>(state.config.level_list), PROUISELPOLICY_SINGLE);
    ProUIListNamesSet(dialog, const_cast<char *>(state.config.level_list), static_cast<int>(name_ptrs.size()), name_ptrs.empty() ? nullptr : name_ptrs.data());
    ProUIListLabelsSet(dialog, const_cast<char *>(state.config.level_list), static_cast<int>(label_ptrs.size()), label_ptrs.empty() ? nullptr : label_ptrs.data());
    if (active_index >= 0 && active_index < static_cast<int>(name_ptrs.size())) {
        char *selected[1] = {name_ptrs[static_cast<size_t>(active_index)]};
        ProUIListSelectednamesSet(dialog, const_cast<char *>(state.config.level_list), 1, selected);
    }
}

void RenderAll(char *dialog, DialogState &state)
{
    if (state.rendering) return;
    state.rendering = true;
    if (state.workspace != nullptr) {
        EnsureChildLevelPlaceholders(*state.workspace);
        RebuildLevelChildren(*state.workspace);
    }
    SyncReplica(state);
    ApplyReplicaRules(state);
    RenderLevelTabs(dialog, state);
    if (core::FtLevelNode *level = ActiveLevel(state)) {
        RenderFamilyTableGrid(dialog,
                              state.config.grid,
                              state.config.cell_input_base,
                              *level,
                              state.grid_state,
                              state.filter_text);
    }
    DrawLevelSplitter(dialog, state);
    RenderStatus(dialog, state);
    RenderMenus(dialog, state);
    state.rendering = false;
}

void RefreshWorkspace(char *dialog, DialogState &state)
{
    if (state.workspace != nullptr && state.workspace->dirty) {
        if (!Confirm(L"Refresh Workspace", L"Unsaved edits, including quick-cloned instances, will be discarded. Continue refresh?")) {
            SetStatus(state, L"Refresh canceled. Unsaved workspace changes were kept.");
            RenderStatus(dialog, state);
            return;
        }
    }
    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR || current == nullptr) {
        Message(PROUIMESSAGE_WARNING, L"No active model", L"请先打开 Creo 零件或装配。");
        return;
    }
    state.hidden_columns.clear();
    application::DiscoverFamilyTableWorkspace(current, *state.workspace);
    if (state.workspace->active_level_path.empty() && !state.workspace->level_nodes.empty()) {
        state.workspace->active_level_path = state.workspace->level_nodes.front().level_path;
    }
    application::ReadFamilyTableWorkspace(*state.workspace);
    EnsureChildLevelPlaceholders(*state.workspace);
    RebuildLevelChildren(*state.workspace);
    std::wstring load_all_error;
    const std::wstring root_path = state.workspace->level_nodes.empty()
        ? L"TOP"
        : state.workspace->level_nodes.front().level_path;
    const bool loaded_all = EnsureCloneSubtreeLoaded(*state.workspace, root_path, load_all_error);
    EnsureChildLevelPlaceholders(*state.workspace);
    RebuildLevelChildren(*state.workspace);
    application::RefreshFtWorkspaceDiff(*state.workspace);
    if (!loaded_all) {
        SetStatus(state, L"Family table refreshed, but some child levels could not be loaded: " + load_all_error);
    } else {
        SetStatus(state, L"Family table refreshed and all reachable instance levels loaded.");
    }
    RenderAll(dialog, state);
}

bool ReloadWorkspaceFromCreo(DialogState &state, bool preserve_logs)
{
    if (state.workspace == nullptr) return false;
    std::vector<core::FtLogEntry> preserved_logs;
    if (preserve_logs) preserved_logs = state.workspace->logs;

    ProMdl current = nullptr;
    if (ProMdlCurrentGet(&current) != PRO_TK_NO_ERROR || current == nullptr) {
        return false;
    }
    state.hidden_columns.clear();
    application::DiscoverFamilyTableWorkspace(current, *state.workspace);
    if (state.workspace->active_level_path.empty() && !state.workspace->level_nodes.empty()) {
        state.workspace->active_level_path = state.workspace->level_nodes.front().level_path;
    }
    application::ReadFamilyTableWorkspace(*state.workspace);
    EnsureChildLevelPlaceholders(*state.workspace);
    RebuildLevelChildren(*state.workspace);
    application::RefreshFtWorkspaceDiff(*state.workspace);
    if (preserve_logs) {
        preserved_logs.insert(preserved_logs.end(), state.workspace->logs.begin(), state.workspace->logs.end());
        state.workspace->logs.swap(preserved_logs);
    }
    return true;
}

void FocusedCell(char *dialog, DialogState &state, std::string &row, std::string &col)
{
    char *r = nullptr;
    char *c = nullptr;
    if (ProUITableFocusCellGet(dialog, const_cast<char *>(state.config.grid), &r, &c) == PRO_TK_NO_ERROR) {
        if (r != nullptr) row = r;
        if (c != nullptr) col = c;
    }
    if (r != nullptr) ProStringFree(r);
    if (c != nullptr) ProStringFree(c);
}

core::FtRow *FocusedRow(char *dialog, DialogState &state)
{
    core::FtLevelNode *level = ActiveLevel(state);
    if (level == nullptr) return nullptr;
    std::string row_name;
    std::string col_name;
    FocusedCell(dialog, state, row_name, col_name);
    auto it = state.grid_state.row_index_by_name.find(row_name);
    if (it != state.grid_state.row_index_by_name.end() && it->second < level->rows.size()) {
        core::FtRow *row = &level->rows[it->second];
        if (row->row_kind == core::FtRowKind::Instance) state.last_focused_instance_name = row->instance_name;
        return row;
    }
    if (!state.last_focused_instance_name.empty()) {
        if (core::FtRow *row = FindRow(*level, state.last_focused_instance_name)) return row;
    }
    return nullptr;
}

const core::FtColumn *FocusedColumn(char *dialog, DialogState &state)
{
    core::FtLevelNode *level = ActiveLevel(state);
    if (level == nullptr) return nullptr;
    std::string row_name;
    std::string col_name;
    FocusedCell(dialog, state, row_name, col_name);
    auto it = state.grid_state.column_key_by_name.find(col_name);
    if (it == state.grid_state.column_key_by_name.end()) return nullptr;
    return FindColumn(*level, it->second);
}

std::wstring FocusedInstanceName(char *dialog, DialogState &state)
{
    if (core::FtRow *row = FocusedRow(dialog, state)) {
        if (row->row_kind == core::FtRowKind::Instance) return row->instance_name;
    }
    return GetInputValue(dialog, state.config.quick_input);
}

bool ResolveQuickCloneTarget(char *dialog,
                             DialogState &state,
                             core::FtLevelNode *active_level,
                             core::FtLevelNode *&target_level_out,
                             std::wstring &source_instance_name_out)
{
    target_level_out = nullptr;
    source_instance_name_out.clear();
    if (state.workspace == nullptr || active_level == nullptr) return false;

    if (!active_level->parent_instance_name.empty()) {
        const std::wstring parent_path = active_level->pending_parent_level_path.empty() ? ParentPath(active_level->level_path) : active_level->pending_parent_level_path;
        core::FtLevelNode *parent_level = parent_path.empty() ? nullptr : FindLevel(*state.workspace, parent_path);
        if (parent_level != nullptr && parent_level->has_family_table) {
            target_level_out = parent_level;
            source_instance_name_out = active_level->parent_instance_name;
            return !source_instance_name_out.empty();
        }
    }

    target_level_out = active_level;
    if (core::FtRow *row = FocusedRow(dialog, state)) {
        if (row->row_kind == core::FtRowKind::Instance) {
            source_instance_name_out = row->instance_name;
            return true;
        }
    }

    const std::wstring quick_input = TrimCopy(GetInputValue(dialog, state.config.quick_input));
    if (!quick_input.empty() &&
        !std::all_of(quick_input.begin(), quick_input.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; })) {
        if (FindRow(*target_level_out, quick_input) != nullptr) {
            source_instance_name_out = quick_input;
            return true;
        }
    }
    return false;
}

void RemoveLevelSubtree(core::FtWorkspace &workspace,
                        DialogState &state,
                        const std::wstring &root_level_path)
{
    RemoveLevelNodesBySubtree(workspace, root_level_path);
    MarkKnownLeafLevelPath(workspace, root_level_path);

    for (auto it = state.expanded_level_paths.begin(); it != state.expanded_level_paths.end();) {
        if (PathInSubtree(*it, root_level_path)) it = state.expanded_level_paths.erase(it);
        else ++it;
    }

    if (workspace.active_level_path.empty() || PathInSubtree(workspace.active_level_path, root_level_path)) {
        const std::wstring parent_path = ParentPath(root_level_path);
        workspace.active_level_path = parent_path.empty() ? L"TOP" : parent_path;
    }
}

void SetRowLocked(core::FtRow &row, bool locked)
{
    row.is_locked = locked;
    if (row.action == core::FtRowAction::Keep) row.action = core::FtRowAction::Modify;
    row.change_kind = core::FtChangeKind::Modify;
    if (core::FtCell *cell = FindCell(row, L"IS_LOCKED")) {
        cell->value = locked ? L"是" : L"否";
        cell->changed = true;
        cell->change_kind = core::FtChangeKind::Modify;
    }
}

void LaunchFile(const std::wstring &path)
{
#ifdef _WIN32
    ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
#else
    (void)path;
#endif
}

void AdjustFocusedColumnWidth(char *dialog, DialogState &state, int delta)
{
    std::string row_name;
    std::string column_name;
    FocusedCell(dialog, state, row_name, column_name);
    if (column_name.empty()) {
        SetStatus(state, L"Select a column first.");
        RenderAll(dialog, state);
        return;
    }
    int width = 0;
    if (ProUITableColumnWidthGet(dialog, const_cast<char *>(state.config.grid), const_cast<char *>(column_name.c_str()), &width) != PRO_TK_NO_ERROR) {
        SetStatus(state, L"Cannot query current column width.");
        RenderAll(dialog, state);
        return;
    }
    width = std::max(6, width + delta);
    ProUITableColumnWidthSet(dialog, const_cast<char *>(state.config.grid), const_cast<char *>(column_name.c_str()), width);
    SetStatus(state, L"Column width updated.");
    RenderStatus(dialog, state);
}

void ResetFocusedColumnWidth(char *dialog, DialogState &state)
{
    std::string row_name;
    std::string column_name;
    FocusedCell(dialog, state, row_name, column_name);
    if (column_name.empty()) {
        SetStatus(state, L"Select a column first.");
        RenderAll(dialog, state);
        return;
    }
    ProUITableResetColumnWidth(dialog, const_cast<char *>(state.config.grid), const_cast<char *>(column_name.c_str()));
    SetStatus(state, L"Column width reset to default.");
    RenderStatus(dialog, state);
}

void OnClose(char *dialog, char *, ProAppData)
{
    ProUIDialogExit(dialog, 0);
}

void OnRefresh(char *dialog, char *, ProAppData data)
{
    if (auto *state = reinterpret_cast<DialogState *>(data)) RefreshWorkspace(dialog, *state);
}

void OnTableSelect(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr) return;
    if (core::FtLevelNode *level = ActiveLevel(*state)) {
        const bool had_active_editor = !state->grid_state.active_component_name.empty();
        HarvestActiveGrid(dialog, *state);
        if (had_active_editor) {
            RenderAll(dialog, *state);
        }
    }
}

void OnTableActivate(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr || state->workspace == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    core::FtLevelNode *level = ActiveLevel(*state);
    if (level == nullptr) return;

    std::string row_name;
    std::string col_name;
    FocusedCell(dialog, *state, row_name, col_name);
    if (row_name.empty() || col_name.empty()) return;

    if (ActivateFamilyTableGridEditor(dialog,
                                      state->config.grid,
                                      state->config.cell_input_base,
                                      *level,
                                      state->grid_state,
                                      row_name,
                                      col_name)) {
        SetStatus(*state, L"Editing cell.");
        RenderStatus(dialog, *state);
    }
}

void OnSplitterArm(char *, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr) return;
#ifdef _WIN32
    POINT pt = {};
    if (GetCursorPos(&pt)) {
        state->splitter_dragging = true;
        state->splitter_drag_start_screen_x = pt.x;
        state->splitter_drag_start_min_columns = state->level_list_min_columns;
    }
#else
    state->splitter_dragging = true;
    state->splitter_drag_start_screen_x = 0;
    state->splitter_drag_start_min_columns = state->level_list_min_columns;
#endif
}

void OnSplitterDisarm(char *, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr) return;
    state->splitter_dragging = false;
}

void OnSplitterMove(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr || !state->splitter_dragging) return;
#ifdef _WIN32
    POINT pt = {};
    if (!GetCursorPos(&pt)) return;
    const int delta_px = pt.x - state->splitter_drag_start_screen_x;
    const int desired_columns = std::clamp(state->splitter_drag_start_min_columns + delta_px / 8, 12, 48);
    if (desired_columns == state->level_list_min_columns) return;
    state->level_list_min_columns = desired_columns;
#else
    return;
#endif
    ProUIListMincolumnsSet(dialog, const_cast<char *>(state->config.level_list), state->level_list_min_columns);
    RenderAll(dialog, *state);
}

void OnSplitterRender(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr) return;
    DrawLevelSplitter(dialog, *state);
}

void OnLevelSelect(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr || state->workspace == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    char *selected = nullptr;
    if (ProUIListLastentereditemGet(dialog, const_cast<char *>(state->config.level_list), &selected) != PRO_TK_NO_ERROR || selected == nullptr) {
        return;
    }
    const std::wstring new_level_path = LevelPathByItem(*state, selected);
    ProStringFree(selected);
    if (new_level_path.empty() || new_level_path == state->workspace->active_level_path) return;
    state->workspace->active_level_path = new_level_path;
    state->last_focused_instance_name.clear();
    EnsureExpandedAncestors(*state, new_level_path);
    if (core::FtLevelNode *level = ActiveLevel(*state)) {
        std::wstring error;
        if (level->pending_resolve && level->rows.empty() && level->columns.empty()) {
            if (!ResolvePendingLevelForBrowse(*state->workspace, *level, error)) {
                SetStatus(*state, L"Pending level not loaded from Creo: " + error);
            } else {
                state->expanded_level_paths.insert(level->level_path);
                application::ReadFamilyTableWorkspace(*state->workspace, false);
                EnsureChildLevelPlaceholders(*state->workspace);
                RebuildLevelChildren(*state->workspace);
                application::RefreshFtWorkspaceDiff(*state->workspace);
            }
        }
    }
    SetStatus(*state, L"Switched level: " + new_level_path);
    RenderAll(dialog, *state);
}

void OnLevelActivate(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr || state->workspace == nullptr) return;

    char *selected = nullptr;
    if (ProUIListLastentereditemGet(dialog, const_cast<char *>(state->config.level_list), &selected) != PRO_TK_NO_ERROR || selected == nullptr) {
        return;
    }
    const std::wstring level_path = LevelPathByItem(*state, selected);
    ProStringFree(selected);
    if (level_path.empty()) return;

    core::FtLevelNode *level = FindLevel(*state->workspace, level_path);
    if (level == nullptr || !HasChildLevels(*state->workspace, *level)) return;

    if (state->expanded_level_paths.find(level_path) != state->expanded_level_paths.end()) {
        state->expanded_level_paths.erase(level_path);
        if (state->workspace->active_level_path != level_path &&
            PathInSubtree(state->workspace->active_level_path, level_path)) {
            state->workspace->active_level_path = level_path;
        }
        SetStatus(*state, L"Collapsed level: " + level_path);
    } else {
        state->expanded_level_paths.insert(level_path);
        SetStatus(*state, L"Expanded level: " + level_path);
    }
    RenderAll(dialog, *state);
}

void OnExportCurrent(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr || state->workspace == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    std::wstring out;
    if (application::ExportFtCurrentLevelExcel(*state->workspace, out)) {
        state->last_file_path = out;
        SetInputValue(dialog, state->config.quick_input, out);
        SetStatus(*state, L"Exported current level: " + out);
    } else {
        Message(PROUIMESSAGE_ERROR, L"Export Current", L"导出当前层失败。");
    }
    RenderAll(dialog, *state);
}

void OnExportAll(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr || state->workspace == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    std::wstring out;
    if (application::ExportFtAllLevelsExcel(*state->workspace, out)) {
        state->last_file_path = out;
        SetInputValue(dialog, state->config.quick_input, out);
        SetStatus(*state, L"Exported all family-table levels workbook: " + out);
    } else {
        Message(PROUIMESSAGE_ERROR, L"Export All", L"导出全部层失败。");
    }
    RenderAll(dialog, *state);
}

void OnImport(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr || state->workspace == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    std::wstring path = GetInputValue(dialog, state->config.quick_input);
    if (!LooksLikeWorkbookPath(path)) path = state->last_file_path;
    std::wstring error;
    if (path.empty()) {
        Message(PROUIMESSAGE_WARNING, L"Import", L"请先在输入栏填入导出的 Excel/XML 路径，或先执行一次导出。");
        return;
    }
    if (application::ImportFtExcelToWorkspace(path, *state->workspace, error)) {
        state->last_file_path = path;
        SetInputValue(dialog, state->config.quick_input, path);
        SetStatus(*state, L"Workbook imported into preview workspace. Creo is unchanged until Apply Update.");
    } else {
        Message(PROUIMESSAGE_ERROR, L"Import", error);
    }
    RenderAll(dialog, *state);
}

void OnValidate(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr || state->workspace == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    std::vector<std::wstring> issues;
    if (application::ValidateFtWorkspaceForApply(*state->workspace, issues)) {
        SetStatus(*state, L"Validation passed.");
    } else {
        std::wstring text = L"发现以下问题：\n";
        for (size_t i = 0; i < std::min<size_t>(issues.size(), 12); ++i) text += L"- " + issues[i] + L"\n";
        Message(PROUIMESSAGE_WARNING, L"Validate", text);
        SetStatus(*state, L"Validation found issues. See warning dialog.");
    }
    RenderAll(dialog, *state);
}

void OnApply(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr || state->workspace == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    std::vector<std::wstring> issues;
    if (!application::ValidateFtWorkspaceForApply(*state->workspace, issues)) {
        application::FtLog(*state->workspace,
                           state->workspace->active_level_path,
                           L"ERROR",
                           L"apply-ui",
                           L"Apply blocked in dialog validation. issue_count=" + std::to_wstring(issues.size()),
                           PRO_TK_BAD_INPUTS);
        for (const auto &issue : issues) {
            application::FtLog(*state->workspace,
                               state->workspace->active_level_path,
                               L"ERROR",
                               L"apply-ui-issue",
                               issue,
                               PRO_TK_BAD_INPUTS);
        }
        SetStatus(*state, L"Apply blocked by validation. Native-semantic family-table edits should use Tools > Open Native. See View Log.");
        RenderAll(dialog, *state);
        return;
    }
    application::FtLog(*state->workspace,
                       state->workspace->active_level_path,
                       L"INFO",
                       L"apply-ui",
                       L"Validation passed; apply starts immediately to avoid nested modal dialogs.",
                       PRO_TK_NO_ERROR);
    SetStatus(*state, L"Apply running... please wait.");
    RenderStatus(dialog, *state);
    const ProError st = application::ApplyFtWorkspaceToCreo(*state->workspace);
    SetStatus(*state, L"Apply finished. ProError=" + std::to_wstring(static_cast<int>(st)) + L".");
    application::FtLog(*state->workspace,
                       state->workspace->active_level_path,
                       st == PRO_TK_NO_ERROR ? L"INFO" : L"ERROR",
                       L"apply-ui",
                       L"Apply returned ProError=" + std::to_wstring(static_cast<int>(st)),
                       st);
    if (st != PRO_TK_NO_ERROR) {
        SetStatus(*state, L"Apply finished with failures. Use View Log / autobbox_report.txt.");
        RenderAll(dialog, *state);
        return;
    }
    if (ReloadWorkspaceFromCreo(*state, true)) {
        SetStatus(*state, L"Apply to Creo succeeded and workspace reloaded.");
    } else {
        SetStatus(*state, L"Apply to Creo succeeded. Manual refresh may be needed.");
    }
    RenderAll(dialog, *state);
}

void OnAddColumn(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    core::FtLevelNode *level = state == nullptr ? nullptr : ActiveLevel(*state);
    if (level == nullptr) return;
    HarvestActiveGrid(dialog, *state);

    application::FtAddColumnSpec spec;
    spec.category = core::FtColumnCategory::Parameter;
    spec.object_name = GetInputValue(dialog, state->config.quick_input);
    spec.insert_index = static_cast<int>(level->columns.size());

    std::wstring error;
    if (!PromptFamilyTableAddColumnDialog(spec, error)) {
        if (!error.empty()) Message(PROUIMESSAGE_WARNING, L"Add Column", error);
        return;
    }

    if (!application::AddFtColumn(*level, spec, error)) {
        Message(PROUIMESSAGE_WARNING, L"Add Column", error);
    } else {
        SetInputValue(dialog, state->config.quick_input, spec.object_name);
        SetStatus(*state, L"Added family table item: " + std::wstring(core::FtColumnCategoryName(spec.category)) + L":" + spec.object_name);
    }
    application::RefreshFtWorkspaceDiff(*state->workspace);
    RenderAll(dialog, *state);
}
void OnDeleteColumn(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    core::FtLevelNode *level = state == nullptr ? nullptr : ActiveLevel(*state);
    if (level == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    const core::FtColumn *column = FocusedColumn(dialog, *state);
    if (column == nullptr) return;
    if (!Confirm(L"Delete Column", L"删除选中列只会先修改复刻工作区，确认继续？")) return;
    std::wstring error;
    if (!application::DeleteFtColumn(*level, column->column_key, error)) {
        Message(PROUIMESSAGE_WARNING, L"Delete Column", error);
    } else {
        SetStatus(*state, L"Deleted column: " + column->column_display_name);
    }
    application::RefreshFtWorkspaceDiff(*state->workspace);
    RenderAll(dialog, *state);
}

void MoveFocusedColumn(char *dialog, DialogState &state, int delta)
{
    core::FtLevelNode *level = ActiveLevel(state);
    if (level == nullptr) return;
    const core::FtColumn *column = FocusedColumn(dialog, state);
    if (column == nullptr) return;
    int current_index = -1;
    for (size_t i = 0; i < level->columns.size(); ++i) {
        if (level->columns[i].column_key == column->column_key) current_index = static_cast<int>(i);
    }
    if (current_index < 0) return;
    std::wstring error;
    if (!application::MoveFtColumn(*level, column->column_key, current_index + delta, error)) {
        Message(PROUIMESSAGE_WARNING, L"Move Column", error);
    } else {
        SetStatus(state, L"Moved column: " + column->column_display_name);
    }
    application::RefreshFtWorkspaceDiff(*state.workspace);
    RenderAll(dialog, state);
}

void OnMoveLeft(char *dialog, char *, ProAppData data)
{
    if (auto *state = reinterpret_cast<DialogState *>(data)) MoveFocusedColumn(dialog, *state, -1);
}

void OnMoveRight(char *dialog, char *, ProAppData data)
{
    if (auto *state = reinterpret_cast<DialogState *>(data)) MoveFocusedColumn(dialog, *state, 1);
}

void OnHideColumn(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    const core::FtColumn *column = state == nullptr ? nullptr : FocusedColumn(dialog, *state);
    if (column == nullptr || column->column_category == core::FtColumnCategory::Fixed) return;
    state->hidden_columns.insert(column->column_key);
    SetStatus(*state, L"Hidden column: " + column->column_display_name);
    RenderAll(dialog, *state);
}

void OnShowAllColumns(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr) return;
    state->hidden_columns.clear();
    SetStatus(*state, L"All columns are visible again.");
    RenderAll(dialog, *state);
}

void OnAddRow(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    core::FtLevelNode *level = state == nullptr ? nullptr : ActiveLevel(*state);
    if (level == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    const std::wstring instance_name = GetInputValue(dialog, state->config.quick_input);
    std::wstring error;
    if (!application::AddFtInstanceRow(*level, instance_name, error)) {
        Message(PROUIMESSAGE_WARNING, L"Add Instance", error);
    } else {
        SetStatus(*state, L"Added instance row: " + instance_name);
    }
    application::RefreshFtWorkspaceDiff(*state->workspace);
    RenderAll(dialog, *state);
}

void OnDeleteRow(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    core::FtLevelNode *level = state == nullptr ? nullptr : ActiveLevel(*state);
    if (level == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    core::FtRow *row = FocusedRow(dialog, *state);
    if (row == nullptr || row->row_kind == core::FtRowKind::Generic) return;
    std::wstring instance_name = row->instance_name;
    const std::wstring removed_level_path = level->level_path + L"/" + instance_name;
    std::wstring error;
    if (!application::DeleteFtInstanceRow(*state->workspace, level->level_path, row->instance_name, false, error)) {
        if (!Confirm(L"Delete Instance", error + L"\n是否强制删除？")) return;
        if (!application::DeleteFtInstanceRow(*state->workspace, level->level_path, row->instance_name, true, error)) {
            Message(PROUIMESSAGE_WARNING, L"Delete Instance", error);
            return;
        }
    }
    RemoveLevelSubtree(*state->workspace, *state, removed_level_path);
    SetStatus(*state, L"Deleted instance row: " + instance_name);
    application::RefreshFtWorkspaceDiff(*state->workspace);
    RenderAll(dialog, *state);
}

void OnDeleteLevel(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr || state->workspace == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    const std::wstring selected_level_path = SelectedLevelPath(dialog, *state);
    core::FtLevelNode *level = selected_level_path.empty() ? nullptr : FindLevel(*state->workspace, selected_level_path);
    if (level == nullptr) level = ActiveLevel(*state);
    if (level == nullptr) return;

    if (level->level_path == L"TOP" || level->parent_instance_name.empty()) {
        Message(PROUIMESSAGE_WARNING, L"Delete Level", L"TOP 不能删除，请选择一个实例层级。");
        return;
    }

    const std::wstring parent_level_path = level->pending_parent_level_path.empty() ? ParentPath(level->level_path) : level->pending_parent_level_path;
    core::FtLevelNode *parent_level = parent_level_path.empty() ? nullptr : FindLevel(*state->workspace, parent_level_path);
    if (parent_level == nullptr) {
        Message(PROUIMESSAGE_WARNING, L"Delete Level", L"未找到父层级，无法删除当前实例层级。");
        return;
    }

    const std::wstring instance_name = level->parent_instance_name;
    std::wstring error;
    if (!application::DeleteFtInstanceRow(*state->workspace, parent_level_path, instance_name, true, error)) {
        Message(PROUIMESSAGE_WARNING, L"Delete Level", error);
        return;
    }

    const std::wstring removed_level_path = level->level_path;
    RemoveLevelSubtree(*state->workspace, *state, removed_level_path);
    application::RefreshFtWorkspaceDiff(*state->workspace);
    application::FtLog(*state->workspace,
                       parent_level_path,
                       L"INFO",
                       L"delete-level",
                       L"Delete selected level " + removed_level_path + L" via parent instance " + instance_name,
                       PRO_TK_NO_ERROR);
    SetStatus(*state, L"Deleted selected level: " + removed_level_path);
    RenderAll(dialog, *state);
}

void OnCloneInstance(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr || state->workspace == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    const std::wstring selected_level_path = SelectedLevelPath(dialog, *state);
    core::FtLevelNode *level = selected_level_path.empty() ? nullptr : FindLevel(*state->workspace, selected_level_path);
    if (level == nullptr) level = ActiveLevel(*state);
    if (level == nullptr) return;

    core::FtLevelNode *target_level = nullptr;
    std::wstring source_instance_name;
    if (!ResolveQuickCloneTarget(dialog, *state, level, target_level, source_instance_name) ||
        target_level == nullptr) {
        Message(PROUIMESSAGE_WARNING, L"Quick Clone", L"请先在层树或表格中选中要复制的实例。");
        return;
    }
    core::FtRow *row = FindRow(*target_level, source_instance_name);
    if (row == nullptr || row->row_kind == core::FtRowKind::Generic) {
        Message(PROUIMESSAGE_WARNING, L"Quick Clone", L"未找到要复制的实例。");
        return;
    }
    if (row->enhanced_clone || target_level->enhanced_clone || level->enhanced_clone) {
        Message(PROUIMESSAGE_WARNING, L"Quick Clone", L"高级复制生成的结果不允许再次在插件内复制，请转官方族表编辑器。");
        return;
    }

    const int copy_count = ParseCloneCount(GetInputValue(dialog, state->config.quick_input));
    std::vector<std::wstring> created_names;
    std::wstring error;
    if (!application::CloneFtInstanceRowsSimple(*state->workspace,
                                                target_level->level_path,
                                                row->instance_name,
                                                copy_count,
                                                created_names,
                                                error)) {
        Message(PROUIMESSAGE_WARNING, L"Quick Clone", error);
        return;
    }

    application::RefreshFtWorkspaceDiff(*state->workspace);
    application::FtLog(*state->workspace,
                       target_level->level_path,
                       L"INFO",
                       L"clone-simple",
                       L"Quick-cloned instance " + row->instance_name + L" count=" + std::to_wstring(copy_count),
                       PRO_TK_NO_ERROR);
    SetStatus(*state,
              L"Quick-cloned " + std::to_wstring(created_names.size()) + L" instance(s) from " +
              row->instance_name + L" on level " + target_level->level_path + L".");
    if (!created_names.empty()) state->last_focused_instance_name = created_names.front();
    RenderAll(dialog, *state);
}

void OnCloneRow(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr || state->workspace == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    const std::wstring selected_level_path = SelectedLevelPath(dialog, *state);
    core::FtLevelNode *level = selected_level_path.empty() ? nullptr : FindLevel(*state->workspace, selected_level_path);
    if (level == nullptr) level = ActiveLevel(*state);
    if (level == nullptr) return;
    const std::wstring source_level_path = level->level_path;
    const std::wstring parent_level_path = level->pending_parent_level_path.empty() ? ParentPath(level->level_path) : level->pending_parent_level_path;
    if (parent_level_path.empty()) {
        Message(PROUIMESSAGE_WARNING, L"Clone Level", L"TOP ????????????????");
        return;
    }
    if (level->enhanced_clone) {
        Message(PROUIMESSAGE_WARNING, L"Clone Level", L"高级复制子树结果不允许再次叠加复制，请转官方族表编辑器。");
        return;
    }

    std::wstring error;
    if (!EnsureCloneSubtreeLoaded(*state->workspace, source_level_path, error)) {
        Message(PROUIMESSAGE_WARNING, L"Clone Level", error);
        return;
    }

    core::FtLevelNode *fresh_level = FindLevel(*state->workspace, source_level_path);
    core::FtLevelNode *parent_level = FindLevel(*state->workspace, parent_level_path);
    if (fresh_level == nullptr) {
        Message(PROUIMESSAGE_WARNING, L"Clone Level", L"???????????");
        return;
    }
    if (parent_level == nullptr) {
        Message(PROUIMESSAGE_WARNING, L"Clone Level", L"???????");
        return;
    }

    const std::wstring source_instance_name = fresh_level->parent_instance_name.empty() ? LeafName(fresh_level->level_path) : fresh_level->parent_instance_name;
    const std::wstring target_instance_name = BuildNextCloneName(*parent_level, source_instance_name);
    if (!application::CloneFtInstanceRowWithChildren(*state->workspace, parent_level_path, source_instance_name, target_instance_name, error)) {
        Message(PROUIMESSAGE_WARNING, L"Clone Level", error);
        return;
    }
    application::RefreshFtWorkspaceDiff(*state->workspace);
    application::FtLog(*state->workspace,
                       parent_level_path,
                       L"WARN",
                       L"clone-enhanced",
                       L"Enhanced subtree clone " + source_instance_name + L" -> " + target_instance_name,
                       PRO_TK_NO_ERROR);
    SetStatus(*state, L"Enhanced subtree clone created: " + source_instance_name + L" -> " + target_instance_name +
                       L". Use the official family-table editor to verify/edit high-risk semantics.");
    if (Confirm(L"Enhanced Clone", L"已创建带子层级的高级复制，建议立即转官方族表验证。\n是否现在打开官方族表编辑器？")) {
        const ProError st = application::BridgeNativeFamilyTableAction(*parent_level, L"edit");
        application::FtLog(*state->workspace,
                           parent_level_path,
                           st == PRO_TK_NO_ERROR ? L"INFO" : L"WARN",
                           L"native-edit",
                           L"Open native family table editor after enhanced clone",
                           st);
    }
    RenderAll(dialog, *state);
}

void OnLock(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    if (core::FtRow *row = FocusedRow(dialog, *state)) {
        if (row->row_kind == core::FtRowKind::Instance) {
            SetRowLocked(*row, true);
            application::RefreshFtWorkspaceDiff(*state->workspace);
            SetStatus(*state, L"Selected instance locked in replica workspace.");
        }
    }
    RenderAll(dialog, *state);
}

void OnUnlock(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    if (core::FtRow *row = FocusedRow(dialog, *state)) {
        if (row->row_kind == core::FtRowKind::Instance) {
            SetRowLocked(*row, false);
            application::RefreshFtWorkspaceDiff(*state->workspace);
            SetStatus(*state, L"Selected instance unlocked in replica workspace.");
        }
    }
    RenderAll(dialog, *state);
}

void OnOpenInstance(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    core::FtLevelNode *level = state == nullptr ? nullptr : ActiveLevel(*state);
    if (level == nullptr) return;
    const std::wstring instance_name = FocusedInstanceName(dialog, *state);
    if (instance_name.empty()) {
        Message(PROUIMESSAGE_WARNING, L"Open Instance", L"请先选中实例行，或在输入栏输入实例名。");
        return;
    }
    const ProError st = application::BridgeNativeFamilyTableAction(*level, L"open-instance", instance_name);
    application::FtLog(*state->workspace,
                       level->level_path,
                       st == PRO_TK_NO_ERROR ? L"INFO" : L"WARN",
                       L"open-instance",
                       L"Open instance " + instance_name,
                       st);
    SetStatus(*state, L"Open instance requested: " + instance_name);
    RenderAll(dialog, *state);
}

void OnPreviewInstance(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    core::FtLevelNode *level = state == nullptr ? nullptr : ActiveLevel(*state);
    if (level == nullptr) return;
    const std::wstring instance_name = FocusedInstanceName(dialog, *state);
    if (instance_name.empty()) {
        Message(PROUIMESSAGE_WARNING, L"Preview Instance", L"请先选中实例行，或在输入栏输入实例名。");
        return;
    }
    const ProError st = application::BridgeNativeFamilyTableAction(*level, L"preview-instance", instance_name);
    application::FtLog(*state->workspace,
                       level->level_path,
                       st == PRO_TK_NO_ERROR ? L"INFO" : L"WARN",
                       L"preview-instance",
                       L"Preview instance " + instance_name,
                       st);
    SetStatus(*state, L"Preview instance requested: " + instance_name);
    RenderAll(dialog, *state);
}

void OnComment(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    core::FtRow *row = FocusedRow(dialog, *state);
    if (row == nullptr || row->row_kind == core::FtRowKind::Generic) {
        Message(PROUIMESSAGE_WARNING, L"Edit Comment", L"请先选中实例行。");
        return;
    }
    const std::wstring comment = GetInputValue(dialog, state->config.quick_input);
    if (core::FtCell *cell = FindCell(*row, L"COMMENT")) {
        cell->value = comment;
        cell->changed = true;
        cell->change_kind = core::FtChangeKind::Modify;
        if (row->action == core::FtRowAction::Keep) row->action = core::FtRowAction::Modify;
        application::FtLog(*state->workspace,
                           ActiveLevel(*state)->level_path,
                           L"INFO",
                           L"comment",
                           L"Comment updated in replica workspace only",
                           PRO_TK_NO_CHANGE);
        SetStatus(*state, L"Comment updated in replica workspace only.");
    }
    RenderAll(dialog, *state);
}

void OnEditOutside(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr || state->workspace == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    std::wstring out;
    if (application::ExportFtCurrentLevelExcel(*state->workspace, out)) {
        state->last_file_path = out;
        SetInputValue(dialog, state->config.quick_input, out);
        LaunchFile(out);
        SetStatus(*state, L"Current level exported and opened externally: " + out);
    } else {
        Message(PROUIMESSAGE_ERROR, L"Edit Outside Creo", L"导出失败。");
    }
    RenderAll(dialog, *state);
}

void OnEditExcel(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr || state->workspace == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    std::wstring out;
    if (application::ExportFtCurrentLevelExcel(*state->workspace, out)) {
        state->last_file_path = out;
        SetInputValue(dialog, state->config.quick_input, out);
        LaunchFile(out);
        SetStatus(*state, L"Current level exported for Excel editing: " + out);
    } else {
        Message(PROUIMESSAGE_ERROR, L"Edit with Excel", L"导出失败。");
    }
    RenderAll(dialog, *state);
}

void OnSearch(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr) return;
    HarvestActiveGrid(dialog, *state);
    state->filter_text = GetInputValue(dialog, state->config.quick_input);
    SetStatus(*state, state->filter_text.empty() ? L"Row filter cleared." : L"Row filter applied.");
    RenderAll(dialog, *state);
}

void OnNative(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    core::FtLevelNode *level = state == nullptr ? nullptr : ActiveLevel(*state);
    if (level == nullptr) return;
    const ProError st = application::BridgeNativeFamilyTableAction(*level, L"edit");
    application::FtLog(*state->workspace,
                       level->level_path,
                       st == PRO_TK_NO_ERROR ? L"INFO" : L"WARN",
                       L"native-edit",
                       L"Open native family table editor",
                       st);
    SetStatus(*state, L"Native family table editor requested.");
    RenderAll(dialog, *state);
}

void OnEnhanced(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state == nullptr) return;
    Message(PROUIMESSAGE_INFO,
            L"Enhanced Mode",
            L"当前默认入口已切换为原生复刻版。\n多层树 / 多 sheet / 跨层搜索 / 全层差异总览将在下一阶段叠加。");
    SetStatus(*state, L"Enhanced mode is reserved for phase 2.");
    RenderAll(dialog, *state);
}

void OnLog(char *, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    if (state != nullptr && state->workspace != nullptr) {
        Message(PROUIMESSAGE_INFO, L"FT_LOG", application::BuildFtLogText(*state->workspace));
    }
}

void OnFormatNarrow(char *dialog, char *, ProAppData data)
{
    if (auto *state = reinterpret_cast<DialogState *>(data)) AdjustFocusedColumnWidth(dialog, *state, -2);
}

void OnFormatWider(char *dialog, char *, ProAppData data)
{
    if (auto *state = reinterpret_cast<DialogState *>(data)) AdjustFocusedColumnWidth(dialog, *state, 2);
}

void OnFormatReset(char *dialog, char *, ProAppData data)
{
    if (auto *state = reinterpret_cast<DialogState *>(data)) ResetFocusedColumnWidth(dialog, *state);
}

void OnHelpAbout(char *dialog, char *, ProAppData data)
{
    auto *state = reinterpret_cast<DialogState *>(data);
    std::wstring text = L"Family Table Replica Mode\n\n"
                        L"- Default entry now follows native single-table workflow.\n"
                        L"- Main shell is aligned to Pro/TABLE editor style.\n"
                        L"- Multi-level tree/sheet enhancements are reserved for phase 2.";
    Message(PROUIMESSAGE_INFO, L"About Replica", text);
    if (state != nullptr) {
        SetStatus(*state, L"Replica help displayed.");
        RenderAll(dialog, *state);
    }
}

} // namespace

bool PromptFamilyTableManagerDialog(core::FtWorkspace &workspace)
{
    DialogState state;
    state.workspace = &workspace;

    if (workspace.active_level_path.empty() && !workspace.level_nodes.empty()) {
        workspace.active_level_path = workspace.level_nodes.front().level_path;
    }

    ProError st = ProUIDialogCreate(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.dialog));
    if (st != PRO_TK_NO_ERROR) {
        st = ProUIDialogCreate(const_cast<char *>(state.config.dialog), const_cast<char *>("resource/autobbox_family_table_manager.res"));
    }
    if (st != PRO_TK_NO_ERROR) {
        Message(PROUIMESSAGE_ERROR,
                L"Dialog create failed",
                L"Cannot load autobbox_family_table_manager.res. ProError=" + std::to_wstring(static_cast<int>(st)));
        return false;
    }

    ProUIInputpanelColumnsSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.quick_input), 96);
    ProUIInputpanelValueSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.quick_input), const_cast<wchar_t *>(L""));

    ProUITableSelectActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.grid), OnTableSelect, &state);
    ProUITableActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.grid), OnTableActivate, &state);
    ProUIListSelectActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.level_list), OnLevelSelect, &state);
    ProUIListActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.level_list), OnLevelActivate, &state);
    ProUIDrawingareaEnableTracking(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.level_splitter));
    ProUIDrawingareaBgcolorSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.level_splitter), PRO_UI_COLOR_WINDOW_BACKGROUND);
    ProUIDrawingareaFgcolorSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.level_splitter), PRO_UI_COLOR_2D_LIGHT_SHADOW);
    ProUIDrawingareaMoveActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.level_splitter), OnSplitterMove, &state);
    ProUIDrawingareaLbuttonarmActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.level_splitter), OnSplitterArm, &state);
    ProUIDrawingareaLbuttondisarmActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.level_splitter), OnSplitterDisarm, &state);
    ProUIDrawingareaPostmanagenotifyActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.level_splitter), OnSplitterRender, &state);
    ProUIDrawingareaResizeActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.level_splitter), OnSplitterRender, &state);
    ProUIDrawingareaUpdateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.level_splitter), OnSplitterRender, &state);

    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.file_refresh), OnRefresh, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.file_import), OnImport, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.file_export_current), OnExportCurrent, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.file_export_all), OnExportAll, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.file_edit_outside), OnEditOutside, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.file_edit_excel), OnEditExcel, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.file_apply), OnApply, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.file_close), OnClose, &state);

    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.edit_add_col), OnAddColumn, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.edit_del_col), OnDeleteColumn, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.edit_move_left), OnMoveLeft, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.edit_move_right), OnMoveRight, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.edit_hide_col), OnHideColumn, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.edit_show_all_cols), OnShowAllColumns, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.edit_add_row), OnAddRow, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.edit_del_row), OnDeleteRow, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.edit_delete_level), OnDeleteLevel, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.edit_clone_instance), OnCloneInstance, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.edit_clone_tree), OnCloneRow, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.edit_lock), OnLock, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.edit_unlock), OnUnlock, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.edit_comment), OnComment, &state);

    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.view_search), OnSearch, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.view_open), OnOpenInstance, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.view_preview), OnPreviewInstance, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.view_log), OnLog, &state);

    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.format_narrow), OnFormatNarrow, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.format_wider), OnFormatWider, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.format_reset), OnFormatReset, &state);

    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.tools_validate), OnValidate, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.tools_native), OnNative, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.tools_enhanced), OnEnhanced, &state);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.help_about), OnHelpAbout, &state);

    ProUIDialogCloseActionSet(const_cast<char *>(state.config.dialog), OnClose, &state);
    ProUIDialogDefaultbuttonSet(const_cast<char *>(state.config.dialog), const_cast<char *>(state.config.file_close));

    RenderAll(const_cast<char *>(state.config.dialog), state);
    int status = 0;
    const ProError act = ProUIDialogActivate(const_cast<char *>(state.config.dialog), &status);
    ProUIDialogDestroy(const_cast<char *>(state.config.dialog));
    return act == PRO_TK_NO_ERROR;
}

} // namespace autobbox::ui
