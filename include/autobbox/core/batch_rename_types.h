#pragma once

#include <ProMdl.h>
#include <ProToolkit.h>

#include <string>

namespace autobbox::core {

struct BatchRenameOptions {
    ProBoolean parts = PRO_B_TRUE;
    ProBoolean assemblies = PRO_B_TRUE;
    ProBoolean top_level_only = PRO_B_FALSE;
};

enum class BatchRenameEditableColumn {
    NewModelName = 0,
    CommonName = 1,
};

enum class BatchRenameReplaceMode {
    PlainText = 0,
    Template = 1,
};

struct BatchRenameCandidate {
    ProMdl mdl = nullptr;
    ProMdlType type = PRO_MDL_UNUSED;
    std::string row_name;
    bool selected = false;
    std::wstring model_name;
    std::wstring new_model_name;
    std::wstring normalized_new_model_name;
    std::wstring common_name;
    std::wstring new_common_name;
    std::wstring status_text;
    bool has_error = false;
};

struct BatchRenameValidationIssue {
    size_t row_index = 0;
    std::wstring message;
};

struct BatchRenameApplySummary {
    int changed_rows = 0;
    int renamed = 0;
    int common_updated = 0;
    int skipped = 0;
    int failed = 0;
    std::wstring summary_text;
};

struct BatchRenameClearSpec {
    bool clear_new_model_name = false;
    bool clear_common_name = false;
};

struct BatchRenameReplaceSpec {
    BatchRenameEditableColumn target_column = BatchRenameEditableColumn::NewModelName;
    BatchRenameReplaceMode mode = BatchRenameReplaceMode::PlainText;
    std::wstring find_text;
    std::wstring replace_text;
    bool case_sensitive = false;
};

struct BatchRenameSequenceSpec {
    BatchRenameEditableColumn target_column = BatchRenameEditableColumn::NewModelName;
    std::wstring template_text = L"{name}_{num}";
    int start = 1;
    int step = 1;
    int width = 3;
};

struct BatchRenameTransformSummary {
    int changed_rows = 0;
    int touched_rows = 0;
    std::wstring summary_text;
};

} // namespace autobbox::core
