#include "autobbox/ui/batch_rename_dialog.h"

#include "autobbox/application/batch_rename.h"
#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/ui/message_dialog.h"

#include <ProArray.h>
#include <ProToolkit.h>
#include <ProUI.h>
#include <ProUIDialog.h>
#include <ProUICheckbutton.h>
#include <ProUIInputpanel.h>
#include <ProUILabel.h>
#include <ProUIOptionmenu.h>
#include <ProUIPushbutton.h>
#include <ProUITable.h>
#include <ProUIMessage.h>
#include <ProUtil.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cstdarg>
#include <cstdlib>
#include <cwctype>
#include <cstdio>
#include <cstdint>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "User32.lib")

namespace autobbox::ui {

namespace {

struct BatchRenameDialogConfig {
    const char *dialog_inst_name = nullptr;
    const char *resource_base_name = nullptr;
    const char *summary_comp = nullptr;
    const char *table_comp = nullptr;
    const char *select_base_comp = nullptr;
    const char *cell_input_base_comp = nullptr;
    const char *paste_comp = nullptr;
    const char *clear_comp = nullptr;
    const char *replace_comp = nullptr;
    const char *sequence_comp = nullptr;
    const char *validate_comp = nullptr;
    const char *reset_comp = nullptr;
    const char *refresh_comp = nullptr;
    const char *apply_comp = nullptr;
    const char *close_comp = nullptr;
    int status_close = 0;
};

BatchRenameDialogConfig DefaultBatchRenameDialogConfig()
{
    BatchRenameDialogConfig config = {};
    config.dialog_inst_name = "autobbox_batch_rename_inst";
    config.resource_base_name = "autobbox_batch_rename";
    config.summary_comp = "SummaryLabel";
    config.table_comp = "RenameTable";
    config.select_base_comp = "SelectBase";
    config.cell_input_base_comp = "CellInputBase";
    config.paste_comp = "PasteBtn";
    config.clear_comp = "ClearBtn";
    config.replace_comp = "ReplaceBtn";
    config.sequence_comp = "SequenceBtn";
    config.validate_comp = "ValidateBtn";
    config.reset_comp = "ResetBtn";
    config.refresh_comp = "RefreshBtn";
    config.apply_comp = "ApplyBtn";
    config.close_comp = "CloseBtn";
    return config;
}

struct BatchRenameDialogState {
    std::vector<core::BatchRenameCandidate> *candidates = nullptr;
    std::unordered_map<std::string, size_t> row_index_by_name;
    std::unordered_map<std::string, size_t> checkbox_index_by_name;
    std::string active_row_name;
    std::string active_column_name;
    std::string active_component_name;
    core::BatchRenameClearSpec last_clear_spec;
    core::BatchRenameReplaceSpec last_replace_spec;
    core::BatchRenameSequenceSpec last_sequence_spec;
    int editor_serial = 0;
    int selection_anchor_index = -1;
    bool dirty = false;
};

struct BatchRenameDialogRuntime {
    BatchRenameDialogState *state = nullptr;
    const BatchRenameDialogConfig *config = nullptr;
    const BatchRenameDialogCallbacks *callbacks = nullptr;
};

void LogLine(const BatchRenameDialogLogSink &log_sink, const char *fmt, ...)
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

ProError TryCreateDialogByBase(const char *dialog_inst_name,
                               const char *resource_base_name,
                               const BatchRenameDialogLogSink &log_sink,
                               std::string &used_resource)
{
    used_resource.clear();
    if (dialog_inst_name == nullptr || resource_base_name == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    const std::string base_name = resource_base_name;
    const std::vector<std::string> rel_candidates = {
        base_name,
        base_name + ".res",
        std::string("resource\\") + base_name,
        std::string("resource\\") + base_name + ".res",
        std::string("text\\resource\\") + base_name,
        std::string("text\\resource\\") + base_name + ".res",
    };

    ProError last = PRO_TK_GENERAL_ERROR;
    for (const std::string &resource : rel_candidates) {
        last = ProUIDialogCreate(const_cast<char *>(dialog_inst_name), const_cast<char *>(resource.c_str()));
        LogLine(log_sink, "batch-rename-dialog create try resource=%s status=%d", resource.c_str(), static_cast<int>(last));
        if (last == PRO_TK_NO_ERROR) {
            used_resource = resource;
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
        };
        for (const std::string &path : abs_candidates) {
            if (!autobbox::common::FileExistsA(path)) {
                continue;
            }
            last = ProUIDialogCreate(const_cast<char *>(dialog_inst_name), const_cast<char *>(path.c_str()));
            LogLine(log_sink, "batch-rename-dialog create try resource=%s status=%d", path.c_str(), static_cast<int>(last));
            if (last == PRO_TK_NO_ERROR) {
                used_resource = path;
                return last;
            }
        }
    }
    return last;
}

ProError TryCreateDialog(const BatchRenameDialogConfig &config,
                         const BatchRenameDialogLogSink &log_sink,
                         std::string &used_resource)
{
    return TryCreateDialogByBase(
        config.dialog_inst_name,
        config.resource_base_name,
        log_sink,
        used_resource);
}

bool ShowYesNoDialog(const wchar_t *title, const wchar_t *message)
{
    ProUIMessageButton choice = PRO_UI_MESSAGE_NO;
    ProUIMessageButton *buttons = nullptr;
    if (ProArrayAlloc(0, sizeof(ProUIMessageButton), 1, reinterpret_cast<ProArray *>(&buttons)) != PRO_TK_NO_ERROR ||
        buttons == nullptr) {
        return false;
    }
    ProUIMessageButton yes_button = PRO_UI_MESSAGE_YES;
    ProUIMessageButton no_button = PRO_UI_MESSAGE_NO;
    ProArrayObjectAdd(reinterpret_cast<ProArray *>(&buttons), PRO_VALUE_UNUSED, 1, &yes_button);
    ProArrayObjectAdd(reinterpret_cast<ProArray *>(&buttons), PRO_VALUE_UNUSED, 1, &no_button);
    ProUIMessageDialogDisplay(
        PROUIMESSAGE_QUESTION,
        const_cast<wchar_t *>(title == nullptr ? L"AutoBBox" : title),
        const_cast<wchar_t *>(message == nullptr ? L"" : message),
        buttons,
        PRO_UI_MESSAGE_NO,
        &choice);
    ProArrayFree(reinterpret_cast<ProArray *>(&buttons));
    return choice == PRO_UI_MESSAGE_YES || choice == PRO_UI_MESSAGE_CONFIRM;
}

void OnSimpleDialogOk(char *dialog, char *, ProAppData)
{
    if (dialog != nullptr) {
        ProUIDialogExit(dialog, 1);
    }
}

void OnSimpleDialogCancel(char *dialog, char *, ProAppData)
{
    if (dialog != nullptr) {
        ProUIDialogExit(dialog, 0);
    }
}

std::wstring TrimText(const std::wstring &value)
{
    size_t begin = 0;
    while (begin < value.size() && std::iswspace(value[begin]) != 0) {
        ++begin;
    }
    size_t end = value.size();
    while (end > begin && std::iswspace(value[end - 1]) != 0) {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool ParseIntValue(const std::wstring &text, int &value_out)
{
    try {
        const std::wstring trimmed = TrimText(text);
        if (trimmed.empty()) {
            return false;
        }
        size_t parsed = 0;
        const int value = std::stoi(trimmed, &parsed, 10);
        if (parsed != trimmed.size()) {
            return false;
        }
        value_out = value;
        return true;
    } catch (...) {
        return false;
    }
}

void SetOptionMenuItems(char *dialog,
                        const char *component,
                        const std::vector<std::string> &names_storage,
                        const std::vector<std::wstring> &labels_storage,
                        int columns)
{
    std::vector<char *> names;
    std::vector<wchar_t *> labels;
    names.reserve(names_storage.size());
    labels.reserve(labels_storage.size());
    for (const std::string &name : names_storage) {
        names.push_back(const_cast<char *>(name.c_str()));
    }
    for (const std::wstring &label : labels_storage) {
        labels.push_back(const_cast<wchar_t *>(label.c_str()));
    }
    ProUIOptionmenuNamesSet(dialog, const_cast<char *>(component), static_cast<int>(names.size()), names.data());
    ProUIOptionmenuLabelsSet(dialog, const_cast<char *>(component), static_cast<int>(labels.size()), labels.data());
    ProUIOptionmenuColumnsSet(dialog, const_cast<char *>(component), columns);
    ProUIOptionmenuVisiblerowsSet(dialog, const_cast<char *>(component), static_cast<int>(labels.size()));
}

std::wstring GetOptionMenuValue(char *dialog, const char *component)
{
    wchar_t *value = nullptr;
    if (ProUIOptionmenuValueGet(dialog, const_cast<char *>(component), &value) != PRO_TK_NO_ERROR ||
        value == nullptr) {
        if (value != nullptr) {
            ProWstringFree(value);
        }
        return std::wstring();
    }
    const std::wstring out(value);
    ProWstringFree(value);
    return out;
}

std::wstring EditableColumnLabel(core::BatchRenameEditableColumn column)
{
    return column == core::BatchRenameEditableColumn::CommonName
               ? L"\u0050\u0054\u0043\u005f\u0043\u004f\u004d\u004d\u004f\u004e\u005f\u004e\u0041\u004d\u0045"
               : L"\u65b0\u6a21\u578b\u540d\u79f0";
}

std::wstring ReplaceModeLabel(core::BatchRenameReplaceMode mode)
{
    return mode == core::BatchRenameReplaceMode::Template
               ? L"\u6a21\u677f\u66ff\u6362"
               : L"\u666e\u901a\u66ff\u6362";
}

std::wstring CaseModeLabel(bool case_sensitive)
{
    return case_sensitive
               ? L"\u533a\u5206\u5927\u5c0f\u5199"
               : L"\u4e0d\u533a\u5206\u5927\u5c0f\u5199";
}

std::wstring BatchRenameTemplateHelpText()
{
    return L"\u53ef\u7528\uff1a{model}\u539f\u6a21\u578b\u540d\uff0c{name}\u5f53\u524d\u5217\u5185\u5bb9\uff0c{target}\u76ee\u6807\u5217\uff0c"
           L"{common} PTC_COMMON_NAME\uff0c{num}\u5e8f\u53f7\uff0c{row}\u884c\u53f7\uff0c{match}\u5339\u914d\u6587\u672c\uff1b"
           L"\u6a21\u578b\u53c2\u6570\u76f4\u63a5\u5199 {\u53c2\u6570\u540d}\uff0c\u5982 {\u56fe\u53f7}\u3002";
}

std::wstring TemplatePresetLabel(const std::wstring &preset)
{
    if (preset == L"DRAWING_NO" || preset == L"\u6a21\u578b\u53c2\u6570\uff1a{\u56fe\u53f7}") {
        return L"\u6a21\u578b\u53c2\u6570\uff1a{\u56fe\u53f7}";
    }
    if (preset == L"DRAWING_NO_MODEL" || preset == L"{\u56fe\u53f7}_{model}") {
        return L"{\u56fe\u53f7}_{model}";
    }
    if (preset == L"MODEL_COMMON" || preset == L"{model}_{common}") {
        return L"{model}_{common}";
    }
    if (preset == L"NAME_NUM" || preset == L"{name}_{num}") {
        return L"{name}_{num}";
    }
    if (preset == L"DRAWING_NO_NUM" || preset == L"{\u56fe\u53f7}_{num}") {
        return L"{\u56fe\u53f7}_{num}";
    }
    return L"\u624b\u52a8\u8f93\u5165";
}

std::wstring TemplatePresetValue(const std::wstring &preset)
{
    if (preset == L"DRAWING_NO" || preset == L"\u6a21\u578b\u53c2\u6570\uff1a{\u56fe\u53f7}") {
        return L"{\u56fe\u53f7}";
    }
    if (preset == L"DRAWING_NO_MODEL" || preset == L"{\u56fe\u53f7}_{model}") {
        return L"{\u56fe\u53f7}_{model}";
    }
    if (preset == L"MODEL_COMMON" || preset == L"{model}_{common}") {
        return L"{model}_{common}";
    }
    if (preset == L"NAME_NUM" || preset == L"{name}_{num}") {
        return L"{name}_{num}";
    }
    if (preset == L"DRAWING_NO_NUM" || preset == L"{\u56fe\u53f7}_{num}") {
        return L"{\u56fe\u53f7}_{num}";
    }
    return std::wstring();
}

core::BatchRenameEditableColumn ParseEditableColumn(const std::wstring &value)
{
    return value == L"COMMON" ||
                   value == L"\u0050\u0054\u0043\u005f\u0043\u004f\u004d\u004d\u004f\u004e\u005f\u004e\u0041\u004d\u0045"
               ? core::BatchRenameEditableColumn::CommonName
               : core::BatchRenameEditableColumn::NewModelName;
}

struct ClearDialogConfig {
    const char *dialog_inst_name = "autobbox_batch_rename_clear_inst";
    const char *resource_base_name = "autobbox_batch_rename_clear";
    const char *prompt_comp = "PromptLabel";
    const char *target_label_comp = "TargetLabel";
    const char *target_comp = "TargetMenu";
    const char *ok_comp = "OKBtn";
    const char *cancel_comp = "CancelBtn";
    int status_ok = 1;
};

bool PromptClearDialog(core::BatchRenameClearSpec &spec_io,
                       bool &cancelled,
                       const BatchRenameDialogLogSink &log_sink,
                       std::wstring &error_out)
{
    cancelled = false;
    error_out.clear();
    const ClearDialogConfig config = {};

    std::string used_resource;
    const ProError status = TryCreateDialogByBase(
        config.dialog_inst_name,
        config.resource_base_name,
        log_sink,
        used_resource);
    if (status != PRO_TK_NO_ERROR) {
        error_out = L"\u65e0\u6cd5\u6253\u5f00\u6e05\u7a7a\u5bf9\u8bdd\u6846\uff0cCreo \u72b6\u6001\uff1a" +
                    std::to_wstring(static_cast<int>(status));
        return false;
    }

    char *dialog = const_cast<char *>(config.dialog_inst_name);
    ProUIDialogTitleSet(dialog, const_cast<wchar_t *>(L"\u6279\u91cf\u91cd\u547d\u540d"));
    ProUILabelTextSet(dialog, const_cast<char *>(config.prompt_comp), const_cast<wchar_t *>(L"\u9009\u62e9\u8981\u6e05\u7a7a\u7684\u5217"));
    ProUILabelTextSet(dialog, const_cast<char *>(config.target_label_comp), const_cast<wchar_t *>(L"\u76ee\u6807\u5217"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.ok_comp), const_cast<wchar_t *>(L"\u786e\u5b9a"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.cancel_comp), const_cast<wchar_t *>(L"\u53d6\u6d88"));

    SetOptionMenuItems(dialog,
                       config.target_comp,
                       {"NEWNAME", "COMMON", "BOTH"},
                       {L"\u65b0\u6a21\u578b\u540d\u79f0", L"PTC_COMMON_NAME", L"\u4e24\u5217"},
                       18);

    std::wstring target = L"\u65b0\u6a21\u578b\u540d\u79f0";
    if (spec_io.clear_new_model_name && spec_io.clear_common_name) {
        target = L"\u4e24\u5217";
    } else if (spec_io.clear_common_name) {
        target = L"\u0050\u0054\u0043\u005f\u0043\u004f\u004d\u004d\u004f\u004e\u005f\u004e\u0041\u004d\u0045";
    }
    ProUIOptionmenuValueSet(dialog, const_cast<char *>(config.target_comp), const_cast<wchar_t *>(target.c_str()));
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.ok_comp), OnSimpleDialogOk, nullptr);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.cancel_comp), OnSimpleDialogCancel, nullptr);
    ProUIDialogCloseActionSet(dialog, OnSimpleDialogCancel, nullptr);
    ProUIDialogDefaultbuttonSet(dialog, const_cast<char *>(config.ok_comp));

    int dialog_status = 0;
    const ProError activate_status = ProUIDialogActivate(dialog, &dialog_status);
    if (activate_status != PRO_TK_NO_ERROR || dialog_status != config.status_ok) {
        cancelled = true;
        ProUIDialogDestroy(dialog);
        return false;
    }

    const std::wstring target_value = GetOptionMenuValue(dialog, config.target_comp);
    ProUIDialogDestroy(dialog);

    spec_io.clear_new_model_name = (target_value == L"NEWNAME" ||
                                    target_value == L"\u65b0\u6a21\u578b\u540d\u79f0" ||
                                    target_value == L"BOTH" ||
                                    target_value == L"\u4e24\u5217");
    spec_io.clear_common_name = (target_value == L"COMMON" ||
                                 target_value == L"\u0050\u0054\u0043\u005f\u0043\u004f\u004d\u004d\u004f\u004e\u005f\u004e\u0041\u004d\u0045" ||
                                 target_value == L"BOTH" ||
                                 target_value == L"\u4e24\u5217");
    if (!spec_io.clear_new_model_name && !spec_io.clear_common_name) {
        error_out = L"\u8bf7\u9009\u62e9\u8981\u6e05\u7a7a\u7684\u5217\u3002";
        return false;
    }
    return true;
}

struct ReplaceDialogConfig {
    const char *dialog_inst_name = "autobbox_batch_rename_replace_inst";
    const char *resource_base_name = "autobbox_batch_rename_replace";
    const char *prompt_comp = "PromptLabel";
    const char *target_label_comp = "TargetLabel";
    const char *target_comp = "TargetMenu";
    const char *mode_label_comp = "ModeLabel";
    const char *mode_comp = "ModeMenu";
    const char *case_label_comp = "CaseLabel";
    const char *case_comp = "CaseMenu";
    const char *find_label_comp = "FindLabel";
    const char *find_comp = "FindInput";
    const char *preset_label_comp = "PresetLabel";
    const char *preset_comp = "PresetMenu";
    const char *replace_label_comp = "ReplaceLabel";
    const char *replace_comp = "ReplaceInput";
    const char *help_comp = "HelpLabel";
    const char *ok_comp = "OKBtn";
    const char *cancel_comp = "CancelBtn";
    int status_ok = 1;
};

bool PromptReplaceDialog(core::BatchRenameReplaceSpec &spec_io,
                         bool &cancelled,
                         const BatchRenameDialogLogSink &log_sink,
                         std::wstring &error_out)
{
    cancelled = false;
    error_out.clear();
    const ReplaceDialogConfig config = {};

    std::string used_resource;
    const ProError status = TryCreateDialogByBase(
        config.dialog_inst_name,
        config.resource_base_name,
        log_sink,
        used_resource);
    if (status != PRO_TK_NO_ERROR) {
        error_out = L"\u65e0\u6cd5\u6253\u5f00\u6279\u91cf\u66ff\u6362\u5bf9\u8bdd\u6846\uff0cCreo \u72b6\u6001\uff1a" +
                    std::to_wstring(static_cast<int>(status));
        return false;
    }

    char *dialog = const_cast<char *>(config.dialog_inst_name);
    ProUIDialogTitleSet(dialog, const_cast<wchar_t *>(L"\u6279\u91cf\u91cd\u547d\u540d"));
    ProUILabelTextSet(dialog, const_cast<char *>(config.prompt_comp), const_cast<wchar_t *>(L"\u6279\u91cf\u66ff\u6362\u6587\u672c\u6216\u6309\u6a21\u677f\u66ff\u6362\uff1b\u6a21\u578b\u53c2\u6570\u76f4\u63a5\u7528 {\u53c2\u6570\u540d}\uff0c\u5982 {\u56fe\u53f7}"));
    ProUILabelTextSet(dialog, const_cast<char *>(config.target_label_comp), const_cast<wchar_t *>(L"\u76ee\u6807\u5217"));
    ProUILabelTextSet(dialog, const_cast<char *>(config.mode_label_comp), const_cast<wchar_t *>(L"\u66ff\u6362\u6a21\u5f0f"));
    ProUILabelTextSet(dialog, const_cast<char *>(config.case_label_comp), const_cast<wchar_t *>(L"\u5339\u914d\u65b9\u5f0f"));
    ProUILabelTextSet(dialog, const_cast<char *>(config.find_label_comp), const_cast<wchar_t *>(L"\u67e5\u627e\u6587\u672c"));
    ProUILabelTextSet(dialog, const_cast<char *>(config.preset_label_comp), const_cast<wchar_t *>(L"\u6a21\u677f\u9009\u9879"));
    ProUILabelTextSet(dialog, const_cast<char *>(config.replace_label_comp), const_cast<wchar_t *>(L"\u66ff\u6362\u4e3a/\u6a21\u677f"));
    const std::wstring help_text = BatchRenameTemplateHelpText();
    ProUILabelTextSet(dialog, const_cast<char *>(config.help_comp), const_cast<wchar_t *>(help_text.c_str()));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.ok_comp), const_cast<wchar_t *>(L"\u786e\u5b9a"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.cancel_comp), const_cast<wchar_t *>(L"\u53d6\u6d88"));

    SetOptionMenuItems(dialog,
                       config.target_comp,
                       {"NEWNAME", "COMMON"},
                       {L"\u65b0\u6a21\u578b\u540d\u79f0", L"PTC_COMMON_NAME"},
                       20);
    SetOptionMenuItems(dialog,
                       config.mode_comp,
                       {"PLAIN", "TEMPLATE"},
                       {L"\u666e\u901a\u66ff\u6362", L"\u6a21\u677f\u66ff\u6362"},
                       16);
    SetOptionMenuItems(dialog,
                       config.case_comp,
                       {"INSENSITIVE", "SENSITIVE"},
                       {L"\u4e0d\u533a\u5206\u5927\u5c0f\u5199", L"\u533a\u5206\u5927\u5c0f\u5199"},
                       18);
    SetOptionMenuItems(dialog,
                       config.preset_comp,
                       {"CUSTOM", "DRAWING_NO", "DRAWING_NO_MODEL", "MODEL_COMMON", "NAME_NUM"},
                       {L"\u624b\u52a8\u8f93\u5165",
                        L"\u6a21\u578b\u53c2\u6570\uff1a{\u56fe\u53f7}",
                        L"{\u56fe\u53f7}_{model}",
                        L"{model}_{common}",
                        L"{name}_{num}"},
                       24);

    const std::wstring target_label = EditableColumnLabel(spec_io.target_column);
    const std::wstring mode_label = ReplaceModeLabel(spec_io.mode);
    const std::wstring case_label = CaseModeLabel(spec_io.case_sensitive);
    const std::wstring custom_preset_label = TemplatePresetLabel(L"CUSTOM");
    ProUIOptionmenuValueSet(dialog, const_cast<char *>(config.target_comp), const_cast<wchar_t *>(target_label.c_str()));
    ProUIOptionmenuValueSet(dialog, const_cast<char *>(config.mode_comp), const_cast<wchar_t *>(mode_label.c_str()));
    ProUIOptionmenuValueSet(dialog, const_cast<char *>(config.case_comp), const_cast<wchar_t *>(case_label.c_str()));
    ProUIOptionmenuValueSet(dialog, const_cast<char *>(config.preset_comp), const_cast<wchar_t *>(custom_preset_label.c_str()));
    ProUIInputpanelValueSet(dialog, const_cast<char *>(config.find_comp), const_cast<wchar_t *>(spec_io.find_text.c_str()));
    ProUIInputpanelValueSet(dialog, const_cast<char *>(config.replace_comp), const_cast<wchar_t *>(spec_io.replace_text.c_str()));
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.ok_comp), OnSimpleDialogOk, nullptr);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.cancel_comp), OnSimpleDialogCancel, nullptr);
    ProUIDialogCloseActionSet(dialog, OnSimpleDialogCancel, nullptr);
    ProUIDialogDefaultbuttonSet(dialog, const_cast<char *>(config.ok_comp));

    int dialog_status = 0;
    const ProError activate_status = ProUIDialogActivate(dialog, &dialog_status);
    if (activate_status != PRO_TK_NO_ERROR || dialog_status != config.status_ok) {
        cancelled = true;
        ProUIDialogDestroy(dialog);
        return false;
    }

    wchar_t *find_value = nullptr;
    wchar_t *replace_value = nullptr;
    if (ProUIInputpanelValueGet(dialog, const_cast<char *>(config.find_comp), &find_value) != PRO_TK_NO_ERROR ||
        ProUIInputpanelValueGet(dialog, const_cast<char *>(config.replace_comp), &replace_value) != PRO_TK_NO_ERROR ||
        find_value == nullptr || replace_value == nullptr) {
        if (find_value != nullptr) {
            ProWstringFree(find_value);
        }
        if (replace_value != nullptr) {
            ProWstringFree(replace_value);
        }
        ProUIDialogDestroy(dialog);
        error_out = L"\u65e0\u6cd5\u8bfb\u53d6\u6279\u91cf\u66ff\u6362\u53c2\u6570\u3002";
        return false;
    }

    spec_io.target_column = ParseEditableColumn(GetOptionMenuValue(dialog, config.target_comp));
    spec_io.mode = (GetOptionMenuValue(dialog, config.mode_comp) == L"TEMPLATE" ||
                    GetOptionMenuValue(dialog, config.mode_comp) == L"\u6a21\u677f\u66ff\u6362")
                       ? core::BatchRenameReplaceMode::Template
                       : core::BatchRenameReplaceMode::PlainText;
    spec_io.case_sensitive = (GetOptionMenuValue(dialog, config.case_comp) == L"SENSITIVE" ||
                              GetOptionMenuValue(dialog, config.case_comp) == L"\u533a\u5206\u5927\u5c0f\u5199");
    spec_io.find_text.assign(find_value);
    spec_io.replace_text.assign(replace_value);
    const std::wstring preset_template = TemplatePresetValue(GetOptionMenuValue(dialog, config.preset_comp));
    if (!preset_template.empty()) {
        spec_io.mode = core::BatchRenameReplaceMode::Template;
        spec_io.replace_text = preset_template;
    }
    ProWstringFree(find_value);
    ProWstringFree(replace_value);
    ProUIDialogDestroy(dialog);
    return true;
}

struct SequenceDialogConfig {
    const char *dialog_inst_name = "autobbox_batch_rename_sequence_inst";
    const char *resource_base_name = "autobbox_batch_rename_sequence";
    const char *prompt_comp = "PromptLabel";
    const char *target_label_comp = "TargetLabel";
    const char *target_comp = "TargetMenu";
    const char *preset_label_comp = "PresetLabel";
    const char *preset_comp = "PresetMenu";
    const char *template_label_comp = "TemplateLabel";
    const char *template_comp = "TemplateInput";
    const char *help_comp = "HelpLabel";
    const char *start_label_comp = "StartLabel";
    const char *start_comp = "StartInput";
    const char *step_label_comp = "StepLabel";
    const char *step_comp = "StepInput";
    const char *width_label_comp = "WidthLabel";
    const char *width_comp = "WidthInput";
    const char *ok_comp = "OKBtn";
    const char *cancel_comp = "CancelBtn";
    int status_ok = 1;
};

bool PromptSequenceDialog(core::BatchRenameSequenceSpec &spec_io,
                          bool &cancelled,
                          const BatchRenameDialogLogSink &log_sink,
                          std::wstring &error_out)
{
    cancelled = false;
    error_out.clear();
    const SequenceDialogConfig config = {};

    std::string used_resource;
    const ProError status = TryCreateDialogByBase(
        config.dialog_inst_name,
        config.resource_base_name,
        log_sink,
        used_resource);
    if (status != PRO_TK_NO_ERROR) {
        error_out = L"\u65e0\u6cd5\u6253\u5f00\u81ea\u52a8\u5e8f\u53f7\u5bf9\u8bdd\u6846\uff0cCreo \u72b6\u6001\uff1a" +
                    std::to_wstring(static_cast<int>(status));
        return false;
    }

    char *dialog = const_cast<char *>(config.dialog_inst_name);
    ProUIDialogTitleSet(dialog, const_cast<wchar_t *>(L"\u6279\u91cf\u91cd\u547d\u540d"));
    ProUILabelTextSet(dialog, const_cast<char *>(config.prompt_comp), const_cast<wchar_t *>(L"\u6309\u6a21\u677f\u4e3a\u5217\u5185\u5bb9\u6279\u91cf\u6dfb\u52a0\u5e8f\u53f7\uff1b\u6a21\u578b\u53c2\u6570\u76f4\u63a5\u7528 {\u53c2\u6570\u540d}\uff0c\u5982 {\u56fe\u53f7}"));
    ProUILabelTextSet(dialog, const_cast<char *>(config.target_label_comp), const_cast<wchar_t *>(L"\u76ee\u6807\u5217"));
    ProUILabelTextSet(dialog, const_cast<char *>(config.preset_label_comp), const_cast<wchar_t *>(L"\u6a21\u677f\u9009\u9879"));
    ProUILabelTextSet(dialog, const_cast<char *>(config.template_label_comp), const_cast<wchar_t *>(L"\u6a21\u677f"));
    const std::wstring help_text = BatchRenameTemplateHelpText();
    ProUILabelTextSet(dialog, const_cast<char *>(config.help_comp), const_cast<wchar_t *>(help_text.c_str()));
    ProUILabelTextSet(dialog, const_cast<char *>(config.start_label_comp), const_cast<wchar_t *>(L"\u8d77\u59cb\u503c"));
    ProUILabelTextSet(dialog, const_cast<char *>(config.step_label_comp), const_cast<wchar_t *>(L"\u6b65\u957f"));
    ProUILabelTextSet(dialog, const_cast<char *>(config.width_label_comp), const_cast<wchar_t *>(L"\u4f4d\u6570"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.ok_comp), const_cast<wchar_t *>(L"\u786e\u5b9a"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.cancel_comp), const_cast<wchar_t *>(L"\u53d6\u6d88"));

    SetOptionMenuItems(dialog,
                       config.target_comp,
                       {"NEWNAME", "COMMON"},
                       {L"\u65b0\u6a21\u578b\u540d\u79f0", L"PTC_COMMON_NAME"},
                       20);
    SetOptionMenuItems(dialog,
                       config.preset_comp,
                       {"CUSTOM", "NAME_NUM", "DRAWING_NO_NUM", "DRAWING_NO_MODEL", "MODEL_COMMON"},
                       {L"\u624b\u52a8\u8f93\u5165",
                        L"{name}_{num}",
                        L"{\u56fe\u53f7}_{num}",
                        L"{\u56fe\u53f7}_{model}",
                        L"{model}_{common}"},
                       24);

    const std::wstring sequence_target_label = EditableColumnLabel(spec_io.target_column);
    ProUIOptionmenuValueSet(dialog,
                            const_cast<char *>(config.target_comp),
                            const_cast<wchar_t *>(sequence_target_label.c_str()));
    const std::wstring custom_preset_label = TemplatePresetLabel(L"CUSTOM");
    ProUIOptionmenuValueSet(dialog, const_cast<char *>(config.preset_comp), const_cast<wchar_t *>(custom_preset_label.c_str()));
    ProUIInputpanelValueSet(dialog, const_cast<char *>(config.template_comp), const_cast<wchar_t *>(spec_io.template_text.c_str()));
    const std::wstring start_text = std::to_wstring(spec_io.start);
    const std::wstring step_text = std::to_wstring(spec_io.step);
    const std::wstring width_text = std::to_wstring(spec_io.width);
    ProUIInputpanelValueSet(dialog, const_cast<char *>(config.start_comp), const_cast<wchar_t *>(start_text.c_str()));
    ProUIInputpanelValueSet(dialog, const_cast<char *>(config.step_comp), const_cast<wchar_t *>(step_text.c_str()));
    ProUIInputpanelValueSet(dialog, const_cast<char *>(config.width_comp), const_cast<wchar_t *>(width_text.c_str()));
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.ok_comp), OnSimpleDialogOk, nullptr);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.cancel_comp), OnSimpleDialogCancel, nullptr);
    ProUIDialogCloseActionSet(dialog, OnSimpleDialogCancel, nullptr);
    ProUIDialogDefaultbuttonSet(dialog, const_cast<char *>(config.ok_comp));

    int dialog_status = 0;
    const ProError activate_status = ProUIDialogActivate(dialog, &dialog_status);
    if (activate_status != PRO_TK_NO_ERROR || dialog_status != config.status_ok) {
        cancelled = true;
        ProUIDialogDestroy(dialog);
        return false;
    }

    wchar_t *template_value = nullptr;
    wchar_t *start_value = nullptr;
    wchar_t *step_value = nullptr;
    wchar_t *width_value = nullptr;
    if (ProUIInputpanelValueGet(dialog, const_cast<char *>(config.template_comp), &template_value) != PRO_TK_NO_ERROR ||
        ProUIInputpanelValueGet(dialog, const_cast<char *>(config.start_comp), &start_value) != PRO_TK_NO_ERROR ||
        ProUIInputpanelValueGet(dialog, const_cast<char *>(config.step_comp), &step_value) != PRO_TK_NO_ERROR ||
        ProUIInputpanelValueGet(dialog, const_cast<char *>(config.width_comp), &width_value) != PRO_TK_NO_ERROR ||
        template_value == nullptr || start_value == nullptr || step_value == nullptr || width_value == nullptr) {
        if (template_value != nullptr) {
            ProWstringFree(template_value);
        }
        if (start_value != nullptr) {
            ProWstringFree(start_value);
        }
        if (step_value != nullptr) {
            ProWstringFree(step_value);
        }
        if (width_value != nullptr) {
            ProWstringFree(width_value);
        }
        ProUIDialogDestroy(dialog);
        error_out = L"\u65e0\u6cd5\u8bfb\u53d6\u81ea\u52a8\u5e8f\u53f7\u53c2\u6570\u3002";
        return false;
    }

    spec_io.target_column = ParseEditableColumn(GetOptionMenuValue(dialog, config.target_comp));
    spec_io.template_text.assign(template_value);
    const std::wstring preset_template = TemplatePresetValue(GetOptionMenuValue(dialog, config.preset_comp));
    if (!preset_template.empty()) {
        spec_io.template_text = preset_template;
    }
    const std::wstring start_input(start_value);
    const std::wstring step_input(step_value);
    const std::wstring width_input(width_value);
    ProWstringFree(template_value);
    ProWstringFree(start_value);
    ProWstringFree(step_value);
    ProWstringFree(width_value);
    ProUIDialogDestroy(dialog);

    if (!ParseIntValue(start_input, spec_io.start) ||
        !ParseIntValue(step_input, spec_io.step) ||
        !ParseIntValue(width_input, spec_io.width)) {
        error_out = L"\u8d77\u59cb\u503c\u3001\u6b65\u957f\u548c\u4f4d\u6570\u5fc5\u987b\u662f\u6574\u6570\u3002";
        return false;
    }
    return true;
}

bool IsEditableColumn(const std::string &column)
{
    return column == "NEWNAME" || column == "COMMON";
}

std::wstring CandidateCellText(const core::BatchRenameCandidate &candidate, const std::string &column)
{
    if (column == "MODEL") {
        return candidate.model_name;
    }
    if (column == "NEWNAME") {
        return candidate.new_model_name;
    }
    if (column == "COMMON") {
        return candidate.new_common_name;
    }
    if (column == "STATUS") {
        return candidate.status_text;
    }
    return std::wstring();
}

void SetCandidateCellText(core::BatchRenameCandidate &candidate, const std::string &column, const std::wstring &value)
{
    if (column == "NEWNAME") {
        candidate.new_model_name = value;
    } else if (column == "COMMON") {
        candidate.new_common_name = value;
    }
}

bool CandidateDirty(const core::BatchRenameCandidate &candidate)
{
    return autobbox::application::BatchRenameCandidateHasChanges(candidate);
}

std::wstring UppercaseForKey(const std::wstring &value)
{
    std::wstring out = value;
    for (wchar_t &ch : out) {
        ch = static_cast<wchar_t>(std::towupper(ch));
    }
    return out;
}

std::string FinalModelNameKey(ProMdlType type, const std::wstring &name)
{
    return std::to_string(static_cast<int>(type)) + ":" +
           autobbox::common::WToA(UppercaseForKey(TrimText(name)).c_str());
}

std::unordered_map<std::string, std::wstring> BuildDuplicateNameWarnings(
    const std::vector<core::BatchRenameCandidate> &candidates)
{
    std::unordered_map<std::string, std::vector<size_t>> owners_by_name;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const core::BatchRenameCandidate &candidate = candidates[i];
        if (!candidate.selected) {
            continue;
        }
        const std::wstring final_name = TrimText(candidate.new_model_name);
        if (final_name.empty()) {
            continue;
        }
        owners_by_name[FinalModelNameKey(candidate.type, final_name)].push_back(i);
    }

    std::unordered_map<std::string, std::wstring> warnings_by_row;
    for (const auto &entry : owners_by_name) {
        if (entry.second.size() < 2) {
            continue;
        }
        for (const size_t index : entry.second) {
            if (index >= candidates.size()) {
                continue;
            }
            const std::wstring final_name = TrimText(candidates[index].new_model_name);
            warnings_by_row[candidates[index].row_name] =
                L"\u91cd\u540d\uff1a\u76ee\u6807\u6a21\u578b\u540d\u201c" +
                final_name +
                L"\u201d\u5728\u672c\u6279\u6b21\u4e2d\u91cd\u590d";
        }
    }
    return warnings_by_row;
}

bool StateDirty(const BatchRenameDialogState &state)
{
    if (state.dirty) {
        return true;
    }
    if (state.candidates == nullptr) {
        return false;
    }
    for (const core::BatchRenameCandidate &candidate : *state.candidates) {
        if (CandidateDirty(candidate)) {
            return true;
        }
    }
    return false;
}

int SelectedCount(const BatchRenameDialogState &state)
{
    if (state.candidates == nullptr) {
        return 0;
    }
    int count = 0;
    for (const core::BatchRenameCandidate &candidate : *state.candidates) {
        if (candidate.selected) {
            ++count;
        }
    }
    return count;
}

std::wstring SummaryText(const BatchRenameDialogState &state)
{
    if (state.candidates == nullptr) {
        return L"当前列表 0 行";
    }
    int selected = 0;
    int changed = 0;
    int errors = 0;
    int parts = 0;
    int assemblies = 0;
    for (const core::BatchRenameCandidate &candidate : *state.candidates) {
        if (candidate.selected) {
            ++selected;
        }
        if (CandidateDirty(candidate)) {
            ++changed;
        }
        if (candidate.has_error) {
            ++errors;
        }
        if (candidate.type == PRO_MDL_PART) {
            ++parts;
        } else if (candidate.type == PRO_MDL_ASSEMBLY) {
            ++assemblies;
        }
    }
    return L"\u5f53\u524d\u5217\u8868 " + std::to_wstring(static_cast<int>(state.candidates->size())) +
           L" \u884c\uff0c\u5df2\u52fe\u9009 " + std::to_wstring(selected) +
           L" \u884c\uff0c\u96f6\u4ef6 " + std::to_wstring(parts) +
           L"\uff0c\u7ec4\u4ef6 " + std::to_wstring(assemblies) +
           L"\uff0c\u5df2\u4fee\u6539 " + std::to_wstring(changed) +
           L"\uff0c\u9519\u8bef " + std::to_wstring(errors) +
           L"\u3002\u53ef\u6309 Shift/Ctrl \u8fde\u7eed\u9009\u62e9\u8868\u683c\u884c\uff0c\u9009\u4e2d\u884c\u4f1a\u81ea\u52a8\u52fe\u9009\u3002";
}

void RefreshSummaryLabel(char *dialog,
                         const BatchRenameDialogConfig &config,
                         const BatchRenameDialogState &state)
{
    if (dialog == nullptr || config.summary_comp == nullptr) {
        return;
    }

    const std::wstring summary = SummaryText(state);
    ProUILabelTextSet(dialog,
                      const_cast<char *>(config.summary_comp),
                      const_cast<wchar_t *>(summary.c_str()));
}

void ClearActiveEditor(BatchRenameDialogState &state)
{
    state.active_row_name.clear();
    state.active_column_name.clear();
    state.active_component_name.clear();
}

void HarvestCheckboxStates(char *dialog, BatchRenameDialogState &state)
{
    if (dialog == nullptr || state.candidates == nullptr) {
        return;
    }
    for (const auto &entry : state.checkbox_index_by_name) {
        if (entry.second >= state.candidates->size()) {
            continue;
        }
        ProBoolean checked = PRO_B_FALSE;
        if (ProUICheckbuttonGetState(dialog,
                                     const_cast<char *>(entry.first.c_str()),
                                     &checked) == PRO_TK_NO_ERROR) {
            (*state.candidates)[entry.second].selected = (checked == PRO_B_TRUE);
        }
    }
}

int CheckSelectedTableRows(char *dialog,
                           BatchRenameDialogState &state,
                           const BatchRenameDialogConfig &config,
                           int *selected_row_count_out = nullptr)
{
    if (selected_row_count_out != nullptr) {
        *selected_row_count_out = 0;
    }
    if (dialog == nullptr || state.candidates == nullptr || config.table_comp == nullptr) {
        return 0;
    }

    int selected_count = 0;
    char **selected_rows = nullptr;
    if (ProUITableSelectedrownamesGet(dialog,
                                      const_cast<char *>(config.table_comp),
                                      &selected_count,
                                      &selected_rows) != PRO_TK_NO_ERROR ||
        selected_rows == nullptr) {
        return 0;
    }

    if (selected_row_count_out != nullptr) {
        *selected_row_count_out = selected_count;
    }

    int changed = 0;
    for (int i = 0; i < selected_count; ++i) {
        if (selected_rows[i] == nullptr) {
            continue;
        }
        const auto found = state.row_index_by_name.find(selected_rows[i]);
        if (found == state.row_index_by_name.end() || found->second >= state.candidates->size()) {
            continue;
        }

        core::BatchRenameCandidate &candidate = (*state.candidates)[found->second];
        if (!candidate.selected) {
            candidate.selected = true;
            ++changed;
        }

        const std::string checkbox_name = "brchk_" + candidate.row_name;
        ProUICheckbuttonSet(dialog, const_cast<char *>(checkbox_name.c_str()));
    }

    ProStringarrayFree(selected_rows, selected_count);
    return changed;
}

void SetRowCheckbox(char *dialog,
                    const core::BatchRenameCandidate &candidate,
                    bool checked)
{
    if (dialog == nullptr || candidate.row_name.empty()) {
        return;
    }
    const std::string checkbox_name = "brchk_" + candidate.row_name;
    if (checked) {
        ProUICheckbuttonSet(dialog, const_cast<char *>(checkbox_name.c_str()));
    } else {
        ProUICheckbuttonUnset(dialog, const_cast<char *>(checkbox_name.c_str()));
    }
}

int SelectCandidateRange(char *dialog,
                         BatchRenameDialogState &state,
                         size_t first,
                         size_t last)
{
    if (dialog == nullptr || state.candidates == nullptr || state.candidates->empty()) {
        return 0;
    }
    if (first > last) {
        std::swap(first, last);
    }
    if (first >= state.candidates->size()) {
        return 0;
    }
    last = std::min(last, state.candidates->size() - 1);

    int changed = 0;
    for (size_t i = first; i <= last; ++i) {
        core::BatchRenameCandidate &candidate = (*state.candidates)[i];
        if (!candidate.selected) {
            candidate.selected = true;
            ++changed;
        }
        SetRowCheckbox(dialog, candidate, true);
    }
    return changed;
}

core::BatchRenameCandidate *FindCandidate(BatchRenameDialogState &state, const std::string &row_name)
{
    if (state.candidates == nullptr) {
        return nullptr;
    }
    const auto it = state.row_index_by_name.find(row_name);
    if (it == state.row_index_by_name.end() || it->second >= state.candidates->size()) {
        return nullptr;
    }
    return &(*state.candidates)[it->second];
}

void HarvestActiveEditor(char *dialog,
                         BatchRenameDialogState &state,
                         const BatchRenameDialogConfig &config)
{
    if (dialog == nullptr || state.active_component_name.empty() || state.active_row_name.empty() || state.active_column_name.empty()) {
        return;
    }
    core::BatchRenameCandidate *candidate = FindCandidate(state, state.active_row_name);
    if (candidate == nullptr) {
        ClearActiveEditor(state);
        return;
    }

    wchar_t *value = nullptr;
    if (ProUIInputpanelValueGet(dialog, const_cast<char *>(state.active_component_name.c_str()), &value) == PRO_TK_NO_ERROR &&
        value != nullptr) {
        SetCandidateCellText(*candidate, state.active_column_name, value);
        ProWstringFree(value);
        state.dirty = true;
    }

    ProUITableCellComponentDelete(
        dialog,
        const_cast<char *>(config.table_comp),
        const_cast<char *>(state.active_row_name.c_str()),
        const_cast<char *>(state.active_column_name.c_str()));
    ProUITableCellLabelSet(
        dialog,
        const_cast<char *>(config.table_comp),
        const_cast<char *>(state.active_row_name.c_str()),
        const_cast<char *>(state.active_column_name.c_str()),
        const_cast<wchar_t *>(CandidateCellText(*candidate, state.active_column_name).c_str()));
    ClearActiveEditor(state);
}

void RenderTable(char *dialog,
                 BatchRenameDialogState &state,
                 const BatchRenameDialogConfig &config)
{
    if (dialog == nullptr || state.candidates == nullptr) {
        return;
    }

    state.row_index_by_name.clear();
    state.checkbox_index_by_name.clear();
    ClearActiveEditor(state);

    std::vector<std::string> column_names_storage = {"SELECT", "MODEL", "NEWNAME", "COMMON", "STATUS"};
    std::vector<std::wstring> column_labels_storage = {
        L"选择",
        L"模型名称",
        L"新模型名称",
        L"PTC_COMMON_NAME",
        L"状态",
    };
    std::vector<int> column_widths = {10, 24, 28, 30, 28};
    std::vector<int> column_resizings = {1, 2, 3, 3, 4};
    std::vector<char *> column_names;
    std::vector<wchar_t *> column_labels;
    for (std::string &name : column_names_storage) {
        column_names.push_back(const_cast<char *>(name.c_str()));
    }
    for (std::wstring &label : column_labels_storage) {
        column_labels.push_back(const_cast<wchar_t *>(label.c_str()));
    }

    std::vector<char *> row_names;
    std::vector<wchar_t *> row_labels;
    row_names.reserve(state.candidates->size());
    row_labels.reserve(state.candidates->size());
    for (size_t i = 0; i < state.candidates->size(); ++i) {
        core::BatchRenameCandidate &candidate = (*state.candidates)[i];
        if (candidate.row_name.empty()) {
            candidate.row_name = "br_" + std::to_string(i);
        }
        state.row_index_by_name[candidate.row_name] = i;
        row_names.push_back(const_cast<char *>(candidate.row_name.c_str()));
        row_labels.push_back(const_cast<wchar_t *>(L""));
    }

    ProUILabelTextSet(dialog, const_cast<char *>(config.summary_comp), const_cast<wchar_t *>(SummaryText(state).c_str()));
    ProUITableColumnnamesSet(dialog, const_cast<char *>(config.table_comp), static_cast<int>(column_names.size()), column_names.data());
    ProUITableColumnlabelsSet(dialog, const_cast<char *>(config.table_comp), static_cast<int>(column_labels.size()), column_labels.data());
    ProUITableColumnwidthsSet(dialog, const_cast<char *>(config.table_comp), static_cast<int>(column_widths.size()), column_widths.data());
    ProUITableColumnresizingsSet(dialog, const_cast<char *>(config.table_comp), static_cast<int>(column_resizings.size()), column_resizings.data());
    ProUITableRownamesSet(dialog, const_cast<char *>(config.table_comp), static_cast<int>(row_names.size()), row_names.empty() ? nullptr : row_names.data());
    ProUITableRowlabelsSet(dialog, const_cast<char *>(config.table_comp), static_cast<int>(row_labels.size()), row_labels.empty() ? nullptr : row_labels.data());
    ProUITableLockedcolumnsSet(dialog, const_cast<char *>(config.table_comp), 2);
    ProUITableSelectionpolicySet(dialog, const_cast<char *>(config.table_comp), PROUISELPOLICY_EXTENDED);
    ProUITableColumnselectionpolicySet(dialog, const_cast<char *>(config.table_comp), PROUISELPOLICY_NONE);
    ProUITableVisiblerowsSet(dialog, const_cast<char *>(config.table_comp), std::min(18, std::max(6, static_cast<int>(state.candidates->size()) + 1)));
    ProUITableMinrowsSet(dialog, const_cast<char *>(config.table_comp), std::min(16, std::max(6, static_cast<int>(state.candidates->size()))));
    ProUITableShowgridSet(dialog, const_cast<char *>(config.table_comp), PRO_B_TRUE);
    ProUITableAutohighlightEnable(dialog, const_cast<char *>(config.table_comp));
    ProUITableActivateonreturnEnable(dialog, const_cast<char *>(config.table_comp));

    const std::unordered_map<std::string, std::wstring> duplicate_warnings =
        BuildDuplicateNameWarnings(*state.candidates);

    for (core::BatchRenameCandidate &candidate : *state.candidates) {
        const auto duplicate_warning = duplicate_warnings.find(candidate.row_name);
        const bool duplicate_name = duplicate_warning != duplicate_warnings.end();
        const std::wstring duplicate_status = duplicate_name ? duplicate_warning->second : std::wstring();
        for (const std::string &column : column_names_storage) {
            ProUITableCellComponentDelete(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.row_name.c_str()), const_cast<char *>(column.c_str()));
            if (column != "SELECT") {
                const std::wstring cell_text =
                    (column == "STATUS" && duplicate_name && !candidate.has_error)
                        ? duplicate_status
                        : CandidateCellText(candidate, column);
                ProUITableCellLabelSet(dialog,
                                       const_cast<char *>(config.table_comp),
                                       const_cast<char *>(candidate.row_name.c_str()),
                                       const_cast<char *>(column.c_str()),
                                       const_cast<wchar_t *>(cell_text.c_str()));
            }
        }

        char check_name[96] = {0};
        std::snprintf(check_name, sizeof(check_name), "brchk_%s", candidate.row_name.c_str());
        ProUITableComponentDelete(dialog,
                                  const_cast<char *>(config.table_comp),
                                  check_name);
        ProUITableCellComponentCopy(dialog,
                                    const_cast<char *>(config.table_comp),
                                    const_cast<char *>(candidate.row_name.c_str()),
                                    const_cast<char *>("SELECT"),
                                    dialog,
                                    const_cast<char *>(config.select_base_comp),
                                    check_name);
        ProUICheckbuttonTextSet(dialog, check_name, const_cast<wchar_t *>(L""));
        if (candidate.selected) {
            ProUICheckbuttonSet(dialog, check_name);
        } else {
            ProUICheckbuttonUnset(dialog, check_name);
        }
        state.checkbox_index_by_name[check_name] = state.row_index_by_name[candidate.row_name];

        ProUITableCellBackgroundColorSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.row_name.c_str()), const_cast<char *>("SELECT"), PRO_UI_COLOR_LT_GREY);
        ProUITableCellBackgroundColorSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.row_name.c_str()), const_cast<char *>("MODEL"), PRO_UI_COLOR_LT_GREY);
        ProUITableCellBackgroundColorSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.row_name.c_str()), const_cast<char *>("STATUS"), duplicate_name ? PRO_UI_COLOR_YELLOW : PRO_UI_COLOR_LT_GREY);
        ProUITableCellForegroundColorSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.row_name.c_str()), const_cast<char *>("STATUS"), (candidate.has_error || duplicate_name) ? PRO_UI_COLOR_RED : PRO_UI_COLOR_BLACK);
        const ProUIColor changed_bg = CandidateDirty(candidate) ? PRO_UI_COLOR_YELLOW : PRO_UI_COLOR_WHITE;
        const ProUIColor newname_bg = duplicate_name ? PRO_UI_COLOR_RED : changed_bg;
        const ProUIColor newname_fg = duplicate_name ? PRO_UI_COLOR_WHITE : PRO_UI_COLOR_BLACK;
        ProUITableCellBackgroundColorSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.row_name.c_str()), const_cast<char *>("NEWNAME"), newname_bg);
        ProUITableCellBackgroundColorSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.row_name.c_str()), const_cast<char *>("COMMON"), changed_bg);
        ProUITableCellForegroundColorSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.row_name.c_str()), const_cast<char *>("NEWNAME"), newname_fg);
        if (duplicate_name) {
            ProUITableCellHelptextStringSet(dialog,
                                            const_cast<char *>(config.table_comp),
                                            const_cast<char *>(candidate.row_name.c_str()),
                                            const_cast<char *>("NEWNAME"),
                                            const_cast<wchar_t *>(duplicate_status.c_str()));
            ProUITableCellHelptextStringSet(dialog,
                                            const_cast<char *>(config.table_comp),
                                            const_cast<char *>(candidate.row_name.c_str()),
                                            const_cast<char *>("STATUS"),
                                            const_cast<wchar_t *>(duplicate_status.c_str()));
        } else if (!candidate.status_text.empty()) {
            ProUITableCellHelptextStringSet(dialog,
                                            const_cast<char *>(config.table_comp),
                                            const_cast<char *>(candidate.row_name.c_str()),
                                            const_cast<char *>("STATUS"),
                                            const_cast<wchar_t *>(candidate.status_text.c_str()));
        }
    }
}

void ActivateCellEditor(char *dialog,
                        BatchRenameDialogState &state,
                        const BatchRenameDialogConfig &config,
                        const std::string &row_name,
                        const std::string &column_name)
{
    if (!IsEditableColumn(column_name)) {
        return;
    }
    core::BatchRenameCandidate *candidate = FindCandidate(state, row_name);
    if (candidate == nullptr) {
        return;
    }

    ++state.editor_serial;
    char input_name[96] = {0};
    std::snprintf(input_name, sizeof(input_name), "brinp_%d_%s_%s", state.editor_serial, row_name.c_str(), column_name.c_str());

    ProUITableCellComponentDelete(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(row_name.c_str()), const_cast<char *>(column_name.c_str()));
    ProUITableCellComponentCopy(
        dialog,
        const_cast<char *>(config.table_comp),
        const_cast<char *>(row_name.c_str()),
        const_cast<char *>(column_name.c_str()),
        dialog,
        const_cast<char *>(config.cell_input_base_comp),
        input_name);
    ProUIInputpanelColumnsSet(dialog, input_name, column_name == "COMMON" ? 30 : 28);
    const std::wstring value = CandidateCellText(*candidate, column_name);
    ProUIInputpanelValueSet(dialog, input_name, const_cast<wchar_t *>(value.c_str()));
    state.active_row_name = row_name;
    state.active_column_name = column_name;
    state.active_component_name = input_name;
    ProUIDialogFocusSet(dialog, input_name);
}

void OnTableSelect(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<BatchRenameDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return;
    }

    char *row_name = nullptr;
    char *column_name = nullptr;
    if (ProUITableFocusCellGet(dialog, const_cast<char *>(runtime->config->table_comp), &row_name, &column_name) != PRO_TK_NO_ERROR ||
        row_name == nullptr || column_name == nullptr) {
        if (row_name != nullptr) {
            ProStringFree(row_name);
        }
        if (column_name != nullptr) {
            ProStringFree(column_name);
        }
        return;
    }

    const std::string row(row_name);
    const std::string column(column_name);
    const bool same_active_cell =
        runtime->state->active_row_name == row && runtime->state->active_column_name == column;
    ProStringFree(row_name);
    ProStringFree(column_name);

    if (column == "SELECT") {
        HarvestActiveEditor(dialog, *runtime->state, *runtime->config);
        HarvestCheckboxStates(dialog, *runtime->state);
        const auto selected_row = runtime->state->row_index_by_name.find(row);
        if (selected_row != runtime->state->row_index_by_name.end()) {
            runtime->state->selection_anchor_index = static_cast<int>(selected_row->second);
        }
        RefreshSummaryLabel(dialog, *runtime->config, *runtime->state);
        return;
    }

    const auto current_row = runtime->state->row_index_by_name.find(row);
    const bool has_current_row =
        current_row != runtime->state->row_index_by_name.end() &&
        runtime->state->candidates != nullptr &&
        current_row->second < runtime->state->candidates->size();
    const bool shift_down = (GetKeyState(VK_SHIFT) & 0x8000) != 0;

    int selected_row_count = 0;
    int checked_from_table = 0;
    bool range_selected = false;
    if (has_current_row &&
        shift_down &&
        runtime->state->selection_anchor_index >= 0 &&
        static_cast<size_t>(runtime->state->selection_anchor_index) < runtime->state->candidates->size()) {
        checked_from_table = SelectCandidateRange(dialog,
                                                  *runtime->state,
                                                  static_cast<size_t>(runtime->state->selection_anchor_index),
                                                  current_row->second);
        selected_row_count = std::abs(static_cast<int>(current_row->second) -
                                      runtime->state->selection_anchor_index) +
                             1;
        range_selected = true;
    } else {
        checked_from_table =
            CheckSelectedTableRows(dialog, *runtime->state, *runtime->config, &selected_row_count);
        if (has_current_row) {
            core::BatchRenameCandidate &candidate = (*runtime->state->candidates)[current_row->second];
            if (!candidate.selected) {
                candidate.selected = true;
                SetRowCheckbox(dialog, candidate, true);
                ++checked_from_table;
            }
            runtime->state->selection_anchor_index = static_cast<int>(current_row->second);
        }
    }

    if (checked_from_table > 0) {
        runtime->state->dirty = true;
    }

    if ((range_selected || selected_row_count > 1) && checked_from_table > 0) {
        HarvestActiveEditor(dialog, *runtime->state, *runtime->config);
        RefreshSummaryLabel(dialog, *runtime->config, *runtime->state);
        return;
    }

    if (same_active_cell) {
        if (checked_from_table > 0) {
            RefreshSummaryLabel(dialog, *runtime->config, *runtime->state);
        }
        return;
    }
    HarvestActiveEditor(dialog, *runtime->state, *runtime->config);
    ActivateCellEditor(dialog, *runtime->state, *runtime->config, row, column);
}

std::vector<std::wstring> SplitLines(const std::wstring &text)
{
    std::vector<std::wstring> lines;
    std::wstring current;
    for (wchar_t ch : text) {
        if (ch == L'\r') {
            continue;
        }
        if (ch == L'\n') {
            lines.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    if (!current.empty()) {
        lines.push_back(current);
    }
    return lines;
}

std::vector<std::wstring> SplitTabs(const std::wstring &line)
{
    std::vector<std::wstring> cols;
    std::wstring current;
    for (wchar_t ch : line) {
        if (ch == L'\t') {
            cols.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    cols.push_back(current);
    return cols;
}

bool LooksLikeHeader(const std::vector<std::wstring> &cols)
{
    if (cols.empty()) {
        return false;
    }
    std::wstring joined;
    for (const std::wstring &col : cols) {
        joined += col;
        joined += L" ";
    }
    return joined.find(L"模型") != std::wstring::npos ||
           joined.find(L"Model") != std::wstring::npos ||
           joined.find(L"PTC_COMMON_NAME") != std::wstring::npos;
}

bool ReadClipboardUnicodeText(std::wstring &text_out)
{
    text_out.clear();
    if (OpenClipboard(nullptr) == 0) {
        return false;
    }
    HANDLE handle = GetClipboardData(CF_UNICODETEXT);
    if (handle == nullptr) {
        CloseClipboard();
        return false;
    }
    const wchar_t *data = static_cast<const wchar_t *>(GlobalLock(handle));
    if (data == nullptr) {
        CloseClipboard();
        return false;
    }
    text_out.assign(data);
    GlobalUnlock(handle);
    CloseClipboard();
    return !text_out.empty();
}

size_t FocusRowIndex(BatchRenameDialogState &state, const BatchRenameDialogConfig &config, char *dialog)
{
    char *row_name = nullptr;
    char *column_name = nullptr;
    size_t row_index = 0;
    if (ProUITableFocusCellGet(dialog, const_cast<char *>(config.table_comp), &row_name, &column_name) == PRO_TK_NO_ERROR && row_name != nullptr) {
        const auto it = state.row_index_by_name.find(row_name);
        if (it != state.row_index_by_name.end()) {
            row_index = it->second;
        }
    }
    if (row_name != nullptr) {
        ProStringFree(row_name);
    }
    if (column_name != nullptr) {
        ProStringFree(column_name);
    }
    return row_index;
}

void OnPaste(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<BatchRenameDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr || runtime->state->candidates == nullptr) {
        return;
    }
    HarvestActiveEditor(dialog, *runtime->state, *runtime->config);
    HarvestCheckboxStates(dialog, *runtime->state);

    std::wstring text;
    if (!ReadClipboardUnicodeText(text)) {
        ShowSimpleMessageDialog(PROUIMESSAGE_WARNING, L"批量重命名", L"剪贴板中没有可粘贴的 Excel 文本。请复制一列或两列数据：1 列=新模型名称，2 列=新模型名称和 PTC_COMMON_NAME。");
        return;
    }

    std::vector<std::wstring> lines = SplitLines(text);
    if (lines.empty()) {
        return;
    }
    size_t line_begin = 0;
    if (LooksLikeHeader(SplitTabs(lines.front()))) {
        line_begin = 1;
    }
    size_t row_index = FocusRowIndex(*runtime->state, *runtime->config, dialog);
    int pasted = 0;
    for (size_t i = line_begin; i < lines.size() && row_index < runtime->state->candidates->size(); ++i, ++row_index) {
        std::vector<std::wstring> cols = SplitTabs(lines[i]);
        if (cols.empty()) {
            continue;
        }
        core::BatchRenameCandidate &candidate = (*runtime->state->candidates)[row_index];
        candidate.new_model_name = cols[0];
        if (cols.size() >= 2) {
            candidate.new_common_name = cols[1];
        }
        candidate.status_text = L"已粘贴，待验证";
        candidate.has_error = false;
        ++pasted;
    }
    runtime->state->dirty = pasted > 0 || runtime->state->dirty;
    LogLine(runtime->callbacks == nullptr ? BatchRenameDialogLogSink() : runtime->callbacks->log_sink,
            "batch-rename-dialog paste rows=%d", pasted);
    RenderTable(dialog, *runtime->state, *runtime->config);
}

void OnClear(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<BatchRenameDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr || runtime->state->candidates == nullptr) {
        return;
    }
    HarvestActiveEditor(dialog, *runtime->state, *runtime->config);
    HarvestCheckboxStates(dialog, *runtime->state);
    if (SelectedCount(*runtime->state) <= 0) {
        ShowSimpleMessageDialog(PROUIMESSAGE_INFO, L"\u6279\u91cf\u91cd\u547d\u540d", L"\u8bf7\u5148\u52fe\u9009\u8981\u4fee\u6539\u7684\u6a21\u578b\u3002");
        return;
    }

    bool cancelled = false;
    std::wstring error;
    core::BatchRenameClearSpec spec = runtime->state->last_clear_spec;
    if (!PromptClearDialog(spec,
                           cancelled,
                           runtime->callbacks == nullptr ? BatchRenameDialogLogSink() : runtime->callbacks->log_sink,
                           error)) {
        if (!cancelled && !error.empty()) {
            ShowSimpleMessageDialog(PROUIMESSAGE_WARNING,
                                    L"\u6279\u91cf\u91cd\u547d\u540d",
                                    error.c_str());
        }
        return;
    }

    core::BatchRenameTransformSummary summary;
    autobbox::application::ClearBatchRenameCandidates(*runtime->state->candidates, spec, summary);
    runtime->state->last_clear_spec = spec;
    runtime->state->dirty = summary.changed_rows > 0 || runtime->state->dirty;
    RenderTable(dialog, *runtime->state, *runtime->config);
    if (summary.changed_rows == 0 && !summary.summary_text.empty()) {
        ShowSimpleMessageDialog(PROUIMESSAGE_INFO,
                                L"\u6279\u91cf\u91cd\u547d\u540d",
                                summary.summary_text.c_str());
    }
}

void OnReplace(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<BatchRenameDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr || runtime->state->candidates == nullptr) {
        return;
    }
    HarvestActiveEditor(dialog, *runtime->state, *runtime->config);
    HarvestCheckboxStates(dialog, *runtime->state);
    if (SelectedCount(*runtime->state) <= 0) {
        ShowSimpleMessageDialog(PROUIMESSAGE_INFO, L"\u6279\u91cf\u91cd\u547d\u540d", L"\u8bf7\u5148\u52fe\u9009\u8981\u4fee\u6539\u7684\u6a21\u578b\u3002");
        return;
    }

    bool cancelled = false;
    std::wstring error;
    core::BatchRenameReplaceSpec spec = runtime->state->last_replace_spec;
    if (!PromptReplaceDialog(spec,
                             cancelled,
                             runtime->callbacks == nullptr ? BatchRenameDialogLogSink() : runtime->callbacks->log_sink,
                             error)) {
        if (!cancelled && !error.empty()) {
            ShowSimpleMessageDialog(PROUIMESSAGE_WARNING,
                                    L"\u6279\u91cf\u91cd\u547d\u540d",
                                    error.c_str());
        }
        return;
    }

    core::BatchRenameTransformSummary summary;
    if (!autobbox::application::ReplaceBatchRenameCandidates(*runtime->state->candidates,
                                                             spec,
                                                             summary,
                                                             error)) {
        ShowSimpleMessageDialog(PROUIMESSAGE_WARNING,
                                L"\u6279\u91cf\u91cd\u547d\u540d",
                                error.c_str());
        return;
    }

    runtime->state->last_replace_spec = spec;
    runtime->state->dirty = summary.changed_rows > 0 || runtime->state->dirty;
    RenderTable(dialog, *runtime->state, *runtime->config);
    if (summary.changed_rows == 0 && !summary.summary_text.empty()) {
        ShowSimpleMessageDialog(PROUIMESSAGE_INFO,
                                L"\u6279\u91cf\u91cd\u547d\u540d",
                                summary.summary_text.c_str());
    }
}

void OnSequence(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<BatchRenameDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr || runtime->state->candidates == nullptr) {
        return;
    }
    HarvestActiveEditor(dialog, *runtime->state, *runtime->config);
    HarvestCheckboxStates(dialog, *runtime->state);

    bool cancelled = false;
    std::wstring error;
    core::BatchRenameSequenceSpec spec = runtime->state->last_sequence_spec;
    if (!PromptSequenceDialog(spec,
                              cancelled,
                              runtime->callbacks == nullptr ? BatchRenameDialogLogSink() : runtime->callbacks->log_sink,
                              error)) {
        if (!cancelled && !error.empty()) {
            ShowSimpleMessageDialog(PROUIMESSAGE_WARNING,
                                    L"\u6279\u91cf\u91cd\u547d\u540d",
                                    error.c_str());
        }
        return;
    }

    core::BatchRenameTransformSummary summary;
    if (!autobbox::application::SequenceBatchRenameCandidates(*runtime->state->candidates,
                                                              spec,
                                                              summary,
                                                              error)) {
        ShowSimpleMessageDialog(PROUIMESSAGE_WARNING,
                                L"\u6279\u91cf\u91cd\u547d\u540d",
                                error.c_str());
        return;
    }

    runtime->state->last_sequence_spec = spec;
    runtime->state->dirty = summary.changed_rows > 0 || runtime->state->dirty;
    RenderTable(dialog, *runtime->state, *runtime->config);
    if (summary.changed_rows == 0 && !summary.summary_text.empty()) {
        ShowSimpleMessageDialog(PROUIMESSAGE_INFO,
                                L"\u6279\u91cf\u91cd\u547d\u540d",
                                summary.summary_text.c_str());
    }
}

void OnValidate(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<BatchRenameDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr || runtime->callbacks == nullptr || runtime->state->candidates == nullptr) {
        return;
    }
    HarvestActiveEditor(dialog, *runtime->state, *runtime->config);
    HarvestCheckboxStates(dialog, *runtime->state);
    if (SelectedCount(*runtime->state) <= 0) {
        ShowSimpleMessageDialog(PROUIMESSAGE_INFO, L"\u6279\u91cf\u91cd\u547d\u540d", L"\u8bf7\u5148\u52fe\u9009\u8981\u4fee\u6539\u7684\u6a21\u578b\u3002");
        return;
    }
    std::wstring error;
    const bool ok = runtime->callbacks->validate_candidates ? runtime->callbacks->validate_candidates(*runtime->state->candidates, error) : true;
    RenderTable(dialog, *runtime->state, *runtime->config);
    ShowSimpleMessageDialog(ok ? PROUIMESSAGE_INFO : PROUIMESSAGE_WARNING,
                            L"批量重命名",
                            ok ? L"校验通过" : (error.empty() ? L"存在错误，请查看状态列" : error.c_str()));
}

void ResetCandidates(std::vector<core::BatchRenameCandidate> &candidates)
{
    for (core::BatchRenameCandidate &candidate : candidates) {
        candidate.new_model_name = candidate.model_name;
        candidate.normalized_new_model_name.clear();
        candidate.new_common_name = candidate.common_name;
        candidate.status_text = L"未改动";
        candidate.has_error = false;
    }
}

void OnReset(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<BatchRenameDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr || runtime->state->candidates == nullptr) {
        return;
    }
    HarvestActiveEditor(dialog, *runtime->state, *runtime->config);
    HarvestCheckboxStates(dialog, *runtime->state);
    ResetCandidates(*runtime->state->candidates);
    runtime->state->dirty = false;
    RenderTable(dialog, *runtime->state, *runtime->config);
}

void OnRefresh(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<BatchRenameDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr || runtime->callbacks == nullptr || runtime->state->candidates == nullptr) {
        return;
    }
    HarvestActiveEditor(dialog, *runtime->state, *runtime->config);
    HarvestCheckboxStates(dialog, *runtime->state);
    if (StateDirty(*runtime->state) &&
        !ShowYesNoDialog(L"批量重命名", L"存在未应用修改，刷新列表会丢弃这些修改。是否继续？")) {
        return;
    }
    if (!runtime->callbacks->collect_candidates) {
        return;
    }
    *runtime->state->candidates = runtime->callbacks->collect_candidates();
    runtime->state->dirty = false;
    if (runtime->state->candidates->empty()) {
        ShowSimpleMessageDialog(PROUIMESSAGE_INFO, L"批量重命名", L"按当前零件/组件/仅二层过滤条件未找到候选模型。" );
    }
    RenderTable(dialog, *runtime->state, *runtime->config);
}

void OnApply(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<BatchRenameDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr || runtime->callbacks == nullptr || runtime->state->candidates == nullptr) {
        return;
    }
    HarvestActiveEditor(dialog, *runtime->state, *runtime->config);
    HarvestCheckboxStates(dialog, *runtime->state);
    if (SelectedCount(*runtime->state) <= 0) {
        ShowSimpleMessageDialog(PROUIMESSAGE_INFO, L"\u6279\u91cf\u91cd\u547d\u540d", L"\u8bf7\u5148\u52fe\u9009\u8981\u4fee\u6539\u7684\u6a21\u578b\u3002");
        return;
    }

    core::BatchRenameApplySummary summary;
    std::wstring error;
    const bool ok = runtime->callbacks->apply_candidates && runtime->callbacks->apply_candidates(*runtime->state->candidates, summary, error);
    runtime->state->dirty = !ok;
    RenderTable(dialog, *runtime->state, *runtime->config);
    const std::wstring message = !summary.summary_text.empty()
                                     ? summary.summary_text
                                     : (error.empty() ? L"应用失败" : error);
    ShowSimpleMessageDialog(ok ? PROUIMESSAGE_INFO : PROUIMESSAGE_WARNING,
                            L"批量重命名",
                            message.c_str());
}

void OnClose(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<BatchRenameDialogRuntime *>(app_data);
    if (runtime != nullptr && runtime->state != nullptr && runtime->config != nullptr) {
        HarvestActiveEditor(dialog, *runtime->state, *runtime->config);
        HarvestCheckboxStates(dialog, *runtime->state);
        if (StateDirty(*runtime->state) &&
            !ShowYesNoDialog(L"批量重命名", L"存在未应用修改，确定关闭吗？")) {
            return;
        }
    }
    ProUIDialogExit(dialog, 0);
}

} // namespace

bool PromptBatchRenameDialog(std::vector<core::BatchRenameCandidate> &candidates,
                             const BatchRenameDialogCallbacks &callbacks)
{
    const BatchRenameDialogConfig config = DefaultBatchRenameDialogConfig();
    std::string used_resource;
    const ProError create_status = TryCreateDialog(config, callbacks.log_sink, used_resource);
    if (create_status != PRO_TK_NO_ERROR) {
        ShowSimpleMessageDialog(
            PROUIMESSAGE_ERROR,
            L"批量重命名",
            (L"无法创建批量重命名对话框，Creo 状态=" + std::to_wstring(static_cast<int>(create_status))).c_str());
        return false;
    }
    LogLine(callbacks.log_sink, "batch-rename-dialog create ok resource=%s", used_resource.c_str());

    char *dialog = const_cast<char *>(config.dialog_inst_name);
    ProUIDialogTitleSet(dialog, const_cast<wchar_t *>(L"批量重命名"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.paste_comp), const_cast<wchar_t *>(L"粘贴"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.clear_comp), const_cast<wchar_t *>(L"清空"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.replace_comp), const_cast<wchar_t *>(L"批量替换"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.sequence_comp), const_cast<wchar_t *>(L"自动序号"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.validate_comp), const_cast<wchar_t *>(L"验证"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.reset_comp), const_cast<wchar_t *>(L"重置"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.refresh_comp), const_cast<wchar_t *>(L"刷新列表"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.apply_comp), const_cast<wchar_t *>(L"应用"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.close_comp), const_cast<wchar_t *>(L"关闭"));

    BatchRenameDialogState state;
    state.candidates = &candidates;
    BatchRenameDialogRuntime runtime;
    runtime.state = &state;
    runtime.config = &config;
    runtime.callbacks = &callbacks;

    ProUITableSelectActionSet(dialog, const_cast<char *>(config.table_comp), OnTableSelect, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.paste_comp), OnPaste, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.clear_comp), OnClear, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.replace_comp), OnReplace, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.sequence_comp), OnSequence, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.validate_comp), OnValidate, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.reset_comp), OnReset, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.refresh_comp), OnRefresh, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.apply_comp), OnApply, &runtime);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(config.close_comp), OnClose, &runtime);
    ProUIDialogCloseActionSet(dialog, OnClose, &runtime);
    ProUIDialogDefaultbuttonSet(dialog, const_cast<char *>(config.apply_comp));

    RenderTable(dialog, state, config);

    int dialog_status = config.status_close;
    const ProError activate_status = ProUIDialogActivate(dialog, &dialog_status);
    LogLine(callbacks.log_sink, "batch-rename-dialog activate status=%d dialog_status=%d", static_cast<int>(activate_status), dialog_status);
    ProUIDialogDestroy(dialog);
    return activate_status == PRO_TK_NO_ERROR;
}

} // namespace autobbox::ui
