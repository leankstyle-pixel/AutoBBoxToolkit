#include "autobbox/application/ft_row_ops.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <set>

namespace autobbox::application {
namespace {
core::FtLevelNode *FindLevel(core::FtWorkspace &workspace, const std::wstring &path)
{
    for (auto &level : workspace.level_nodes) if (level.level_path == path) return &level;
    return nullptr;
}
core::FtRow *FindRow(core::FtLevelNode &level, const std::wstring &name)
{
    for (auto &row : level.rows) if (row.instance_name == name) return &row;
    return nullptr;
}

const core::FtRow *FindGenericRow(const core::FtLevelNode &level)
{
    for (const auto &row : level.rows) {
        if (row.row_kind == core::FtRowKind::Generic) return &row;
    }
    return nullptr;
}

const core::FtCell *FindCell(const core::FtRow &row, const std::wstring &column_key)
{
    for (const auto &cell : row.cells) {
        if (cell.column_key == column_key) return &cell;
    }
    return nullptr;
}

const core::FtLevelNode *FindLevel(const core::FtWorkspace &workspace, const std::wstring &path)
{
    for (const auto &level : workspace.level_nodes) if (level.level_path == path) return &level;
    return nullptr;
}

std::wstring ParentPath(const std::wstring &path)
{
    const size_t pos = path.rfind(L'/');
    if (pos == std::wstring::npos) return L"";
    return path.substr(0, pos);
}

std::wstring LeafName(const std::wstring &path)
{
    const size_t pos = path.rfind(L'/');
    return pos == std::wstring::npos ? path : path.substr(pos + 1);
}

bool StartsWithPath(const std::wstring &path, const std::wstring &prefix)
{
    return path.size() >= prefix.size() && path.compare(0, prefix.size(), prefix) == 0;
}

std::wstring ReplacePrefix(const std::wstring &text, const std::wstring &from, const std::wstring &to)
{
    if (!StartsWithPath(text, from)) return text;
    if (text.size() == from.size()) return to;
    return to + text.substr(from.size());
}

std::wstring BuildCloneInstName(const std::wstring &source_name, std::set<std::wstring> &used_names)
{
    const std::wstring base = source_name + L"_INST";
    if (used_names.insert(base).second) return base;

    for (int attempt = 2; attempt < 10000; ++attempt) {
        const std::wstring candidate = base + std::to_wstring(attempt);
        if (used_names.insert(candidate).second) return candidate;
    }

    const std::wstring fallback = base + L"_9999";
    used_names.insert(fallback);
    return fallback;
}

bool IsInheritedPlaceholder(const std::wstring &value)
{
    return value == L"*" || value == L"<GENERIC>";
}

bool IsCloneDeferredValue(const std::wstring &value)
{
    return value.empty() ||
           value == L"*" ||
           value == L"<GENERIC>" ||
           value == L"<UNREADABLE>";
}

void MarkInstanceRowAsNew(core::FtRow &row,
                          const std::wstring &new_instance_name,
                          bool enhanced_clone,
                          bool suppress_child_placeholder)
{
    row.row_kind = core::FtRowKind::Instance;
    row.instance_name = new_instance_name;
    row.original_instance_name.clear();
    row.verify_status = enhanced_clone ? L"ENH_CLONED" : L"CLONED";
    row.enhanced_clone = enhanced_clone;
    row.suppress_child_placeholder = suppress_child_placeholder;
    row.action = core::FtRowAction::New;
    row.change_kind = core::FtChangeKind::New;
    for (auto &cell : row.cells) {
        const bool auto_writable = cell.support_status == core::FtSupportStatus::Full;
        const std::wstring source_value = cell.value;
        const bool defer_to_creo_default = IsCloneDeferredValue(source_value);
        cell.old_value = auto_writable ? (defer_to_creo_default ? source_value : L"") : cell.value;
        cell.change_kind = core::FtChangeKind::New;
        cell.changed = auto_writable && !defer_to_creo_default;
        cell.clone_seeded = auto_writable && !defer_to_creo_default;
        cell.clone_seed_value = defer_to_creo_default ? L"" : source_value;
        if (cell.column_key == L"INSTANCE_NAME") {
            cell.value = new_instance_name;
            cell.changed = true;
            cell.clone_seeded = false;
            cell.clone_seed_value.clear();
        } else if (cell.column_key == L"COMMON_NAME") {
            row.common_name = cell.value;
        } else if (cell.column_key == L"VERIFY_STATUS") {
            cell.value = enhanced_clone ? L"ENH_CLONED" : L"CLONED";
            cell.changed = true;
            cell.clone_seeded = false;
            cell.clone_seed_value.clear();
        }
    }
}

std::vector<std::wstring> BuildCloneInstNames(const core::FtLevelNode &level,
                                              const std::wstring &source_name,
                                              int copy_count)
{
    std::set<std::wstring> used_names;
    for (const auto &row : level.rows) {
        if (row.row_kind == core::FtRowKind::Instance && !row.instance_name.empty()) {
            used_names.insert(row.instance_name);
        }
    }

    std::vector<std::wstring> names;
    names.reserve(static_cast<size_t>(std::max(copy_count, 0)));
    for (int i = 0; i < copy_count; ++i) {
        names.push_back(BuildCloneInstName(source_name, used_names));
    }
    return names;
}
}

bool AddFtInstanceRow(core::FtLevelNode &level, const std::wstring &instance_name, std::wstring &error_out)
{
    if (instance_name.empty()) { error_out = L"Instance name is empty"; return false; }
    if (FindRow(level, instance_name) != nullptr) { error_out = L"Instance already exists"; return false; }
    core::FtRow row;
    row.row_kind = core::FtRowKind::Instance;
    row.instance_name = instance_name;
    row.original_instance_name.clear();
    row.common_name.clear();
    row.verify_status = L"NEW";
    row.enhanced_clone = false;
    row.suppress_child_placeholder = false;
    row.action = core::FtRowAction::New;
    row.change_kind = core::FtChangeKind::New;
    for (const auto &col : level.columns) {
        core::FtCell cell;
        cell.column_key = col.column_key;
        if (col.column_key == L"INSTANCE_NAME") cell.value = instance_name;
        else if (col.column_key == L"COMMON_NAME") cell.value = L"";
        else if (col.column_key == L"ROW_KIND") cell.value = L"INSTANCE";
        else if (col.column_key == L"VERIFY_STATUS") cell.value = L"NEW";
        else if (col.column_key == L"IS_LOCKED" || col.column_key == L"IS_EXT_LOCKED") cell.value = L"FALSE";
        cell.old_value = L"";
        cell.clone_seeded = false;
        cell.editable = col.editable;
        cell.support_status = col.support_status;
        cell.change_kind = core::FtChangeKind::New;
        row.cells.push_back(cell);
    }
    level.rows.push_back(row);
    return true;
}

bool DeleteFtInstanceRow(core::FtWorkspace &workspace, const std::wstring &level_path, const std::wstring &instance_name, bool force, std::wstring &error_out)
{
    core::FtLevelNode *level = FindLevel(workspace, level_path);
    if (level == nullptr) { error_out = L"Level not found"; return false; }
    core::FtRow *row = FindRow(*level, instance_name);
    if (row == nullptr || row->row_kind == core::FtRowKind::Generic) { error_out = L"Instance row not found"; return false; }
    if (row->enhanced_clone) {
        if (row->action != core::FtRowAction::New) {
            error_out = L"Enhanced-clone rows already applied to Creo must be deleted in the official family-table editor.";
            return false;
        }
    }
    for (const auto &child : workspace.level_nodes) {
        if (child.parent_instance_name == instance_name && child.parent_generic_name == level->generic_name && !force) {
            error_out = L"This instance is a child-level generic; delete the child level first or confirm force delete";
            return false;
        }
    }
    if (row->action == core::FtRowAction::New) {
        level->rows.erase(std::remove_if(level->rows.begin(), level->rows.end(), [&](const core::FtRow &r){ return r.instance_name == instance_name; }), level->rows.end());
    } else {
        row->action = core::FtRowAction::Delete;
        row->change_kind = core::FtChangeKind::Delete;
    }
    return true;
}

bool RenameFtInstanceRow(core::FtLevelNode &level, const std::wstring &old_name, const std::wstring &new_name, std::wstring &error_out)
{
    if (new_name.empty()) { error_out = L"New instance name is empty"; return false; }
    if (FindRow(level, new_name) != nullptr) { error_out = L"Target instance already exists"; return false; }
    core::FtRow *row = FindRow(level, old_name);
    if (row == nullptr || row->row_kind == core::FtRowKind::Generic) { error_out = L"Instance row not found"; return false; }
    if (row->enhanced_clone) {
        error_out = L"Enhanced-clone rows cannot be renamed in plugin mode; use the official family-table editor.";
        return false;
    }
    row->instance_name = new_name;
    if (row->original_instance_name.empty()) row->original_instance_name = old_name;
    if (row->action == core::FtRowAction::Keep) row->action = core::FtRowAction::Modify;
    row->change_kind = core::FtChangeKind::Modify;
    for (auto &cell : row->cells) {
        if (cell.column_key == L"INSTANCE_NAME") {
            cell.value = new_name;
            cell.changed = true;
            cell.change_kind = core::FtChangeKind::Modify;
        }
    }
    return true;
}

void RenameFtInstanceSubtree(core::FtWorkspace &workspace,
                             const std::wstring &parent_level_path,
                             const std::wstring &old_instance_name,
                             const std::wstring &new_instance_name)
{
    if (parent_level_path.empty() || old_instance_name.empty() || new_instance_name.empty() || old_instance_name == new_instance_name) {
        return;
    }

    const std::wstring source_root_path = parent_level_path + L"/" + old_instance_name;
    const std::wstring target_root_path = parent_level_path + L"/" + new_instance_name;

    for (auto &level : workspace.level_nodes) {
        const bool in_renamed_subtree = (level.level_path == source_root_path ||
                                         StartsWithPath(level.level_path, source_root_path + L"/"));
        if (in_renamed_subtree) {
            level.level_path = ReplacePrefix(level.level_path, source_root_path, target_root_path);
            level.level_depth = static_cast<int>(std::count(level.level_path.begin(), level.level_path.end(), L'/'));
            level.generic_name = LeafName(level.level_path);
            if (level.pending_parent_level_path == parent_level_path) {
                level.parent_instance_name = new_instance_name;
            }
            level.pending_resolve = true;
            level.generic_mdl = nullptr;
            std::memset(&level.famtable, 0, sizeof(level.famtable));
        }

        if (level.pending_parent_level_path == source_root_path ||
            StartsWithPath(level.pending_parent_level_path, source_root_path + L"/")) {
            level.pending_parent_level_path = ReplacePrefix(level.pending_parent_level_path, source_root_path, target_root_path);
        }

        for (auto &row : level.rows) {
            if (row.row_kind != core::FtRowKind::Generic) continue;
            if (row.instance_name != level.generic_name) {
                row.instance_name = level.generic_name;
                row.original_instance_name = level.generic_name;
            }
            for (auto &cell : row.cells) {
                if (cell.column_key == L"INSTANCE_NAME") {
                    cell.value = level.generic_name;
                    cell.old_value = level.generic_name;
                    cell.changed = false;
                    cell.change_kind = core::FtChangeKind::None;
                }
            }
        }
    }

    if (workspace.active_level_path == source_root_path ||
        StartsWithPath(workspace.active_level_path, source_root_path + L"/")) {
        workspace.active_level_path = ReplacePrefix(workspace.active_level_path, source_root_path, target_root_path);
    }
}

bool CloneFtInstanceRowsSimple(core::FtWorkspace &workspace,
                               const std::wstring &level_path,
                               const std::wstring &source_instance_name,
                               int copy_count,
                               std::vector<std::wstring> &created_names,
                               std::wstring &error_out)
{
    created_names.clear();
    error_out.clear();
    if (copy_count < 1) {
        error_out = L"Copy count must be >= 1";
        return false;
    }

    core::FtLevelNode *level = FindLevel(workspace, level_path);
    if (level == nullptr) { error_out = L"Level not found"; return false; }
    core::FtRow *source_row = FindRow(*level, source_instance_name);
    if (source_row == nullptr || source_row->row_kind == core::FtRowKind::Generic) {
        error_out = L"Source instance row not found";
        return false;
    }
    if (source_row->enhanced_clone) {
        error_out = L"Enhanced-clone rows cannot be copied again in plugin mode. Use the official family-table editor.";
        return false;
    }

    const std::vector<std::wstring> target_names = BuildCloneInstNames(*level, source_instance_name, copy_count);
    for (const auto &target_name : target_names) {
        core::FtRow cloned_row = *source_row;
        MarkInstanceRowAsNew(cloned_row, target_name, false, false);
        level->rows.push_back(cloned_row);
        created_names.push_back(target_name);
    }
    return true;
}

bool CloneFtInstanceRowWithChildren(core::FtWorkspace &workspace,
                                    const std::wstring &level_path,
                                    const std::wstring &source_instance_name,
                                    const std::wstring &target_instance_name,
                                    std::wstring &error_out)
{
    error_out.clear();
    core::FtLevelNode *level = FindLevel(workspace, level_path);
    if (level == nullptr) { error_out = L"Level not found"; return false; }
    if (source_instance_name.empty() || target_instance_name.empty()) { error_out = L"Source or target instance name is empty"; return false; }
    if (source_instance_name == target_instance_name) { error_out = L"Target instance name must be different"; return false; }
    core::FtRow *source_row = FindRow(*level, source_instance_name);
    if (source_row == nullptr || source_row->row_kind == core::FtRowKind::Generic) { error_out = L"Source instance row not found"; return false; }
    if (FindRow(*level, target_instance_name) != nullptr) { error_out = L"Target instance already exists"; return false; }
    if (source_row->enhanced_clone || level->enhanced_clone) {
        error_out = L"Enhanced clone subtrees cannot be copied again in plugin mode. Use the official family-table editor.";
        return false;
    }

    core::FtRow cloned_row = *source_row;
    MarkInstanceRowAsNew(cloned_row, target_instance_name, true, false);
    level->rows.push_back(cloned_row);

    const std::wstring source_root_path = level->level_path + L"/" + source_instance_name;
    const std::wstring target_root_path = level->level_path + L"/" + target_instance_name;

    std::vector<core::FtLevelNode> source_subtree_levels;
    source_subtree_levels.reserve(workspace.level_nodes.size());
    for (const auto &candidate : workspace.level_nodes) {
        if (candidate.level_path == source_root_path ||
            StartsWithPath(candidate.level_path, source_root_path + L"/")) {
            source_subtree_levels.push_back(candidate);
        }
    }
    std::sort(source_subtree_levels.begin(),
              source_subtree_levels.end(),
              [](const core::FtLevelNode &a, const core::FtLevelNode &b) { return a.level_depth < b.level_depth; });

    std::map<std::wstring, std::wstring> target_path_by_source_path;
    std::map<std::wstring, std::map<std::wstring, std::wstring>> cloned_child_name_map;
    target_path_by_source_path[source_root_path] = target_root_path;

    for (const auto &source_level : source_subtree_levels) {
        core::FtLevelNode cloned_level = source_level;
        cloned_level.enhanced_clone = true;
        for (auto &column : cloned_level.columns) {
            // The copied level is later resolved to a different instance model.
            // Any cached owner handle from the source subtree becomes stale and
            // must be rebound to the resolved target generic before apply.
            column.creo_item_owner = nullptr;
        }
        if (source_level.level_path == source_root_path) {
            cloned_level.level_path = target_root_path;
        } else {
            const std::wstring source_parent_path = ParentPath(source_level.level_path);
            const std::wstring source_leaf_name = LeafName(source_level.level_path);
            const auto target_parent_it = target_path_by_source_path.find(source_parent_path);
            const auto child_name_map_it = cloned_child_name_map.find(source_parent_path);
            if (target_parent_it == target_path_by_source_path.end() ||
                child_name_map_it == cloned_child_name_map.end()) {
                error_out = L"Cannot map cloned child path for level: " + source_level.level_path;
                return false;
            }
            const auto renamed_child_it = child_name_map_it->second.find(source_leaf_name);
            if (renamed_child_it == child_name_map_it->second.end()) {
                error_out = L"Cannot map cloned child instance name for level: " + source_level.level_path;
                return false;
            }
            cloned_level.level_path = target_parent_it->second + L"/" + renamed_child_it->second;
        }
        cloned_level.level_depth = static_cast<int>(std::count(cloned_level.level_path.begin(), cloned_level.level_path.end(), L'/'));
        cloned_level.generic_name = LeafName(cloned_level.level_path);
        cloned_level.parent_instance_name = LeafName(cloned_level.level_path);
        cloned_level.pending_parent_level_path = ParentPath(cloned_level.level_path);
        cloned_level.clone_source_level_path = source_level.level_path;
        cloned_level.pending_resolve = true;
        cloned_level.generic_mdl = nullptr;
        std::memset(&cloned_level.famtable, 0, sizeof(cloned_level.famtable));
        cloned_level.children.clear();

        if (const core::FtLevelNode *parent_level = FindLevel(workspace, cloned_level.pending_parent_level_path)) {
            cloned_level.parent_generic_name = parent_level->generic_name;
        }

        std::set<std::wstring> used_instance_names;
        for (const auto &row : source_level.rows) {
            if (row.row_kind == core::FtRowKind::Instance && !row.instance_name.empty()) {
                used_instance_names.insert(row.instance_name);
            }
        }
        std::map<std::wstring, std::wstring> renamed_child_rows;

        for (auto &row : cloned_level.rows) {
            if (row.row_kind == core::FtRowKind::Generic) {
                row.instance_name = cloned_level.generic_name;
                row.original_instance_name = row.instance_name;
                row.verify_status = L"GENERIC";
                row.action = core::FtRowAction::Keep;
                row.change_kind = core::FtChangeKind::None;
                for (auto &cell : row.cells) {
                    if (cell.column_key == L"INSTANCE_NAME") cell.value = cloned_level.generic_name;
                    else if (cell.column_key == L"VERIFY_STATUS") cell.value = L"GENERIC";
                    cell.old_value = cell.value;
                    cell.changed = false;
                    cell.change_kind = core::FtChangeKind::None;
                }
            } else {
                const std::wstring source_row_name = row.instance_name;
                // Child instance rows live in the cloned level's family table,
                // so they must also receive unique clone names.  Keeping the
                // original child names would collide with the source rows when
                // the cloned child level is materialized/applied.
                const std::wstring renamed_row_name = BuildCloneInstName(source_row_name, used_instance_names);
                renamed_child_rows[source_row_name] = renamed_row_name;
                MarkInstanceRowAsNew(row, renamed_row_name, true, false);
            }
        }

        target_path_by_source_path[source_level.level_path] = cloned_level.level_path;
        cloned_child_name_map[source_level.level_path] = renamed_child_rows;

        workspace.level_nodes.push_back(cloned_level);
        const size_t new_index = workspace.level_nodes.size() - 1;
        if (core::FtLevelNode *parent_level = FindLevel(workspace, workspace.level_nodes[new_index].pending_parent_level_path)) {
            if (std::find(parent_level->children.begin(), parent_level->children.end(), new_index) == parent_level->children.end()) {
                parent_level->children.push_back(new_index);
            }
        }
    }

    return true;
}

} // namespace autobbox::application
