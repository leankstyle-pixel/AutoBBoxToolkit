#pragma once

#include <ProFamtable.h>
#include <ProMdl.h>
#include <ProParamval.h>
#include <ProToolkit.h>

#include <string>
#include <vector>

namespace autobbox::core {

enum class FtSupportStatus {
    Full,
    ReadOnly,
    BridgeNative,
    Todo,
};

enum class FtColumnCategory {
    Fixed,
    Dimension,
    Parameter,
    SystemParameter,
    Feature,
    Udf,
    ReferenceModel,
    PatternTable,
    AssemblyMember,
    MergePart,
    Unknown,
};

enum class FtRowKind {
    Generic,
    Instance,
};

enum class FtRowAction {
    Keep,
    Modify,
    New,
    Delete,
};

enum class FtChangeKind {
    None,
    New,
    Modify,
    Delete,
    Moved,
};

struct FtCell {
    std::wstring column_key;
    std::wstring value;
    std::wstring old_value;
    bool changed = false;
    bool clone_seeded = false;
    bool editable = false;
    FtSupportStatus support_status = FtSupportStatus::Todo;
    FtChangeKind change_kind = FtChangeKind::None;
    ProParamvalueType value_type = PRO_PARAM_NOT_SET;
    std::wstring clone_seed_value;
};

struct FtColumn {
    std::wstring column_key;
    std::wstring column_display_name;
    FtColumnCategory column_category = FtColumnCategory::Unknown;
    std::wstring value_type_name;
    ProParamvalueType value_type = PRO_PARAM_NOT_SET;
    FtSupportStatus support_status = FtSupportStatus::Todo;
    bool editable = false;
    bool visible = true;
    bool required = false;
    int order_index = 0;
    FtChangeKind change_kind = FtChangeKind::None;

    bool has_creo_item = false;
    ProFamtabType famtab_type = PRO_FAM_TYPE_UNUSED;
    std::wstring famtab_string;
    ProMdl creo_item_owner = nullptr;
};

struct FtRow {
    FtRowKind row_kind = FtRowKind::Instance;
    std::wstring instance_name;
    std::wstring original_instance_name;
    std::wstring common_name;
    std::wstring verify_status;
    bool is_locked = false;
    bool is_ext_locked = false;
    bool enhanced_clone = false;
    bool suppress_child_placeholder = false;
    FtRowAction action = FtRowAction::Keep;
    FtChangeKind change_kind = FtChangeKind::None;
    std::vector<FtCell> cells;
};

struct FtLevelNode {
    std::wstring level_path;
    int level_depth = 0;
    std::wstring generic_name;
    std::wstring parent_generic_name;
    std::wstring parent_instance_name;
    std::wstring pending_parent_level_path;
    std::wstring clone_source_level_path;
    ProMdlType model_type = PRO_MDL_UNUSED;
    ProMdl generic_mdl = nullptr;
    ProFamtable famtable = {};
    bool has_family_table = false;
    bool famtable_modifiable = false;
    bool pending_resolve = false;
    bool enhanced_clone = false;
    std::vector<FtRow> rows;
    std::vector<FtColumn> columns;
    std::vector<size_t> children;
};

struct FtCellDiff {
    std::wstring level_path;
    std::wstring instance_name;
    std::wstring column_key;
    std::wstring old_value;
    std::wstring new_value;
    FtChangeKind kind = FtChangeKind::None;
};

struct FtColumnDiff {
    std::wstring level_path;
    std::wstring column_key;
    FtChangeKind kind = FtChangeKind::None;
};

struct FtRowDiff {
    std::wstring level_path;
    std::wstring instance_name;
    FtChangeKind kind = FtChangeKind::None;
};

struct FtDiff {
    std::vector<FtRowDiff> added_rows;
    std::vector<FtRowDiff> removed_rows;
    std::vector<FtCellDiff> modified_cells;
    std::vector<FtColumnDiff> moved_columns;
    std::vector<FtColumnDiff> added_columns;
    std::vector<FtColumnDiff> removed_columns;
};

struct FtLogEntry {
    std::wstring level_path;
    std::wstring severity;
    std::wstring operation;
    std::wstring message;
    ProError status = PRO_TK_NO_ERROR;
};

struct FtSupportMatrixEntry {
    std::wstring feature_key;
    std::wstring display_name;
    FtSupportStatus status = FtSupportStatus::Todo;
    std::wstring remark;
};

struct FtWorkspace {
    std::vector<FtLevelNode> level_nodes;
    std::vector<FtLevelNode> original_snapshot;
    // Instance-level paths that were checked and confirmed to have no child
    // family table.  This prevents the manager from recreating fake hierarchy
    // placeholders after "load all instance levels" or after deleting a level.
    std::vector<std::wstring> known_leaf_level_paths;
    std::wstring active_level_path;
    FtDiff diff_result;
    std::vector<FtSupportMatrixEntry> support_matrix;
    std::vector<FtLogEntry> logs;
    bool dirty = false;
};

const wchar_t *FtSupportStatusName(FtSupportStatus status);
const wchar_t *FtColumnCategoryName(FtColumnCategory category);
const wchar_t *FtRowActionName(FtRowAction action);
const wchar_t *FtChangeKindName(FtChangeKind kind);

} // namespace autobbox::core
