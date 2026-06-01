#pragma once

#include <ProAsmcomppath.h>
#include <ProMdl.h>
#include <ProSolid.h>

#include <string>

namespace autobbox::core {

struct SplitCandidate {
    ProMdl mdl = nullptr;
    ProMdlType type = PRO_MDL_UNUSED;
    ProSolid owner = nullptr;
    ProAsmcomppath comp_path = {};
    bool has_owner = false;
    int feat_id = -1;
    std::wstring model_name;
    std::wstring generic_name;
    std::wstring label;
    std::string item_name;
};

struct SplitRunOptions {
    bool replace_in_assembly = true;
    bool output_to_split_dir = true;
    bool reuse_existing_split = true;
};

} // namespace autobbox::core
