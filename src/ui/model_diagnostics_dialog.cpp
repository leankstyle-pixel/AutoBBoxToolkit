#include "autobbox/ui/model_diagnostics_dialog.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/ui/message_dialog.h"

#include <ProArray.h>
#include <ProUILabel.h>
#include <ProUIPushbutton.h>
#include <ProUITable.h>
#include <ProUIDialog.h>
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
    const char *dialog = "autobbox_model_diagnostics_inst";
    const char *resource = "autobbox_model_diagnostics";
    const char *summary_label = "SummaryLabel";
    const char *table = "IssueTable";
    const char *status_label = "StatusLabel";
    const char *locate_btn = "LocateBtn";
    const char *deep_btn = "DeepBtn";
    const char *report_btn = "ReportBtn";
    const char *close_btn = "CloseBtn";
    int status_close = 0;
    int status_deep = 2;
};

struct DialogState {
    const std::vector<autobbox::application::ModelDiagnosticItem> *items = nullptr;
    std::unordered_map<std::string, size_t> row_to_index;
    size_t selected_index = 0;
    bool deep_already_run = false;
};

struct DialogRuntime {
    const DialogConfig *config = nullptr;
    DialogState *state = nullptr;
    const ModelDiagnosticsLocateAction *locate_action = nullptr;
    const ModelDiagnosticsOpenReportAction *open_report_action = nullptr;
    ModelDiagnosticsDialogLogSink log_sink;
};

void LogLine(const ModelDiagnosticsDialogLogSink &log_sink, const char *fmt, ...)
{
    if (!log_sink || fmt == nullptr) {
        return;
    }
    char buffer[4096] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_sink(buffer);
}

ProError TryCreateDialog(const DialogConfig &config,
                         const ModelDiagnosticsDialogLogSink &log_sink)
{
    const std::string base_name = config.resource;
    const std::vector<std::string> rel_candidates = {
        base_name,
        base_name + ".res",
        std::string("resource\\") + base_name,
        std::string("resource\\") + base_name + ".res",
        std::string("text\\resource\\") + base_name,
        std::string("text\\resource\\") + base_name + ".res",
        std::string("text\\usascii\\resource\\") + base_name,
        std::string("text\\usascii\\resource\\") + base_name + ".res",
    };

    ProError last = PRO_TK_GENERAL_ERROR;
    for (const std::string &resource : rel_candidates) {
        last = ProUIDialogCreate(const_cast<char *>(config.dialog),
                                 const_cast<char *>(resource.c_str()));
        LogLine(log_sink,
                "model-diagnostics-dialog create try resource=%s status=%d",
                resource.c_str(),
                static_cast<int>(last));
        if (last == PRO_TK_NO_ERROR) {
            return last;
        }
    }

    ProPath text_root_w = {0};
    if (ProToolkitApplTextPathGet(text_root_w) == PRO_TK_NO_ERROR) {
        const std::string text_root = autobbox::common::WToA(text_root_w);
        const std::vector<std::string> abs_candidates = {
            text_root + "\\resource\\" + base_name,
            text_root + "\\resource\\" + base_name + ".res",
            text_root + "\\text\\resource\\" + base_name,
            text_root + "\\text\\resource\\" + base_name + ".res",
            text_root + "\\text\\usascii\\resource\\" + base_name,
            text_root + "\\text\\usascii\\resource\\" + base_name + ".res",
        };
        for (const std::string &resource : abs_candidates) {
            if (!autobbox::common::FileExistsA(resource)) {
                continue;
            }
            last = ProUIDialogCreate(const_cast<char *>(config.dialog),
                                     const_cast<char *>(resource.c_str()));
            LogLine(log_sink,
                    "model-diagnostics-dialog create try resource=%s status=%d",
                    resource.c_str(),
                    static_cast<int>(last));
            if (last == PRO_TK_NO_ERROR) {
                return last;
            }
        }
    }
    return last;
}

std::wstring FirstFeatureText(const autobbox::application::ModelDiagnosticItem &item)
{
    auto format = [](const autobbox::application::ModelDiagnosticFeature &feature) {
        std::wstring out = L"#" + std::to_wstring(feature.id);
        if (!feature.tree_name.empty()) {
            out += L" ";
            out += feature.tree_name;
        } else if (!feature.type_name.empty()) {
            out += L" ";
            out += feature.type_name;
        }
        if (!feature.kind.empty()) {
            out += L" ";
            out += feature.kind;
        }
        return out;
    };
    if (!item.failed_features.empty()) {
        return format(item.failed_features.front());
    }
    if (!item.child_failed_features.empty()) {
        return format(item.child_failed_features.front());
    }
    if (!item.external_child_failed_features.empty()) {
        return format(item.external_child_failed_features.front());
    }
    if (!item.flagged_features.empty()) {
        return format(item.flagged_features.front());
    }
    return L"-";
}

std::wstring Truncate(const std::wstring &value, size_t max_chars)
{
    if (value.size() <= max_chars) {
        return value;
    }
    if (max_chars <= 1) {
        return value.substr(0, max_chars);
    }
    return value.substr(0, max_chars - 1) + L"…";
}

std::wstring CellText(const autobbox::application::ModelDiagnosticItem &item,
                      const std::string &column)
{
    if (column == "LEVEL") {
        return autobbox::application::ModelDiagnosticSeverityLabel(item.severity);
    }
    if (column == "MODEL") {
        return item.model_name;
    }
    if (column == "TYPE") {
        return item.model_type_label;
    }
    if (column == "REASON") {
        return Truncate(item.reason, 46);
    }
    if (column == "FEATURE") {
        return Truncate(FirstFeatureText(item), 30);
    }
    if (column == "SUGGESTION") {
        return Truncate(item.suggestion, 54);
    }
    return L"";
}

std::wstring SummaryText(const std::vector<autobbox::application::ModelDiagnosticItem> &items,
                         bool deep_already_run)
{
    return autobbox::application::BuildModelDiagnosticsSummary(items) +
           (deep_already_run ? L"（已执行深度检测）" : L"");
}

void SetStatus(const DialogConfig &config, const std::wstring &text)
{
    ProUILabelTextSet(const_cast<char *>(config.dialog),
                      const_cast<char *>(config.status_label),
                      const_cast<wchar_t *>(text.c_str()));
}

bool ResolveFocusedRow(char *dialog,
                       const DialogRuntime *runtime,
                       size_t &index_out)
{
    if (dialog == nullptr || runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return false;
    }

    char *row_name = nullptr;
    char *column_name = nullptr;
    const ProError st = ProUITableFocusCellGet(
        dialog,
        const_cast<char *>(runtime->config->table),
        &row_name,
        &column_name);
    if (column_name != nullptr) {
        ProStringFree(column_name);
    }
    if (st == PRO_TK_NO_ERROR && row_name != nullptr) {
        const auto it = runtime->state->row_to_index.find(row_name);
        ProStringFree(row_name);
        if (it != runtime->state->row_to_index.end()) {
            runtime->state->selected_index = it->second;
            index_out = it->second;
            return true;
        }
    } else if (row_name != nullptr) {
        ProStringFree(row_name);
    }

    if (runtime->state->items != nullptr &&
        runtime->state->selected_index < runtime->state->items->size()) {
        index_out = runtime->state->selected_index;
        return true;
    }
    return false;
}

void OnTableSelect(char *dialog, char *, ProAppData app_data)
{
    DialogRuntime *runtime = reinterpret_cast<DialogRuntime *>(app_data);
    size_t index = 0;
    if (ResolveFocusedRow(dialog, runtime, index) &&
        runtime != nullptr &&
        runtime->state != nullptr &&
        runtime->state->items != nullptr &&
        index < runtime->state->items->size()) {
        const auto &item = (*runtime->state->items)[index];
        SetStatus(*runtime->config, L"已选择：" + item.model_name + L"。点击“定位模型”可在当前窗口的选择缓冲区/模型树中定位。");
    }
}

void OnLocate(char *dialog, char *, ProAppData app_data)
{
    DialogRuntime *runtime = reinterpret_cast<DialogRuntime *>(app_data);
    if (runtime == nullptr ||
        runtime->config == nullptr ||
        runtime->state == nullptr ||
        runtime->state->items == nullptr ||
        runtime->locate_action == nullptr ||
        !*runtime->locate_action) {
        return;
    }

    size_t index = 0;
    if (!ResolveFocusedRow(dialog, runtime, index) || index >= runtime->state->items->size()) {
        SetStatus(*runtime->config, L"请先在表格中选择一个异常模型。");
        return;
    }

    const auto &item = (*runtime->state->items)[index];
    const ProError st = (*runtime->locate_action)(item);
    if (st == PRO_TK_NO_ERROR) {
        SetStatus(*runtime->config, L"已在模型树/当前窗口定位：" + item.model_name);
    } else {
        SetStatus(*runtime->config,
                  L"定位失败：" + item.model_name + L"，状态码 " +
                      std::to_wstring(static_cast<int>(st)));
    }
}

void OnOpenReport(char *, char *, ProAppData app_data)
{
    DialogRuntime *runtime = reinterpret_cast<DialogRuntime *>(app_data);
    if (runtime != nullptr &&
        runtime->open_report_action != nullptr &&
        *runtime->open_report_action) {
        (*runtime->open_report_action)();
        if (runtime->config != nullptr) {
            SetStatus(*runtime->config, L"已打开/刷新报告文件。");
        }
    }
}

void OnRequestDeep(char *dialog, char *, ProAppData app_data)
{
    DialogRuntime *runtime = reinterpret_cast<DialogRuntime *>(app_data);
    if (runtime != nullptr && runtime->state != nullptr && runtime->state->deep_already_run) {
        if (runtime->config != nullptr) {
            SetStatus(*runtime->config, L"已执行过深度检测。");
        }
        return;
    }
    if (dialog != nullptr) {
        ProUIDialogExit(dialog, DialogConfig().status_deep);
    }
}

void OnClose(char *dialog, char *, ProAppData)
{
    if (dialog != nullptr) {
        ProUIDialogExit(dialog, DialogConfig().status_close);
    }
}

void ConfigureTable(const DialogConfig &config, DialogState &state)
{
    const std::vector<std::string> column_names_storage = {
        "LEVEL", "MODEL", "TYPE", "REASON", "FEATURE", "SUGGESTION"};
    std::vector<std::wstring> column_labels_storage = {
        L"等级", L"模型", L"类型", L"异常原因", L"首个特征", L"建议"};
    std::vector<int> column_widths = {8, 24, 8, 34, 24, 42};
    std::vector<int> column_resizings = {1, 2, 1, 3, 2, 4};
    std::vector<char *> column_names;
    std::vector<wchar_t *> column_labels;
    for (const std::string &name : column_names_storage) {
        column_names.push_back(const_cast<char *>(name.c_str()));
    }
    for (std::wstring &label : column_labels_storage) {
        column_labels.push_back(const_cast<wchar_t *>(label.c_str()));
    }

    std::vector<std::string> row_name_storage;
    std::vector<std::wstring> row_label_storage;
    const size_t row_count = state.items == nullptr ? 0 : state.items->size();
    row_name_storage.reserve(row_count);
    row_label_storage.reserve(row_count);
    for (size_t i = 0; i < row_count; ++i) {
        row_name_storage.push_back("md_" + std::to_string(i));
        row_label_storage.emplace_back();
        state.row_to_index[row_name_storage.back()] = i;
    }

    std::vector<char *> row_names;
    std::vector<wchar_t *> row_labels;
    for (std::string &name : row_name_storage) {
        row_names.push_back(const_cast<char *>(name.c_str()));
    }
    for (std::wstring &label : row_label_storage) {
        row_labels.push_back(const_cast<wchar_t *>(label.c_str()));
    }

    char *dialog = const_cast<char *>(config.dialog);
    char *table = const_cast<char *>(config.table);
    ProUITableColumnnamesSet(dialog, table, static_cast<int>(column_names.size()), column_names.data());
    ProUITableColumnlabelsSet(dialog, table, static_cast<int>(column_labels.size()), column_labels.data());
    ProUITableColumnwidthsSet(dialog, table, static_cast<int>(column_widths.size()), column_widths.data());
    ProUITableColumnresizingsSet(dialog, table, static_cast<int>(column_resizings.size()), column_resizings.data());
    ProUITableRownamesSet(dialog, table, static_cast<int>(row_names.size()), row_names.empty() ? nullptr : row_names.data());
    ProUITableRowlabelsSet(dialog, table, static_cast<int>(row_labels.size()), row_labels.empty() ? nullptr : row_labels.data());
    ProUITableSelectionpolicySet(dialog, table, PROUISELPOLICY_SINGLE);
    ProUITableColumnselectionpolicySet(dialog, table, PROUISELPOLICY_NONE);
    ProUITableShowgridSet(dialog, table, PRO_B_TRUE);
    ProUITableAutohighlightEnable(dialog, table);
    ProUITableVisiblerowsSet(dialog, table, std::min(14, std::max(4, static_cast<int>(row_count) + 1)));
    ProUITableMinrowsSet(dialog, table, std::min(12, std::max(4, static_cast<int>(row_count))));

    if (state.items == nullptr) {
        return;
    }
    for (size_t i = 0; i < state.items->size(); ++i) {
        const std::string &row = row_name_storage[i];
        const auto &item = (*state.items)[i];
        for (const std::string &column : column_names_storage) {
            ProUITableCellLabelSet(dialog,
                                   table,
                                   const_cast<char *>(row.c_str()),
                                   const_cast<char *>(column.c_str()),
                                   const_cast<wchar_t *>(CellText(item, column).c_str()));
        }
        const ProUIColor level_bg =
            item.severity == autobbox::application::ModelDiagnosticSeverity::Error
                ? PRO_UI_COLOR_RED
                : (item.severity == autobbox::application::ModelDiagnosticSeverity::Warning
                       ? PRO_UI_COLOR_YELLOW
                       : PRO_UI_COLOR_LT_GREY);
        const ProUIColor level_fg =
            item.severity == autobbox::application::ModelDiagnosticSeverity::Error
                ? PRO_UI_COLOR_WHITE
                : PRO_UI_COLOR_BLACK;
        ProUITableCellBackgroundColorSet(dialog, table, const_cast<char *>(row.c_str()), const_cast<char *>("LEVEL"), level_bg);
        ProUITableCellForegroundColorSet(dialog, table, const_cast<char *>(row.c_str()), const_cast<char *>("LEVEL"), level_fg);
    }
}

} // namespace

ModelDiagnosticsDialogResult PromptModelDiagnosticsDialog(
    const std::vector<autobbox::application::ModelDiagnosticItem> &items,
    bool deep_already_run,
    const ModelDiagnosticsLocateAction &locate_action,
    const ModelDiagnosticsOpenReportAction &open_report_action,
    const ModelDiagnosticsDialogLogSink &log_sink)
{
    const DialogConfig config;
    const ProError create_status = TryCreateDialog(config, log_sink);
    if (create_status != PRO_TK_NO_ERROR) {
        ShowSimpleMessageDialog(
            PROUIMESSAGE_ERROR,
            L"模型检测",
            L"打开模型检测摘要窗口失败。请查看报告文件。");
        return ModelDiagnosticsDialogResult::Closed;
    }

    DialogState state;
    state.items = &items;
    state.deep_already_run = deep_already_run;
    state.selected_index = 0;

    DialogRuntime runtime;
    runtime.config = &config;
    runtime.state = &state;
    runtime.locate_action = &locate_action;
    runtime.open_report_action = &open_report_action;
    runtime.log_sink = log_sink;

    char *dialog = const_cast<char *>(config.dialog);
    ProUIDialogTitleSet(dialog, const_cast<wchar_t *>(L"模型检测"));
    ProUILabelTextSet(dialog,
                      const_cast<char *>(config.summary_label),
                      const_cast<wchar_t *>(SummaryText(items, deep_already_run).c_str()));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.locate_btn), const_cast<wchar_t *>(L"定位模型"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.deep_btn), const_cast<wchar_t *>(deep_already_run ? L"已深度检测" : L"深度检测"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.report_btn), const_cast<wchar_t *>(L"打开报告"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.close_btn), const_cast<wchar_t *>(L"关闭"));
    SetStatus(config, items.empty() ? L"未找到可检测模型。" : L"选择异常模型后点击“定位模型”，不会单独打开或替换显示窗口。");

    ConfigureTable(config, state);
    ProUITableSelectActionSet(dialog, const_cast<char *>(config.table), OnTableSelect, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.locate_btn), OnLocate, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.report_btn), OnOpenReport, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.deep_btn), OnRequestDeep, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.close_btn), OnClose, &runtime);
    ProUIDialogCloseActionSet(dialog, OnClose, nullptr);
    ProUIDialogDefaultbuttonSet(dialog, const_cast<char *>(config.locate_btn));

    int dialog_status = config.status_close;
    const ProError activate_status = ProUIDialogActivate(dialog, &dialog_status);
    LogLine(log_sink,
            "model-diagnostics-dialog activate status=%d dialog_status=%d",
            static_cast<int>(activate_status),
            dialog_status);
    ProUIDialogDestroy(dialog);

    if (activate_status == PRO_TK_NO_ERROR && dialog_status == config.status_deep) {
        return ModelDiagnosticsDialogResult::RequestDeepCheck;
    }
    return ModelDiagnosticsDialogResult::Closed;
}

} // namespace autobbox::ui
