#pragma once

#include "autobbox/core/family_table_types.h"

#include <ProToolkit.h>

#include <string>
#include <vector>

namespace autobbox::application {

enum class FtReplicaCellEditorKind {
    ReadOnly,
    Number,
    Text,
    BooleanYN,
    YNOrInstance,
};

enum class FtReplicaDisplayMode {
    AbsoluteDimension,
    RelativeDimension,
};

struct FtReplicaColumnRule {
    std::wstring column_key;
    core::FtColumnCategory column_category = core::FtColumnCategory::Unknown;
    FtReplicaCellEditorKind native_editor_kind = FtReplicaCellEditorKind::ReadOnly;
    FtReplicaDisplayMode display_mode = FtReplicaDisplayMode::AbsoluteDimension;
    bool native_visible_default = true;
    bool supports_comment = false;
    bool supports_lock = false;
    std::wstring native_display_name;
};

struct FtReplicaMenuState {
    bool has_family_table = false;
    bool can_add_column = false;
    bool can_delete_column = false;
    bool can_add_row = false;
    bool can_delete_row = false;
    bool can_lock = false;
    bool can_unlock = false;
    bool can_open_instance = false;
    bool can_preview_instance = false;
    bool can_apply = false;
    bool can_show_enhanced_mode = true;
};

struct FtReplicaSession {
    std::wstring active_level_path;
    std::wstring title;
    std::wstring summary;
    std::vector<FtReplicaColumnRule> column_rules;
    FtReplicaMenuState menu_state;
};

std::vector<FtReplicaColumnRule> BuildFtReplicaColumnRules(const core::FtLevelNode &level);
FtReplicaSession BuildFtReplicaSession(const core::FtWorkspace &workspace);
void ApplyFtReplicaColumnRules(core::FtLevelNode &level, const FtReplicaSession &session);
void ApplyFtReplicaEditsToWorkspace(core::FtWorkspace &workspace, const FtReplicaSession &session);

} // namespace autobbox::application
