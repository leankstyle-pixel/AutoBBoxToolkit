#include "autobbox/ui/save_copy_to_workdir_dialog.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"

#include <ProUICheckbutton.h>
#include <ProUIDialog.h>
#include <ProUIInputpanel.h>
#include <ProUILabel.h>
#include <ProUIPushbutton.h>
#include <ProSizeConst.h>
#include <ProToolkit.h>
#include <ProUtil.h>

#include <cstdint>
#include <string>
#include <vector>

namespace autobbox::ui {

namespace {

struct SaveCopyDialogConfig {
    const char *dialog_name = "autobbox_save_copy_to_workdir_inst";
    const char *resource_base_name = "autobbox_save_copy_to_workdir";
    const char *source_label_comp = "SourceLabel";
    const char *name_input_comp = "NewNameInput";
    const char *replace_comp = "ReplaceCheck";
    const char *ok_comp = "OKBtn";
    const char *cancel_comp = "CancelBtn";
    int status_cancel = 0;
    int status_ok = 1;
};

void LogLine(const SaveCopyToWorkdirDialogLogSink &log_sink, const std::string &line)
{
    if (log_sink) {
        log_sink(line);
    }
}

ProError TryCreateDialog(const SaveCopyDialogConfig &config,
                         const SaveCopyToWorkdirDialogLogSink &log_sink,
                         std::string &used_resource)
{
    used_resource.clear();
    ProError last = PRO_TK_GENERAL_ERROR;
    const std::string base_name = config.resource_base_name;
    const std::vector<std::string> candidates = {
        base_name,
        base_name + ".res",
        std::string("resource\\") + base_name,
        std::string("resource\\") + base_name + ".res",
    };

    for (const std::string &resource : candidates) {
        last = ProUIDialogCreate(
            const_cast<char *>(config.dialog_name),
            const_cast<char *>(resource.c_str()));
        LogLine(log_sink,
                "save-copy-dialog create try resource=" + resource +
                    " status=" + std::to_string(static_cast<int>(last)));
        if (last == PRO_TK_NO_ERROR) {
            used_resource = resource;
            return last;
        }
    }

    ProPath text_root_w = {0};
    if (ProToolkitApplTextPathGet(text_root_w) == PRO_TK_NO_ERROR) {
        const std::string text_root = autobbox::common::WToA(text_root_w);
        if (!text_root.empty()) {
            const std::vector<std::string> abs_candidates = {
                text_root + "\\resource\\" + base_name,
                text_root + "\\resource\\" + base_name + ".res",
                text_root + "\\text\\resource\\" + base_name,
                text_root + "\\text\\resource\\" + base_name + ".res",
                text_root + "\\text\\usascii\\resource\\" + base_name,
                text_root + "\\text\\usascii\\resource\\" + base_name + ".res",
            };
            for (const std::string &path : abs_candidates) {
                if (!autobbox::common::FileExistsA(path)) {
                    continue;
                }
                last = ProUIDialogCreate(
                    const_cast<char *>(config.dialog_name),
                    const_cast<char *>(path.c_str()));
                LogLine(log_sink,
                        "save-copy-dialog create try resource=" + path +
                            " status=" + std::to_string(static_cast<int>(last)));
                if (last == PRO_TK_NO_ERROR) {
                    used_resource = path;
                    return last;
                }
            }
        }
    }

    return last;
}

void ExitWithStatus(char *dialog, char *, ProAppData app_data)
{
    if (dialog != nullptr) {
        ProUIDialogExit(dialog, static_cast<int>(reinterpret_cast<std::intptr_t>(app_data)));
    }
}

void SetCheckState(char *dialog, const char *comp, bool checked)
{
    if (dialog == nullptr || comp == nullptr) {
        return;
    }
    if (checked) {
        ProUICheckbuttonSet(dialog, const_cast<char *>(comp));
    } else {
        ProUICheckbuttonUnset(dialog, const_cast<char *>(comp));
    }
}

bool GetCheckState(char *dialog, const char *comp, bool fallback)
{
    if (dialog == nullptr || comp == nullptr) {
        return fallback;
    }
    ProBoolean checked = PRO_B_FALSE;
    if (ProUICheckbuttonGetState(dialog, const_cast<char *>(comp), &checked) != PRO_TK_NO_ERROR) {
        return fallback;
    }
    return checked == PRO_B_TRUE;
}

std::wstring DefaultCopyName(const std::wstring &source_name)
{
    if (source_name.empty()) {
        return L"copy";
    }
    if (source_name.size() + 5 < PRO_NAME_SIZE) {
        return source_name + L"_COPY";
    }
    return source_name;
}

std::wstring SourceSummary(const application::SaveCopyToWorkdirSource &source)
{
    std::wstring summary = L"\u6e90\u6a21\u578b\uff1a" + source.name;
    if (source.from_file_picker && !source.source_path.empty()) {
        summary += L"\n\u6765\u6e90\u6587\u4ef6\uff1a" + source.source_path;
    }
    summary += L"\n\u4fdd\u5b58\u540e\u5c06\u7ec4\u88c5\u5230\u5f53\u524d\u88c5\u914d\uff0c\u5e76\u6253\u5f00 Creo \u5b98\u65b9\u7ec4\u4ef6\u7ea6\u675f\u754c\u9762\u3002";
    return summary;
}

} // namespace

bool PromptSaveCopyToWorkdirDialog(
    const application::SaveCopyToWorkdirSource &source,
    SaveCopyToWorkdirDialogResult &result,
    bool &cancelled,
    std::wstring &error_out,
    const SaveCopyToWorkdirDialogLogSink &log_sink)
{
    const SaveCopyDialogConfig config = {};
    result = {};
    cancelled = false;
    error_out.clear();

    std::string used_resource;
    const ProError create_status = TryCreateDialog(config, log_sink, used_resource);
    if (create_status != PRO_TK_NO_ERROR) {
        error_out =
            L"\u6253\u5f00\u7ec4\u88c5\u526f\u672c\u5bf9\u8bdd\u6846\u5931\u8d25\uff0cCreo \u8fd4\u56de\u72b6\u6001\uff1a" +
            std::to_wstring(static_cast<int>(create_status));
        return false;
    }
    LogLine(log_sink, "save-copy-dialog create ok resource=" + used_resource);

    char *dialog = const_cast<char *>(config.dialog_name);
    ProUIDialogTitleSet(dialog, const_cast<wchar_t *>(L"\u7ec4\u88c5\u526f\u672c"));
    ProUILabelTextSet(
        dialog,
        const_cast<char *>(config.source_label_comp),
        const_cast<wchar_t *>(SourceSummary(source).c_str()));
    ProUIInputpanelColumnsSet(dialog, const_cast<char *>(config.name_input_comp), 28);
    const std::wstring default_name = DefaultCopyName(source.name);
    ProUIInputpanelValueSet(
        dialog,
        const_cast<char *>(config.name_input_comp),
        const_cast<wchar_t *>(default_name.c_str()));
    ProUICheckbuttonTextSet(
        dialog,
        const_cast<char *>(config.replace_comp),
        const_cast<wchar_t *>(L"\u4fdd\u5b58\u540e\u7ec4\u88c5\u5230\u5f53\u524d\u88c5\u914d\u5e76\u5b9a\u4e49\u7ea6\u675f"));
    SetCheckState(dialog, config.replace_comp, true);
    ProUICheckbuttonDisable(dialog, const_cast<char *>(config.replace_comp));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.ok_comp), const_cast<wchar_t *>(L"\u4fdd\u5b58\u5e76\u7ec4\u88c5"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.cancel_comp), const_cast<wchar_t *>(L"\u53d6\u6d88"));

    ProUIPushbuttonActivateActionSet(
        dialog,
        const_cast<char *>(config.ok_comp),
        ExitWithStatus,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_ok)));
    ProUIPushbuttonActivateActionSet(
        dialog,
        const_cast<char *>(config.cancel_comp),
        ExitWithStatus,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_cancel)));
    ProUIDialogCloseActionSet(
        dialog,
        ExitWithStatus,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_cancel)));
    ProUIDialogDefaultbuttonSet(dialog, const_cast<char *>(config.ok_comp));
    ProUIInputpanelActivateActionSet(
        dialog,
        const_cast<char *>(config.name_input_comp),
        ExitWithStatus,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_ok)));

    int dialog_status = config.status_cancel;
    const ProError activate_status = ProUIDialogActivate(dialog, &dialog_status);
    LogLine(log_sink,
            "save-copy-dialog activate status=" +
                std::to_string(static_cast<int>(activate_status)) +
                " dialog_status=" + std::to_string(dialog_status));
    if (activate_status != PRO_TK_NO_ERROR) {
        error_out =
            L"\u6fc0\u6d3b\u7ec4\u88c5\u526f\u672c\u5bf9\u8bdd\u6846\u5931\u8d25\uff0cCreo \u8fd4\u56de\u72b6\u6001\uff1a" +
            std::to_wstring(static_cast<int>(activate_status));
        ProUIDialogDestroy(dialog);
        return false;
    }
    if (dialog_status != config.status_ok) {
        cancelled = true;
        ProUIDialogDestroy(dialog);
        return false;
    }

    wchar_t *input_value = nullptr;
    if (ProUIInputpanelValueGet(dialog, const_cast<char *>(config.name_input_comp), &input_value) != PRO_TK_NO_ERROR ||
        input_value == nullptr) {
        if (input_value != nullptr) {
            ProWstringFree(input_value);
        }
        error_out = L"\u8bfb\u53d6\u65b0\u540d\u79f0\u5931\u8d25\u3002";
        ProUIDialogDestroy(dialog);
        return false;
    }

    result.new_name = input_value;
    result.replace_component = false;
    ProWstringFree(input_value);
    ProUIDialogDestroy(dialog);
    return true;
}

} // namespace autobbox::ui
