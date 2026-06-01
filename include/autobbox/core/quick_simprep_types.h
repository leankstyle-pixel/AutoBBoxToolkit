#pragma once

#include <ProAsmcomppath.h>
#include <ProMdl.h>
#include <ProSimprep.h>
#include <ProToolkit.h>

#include <string>
#include <vector>

namespace autobbox::core {

struct QuickSimprepOccurrence {
    ProAsmcomppath path = {};
    int feat_id = -1;
    ProMdl mdl = nullptr;
    ProMdlType type = PRO_MDL_UNUSED;
    std::wstring model_name;
};

struct QuickSimprepCategory {
    std::string item_name;
    bool selected = true;
    std::wstring common_name;
    std::vector<QuickSimprepOccurrence> occurrences;
    std::wstring status_text;
    bool has_error = false;
};

struct QuickSimprepCollectResult {
    std::vector<QuickSimprepCategory> categories;
    int direct_component_count = 0;
    int grouped_component_count = 0;
    int skipped_missing_common_name = 0;
    int skipped_unreadable_common_name = 0;
};

enum class QuickSimprepCreateMode {
    PerCategory = 0,
    Merged = 1,
};

struct QuickSimprepCreatedRep {
    std::wstring source_label;
    std::wstring rep_name;
    int included_count = 0;
    ProError status = PRO_TK_GENERAL_ERROR;
    bool updated = false;
};

struct QuickSimprepCreateSummary {
    int requested = 0;
    int created = 0;
    int updated = 0;
    int failed = 0;
    int included_count = 0;
    std::vector<QuickSimprepCreatedRep> reps;
    std::wstring summary_text;
};

struct QuickSimprepExistingRep {
    std::wstring rep_name;
    ProSimprep handle = {};
    ProSimprepType type = PRO_SIMPREP_MASTER_REP;
    int item_count = 0;
    bool is_active = false;
    std::vector<QuickSimprepCategory> categories;
};

struct QuickSimprepExistingRepsResult {
    std::vector<QuickSimprepExistingRep> reps;
    int total_count = 0;
};

enum class QuickSimprepManageAction {
    DeleteCategory = 0,
    UpdateCategory = 1,
    Rename = 2,
    AddCategories = 3,
};

struct QuickSimprepManageResult {
    std::wstring rep_name;
    std::wstring category_name;
    QuickSimprepManageAction action;
    ProError status = PRO_TK_GENERAL_ERROR;
    std::wstring message;
};

struct QuickSimprepManageSummary {
    int total = 0;
    int succeeded = 0;
    int failed = 0;
    std::vector<QuickSimprepManageResult> results;
    std::wstring summary_text;
};

enum class QuickSimprepDialogMode {
    Create = 0,
    Manage = 1,
};

} // namespace autobbox::core
