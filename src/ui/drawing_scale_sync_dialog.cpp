#include "autobbox/ui/drawing_scale_sync_dialog.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/ui/message_dialog.h"

#include <ProUIDialog.h>
#include <ProUIInputpanel.h>
#include <ProUILabel.h>
#include <ProUIPushbutton.h>
#include <ProUIRadiogroup.h>
#include <ProToolkit.h>
#include <ProUtil.h>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cwchar>
#include <string>
#include <vector>

namespace autobbox::ui {

namespace {

struct DrawingScaleSyncDialogConfig {
    const char *dialog_inst_name = "autobbox_drawing_scale_sync_inst";
    const char *resource_base_name = "autobbox_drawing_scale_sync";
    const char *prompt_label_comp = "PromptLabel";
    const char *scope_label_comp = "ScopeLabel";
    const char *scope_group_comp = "ScopeGroup";
    const char *scale_label_comp = "ScaleLabel";
    const char *scale_input_comp = "ScaleInput";
    const char *help_label_comp = "HelpLabel";
    const char *ok_comp = "OKBtn";
    const char *cancel_comp = "CancelBtn";
    int status_ok = 1;
    int status_cancel = 0;
};

void LogLine(const DrawingScaleSyncDialogLogSink &log_sink, const char *fmt, ...)
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

ProError TryCreateDialog(const DrawingScaleSyncDialogConfig &config,
                         const DrawingScaleSyncDialogLogSink &log_sink,
                         std::string &used_resource)
{
    used_resource.clear();
    const std::string base_name = config.resource_base_name;
    const std::vector<std::string> rel_candidates = {
        base_name,
        std::string("resource\\") + base_name,
        std::string("text\\resource\\") + base_name,
        std::string("text\\usascii\\resource\\") + base_name,
        std::string("usascii\\resource\\") + base_name};

    ProError last = PRO_TK_GENERAL_ERROR;
    for (const std::string &res : rel_candidates) {
        last = ProUIDialogCreate(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(res.c_str()));
        LogLine(log_sink,
                "drawing-scale-sync-dialog-create try resource=%s status=%d",
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
                text_root + "\\text\\usascii\\resource\\" + base_name + ".res"};
            for (const std::string &path : abs_candidates) {
                if (!autobbox::common::FileExistsA(path)) {
                    continue;
                }
                last = ProUIDialogCreate(
                    const_cast<char *>(config.dialog_inst_name),
                    const_cast<char *>(path.c_str()));
                LogLine(log_sink,
                        "drawing-scale-sync-dialog-create try resource=%s status=%d",
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

std::wstring Trim(const std::wstring &text)
{
    const wchar_t *ws = L" \t\r\n";
    const size_t first = text.find_first_not_of(ws);
    if (first == std::wstring::npos) {
        return L"";
    }
    const size_t last = text.find_last_not_of(ws);
    return text.substr(first, last - first + 1);
}

bool ParseDoubleStrict(const std::wstring &text, double &value_out)
{
    const std::wstring trimmed = Trim(text);
    if (trimmed.empty()) {
        return false;
    }

    wchar_t *end = nullptr;
    const double value = std::wcstod(trimmed.c_str(), &end);
    if (end == trimmed.c_str()) {
        return false;
    }
    while (end != nullptr && *end == L' ') {
        ++end;
    }
    if (end == nullptr || *end != L'\0' || !std::isfinite(value)) {
        return false;
    }
    value_out = value;
    return true;
}

bool ParseScaleText(const std::wstring &text, double &value_out)
{
    const std::wstring trimmed = Trim(text);
    if (trimmed.empty()) {
        return false;
    }

    const size_t slash = trimmed.find(L'/');
    if (slash == std::wstring::npos) {
        if (!ParseDoubleStrict(trimmed, value_out)) {
            return false;
        }
    } else {
        if (trimmed.find(L'/', slash + 1) != std::wstring::npos) {
            return false;
        }
        double numerator = 0.0;
        double denominator = 0.0;
        if (!ParseDoubleStrict(trimmed.substr(0, slash), numerator) ||
            !ParseDoubleStrict(trimmed.substr(slash + 1), denominator) ||
            denominator == 0.0) {
            return false;
        }
        value_out = numerator / denominator;
    }

    return value_out > 0.0 && std::isfinite(value_out);
}

std::wstring FormatScaleForInput(double value)
{
    wchar_t buffer[64] = {0};
    std::swprintf(buffer, sizeof(buffer) / sizeof(buffer[0]), L"%.6g", value);
    return buffer;
}

void SetRadiogroup(char *dialog,
                   const DrawingScaleSyncDialogConfig &config,
                   autobbox::application::DrawingPageScaleSyncScope scope)
{
    char *scope_names[] = {
        const_cast<char *>("current"),
        const_cast<char *>("all")};
    wchar_t *scope_labels[] = {
        const_cast<wchar_t *>(L"\u5f53\u524d\u9875\u9762"),
        const_cast<wchar_t *>(L"\u6240\u6709\u9875\u9762")};
    ProUIRadiogroupNamesSet(dialog, const_cast<char *>(config.scope_group_comp), 2, scope_names);
    ProUIRadiogroupLabelsSet(dialog, const_cast<char *>(config.scope_group_comp), 2, scope_labels);
    char *selected[] = {
        const_cast<char *>(scope == autobbox::application::DrawingPageScaleSyncScope::AllSheets ? "all" : "current")};
    ProUIRadiogroupSelectednamesSet(dialog, const_cast<char *>(config.scope_group_comp), 1, selected);
}

void SetDialogChineseText(char *dialog, const DrawingScaleSyncDialogConfig &config)
{
    ProUIDialogTitleSet(dialog, const_cast<wchar_t *>(L"\u7edf\u4e00\u6bd4\u4f8b"));
    ProUILabelTextSet(
        dialog,
        const_cast<char *>(config.prompt_label_comp),
        const_cast<wchar_t *>(L"\u5c06\u5de5\u7a0b\u56fe\u6a21\u578b\u89c6\u56fe\u6bd4\u4f8b\u7edf\u4e00\u4e3a\u6307\u5b9a\u9875\u9762\u6bd4\u4f8b"));
    ProUILabelTextSet(
        dialog,
        const_cast<char *>(config.scope_label_comp),
        const_cast<wchar_t *>(L"\u4f5c\u7528\u8303\u56f4:"));
    ProUILabelTextSet(
        dialog,
        const_cast<char *>(config.scale_label_comp),
        const_cast<wchar_t *>(L"\u9875\u9762\u6bd4\u4f8b\u503c:"));
    ProUILabelTextSet(
        dialog,
        const_cast<char *>(config.help_label_comp),
        const_cast<wchar_t *>(L"\u652f\u6301\u8f93\u5165 1/2 \u6216 0.5"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.ok_comp), const_cast<wchar_t *>(L"\u786e\u5b9a"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.cancel_comp), const_cast<wchar_t *>(L"\u53d6\u6d88"));
}

void ReadScope(char *dialog,
               const DrawingScaleSyncDialogConfig &config,
               autobbox::application::DrawingPageScaleSyncOptions &options)
{
    int count = 0;
    char **names = nullptr;
    if (ProUIRadiogroupSelectednamesGet(
            dialog,
            const_cast<char *>(config.scope_group_comp),
            &count,
            &names) == PRO_TK_NO_ERROR &&
        count > 0 &&
        names != nullptr &&
        names[0] != nullptr) {
        const std::string selected(names[0]);
        options.scope = selected == "all"
            ? autobbox::application::DrawingPageScaleSyncScope::AllSheets
            : autobbox::application::DrawingPageScaleSyncScope::CurrentSheet;
    }
    if (names != nullptr) {
        ProStringarrayFree(names, count);
    }
}

bool ReadScale(char *dialog,
               const DrawingScaleSyncDialogConfig &config,
               autobbox::application::DrawingPageScaleSyncOptions &options)
{
    wchar_t *raw = nullptr;
    if (ProUIInputpanelValueGet(dialog, const_cast<char *>(config.scale_input_comp), &raw) != PRO_TK_NO_ERROR ||
        raw == nullptr) {
        return false;
    }

    double value = 0.0;
    if (!ParseScaleText(raw, value)) {
        return false;
    }

    options.target_scale = value;
    return true;
}

} // namespace

bool PromptDrawingScaleSyncOptionsDialog(
    autobbox::application::DrawingPageScaleSyncOptions &options_io,
    bool &cancelled,
    const DrawingScaleSyncDialogLogSink &log_sink)
{
    cancelled = false;
    const DrawingScaleSyncDialogConfig config = {};

    std::string used_resource;
    const ProError st_create = TryCreateDialog(config, log_sink, used_resource);
    if (st_create != PRO_TK_NO_ERROR) {
        LogLine(log_sink,
                "drawing-scale-sync-dialog fail reason=create status=%d",
                static_cast<int>(st_create));
        ShowSimpleMessageDialog(
            PROUIMESSAGE_ERROR,
            L"\u7edf\u4e00\u6bd4\u4f8b",
            L"\u7edf\u4e00\u6bd4\u4f8b\u7a97\u53e3\u52a0\u8f7d\u5931\u8d25\uff0c\u8bf7\u66f4\u65b0\u63d2\u4ef6\u8d44\u6e90\u6587\u4ef6\u3002");
        return false;
    }
    LogLine(log_sink, "drawing-scale-sync-dialog using resource=%s", used_resource.c_str());

    char *dialog = const_cast<char *>(config.dialog_inst_name);
    SetDialogChineseText(dialog, config);
    SetRadiogroup(dialog, config, options_io.scope);

    const std::wstring scale_text = FormatScaleForInput(options_io.target_scale);
    ProUIInputpanelColumnsSet(dialog, const_cast<char *>(config.scale_input_comp), 18);
    ProUIInputpanelValueSet(dialog, const_cast<char *>(config.scale_input_comp), const_cast<wchar_t *>(scale_text.c_str()));

    ProUIPushbuttonActivateActionSet(
        dialog,
        const_cast<char *>(config.ok_comp),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_ok)));
    ProUIPushbuttonActivateActionSet(
        dialog,
        const_cast<char *>(config.cancel_comp),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_cancel)));
    ProUIDialogCloseActionSet(
        dialog,
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_cancel)));
    ProUIDialogDefaultbuttonSet(dialog, const_cast<char *>(config.ok_comp));

    int dialog_status = config.status_cancel;
    const ProError st_activate = ProUIDialogActivate(dialog, &dialog_status);
    if (st_activate != PRO_TK_NO_ERROR || dialog_status != config.status_ok) {
        cancelled = true;
        ProUIDialogDestroy(dialog);
        LogLine(log_sink,
                "drawing-scale-sync-dialog cancelled-or-failed activate_status=%d dialog_status=%d",
                static_cast<int>(st_activate),
                dialog_status);
        return false;
    }

    ReadScope(dialog, config, options_io);
    if (!ReadScale(dialog, config, options_io)) {
        ProUIDialogDestroy(dialog);
        ShowSimpleMessageDialog(
            PROUIMESSAGE_ERROR,
            L"\u7edf\u4e00\u6bd4\u4f8b",
            L"\u9875\u9762\u6bd4\u4f8b\u503c\u65e0\u6548\uff0c\u8bf7\u8f93\u5165\u6b63\u6570\uff0c\u4f8b\u5982 1/2 \u6216 0.5\u3002");
        LogLine(log_sink, "drawing-scale-sync-dialog fail reason=invalid-scale-input");
        return false;
    }

    ProUIDialogDestroy(dialog);
    LogLine(log_sink,
            "drawing-scale-sync-dialog selected scope=%d current_sheet=%d sheet_count=%d target_scale=%.6f",
            static_cast<int>(options_io.scope),
            options_io.current_sheet,
            options_io.sheet_count,
            options_io.target_scale);
    return true;
}

} // namespace autobbox::ui
