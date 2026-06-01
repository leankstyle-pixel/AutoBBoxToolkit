#include "autobbox/ui/sheetmetal_flat_batch_dialog.h"

#include "autobbox/application/sheetmetal_flat_batch.h"
#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/ui/message_dialog.h"

#include <ProUICheckbutton.h>
#include <ProUIDialog.h>
#include <ProUILabel.h>
#include <ProUIPushbutton.h>
#include <ProUITable.h>
#include <ProToolkit.h>
#include <ProUI.h>
#include <ProUIMessage.h>
#include <ProUtil.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace autobbox::ui {
namespace {

struct DialogConfig {
    const char *dialog = "autobbox_sheetmetal_flat_batch_inst";
    const char *resource = "autobbox_sheetmetal_flat_batch";
    const char *summary = "SummaryLabel";
    const char *table = "TargetTable";
    const char *base_check = "BaseTargetCheck";
    const char *select_all = "SelectAllBtn";
    const char *clear = "ClearBtn";
    const char *refresh = "RefreshBtn";
    const char *create_simprep = "CreateSimprepBtn";
    const char *create_family_flat = "CreateFamilyFlatBtn";
    const char *delete_selected = "DeleteBtn";
    const char *close = "CloseBtn";
    int status_close = 0;
    int status_deferred_action = 10;
};

struct DialogState {
    core::SheetmetalFlatCollectResult *collect_result = nullptr;
    std::unordered_map<std::string, std::string> checkbox_by_item_name;
    int checkbox_serial = 0;
    bool deferred_action_requested = false;
    core::SheetmetalFlatAction deferred_action = core::SheetmetalFlatAction::CreateModelFlatRep;
};

struct DialogRuntime {
    DialogState *state = nullptr;
    const DialogConfig *config = nullptr;
    SheetmetalFlatBatchDialogLogSink log_sink;
};

void PopulateTargetTable(char *dialog, DialogState *state, const DialogConfig &config);

void LogLine(const SheetmetalFlatBatchDialogLogSink &log_sink, const char *fmt, ...)
{
    if (!log_sink || fmt == nullptr) return;
    char buffer[4096] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_sink(buffer);
}

void OnDialogExit(char *dialog, char *, ProAppData app_data)
{
    if (dialog != nullptr) ProUIDialogExit(dialog, static_cast<int>(reinterpret_cast<std::intptr_t>(app_data)));
}

void SetLabelText(char *dialog, const char *component, const wchar_t *text)
{
    if (dialog != nullptr && component != nullptr) ProUILabelTextSet(dialog, const_cast<char *>(component), const_cast<wchar_t *>(text == nullptr ? L"" : text));
}

void SetButtonText(char *dialog, const char *component, const wchar_t *text)
{
    if (dialog != nullptr && component != nullptr) ProUIPushbuttonTextSet(dialog, const_cast<char *>(component), const_cast<wchar_t *>(text == nullptr ? L"" : text));
}

void ApplyChineseText(const DialogConfig &config, const core::SheetmetalFlatCollectResult &result)
{
    char *dialog = const_cast<char *>(config.dialog);
    ProUIDialogTitleSet(dialog, const_cast<wchar_t *>(L"批量钣金展平"));
    SetLabelText(dialog, config.summary, autobbox::application::BuildSheetmetalFlatCollectSummary(result).c_str());
    SetButtonText(dialog, config.select_all, L"全选");
    SetButtonText(dialog, config.clear, L"清空");
    SetButtonText(dialog, config.refresh, L"刷新");
    SetButtonText(dialog, config.create_simprep, L"创建展平表示(已取消)");
    SetButtonText(dialog, config.create_family_flat, L"创建展平实例");
    SetButtonText(dialog, config.delete_selected, L"删除勾选对象");
    SetButtonText(dialog, config.close, L"关闭");
    ProUIPushbuttonHelptextSet(dialog,
                               const_cast<char *>(config.create_simprep),
                               const_cast<wchar_t *>(L"创建展平表示功能已取消。请使用“创建展平实例”；删除按钮仍会删除已有展平表示。"));
    ProUIPushbuttonHelptextSet(dialog,
                               const_cast<char *>(config.create_family_flat),
                               const_cast<wchar_t *>(L"Create a flat family-table instance and set the Flat Pattern feature column to YES."));
    ProUIPushbuttonDisable(dialog, const_cast<char *>(config.create_simprep));
    ProUIPushbuttonEnable(dialog, const_cast<char *>(config.create_family_flat));
}

std::vector<std::string> GetSelectedRowNames(char *dialog, const DialogState *state)
{
    std::vector<std::string> selected;
    if (dialog == nullptr || state == nullptr || state->collect_result == nullptr) return selected;
    for (const auto &target : state->collect_result->targets) {
        const auto it = state->checkbox_by_item_name.find(target.item_name);
        if (it == state->checkbox_by_item_name.end()) continue;
        ProBoolean checked = PRO_B_FALSE;
        if (ProUICheckbuttonGetState(dialog, const_cast<char *>(it->second.c_str()), &checked) == PRO_TK_NO_ERROR &&
            checked == PRO_B_TRUE) {
            selected.push_back(target.item_name);
        }
    }
    return selected;
}

void SyncSelection(char *dialog, DialogState *state)
{
    if (dialog == nullptr || state == nullptr || state->collect_result == nullptr) return;
    const std::vector<std::string> selected = GetSelectedRowNames(dialog, state);
    for (auto &target : state->collect_result->targets) {
        target.selected = std::find(selected.begin(), selected.end(), target.item_name) != selected.end();
    }
}

void SetListState(char *dialog, const DialogState *state, ProUIMixedState item_state)
{
    if (dialog == nullptr || state == nullptr || state->collect_result == nullptr) return;
    for (const auto &target : state->collect_result->targets) {
        const auto it = state->checkbox_by_item_name.find(target.item_name);
        if (it == state->checkbox_by_item_name.end()) continue;
        if (item_state == PROUI_SET) ProUICheckbuttonSet(dialog, const_cast<char *>(it->second.c_str()));
        else ProUICheckbuttonUnset(dialog, const_cast<char *>(it->second.c_str()));
    }
}

void OnSelectAll(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<DialogRuntime *>(app_data);
    if (runtime != nullptr) SetListState(dialog, runtime->state, PROUI_SET);
}

void OnClear(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<DialogRuntime *>(app_data);
    if (runtime != nullptr) SetListState(dialog, runtime->state, PROUI_UNSET);
}

void OnRefresh(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<DialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->state->collect_result == nullptr || runtime->config == nullptr) return;
    *runtime->state->collect_result = autobbox::application::CollectSheetmetalFlatTargets();
    ApplyChineseText(*runtime->config, *runtime->state->collect_result);
    PopulateTargetTable(dialog, runtime->state, *runtime->config);
    SetListState(dialog, runtime->state, PROUI_SET);
    LogLine(runtime->log_sink,
            "sheetmetal-flat-dialog refresh targets=%d visited=%d skipped=%d",
            static_cast<int>(runtime->state->collect_result->targets.size()),
            runtime->state->collect_result->visited_components,
            runtime->state->collect_result->skipped_non_sheetmetal + runtime->state->collect_result->skipped_unreadable);
}

void RunAction(char *dialog,
               DialogRuntime *runtime,
               core::SheetmetalFlatAction action)
{
    if (dialog == nullptr || runtime == nullptr || runtime->state == nullptr || runtime->state->collect_result == nullptr || runtime->config == nullptr) return;
    SyncSelection(dialog, runtime->state);
    const int selected_count = static_cast<int>(std::count_if(
        runtime->state->collect_result->targets.begin(),
        runtime->state->collect_result->targets.end(),
        [](const auto &target) { return target.selected; }));
    if (selected_count <= 0) {
        ShowSimpleMessageDialog(PROUIMESSAGE_WARNING, L"批量钣金展平", L"请先勾选至少一个目标");
        return;
    }

    if (action == core::SheetmetalFlatAction::CreateModelFlatRep ||
        action == core::SheetmetalFlatAction::CreateFamilyFlat) {
        runtime->state->deferred_action_requested = true;
        runtime->state->deferred_action = action;
        ProUIDialogExit(dialog, runtime->config->status_deferred_action);
        return;
    }

    core::SheetmetalFlatActionResult result;
    bool ok = autobbox::application::DeleteSheetmetalFlatObjects(runtime->state->collect_result->targets, result, runtime->log_sink);
    PopulateTargetTable(dialog, runtime->state, *runtime->config);
    ShowSimpleMessageDialog(ok ? PROUIMESSAGE_INFO : PROUIMESSAGE_WARNING,
                            L"批量钣金展平",
                            result.summary_text.empty() ? (ok ? L"操作完成" : L"操作未全部完成") : result.summary_text.c_str());
}

void OnCreateSimprep(char *dialog, char *, ProAppData app_data)
{
    RunAction(dialog, reinterpret_cast<DialogRuntime *>(app_data), core::SheetmetalFlatAction::CreateModelFlatRep);
}

void OnCreateFamilyFlat(char *dialog, char *, ProAppData app_data)
{
    RunAction(dialog, reinterpret_cast<DialogRuntime *>(app_data), core::SheetmetalFlatAction::CreateFamilyFlat);
}

void OnDeleteSelected(char *dialog, char *, ProAppData app_data)
{
    RunAction(dialog, reinterpret_cast<DialogRuntime *>(app_data), core::SheetmetalFlatAction::DeleteSelected);
}

std::wstring SimprepLabel(const core::SheetmetalFlatTarget &target)
{
    if (!target.simprep_applicable) return L"不适用";
    if (!target.tool_simprep_names.empty()) {
        std::wstring out;
        for (size_t i = 0; i < target.tool_simprep_names.size(); ++i) {
            if (i > 0) out += L", ";
            out += target.tool_simprep_names[i];
        }
        return out;
    }
    return target.has_tool_simprep ? (target.tool_simprep_name.empty() ? L"已有" : target.tool_simprep_name) : L"未有";
}

std::wstring FamilyFlatLabel(const core::SheetmetalFlatTarget &target)
{
    if (!target.has_family_table) return L"未有族表";
    if (target.family_flat_instances.empty()) return L"?";
    std::wstring out;
    for (size_t i = 0; i < target.family_flat_instances.size(); ++i) {
        if (i > 0) out += L", ";
        out += target.family_flat_instances[i];
    }
    return out;
}

void PopulateTargetTable(char *dialog, DialogState *state, const DialogConfig &config)
{
    if (dialog == nullptr || state == nullptr || state->collect_result == nullptr) return;
    ++state->checkbox_serial;
    state->checkbox_by_item_name.clear();

    std::vector<std::string> column_names_storage = {"USE", "MODEL", "PATH", "SIMPREP", "FAMILY", "STATUS"};
    std::vector<std::wstring> column_labels_storage = {L"选择", L"模型", L"路径/层级", L"展平简化表示", L"族表 Flat/平整状态", L"状态"};
    std::vector<int> column_widths = {8, 24, 26, 24, 28, 52};
    std::vector<int> column_resizings = {0, 2, 2, 2, 2, 4};

    std::vector<char *> column_names;
    std::vector<wchar_t *> column_labels;
    for (auto &name : column_names_storage) column_names.push_back(const_cast<char *>(name.c_str()));
    for (auto &label : column_labels_storage) column_labels.push_back(const_cast<wchar_t *>(label.c_str()));

    std::vector<char *> row_names;
    std::vector<wchar_t *> row_labels;
    for (const auto &target : state->collect_result->targets) {
        row_names.push_back(const_cast<char *>(target.item_name.c_str()));
        row_labels.push_back(const_cast<wchar_t *>(L""));
    }

    ProUITableColumnnamesSet(dialog, const_cast<char *>(config.table), static_cast<int>(column_names.size()), column_names.data());
    ProUITableColumnlabelsSet(dialog, const_cast<char *>(config.table), static_cast<int>(column_labels.size()), column_labels.data());
    ProUITableColumnwidthsSet(dialog, const_cast<char *>(config.table), static_cast<int>(column_widths.size()), column_widths.data());
    ProUITableColumnresizingsSet(dialog, const_cast<char *>(config.table), static_cast<int>(column_resizings.size()), column_resizings.data());
    ProUITableRownamesSet(dialog, const_cast<char *>(config.table), static_cast<int>(row_names.size()), row_names.empty() ? nullptr : row_names.data());
    ProUITableRowlabelsSet(dialog, const_cast<char *>(config.table), static_cast<int>(row_labels.size()), row_labels.empty() ? nullptr : row_labels.data());
    ProUITableSelectionpolicySet(dialog, const_cast<char *>(config.table), PROUISELPOLICY_NONE);
    ProUITableColumnselectionpolicySet(dialog, const_cast<char *>(config.table), PROUISELPOLICY_NONE);
    ProUITableVisiblerowsSet(dialog, const_cast<char *>(config.table), std::min(14, std::max(4, static_cast<int>(state->collect_result->targets.size()))));
    ProUITableMinrowsSet(dialog, const_cast<char *>(config.table), std::min(14, std::max(4, static_cast<int>(state->collect_result->targets.size()))));
    ProUITableShowgridSet(dialog, const_cast<char *>(config.table), PRO_B_TRUE);
    ProUITableLockedcolumnsSet(dialog, const_cast<char *>(config.table), 0);

    for (const auto &target : state->collect_result->targets) {
        char check_name[80] = {0};
        std::snprintf(check_name, sizeof(check_name), "smtflatchk_%d_%s", state->checkbox_serial, target.item_name.c_str());
        state->checkbox_by_item_name[target.item_name] = check_name;
        ProUITableCellComponentCopy(dialog,
                                    const_cast<char *>(config.table),
                                    const_cast<char *>(target.item_name.c_str()),
                                    const_cast<char *>("USE"),
                                    dialog,
                                    const_cast<char *>(config.base_check),
                                    const_cast<char *>(state->checkbox_by_item_name[target.item_name].c_str()));
        ProUICheckbuttonTextSet(dialog, const_cast<char *>(state->checkbox_by_item_name[target.item_name].c_str()), const_cast<wchar_t *>(L""));
        if (target.selected) ProUICheckbuttonSet(dialog, const_cast<char *>(state->checkbox_by_item_name[target.item_name].c_str()));
        else ProUICheckbuttonUnset(dialog, const_cast<char *>(state->checkbox_by_item_name[target.item_name].c_str()));

        const std::wstring simprep = SimprepLabel(target);
        const std::wstring family = FamilyFlatLabel(target);
        ProUITableCellLabelSet(dialog, const_cast<char *>(config.table), const_cast<char *>(target.item_name.c_str()), const_cast<char *>("MODEL"), const_cast<wchar_t *>(target.model_name.c_str()));
        ProUITableCellLabelSet(dialog, const_cast<char *>(config.table), const_cast<char *>(target.item_name.c_str()), const_cast<char *>("PATH"), const_cast<wchar_t *>(target.display_path.c_str()));
        ProUITableCellLabelSet(dialog, const_cast<char *>(config.table), const_cast<char *>(target.item_name.c_str()), const_cast<char *>("SIMPREP"), const_cast<wchar_t *>(simprep.c_str()));
        ProUITableCellLabelSet(dialog, const_cast<char *>(config.table), const_cast<char *>(target.item_name.c_str()), const_cast<char *>("FAMILY"), const_cast<wchar_t *>(family.c_str()));
        ProUITableCellLabelSet(dialog, const_cast<char *>(config.table), const_cast<char *>(target.item_name.c_str()), const_cast<char *>("STATUS"), const_cast<wchar_t *>(target.status_text.c_str()));
        ProUITableCellHelptextStringSet(dialog, const_cast<char *>(config.table), const_cast<char *>(target.item_name.c_str()), const_cast<char *>("PATH"), const_cast<wchar_t *>(target.display_path.c_str()));
        ProUITableCellHelptextStringSet(dialog, const_cast<char *>(config.table), const_cast<char *>(target.item_name.c_str()), const_cast<char *>("FAMILY"), const_cast<wchar_t *>(family.c_str()));
        ProUITableCellHelptextStringSet(dialog, const_cast<char *>(config.table), const_cast<char *>(target.item_name.c_str()), const_cast<char *>("STATUS"), const_cast<wchar_t *>(target.status_text.c_str()));
        ProUITableCellBackgroundColorSet(dialog, const_cast<char *>(config.table), const_cast<char *>(target.item_name.c_str()), const_cast<char *>("STATUS"), target.has_error ? PRO_UI_COLOR_RED : PRO_UI_COLOR_WHITE);
        ProUITableCellForegroundColorSet(dialog, const_cast<char *>(config.table), const_cast<char *>(target.item_name.c_str()), const_cast<char *>("STATUS"), target.has_error ? PRO_UI_COLOR_WHITE : PRO_UI_COLOR_BLACK);
    }
}

ProError TryCreateDialog(const DialogConfig &config, const SheetmetalFlatBatchDialogLogSink &log_sink, std::string &used_resource)
{
    used_resource.clear();
    const std::string base = config.resource;
    std::vector<std::string> rel_candidates = {
        base,
        base + ".res",
        std::string("resource\\") + base,
        std::string("resource\\") + base + ".res",
        std::string("text\\resource\\") + base,
        std::string("text\\resource\\") + base + ".res",
    };
    ProError last = PRO_TK_GENERAL_ERROR;
    for (const auto &res : rel_candidates) {
        last = ProUIDialogCreate(const_cast<char *>(config.dialog), const_cast<char *>(res.c_str()));
        LogLine(log_sink, "sheetmetal-flat-dialog create try resource=%s status=%d", res.c_str(), static_cast<int>(last));
        if (last == PRO_TK_NO_ERROR) { used_resource = res; return last; }
    }

    ProPath text_root_w = {0};
    if (ProToolkitApplTextPathGet(text_root_w) == PRO_TK_NO_ERROR) {
        const std::string text_root = autobbox::common::WToA(text_root_w);
        std::vector<std::string> abs_candidates = {
            text_root + "\\resource\\" + base,
            text_root + "\\resource\\" + base + ".res",
            text_root + "\\text\\resource\\" + base,
            text_root + "\\text\\resource\\" + base + ".res",
        };
        for (const auto &path : abs_candidates) {
            if (!autobbox::common::FileExistsA(path)) continue;
            last = ProUIDialogCreate(const_cast<char *>(config.dialog), const_cast<char *>(path.c_str()));
            LogLine(log_sink, "sheetmetal-flat-dialog create try resource=%s status=%d", path.c_str(), static_cast<int>(last));
            if (last == PRO_TK_NO_ERROR) { used_resource = path; return last; }
        }
    }
    return last;
}

} // namespace

bool PromptSheetmetalFlatBatchDialog(core::SheetmetalFlatCollectResult &collect_result,
                                     bool &cancelled,
                                     bool &deferred_action_requested,
                                     core::SheetmetalFlatAction &deferred_action,
                                     const SheetmetalFlatBatchDialogLogSink &log_sink)
{
    cancelled = false;
    deferred_action_requested = false;
    deferred_action = core::SheetmetalFlatAction::CreateModelFlatRep;
    const DialogConfig config;
    std::string used_resource;
    const ProError create_status = TryCreateDialog(config, log_sink, used_resource);
    if (create_status != PRO_TK_NO_ERROR) {
        ShowSimpleMessageDialog(PROUIMESSAGE_ERROR, L"批量钣金展平", L"无法创建批量钣金展平对话框");
        cancelled = true;
        return false;
    }

    DialogState state;
    state.collect_result = &collect_result;
    DialogRuntime runtime;
    runtime.state = &state;
    runtime.config = &config;
    runtime.log_sink = log_sink;

    ApplyChineseText(config, collect_result);
    PopulateTargetTable(const_cast<char *>(config.dialog), &state, config);
    SetListState(const_cast<char *>(config.dialog), &state, PROUI_SET);

    ProUIPushbuttonActivateActionSet(const_cast<char *>(config.dialog), const_cast<char *>(config.select_all), OnSelectAll, &runtime);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(config.dialog), const_cast<char *>(config.clear), OnClear, &runtime);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(config.dialog), const_cast<char *>(config.refresh), OnRefresh, &runtime);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(config.dialog), const_cast<char *>(config.create_simprep), OnCreateSimprep, &runtime);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(config.dialog), const_cast<char *>(config.create_family_flat), OnCreateFamilyFlat, &runtime);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(config.dialog), const_cast<char *>(config.delete_selected), OnDeleteSelected, &runtime);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(config.dialog), const_cast<char *>(config.close), OnDialogExit, reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_close)));
    ProUIDialogCloseActionSet(const_cast<char *>(config.dialog), OnDialogExit, reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_close)));

    int dialog_status = config.status_close;
    const ProError activate_status = ProUIDialogActivate(const_cast<char *>(config.dialog), &dialog_status);
    if (activate_status != PRO_TK_NO_ERROR) {
        cancelled = true;
        ProUIDialogDestroy(const_cast<char *>(config.dialog));
        return false;
    }

    cancelled = (dialog_status == config.status_close);
    deferred_action_requested = state.deferred_action_requested && dialog_status == config.status_deferred_action;
    deferred_action = state.deferred_action;
    ProUIDialogDestroy(const_cast<char *>(config.dialog));
    return true;
}

} // namespace autobbox::ui
