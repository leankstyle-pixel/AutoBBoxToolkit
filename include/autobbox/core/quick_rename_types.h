#pragma once

#include <ProMdl.h>
#include <ProObjects.h>

#include <string>

namespace autobbox::core {

struct QuickRenameTarget {
    ProMdl mdl = nullptr;
    ProMdlType type = PRO_MDL_UNUSED;
    std::wstring old_name;
    bool has_component_path = false;
    ProAsmcomppath component_path = {};
    ProAssembly parent_assembly = nullptr;
    int component_id = 0;
};

struct QuickRenameValidationResult {
    bool ok = false;
    bool unchanged = false;
    bool existing_name_conflict = false;
    ProMdl existing_mdl = nullptr;
    std::wstring normalized_name;
    std::wstring error_text;
};

} // namespace autobbox::core
