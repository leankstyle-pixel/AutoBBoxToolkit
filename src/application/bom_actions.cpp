#include "autobbox/application/bom_actions.h"

#include "autobbox/application/bom_export.h"
#include "autobbox/application/bom_state.h"

namespace autobbox::application {

namespace {

bool SplitBomDraftKey(const std::wstring &key, std::wstring &row_key, std::wstring &param_name)
{
    const size_t sep = key.find(L'\x1f');
    if (sep == std::wstring::npos) {
        return false;
    }
    row_key = key.substr(0, sep);
    param_name = key.substr(sep + 1);
    return !row_key.empty() && !param_name.empty();
}

bool HasCheckedRowDrafts(const core::BomToolState &state)
{
    if (state.draft_values.empty() || state.checked_update_row_keys.empty()) {
        return false;
    }

    for (const auto &entry : state.draft_values) {
        std::wstring row_key;
        std::wstring param_name;
        if (SplitBomDraftKey(entry.first, row_key, param_name) &&
            state.checked_update_row_keys.find(row_key) != state.checked_update_row_keys.end()) {
            return true;
        }
    }
    return false;
}

} // namespace

bool HandleBomAddColumnsAction(core::BomToolState &state,
                               const BomStateRefresher &refresh_state,
                               BomAddColumnsActionResult &result_out,
                               std::wstring &error_out)
{
    result_out = {};
    error_out.clear();

    result_out.has_inline_create = BuildBomInlineCreateSpec(state, result_out.inline_spec, error_out);
    if (!error_out.empty()) {
        return false;
    }

    if (result_out.has_inline_create) {
        result_out.created_names = AddCustomBomAvailableParams(
            state,
            std::vector<core::ParamAddSpec>{ result_out.inline_spec });
        state.pending_create_name.clear();
        state.pending_display_name.clear();
        state.pending_default_value.clear();
        state.pending_option_value.clear();
        state.pending_create_type = PRO_PARAM_STRING;
        if (refresh_state) {
            refresh_state(state);
        }
    }

    return true;
}

bool HandleBomCreateParamsAction(core::BomToolState &state,
                                 const core::ParamAddSpec &spec,
                                 const BomStateRefresher &refresh_state,
                                 BomCreateParamsActionResult &result_out,
                                 std::wstring &error_out)
{
    result_out = {};
    error_out.clear();

    if (!ValidateBomCustomParamSpec(state, spec, error_out)) {
        return false;
    }

    result_out.added_names = AddCustomBomColumns(
        state,
        std::vector<core::ParamAddSpec>{ spec });
    if (refresh_state) {
        refresh_state(state);
    }

    return true;
}

bool HandleBomUpdateParamAction(core::BomToolState &state,
                                const std::wstring &old_param_name,
                                const BomStateRefresher &refresh_state,
                                BomUpdateParamActionResult &result_out,
                                std::wstring &error_out)
{
    result_out = {};
    error_out.clear();

    core::ParamAddSpec spec;
    if (!BuildBomInlineCreateSpec(state, spec, error_out)) {
        if (error_out.empty()) {
            error_out = L"请输入参数名称。";
        }
        return false;
    }

    if (!UpdateCustomBomAvailableParam(state, old_param_name, spec, error_out)) {
        return false;
    }

    result_out.old_name = old_param_name;
    result_out.new_name = spec.name;
    if (refresh_state) {
        refresh_state(state);
    }
    return true;
}

std::vector<std::wstring> HandleBomRemoveColumnsAction(core::BomToolState &state)
{
    return ClearCheckedBomAvailableParams(state);
}

BomUpdateModelActionResult HandleBomUpdateModelAction(core::BomToolState &state,
                                                      const BomModelTagFormatter &format_model_tag,
                                                      const BomLogSink &log_sink,
                                                      const BomStateRefresher &refresh_state)
{
    BomUpdateModelActionResult result;
    result.had_drafts = HasCheckedRowDrafts(state);
    if (!result.had_drafts) {
        return result;
    }

    result.summary = ApplyBomDraftsToModels(state, format_model_tag, log_sink);
    if (refresh_state) {
        refresh_state(state);
    }
    return result;
}

BomExportCsvActionResult HandleBomExportCsvAction(const core::BomToolState &state)
{
    BomExportCsvActionResult result;
    result.exported = ExportBomExcel(state, result.export_path);
    if (!result.exported) {
        result.export_path.clear();
    }
    return result;
}

} // namespace autobbox::application
