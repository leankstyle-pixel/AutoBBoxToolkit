#include "autobbox/ui/drawing_export_dialog.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/ui/message_dialog.h"

#include <ProUIDialog.h>
#include <ProUILabel.h>
#include <ProUIList.h>
#include <ProUIPushbutton.h>
#include <ProUIRadiogroup.h>
#include <ProToolkit.h>
#include <ProUtil.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

namespace autobbox::ui {

namespace {

struct DrawingExportDialogConfig {
    const char *dialog_inst_name = nullptr;
    const char *resource_base_name = nullptr;
    const char *prompt_label_comp = nullptr;
    const char *format_group_comp = nullptr;
    const char *dwg_mode_label_comp = nullptr;
    const char *dwg_mode_group_comp = nullptr;
    const char *sheet_label_comp = nullptr;
    const char *sheet_list_comp = nullptr;
    const char *output_label_comp = nullptr;
    const char *ok_comp = nullptr;
    const char *cancel_comp = nullptr;
    int status_ok = 0;
    int status_cancel = 0;
};

DrawingExportDialogConfig DefaultConfig()
{
    DrawingExportDialogConfig config = {};
    config.dialog_inst_name = "autobbox_drawing_export_inst";
    config.resource_base_name = "autobbox_drawing_export";
    config.prompt_label_comp = "PromptLabel";
    config.format_group_comp = "FormatGroup";
    config.dwg_mode_label_comp = "DwgModeLabel";
    config.dwg_mode_group_comp = "DwgModeGroup";
    config.sheet_label_comp = "SheetLabel";
    config.sheet_list_comp = "SheetList";
    config.output_label_comp = "OutputLabel";
    config.ok_comp = "OKBtn";
    config.cancel_comp = "CancelBtn";
    config.status_ok = 1;
    config.status_cancel = 0;
    return config;
}

struct DrawingExportDialogState {
    core::DrawingExportRequest request;
    std::vector<core::DrawingExportSheetChoice> sheet_choices;
    std::vector<std::string> sheet_names;
    std::vector<std::wstring> sheet_labels;
};

struct DrawingExportDialogRuntime {
    DrawingExportDialogState *state = nullptr;
    const DrawingExportDialogConfig *config = nullptr;
    DrawingExportDialogLogSink log_sink;
};

void LogLine(const DrawingExportDialogLogSink &log_sink, const char *fmt, ...)
{
    if (!log_sink || fmt == nullptr) {
        return;
    }

    char buffer[2048] = {0};
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    log_sink(buffer);
}

void OnDialogExit(char *dialog, char *, ProAppData app_data)
{
    if (dialog != nullptr) {
        ProUIDialogExit(dialog, static_cast<int>(reinterpret_cast<std::intptr_t>(app_data)));
    }
}

void UpdateDwgModeAvailability(char *dialog, const DrawingExportDialogRuntime *runtime)
{
    if (dialog == nullptr || runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return;
    }
    if (runtime->state->request.format == core::DrawingExportFormat::Dwg) {
        ProUIRadiogroupEnable(dialog, const_cast<char *>(runtime->config->dwg_mode_group_comp));
    } else {
        ProUIRadiogroupDisable(dialog, const_cast<char *>(runtime->config->dwg_mode_group_comp));
    }

    if (runtime->state->request.format == core::DrawingExportFormat::Dwg) {
        ProUIListEnable(dialog, const_cast<char *>(runtime->config->sheet_list_comp));
    } else {
        ProUIListDisable(dialog, const_cast<char *>(runtime->config->sheet_list_comp));
    }
}

void OnFormatChanged(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<DrawingExportDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr || dialog == nullptr) {
        return;
    }

    int count = 0;
    char **names = nullptr;
    if (ProUIRadiogroupSelectednamesGet(
            dialog,
            const_cast<char *>(runtime->config->format_group_comp),
            &count,
            &names) == PRO_TK_NO_ERROR &&
        count > 0 &&
        names != nullptr &&
        names[0] != nullptr) {
        const std::string selected(names[0]);
        if (selected == "dwg") {
            runtime->state->request.format = core::DrawingExportFormat::Dwg;
        } else if (selected == "dxf") {
            runtime->state->request.format = core::DrawingExportFormat::Dxf;
        } else {
            runtime->state->request.format = core::DrawingExportFormat::Pdf;
        }
    }
    if (names != nullptr) {
        ProStringarrayFree(names, count);
    }

    UpdateDwgModeAvailability(dialog, runtime);
}

void OnDwgModeChanged(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<DrawingExportDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr || dialog == nullptr) {
        return;
    }

    int count = 0;
    char **names = nullptr;
    if (ProUIRadiogroupSelectednamesGet(
            dialog,
            const_cast<char *>(runtime->config->dwg_mode_group_comp),
            &count,
            &names) == PRO_TK_NO_ERROR &&
        count > 0 &&
        names != nullptr &&
        names[0] != nullptr) {
        const std::string selected(names[0]);
        runtime->state->request.dwg_mode =
            selected == "per_sheet" ? core::DwgExportMode::PerSheetFiles : core::DwgExportMode::MultiLayoutFile;
    }
    if (names != nullptr) {
        ProStringarrayFree(names, count);
    }

    UpdateDwgModeAvailability(dialog, runtime);
}

void OnSheetSelect(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<DrawingExportDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr || dialog == nullptr) {
        return;
    }

    int count = 0;
    char **names = nullptr;
    if (ProUIListSelectednamesGet(
            dialog,
            const_cast<char *>(runtime->config->sheet_list_comp),
            &count,
            &names) == PRO_TK_NO_ERROR &&
        count > 0 &&
        names != nullptr &&
        names[0] != nullptr) {
        const std::string selected(names[0]);
        constexpr char kPrefix[] = "sheet_";
        if (selected.rfind(kPrefix, 0) == 0) {
            const int sheet = std::atoi(selected.c_str() + sizeof(kPrefix) - 1);
            if (sheet > 0) {
                runtime->state->request.selected_sheet = sheet;
            }
        }
    }
    if (names != nullptr) {
        ProStringarrayFree(names, count);
    }
}

ProError TryCreateDialog(const DrawingExportDialogConfig &config,
                         const DrawingExportDialogLogSink &log_sink,
                         std::string &used_resource)
{
    used_resource.clear();
    if (config.dialog_inst_name == nullptr || config.resource_base_name == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    const std::string base_name = config.resource_base_name;
    const std::vector<std::string> rel_candidates = {
        base_name,
        std::string("resource\\") + base_name,
        std::string("text\\resource\\") + base_name,
        std::string("text\\usascii\\resource\\") + base_name,
        std::string("usascii\\resource\\") + base_name
    };

    ProError last = PRO_TK_GENERAL_ERROR;
    for (const std::string &res : rel_candidates) {
        last = ProUIDialogCreate(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(res.c_str()));
        LogLine(log_sink,
                "drawing-export-dialog-create try resource=%s status=%d",
                res.c_str(),
                static_cast<int>(last));
        if (last == PRO_TK_NO_ERROR) {
            used_resource = res;
            return last;
        }
    }

    ProPath wtext = {0};
    if (ProToolkitApplTextPathGet(wtext) == PRO_TK_NO_ERROR) {
        const std::string text_root = autobbox::common::WToA(wtext);
        if (!text_root.empty()) {
            const std::vector<std::string> abs_candidates = {
                text_root + "\\resource\\" + base_name,
                text_root + "\\resource\\" + base_name + ".res",
                text_root + "\\text\\resource\\" + base_name,
                text_root + "\\text\\resource\\" + base_name + ".res",
                text_root + "\\text\\usascii\\resource\\" + base_name,
                text_root + "\\text\\usascii\\resource\\" + base_name + ".res"
            };
            for (const std::string &path : abs_candidates) {
                LogLine(log_sink,
                        "drawing-export-dialog-path probe exists=%d path=%s",
                        autobbox::common::FileExistsA(path) ? 1 : 0,
                        path.c_str());
                last = ProUIDialogCreate(
                    const_cast<char *>(config.dialog_inst_name),
                    const_cast<char *>(path.c_str()));
                LogLine(log_sink,
                        "drawing-export-dialog-create try resource=%s status=%d",
                        path.c_str(),
                        static_cast<int>(last));
                if (last == PRO_TK_NO_ERROR) {
                    used_resource = path;
                    return last;
                }
            }
        }
    }

    return last;
}

void SetRadiogroup(char *dialog,
                   const char *component,
                   int count,
                   char **names,
                   wchar_t **labels,
                   const char *selected)
{
    if (dialog == nullptr || component == nullptr || count <= 0 || names == nullptr || labels == nullptr) {
        return;
    }
    ProUIRadiogroupNamesSet(dialog, const_cast<char *>(component), count, names);
    ProUIRadiogroupLabelsSet(dialog, const_cast<char *>(component), count, labels);
    char *selected_items[] = {const_cast<char *>(selected)};
    ProUIRadiogroupSelectednamesSet(dialog, const_cast<char *>(component), 1, selected_items);
}

void SetSheetList(char *dialog,
                  const DrawingExportDialogConfig &config,
                  DrawingExportDialogState &state)
{
    if (dialog == nullptr) {
        return;
    }

    state.sheet_names.clear();
    state.sheet_labels.clear();
    state.sheet_names.reserve(state.sheet_choices.size());
    state.sheet_labels.reserve(state.sheet_choices.size());
    for (const core::DrawingExportSheetChoice &choice : state.sheet_choices) {
        char name[32] = {0};
        std::snprintf(name, sizeof(name), "sheet_%d", choice.sheet);
        state.sheet_names.emplace_back(name);
        state.sheet_labels.push_back(choice.display_name.empty() ? std::wstring(L"sheet") : choice.display_name);
    }

    std::vector<char *> name_ptrs;
    std::vector<wchar_t *> label_ptrs;
    name_ptrs.reserve(state.sheet_names.size());
    label_ptrs.reserve(state.sheet_labels.size());
    for (std::string &name : state.sheet_names) {
        name_ptrs.push_back(const_cast<char *>(name.c_str()));
    }
    for (std::wstring &label : state.sheet_labels) {
        label_ptrs.push_back(const_cast<wchar_t *>(label.c_str()));
    }

    ProUIListColumnsSet(dialog, const_cast<char *>(config.sheet_list_comp), 1);
    ProUIListVisiblerowsSet(
        dialog,
        const_cast<char *>(config.sheet_list_comp),
        std::min(8, std::max(3, static_cast<int>(state.sheet_choices.size()))));
    ProUIListMinrowsSet(
        dialog,
        const_cast<char *>(config.sheet_list_comp),
        std::min(8, std::max(3, static_cast<int>(state.sheet_choices.size()))));
    ProUIListSelectionpolicySet(dialog, const_cast<char *>(config.sheet_list_comp), PROUISELPOLICY_SINGLE);
    ProUIListListtypeSet(dialog, const_cast<char *>(config.sheet_list_comp), PROUILISTTYPE_CHECk);
    ProUIListSelectionpolicySet(dialog, const_cast<char *>(config.sheet_list_comp), PROUISELPOLICY_NONE);
    if (!name_ptrs.empty()) {
        ProUIListNamesSet(
            dialog,
            const_cast<char *>(config.sheet_list_comp),
            static_cast<int>(name_ptrs.size()),
            name_ptrs.data());
        ProUIListLabelsSet(
            dialog,
            const_cast<char *>(config.sheet_list_comp),
            static_cast<int>(label_ptrs.size()),
            label_ptrs.data());
    } else {
        return;
    }

    std::vector<int> checked_sheets = state.request.selected_sheets;
    if (checked_sheets.empty()) {
        checked_sheets.push_back(state.request.selected_sheet > 0 ? state.request.selected_sheet : 1);
    }
    for (const core::DrawingExportSheetChoice &choice : state.sheet_choices) {
        const bool checked =
            std::find(checked_sheets.begin(), checked_sheets.end(), choice.sheet) != checked_sheets.end();
        char item_name[32] = {0};
        std::snprintf(item_name, sizeof(item_name), "sheet_%d", choice.sheet);
        ProUIListStateSet(
            dialog,
            const_cast<char *>(config.sheet_list_comp),
            item_name,
            checked ? PROUI_SET : PROUI_UNSET);
    }
}

void ReadSheetListState(char *dialog,
                        const DrawingExportDialogConfig &config,
                        DrawingExportDialogState &state)
{
    state.request.selected_sheets.clear();
    if (dialog == nullptr) {
        return;
    }

    for (const core::DrawingExportSheetChoice &choice : state.sheet_choices) {
        char item_name[32] = {0};
        std::snprintf(item_name, sizeof(item_name), "sheet_%d", choice.sheet);
        ProUIMixedState item_state = PROUI_UNSET;
        if (ProUIListStateGet(
                dialog,
                const_cast<char *>(config.sheet_list_comp),
                item_name,
                &item_state) == PRO_TK_NO_ERROR &&
            item_state == PROUI_SET) {
            state.request.selected_sheets.push_back(choice.sheet);
        }
    }

    if (state.request.selected_sheets.empty()) {
        const int fallback = state.request.selected_sheet > 0 ? state.request.selected_sheet : 1;
        state.request.selected_sheets.push_back(fallback);
    }
    state.request.selected_sheet = state.request.selected_sheets.front();
}

void SetDialogChineseText(char *dialog, const DrawingExportDialogConfig &config)
{
    if (dialog == nullptr) {
        return;
    }

    ProUILabelTextSet(
        dialog,
        const_cast<char *>(config.prompt_label_comp),
        const_cast<wchar_t *>(L"\u9009\u62e9\u5f53\u524d\u5de5\u7a0b\u56fe\u5bfc\u51fa\u683c\u5f0f:"));
    ProUILabelTextSet(
        dialog,
        const_cast<char *>(config.dwg_mode_label_comp),
        const_cast<wchar_t *>(L"DWG \u591a\u9875\u8f93\u51fa\u65b9\u5f0f:"));
    ProUILabelTextSet(
        dialog,
        const_cast<char *>(config.sheet_label_comp),
        const_cast<wchar_t *>(L"\u9009\u62e9 DWG \u5bfc\u51fa\u9875\u9762\uff08\u53ef\u591a\u9009\uff09:"));
    ProUILabelTextSet(
        dialog,
        const_cast<char *>(config.output_label_comp),
        const_cast<wchar_t *>(
            L"\u8f93\u51fa\u5230\u5f53\u524d\u5de5\u4f5c\u76ee\u5f55\u7684 export \u6587\u4ef6\u5939\u3002"));
    ProUIPushbuttonTextSet(
        dialog,
        const_cast<char *>(config.cancel_comp),
        const_cast<wchar_t *>(L"\u53d6\u6d88"));
    ProUIPushbuttonTextSet(
        dialog,
        const_cast<char *>(config.ok_comp),
        const_cast<wchar_t *>(L"\u5bfc\u51fa"));
}

void ShowCreateDialogError()
{
    ShowSimpleMessageDialog(
        PROUIMESSAGE_ERROR,
        L"\u5de5\u7a0b\u56fe\u5bfc\u51fa",
        L"\u5de5\u7a0b\u56fe\u5bfc\u51fa\u7a97\u53e3\u52a0\u8f7d\u5931\u8d25\uff0c\u8bf7\u66f4\u65b0\u63d2\u4ef6\u8d44\u6e90\u6587\u4ef6\u3002");
}

} // namespace

bool PromptDrawingExportOptions(core::DrawingExportRequest &request,
                                const std::vector<core::DrawingExportSheetChoice> &sheet_choices,
                                bool &cancelled,
                                const DrawingExportDialogLogSink &log_sink)
{
    cancelled = false;
    const DrawingExportDialogConfig config = DefaultConfig();

    std::string used_resource;
    const ProError st_create = TryCreateDialog(config, log_sink, used_resource);
    if (st_create != PRO_TK_NO_ERROR) {
        LogLine(log_sink,
                "drawing-export-dialog fail reason=create status=%d",
                static_cast<int>(st_create));
        ShowCreateDialogError();
        return false;
    }
    LogLine(log_sink, "drawing-export-dialog using resource=%s", used_resource.c_str());

    DrawingExportDialogState state = {};
    state.request = request;
    state.sheet_choices = sheet_choices;
    DrawingExportDialogRuntime runtime = {};
    runtime.state = &state;
    runtime.config = &config;
    runtime.log_sink = log_sink;

    char *format_names[] = {
        const_cast<char *>("dwg"),
        const_cast<char *>("pdf"),
        const_cast<char *>("dxf")
    };
    wchar_t *format_labels[] = {
        const_cast<wchar_t *>(L"DWG"),
        const_cast<wchar_t *>(L"PDF"),
        const_cast<wchar_t *>(L"DXF")
    };
    char *dwg_mode_names[] = {
        const_cast<char *>("multi_layout"),
        const_cast<char *>("per_sheet")
    };
    wchar_t *dwg_mode_labels[] = {
        const_cast<wchar_t *>(L"\u4e00\u4e2a\u591a\u5e03\u5c40 DWG"),
        const_cast<wchar_t *>(L"\u6bcf\u4e2a\u9875\u9762\u5355\u72ec DWG")
    };

    const char *selected_format = "pdf";
    if (request.format == core::DrawingExportFormat::Dwg) {
        selected_format = "dwg";
    } else if (request.format == core::DrawingExportFormat::Dxf) {
        selected_format = "dxf";
    }
    const char *selected_dwg_mode =
        request.dwg_mode == core::DwgExportMode::PerSheetFiles ? "per_sheet" : "multi_layout";

    SetRadiogroup(
        const_cast<char *>(config.dialog_inst_name),
        config.format_group_comp,
        3,
        format_names,
        format_labels,
        selected_format);
    SetRadiogroup(
        const_cast<char *>(config.dialog_inst_name),
        config.dwg_mode_group_comp,
        2,
        dwg_mode_names,
        dwg_mode_labels,
        selected_dwg_mode);
    SetSheetList(const_cast<char *>(config.dialog_inst_name), config, state);
    SetDialogChineseText(const_cast<char *>(config.dialog_inst_name), config);

    ProUIRadiogroupSelectActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.format_group_comp),
        OnFormatChanged,
        &runtime);
    ProUIRadiogroupSelectActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.dwg_mode_group_comp),
        OnDwgModeChanged,
        &runtime);
    ProUIListSelectActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.sheet_list_comp),
        OnSheetSelect,
        &runtime);
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.ok_comp),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_ok)));
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.cancel_comp),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_cancel)));
    ProUIDialogCloseActionSet(
        const_cast<char *>(config.dialog_inst_name),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_cancel)));
    ProUIDialogDefaultbuttonSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.ok_comp));

    UpdateDwgModeAvailability(const_cast<char *>(config.dialog_inst_name), &runtime);

    int dialog_status = config.status_cancel;
    const ProError st_act = ProUIDialogActivate(
        const_cast<char *>(config.dialog_inst_name),
        &dialog_status);
    if (st_act != PRO_TK_NO_ERROR || dialog_status != config.status_ok) {
        cancelled = true;
        ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
        LogLine(log_sink,
                "drawing-export-dialog cancelled-or-failed activate_status=%d dialog_status=%d",
                static_cast<int>(st_act),
                dialog_status);
        return false;
    }

    ReadSheetListState(const_cast<char *>(config.dialog_inst_name), config, state);
    request = state.request;
    ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
    LogLine(log_sink,
            "drawing-export-dialog selected format=%d dwg_mode=%d selected_sheet=%d selected_count=%d",
            static_cast<int>(request.format),
            static_cast<int>(request.dwg_mode),
            request.selected_sheet,
            static_cast<int>(request.selected_sheets.size()));
    return true;
}

} // namespace autobbox::ui
