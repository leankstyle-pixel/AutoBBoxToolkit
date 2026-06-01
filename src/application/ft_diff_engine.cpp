#include "autobbox/application/ft_diff_engine.h"

#include <algorithm>

namespace autobbox::application {
namespace {

const core::FtLevelNode *FindLevel(const std::vector<core::FtLevelNode> &levels, const std::wstring &path)
{
    for (const auto &level : levels) if (level.level_path == path) return &level;
    return nullptr;
}

core::FtLevelNode *FindLevel(std::vector<core::FtLevelNode> &levels, const std::wstring &path)
{
    for (auto &level : levels) if (level.level_path == path) return &level;
    return nullptr;
}

const core::FtRow *FindRow(const core::FtLevelNode &level, const std::wstring &name)
{
    for (const auto &row : level.rows) if (row.instance_name == name || row.original_instance_name == name) return &row;
    return nullptr;
}

core::FtRow *FindRow(core::FtLevelNode &level, const std::wstring &name)
{
    for (auto &row : level.rows) if (row.instance_name == name || row.original_instance_name == name) return &row;
    return nullptr;
}

const core::FtColumn *FindColumn(const core::FtLevelNode &level, const std::wstring &key)
{
    for (const auto &col : level.columns) if (col.column_key == key) return &col;
    return nullptr;
}

core::FtColumn *FindColumn(core::FtLevelNode &level, const std::wstring &key)
{
    for (auto &col : level.columns) if (col.column_key == key) return &col;
    return nullptr;
}

const core::FtCell *FindCell(const core::FtRow &row, const std::wstring &key)
{
    for (const auto &cell : row.cells) if (cell.column_key == key) return &cell;
    return nullptr;
}

core::FtCell *FindCell(core::FtRow &row, const std::wstring &key)
{
    for (auto &cell : row.cells) if (cell.column_key == key) return &cell;
    return nullptr;
}

} // namespace

core::FtDiff BuildFtDiff(const std::vector<core::FtLevelNode> &original, std::vector<core::FtLevelNode> &edited)
{
    core::FtDiff diff;
    for (core::FtLevelNode &level : edited) {
        const core::FtLevelNode *orig_level = FindLevel(original, level.level_path);
        if (orig_level == nullptr) continue;

        for (core::FtColumn &col : level.columns) {
            const core::FtColumn *orig_col = FindColumn(*orig_level, col.column_key);
            col.change_kind = core::FtChangeKind::None;
            if (orig_col == nullptr) {
                col.change_kind = core::FtChangeKind::New;
                diff.added_columns.push_back({level.level_path, col.column_key, core::FtChangeKind::New});
            } else if (orig_col->order_index != col.order_index) {
                col.change_kind = core::FtChangeKind::Moved;
                diff.moved_columns.push_back({level.level_path, col.column_key, core::FtChangeKind::Moved});
            }
        }
        for (const core::FtColumn &orig_col : orig_level->columns) {
            if (FindColumn(level, orig_col.column_key) == nullptr) {
                diff.removed_columns.push_back({level.level_path, orig_col.column_key, core::FtChangeKind::Delete});
            }
        }

        for (core::FtRow &row : level.rows) {
            row.change_kind = core::FtChangeKind::None;
            const core::FtRow *orig_row = FindRow(*orig_level, row.original_instance_name.empty() ? row.instance_name : row.original_instance_name);
            if (row.action == core::FtRowAction::New || orig_row == nullptr) {
                row.change_kind = core::FtChangeKind::New;
                diff.added_rows.push_back({level.level_path, row.instance_name, core::FtChangeKind::New});
                continue;
            }
            if (row.action == core::FtRowAction::Delete) {
                row.change_kind = core::FtChangeKind::Delete;
                diff.removed_rows.push_back({level.level_path, row.instance_name, core::FtChangeKind::Delete});
                continue;
            }
            if (row.instance_name != row.original_instance_name && !row.original_instance_name.empty()) {
                row.change_kind = core::FtChangeKind::Modify;
            }
            for (core::FtCell &cell : row.cells) {
                cell.change_kind = core::FtChangeKind::None;
                const core::FtCell *orig_cell = FindCell(*orig_row, cell.column_key);
                if (orig_cell != nullptr && orig_cell->value != cell.value) {
                    cell.changed = true;
                    cell.old_value = orig_cell->value;
                    cell.change_kind = core::FtChangeKind::Modify;
                    row.change_kind = core::FtChangeKind::Modify;
                    diff.modified_cells.push_back({level.level_path, row.instance_name, cell.column_key, orig_cell->value, cell.value, core::FtChangeKind::Modify});
                }
            }
            if (row.change_kind == core::FtChangeKind::Modify && row.action == core::FtRowAction::Keep) {
                row.action = core::FtRowAction::Modify;
            }
        }
        for (const core::FtRow &orig_row : orig_level->rows) {
            if (orig_row.row_kind == core::FtRowKind::Generic) continue;
            core::FtRow *row = FindRow(level, orig_row.instance_name);
            if (row == nullptr) {
                diff.removed_rows.push_back({level.level_path, orig_row.instance_name, core::FtChangeKind::Delete});
            }
        }
    }
    return diff;
}

void RefreshFtWorkspaceDiff(core::FtWorkspace &workspace)
{
    workspace.diff_result = BuildFtDiff(workspace.original_snapshot, workspace.level_nodes);
    workspace.dirty = !workspace.diff_result.added_rows.empty() ||
                      !workspace.diff_result.removed_rows.empty() ||
                      !workspace.diff_result.modified_cells.empty() ||
                      !workspace.diff_result.moved_columns.empty() ||
                      !workspace.diff_result.added_columns.empty() ||
                      !workspace.diff_result.removed_columns.empty();
}

} // namespace autobbox::application
