#include "autobbox/ui/balloon_arrange_dialog.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/creo/parameter_api.h"

#include <ProUIDialog.h>
#include <ProUIInputpanel.h>
#include <ProUILabel.h>
#include <ProUIOptionmenu.h>
#include <ProUIPushbutton.h>
#include <ProToolkit.h>
#include <ProUtil.h>

#include <vector>

namespace autobbox::ui {

namespace {

constexpr const wchar_t *kModelNameLabel = L"真实模型名称";
constexpr const wchar_t *kParameterLabel = L"模型参数值";

struct BalloonArrangeDialogConfig {
    const char *dialog_inst_name = "autobbox_balloon_arrange_inst";
    const char *resource_base_name = "autobbox_balloon_arrange";
    const char *prompt_comp = "PromptLabel";
    const char *source_label_comp = "SourceLabel";
    const char *source_comp = "SourceMenu";
    const char *param_label_comp = "ParamLabel";
    const char *param_comp = "ParamInput";
    const char *ok_comp = "OKBtn";
    const char *cancel_comp = "CancelBtn";
    int status_ok = 1;
    int status_cancel = 0;
};

ProError TryCreateDialog(const BalloonArrangeDialogConfig &config, std::string &used_resource)
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

void OnDialogOk(char *dialog, char *, ProAppData)
{
    if (dialog != nullptr) {
        ProUIDialogExit(dialog, 1);
    }
}

void OnDialogCancel(char *dialog, char *, ProAppData)
{
    if (dialog != nullptr) {
        ProUIDialogExit(dialog, 0);
    }
}

} // namespace

bool PromptBalloonArrangeOptionsDialog(autobbox::application::BalloonArrangeOptions &options_io,
                                       bool &cancelled,
                                       std::wstring &error_out)
{
    const BalloonArrangeDialogConfig config = {};
    cancelled = false;
    error_out.clear();

    std::string used_resource;
    if (TryCreateDialog(config, used_resource) != PRO_TK_NO_ERROR) {
        cancelled = true;
        error_out = L"无法创建球标整理选项窗口。";
        return false;
    }

    ProUIDialogTitleSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<wchar_t *>(L"球标整理"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.prompt_comp),
        const_cast<wchar_t *>(L"请选择 BOM 表中的任意单元格；将按该 BOM 表重复区域里的元件生成自定义 note 球标。"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.source_label_comp),
        const_cast<wchar_t *>(L"球标内容"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_label_comp),
        const_cast<wchar_t *>(L"参数名"));
    ProUIInputpanelColumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_comp),
        32);

    std::vector<std::string> source_names_storage = {"MODEL_NAME", "PARAMETER"};
    std::vector<std::wstring> source_labels_storage = {kModelNameLabel, kParameterLabel};
    std::vector<char *> source_names;
    std::vector<wchar_t *> source_labels;
    for (std::string &name : source_names_storage) {
        source_names.push_back(const_cast<char *>(name.c_str()));
    }
    for (std::wstring &label : source_labels_storage) {
        source_labels.push_back(const_cast<wchar_t *>(label.c_str()));
    }
    ProUIOptionmenuNamesSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.source_comp),
        static_cast<int>(source_names.size()),
        source_names.data());
    ProUIOptionmenuLabelsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.source_comp),
        static_cast<int>(source_labels.size()),
        source_labels.data());
    ProUIOptionmenuColumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.source_comp),
        18);
    ProUIOptionmenuVisiblerowsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.source_comp),
        2);
    ProUIOptionmenuValueSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.source_comp),
        const_cast<wchar_t *>(options_io.label_source == autobbox::application::BalloonArrangeLabelSource::ModelName
                                  ? kModelNameLabel
                                  : kParameterLabel));
    ProUIInputpanelValueSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.param_comp),
        const_cast<wchar_t *>(options_io.parameter_name.c_str()));

    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.ok_comp),
        const_cast<wchar_t *>(L"确定"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.cancel_comp),
        const_cast<wchar_t *>(L"取消"));
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.ok_comp),
        OnDialogOk,
        nullptr);
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.cancel_comp),
        OnDialogCancel,
        nullptr);
    ProUIDialogCloseActionSet(
        const_cast<char *>(config.dialog_inst_name),
        OnDialogCancel,
        nullptr);
    ProUIDialogDefaultbuttonSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.ok_comp));

    int dlg_status = config.status_cancel;
    const ProError st_activate = ProUIDialogActivate(
        const_cast<char *>(config.dialog_inst_name),
        &dlg_status);
    if (st_activate != PRO_TK_NO_ERROR || dlg_status != config.status_ok) {
        cancelled = true;
        ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
        return false;
    }

    wchar_t *source_value = nullptr;
    wchar_t *param_value = nullptr;
    if (ProUIOptionmenuValueGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.source_comp),
            &source_value) != PRO_TK_NO_ERROR ||
        ProUIInputpanelValueGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.param_comp),
            &param_value) != PRO_TK_NO_ERROR ||
        source_value == nullptr || param_value == nullptr) {
        if (source_value != nullptr) {
            ProWstringFree(source_value);
        }
        if (param_value != nullptr) {
            ProWstringFree(param_value);
        }
        ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
        cancelled = true;
        return false;
    }

    const std::wstring source(source_value);
    const std::wstring param = autobbox::creo::NormalizeParameterName(param_value);
    ProWstringFree(source_value);
    ProWstringFree(param_value);
    ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));

    if (source == kModelNameLabel || source == L"MODEL_NAME") {
        options_io.label_source = autobbox::application::BalloonArrangeLabelSource::ModelName;
        return true;
    }

    if (param.empty()) {
        error_out = L"选择“模型参数值”时必须填写参数名。";
        return false;
    }
    options_io.label_source = autobbox::application::BalloonArrangeLabelSource::ParameterValue;
    options_io.parameter_name = param;
    options_io.fallback_to_model_name = true;
    return true;
}

} // namespace autobbox::ui
