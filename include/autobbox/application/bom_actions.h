#pragma once

#include "autobbox/application/bom_update.h"
#include "autobbox/core/bom_types.h"

#include <functional>
#include <string>
#include <vector>

namespace autobbox::application {

using BomStateRefresher = std::function<void(core::BomToolState &state)>;

struct BomAddColumnsActionResult {
    bool has_inline_create = false;
    core::ParamAddSpec inline_spec;
    std::vector<std::wstring> created_names;
    std::vector<std::wstring> added_names;
};

struct BomCreateParamsActionResult {
    std::vector<std::wstring> added_names;
};

struct BomUpdateParamActionResult {
    std::wstring old_name;
    std::wstring new_name;
};

struct BomUpdateModelActionResult {
    bool had_drafts = false;
    core::BomUpdateSummary summary;
};

struct BomExportCsvActionResult {
    bool exported = false;
    std::wstring export_path;
};

bool HandleBomAddColumnsAction(core::BomToolState &state,
                               const BomStateRefresher &refresh_state,
                               BomAddColumnsActionResult &result_out,
                               std::wstring &error_out);
bool HandleBomCreateParamsAction(core::BomToolState &state,
                                 const core::ParamAddSpec &spec,
                                 const BomStateRefresher &refresh_state,
                                 BomCreateParamsActionResult &result_out,
                                 std::wstring &error_out);
bool HandleBomUpdateParamAction(core::BomToolState &state,
                                const std::wstring &old_param_name,
                                const BomStateRefresher &refresh_state,
                                BomUpdateParamActionResult &result_out,
                                std::wstring &error_out);
std::vector<std::wstring> HandleBomRemoveColumnsAction(core::BomToolState &state);
BomUpdateModelActionResult HandleBomUpdateModelAction(core::BomToolState &state,
                                                      const BomModelTagFormatter &format_model_tag,
                                                      const BomLogSink &log_sink,
                                                      const BomStateRefresher &refresh_state);
BomExportCsvActionResult HandleBomExportCsvAction(const core::BomToolState &state);

} // namespace autobbox::application
