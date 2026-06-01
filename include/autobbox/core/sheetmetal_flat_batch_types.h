#pragma once

#include <ProAsmcomppath.h>
#include <ProMdl.h>
#include <ProToolkit.h>

#include <string>
#include <vector>

namespace autobbox::core {

struct SheetmetalFlatOccurrence {
    ProAsmcomppath path = {};
    int depth = 0;
    std::wstring path_label;
};

struct SheetmetalFlatTarget {
    std::string item_name;
    bool selected = true;
    ProMdl mdl = nullptr;
    ProMdlType type = PRO_MDL_UNUSED;
    std::wstring model_name;
    std::wstring display_path;
    bool current_model = false;
    bool from_flat_state_instance = false;
    ProMdl flat_generic_mdl = nullptr;
    std::vector<SheetmetalFlatOccurrence> occurrences;

    bool simprep_applicable = false;
    bool has_tool_simprep = false;
    std::wstring tool_simprep_name;
    std::vector<std::wstring> tool_simprep_names;

    bool has_family_table = false;
    bool family_table_modifiable = false;
    bool family_flat_create_supported = false;
    std::vector<std::wstring> family_flat_instances;

    std::wstring status_text;
    bool has_error = false;
};

struct SheetmetalFlatCollectResult {
    std::vector<SheetmetalFlatTarget> targets;
    int visited_components = 0;
    int duplicate_components = 0;
    int skipped_non_sheetmetal = 0;
    int skipped_unreadable = 0;
    bool current_is_assembly = false;
    bool current_is_part = false;
};

enum class SheetmetalFlatAction {
    CreateModelFlatRep,
    CreateFamilyFlat,
    DeleteSelected,
    CreateSimprep = CreateModelFlatRep,
};

struct SheetmetalFlatActionResult {
    SheetmetalFlatAction action = SheetmetalFlatAction::CreateModelFlatRep;
    int requested = 0;
    int succeeded = 0;
    int failed = 0;
    int skipped = 0;
    std::wstring summary_text;
};

} // namespace autobbox::core
