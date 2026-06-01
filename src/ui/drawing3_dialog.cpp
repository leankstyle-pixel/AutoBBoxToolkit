#include "autobbox/ui/drawing3_dialog.h"

#include "autobbox/application/drawing3_views.h"
#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/ui/message_dialog.h"

#include <ProArray.h>
#include <ProSimprep.h>
#include <ProUICheckbutton.h>
#include <ProUIDialog.h>
#include <ProUILabel.h>
#include <ProUIMessage.h>
#include <ProUIOptionmenu.h>
#include <ProUIPushbutton.h>
#include <ProUITable.h>
#include <ProToolkit.h>
#include <ProUtil.h>
#include <ProGraphic.h>
#include <ProSolid.h>
#include <ProWindows.h>

#include <algorithm>
#include <array>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <cwchar>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace autobbox::ui {

namespace {

struct Drawing3DialogConfig {
    const char *dialog_inst_name = nullptr;
    const char *resource_base_name = nullptr;
    const char *view_prompt_comp = nullptr;
    const char *front_comp = nullptr;
    const char *right_comp = nullptr;
    const char *left_comp = nullptr;
    const char *top_comp = nullptr;
    const char *bottom_comp = nullptr;
    const char *back_comp = nullptr;
    const char *iso_comp = nullptr;
    const char *prompt_comp = nullptr;
    const char *simprep_label_comp = nullptr;
    const char *simprep_menu_comp = nullptr;
    const char *list_comp = nullptr;
    const char *base_list_check_comp = nullptr;
    const char *select_all_comp = nullptr;
    const char *clear_comp = nullptr;
    const char *ok_comp = nullptr;
    const char *cancel_comp = nullptr;
    const char *quick_comp = nullptr;
    const char *frame_label_comp = nullptr;
    const char *frame_menu_comp = nullptr;
    int status_ok = 0;
    int status_cancel = 0;
};

Drawing3DialogConfig DefaultDrawing3DialogConfig()
{
    Drawing3DialogConfig config = {};
    config.dialog_inst_name = "autobbox_dwg3_pick_inst";
    config.resource_base_name = "autobbox_dwg3_pick";
    config.view_prompt_comp = "ViewPromptLabel";
    config.front_comp = "FrontCheck";
    config.right_comp = "RightCheck";
    config.left_comp = "LeftCheck";
    config.top_comp = "TopCheck";
    config.bottom_comp = "BottomCheck";
    config.back_comp = "BackCheck";
    config.iso_comp = "IsoCheck";
    config.prompt_comp = "PromptLabel";
    config.simprep_label_comp = "SimprepLabel";
    config.simprep_menu_comp = "SimprepMenu";
    config.list_comp = "ModelList";
    config.base_list_check_comp = "BaseModelCheck";
    config.select_all_comp = "SelectAllBtn";
    config.clear_comp = "ClearBtn";
    config.ok_comp = "OKBtn";
    config.cancel_comp = "CancelBtn";
    config.quick_comp = "QuickModeCheck";
    config.frame_label_comp = "FrameLabel";
    config.frame_menu_comp = "FrameModeMenu";
    config.status_ok = 1;
    config.status_cancel = 0;
    return config;
}

struct Drawing3DialogState {
    ProSolid root_solid = nullptr;
    std::vector<core::Dwg3SimprepOption> simprep_options;
    std::vector<core::Dwg3Candidate> candidates;
    std::vector<core::Dwg3Candidate> fixed_candidates;
    std::wstring fixed_source_label;
    std::unordered_map<std::string, std::string> checkbox_component_by_item_name;
    int checkbox_render_serial = 0;
    int active_simprep_index = -1;
    bool use_fixed_candidates = false;
    core::Dwg3FrameOptions frame_options;
    const Drawing3DialogLogSink *log_sink = nullptr;
};

struct Drawing3DialogRuntime {
    Drawing3DialogState *state = nullptr;
    const Drawing3DialogConfig *config = nullptr;
};

bool ValidateDialogSelection(char *dialog,
                             const Drawing3DialogState &state,
                             const Drawing3DialogConfig &config,
                             const Drawing3DialogLogSink &log_sink);

void LogLine(const Drawing3DialogLogSink &log_sink, const char *fmt, ...)
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

std::array<core::Dwg3ViewType, core::kDwg3ViewCount> AllDwg3ViewTypes()
{
    return {
        core::Dwg3ViewType::Front,
        core::Dwg3ViewType::Right,
        core::Dwg3ViewType::Left,
        core::Dwg3ViewType::Top,
        core::Dwg3ViewType::Bottom,
        core::Dwg3ViewType::Back,
        core::Dwg3ViewType::Iso
    };
}

core::Dwg3ViewMask DefaultDwg3ViewMask()
{
    return core::Dwg3ViewBit(core::Dwg3ViewType::Front) |
           core::Dwg3ViewBit(core::Dwg3ViewType::Right) |
           core::Dwg3ViewBit(core::Dwg3ViewType::Iso);
}

const wchar_t *ViewLabel(core::Dwg3ViewType type)
{
    switch (type) {
    case core::Dwg3ViewType::Front:
        return L"\u4e3b\u89c6";
    case core::Dwg3ViewType::Right:
        return L"\u53f3\u89c6";
    case core::Dwg3ViewType::Left:
        return L"\u5de6\u89c6";
    case core::Dwg3ViewType::Top:
        return L"\u4ef0\u89c6";
    case core::Dwg3ViewType::Bottom:
        return L"\u4fef\u89c6";
    case core::Dwg3ViewType::Back:
        return L"\u540e\u89c6";
    case core::Dwg3ViewType::Iso:
        return L"\u8f74\u6d4b\u56fe";
    default:
        return L"\u89c6\u56fe";
    }
}

const char *DialogCheckComp(core::Dwg3ViewType type, const Drawing3DialogConfig &config)
{
    switch (type) {
    case core::Dwg3ViewType::Front:
        return config.front_comp;
    case core::Dwg3ViewType::Right:
        return config.right_comp;
    case core::Dwg3ViewType::Left:
        return config.left_comp;
    case core::Dwg3ViewType::Top:
        return config.top_comp;
    case core::Dwg3ViewType::Bottom:
        return config.bottom_comp;
    case core::Dwg3ViewType::Back:
        return config.back_comp;
    case core::Dwg3ViewType::Iso:
        return config.iso_comp;
    default:
        return config.front_comp;
    }
}

void OnDialogExit(char *dialog, char *, ProAppData app_data)
{
    if (dialog != nullptr) {
        ProUIDialogExit(dialog, static_cast<int>(reinterpret_cast<std::intptr_t>(app_data)));
    }
}

void SetDialogListState(char *dialog,
                        const Drawing3DialogState *state,
                        ProUIMixedState item_state,
                        const Drawing3DialogConfig &config)
{
    if (dialog == nullptr || state == nullptr || config.list_comp == nullptr) {
        return;
    }

    for (const core::Dwg3Candidate &cand : state->candidates) {
        const auto it = state->checkbox_component_by_item_name.find(cand.item_name);
        if (it == state->checkbox_component_by_item_name.end()) {
            continue;
        }

        if (item_state == PROUI_SET) {
            ProUICheckbuttonSet(dialog, const_cast<char *>(it->second.c_str()));
        } else {
            ProUICheckbuttonUnset(dialog, const_cast<char *>(it->second.c_str()));
        }
    }
}

std::vector<std::string> GetDialogSelectedRowNames(char *dialog,
                                                   const Drawing3DialogState *state,
                                                   const Drawing3DialogConfig &config)
{
    std::vector<std::string> selected;
    if (dialog == nullptr || state == nullptr || config.list_comp == nullptr) {
        return selected;
    }

    selected.reserve(state->candidates.size());
    for (const core::Dwg3Candidate &cand : state->candidates) {
        const auto it = state->checkbox_component_by_item_name.find(cand.item_name);
        if (it == state->checkbox_component_by_item_name.end()) {
            continue;
        }

        ProBoolean checked = PRO_B_FALSE;
        if (ProUICheckbuttonGetState(dialog, const_cast<char *>(it->second.c_str()), &checked) ==
                PRO_TK_NO_ERROR &&
            checked == PRO_B_TRUE) {
            selected.push_back(cand.item_name);
        }
    }
    return selected;
}

void SetDialogSelectedRows(char *dialog,
                           const Drawing3DialogState *state,
                           const std::vector<std::string> &selected_row_names,
                           const Drawing3DialogConfig &config)
{
    if (dialog == nullptr || state == nullptr || config.list_comp == nullptr) {
        return;
    }

    for (const core::Dwg3Candidate &cand : state->candidates) {
        const auto it = state->checkbox_component_by_item_name.find(cand.item_name);
        if (it == state->checkbox_component_by_item_name.end()) {
            continue;
        }

        if (std::find(selected_row_names.begin(), selected_row_names.end(), cand.item_name) !=
            selected_row_names.end()) {
            ProUICheckbuttonSet(dialog, const_cast<char *>(it->second.c_str()));
        } else {
            ProUICheckbuttonUnset(dialog, const_cast<char *>(it->second.c_str()));
        }
    }
}

void OnSelectAll(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<Drawing3DialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return;
    }
    SetDialogListState(dialog, runtime->state, PROUI_SET, *runtime->config);
}

void OnClearAll(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<Drawing3DialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return;
    }
    SetDialogListState(dialog, runtime->state, PROUI_UNSET, *runtime->config);
}

void OnConfirm(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<Drawing3DialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return;
    }

    const Drawing3DialogLogSink log_sink =
        (runtime->state->log_sink != nullptr) ? *runtime->state->log_sink : Drawing3DialogLogSink();
    if (!ValidateDialogSelection(dialog, *runtime->state, *runtime->config, log_sink)) {
        return;
    }

    ProUIDialogExit(dialog, runtime->config->status_ok);
}

void SetDialogCheckState(char *dialog, const char *comp, bool checked)
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

bool GetDialogCheckState(char *dialog, const char *comp)
{
    if (dialog == nullptr || comp == nullptr) {
        return false;
    }

    ProBoolean checked = PRO_B_FALSE;
    const ProError st = ProUICheckbuttonGetState(dialog, const_cast<char *>(comp), &checked);
    return st == PRO_TK_NO_ERROR && checked == PRO_B_TRUE;
}

const wchar_t *FrameAutoLabel()
{
    return L"\u81ea\u52a8\u7ed8\u5236\u56fe\u6846";
}

const wchar_t *FrameSymbolPlainLabel()
{
    return L"\u7b26\u53f7\uff1aa4h_4x2_01";
}

const wchar_t *FrameSymbolTitleLabel()
{
    return L"\u7b26\u53f7\uff1aa4h_4x2_01_t";
}

core::Dwg3FrameOptions FrameOptionsFromMenuValue(const wchar_t *value)
{
    core::Dwg3FrameOptions options;
    if (value == nullptr || value[0] == L'\0' || std::wcscmp(value, FrameAutoLabel()) == 0 ||
        std::wcscmp(value, L"AUTO") == 0) {
        return options;
    }

    options.mode = core::Dwg3FrameMode::Symbol;
    options.symbol_version = 276;
    if (std::wcscmp(value, FrameSymbolTitleLabel()) == 0 ||
        std::wcscmp(value, L"A4H_4X2_01_T") == 0) {
        options.symbol_label = FrameSymbolTitleLabel();
        options.symbol_file_name = L"a4h_4x2_01_t.sym";
    } else {
        options.symbol_label = FrameSymbolPlainLabel();
        options.symbol_file_name = L"a4h_4x2_01.sym";
    }
    return options;
}

core::Dwg3FrameOptions GetDialogFrameOptions(char *dialog, const Drawing3DialogConfig &config)
{
    if (dialog == nullptr || config.frame_menu_comp == nullptr) {
        return core::Dwg3FrameOptions{};
    }

    wchar_t *value = nullptr;
    if (ProUIOptionmenuValueGet(dialog, const_cast<char *>(config.frame_menu_comp), &value) != PRO_TK_NO_ERROR ||
        value == nullptr) {
        if (value != nullptr) {
            ProWstringFree(value);
        }
        return core::Dwg3FrameOptions{};
    }
    core::Dwg3FrameOptions options = FrameOptionsFromMenuValue(value);
    ProWstringFree(value);
    return options;
}

core::Dwg3ViewMask GetDialogSelectedViewMask(char *dialog, const Drawing3DialogConfig &config)
{
    core::Dwg3ViewMask mask = 0;
    for (core::Dwg3ViewType type : AllDwg3ViewTypes()) {
        if (GetDialogCheckState(dialog, DialogCheckComp(type, config))) {
            mask |= core::Dwg3ViewBit(type);
        }
    }
    return mask;
}

bool ValidateDialogSelection(char *dialog,
                             const Drawing3DialogState &state,
                             const Drawing3DialogConfig &config,
                             const Drawing3DialogLogSink &log_sink)
{
    if (dialog == nullptr) {
        return false;
    }

    if (state.candidates.empty()) {
        LogLine(log_sink, "dwg3-dialog-validate failed reason=no-candidates");
        ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u5efa\u89c6\u56fe",
            L"\u5f53\u524d\u7b80\u5316\u8868\u793a\u4e0b\u6ca1\u6709\u53ef\u5efa\u89c6\u56fe\u7684\u6a21\u578b\uff0c\u8bf7\u5148\u5207\u6362\u7b80\u5316\u8868\u793a\u540e\u518d\u8bd5\u3002");
        return false;
    }

    if (GetDialogSelectedRowNames(dialog, &state, config).empty()) {
        LogLine(log_sink, "dwg3-dialog-validate failed reason=no-model-selected");
        ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u5efa\u89c6\u56fe",
            L"\u8bf7\u81f3\u5c11\u52fe\u9009\u4e00\u4e2a\u9700\u8981\u5efa\u89c6\u56fe\u7684\u6a21\u578b\u3002");
        return false;
    }

    if (GetDialogSelectedViewMask(dialog, config) == 0) {
        LogLine(log_sink, "dwg3-dialog-validate failed reason=no-view-selected");
        ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u5efa\u89c6\u56fe",
            L"\u8bf7\u81f3\u5c11\u52fe\u9009\u4e00\u4e2a\u9700\u8981\u521b\u5efa\u7684\u89c6\u56fe\u65b9\u5411\u3002");
        return false;
    }

    return true;
}

int FindSimprepOptionIndexByLabel(const Drawing3DialogState &state, const wchar_t *label)
{
    if (label == nullptr) {
        return -1;
    }

    for (size_t i = 0; i < state.simprep_options.size(); ++i) {
        if (state.simprep_options[i].display_label == label) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void PopulateDialogModelList(char *dialog,
                             Drawing3DialogState *state,
                             const Drawing3DialogConfig &config)
{
    if (dialog == nullptr || state == nullptr || config.list_comp == nullptr) {
        return;
    }

    ++state->checkbox_render_serial;
    state->checkbox_component_by_item_name.clear();

    std::vector<std::string> column_names_storage = { "USE", "MODEL_NAME", "COMMON_NAME", "QTY" };
    std::vector<std::wstring> column_labels_storage = { L"", L"\u6a21\u578b\u540d\u79f0", L"PTC_COMMON_NAME", L"\u6570\u91cf" };
    std::vector<int> column_widths = { 4, 22, 22, 8 };
    std::vector<int> column_resizings = { 0, 3, 2, 1 };
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
    row_names.reserve(state->candidates.size());
    row_labels.reserve(state->candidates.size());
    for (core::Dwg3Candidate &cand : state->candidates) {
        row_names.push_back(const_cast<char *>(cand.item_name.c_str()));
        row_labels.push_back(const_cast<wchar_t *>(L""));
    }

    ProUITableColumnnamesSet(
        dialog,
        const_cast<char *>(config.list_comp),
        static_cast<int>(column_names.size()),
        column_names.data());
    ProUITableColumnlabelsSet(
        dialog,
        const_cast<char *>(config.list_comp),
        static_cast<int>(column_labels.size()),
        column_labels.data());
    ProUITableColumnwidthsSet(
        dialog,
        const_cast<char *>(config.list_comp),
        static_cast<int>(column_widths.size()),
        column_widths.data());
    ProUITableColumnresizingsSet(
        dialog,
        const_cast<char *>(config.list_comp),
        static_cast<int>(column_resizings.size()),
        column_resizings.data());
    ProUITableRownamesSet(
        dialog,
        const_cast<char *>(config.list_comp),
        static_cast<int>(row_names.size()),
        row_names.empty() ? nullptr : row_names.data());
    ProUITableRowlabelsSet(
        dialog,
        const_cast<char *>(config.list_comp),
        static_cast<int>(row_labels.size()),
        row_labels.empty() ? nullptr : row_labels.data());
    ProUITableSelectionpolicySet(
        dialog,
        const_cast<char *>(config.list_comp),
        PROUISELPOLICY_NONE);
    ProUITableColumnselectionpolicySet(
        dialog,
        const_cast<char *>(config.list_comp),
        PROUISELPOLICY_NONE);
    ProUITableVisiblerowsSet(
        dialog,
        const_cast<char *>(config.list_comp),
        std::min(12, std::max(4, static_cast<int>(state->candidates.size()))));
    ProUITableMinrowsSet(
        dialog,
        const_cast<char *>(config.list_comp),
        std::min(12, std::max(4, static_cast<int>(state->candidates.size()))));
    ProUITableShowgridSet(
        dialog,
        const_cast<char *>(config.list_comp),
        PRO_B_TRUE);
    ProUITableLockedcolumnsSet(
        dialog,
        const_cast<char *>(config.list_comp),
        0);

    for (core::Dwg3Candidate &cand : state->candidates) {
        const std::wstring common_name = cand.common_name.empty() ? L"-" : cand.common_name;
        const std::wstring qty_label = std::to_wstring(std::max(1, cand.occurrence_count));
        char check_name[48] = {0};
        std::snprintf(check_name,
                      sizeof(check_name),
                      "mdlchk_%d_%s",
                      state->checkbox_render_serial,
                      cand.item_name.c_str());
        state->checkbox_component_by_item_name[cand.item_name] = check_name;
        ProUITableCellComponentCopy(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.list_comp),
            const_cast<char *>(cand.item_name.c_str()),
            const_cast<char *>("USE"),
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.base_list_check_comp),
            const_cast<char *>(state->checkbox_component_by_item_name[cand.item_name].c_str()));
        ProUICheckbuttonTextSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(state->checkbox_component_by_item_name[cand.item_name].c_str()),
            const_cast<wchar_t *>(L""));
        ProUITableCellLabelSet(
            dialog,
            const_cast<char *>(config.list_comp),
            const_cast<char *>(cand.item_name.c_str()),
            const_cast<char *>("MODEL_NAME"),
            const_cast<wchar_t *>(cand.model_name.c_str()));
        ProUITableCellLabelSet(
            dialog,
            const_cast<char *>(config.list_comp),
            const_cast<char *>(cand.item_name.c_str()),
            const_cast<char *>("COMMON_NAME"),
            const_cast<wchar_t *>(common_name.c_str()));
        ProUITableCellLabelSet(
            dialog,
            const_cast<char *>(config.list_comp),
            const_cast<char *>(cand.item_name.c_str()),
            const_cast<char *>("QTY"),
            const_cast<wchar_t *>(qty_label.c_str()));
    }
}

void RefreshDialogCandidates(char *dialog,
                             Drawing3DialogState *state,
                             const Drawing3DialogConfig &config,
                             const Drawing3DialogLogSink &log_sink,
                             const std::vector<std::string> *selected_row_names = nullptr)
{
    if (dialog == nullptr || state == nullptr) {
        return;
    }

    const core::Dwg3SimprepOption *active_option = nullptr;
    if (state->use_fixed_candidates) {
        state->candidates = state->fixed_candidates;
    } else {
        if (state->active_simprep_index >= 0 &&
            state->active_simprep_index < static_cast<int>(state->simprep_options.size())) {
            active_option = &state->simprep_options[static_cast<size_t>(state->active_simprep_index)];
        }

        state->candidates =
            autobbox::application::CollectDrawingViewCandidatesForSimprep(state->root_solid, active_option);
    }

    PopulateDialogModelList(dialog, state, config);
    if (selected_row_names != nullptr && !selected_row_names->empty()) {
        SetDialogSelectedRows(dialog, state, *selected_row_names, config);
        if (GetDialogSelectedRowNames(dialog, state, config).empty() && !state->candidates.empty()) {
            SetDialogListState(dialog, state, PROUI_SET, config);
        }
    } else {
        SetDialogListState(dialog, state, PROUI_SET, config);
    }

    const int selected_count = static_cast<int>(GetDialogSelectedRowNames(dialog, state, config).size());

    if (state->use_fixed_candidates) {
        LogLine(log_sink,
                "dwg3-dialog-fixed-source candidates=%d selected=%d source=%s",
                static_cast<int>(state->candidates.size()),
                selected_count,
                autobbox::common::WToA(state->fixed_source_label.c_str()).c_str());
    } else if (active_option != nullptr) {
        LogLine(log_sink,
                "dwg3-dialog-simprep label=%s candidates=%d selected=%d",
                autobbox::common::WToA(active_option->display_label.c_str()).c_str(),
                static_cast<int>(state->candidates.size()),
                selected_count);
    } else {
        LogLine(log_sink,
                "dwg3-dialog-simprep candidates=%d selected=%d",
                static_cast<int>(state->candidates.size()),
                selected_count);
    }
}

void OnSimprepChanged(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<Drawing3DialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return;
    }
    if (runtime->state->use_fixed_candidates) {
        return;
    }

    wchar_t *value = nullptr;
    if (ProUIOptionmenuValueGet(
            dialog,
            const_cast<char *>(runtime->config->simprep_menu_comp),
            &value) != PRO_TK_NO_ERROR ||
        value == nullptr) {
        if (value != nullptr) {
            ProWstringFree(value);
        }
        return;
    }

    const int option_index = FindSimprepOptionIndexByLabel(*runtime->state, value);
    ProWstringFree(value);
    if (option_index < 0) {
        return;
    }

    const std::vector<std::string> selected_row_names =
        GetDialogSelectedRowNames(dialog, runtime->state, *runtime->config);
    runtime->state->active_simprep_index = option_index;
    RefreshDialogCandidates(
        dialog,
        runtime->state,
        *runtime->config,
        runtime->state->log_sink != nullptr ? *runtime->state->log_sink : Drawing3DialogLogSink(),
        &selected_row_names);
}

ProError TryCreateDialog(const Drawing3DialogConfig &config,
                         const Drawing3DialogLogSink &log_sink,
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
        LogLine(log_sink, "dwg3-dialog-create try resource=%s status=%d", res.c_str(), static_cast<int>(last));
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
                        "dwg3-dialog-path probe exists=%d path=%s",
                        autobbox::common::FileExistsA(path) ? 1 : 0,
                        path.c_str());
                last = ProUIDialogCreate(
                    const_cast<char *>(config.dialog_inst_name),
                    const_cast<char *>(path.c_str()));
                LogLine(log_sink, "dwg3-dialog-create try resource=%s status=%d", path.c_str(), static_cast<int>(last));
                if (last == PRO_TK_NO_ERROR) {
                    used_resource = path;
                    return last;
                }
            }
        }
    }

    return last;
}

void ShowCreateDialogError()
{
    ProUIMessageButton choice = PRO_UI_MESSAGE_OK;
    ProUIMessageButton *buttons = nullptr;
    if (ProArrayAlloc(0, sizeof(ProUIMessageButton), 1, (ProArray *)&buttons) == PRO_TK_NO_ERROR &&
        buttons != nullptr) {
        ProUIMessageButton button = PRO_UI_MESSAGE_OK;
        ProArrayObjectAdd((ProArray *)&buttons, PRO_VALUE_UNUSED, 1, &button);
        ProUIMessageDialogDisplay(
            PROUIMESSAGE_ERROR,
            const_cast<wchar_t *>(L"\u5efa\u89c6\u56fe"),
            const_cast<wchar_t *>(L"\u5efa\u89c6\u56fe\u7a97\u53e3\u52a0\u8f7d\u5931\u8d25\uff0c\u8bf7\u8054\u7cfb\u7ba1\u7406\u5458\u66f4\u65b0\u63d2\u4ef6\u8d44\u6e90\u6587\u4ef6\u3002"),
            buttons,
            PRO_UI_MESSAGE_OK,
            &choice);
        ProArrayFree((ProArray *)&buttons);
    }
}

} // namespace

bool PromptDrawing3TargetsImpl(ProSolid root_solid,
                               const std::vector<core::Dwg3Candidate> *fixed_candidates,
                               const std::wstring &fixed_source_label,
                               std::vector<core::Dwg3Candidate> &selected,
                               int &candidates_total,
                               core::Dwg3ViewMask &view_mask,
                               bool &quick_mode,
                               core::Dwg3FrameOptions &frame_options,
                               bool &cancelled,
                               const Drawing3DialogLogSink &log_sink)
{
    const Drawing3DialogConfig config = DefaultDrawing3DialogConfig();
    selected.clear();
    candidates_total = 0;
    view_mask = 0;
    quick_mode = true;
    frame_options = core::Dwg3FrameOptions{};
    cancelled = false;
    if (root_solid == nullptr) {
        cancelled = true;
        return false;
    }

    std::string used_resource;
    const ProError st = TryCreateDialog(config, log_sink, used_resource);
    if (st != PRO_TK_NO_ERROR) {
        LogLine(log_sink, "ERROR dwg3-dialog-create failed status=%d", static_cast<int>(st));
        ShowCreateDialogError();
        cancelled = true;
        return false;
    }

    Drawing3DialogState state;
    state.root_solid = root_solid;
    state.use_fixed_candidates = fixed_candidates != nullptr;
    if (state.use_fixed_candidates) {
        state.fixed_candidates = *fixed_candidates;
        state.fixed_source_label = fixed_source_label.empty() ? L"BOM \u8868\u6a21\u578b" : fixed_source_label;
    } else {
        state.simprep_options = autobbox::application::CollectDrawingViewSimprepOptions(root_solid);
    }
    state.log_sink = &log_sink;
    for (size_t i = 0; i < state.simprep_options.size(); ++i) {
        if (state.simprep_options[i].is_active) {
            state.active_simprep_index = static_cast<int>(i);
            break;
        }
    }
    if (state.active_simprep_index < 0 && !state.simprep_options.empty()) {
        state.active_simprep_index = 0;
        state.simprep_options[0].is_active = true;
    }
    std::vector<std::string> simprep_name_storage;
    std::vector<char *> simprep_name_ptrs;
    std::vector<wchar_t *> simprep_label_ptrs;
    simprep_name_storage.reserve(state.simprep_options.size());
    simprep_name_ptrs.reserve(state.simprep_options.size());
    simprep_label_ptrs.reserve(state.simprep_options.size());
    for (size_t i = 0; i < state.simprep_options.size(); ++i) {
        core::Dwg3SimprepOption &option = state.simprep_options[i];
        char name_token[32] = {0};
        std::snprintf(name_token, sizeof(name_token), "simp_%zu", i);
        simprep_name_storage.push_back(name_token);
        simprep_name_ptrs.push_back(const_cast<char *>(simprep_name_storage.back().c_str()));
        simprep_label_ptrs.push_back(const_cast<wchar_t *>(option.display_label.c_str()));
    }

    LogLine(log_sink, "dwg3-dialog-create success resource=%s", used_resource.c_str());
    ProUIDialogTitleSet(const_cast<char *>(config.dialog_inst_name), const_cast<wchar_t *>(L"\u5efa\u89c6\u56fe"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.view_prompt_comp),
        const_cast<wchar_t *>(L"\u52fe\u9009\u672c\u6b21\u9700\u8981\u521b\u5efa\u7684\u89c6\u56fe\uff1a"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.prompt_comp),
        const_cast<wchar_t *>(state.use_fixed_candidates
            ? state.fixed_source_label.c_str()
            : L"\u52fe\u9009\u9700\u8981\u5728\u5f53\u524d\u9875\u521b\u5efa\u89c6\u56fe\u7684\u96f6\u4ef6/\u7ec4\u4ef6\uff1a"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.simprep_label_comp),
        const_cast<wchar_t *>(L"\u7b80\u5316\u8868\u793a"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.select_all_comp),
        const_cast<wchar_t *>(L"\u5168\u9009"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.clear_comp),
        const_cast<wchar_t *>(L"\u6e05\u7a7a"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.ok_comp),
        const_cast<wchar_t *>(L"\u786e\u5b9a"));
    ProUIPushbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.cancel_comp),
        const_cast<wchar_t *>(L"\u53d6\u6d88"));
    ProUICheckbuttonTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.quick_comp),
        const_cast<wchar_t *>(L"\u5feb\u901f\u6a21\u5f0f\uff08\u5148\u5efa\u89c6\u56fe\uff0c\u6700\u540e\u8865\u56fe\u6846/\u6807\u9898\uff09"));
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.frame_label_comp),
        const_cast<wchar_t *>(L"\u56fe\u6846"));

    std::vector<std::string> frame_name_storage = {"AUTO", "A4H_4X2_01", "A4H_4X2_01_T"};
    std::vector<std::wstring> frame_label_storage = {
        FrameAutoLabel(),
        FrameSymbolPlainLabel(),
        FrameSymbolTitleLabel()
    };
    std::vector<char *> frame_name_ptrs;
    std::vector<wchar_t *> frame_label_ptrs;
    frame_name_ptrs.reserve(frame_name_storage.size());
    frame_label_ptrs.reserve(frame_label_storage.size());
    for (std::string &name : frame_name_storage) {
        frame_name_ptrs.push_back(const_cast<char *>(name.c_str()));
    }
    for (std::wstring &label : frame_label_storage) {
        frame_label_ptrs.push_back(const_cast<wchar_t *>(label.c_str()));
    }
    ProUIOptionmenuNamesSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.frame_menu_comp),
        static_cast<int>(frame_name_ptrs.size()),
        frame_name_ptrs.data());
    ProUIOptionmenuLabelsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.frame_menu_comp),
        static_cast<int>(frame_label_ptrs.size()),
        frame_label_ptrs.data());
    ProUIOptionmenuColumnsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.frame_menu_comp),
        24);
    ProUIOptionmenuVisiblerowsSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.frame_menu_comp),
        static_cast<int>(frame_label_ptrs.size()));
    ProUIOptionmenuValueSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.frame_menu_comp),
        const_cast<wchar_t *>(FrameAutoLabel()));

    if (state.use_fixed_candidates) {
        ProUILabelHide(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.simprep_label_comp));
        ProUIOptionmenuHide(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.simprep_menu_comp));
    } else {
        ProUIOptionmenuNamesSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.simprep_menu_comp),
            static_cast<int>(simprep_name_ptrs.size()),
            simprep_name_ptrs.empty() ? nullptr : simprep_name_ptrs.data());
        ProUIOptionmenuLabelsSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.simprep_menu_comp),
            static_cast<int>(simprep_label_ptrs.size()),
            simprep_label_ptrs.empty() ? nullptr : simprep_label_ptrs.data());
        ProUIOptionmenuColumnsSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.simprep_menu_comp),
            24);
        ProUIOptionmenuVisiblerowsSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(config.simprep_menu_comp),
            std::min(8, std::max(1, static_cast<int>(state.simprep_options.size()))));
        if (state.active_simprep_index >= 0 &&
            state.active_simprep_index < static_cast<int>(state.simprep_options.size())) {
            ProUIOptionmenuValueSet(
                const_cast<char *>(config.dialog_inst_name),
                const_cast<char *>(config.simprep_menu_comp),
                const_cast<wchar_t *>(
                    state.simprep_options[static_cast<size_t>(state.active_simprep_index)].display_label.c_str()));
        }
    }

    Drawing3DialogRuntime runtime;
    runtime.state = &state;
    runtime.config = &config;

    for (core::Dwg3ViewType type : AllDwg3ViewTypes()) {
        ProUICheckbuttonEnableResizing(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(DialogCheckComp(type, config)));
        ProUICheckbuttonButtonstyleSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(DialogCheckComp(type, config)),
            PROUIBUTTONSTYLE_TOGGLE);
        ProUICheckbuttonTextSet(
            const_cast<char *>(config.dialog_inst_name),
            const_cast<char *>(DialogCheckComp(type, config)),
            const_cast<wchar_t *>(ViewLabel(type)));
    }

    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.select_all_comp),
        OnSelectAll,
        &runtime);
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.clear_comp),
        OnClearAll,
        &runtime);
    ProUIOptionmenuSelectActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.simprep_menu_comp),
        OnSimprepChanged,
        &runtime);
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.ok_comp),
        OnConfirm,
        &runtime);
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

    RefreshDialogCandidates(const_cast<char *>(config.dialog_inst_name), &state, config, log_sink);
    const core::Dwg3ViewMask default_mask = DefaultDwg3ViewMask();
    for (core::Dwg3ViewType type : AllDwg3ViewTypes()) {
        SetDialogCheckState(
            const_cast<char *>(config.dialog_inst_name),
            DialogCheckComp(type, config),
            (default_mask & core::Dwg3ViewBit(type)) != 0);
    }
    SetDialogCheckState(const_cast<char *>(config.dialog_inst_name), config.quick_comp, true);

    int dialog_status = config.status_cancel;
    const ProError st_act = ProUIDialogActivate(
        const_cast<char *>(config.dialog_inst_name),
        &dialog_status);
    if (st_act != PRO_TK_NO_ERROR || dialog_status != config.status_ok) {
        cancelled = true;
        ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
        return false;
    }

    candidates_total = static_cast<int>(state.candidates.size());
    const std::vector<std::string> selected_row_names =
        GetDialogSelectedRowNames(const_cast<char *>(config.dialog_inst_name), &state, config);
    for (const std::string &selected_row : selected_row_names) {
        for (const core::Dwg3Candidate &cand : state.candidates) {
            if (cand.item_name == selected_row) {
                selected.push_back(cand);
                break;
            }
        }
    }

    view_mask = GetDialogSelectedViewMask(const_cast<char *>(config.dialog_inst_name), config);
    quick_mode = GetDialogCheckState(const_cast<char *>(config.dialog_inst_name), config.quick_comp);
    frame_options = GetDialogFrameOptions(const_cast<char *>(config.dialog_inst_name), config);
    LogLine(log_sink,
            "dwg3-dialog-frame mode=%s symbol=%s",
            frame_options.mode == core::Dwg3FrameMode::Symbol ? "symbol" : "auto",
            autobbox::common::WToA(frame_options.symbol_file_name.c_str()).c_str());

    ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
    return true;
}

bool PromptDrawing3Targets(ProSolid root_solid,
                           std::vector<core::Dwg3Candidate> &selected,
                           int &candidates_total,
                           core::Dwg3ViewMask &view_mask,
                           bool &quick_mode,
                           core::Dwg3FrameOptions &frame_options,
                           bool &cancelled,
                           const Drawing3DialogLogSink &log_sink)
{
    return PromptDrawing3TargetsImpl(
        root_solid,
        nullptr,
        std::wstring(),
        selected,
        candidates_total,
        view_mask,
        quick_mode,
        frame_options,
        cancelled,
        log_sink);
}

bool PromptDrawing3TargetsFromCandidates(ProSolid root_solid,
                                         const std::vector<core::Dwg3Candidate> &candidates,
                                         const std::wstring &source_label,
                                         std::vector<core::Dwg3Candidate> &selected,
                                         int &candidates_total,
                                         core::Dwg3ViewMask &view_mask,
                                         bool &quick_mode,
                                         core::Dwg3FrameOptions &frame_options,
                                         bool &cancelled,
                                         const Drawing3DialogLogSink &log_sink)
{
    return PromptDrawing3TargetsImpl(
        root_solid,
        &candidates,
        source_label,
        selected,
        candidates_total,
        view_mask,
        quick_mode,
        frame_options,
        cancelled,
        log_sink);
}

bool PromptDrawing3StartPoint(ProPoint3d start_point, bool &cancelled)
{
    cancelled = false;
    if (start_point == nullptr) {
        return false;
    }

    ProUIMessageButton choice = PRO_UI_MESSAGE_OK;
    ProUIMessageButton *buttons = nullptr;
    if (ProArrayAlloc(0, sizeof(ProUIMessageButton), 1, (ProArray *)&buttons) == PRO_TK_NO_ERROR &&
        buttons != nullptr) {
        ProUIMessageButton button = PRO_UI_MESSAGE_OK;
        ProArrayObjectAdd((ProArray *)&buttons, PRO_VALUE_UNUSED, 1, &button);
        ProUIMessageDialogDisplay(
            PROUIMESSAGE_INFO,
            const_cast<wchar_t *>(L"\u5efa\u89c6\u56fe"),
            const_cast<wchar_t *>(L"\u8bf7\u5728\u5f53\u524d\u5de5\u7a0b\u56fe\u9875\u5de6\u952e\u70b9\u9009\u9996\u4e2a\u6a21\u578b\u4e3b\u89c6\u57fa\u51c6\u4f4d\u7f6e\uff0c\u53f3\u952e\u53d6\u6d88\u3002"),
            buttons,
            PRO_UI_MESSAGE_OK,
            &choice);
        ProArrayFree((ProArray *)&buttons);
    }

    ProMouseButton button = PRO_NO_BUTTON;
    const ProError st = ProMousePickGet(
        static_cast<ProMouseButton>(PRO_LEFT_BUTTON | PRO_RIGHT_BUTTON),
        &button,
        start_point);
    if (st != PRO_TK_NO_ERROR) {
        return false;
    }
    if (button != PRO_LEFT_BUTTON) {
        cancelled = true;
        return false;
    }
    return true;
}

} // namespace autobbox::ui
