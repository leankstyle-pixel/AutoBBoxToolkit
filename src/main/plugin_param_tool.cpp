#include "autobbox/main/plugin_param_tool.h"

#include "autobbox/application/bom_actions.h"
#include "autobbox/application/bom_export.h"
#include "autobbox/application/bom_state.h"
#include "autobbox/application/bom_update.h"
#include "autobbox/application/target_collectors.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/model_info.h"
#include "autobbox/main/plugin_runtime_bridge.h"
#include "autobbox/ui/bom_dialog.h"
#include "autobbox/ui/message_dialog.h"
#include "autobbox/ui/param_add_dialog.h"

#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>

namespace autobbox::main {

namespace {

using autobbox::ui::BomToolDialogAction;

void LogLine(const char *fmt, ...)
{
    if (fmt == nullptr) {
        return;
    }

    char buffer[4096] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    LogPluginReportLine(buffer);
}

std::string ModelTag(ProMdl mdl, const PluginParamToolRuntime &runtime)
{
    if (runtime.format_model_tag != nullptr) {
        return runtime.format_model_tag(mdl);
    }
    return autobbox::creo::DefaultModelTag(mdl);
}

struct TaskGuard {
    explicit TaskGuard(bool *task_running)
        : task_running(task_running)
    {
    }

    ~TaskGuard()
    {
        EndPluginReportSession();
        if (task_running != nullptr) {
            *task_running = false;
        }
    }

    bool *task_running = nullptr;
};

void RefreshBomState(autobbox::core::BomToolState &state)
{
    autobbox::application::RefreshBomState(
        state,
        [](const autobbox::core::BomToolState &tool_state) {
            return autobbox::application::CollectBomTargets(tool_state);
        },
        autobbox::application::BuildBomAvailableLabel);
}

std::wstring JoinOrderedNames(const std::vector<std::wstring> &names)
{
    std::wstring out;
    for (size_t i = 0; i < names.size(); ++i) {
        if (i > 0) {
            out += L",";
        }
        out += names[i];
    }
    return out;
}

bool PromptParamAddSpec(autobbox::core::ParamAddSpec &spec_io, bool &cancelled)
{
    std::wstring error_text;
    autobbox::ui::ParamAddDialogCallbacks callbacks = {};
    callbacks.param_add_type_menu_label = autobbox::application::ParamAddTypeMenuLabel;
    callbacks.parse_param_add_dialog_spec = autobbox::application::ParseParamAddDialogSpec;

    const bool ok = autobbox::ui::PromptParamAddDialog(spec_io, cancelled, error_text, callbacks);
    if (!ok && !cancelled && !error_text.empty()) {
        autobbox::ui::ShowSimpleMessageDialog(PROUIMESSAGE_ERROR, L"BOM清单", error_text.c_str());
    } else if (!ok && cancelled && error_text == L"新建参数窗口加载失败。") {
        LogLine("ERROR paramadd-dialog-create failed");
        autobbox::ui::ShowSimpleMessageDialog(PROUIMESSAGE_ERROR, L"BOM清单", error_text.c_str());
    }
    return ok;
}

ProBoolean ValueOrDefault(const ProBoolean *value, ProBoolean fallback)
{
    return value != nullptr ? *value : fallback;
}

void ApplyRuntimeBomFilters(autobbox::core::BomToolState &state, const PluginParamToolRuntime &runtime)
{
    (void)runtime;
    state.top_level_only = PRO_B_FALSE;
    if (state.max_bom_level < 1) {
        state.max_bom_level = 3;
    }
    if (state.parts_option != PRO_B_TRUE && state.assemblies_option != PRO_B_TRUE) {
        state.parts_option = PRO_B_TRUE;
        state.assemblies_option = PRO_B_TRUE;
    }
}

} // namespace

int RunPluginParamToolTask(const PluginParamToolRuntime &runtime)
{
    if (runtime.task_running != nullptr && *runtime.task_running) {
        LogLine("SKIP run mode=bom-tool reason=already-running");
        return 0;
    }
    if (runtime.task_running != nullptr) {
        *runtime.task_running = true;
    }
    TaskGuard guard(runtime.task_running);

    BeginPluginReportSession();
    LogLine("===== Run begin mode=bom-tool =====");

    autobbox::core::BomToolState local_state;
    autobbox::core::BomToolState &state =
        runtime.persisted_state != nullptr ? *runtime.persisted_state : local_state;
    ApplyRuntimeBomFilters(state, runtime);
    RefreshBomState(state);
    const autobbox::core::BomRenderStats initial_stats = autobbox::application::BuildBomRenderStats(state);
    LogLine("bom rows=%d cols=%d available=%d max_level=%d parts_flag=%d asms_flag=%d",
            initial_stats.row_count,
            initial_stats.column_count,
            static_cast<int>(state.available_params.size()),
            state.max_bom_level,
            static_cast<int>(state.parts_option),
            static_cast<int>(state.assemblies_option));

    const bool has_bom_filter =
        !state.filter_model_name.empty() ||
        !state.filter_param_name.empty() ||
        !state.filter_param_value.empty();
    if (state.rows.empty() && !has_bom_filter) {
        LogLine("BOM tool skipped: no targets after option filter");
        OpenPluginReportLog();
        return 0;
    }

    while (true) {
        const auto refresh_bom_state = [](autobbox::core::BomToolState &state) { RefreshBomState(state); };
        const autobbox::core::BomRenderStats stats = autobbox::application::BuildBomRenderStats(state);
        LogLine("Summary mode=bom-view rows=%d cols=%d available=%d added_cols=%d removed_cols=%d writable_cells=%d readonly_cells=%d drafts=%d",
                stats.row_count,
                stats.column_count,
                static_cast<int>(state.available_params.size()),
                state.last_added_columns,
                state.last_removed_columns,
                stats.writable_cells,
                stats.readonly_cells,
                static_cast<int>(state.draft_values.size()));
        state.last_added_columns = 0;
        state.last_removed_columns = 0;

        BomToolDialogAction action = BomToolDialogAction::Close;
        autobbox::ui::BomDialogCallbacks dialog_callbacks = {};
        dialog_callbacks.build_summary_text = autobbox::application::BuildBomSummaryText;
        dialog_callbacks.param_type_from_menu_label = autobbox::application::ParamAddTypeFromMenuLabel;
        dialog_callbacks.bool_menu_value_to_short = autobbox::application::BoolMenuValueToShort;
        dialog_callbacks.param_add_type_menu_label = autobbox::application::ParamAddTypeMenuLabel;
        dialog_callbacks.find_available_param = autobbox::application::FindBomAvailableParam;
        dialog_callbacks.build_cell_view = autobbox::application::BuildBomCellView;
        dialog_callbacks.handle_add_action = [&refresh_bom_state](autobbox::core::BomToolState &state,
                                                                  std::wstring &error_out,
                                                                  std::wstring &warning_out) {
            warning_out.clear();
            autobbox::application::BomAddColumnsActionResult result;
            if (!autobbox::application::HandleBomAddColumnsAction(
                    state,
                    refresh_bom_state,
                    result,
                    error_out)) {
                return false;
            }
            LogLine("BOM available-list add count=%d names=%s inline_create=%d create_name=%s",
                    static_cast<int>(result.created_names.size()),
                    autobbox::common::WToA(JoinOrderedNames(result.created_names).c_str()).c_str(),
                    result.has_inline_create ? 1 : 0,
                    result.has_inline_create ? autobbox::common::WToA(result.inline_spec.name.c_str()).c_str() : "");
            if (!result.has_inline_create) {
                warning_out = L"请输入参数名称后再点“添加”；如需在 BOM 表格显示参数列，请勾选左侧参数后点“刷新”。";
            }
            return true;
        };
        dialog_callbacks.handle_move_left_action = [](autobbox::core::BomToolState &state,
                                                      std::wstring &error_out) {
            const bool ok = autobbox::application::MoveSelectedBomColumnsLeft(state, error_out);
            if (ok) {
                LogLine("BOM move columns direction=left");
            }
            return ok;
        };
        dialog_callbacks.handle_move_right_action = [](autobbox::core::BomToolState &state,
                                                       std::wstring &error_out) {
            const bool ok = autobbox::application::MoveSelectedBomColumnsRight(state, error_out);
            if (ok) {
                LogLine("BOM move columns direction=right");
            }
            return ok;
        };
        dialog_callbacks.handle_refresh_action = [](autobbox::core::BomToolState &state) {
            const std::vector<std::wstring> removed_names =
                autobbox::application::SyncVisibleBomColumnsFromChecked(state);
            LogLine("BOM refresh sync-checked added=%d removed=%d removed_names=%s",
                    state.last_added_columns,
                    static_cast<int>(removed_names.size()),
                    autobbox::common::WToA(JoinOrderedNames(removed_names).c_str()).c_str());
            RefreshBomState(state);
        };
        dialog_callbacks.handle_rebuild_action = [](autobbox::core::BomToolState &state) {
            LogLine("BOM rebuild requested by filter change");
            RefreshBomState(state);
        };
        dialog_callbacks.handle_delete_param_action = [&refresh_bom_state](autobbox::core::BomToolState &state,
                                                                           const std::wstring &param_name,
                                                                           std::wstring &error_out) {
            if (!autobbox::application::RemoveCustomBomAvailableParam(state, param_name, error_out)) {
                return false;
            }
            LogLine("BOM delete custom param name=%s",
                    autobbox::common::WToA(param_name.c_str()).c_str());
            refresh_bom_state(state);
            return true;
        };
        dialog_callbacks.handle_update_param_action = [&refresh_bom_state](autobbox::core::BomToolState &state,
                                                                           const std::wstring &param_name,
                                                                           std::wstring &error_out) {
            autobbox::application::BomUpdateParamActionResult result;
            if (!autobbox::application::HandleBomUpdateParamAction(
                    state,
                    param_name,
                    refresh_bom_state,
                    result,
                    error_out)) {
                return false;
            }
            LogLine("BOM update custom param old=%s new=%s",
                    autobbox::common::WToA(result.old_name.c_str()).c_str(),
                    autobbox::common::WToA(result.new_name.c_str()).c_str());
            return true;
        };
        if (!autobbox::ui::PromptBomToolDialog(state, action, dialog_callbacks)) {
            LogLine("BOM tool closed");
            break;
        }

        if (action == BomToolDialogAction::UpdateModel) {
            const autobbox::application::BomUpdateModelActionResult result =
                autobbox::application::HandleBomUpdateModelAction(
                    state,
                    [&runtime](ProMdl mdl) { return ModelTag(mdl, runtime); },
                    [](const std::string &line) { LogPluginReportLine(line); },
                    refresh_bom_state);
            if (!result.had_drafts) {
                autobbox::ui::ShowSimpleMessageDialog(
                    PROUIMESSAGE_INFO,
                    L"BOM清单",
                    L"当前没有已勾选模型的待更新草稿值。");
                continue;
            }

            LogLine("Summary mode=bom-update modified_cells=%d cell_success=%d cell_skip=%d cell_fail=%d write_success=%d write_skip=%d write_fail=%d parse_fail=%d",
                    result.summary.modified_cells,
                    result.summary.cell_success,
                    result.summary.cell_skip,
                    result.summary.cell_fail,
                    result.summary.write_success,
                    result.summary.write_skip,
                    result.summary.write_fail,
                    result.summary.parse_fail);
            autobbox::ui::ShowSimpleMessageDialog(
                PROUIMESSAGE_INFO,
                L"BOM清单",
                (L"更新完成。\n成功单元格：" + std::to_wstring(result.summary.cell_success) +
                 L"\n跳过单元格：" + std::to_wstring(result.summary.cell_skip) +
                 L"\n失败单元格：" + std::to_wstring(result.summary.cell_fail)).c_str());
            continue;
        }

        if (action == BomToolDialogAction::ExportCsv) {
            const autobbox::application::BomExportCsvActionResult result =
                autobbox::application::HandleBomExportCsvAction(state);
            if (result.exported) {
                LogLine("BOM export-excel path=%s", autobbox::common::WToA(result.export_path.c_str()).c_str());
                autobbox::ui::ShowSimpleMessageDialog(
                    PROUIMESSAGE_INFO,
                    L"BOM清单",
                    (L"导出完成：\n" + result.export_path).c_str());
            } else {
                LogLine("FAIL bom-export-excel reason=create-file");
                autobbox::ui::ShowSimpleMessageDialog(
                    PROUIMESSAGE_ERROR,
                    L"BOM清单",
                    L"导出 CSV 失败，请检查当前工作目录权限。");
            }
            continue;
        }

        break;
    }

    OpenPluginReportLog();
    return 0;
}

} // namespace autobbox::main
