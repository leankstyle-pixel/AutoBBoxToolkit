#include "autobbox/application/ft_replica_view.h"

#include "autobbox/application/ft_diff_engine.h"

#include <algorithm>

namespace autobbox::application {
namespace {

const core::FtLevelNode *ActiveLevel(const core::FtWorkspace &workspace)
{
    for (const auto &level : workspace.level_nodes) {
        if (level.level_path == workspace.active_level_path) return &level;
    }
    if (!workspace.level_nodes.empty()) return &workspace.level_nodes.front();
    return nullptr;
}

bool IsYnOnly(core::FtColumnCategory category)
{
    return category == core::FtColumnCategory::Feature;
}

bool IsYnOrInstance(core::FtColumnCategory category)
{
    return category == core::FtColumnCategory::AssemblyMember ||
           category == core::FtColumnCategory::Udf ||
           category == core::FtColumnCategory::ReferenceModel ||
           category == core::FtColumnCategory::MergePart;
}

FtReplicaCellEditorKind EditorKindForColumn(const core::FtColumn &column)
{
    if (!column.editable || column.support_status != core::FtSupportStatus::Full) {
        return FtReplicaCellEditorKind::ReadOnly;
    }
    if (column.column_key == L"INSTANCE_NAME") return FtReplicaCellEditorKind::Text;
    if (column.column_key == L"IS_LOCKED") return FtReplicaCellEditorKind::BooleanYN;
    if (IsYnOnly(column.column_category)) return FtReplicaCellEditorKind::BooleanYN;
    if (IsYnOrInstance(column.column_category)) return FtReplicaCellEditorKind::YNOrInstance;
    switch (column.value_type) {
    case PRO_PARAM_DOUBLE:
    case PRO_PARAM_INTEGER:
        return FtReplicaCellEditorKind::Number;
    case PRO_PARAM_BOOLEAN:
        return FtReplicaCellEditorKind::BooleanYN;
    default:
        break;
    }
    if (column.column_category == core::FtColumnCategory::Dimension) return FtReplicaCellEditorKind::Number;
    return FtReplicaCellEditorKind::Text;
}

bool NativeVisibleDefault(const core::FtColumn &column)
{
    if (column.column_category != core::FtColumnCategory::Fixed) return true;
    return column.column_key == L"INSTANCE_NAME" || column.column_key == L"COMMON_NAME";
}

} // namespace

std::vector<FtReplicaColumnRule> BuildFtReplicaColumnRules(const core::FtLevelNode &level)
{
    std::vector<FtReplicaColumnRule> rules;
    for (const auto &column : level.columns) {
        FtReplicaColumnRule rule;
        rule.column_key = column.column_key;
        rule.column_category = column.column_category;
        rule.native_editor_kind = EditorKindForColumn(column);
        rule.display_mode = FtReplicaDisplayMode::AbsoluteDimension;
        rule.native_visible_default = NativeVisibleDefault(column);
        rule.supports_comment = column.column_key == L"COMMENT";
        rule.supports_lock = column.column_key == L"IS_LOCKED";
        rule.native_display_name = column.column_display_name.empty() ? column.column_key : column.column_display_name;
        rules.push_back(rule);
    }
    return rules;
}

FtReplicaSession BuildFtReplicaSession(const core::FtWorkspace &workspace)
{
    FtReplicaSession session;
    const core::FtLevelNode *level = ActiveLevel(workspace);
    if (level == nullptr) {
        session.title = L"Family Table Editor";
        session.summary = L"No family table level is active.";
        return session;
    }

    session.active_level_path = level->level_path;
    session.title = L"Family Table Editor";
    session.summary = L"GENERIC=" + level->generic_name +
                      L" | LEVEL=" + level->level_path +
                      L" | ROWS=" + std::to_wstring(level->rows.size()) +
                      L" | COLS=" + std::to_wstring(level->columns.size());
    if (workspace.dirty) session.summary += L" | *modified";
    session.column_rules = BuildFtReplicaColumnRules(*level);
    session.menu_state.has_family_table = level->has_family_table;
    session.menu_state.can_add_column = level->has_family_table;
    session.menu_state.can_delete_column = level->has_family_table;
    session.menu_state.can_add_row = level->has_family_table;
    session.menu_state.can_delete_row = level->has_family_table;
    session.menu_state.can_lock = level->has_family_table;
    session.menu_state.can_unlock = level->has_family_table;
    session.menu_state.can_open_instance = level->has_family_table;
    session.menu_state.can_preview_instance = level->has_family_table;
    session.menu_state.can_apply = level->has_family_table;
    return session;
}

void ApplyFtReplicaColumnRules(core::FtLevelNode &level, const FtReplicaSession &session)
{
    for (auto &column : level.columns) {
        auto it = std::find_if(session.column_rules.begin(), session.column_rules.end(),
                               [&](const FtReplicaColumnRule &rule) { return rule.column_key == column.column_key; });
        if (it == session.column_rules.end()) continue;
        column.visible = it->native_visible_default;
        column.column_display_name = it->native_display_name;
    }
}

void ApplyFtReplicaEditsToWorkspace(core::FtWorkspace &workspace, const FtReplicaSession &)
{
    RefreshFtWorkspaceDiff(workspace);
}

} // namespace autobbox::application
