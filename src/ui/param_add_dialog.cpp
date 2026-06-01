#include "autobbox/ui/param_add_dialog.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"

#include <ProToolkit.h>
#include <ProUIDialog.h>
#include <ProUIInputpanel.h>
#include <ProUILabel.h>
#include <ProUIOptionmenu.h>
#include <ProUIPushbutton.h>
#include <ProUtil.h>

#include <vector>

namespace autobbox::ui {

namespace {

struct ParamAddDialogConfig {
    const char *dialog_inst_name = nullptr;
    const char *resource_base_name = nullptr;
    const char *prompt_comp = nullptr;
    const char *name_label_comp = nullptr;
    const char *name_comp = nullptr;
    const char *type_label_comp = nullptr;
    const char *type_comp = nullptr;
    const char *value_label_comp = nullptr;
    const char *value_comp = nullptr;
    const char *ok_comp = nullptr;
    const char *cancel_comp = nullptr;
    int status_ok = 0;
    int status_cancel = 0;
};

ParamAddDialogConfig DefaultParamAddDialogConfig()
{
    ParamAddDialogConfig config = {};
    config.dialog_inst_name = "autobbox_param_add_inst";
    config.resource_base_name = "autobbox_param_add";
    config.prompt_comp = "PromptLabel";
    config.name_label_comp = "NameLabel";
    config.name_comp = "NameInput";
    config.type_label_comp = "TypeLabel";
    config.type_comp = "TypeMenu";
    config.value_label_comp = "ValueLabel";
    config.value_comp = "ValueInput";
    config.ok_comp = "OKBtn";
    config.cancel_comp = "CancelBtn";
    config.status_ok = 1;
    config.status_cancel = 0;
    return config;
}

ProError TryCreateDialog(const ParamAddDialogConfig &config, std::string &used_resource)
{
    used_resource.clear();
    if (config.dialog_inst_name == nullptr || config.resource_base_name == nullptr) {
        return PRO_TK_BAD_INPUTS;
    }

    ProError last = PRO_TK_GENERAL_ERROR;
    const std::string base_name = config.resource_base_name;
    const std::vector<std::string> candidates = {
        base_name,
        base_name + ".res"
    };

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
                text_root + "\\text\\usascii\\resource\\" + base_name + ".res"
            };
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

void OnParamAddDialogOk(char *dialog, char *, ProAppData)
{
    if (dialog != nullptr) {
        ProUIDialogExit(dialog, 1);
    }
}

void OnParamAddDialogCancel(char *dialog, char *, ProAppData)
{
    if (dialog != nullptr) {
        ProUIDialogExit(dialog, 0);
    }
}

} // namespace

bool PromptParamAddDialog(core::ParamAddSpec &spec_io,
                          bool &cancelled,
                          std::wstring &error_out,
                          const ParamAddDialogCallbacks &callbacks)
{
    const ParamAddDialogConfig config = DefaultParamAddDialogConfig();
    cancelled = false;
    error_out.clear();

    std::string used_resource;
    if (TryCreateDialog(config, used_resource) != PRO_TK_NO_ERROR) {
        cancelled = true;
        error_out = L"鏂板缓鍙傛暟绐楀彛鍔犺浇澶辫触銆?";
        return false;
    }

    ProUIDialogTitleSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<wchar_t *>(L"鏂板缓鍙傛暟"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.prompt_comp),
        const_cast<wchar_t *>(L"鎸夊畼鏂规柟寮忓垱寤轰竴涓ā鍨嬬骇鍙傛暟銆?"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.name_label_comp),
        const_cast<wchar_t *>(L"鍙傛暟鍚?"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.type_label_comp),
        const_cast<wchar_t *>(L"绫诲瀷"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.value_label_comp),
        const_cast<wchar_t *>(L"鍊?"));
    ProUIInputpanelColumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.name_comp),
        36);
    ProUIInputpanelColumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.value_comp),
        36);

    std::vector<std::string> type_names_storage = { "STRING", "INTEGER", "DOUBLE", "BOOLEAN" };
    std::vector<std::wstring> type_labels_storage = { L"瀛楃涓?", L"鏁存暟", L"瀹炴暟", L"甯冨皵" };
    std::vector<char *> type_names;
    std::vector<wchar_t *> type_labels;
    for (std::string &name : type_names_storage) {
        type_names.push_back(const_cast<char *>(name.c_str()));
    }
    for (std::wstring &label : type_labels_storage) {
        type_labels.push_back(const_cast<wchar_t *>(label.c_str()));
    }
    ProUIOptionmenuNamesSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.type_comp),
        static_cast<int>(type_names.size()),
        type_names.data());
    ProUIOptionmenuLabelsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.type_comp),
        static_cast<int>(type_labels.size()),
        type_labels.data());
    ProUIOptionmenuColumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.type_comp),
        18);
    ProUIOptionmenuVisiblerowsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.type_comp),
        4);

    ProUIInputpanelValueSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.name_comp),
        const_cast<wchar_t *>(spec_io.name.c_str()));
    if (callbacks.param_add_type_menu_label) {
        ProUIOptionmenuValueSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.type_comp),
            const_cast<wchar_t *>(callbacks.param_add_type_menu_label(spec_io.type)));
    }
    ProUIInputpanelValueSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.value_comp),
        const_cast<wchar_t *>(spec_io.raw_value.c_str()));

    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.ok_comp),
        const_cast<wchar_t *>(L"纭畾"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.cancel_comp),
        const_cast<wchar_t *>(L"鍙栨秷"));
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.ok_comp),
        OnParamAddDialogOk,
        nullptr);
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.cancel_comp),
        OnParamAddDialogCancel,
        nullptr);
    ProUIDialogCloseActionSet(
        const_cast<char *>(config.dialog_inst_name),
        OnParamAddDialogCancel,
        nullptr);
    ProUIDialogDefaultbuttonSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.ok_comp));

    int dlg_status = config.status_cancel;
    const ProError st_act = ProUIDialogActivate(
        const_cast<char *>(config.dialog_inst_name),
        &dlg_status);
    if (st_act != PRO_TK_NO_ERROR || dlg_status != config.status_ok) {
        cancelled = true;
        ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
        return false;
    }

    wchar_t *name_value = nullptr;
    wchar_t *type_value = nullptr;
    wchar_t *raw_value = nullptr;
    if (ProUIInputpanelValueGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.name_comp),
            &name_value) != PRO_TK_NO_ERROR ||
        ProUIOptionmenuValueGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.type_comp),
            &type_value) != PRO_TK_NO_ERROR ||
        ProUIInputpanelValueGet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.value_comp),
            &raw_value) != PRO_TK_NO_ERROR ||
        name_value == nullptr || type_value == nullptr || raw_value == nullptr) {
        if (name_value != nullptr) {
            ProWstringFree(name_value);
        }
        if (type_value != nullptr) {
            ProWstringFree(type_value);
        }
        if (raw_value != nullptr) {
            ProWstringFree(raw_value);
        }
        ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
        cancelled = true;
        return false;
    }

    core::ParamAddSpec parsed_spec = spec_io;
    const bool parsed_ok = callbacks.parse_param_add_dialog_spec &&
                           callbacks.parse_param_add_dialog_spec(
                               name_value,
                               type_value,
                               raw_value,
                               parsed_spec,
                               error_out);
    ProWstringFree(name_value);
    ProWstringFree(type_value);
    ProWstringFree(raw_value);
    ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));

    if (!parsed_ok) {
        return false;
    }

    spec_io = parsed_spec;
    return true;
}

} // namespace autobbox::ui
