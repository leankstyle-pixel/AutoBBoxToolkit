#include "autobbox/ui/drawing_view_brush_dialog.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/ui/message_dialog.h"

#include <ProUIDialog.h>
#include <ProUILabel.h>
#include <ProUIOptionmenu.h>
#include <ProUIPushbutton.h>
#include <ProUIRadiogroup.h>
#include <ProToolkit.h>
#include <ProUtil.h>

#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <string>
#include <vector>

namespace autobbox::ui {

namespace {

struct BrushDialogConfig {
    const char *dialog_inst_name = "autobbox_drawing_view_brush_inst";
    const char *resource_base_name = "autobbox_drawing_view_brush";
    const char *mode_label_comp = "ModeLabel";
    const char *mode_group_comp = "ModeGroup";
    const char *prompt_label_comp = "PromptLabel";
    const char *source_group_comp = "SourceGroup";
    const char *preset_label_comp = "PresetLabel";
    const char *preset_menu_comp = "PresetMenu";
    const char *ok_comp = "OKBtn";
    const char *cancel_comp = "CancelBtn";
    int status_ok = 1;
    int status_cancel = 0;
};

struct BrushDialogRuntime {
    DrawingViewBrushDialogRequest *request = nullptr;
    const BrushDialogConfig *config = nullptr;
    DrawingViewBrushDialogLogSink log_sink;
};

void LogLine(const DrawingViewBrushDialogLogSink &log_sink, const char *fmt, ...)
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

application::DrawingViewBrushPreset PresetFromName(const std::string &name)
{
    if (name == "back") {
        return application::DrawingViewBrushPreset::Back;
    }
    if (name == "right") {
        return application::DrawingViewBrushPreset::Right;
    }
    if (name == "left") {
        return application::DrawingViewBrushPreset::Left;
    }
    if (name == "top") {
        return application::DrawingViewBrushPreset::Top;
    }
    if (name == "bottom") {
        return application::DrawingViewBrushPreset::Bottom;
    }
    if (name == "iso") {
        return application::DrawingViewBrushPreset::Iso;
    }
    if (name == "iso_z_ne") {
        return application::DrawingViewBrushPreset::IsoZUpNE;
    }
    if (name == "iso_z_nw") {
        return application::DrawingViewBrushPreset::IsoZUpNW;
    }
    if (name == "iso_z_sw") {
        return application::DrawingViewBrushPreset::IsoZUpSW;
    }
    if (name == "iso_z_se") {
        return application::DrawingViewBrushPreset::IsoZUpSE;
    }
    if (name == "iso_y_ne") {
        return application::DrawingViewBrushPreset::IsoYUpNE;
    }
    if (name == "iso_y_nw") {
        return application::DrawingViewBrushPreset::IsoYUpNW;
    }
    if (name == "iso_y_sw") {
        return application::DrawingViewBrushPreset::IsoYUpSW;
    }
    if (name == "iso_y_se") {
        return application::DrawingViewBrushPreset::IsoYUpSE;
    }
    return application::DrawingViewBrushPreset::Front;
}

const char *PresetName(application::DrawingViewBrushPreset preset)
{
    switch (preset) {
    case application::DrawingViewBrushPreset::Back:
        return "back";
    case application::DrawingViewBrushPreset::Right:
        return "right";
    case application::DrawingViewBrushPreset::Left:
        return "left";
    case application::DrawingViewBrushPreset::Top:
        return "top";
    case application::DrawingViewBrushPreset::Bottom:
        return "bottom";
    case application::DrawingViewBrushPreset::Iso:
        return "iso";
    case application::DrawingViewBrushPreset::IsoZUpNE:
        return "iso_z_ne";
    case application::DrawingViewBrushPreset::IsoZUpNW:
        return "iso_z_nw";
    case application::DrawingViewBrushPreset::IsoZUpSW:
        return "iso_z_sw";
    case application::DrawingViewBrushPreset::IsoZUpSE:
        return "iso_z_se";
    case application::DrawingViewBrushPreset::IsoYUpNE:
        return "iso_y_ne";
    case application::DrawingViewBrushPreset::IsoYUpNW:
        return "iso_y_nw";
    case application::DrawingViewBrushPreset::IsoYUpSW:
        return "iso_y_sw";
    case application::DrawingViewBrushPreset::IsoYUpSE:
        return "iso_y_se";
    case application::DrawingViewBrushPreset::Front:
    default:
        return "front";
    }
}

std::wstring PresetMenuLabel(application::DrawingViewBrushPreset preset)
{
    switch (preset) {
    case application::DrawingViewBrushPreset::Back:
        return L"back / -X+Y";
    case application::DrawingViewBrushPreset::Right:
        return L"right / -Z+Y";
    case application::DrawingViewBrushPreset::Left:
        return L"left / +Z+Y";
    case application::DrawingViewBrushPreset::Top:
        return L"top / +X+Z";
    case application::DrawingViewBrushPreset::Bottom:
        return L"bottom / +X-Z";
    case application::DrawingViewBrushPreset::Iso:
        return L"iso";
    case application::DrawingViewBrushPreset::IsoZUpNE:
        return L"\u8f74\u6d4b Z\u5411\u4e0a \u53f3\u524d";
    case application::DrawingViewBrushPreset::IsoZUpNW:
        return L"\u8f74\u6d4b Z\u5411\u4e0a \u5de6\u524d";
    case application::DrawingViewBrushPreset::IsoZUpSW:
        return L"\u8f74\u6d4b Z\u5411\u4e0a \u5de6\u540e";
    case application::DrawingViewBrushPreset::IsoZUpSE:
        return L"\u8f74\u6d4b Z\u5411\u4e0a \u53f3\u540e";
    case application::DrawingViewBrushPreset::IsoYUpNE:
        return L"\u8f74\u6d4b Y\u5411\u4e0a \u53f3\u524d";
    case application::DrawingViewBrushPreset::IsoYUpNW:
        return L"\u8f74\u6d4b Y\u5411\u4e0a \u5de6\u524d";
    case application::DrawingViewBrushPreset::IsoYUpSW:
        return L"\u8f74\u6d4b Y\u5411\u4e0a \u5de6\u540e";
    case application::DrawingViewBrushPreset::IsoYUpSE:
        return L"\u8f74\u6d4b Y\u5411\u4e0a \u53f3\u540e";
    case application::DrawingViewBrushPreset::Front:
    default:
        return L"front / +X+Y";
    }
}

application::DrawingViewBrushPreset PresetFromValue(const std::wstring &value)
{
    for (application::DrawingViewBrushPreset preset : {
             application::DrawingViewBrushPreset::Front,
             application::DrawingViewBrushPreset::Back,
             application::DrawingViewBrushPreset::Right,
             application::DrawingViewBrushPreset::Left,
             application::DrawingViewBrushPreset::Top,
             application::DrawingViewBrushPreset::Bottom,
             application::DrawingViewBrushPreset::Iso,
             application::DrawingViewBrushPreset::IsoZUpNE,
             application::DrawingViewBrushPreset::IsoZUpNW,
             application::DrawingViewBrushPreset::IsoZUpSW,
             application::DrawingViewBrushPreset::IsoZUpSE,
             application::DrawingViewBrushPreset::IsoYUpNE,
             application::DrawingViewBrushPreset::IsoYUpNW,
             application::DrawingViewBrushPreset::IsoYUpSW,
             application::DrawingViewBrushPreset::IsoYUpSE
         }) {
        if (value == PresetMenuLabel(preset) || value == autobbox::common::AToW(PresetName(preset))) {
            return preset;
        }
    }
    return application::DrawingViewBrushPreset::Front;
}

const char *ModeName(application::DrawingViewBrushMode mode)
{
    switch (mode) {
    case application::DrawingViewBrushMode::AxonometricView:
        return "axon";
    case application::DrawingViewBrushMode::MainView:
    default:
        return "main";
    }
}

bool IsAxonometricPreset(application::DrawingViewBrushPreset preset)
{
    switch (preset) {
    case application::DrawingViewBrushPreset::Iso:
    case application::DrawingViewBrushPreset::IsoZUpNE:
    case application::DrawingViewBrushPreset::IsoZUpNW:
    case application::DrawingViewBrushPreset::IsoZUpSW:
    case application::DrawingViewBrushPreset::IsoZUpSE:
    case application::DrawingViewBrushPreset::IsoYUpNE:
    case application::DrawingViewBrushPreset::IsoYUpNW:
    case application::DrawingViewBrushPreset::IsoYUpSW:
    case application::DrawingViewBrushPreset::IsoYUpSE:
        return true;
    default:
        return false;
    }
}

void SelectPreset(char *dialog, const BrushDialogConfig *config, application::DrawingViewBrushPreset preset)
{
    if (dialog == nullptr || config == nullptr) {
        return;
    }
    const std::wstring label = PresetMenuLabel(preset);
    ProUIOptionmenuValueSet(dialog, const_cast<char *>(config->preset_menu_comp), const_cast<wchar_t *>(label.c_str()));
}

void ReadMode(char *dialog, BrushDialogRuntime *runtime)
{
    if (dialog == nullptr || runtime == nullptr || runtime->request == nullptr || runtime->config == nullptr) {
        return;
    }

    int count = 0;
    char **names = nullptr;
    if (ProUIRadiogroupSelectednamesGet(
            dialog,
            const_cast<char *>(runtime->config->mode_group_comp),
            &count,
            &names) == PRO_TK_NO_ERROR &&
        count > 0 &&
        names != nullptr &&
        names[0] != nullptr) {
        const std::string selected(names[0]);
        runtime->request->mode = selected == "axon"
            ? application::DrawingViewBrushMode::AxonometricView
            : application::DrawingViewBrushMode::MainView;
    }
    if (names != nullptr) {
        ProStringarrayFree(names, count);
    }
}

void ReadSource(char *dialog, BrushDialogRuntime *runtime)
{
    if (dialog == nullptr || runtime == nullptr || runtime->request == nullptr || runtime->config == nullptr) {
        return;
    }

    int count = 0;
    char **names = nullptr;
    if (ProUIRadiogroupSelectednamesGet(
            dialog,
            const_cast<char *>(runtime->config->source_group_comp),
            &count,
            &names) == PRO_TK_NO_ERROR &&
        count > 0 &&
        names != nullptr &&
        names[0] != nullptr) {
        const std::string selected(names[0]);
        runtime->request->source = selected == "manual"
            ? application::DrawingViewBrushSource::ManualPreset
            : application::DrawingViewBrushSource::ReferenceView;
    }
    if (names != nullptr) {
        ProStringarrayFree(names, count);
    }
}

void ReadPreset(char *dialog, BrushDialogRuntime *runtime)
{
    if (dialog == nullptr || runtime == nullptr || runtime->request == nullptr || runtime->config == nullptr) {
        return;
    }

    wchar_t *value = nullptr;
    if (ProUIOptionmenuValueGet(
            dialog,
            const_cast<char *>(runtime->config->preset_menu_comp),
            &value) == PRO_TK_NO_ERROR &&
        value != nullptr) {
        runtime->request->preset = PresetFromValue(value);
    }
    if (value != nullptr) {
        ProWstringFree(value);
    }
}

void UpdatePresetAvailability(char *dialog, const BrushDialogRuntime *runtime)
{
    if (dialog == nullptr || runtime == nullptr || runtime->request == nullptr || runtime->config == nullptr) {
        return;
    }
    if (runtime->request->source == application::DrawingViewBrushSource::ManualPreset) {
        ProUIOptionmenuEnable(dialog, const_cast<char *>(runtime->config->preset_menu_comp));
    } else {
        ProUIOptionmenuDisable(dialog, const_cast<char *>(runtime->config->preset_menu_comp));
    }
}

void OnSourceChanged(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<BrushDialogRuntime *>(app_data);
    ReadSource(dialog, runtime);
    if (runtime != nullptr && runtime->request != nullptr && runtime->config != nullptr &&
        runtime->request->mode == application::DrawingViewBrushMode::AxonometricView &&
        runtime->request->source == application::DrawingViewBrushSource::ManualPreset &&
        !IsAxonometricPreset(runtime->request->preset)) {
        runtime->request->preset = application::DrawingViewBrushPreset::IsoZUpNE;
        SelectPreset(dialog, runtime->config, runtime->request->preset);
    }
    UpdatePresetAvailability(dialog, runtime);
}

void OnModeChanged(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<BrushDialogRuntime *>(app_data);
    ReadMode(dialog, runtime);
    if (runtime != nullptr && runtime->request != nullptr && runtime->config != nullptr &&
        runtime->request->mode == application::DrawingViewBrushMode::AxonometricView &&
        runtime->request->source == application::DrawingViewBrushSource::ManualPreset &&
        !IsAxonometricPreset(runtime->request->preset)) {
        runtime->request->preset = application::DrawingViewBrushPreset::IsoZUpNE;
        SelectPreset(dialog, runtime->config, runtime->request->preset);
    }
}

void OnPresetChanged(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<BrushDialogRuntime *>(app_data);
    ReadPreset(dialog, runtime);
}

ProError TryCreateDialog(const BrushDialogConfig &config,
                         const DrawingViewBrushDialogLogSink &log_sink,
                         std::string &used_resource)
{
    used_resource.clear();
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
                "view-brush-dialog-create try resource=%s status=%d",
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
                        "view-brush-dialog-path probe exists=%d path=%s",
                        autobbox::common::FileExistsA(path) ? 1 : 0,
                        path.c_str());
                last = ProUIDialogCreate(
                    const_cast<char *>(config.dialog_inst_name),
                    const_cast<char *>(path.c_str()));
                LogLine(log_sink,
                        "view-brush-dialog-create try resource=%s status=%d",
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

void SetOptionMenu(char *dialog,
                   const char *component,
                   const std::vector<std::string> &names_storage,
                   const std::vector<std::wstring> &labels_storage,
                   int columns,
                   int visible_rows,
                   const std::wstring &selected_label)
{
    if (dialog == nullptr || component == nullptr || names_storage.empty() ||
        names_storage.size() != labels_storage.size()) {
        return;
    }

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
    ProUIOptionmenuVisiblerowsSet(dialog, const_cast<char *>(component), visible_rows);
    ProUIOptionmenuValueSet(dialog, const_cast<char *>(component), const_cast<wchar_t *>(selected_label.c_str()));
}

void SetDialogChineseText(char *dialog, const BrushDialogConfig &config)
{
    if (dialog == nullptr) {
        return;
    }

    ProUILabelTextSet(
        dialog,
        const_cast<char *>(config.mode_label_comp),
        const_cast<wchar_t *>(L"\u5237\u56fe\u6a21\u5f0f\uff1a"));
    ProUILabelTextSet(
        dialog,
        const_cast<char *>(config.prompt_label_comp),
        const_cast<wchar_t *>(L"\u9009\u62e9\u89c6\u5411\u6765\u6e90\uff1a"));
    ProUILabelTextSet(
        dialog,
        const_cast<char *>(config.preset_label_comp),
        const_cast<wchar_t *>(L"\u624b\u52a8\u89c6\u5411\uff1a"));
    ProUIPushbuttonTextSet(
        dialog,
        const_cast<char *>(config.cancel_comp),
        const_cast<wchar_t *>(L"\u53d6\u6d88"));
    ProUIPushbuttonTextSet(
        dialog,
        const_cast<char *>(config.ok_comp),
        const_cast<wchar_t *>(L"\u786e\u5b9a"));
}

void ShowCreateDialogError()
{
    ShowSimpleMessageDialog(
        PROUIMESSAGE_ERROR,
        L"\u89c6\u56fe\u5237",
        L"\u89c6\u56fe\u5237\u7a97\u53e3\u52a0\u8f7d\u5931\u8d25\uff0c\u8bf7\u66f4\u65b0\u63d2\u4ef6\u8d44\u6e90\u6587\u4ef6\u3002");
}

} // namespace

bool PromptDrawingViewBrushOptions(DrawingViewBrushDialogRequest &request,
                                   bool &cancelled,
                                   const DrawingViewBrushDialogLogSink &log_sink)
{
    cancelled = false;
    const BrushDialogConfig config = {};

    std::string used_resource;
    const ProError st_create = TryCreateDialog(config, log_sink, used_resource);
    if (st_create != PRO_TK_NO_ERROR) {
        LogLine(log_sink,
                "view-brush-dialog fail reason=create status=%d",
                static_cast<int>(st_create));
        ShowCreateDialogError();
        return false;
    }
    LogLine(log_sink, "view-brush-dialog using resource=%s", used_resource.c_str());

    BrushDialogRuntime runtime = {};
    runtime.request = &request;
    runtime.config = &config;
    runtime.log_sink = log_sink;

    char *mode_names[] = {
        const_cast<char *>("main"),
        const_cast<char *>("axon")
    };
    wchar_t *mode_labels[] = {
        const_cast<wchar_t *>(L"\u4e3b\u89c6\u56fe\u6a21\u5f0f"),
        const_cast<wchar_t *>(L"\u8f74\u6d4b\u56fe\u6a21\u5f0f")
    };
    char *source_names[] = {
        const_cast<char *>("reference"),
        const_cast<char *>("manual")
    };
    wchar_t *source_labels[] = {
        const_cast<wchar_t *>(L"\u53c2\u8003\u89c6\u56fe"),
        const_cast<wchar_t *>(L"\u624b\u52a8\u9009\u62e9\u89c6\u5411")
    };
    const std::vector<std::string> preset_names = {
        "front", "back", "right", "left", "top", "bottom", "iso",
        "iso_z_ne", "iso_z_nw", "iso_z_sw", "iso_z_se",
        "iso_y_ne", "iso_y_nw", "iso_y_sw", "iso_y_se"
    };
    const std::vector<std::wstring> preset_labels = {
        PresetMenuLabel(application::DrawingViewBrushPreset::Front),
        PresetMenuLabel(application::DrawingViewBrushPreset::Back),
        PresetMenuLabel(application::DrawingViewBrushPreset::Right),
        PresetMenuLabel(application::DrawingViewBrushPreset::Left),
        PresetMenuLabel(application::DrawingViewBrushPreset::Top),
        PresetMenuLabel(application::DrawingViewBrushPreset::Bottom),
        PresetMenuLabel(application::DrawingViewBrushPreset::Iso),
        PresetMenuLabel(application::DrawingViewBrushPreset::IsoZUpNE),
        PresetMenuLabel(application::DrawingViewBrushPreset::IsoZUpNW),
        PresetMenuLabel(application::DrawingViewBrushPreset::IsoZUpSW),
        PresetMenuLabel(application::DrawingViewBrushPreset::IsoZUpSE),
        PresetMenuLabel(application::DrawingViewBrushPreset::IsoYUpNE),
        PresetMenuLabel(application::DrawingViewBrushPreset::IsoYUpNW),
        PresetMenuLabel(application::DrawingViewBrushPreset::IsoYUpSW),
        PresetMenuLabel(application::DrawingViewBrushPreset::IsoYUpSE)
    };

    const char *selected_source =
        request.source == application::DrawingViewBrushSource::ManualPreset ? "manual" : "reference";
    if (request.mode == application::DrawingViewBrushMode::AxonometricView &&
        request.source == application::DrawingViewBrushSource::ManualPreset &&
        !IsAxonometricPreset(request.preset)) {
        request.preset = application::DrawingViewBrushPreset::IsoZUpNE;
    }
    SetRadiogroup(
        const_cast<char *>(config.dialog_inst_name),
        config.mode_group_comp,
        2,
        mode_names,
        mode_labels,
        ModeName(request.mode));
    SetRadiogroup(
        const_cast<char *>(config.dialog_inst_name),
        config.source_group_comp,
        2,
        source_names,
        source_labels,
        selected_source);
    SetOptionMenu(
        const_cast<char *>(config.dialog_inst_name),
        config.preset_menu_comp,
        preset_names,
        preset_labels,
        24,
        10,
        PresetMenuLabel(request.preset));
    SetDialogChineseText(const_cast<char *>(config.dialog_inst_name), config);

    ProUIRadiogroupSelectActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.mode_group_comp),
        OnModeChanged,
        &runtime);
    ProUIRadiogroupSelectActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.source_group_comp),
        OnSourceChanged,
        &runtime);
    ProUIOptionmenuSelectActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.preset_menu_comp),
        OnPresetChanged,
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

    UpdatePresetAvailability(const_cast<char *>(config.dialog_inst_name), &runtime);

    int dialog_status = config.status_cancel;
    const ProError st_act = ProUIDialogActivate(
        const_cast<char *>(config.dialog_inst_name),
        &dialog_status);
    if (st_act != PRO_TK_NO_ERROR || dialog_status != config.status_ok) {
        cancelled = true;
        ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
        LogLine(log_sink,
                "view-brush-dialog cancelled-or-failed activate_status=%d dialog_status=%d",
                static_cast<int>(st_act),
                dialog_status);
        return false;
    }

    ReadMode(const_cast<char *>(config.dialog_inst_name), &runtime);
    ReadSource(const_cast<char *>(config.dialog_inst_name), &runtime);
    ReadPreset(const_cast<char *>(config.dialog_inst_name), &runtime);
    ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
    LogLine(log_sink,
            "view-brush-dialog selected mode=%d source=%d preset=%d",
            static_cast<int>(request.mode),
            static_cast<int>(request.source),
            static_cast<int>(request.preset));
    return true;
}

} // namespace autobbox::ui
