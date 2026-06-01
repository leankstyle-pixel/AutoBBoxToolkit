#include "autobbox/ui/relations_text_dialog.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"

#include <ProUIDialog.h>
#include <ProUILabel.h>
#include <ProUIPushbutton.h>
#include <ProToolkit.h>
#include <ProUITextarea.h>
#include <ProUtil.h>

#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace autobbox::ui {

namespace {

struct RelationsTextDialogConfig {
    const char *dialog_inst_name = nullptr;
    const char *resource_base_name = nullptr;
    const char *prompt_comp = nullptr;
    const char *text_comp = nullptr;
    const char *ok_comp = nullptr;
    const char *cancel_comp = nullptr;
    int status_ok = 0;
    int status_cancel = 0;
};

RelationsTextDialogConfig DefaultRelationsTextDialogConfig()
{
    RelationsTextDialogConfig config = {};
    config.dialog_inst_name = "autobbox_rel_add_inst";
    config.resource_base_name = "autobbox_rel_add";
    config.prompt_comp = "PromptLabel";
    config.text_comp = "RelationText";
    config.ok_comp = "OKBtn";
    config.cancel_comp = "CancelBtn";
    config.status_ok = 1;
    config.status_cancel = 0;
    return config;
}

void LogLine(const RelationsTextDialogLogSink &log_sink, const char *fmt, ...)
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

ProError TryCreateDialog(const RelationsTextDialogConfig &config,
                         const RelationsTextDialogLogSink &log_sink,
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
        LogLine(log_sink, "reladd-dialog-create try resource=%s status=%d", res.c_str(), static_cast<int>(last));
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
                        "reladd-dialog-path probe exists=%d path=%s",
                        autobbox::common::FileExistsA(path) ? 1 : 0,
                        path.c_str());
                last = ProUIDialogCreate(
                    const_cast<char *>(config.dialog_inst_name),
                    const_cast<char *>(path.c_str()));
                LogLine(log_sink, "reladd-dialog-create try resource=%s status=%d", path.c_str(), static_cast<int>(last));
                if (last == PRO_TK_NO_ERROR) {
                    used_resource = path;
                    return last;
                }
            }
        }
    }

    return last;
}

} // namespace

bool PromptRelationsTextDialog(std::wstring &text,
                               bool &cancelled,
                               const RelationsTextDialogLogSink &log_sink)
{
    const RelationsTextDialogConfig config = DefaultRelationsTextDialogConfig();
    text.clear();
    cancelled = false;

    std::string used_resource;
    const ProError st = TryCreateDialog(config, log_sink, used_resource);
    if (st != PRO_TK_NO_ERROR) {
        LogLine(log_sink, "ERROR reladd-dialog-create failed status=%d", static_cast<int>(st));
        cancelled = true;
        return false;
    }

    LogLine(log_sink, "reladd-dialog-create success resource=%s", used_resource.c_str());
    ProUIDialogTitleSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<wchar_t *>(L"加关系式"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.prompt_comp),
        const_cast<wchar_t *>(L"粘贴需要批量追加到主关系式集的关系式文本："));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.ok_comp),
        const_cast<wchar_t *>(L"确定"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.cancel_comp),
        const_cast<wchar_t *>(L"取消"));
    ProUITextareaRowsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.text_comp),
        18);
    ProUITextareaMinrowsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.text_comp),
        14);
    ProUITextareaColumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.text_comp),
        110);
    ProUITextareaMaxlenSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.text_comp),
        50000);
    ProUITextareaValueSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.text_comp),
        const_cast<wchar_t *>(L""));
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

    int dialog_status = config.status_cancel;
    const ProError st_act = ProUIDialogActivate(
        const_cast<char *>(config.dialog_inst_name),
        &dialog_status);
    if (st_act != PRO_TK_NO_ERROR || dialog_status != config.status_ok) {
        cancelled = true;
        ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
        return false;
    }

    wchar_t *value = nullptr;
    if (ProUITextareaValueGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.text_comp),
            &value) == PRO_TK_NO_ERROR &&
        value != nullptr) {
        text.assign(value);
        ProWstringFree(value);
    }

    ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
    return true;
}

} // namespace autobbox::ui
