#include "autobbox/ui/drawing_arrange_dialog.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"

#include <ProUICheckbutton.h>
#include <ProUIDialog.h>
#include <ProUILabel.h>
#include <ProUIPushbutton.h>
#include <ProToolkit.h>
#include <ProUtil.h>

#include <cstdint>
#include <string>
#include <vector>

namespace autobbox::ui {

namespace {

struct DrawingArrangeDialogConfig {
    const char *dialog_inst_name = "autobbox_drawing_arrange_inst";
    const char *resource_base_name = "autobbox_drawing_arrange";
    const char *prompt_comp = "PromptLabel";
    const char *frame_comp = "FrameCheck";
    const char *title_comp = "TitleCheck";
    const char *ok_comp = "OKBtn";
    const char *cancel_comp = "CancelBtn";
    int status_ok = 1;
    int status_cancel = 0;
};

ProError TryCreateDialog(const DrawingArrangeDialogConfig &config, std::string &used_resource)
{
    used_resource.clear();
    ProError last = PRO_TK_GENERAL_ERROR;
    const std::string base_name = config.resource_base_name;
    const std::vector<std::string> candidates = {base_name, base_name + ".res"};
    for (const std::string &resource : candidates) {
        last = ProUIDialogCreate(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(resource.c_str()));
        if (last == PRO_TK_NO_ERROR) {
            used_resource = resource;
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
                if (last == PRO_TK_NO_ERROR) {
                    used_resource = path;
                    return last;
                }
            }
        }
    }

    return last;
}

void OnDialogExit(char *dialog, char *, ProAppData app_data)
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
    const ProError st = ProUICheckbuttonGetState(dialog, const_cast<char *>(comp), &checked);
    if (st != PRO_TK_NO_ERROR) {
        return fallback;
    }
    return checked == PRO_B_TRUE;
}

} // namespace

bool PromptDrawingArrangeOptionsDialog(autobbox::application::DrawingArrangeOptions &options_io,
                                       bool &cancelled,
                                       std::wstring &error_out)
{
    const DrawingArrangeDialogConfig config = {};
    cancelled = false;
    error_out.clear();

    std::string used_resource;
    if (TryCreateDialog(config, used_resource) != PRO_TK_NO_ERROR) {
        cancelled = false;
        error_out = L"无法创建视图整理选项窗口。";
        return false;
    }

    char *dialog = const_cast<char *>(config.dialog_inst_name);
    ProUIDialogTitleSet(dialog, const_cast<wchar_t *>(L"视图整理"));
    ProUILabelTextSet(
        dialog,
        const_cast<char *>(config.prompt_comp),
        const_cast<wchar_t *>(L"请选择整理后的附加操作："));
    ProUICheckbuttonTextSet(
        dialog,
        const_cast<char *>(config.frame_comp),
        const_cast<wchar_t *>(L"添加图框（添加前删除同组旧图框）"));
    ProUICheckbuttonTextSet(
        dialog,
        const_cast<char *>(config.title_comp),
        const_cast<wchar_t *>(L"更新模型名称及模型数量"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.ok_comp), const_cast<wchar_t *>(L"确定"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(config.cancel_comp), const_cast<wchar_t *>(L"取消"));

    SetCheckState(dialog, config.frame_comp, options_io.add_frame);
    SetCheckState(dialog, config.title_comp, options_io.update_model_title);

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
        return false;
    }

    options_io.add_frame = GetCheckState(dialog, config.frame_comp, options_io.add_frame);
    options_io.update_model_title = GetCheckState(dialog, config.title_comp, options_io.update_model_title);
    ProUIDialogDestroy(dialog);
    return true;
}

} // namespace autobbox::ui
