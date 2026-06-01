#include "autobbox/ui/random_color_dialog.h"

#include "autobbox/application/random_color.h"
#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"
#include "autobbox/ui/message_dialog.h"

#include <ProUICheckbutton.h>
#include <ProUIDialog.h>
#include <ProUIInputpanel.h>
#include <ProUILabel.h>
#include <ProUIOptionmenu.h>
#include <ProUIPushbutton.h>
#include <ProUITable.h>
#include <ProToolkit.h>
#include <ProUI.h>
#include <ProUtil.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <commdlg.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#pragma comment(lib, "Comdlg32.lib")

namespace autobbox::ui {

namespace {

struct RandomColorDialogConfig {
    const char *dialog_inst_name = nullptr;
    const char *resource_base_name = nullptr;
    const char *mode_comp = nullptr;
    const char *param_comp = nullptr;
    const char *file_value_comp = nullptr;
    const char *table_comp = nullptr;
    const char *base_check_comp = nullptr;
    const char *browse_comp = nullptr;
    const char *randomize_comp = nullptr;
    const char *refresh_comp = nullptr;
    const char *select_all_comp = nullptr;
    const char *clear_comp = nullptr;
    const char *add_library_comp = nullptr;
    const char *write_param_comp = nullptr;
    const char *clear_colors_comp = nullptr;
    const char *ok_comp = nullptr;
    const char *cancel_comp = nullptr;
    int status_ok = 0;
    int status_cancel = 0;
    int status_clear_colors = 0;
};

RandomColorDialogConfig DefaultRandomColorDialogConfig()
{
    RandomColorDialogConfig config = {};
    config.dialog_inst_name = "autobbox_random_color_inst";
    config.resource_base_name = "autobbox_random_color";
    config.mode_comp = "ModeMenu";
    config.param_comp = "ParamInput";
    config.file_value_comp = "FileValueLabel";
    config.table_comp = "ModelTable";
    config.base_check_comp = "BaseModelCheck";
    config.browse_comp = "BrowseBtn";
    config.randomize_comp = "RandomizeBtn";
    config.refresh_comp = "RefreshBtn";
    config.select_all_comp = "SelectAllBtn";
    config.clear_comp = "ClearBtn";
    config.add_library_comp = "AddLibraryBtn";
    config.write_param_comp = "WriteParamBtn";
    config.clear_colors_comp = "ClearColorsBtn";
    config.ok_comp = "OKBtn";
    config.cancel_comp = "CancelBtn";
    config.status_ok = 1;
    config.status_cancel = 0;
    config.status_clear_colors = 2;
    return config;
}

struct RandomColorDialogState {
    std::vector<core::RandomColorCandidate> candidates_storage;
    const std::vector<core::RandomColorCandidate> *candidates = nullptr;
    std::vector<core::RandomColorEntry> loaded_colors;
    std::vector<core::RandomColorAssignment> preview_assignments;
    std::vector<core::RandomColorParameterPreview> parameter_previews;
    std::unordered_map<std::string, std::string> checkbox_component_by_item_name;
    std::vector<std::wstring> color_option_labels_storage;
    int checkbox_render_serial = 0;
    core::RandomColorMode mode = core::RandomColorMode::Random;
    bool clear_colors_mode = false;
    bool opening_color_palette = false;
    std::wstring library_path;
    std::wstring library_error;
    std::wstring parameter_name = L"\u989c\u8272";
};

struct RandomColorDialogRuntime {
    RandomColorDialogState *state = nullptr;
    const RandomColorDialogConfig *config = nullptr;
    RandomColorDialogLogSink log_sink;
};

void ReloadPreviewAssignments(char *dialog,
                              RandomColorDialogState *state,
                              const RandomColorDialogConfig &config,
                              const RandomColorDialogLogSink &log_sink);

void RefreshDialogList(char *dialog,
                       RandomColorDialogState *state,
                       const RandomColorDialogConfig &config,
                       const RandomColorDialogLogSink &log_sink,
                       bool preserve_random_targets);

void RerandomizeSelectedAssignments(char *dialog,
                                    RandomColorDialogState *state,
                                    const RandomColorDialogConfig &config,
                                    const RandomColorDialogLogSink &log_sink);

void PopulateModelTable(char *dialog,
                        RandomColorDialogState *state,
                        const RandomColorDialogConfig &config);

void LogLine(const RandomColorDialogLogSink &log_sink, const char *fmt, ...)
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

constexpr const wchar_t *kRandomModeLabel = L"\u968f\u673a\u5206\u914d";
constexpr const wchar_t *kParameterModeLabel = L"\u6309\u989c\u8272\u53c2\u6570";

bool IsMatchedParameterPreview(const core::RandomColorParameterPreview &preview)
{
    return preview.match_source != core::RandomColorMatchSource::None;
}

std::wstring MatchSourceLabel(core::RandomColorMatchSource source)
{
    switch (source) {
    case core::RandomColorMatchSource::Preset:
        return L"\u9884\u8bbe";
    case core::RandomColorMatchSource::Library:
        return L"\u5916\u89c2\u5e93";
    case core::RandomColorMatchSource::None:
    default:
        return L"";
    }
}

void OnDialogExit(char *dialog, char *, ProAppData app_data)
{
    if (dialog != nullptr) {
        ProUIDialogExit(dialog, static_cast<int>(reinterpret_cast<std::intptr_t>(app_data)));
    }
}

ProUIColor ToUiColor(const core::RandomColorEntry &color)
{
    const int red = static_cast<int>(std::clamp(color.red * 255.0, 0.0, 255.0));
    const int green = static_cast<int>(std::clamp(color.green * 255.0, 0.0, 255.0));
    const int blue = static_cast<int>(std::clamp(color.blue * 255.0, 0.0, 255.0));
    return PRO_UI_COLOR_RGB(red, green, blue);
}

ProUIColor TextColorForColor(const core::RandomColorEntry &color)
{
    const double luminance = color.red * 0.299 + color.green * 0.587 + color.blue * 0.114;
    return luminance > 0.6 ? PRO_UI_COLOR_BLACK : PRO_UI_COLOR_WHITE;
}

const core::RandomColorAssignment *FindPreviewAssignment(const RandomColorDialogState &state,
                                                         const std::string &item_name)
{
    for (const core::RandomColorAssignment &assignment : state.preview_assignments) {
        if (assignment.candidate.item_name == item_name) {
            return &assignment;
        }
    }
    return nullptr;
}

core::RandomColorAssignment *FindPreviewAssignment(RandomColorDialogState &state,
                                                   const std::string &item_name)
{
    for (core::RandomColorAssignment &assignment : state.preview_assignments) {
        if (assignment.candidate.item_name == item_name) {
            return &assignment;
        }
    }
    return nullptr;
}

const core::RandomColorParameterPreview *FindParameterPreview(const RandomColorDialogState &state,
                                                              const std::string &item_name)
{
    for (const core::RandomColorParameterPreview &preview : state.parameter_previews) {
        if (preview.candidate.item_name == item_name) {
            return &preview;
        }
    }
    return nullptr;
}

std::wstring MakeColorChoiceLabel(const core::RandomColorEntry &color, size_t index)
{
    std::wstring label = color.display_name.empty() ? color.material_name : color.display_name;
    if (label.empty()) {
        label = autobbox::common::AToW(color.id.c_str());
    }
    if (label.empty()) {
        label = L"\u989c\u8272 " + std::to_wstring(index + 1);
    }
    if (!color.material_name.empty() && color.material_name != label) {
        label += L" [" + color.material_name + L"]";
    }
    return label;
}

void RebuildColorOptionChoices(RandomColorDialogState &state)
{
    state.color_option_labels_storage.clear();
    state.color_option_labels_storage.reserve(state.loaded_colors.size());

    std::unordered_map<std::wstring, int> label_counts;
    for (size_t i = 0; i < state.loaded_colors.size(); ++i) {
        std::wstring label = MakeColorChoiceLabel(state.loaded_colors[i], i);
        const int count = ++label_counts[label];
        if (count > 1) {
            label += L" (" + std::to_wstring(count) + L")";
        }
        state.color_option_labels_storage.push_back(std::move(label));
    }
}

int FindColorChoiceIndexByAssignment(const RandomColorDialogState &state,
                                     const core::RandomColorEntry &color)
{
    for (size_t i = 0; i < state.loaded_colors.size(); ++i) {
        const core::RandomColorEntry &candidate = state.loaded_colors[i];
        if (candidate.id == color.id &&
            candidate.material_name == color.material_name &&
            candidate.display_name == color.display_name) {
            return static_cast<int>(i);
        }
    }
    for (size_t i = 0; i < state.loaded_colors.size(); ++i) {
        const core::RandomColorEntry &candidate = state.loaded_colors[i];
        if (candidate.material_name == color.material_name &&
            candidate.display_name == color.display_name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void SetPreviewAssignmentColor(RandomColorDialogState &state,
                               const std::string &item_name,
                               const core::RandomColorEntry &color)
{
    if (core::RandomColorAssignment *assignment = FindPreviewAssignment(state, item_name)) {
        assignment->color = color;
    }
}

std::string ColorEntryKey(const core::RandomColorEntry &color)
{
    if (!color.id.empty()) {
        return std::string("id:") + color.id;
    }
    if (!color.material_name.empty()) {
        return std::string("material:") + autobbox::common::WToA(color.material_name.c_str());
    }
    if (!color.display_name.empty()) {
        return std::string("display:") + autobbox::common::WToA(color.display_name.c_str());
    }

    char buffer[128] = {0};
    std::snprintf(buffer,
                  sizeof(buffer),
                  "rgb:%.6f,%.6f,%.6f,%.6f,%.6f,%.6f",
                  color.red,
                  color.green,
                  color.blue,
                  color.highlight_red,
                  color.highlight_green,
                  color.highlight_blue);
    return buffer;
}

bool ContainsItemName(const std::vector<std::string> &item_names, const std::string &item_name)
{
    return std::find(item_names.begin(), item_names.end(), item_name) != item_names.end();
}

std::vector<core::RandomColorEntry> ExcludeUnselectedTargetColors(
    const std::vector<core::RandomColorEntry> &colors,
    const std::vector<core::RandomColorAssignment> &assignments,
    const std::vector<std::string> &selected_rows)
{
    std::set<std::string> excluded_color_keys;
    for (const core::RandomColorAssignment &assignment : assignments) {
        if (!ContainsItemName(selected_rows, assignment.candidate.item_name)) {
            excluded_color_keys.insert(ColorEntryKey(assignment.color));
        }
    }

    if (excluded_color_keys.empty()) {
        return colors;
    }

    std::vector<core::RandomColorEntry> filtered;
    filtered.reserve(colors.size());
    for (const core::RandomColorEntry &color : colors) {
        if (excluded_color_keys.find(ColorEntryKey(color)) == excluded_color_keys.end()) {
            filtered.push_back(color);
        }
    }
    return filtered;
}

std::vector<core::RandomColorCandidate> FilterCandidatesByItemNames(
    const std::vector<core::RandomColorCandidate> &candidates,
    const std::vector<std::string> &item_names)
{
    std::vector<core::RandomColorCandidate> filtered;
    for (const std::string &item_name : item_names) {
        for (const core::RandomColorCandidate &candidate : candidates) {
            if (candidate.item_name == item_name) {
                filtered.push_back(candidate);
                break;
            }
        }
    }
    return filtered;
}

std::vector<std::string> GetSelectedRowNames(char *dialog,
                                             const RandomColorDialogState *state,
                                             const RandomColorDialogConfig &config)
{
    std::vector<std::string> selected;
    if (dialog == nullptr || state == nullptr || state->candidates == nullptr) {
        return selected;
    }

    for (const core::RandomColorCandidate &candidate : *state->candidates) {
        const auto it = state->checkbox_component_by_item_name.find(candidate.item_name);
        if (it == state->checkbox_component_by_item_name.end()) {
            continue;
        }

        ProBoolean checked = PRO_B_FALSE;
        if (ProUICheckbuttonGetState(dialog, const_cast<char *>(it->second.c_str()), &checked) ==
                PRO_TK_NO_ERROR &&
            checked == PRO_B_TRUE) {
            selected.push_back(candidate.item_name);
        }
    }
    return selected;
}

void SetSelectedRows(char *dialog,
                     const RandomColorDialogState *state,
                     const std::vector<std::string> &selected_row_names)
{
    if (dialog == nullptr || state == nullptr || state->candidates == nullptr) {
        return;
    }

    for (const core::RandomColorCandidate &candidate : *state->candidates) {
        const auto it = state->checkbox_component_by_item_name.find(candidate.item_name);
        if (it == state->checkbox_component_by_item_name.end()) {
            continue;
        }

        if (std::find(selected_row_names.begin(), selected_row_names.end(), candidate.item_name) !=
            selected_row_names.end()) {
            ProUICheckbuttonSet(dialog, const_cast<char *>(it->second.c_str()));
        } else {
            ProUICheckbuttonUnset(dialog, const_cast<char *>(it->second.c_str()));
        }
    }
}

std::vector<core::RandomColorCandidate> CollectSelectedCandidates(
    char *dialog,
    const RandomColorDialogState *state,
    const RandomColorDialogConfig &config)
{
    std::vector<core::RandomColorCandidate> selected;
    if (state == nullptr || state->candidates == nullptr) {
        return selected;
    }

    const std::vector<std::string> selected_row_names =
        GetSelectedRowNames(dialog, state, config);
    for (const std::string &row_name : selected_row_names) {
        for (const core::RandomColorCandidate &candidate : *state->candidates) {
            if (candidate.item_name == row_name) {
                selected.push_back(candidate);
                break;
            }
        }
    }
    return selected;
}

void SetDialogListState(char *dialog,
                        const RandomColorDialogState *state,
                        ProUIMixedState item_state)
{
    if (dialog == nullptr || state == nullptr || state->candidates == nullptr) {
        return;
    }

    for (const core::RandomColorCandidate &candidate : *state->candidates) {
        const auto it = state->checkbox_component_by_item_name.find(candidate.item_name);
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

void ConfigureModeControls(const RandomColorDialogConfig &config, const RandomColorDialogState &state)
{
    char *dialog = const_cast<char *>(config.dialog_inst_name);
    std::vector<std::string> mode_names_storage = {"RANDOM", "PARAMETER"};
    std::vector<std::wstring> mode_labels_storage = {kRandomModeLabel, kParameterModeLabel};
    std::vector<char *> mode_names;
    std::vector<wchar_t *> mode_labels;
    for (std::string &name : mode_names_storage) {
        mode_names.push_back(const_cast<char *>(name.c_str()));
    }
    for (std::wstring &label : mode_labels_storage) {
        mode_labels.push_back(const_cast<wchar_t *>(label.c_str()));
    }
    ProUIOptionmenuNamesSet(dialog, const_cast<char *>(config.mode_comp), static_cast<int>(mode_names.size()), mode_names.data());
    ProUIOptionmenuLabelsSet(dialog, const_cast<char *>(config.mode_comp), static_cast<int>(mode_labels.size()), mode_labels.data());
    ProUIOptionmenuColumnsSet(dialog, const_cast<char *>(config.mode_comp), 18);
    ProUIOptionmenuVisiblerowsSet(dialog, const_cast<char *>(config.mode_comp), 2);
    ProUIOptionmenuValueSet(
        dialog,
        const_cast<char *>(config.mode_comp),
        const_cast<wchar_t *>(state.mode == core::RandomColorMode::Parameter ? kParameterModeLabel : kRandomModeLabel));
    ProUIInputpanelColumnsSet(dialog, const_cast<char *>(config.param_comp), 20);
    ProUIInputpanelValueSet(dialog, const_cast<char *>(config.param_comp), const_cast<wchar_t *>(state.parameter_name.c_str()));
}

void ReadModeControls(char *dialog, RandomColorDialogState *state, const RandomColorDialogConfig &config)
{
    if (dialog == nullptr || state == nullptr) {
        return;
    }

    wchar_t *mode_value = nullptr;
    if (ProUIOptionmenuValueGet(dialog, const_cast<char *>(config.mode_comp), &mode_value) == PRO_TK_NO_ERROR &&
        mode_value != nullptr) {
        const std::wstring mode(mode_value);
        state->mode = (mode == kParameterModeLabel || mode == L"PARAMETER")
                          ? core::RandomColorMode::Parameter
                          : core::RandomColorMode::Random;
        ProWstringFree(mode_value);
    }

    wchar_t *param_value = nullptr;
    if (ProUIInputpanelValueGet(dialog, const_cast<char *>(config.param_comp), &param_value) == PRO_TK_NO_ERROR &&
        param_value != nullptr) {
        state->parameter_name = param_value;
        ProWstringFree(param_value);
    }
}

void OnSelectAll(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<RandomColorDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr) {
        return;
    }
    SetDialogListState(dialog, runtime->state, PROUI_SET);
}

void OnClearSelection(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<RandomColorDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr) {
        return;
    }
    SetDialogListState(dialog, runtime->state, PROUI_UNSET);
}

void OnRandomizePreview(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<RandomColorDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return;
    }

    ReadModeControls(dialog, runtime->state, *runtime->config);
    runtime->state->clear_colors_mode = false;
    if (runtime->state->mode == core::RandomColorMode::Random) {
        RerandomizeSelectedAssignments(dialog, runtime->state, *runtime->config, runtime->log_sink);
    } else {
        ReloadPreviewAssignments(dialog, runtime->state, *runtime->config, runtime->log_sink);
    }
    if (runtime->state->mode == core::RandomColorMode::Random && !runtime->state->library_error.empty()) {
        ShowSimpleMessageDialog(PROUIMESSAGE_WARNING, L"\u968f\u673a\u4e0a\u8272", runtime->state->library_error.c_str());
        return;
    }

    LogLine(runtime->log_sink,
            "random-color-dialog rerandomized file=%s assignments=%d",
            autobbox::common::WToA(runtime->state->library_path.c_str()).c_str(),
            static_cast<int>(runtime->state->preview_assignments.size()));
}

void OnRefreshList(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<RandomColorDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return;
    }

    ReadModeControls(dialog, runtime->state, *runtime->config);
    runtime->state->clear_colors_mode = false;
    RefreshDialogList(dialog, runtime->state, *runtime->config, runtime->log_sink, true);
}

void OnModeChanged(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<RandomColorDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return;
    }

    ReadModeControls(dialog, runtime->state, *runtime->config);
    runtime->state->clear_colors_mode = false;
    ReloadPreviewAssignments(dialog, runtime->state, *runtime->config, runtime->log_sink);
}

void OnClearColorsPreview(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<RandomColorDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return;
    }

    const std::vector<std::string> previously_selected =
        GetSelectedRowNames(dialog, runtime->state, *runtime->config);
    runtime->state->clear_colors_mode = true;
    runtime->state->preview_assignments.clear();
    runtime->state->parameter_previews.clear();
    runtime->state->library_error.clear();
    PopulateModelTable(dialog, runtime->state, *runtime->config);
    if (!previously_selected.empty()) {
        SetSelectedRows(dialog, runtime->state, previously_selected);
    } else {
        SetDialogListState(dialog, runtime->state, PROUI_SET);
    }

    LogLine(runtime->log_sink,
            "random-color-dialog clear-preview targets=%d",
            static_cast<int>(runtime->state->candidates == nullptr ? 0 : runtime->state->candidates->size()));
}

std::wstring TrimPathForUi(const std::wstring &path_text)
{
    constexpr size_t kMaxUiPathLength = 92;
    if (path_text.size() <= kMaxUiPathLength) {
        return path_text;
    }

    const size_t head = 48;
    const size_t tail = 36;
    return path_text.substr(0, head) + L" ... " + path_text.substr(path_text.size() - tail);
}

void UpdateFileLabel(const RandomColorDialogConfig &config, const RandomColorDialogState &state)
{
    std::wstring text =
        state.library_path.empty() ? L"<鏈€夋嫨 .dmt 鏂囦欢>" : TrimPathForUi(state.library_path);
    if (state.library_path.empty()) {
        text = L"<\u672a\u9009\u62e9 .dmt \u6587\u4ef6>";
    }
    ProUILabelTextSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.file_value_comp),
        const_cast<wchar_t *>(text.c_str()));
}

void ApplyChineseDialogText(const RandomColorDialogConfig &config)
{
    char *dialog = const_cast<char *>(config.dialog_inst_name);
    ProUILabelTextSet(
        dialog,
        const_cast<char *>("PromptLabel"),
        const_cast<wchar_t *>(L"\u8bf7\u9009\u62e9\u8981\u4e0a\u8272\u7684\u6a21\u578b\u3001\u6a21\u5f0f\u548c .dmt \u989c\u8272\u5e93\uff1a"));
    ProUILabelTextSet(
        dialog,
        const_cast<char *>("ModeLabel"),
        const_cast<wchar_t *>(L"\u4e0a\u8272\u6a21\u5f0f"));
    ProUILabelTextSet(
        dialog,
        const_cast<char *>("ParamLabel"),
        const_cast<wchar_t *>(L"\u989c\u8272\u53c2\u6570"));
    ProUILabelTextSet(
        dialog,
        const_cast<char *>("FileLabel"),
        const_cast<wchar_t *>(L"\u989c\u8272\u5e93 (.dmt)"));
    ProUIPushbuttonTextSet(
        dialog,
        const_cast<char *>(config.browse_comp),
        const_cast<wchar_t *>(L"\u9009\u62e9 DMT"));
    ProUIPushbuttonTextSet(
        dialog,
        const_cast<char *>(config.select_all_comp),
        const_cast<wchar_t *>(L"\u5168\u9009"));
    ProUIPushbuttonTextSet(
        dialog,
        const_cast<char *>(config.clear_comp),
        const_cast<wchar_t *>(L"\u6e05\u7a7a\u9009\u62e9"));
    ProUIPushbuttonTextSet(
        dialog,
        const_cast<char *>(config.randomize_comp),
        const_cast<wchar_t *>(L"\u91cd\u65b0\u968f\u673a"));
    ProUIPushbuttonTextSet(
        dialog,
        const_cast<char *>(config.refresh_comp),
        const_cast<wchar_t *>(L"\u5237\u65b0"));
    ProUIPushbuttonTextSet(
        dialog,
        const_cast<char *>(config.add_library_comp),
        const_cast<wchar_t *>(L"\u52a0\u5165\u5916\u89c2\u5e93"));
    ProUIPushbuttonTextSet(
        dialog,
        const_cast<char *>(config.write_param_comp),
        const_cast<wchar_t *>(L"\u56de\u5199\u53c2\u6570"));
    ProUIPushbuttonTextSet(
        dialog,
        const_cast<char *>(config.clear_colors_comp),
        const_cast<wchar_t *>(L"\u6e05\u9664\u989c\u8272"));
    ProUIPushbuttonTextSet(
        dialog,
        const_cast<char *>(config.cancel_comp),
        const_cast<wchar_t *>(L"\u53d6\u6d88"));
    ProUIPushbuttonTextSet(
        dialog,
        const_cast<char *>(config.ok_comp),
        const_cast<wchar_t *>(L"\u5e94\u7528"));
}

std::wstring ResolveBrowseDirectory(const std::wstring &library_path)
{
    if (library_path.empty()) {
        return std::wstring();
    }

    const std::filesystem::path path(library_path);
    if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
        return path.wstring();
    }
    if (path.has_parent_path()) {
        return path.parent_path().wstring();
    }
    return std::wstring();
}

bool ChooseDmtFile(const std::wstring &initial_path, std::wstring &selected_path)
{
    std::wstring browse_directory = ResolveBrowseDirectory(initial_path);
    std::vector<wchar_t> file_buffer(4096, L'\0');
    if (!initial_path.empty()) {
        wcsncpy_s(file_buffer.data(), file_buffer.size(), initial_path.c_str(), _TRUNCATE);
    }

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = GetActiveWindow();
    ofn.lpstrFile = file_buffer.data();
    ofn.nMaxFile = static_cast<DWORD>(file_buffer.size());
    ofn.lpstrFilter = L"DMT \u989c\u8272\u5e93 (*.dmt)\0*.dmt\0\u6240\u6709\u6587\u4ef6 (*.*)\0*.*\0\0";
    ofn.lpstrTitle = L"\u9009\u62e9\u989c\u8272\u5e93\u6587\u4ef6";
    ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (!browse_directory.empty()) {
        ofn.lpstrInitialDir = browse_directory.c_str();
    }

    if (!GetOpenFileNameW(&ofn)) {
        return false;
    }

    selected_path = file_buffer.data();
    return !selected_path.empty();
}

struct ColorPaletteDialogState {
    const std::vector<core::RandomColorEntry> *colors = nullptr;
    std::vector<std::wstring> labels;
    int selected_index = -1;
    const char *dialog_inst_name = "autobbox_color_palette_inst";
    const char *resource_base_name = "autobbox_color_palette";
    const char *table_comp = "ColorTable";
    const char *ok_comp = "OKBtn";
    const char *cancel_comp = "CancelBtn";
};

void OnPaletteDialogExit(char *dialog, char *, ProAppData app_data)
{
    if (dialog != nullptr) {
        ProUIDialogExit(dialog, static_cast<int>(reinterpret_cast<std::intptr_t>(app_data)));
    }
}

bool SelectFocusedPaletteColor(char *dialog, ColorPaletteDialogState *state)
{
    if (dialog == nullptr || state == nullptr) {
        return false;
    }

    char *row_name = nullptr;
    char *column_name = nullptr;
    if (ProUITableFocusCellGet(
            dialog,
            const_cast<char *>(state->table_comp),
            &row_name,
            &column_name) != PRO_TK_NO_ERROR ||
        row_name == nullptr) {
        if (row_name != nullptr) {
            ProStringFree(row_name);
        }
        if (column_name != nullptr) {
            ProStringFree(column_name);
        }
        return false;
    }

    int selected = -1;
    if (sscanf_s(row_name, "color_%d", &selected) == 1 &&
        state->colors != nullptr &&
        selected >= 0 &&
        static_cast<size_t>(selected) < state->colors->size()) {
        state->selected_index = selected;
    }

    ProStringFree(row_name);
    if (column_name != nullptr) {
        ProStringFree(column_name);
    }
    return state->selected_index >= 0;
}

void OnPaletteTableSelect(char *dialog, char *, ProAppData app_data)
{
    SelectFocusedPaletteColor(dialog, reinterpret_cast<ColorPaletteDialogState *>(app_data));
}

void OnPaletteTableActivate(char *dialog, char *, ProAppData app_data)
{
    auto *state = reinterpret_cast<ColorPaletteDialogState *>(app_data);
    if (SelectFocusedPaletteColor(dialog, state)) {
        ProUIDialogExit(dialog, 1);
    }
}

void OnPaletteConfirm(char *dialog, char *, ProAppData app_data)
{
    auto *state = reinterpret_cast<ColorPaletteDialogState *>(app_data);
    SelectFocusedPaletteColor(dialog, state);
    ProUIDialogExit(dialog, 1);
}

ProError TryCreateColorPaletteDialog(const ColorPaletteDialogState &state,
                                     const RandomColorDialogLogSink &log_sink)
{
    const std::string base_name = state.resource_base_name;
    const std::vector<std::string> rel_candidates = {
        base_name,
        std::string("resource\\") + base_name,
        std::string("text\\resource\\") + base_name,
        std::string("text\\usascii\\resource\\") + base_name,
        std::string("usascii\\resource\\") + base_name,
    };

    ProError last = PRO_TK_GENERAL_ERROR;
    for (const std::string &resource : rel_candidates) {
        last = ProUIDialogCreate(const_cast<char *>(state.dialog_inst_name), const_cast<char *>(resource.c_str()));
        LogLine(log_sink, "color-palette-dialog create try resource=%s status=%d", resource.c_str(), static_cast<int>(last));
        if (last == PRO_TK_NO_ERROR) {
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
            text_root + "\\text\\usascii\\resource\\" + base_name,
            text_root + "\\text\\usascii\\resource\\" + base_name + ".res",
        };
        for (const std::string &path : abs_candidates) {
            last = ProUIDialogCreate(const_cast<char *>(state.dialog_inst_name), const_cast<char *>(path.c_str()));
            LogLine(log_sink, "color-palette-dialog create try resource=%s status=%d", path.c_str(), static_cast<int>(last));
            if (last == PRO_TK_NO_ERROR) {
                return last;
            }
        }
    }
    return last;
}

bool PromptColorPaletteDialog(const std::vector<core::RandomColorEntry> &colors,
                              const std::vector<std::wstring> &labels,
                              int initial_index,
                              int &selected_index,
                              const RandomColorDialogLogSink &log_sink)
{
    selected_index = initial_index;
    if (colors.empty()) {
        return false;
    }

    ColorPaletteDialogState state;
    state.colors = &colors;
    state.labels = labels;
    state.selected_index = (initial_index >= 0 && static_cast<size_t>(initial_index) < colors.size())
                               ? initial_index
                               : 0;

    if (TryCreateColorPaletteDialog(state, log_sink) != PRO_TK_NO_ERROR) {
        ShowSimpleMessageDialog(
            PROUIMESSAGE_ERROR,
            L"\u9009\u62e9\u76ee\u6807\u989c\u8272",
            L"\u6253\u5f00\u8272\u5361\u5bf9\u8bdd\u6846\u5931\u8d25\u3002");
        return false;
    }

    char *dialog = const_cast<char *>(state.dialog_inst_name);
    ProUIDialogTitleSet(dialog, const_cast<wchar_t *>(L"\u9009\u62e9\u76ee\u6807\u989c\u8272"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(state.ok_comp), const_cast<wchar_t *>(L"\u786e\u5b9a"));
    ProUIPushbuttonTextSet(dialog, const_cast<char *>(state.cancel_comp), const_cast<wchar_t *>(L"\u53d6\u6d88"));

    std::vector<std::string> column_names_storage = {"SWATCH", "NAME"};
    std::vector<std::wstring> column_labels_storage = {L"\u8272\u5361", L"\u5916\u89c2"};
    std::vector<int> column_widths = {10, 44};
    std::vector<int> column_resizings = {0, 4};
    std::vector<char *> column_names;
    std::vector<wchar_t *> column_labels;
    for (std::string &name : column_names_storage) {
        column_names.push_back(const_cast<char *>(name.c_str()));
    }
    for (std::wstring &label : column_labels_storage) {
        column_labels.push_back(const_cast<wchar_t *>(label.c_str()));
    }

    std::vector<std::string> row_names_storage;
    std::vector<wchar_t *> row_labels;
    row_names_storage.reserve(colors.size());
    row_labels.reserve(colors.size());
    for (size_t i = 0; i < colors.size(); ++i) {
        row_names_storage.push_back("color_" + std::to_string(i));
        row_labels.push_back(const_cast<wchar_t *>(L""));
    }
    std::vector<char *> row_names;
    for (std::string &name : row_names_storage) {
        row_names.push_back(const_cast<char *>(name.c_str()));
    }

    ProUITableColumnnamesSet(dialog, const_cast<char *>(state.table_comp), static_cast<int>(column_names.size()), column_names.data());
    ProUITableColumnlabelsSet(dialog, const_cast<char *>(state.table_comp), static_cast<int>(column_labels.size()), column_labels.data());
    ProUITableColumnwidthsSet(dialog, const_cast<char *>(state.table_comp), static_cast<int>(column_widths.size()), column_widths.data());
    ProUITableColumnresizingsSet(dialog, const_cast<char *>(state.table_comp), static_cast<int>(column_resizings.size()), column_resizings.data());
    ProUITableRownamesSet(dialog, const_cast<char *>(state.table_comp), static_cast<int>(row_names.size()), row_names.data());
    ProUITableRowlabelsSet(dialog, const_cast<char *>(state.table_comp), static_cast<int>(row_labels.size()), row_labels.data());
    ProUITableSelectionpolicySet(dialog, const_cast<char *>(state.table_comp), PROUISELPOLICY_BROWSE);
    ProUITableColumnselectionpolicySet(dialog, const_cast<char *>(state.table_comp), PROUISELPOLICY_NONE);
    ProUITableVisiblerowsSet(dialog, const_cast<char *>(state.table_comp), std::min(14, std::max(6, static_cast<int>(colors.size()))));
    ProUITableMinrowsSet(dialog, const_cast<char *>(state.table_comp), std::min(14, std::max(6, static_cast<int>(colors.size()))));
    ProUITableShowgridSet(dialog, const_cast<char *>(state.table_comp), PRO_B_TRUE);

    for (size_t i = 0; i < colors.size(); ++i) {
        const std::string &row = row_names_storage[i];
        const std::wstring label = i < labels.size() ? labels[i] : MakeColorChoiceLabel(colors[i], i);
        ProUITableCellLabelSet(dialog, const_cast<char *>(state.table_comp), const_cast<char *>(row.c_str()), const_cast<char *>("SWATCH"), const_cast<wchar_t *>(L"      "));
        ProUITableCellBackgroundColorSet(dialog, const_cast<char *>(state.table_comp), const_cast<char *>(row.c_str()), const_cast<char *>("SWATCH"), ToUiColor(colors[i]));
        ProUITableCellForegroundColorSet(dialog, const_cast<char *>(state.table_comp), const_cast<char *>(row.c_str()), const_cast<char *>("SWATCH"), TextColorForColor(colors[i]));
        ProUITableCellLabelSet(dialog, const_cast<char *>(state.table_comp), const_cast<char *>(row.c_str()), const_cast<char *>("NAME"), const_cast<wchar_t *>(label.c_str()));
        ProUITableCellHelptextStringSet(dialog, const_cast<char *>(state.table_comp), const_cast<char *>(row.c_str()), const_cast<char *>("NAME"), const_cast<wchar_t *>(label.c_str()));
    }

    if (state.selected_index >= 0 && static_cast<size_t>(state.selected_index) < row_names_storage.size()) {
        ProUITableFocusCellSet(
            dialog,
            const_cast<char *>(state.table_comp),
            const_cast<char *>(row_names_storage[static_cast<size_t>(state.selected_index)].c_str()),
            const_cast<char *>("NAME"));
    }

    ProUITableSelectActionSet(dialog, const_cast<char *>(state.table_comp), OnPaletteTableSelect, &state);
    ProUITableActivateActionSet(dialog, const_cast<char *>(state.table_comp), OnPaletteTableActivate, &state);
    ProUIPushbuttonActivateActionSet(dialog, const_cast<char *>(state.ok_comp), OnPaletteConfirm, &state);
    ProUIPushbuttonActivateActionSet(
        dialog,
        const_cast<char *>(state.cancel_comp),
        OnPaletteDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(0)));
    ProUIDialogCloseActionSet(
        dialog,
        OnPaletteDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(0)));
    ProUIDialogDefaultbuttonSet(dialog, const_cast<char *>(state.ok_comp));

    int status = 0;
    const ProError activate_status = ProUIDialogActivate(dialog, &status);
    if (activate_status != PRO_TK_NO_ERROR) {
        ProUIDialogDestroy(dialog);
        return false;
    }
    if (status == 1 &&
        state.selected_index >= 0 &&
        static_cast<size_t>(state.selected_index) < colors.size()) {
        selected_index = state.selected_index;
        ProUIDialogDestroy(dialog);
        return true;
    }

    ProUIDialogDestroy(dialog);
    return false;
}

void PopulateModelTable(char *dialog,
                        RandomColorDialogState *state,
                        const RandomColorDialogConfig &config)
{
    if (dialog == nullptr || state == nullptr || state->candidates == nullptr) {
        return;
    }

    ++state->checkbox_render_serial;
    state->checkbox_component_by_item_name.clear();

    std::vector<std::string> column_names_storage = {"USE", "MODEL", "TYPE", "QTY", "CURRENT", "PARAM", "COLOR"};
    std::vector<std::wstring> column_labels_storage = {
        L"",
        L"\u6a21\u578b",
        L"\u7c7b\u578b",
        L"\u6570\u91cf",
        L"\u5f53\u524d\u5916\u89c2",
        L"\u53c2\u6570\u503c",
        L"\u76ee\u6807\u989c\u8272",
    };
    std::vector<int> column_widths = {4, 24, 7, 6, 20, 16, 22};
    std::vector<int> column_resizings = {0, 4, 1, 1, 3, 2, 3};
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
    row_names.reserve(state->candidates->size());
    row_labels.reserve(state->candidates->size());
    for (const core::RandomColorCandidate &candidate : *state->candidates) {
        row_names.push_back(const_cast<char *>(candidate.item_name.c_str()));
        row_labels.push_back(const_cast<wchar_t *>(L""));
    }

    ProUITableColumnnamesSet(dialog, const_cast<char *>(config.table_comp), static_cast<int>(column_names.size()), column_names.data());
    ProUITableColumnlabelsSet(dialog, const_cast<char *>(config.table_comp), static_cast<int>(column_labels.size()), column_labels.data());
    ProUITableColumnwidthsSet(dialog, const_cast<char *>(config.table_comp), static_cast<int>(column_widths.size()), column_widths.data());
    ProUITableColumnresizingsSet(dialog, const_cast<char *>(config.table_comp), static_cast<int>(column_resizings.size()), column_resizings.data());
    ProUITableRownamesSet(dialog, const_cast<char *>(config.table_comp), static_cast<int>(row_names.size()), row_names.empty() ? nullptr : row_names.data());
    ProUITableRowlabelsSet(dialog, const_cast<char *>(config.table_comp), static_cast<int>(row_labels.size()), row_labels.empty() ? nullptr : row_labels.data());
    ProUITableSelectionpolicySet(dialog, const_cast<char *>(config.table_comp), PROUISELPOLICY_SINGLE);
    ProUITableColumnselectionpolicySet(dialog, const_cast<char *>(config.table_comp), PROUISELPOLICY_NONE);
    ProUITableVisiblerowsSet(dialog, const_cast<char *>(config.table_comp), std::min(12, std::max(4, static_cast<int>(state->candidates->size()))));
    ProUITableMinrowsSet(dialog, const_cast<char *>(config.table_comp), std::min(12, std::max(4, static_cast<int>(state->candidates->size()))));
    ProUITableShowgridSet(dialog, const_cast<char *>(config.table_comp), PRO_B_TRUE);
    ProUITableLockedcolumnsSet(dialog, const_cast<char *>(config.table_comp), 0);

    for (const core::RandomColorCandidate &candidate : *state->candidates) {
        char check_name[48] = {0};
        std::snprintf(check_name, sizeof(check_name), "rcchk_%d_%s", state->checkbox_render_serial, candidate.item_name.c_str());
        state->checkbox_component_by_item_name[candidate.item_name] = check_name;

        ProUITableCellComponentCopy(
            dialog,
            const_cast<char *>(config.table_comp),
            const_cast<char *>(candidate.item_name.c_str()),
            const_cast<char *>("USE"),
            dialog,
            const_cast<char *>(config.base_check_comp),
            const_cast<char *>(state->checkbox_component_by_item_name[candidate.item_name].c_str()));
        ProUICheckbuttonTextSet(
            dialog,
            const_cast<char *>(state->checkbox_component_by_item_name[candidate.item_name].c_str()),
            const_cast<wchar_t *>(L""));

        ProUITableCellLabelSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.item_name.c_str()), const_cast<char *>("MODEL"), const_cast<wchar_t *>(candidate.model_name.c_str()));
        ProUITableCellLabelSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.item_name.c_str()), const_cast<char *>("TYPE"), const_cast<wchar_t *>(candidate.type == PRO_MDL_ASSEMBLY ? L"ASM" : L"PRT"));

        const std::wstring qty_label = std::to_wstring(std::max(1, static_cast<int>(candidate.occurrences.size())));
        ProUITableCellLabelSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.item_name.c_str()), const_cast<char *>("QTY"), const_cast<wchar_t *>(qty_label.c_str()));

        std::wstring current_label = L"\u672a\u8bbe\u7f6e";
        ProUIColor current_bg = PRO_UI_COLOR_WHITE;
        ProUIColor current_fg = PRO_UI_COLOR_BLACK;
        if (candidate.has_current_appearance) {
            current_label = candidate.current_appearance_label.empty()
                                ? candidate.current_appearance.display_name
                                : candidate.current_appearance_label;
            if (!candidate.has_mixed_current_appearance) {
                current_bg = ToUiColor(candidate.current_appearance);
                current_fg = TextColorForColor(candidate.current_appearance);
            }
        }

        ProUIColor bg = PRO_UI_COLOR_WHITE;
        ProUIColor fg = PRO_UI_COLOR_BLACK;
        std::wstring param_label;
        std::wstring color_label = state->clear_colors_mode ? L"\u5f85\u6e05\u9664" : L"<\u65e0\u989c\u8272>";
        if (!state->clear_colors_mode) {
            const core::RandomColorParameterPreview *preview =
                FindParameterPreview(*state, candidate.item_name);
            if (preview != nullptr) {
                param_label = preview->parameter_value.empty() ? preview->status_text : preview->parameter_value;
            }
            if (state->mode == core::RandomColorMode::Parameter) {
                if (preview != nullptr) {
                    if (IsMatchedParameterPreview(*preview)) {
                        bg = ToUiColor(preview->color);
                        fg = TextColorForColor(preview->color);
                        color_label = preview->color.display_name + L" (" + MatchSourceLabel(preview->match_source) + L")";
                    } else {
                        color_label = preview->status_text.empty() ? L"\u672a\u5339\u914d" : preview->status_text;
                    }
                }
            } else if (const core::RandomColorAssignment *assignment = FindPreviewAssignment(*state, candidate.item_name)) {
                bg = ToUiColor(assignment->color);
                fg = TextColorForColor(assignment->color);
                color_label = assignment->color.display_name;
            }
        }

        ProUITableCellBackgroundColorSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.item_name.c_str()), const_cast<char *>("MODEL"), PRO_UI_COLOR_WHITE);
        ProUITableCellForegroundColorSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.item_name.c_str()), const_cast<char *>("MODEL"), PRO_UI_COLOR_BLACK);
        ProUITableCellLabelSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.item_name.c_str()), const_cast<char *>("CURRENT"), const_cast<wchar_t *>(current_label.c_str()));
        ProUITableCellBackgroundColorSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.item_name.c_str()), const_cast<char *>("CURRENT"), current_bg);
        ProUITableCellForegroundColorSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.item_name.c_str()), const_cast<char *>("CURRENT"), current_fg);
        ProUITableCellHelptextStringSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.item_name.c_str()), const_cast<char *>("CURRENT"), const_cast<wchar_t *>(current_label.c_str()));
        ProUITableCellLabelSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.item_name.c_str()), const_cast<char *>("PARAM"), const_cast<wchar_t *>(param_label.c_str()));
        ProUITableCellLabelSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.item_name.c_str()), const_cast<char *>("COLOR"), const_cast<wchar_t *>(color_label.c_str()));
        ProUITableCellBackgroundColorSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.item_name.c_str()), const_cast<char *>("COLOR"), bg);
        ProUITableCellForegroundColorSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.item_name.c_str()), const_cast<char *>("COLOR"), fg);
        ProUITableCellHelptextStringSet(dialog, const_cast<char *>(config.table_comp), const_cast<char *>(candidate.item_name.c_str()), const_cast<char *>("COLOR"), const_cast<wchar_t *>(color_label.c_str()));
    }
}

void ReloadPreviewAssignments(char *dialog,
                              RandomColorDialogState *state,
                              const RandomColorDialogConfig &config,
                              const RandomColorDialogLogSink &log_sink)
{
    if (dialog == nullptr || state == nullptr || state->candidates == nullptr) {
        return;
    }

    const std::vector<std::string> previously_selected = GetSelectedRowNames(dialog, state, config);
    std::wstring error_text;
    const std::vector<core::RandomColorEntry> colors =
        autobbox::application::LoadRandomColorEntriesFromLibraryPath(state->library_path, error_text);
    state->loaded_colors = colors;
    RebuildColorOptionChoices(*state);
    state->clear_colors_mode = false;
    state->library_error = error_text;
    state->preview_assignments.clear();
    state->parameter_previews.clear();
    state->parameter_previews =
        autobbox::application::BuildParameterColorPreview(*state->candidates, colors, state->parameter_name);
    if (state->mode == core::RandomColorMode::Random) {
        state->preview_assignments =
            autobbox::application::BuildRandomColorAssignments(*state->candidates, colors);
    }

    PopulateModelTable(dialog, state, config);
    if (!previously_selected.empty()) {
        SetSelectedRows(dialog, state, previously_selected);
    } else {
        SetDialogListState(dialog, state, PROUI_SET);
    }

    LogLine(log_sink,
            "random-color-dialog preview file=%s colors=%d assignments=%d",
            autobbox::common::WToA(state->library_path.c_str()).c_str(),
            static_cast<int>(colors.size()),
            static_cast<int>(state->preview_assignments.size()));
    if (state->mode == core::RandomColorMode::Parameter) {
        LogLine(log_sink,
                "random-color-dialog parameter-preview param=%s rows=%d",
                autobbox::common::WToA(state->parameter_name.c_str()).c_str(),
                static_cast<int>(state->parameter_previews.size()));
    }
}

void RefreshDialogList(char *dialog,
                       RandomColorDialogState *state,
                       const RandomColorDialogConfig &config,
                       const RandomColorDialogLogSink &log_sink,
                       bool preserve_random_targets)
{
    if (dialog == nullptr || state == nullptr) {
        return;
    }
    if (state->candidates_storage.empty() && state->candidates != nullptr) {
        state->candidates_storage = *state->candidates;
    }
    state->candidates = &state->candidates_storage;

    const std::vector<std::string> previously_selected =
        GetSelectedRowNames(dialog, state, config);

    autobbox::application::RefreshRandomColorCandidateAppearances(state->candidates_storage);

    std::wstring error_text;
    const std::vector<core::RandomColorEntry> colors =
        autobbox::application::LoadRandomColorEntriesFromLibraryPath(state->library_path, error_text);
    state->loaded_colors = colors;
    RebuildColorOptionChoices(*state);
    state->library_error = error_text;
    state->parameter_previews =
        autobbox::application::BuildParameterColorPreview(state->candidates_storage, colors, state->parameter_name);

    if (state->mode == core::RandomColorMode::Random) {
        if (!preserve_random_targets || state->preview_assignments.empty()) {
            state->preview_assignments =
                autobbox::application::BuildRandomColorAssignments(state->candidates_storage, colors);
        } else {
            for (core::RandomColorAssignment &assignment : state->preview_assignments) {
                for (const core::RandomColorCandidate &candidate : state->candidates_storage) {
                    if (candidate.item_name == assignment.candidate.item_name) {
                        assignment.candidate = candidate;
                        break;
                    }
                }
            }
        }
    }

    PopulateModelTable(dialog, state, config);
    if (!previously_selected.empty()) {
        SetSelectedRows(dialog, state, previously_selected);
    } else {
        SetDialogListState(dialog, state, PROUI_SET);
    }

    LogLine(log_sink,
            "random-color-dialog refresh-list file=%s colors=%d assignments=%d candidates=%d",
            autobbox::common::WToA(state->library_path.c_str()).c_str(),
            static_cast<int>(colors.size()),
            static_cast<int>(state->preview_assignments.size()),
            static_cast<int>(state->candidates_storage.size()));
}

void RerandomizeSelectedAssignments(char *dialog,
                                    RandomColorDialogState *state,
                                    const RandomColorDialogConfig &config,
                                    const RandomColorDialogLogSink &log_sink)
{
    if (dialog == nullptr || state == nullptr || state->candidates == nullptr) {
        return;
    }

    const std::vector<std::string> selected_rows = GetSelectedRowNames(dialog, state, config);
    if (selected_rows.empty()) {
        LogLine(log_sink, "random-color-dialog rerandomized skipped reason=no-selected-rows");
        ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u91cd\u65b0\u968f\u673a",
            L"\u8bf7\u5148\u52fe\u9009\u9700\u8981\u91cd\u65b0\u968f\u673a\u989c\u8272\u7684\u6a21\u578b\u3002");
        return;
    }

    std::wstring error_text;
    const std::vector<core::RandomColorEntry> colors =
        autobbox::application::LoadRandomColorEntriesFromLibraryPath(state->library_path, error_text);
    state->loaded_colors = colors;
    RebuildColorOptionChoices(*state);
    state->clear_colors_mode = false;
    state->library_error = error_text;
    state->parameter_previews =
        autobbox::application::BuildParameterColorPreview(*state->candidates, colors, state->parameter_name);

    std::vector<core::RandomColorEntry> available_colors =
        ExcludeUnselectedTargetColors(colors, state->preview_assignments, selected_rows);
    if (available_colors.empty() && !colors.empty()) {
        LogLine(log_sink,
                "random-color-dialog rerandomized fallback reason=all-colors-used-by-unselected selected=%d colors=%d",
                static_cast<int>(selected_rows.size()),
                static_cast<int>(colors.size()));
        available_colors = colors;
    }

    const std::vector<core::RandomColorCandidate> selected_candidates =
        FilterCandidatesByItemNames(*state->candidates, selected_rows);
    const std::vector<core::RandomColorAssignment> new_assignments =
        autobbox::application::BuildRandomColorAssignments(selected_candidates, available_colors);
    for (const core::RandomColorAssignment &assignment : new_assignments) {
        if (core::RandomColorAssignment *existing =
                FindPreviewAssignment(*state, assignment.candidate.item_name)) {
            existing->color = assignment.color;
        } else {
            state->preview_assignments.push_back(assignment);
        }
    }

    PopulateModelTable(dialog, state, config);
    SetSelectedRows(dialog, state, selected_rows);

    LogLine(log_sink,
            "random-color-dialog rerandomized-selected file=%s selected=%d colors=%d available=%d assignments=%d",
            autobbox::common::WToA(state->library_path.c_str()).c_str(),
            static_cast<int>(selected_rows.size()),
            static_cast<int>(colors.size()),
            static_cast<int>(available_colors.size()),
            static_cast<int>(new_assignments.size()));
}

void OnBrowseFile(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<RandomColorDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return;
    }

    std::wstring selected_path;
    if (!ChooseDmtFile(runtime->state->library_path, selected_path)) {
        LogLine(runtime->log_sink, "random-color-dialog browse status=cancelled");
        return;
    }

    runtime->state->library_path = selected_path;
    runtime->state->clear_colors_mode = false;
    ReadModeControls(dialog, runtime->state, *runtime->config);
    UpdateFileLabel(*runtime->config, *runtime->state);
    ReloadPreviewAssignments(dialog, runtime->state, *runtime->config, runtime->log_sink);
    if (runtime->state->mode == core::RandomColorMode::Random && !runtime->state->library_error.empty()) {
        ShowSimpleMessageDialog(PROUIMESSAGE_WARNING, L"\u968f\u673a\u4e0a\u8272", runtime->state->library_error.c_str());
    }
}

std::wstring BuildAppendLibrarySummary(const autobbox::application::RandomColorLibraryAppendResult &result)
{
    std::wstring summary = L"\u5df2\u52a0\u5165\u5916\u89c2\uff1a" + std::to_wstring(result.added_count);
    if (result.duplicate_count > 0) {
        summary += L"\n\u91cd\u590d\u8df3\u8fc7\uff1a" + std::to_wstring(result.duplicate_count);
    }
    if (result.no_current_appearance_count > 0) {
        summary += L"\n\u65e0\u5f53\u524d\u5916\u89c2\uff1a" + std::to_wstring(result.no_current_appearance_count);
    }
    if (result.mixed_appearance_count > 0) {
        summary += L"\n\u591a\u79cd\u5916\u89c2\u8df3\u8fc7\uff1a" + std::to_wstring(result.mixed_appearance_count);
    }
    if (result.used_fallback_library) {
        summary += L"\n\u5f53\u524d DMT \u4e0d\u53ef\u5199\uff0c\u5df2\u6539\u5199\u5165\u7528\u6237\u5916\u89c2\u5e93\u3002";
    }
    if (!result.actual_library_path.empty()) {
        summary += L"\n\u5916\u89c2\u5e93\uff1a" + result.actual_library_path;
    }
    return summary;
}

void OnAddCurrentAppearancesToLibrary(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<RandomColorDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return;
    }

    ReadModeControls(dialog, runtime->state, *runtime->config);
    const std::vector<std::string> previously_selected =
        GetSelectedRowNames(dialog, runtime->state, *runtime->config);
    const std::vector<core::RandomColorCandidate> selected_candidates =
        CollectSelectedCandidates(dialog, runtime->state, *runtime->config);
    if (selected_candidates.empty()) {
        ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u52a0\u5165\u5916\u89c2\u5e93",
            L"\u8bf7\u5148\u52fe\u9009\u8981\u52a0\u5165\u5916\u89c2\u5e93\u7684\u6a21\u578b\u3002");
        return;
    }

    autobbox::application::RandomColorLibraryAppendResult result =
        autobbox::application::AppendCurrentAppearancesToLibrary(
            selected_candidates,
            runtime->state->library_path,
            runtime->log_sink);

    if (result.write_failed) {
        ShowSimpleMessageDialog(
            PROUIMESSAGE_ERROR,
            L"\u52a0\u5165\u5916\u89c2\u5e93",
            result.actual_library_path.empty()
                ? L"\u5199\u5165\u5916\u89c2\u5e93\u5931\u8d25\u3002"
                : (L"\u5199\u5165\u5916\u89c2\u5e93\u5931\u8d25\uff1a\n" + result.actual_library_path).c_str());
        return;
    }

    if (!result.actual_library_path.empty() &&
        (result.added_count > 0 || result.duplicate_count > 0)) {
        runtime->state->library_path = result.actual_library_path;
    }
    runtime->state->clear_colors_mode = false;
    UpdateFileLabel(*runtime->config, *runtime->state);
    ReloadPreviewAssignments(dialog, runtime->state, *runtime->config, runtime->log_sink);
    if (!previously_selected.empty()) {
        SetSelectedRows(dialog, runtime->state, previously_selected);
    }

    const std::wstring summary = BuildAppendLibrarySummary(result);
    ShowSimpleMessageDialog(
        result.added_count > 0 ? PROUIMESSAGE_INFO : PROUIMESSAGE_WARNING,
        L"\u52a0\u5165\u5916\u89c2\u5e93",
        summary.c_str());
}

std::wstring BuildWriteColorParamSummary(const autobbox::application::RandomColorParameterWriteResult &result)
{
    std::wstring summary = L"\u989c\u8272\u53c2\u6570\u56de\u5199\uff1a" + std::to_wstring(result.success_count);
    if (result.created_count > 0) {
        summary += L"\n\u65b0\u589e\u53c2\u6570\uff1a" + std::to_wstring(result.created_count);
    }
    if (result.updated_count > 0) {
        summary += L"\n\u66f4\u65b0\u5df2\u6709\u53c2\u6570\uff1a" + std::to_wstring(result.updated_count);
    }
    if (result.failure_count > 0) {
        summary += L"\n\u56de\u5199\u5931\u8d25\uff1a" + std::to_wstring(result.failure_count);
    }
    if (result.no_current_appearance_count > 0) {
        summary += L"\n\u65e0\u5f53\u524d\u5916\u89c2\uff1a" + std::to_wstring(result.no_current_appearance_count);
    }
    if (result.mixed_appearance_count > 0) {
        summary += L"\n\u591a\u79cd\u5916\u89c2\u8df3\u8fc7\uff1a" + std::to_wstring(result.mixed_appearance_count);
    }
    summary += L"\n\u53c2\u6570\u540d\uff1a\u989c\u8272";
    return summary;
}

void OnWriteCurrentAppearanceParameter(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<RandomColorDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return;
    }

    const std::vector<core::RandomColorCandidate> selected_candidates =
        CollectSelectedCandidates(dialog, runtime->state, *runtime->config);
    if (selected_candidates.empty()) {
        ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u56de\u5199\u53c2\u6570",
            L"\u8bf7\u5148\u52fe\u9009\u8981\u56de\u5199\u989c\u8272\u53c2\u6570\u7684\u6a21\u578b\u3002");
        return;
    }

    const autobbox::application::RandomColorParameterWriteResult result =
        autobbox::application::WriteCurrentAppearanceColorParameters(
            selected_candidates,
            runtime->log_sink);
    const std::wstring summary = BuildWriteColorParamSummary(result);
    ShowSimpleMessageDialog(
        result.success_count > 0 ? PROUIMESSAGE_INFO : PROUIMESSAGE_WARNING,
        L"\u56de\u5199\u53c2\u6570",
        summary.c_str());
}

void OnModelTableSelect(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<RandomColorDialogRuntime *>(app_data);
    if (runtime == nullptr ||
        runtime->state == nullptr ||
        runtime->config == nullptr ||
        runtime->state->opening_color_palette ||
        runtime->state->mode != core::RandomColorMode::Random ||
        runtime->state->clear_colors_mode ||
        runtime->state->loaded_colors.empty()) {
        return;
    }

    char *row_name = nullptr;
    char *column_name = nullptr;
    if (ProUITableFocusCellGet(
            dialog,
            const_cast<char *>(runtime->config->table_comp),
            &row_name,
            &column_name) != PRO_TK_NO_ERROR ||
        row_name == nullptr ||
        column_name == nullptr) {
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
    ProStringFree(row_name);
    ProStringFree(column_name);
    if (column != "COLOR") {
        return;
    }

    core::RandomColorAssignment *assignment = FindPreviewAssignment(*runtime->state, row);
    if (assignment == nullptr) {
        return;
    }

    int selected_index = FindColorChoiceIndexByAssignment(*runtime->state, assignment->color);
    runtime->state->opening_color_palette = true;
    const bool selected = PromptColorPaletteDialog(
        runtime->state->loaded_colors,
        runtime->state->color_option_labels_storage,
        selected_index,
        selected_index,
        runtime->log_sink);
    if (selected &&
        selected_index >= 0 &&
        static_cast<size_t>(selected_index) < runtime->state->loaded_colors.size()) {
        const std::vector<std::string> previously_selected =
            GetSelectedRowNames(dialog, runtime->state, *runtime->config);
        SetPreviewAssignmentColor(*runtime->state, row, runtime->state->loaded_colors[static_cast<size_t>(selected_index)]);
        PopulateModelTable(dialog, runtime->state, *runtime->config);
        if (!previously_selected.empty()) {
            SetSelectedRows(dialog, runtime->state, previously_selected);
        }
    }
    runtime->state->opening_color_palette = false;
}

bool ValidateDialogSelection(char *dialog,
                             const RandomColorDialogState &state,
                             const RandomColorDialogConfig &config,
                             const RandomColorDialogLogSink &log_sink)
{
    if (dialog == nullptr || state.candidates == nullptr) {
        return false;
    }
    const std::vector<std::string> selected_rows = GetSelectedRowNames(dialog, &state, config);
    if (selected_rows.empty()) {
        LogLine(log_sink, "random-color-dialog validate failed reason=no-model-selected");
        ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u968f\u673a\u4e0a\u8272",
            L"\u8bf7\u81f3\u5c11\u9009\u62e9\u4e00\u4e2a\u6a21\u578b\u3002");
        return false;
    }
    if (state.clear_colors_mode) {
        return true;
    }
    if (state.mode == core::RandomColorMode::Parameter) {
        return true;
    }
    if (state.preview_assignments.empty()) {
        LogLine(log_sink, "random-color-dialog validate failed reason=no-colors-loaded");
        ShowSimpleMessageDialog(
            PROUIMESSAGE_WARNING,
            L"\u968f\u673a\u4e0a\u8272",
            state.library_error.empty()
                ? L"\u9009\u4e2d\u7684 .dmt \u6587\u4ef6\u672a\u52a0\u8f7d\u5230\u4efb\u4f55\u989c\u8272\u3002"
                : state.library_error.c_str());
        return false;
    }
    return true;
}

void OnApplySettings(char *dialog, char *, ProAppData app_data)
{
    auto *runtime = reinterpret_cast<RandomColorDialogRuntime *>(app_data);
    if (runtime == nullptr || runtime->state == nullptr || runtime->config == nullptr) {
        return;
    }
    ReadModeControls(dialog, runtime->state, *runtime->config);
    if (!runtime->state->clear_colors_mode && runtime->state->mode == core::RandomColorMode::Parameter) {
        RefreshDialogList(dialog, runtime->state, *runtime->config, runtime->log_sink, true);
    }
    if (!ValidateDialogSelection(dialog, *runtime->state, *runtime->config, runtime->log_sink)) {
        return;
    }

    const std::vector<std::string> selected_row_names =
        GetSelectedRowNames(dialog, runtime->state, *runtime->config);
    std::wstring summary_text;
    bool ok = false;
    const wchar_t *title = L"\u968f\u673a\u4e0a\u8272";

    if (runtime->state->clear_colors_mode) {
        std::vector<core::RandomColorCandidate> clear_targets;
        for (const std::string &row_name : selected_row_names) {
            for (const core::RandomColorCandidate &candidate : *runtime->state->candidates) {
                if (candidate.item_name == row_name) {
                    clear_targets.push_back(candidate);
                    break;
                }
            }
        }
        title = L"\u6e05\u9664\u989c\u8272";
        ok = autobbox::application::ClearRandomColors(
            clear_targets,
            summary_text,
            runtime->log_sink);
    } else if (runtime->state->mode == core::RandomColorMode::Parameter) {
        std::vector<core::RandomColorParameterPreview> parameter_selected;
        for (const std::string &row_name : selected_row_names) {
            if (const core::RandomColorParameterPreview *preview = FindParameterPreview(*runtime->state, row_name)) {
                parameter_selected.push_back(*preview);
            }
        }
        title = L"\u6309\u53c2\u6570\u4e0a\u8272";
        ok = autobbox::application::ApplyParameterColors(
            parameter_selected,
            summary_text,
            runtime->log_sink);
    } else {
        std::vector<core::RandomColorAssignment> selected_assignments;
        for (const std::string &row_name : selected_row_names) {
            if (const core::RandomColorAssignment *assignment = FindPreviewAssignment(*runtime->state, row_name)) {
                selected_assignments.push_back(*assignment);
            }
        }
        ok = autobbox::application::ApplyRandomColors(
            selected_assignments,
            summary_text,
            runtime->log_sink);
    }

    ShowSimpleMessageDialog(
        ok ? PROUIMESSAGE_INFO : PROUIMESSAGE_ERROR,
        title,
        summary_text.empty()
            ? (ok ? L"\u64cd\u4f5c\u5b8c\u6210\u3002" : L"\u64cd\u4f5c\u5931\u8d25\u3002")
            : summary_text.c_str());

    runtime->state->clear_colors_mode = false;
    RefreshDialogList(dialog, runtime->state, *runtime->config, runtime->log_sink, true);
}

ProError TryCreateDialog(const RandomColorDialogConfig &config,
                         const RandomColorDialogLogSink &log_sink,
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
        std::string("usascii\\resource\\") + base_name,
    };

    ProError last = PRO_TK_GENERAL_ERROR;
    for (const std::string &resource : rel_candidates) {
        last = ProUIDialogCreate(const_cast<char *>(config.dialog_inst_name), const_cast<char *>(resource.c_str()));
        LogLine(log_sink, "random-color-dialog create try resource=%s status=%d", resource.c_str(), static_cast<int>(last));
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
            text_root + "\\text\\usascii\\resource\\" + base_name,
            text_root + "\\text\\usascii\\resource\\" + base_name + ".res",
        };
        for (const std::string &path : abs_candidates) {
            last = ProUIDialogCreate(const_cast<char *>(config.dialog_inst_name), const_cast<char *>(path.c_str()));
            LogLine(log_sink, "random-color-dialog create try resource=%s status=%d", path.c_str(), static_cast<int>(last));
            if (last == PRO_TK_NO_ERROR) {
                used_resource = path;
                return last;
            }
        }
    }

    return last;
}

} // namespace

bool PromptRandomColorDialog(const std::vector<core::RandomColorCandidate> &candidates,
                             const std::wstring &default_library_path,
                             std::vector<core::RandomColorAssignment> &selected,
                             std::vector<core::RandomColorParameterPreview> &parameter_selected,
                             std::vector<core::RandomColorCandidate> &clear_targets,
                             bool &use_parameter_colors,
                             std::wstring &parameter_name,
                             bool &clear_all_colors,
                             bool &cancelled,
                             const RandomColorDialogLogSink &log_sink)
{
    selected.clear();
    parameter_selected.clear();
    clear_targets.clear();
    use_parameter_colors = false;
    parameter_name = L"\u989c\u8272";
    clear_all_colors = false;
    cancelled = false;
    if (candidates.empty()) {
        cancelled = true;
        return false;
    }

    const RandomColorDialogConfig config = DefaultRandomColorDialogConfig();
    std::string used_resource;
    const ProError create_status = TryCreateDialog(config, log_sink, used_resource);
    if (create_status != PRO_TK_NO_ERROR) {
        ShowSimpleMessageDialog(
            PROUIMESSAGE_ERROR,
            L"\u4e0a\u8272\u5de5\u5177",
            L"\u6253\u5f00\u4e0a\u8272\u5de5\u5177\u5bf9\u8bdd\u6846\u5931\u8d25\u3002");
        cancelled = true;
        return false;
    }

    RandomColorDialogState state;
    state.candidates_storage = candidates;
    state.candidates = &state.candidates_storage;
    state.library_path = default_library_path;

    RandomColorDialogRuntime runtime;
    runtime.state = &state;
    runtime.config = &config;
    runtime.log_sink = log_sink;

    ProUIDialogTitleSet(const_cast<char *>(config.dialog_inst_name), const_cast<wchar_t *>(L"\u4e0a\u8272\u5de5\u5177"));
    ApplyChineseDialogText(config);
    ConfigureModeControls(config, state);
    UpdateFileLabel(config, state);
    ReloadPreviewAssignments(const_cast<char *>(config.dialog_inst_name), &state, config, log_sink);

    ProUIPushbuttonActivateActionSet(const_cast<char *>(config.dialog_inst_name), const_cast<char *>(config.browse_comp), OnBrowseFile, &runtime);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(config.dialog_inst_name), const_cast<char *>(config.randomize_comp), OnRandomizePreview, &runtime);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(config.dialog_inst_name), const_cast<char *>(config.refresh_comp), OnRefreshList, &runtime);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(config.dialog_inst_name), const_cast<char *>(config.select_all_comp), OnSelectAll, &runtime);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(config.dialog_inst_name), const_cast<char *>(config.clear_comp), OnClearSelection, &runtime);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(config.dialog_inst_name), const_cast<char *>(config.add_library_comp), OnAddCurrentAppearancesToLibrary, &runtime);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(config.dialog_inst_name), const_cast<char *>(config.write_param_comp), OnWriteCurrentAppearanceParameter, &runtime);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(config.dialog_inst_name), const_cast<char *>(config.ok_comp), OnApplySettings, &runtime);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(config.dialog_inst_name), const_cast<char *>(config.clear_colors_comp), OnClearColorsPreview, &runtime);
    ProUIOptionmenuSelectActionSet(const_cast<char *>(config.dialog_inst_name), const_cast<char *>(config.mode_comp), OnModeChanged, &runtime);
    ProUITableSelectActionSet(const_cast<char *>(config.dialog_inst_name), const_cast<char *>(config.table_comp), OnModelTableSelect, &runtime);
    ProUIPushbuttonActivateActionSet(
        const_cast<char *>(config.dialog_inst_name),
        const_cast<char *>(config.cancel_comp),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_cancel)));
    ProUIDialogCloseActionSet(
        const_cast<char *>(config.dialog_inst_name),
        OnDialogExit,
        reinterpret_cast<ProAppData>(static_cast<std::intptr_t>(config.status_cancel)));
    ProUIDialogDefaultbuttonSet(const_cast<char *>(config.dialog_inst_name), const_cast<char *>(config.ok_comp));

    int dialog_status = config.status_cancel;
    const ProError activate_status = ProUIDialogActivate(const_cast<char *>(config.dialog_inst_name), &dialog_status);
    if (activate_status != PRO_TK_NO_ERROR) {
        cancelled = true;
        ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
        return false;
    }
    if (dialog_status != config.status_ok) {
        cancelled = true;
        ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
        return false;
    }

    const std::vector<std::string> selected_row_names =
        GetSelectedRowNames(const_cast<char *>(config.dialog_inst_name), &state, config);
    if (state.clear_colors_mode) {
        clear_all_colors = true;
        for (const std::string &row_name : selected_row_names) {
            for (const core::RandomColorCandidate &candidate : candidates) {
                if (candidate.item_name == row_name) {
                    clear_targets.push_back(candidate);
                    break;
                }
            }
        }
    } else if (state.mode == core::RandomColorMode::Parameter) {
        use_parameter_colors = true;
        parameter_name = state.parameter_name;
        for (const std::string &row_name : selected_row_names) {
            if (const core::RandomColorParameterPreview *preview = FindParameterPreview(state, row_name)) {
                parameter_selected.push_back(*preview);
            }
        }
    } else {
        for (const std::string &row_name : selected_row_names) {
            if (const core::RandomColorAssignment *assignment = FindPreviewAssignment(state, row_name)) {
                selected.push_back(*assignment);
            }
        }
    }

    ProUIDialogDestroy(const_cast<char *>(config.dialog_inst_name));
    return true;
}

} // namespace autobbox::ui

