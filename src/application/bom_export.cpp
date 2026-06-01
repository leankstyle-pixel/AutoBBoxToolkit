#include "autobbox/application/bom_export.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <algorithm>
#include <cstdio>

namespace autobbox::application {

namespace {

std::wstring HtmlEscape(const std::wstring &value)
{
    std::wstring escaped;
    escaped.reserve(value.size() + 8);
    for (wchar_t ch : value) {
        switch (ch) {
        case L'&':
            escaped += L"&amp;";
            break;
        case L'<':
            escaped += L"&lt;";
            break;
        case L'>':
            escaped += L"&gt;";
            break;
        case L'"':
            escaped += L"&quot;";
            break;
        case L'\r':
            break;
        case L'\n':
            escaped += L"<br/>";
            break;
        default:
            escaped.push_back(ch);
            break;
        }
    }
    return escaped;
}

bool BuildBomExportPath(std::wstring &file_path_out)
{
    file_path_out.clear();
    const std::wstring cwd = autobbox::common::CurrentWorkingDirectoryW();
    if (cwd.empty()) {
        return false;
    }

    const std::wstring out_dir = autobbox::common::JoinPath(cwd, L"bom");
    if (!autobbox::common::EnsureDirectoryW(out_dir)) {
        return false;
    }

    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t name[64] = {0};
    std::swprintf(name,
                  sizeof(name) / sizeof(name[0]) - 1,
                  L"bom_%04d%02d%02d_%02d%02d%02d.xls",
                  st.wYear,
                  st.wMonth,
                  st.wDay,
                  st.wHour,
                  st.wMinute,
                  st.wSecond);
    file_path_out = autobbox::common::JoinPath(out_dir, name);
    return true;
}

std::wstring MakeBomDraftKey(const std::wstring &row_key, const std::wstring &param_name)
{
    std::wstring key(row_key);
    key.push_back(L'\x1f');
    key += param_name;
    return key;
}

std::wstring FormatDraftDisplayValue(const core::BomAvailableParam &column, const std::wstring &raw_value)
{
    (void)column;
    return raw_value;
}

} // namespace

const core::BomAvailableParam *FindBomAvailableParam(const core::BomToolState &state, const std::wstring &name)
{
    const auto it = state.available_index_by_name.find(name);
    if (it == state.available_index_by_name.end()) {
        return nullptr;
    }
    return &state.available_params[it->second];
}

const core::BomModelSnapshot *FindBomSnapshot(const core::BomToolState &state, ProMdl mdl)
{
    const auto it = state.snapshots_by_mdl.find(reinterpret_cast<std::uintptr_t>(mdl));
    if (it == state.snapshots_by_mdl.end()) {
        return nullptr;
    }
    return &it->second;
}

core::BomCellView BuildBomCellView(const core::BomToolState &state,
                                   const core::BomRow &row,
                                   const core::BomAvailableParam &column)
{
    core::BomCellView view;
    std::wstring first_value;
    std::wstring first_readonly_reason;
    bool have_first_value = false;

    for (ProMdl mdl : row.models) {
        const core::BomModelSnapshot *snapshot = FindBomSnapshot(state, mdl);
        if (snapshot == nullptr) {
            ++view.missing_targets;
            continue;
        }

        const auto it = snapshot->params.find(column.name);
        if (it == snapshot->params.end() || !it->second.exists) {
            ++view.missing_targets;
            continue;
        }

        ++view.existing_targets;
        const core::BomModelParamInfo &info = it->second;
        if (!have_first_value) {
            first_value = info.display_value;
            have_first_value = true;
        } else if (first_value != info.display_value) {
            view.value_mixed = true;
        }

        if (info.writable) {
            ++view.writable_targets;
        } else {
            ++view.readonly_targets;
            if (first_readonly_reason.empty()) {
                first_readonly_reason = info.readonly_reason.empty() ? L"READONLY" : info.readonly_reason;
            }
        }
    }

    if (view.existing_targets <= 0) {
        view.actual_value.clear();
    } else if (view.value_mixed) {
        view.actual_value = L"<MULTI>";
    } else {
        view.actual_value = first_value;
    }

    if (column.mixed_type) {
        view.editable = false;
        view.readonly_reason = L"MIXED_TYPE";
    } else if (!column.write_supported) {
        view.editable = false;
        view.readonly_reason = L"UNSUPPORTED_TYPE";
    } else if (view.readonly_targets > 0) {
        view.editable = false;
        view.readonly_reason = first_readonly_reason;
    } else {
        view.editable = true;
    }

    const std::wstring draft_key = MakeBomDraftKey(row.key, column.name);
    const auto draft_it = state.draft_values.find(draft_key);
    if (draft_it != state.draft_values.end()) {
        view.modified = true;
        view.display_value = FormatDraftDisplayValue(column, draft_it->second);
    } else {
        view.display_value = view.actual_value;
    }

    if (view.editable) {
        view.helptext = L"Click to edit draft value.";
        if (view.missing_targets > 0 && view.existing_targets > 0) {
            view.helptext += L" Missing parameters will be created on update.";
        } else if (view.missing_targets > 0 && view.existing_targets == 0) {
            view.helptext += L" This row will create the parameter on update.";
        }
    } else {
        view.helptext = view.readonly_reason;
    }

    return view;
}

core::BomRenderStats BuildBomRenderStats(const core::BomToolState &state)
{
    core::BomRenderStats stats;
    stats.row_count = static_cast<int>(state.rows.size());
    stats.column_count = 5 + static_cast<int>(state.visible_param_names.size());

    for (const core::BomRow &row : state.rows) {
        for (const std::wstring &param_name : state.visible_param_names) {
            const core::BomAvailableParam *column = FindBomAvailableParam(state, param_name);
            if (column == nullptr) {
                continue;
            }
            const core::BomCellView view = BuildBomCellView(state, row, *column);
            if (view.editable) {
                ++stats.writable_cells;
            } else {
                ++stats.readonly_cells;
            }
        }
    }
    return stats;
}

std::wstring BuildBomSummaryText(const core::BomToolState &state)
{
    const core::BomRenderStats stats = BuildBomRenderStats(state);
    wchar_t buffer[512] = {0};
    std::swprintf(buffer,
                  sizeof(buffer) / sizeof(buffer[0]) - 1,
                  L"BOM rows=%d | checked rows=%d | shown cols=%d | available params=%d | parts=%d | assemblies=%d | max level=%d | writable=%d | readonly=%d",
                  stats.row_count,
                  static_cast<int>(state.checked_update_row_keys.size()),
                  stats.column_count,
                  static_cast<int>(state.available_params.size()),
                  static_cast<int>(state.parts_option),
                  static_cast<int>(state.assemblies_option),
                  state.max_bom_level,
                  stats.writable_cells,
                  stats.readonly_cells);
    std::wstring summary(buffer);
    if (!state.filter_model_name.empty() ||
        !state.filter_param_name.empty() ||
        !state.filter_param_value.empty()) {
        summary += L" | filter model='";
        summary += state.filter_model_name;
        summary += L"' param='";
        summary += state.filter_param_name;
        summary += L"' value='";
        summary += state.filter_param_value;
        summary += L"'";
    }
    return summary;
}

bool ExportBomExcel(const core::BomToolState &state, std::wstring &file_path_out)
{
    if (!BuildBomExportPath(file_path_out)) {
        return false;
    }

    std::wstring html;
    html += L"<html><head><meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\"/>";
    html += L"<style>";
    html += L"table{border-collapse:collapse;}";
    html += L"td,th{border:1px solid #999;padding:4px 8px;white-space:nowrap;}";
    html += L"</style></head><body><table>";

    html += L"<tr><th>Index</th><th>Level</th><th>Model</th><th>Qty</th>";
    for (const std::wstring &param_name : state.visible_param_names) {
        html += L"<th>";
        html += HtmlEscape(param_name);
        html += L"</th>";
    }
    html += L"</tr>";

    for (size_t i = 0; i < state.rows.size(); ++i) {
        const core::BomRow &row = state.rows[i];
        html += L"<tr><td>";
        html += HtmlEscape(std::to_wstring(i + 1));
        html += L"</td><td>";
        html += HtmlEscape(std::to_wstring(std::max(1, row.level)));
        html += L"</td><td>";
        html += HtmlEscape(row.display_name);
        html += L"</td><td>";
        html += HtmlEscape(std::to_wstring(row.quantity));
        html += L"</td>";
        for (const std::wstring &param_name : state.visible_param_names) {
            html += L"<td>";
            const core::BomAvailableParam *column = FindBomAvailableParam(state, param_name);
            if (column != nullptr) {
                const core::BomCellView view = BuildBomCellView(state, row, *column);
                html += HtmlEscape(view.display_value);
            }
            html += L"</td>";
        }
        html += L"</tr>";
    }

    html += L"</table></body></html>";

    const std::string utf8 = autobbox::common::WideToUtf8(html);
    std::FILE *fp = autobbox::common::OpenFile(autobbox::common::WToA(file_path_out.c_str()), "wb");
    if (fp == nullptr) {
        return false;
    }

    const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    std::fwrite(bom, 1, sizeof(bom), fp);
    if (!utf8.empty()) {
        std::fwrite(utf8.data(), 1, utf8.size(), fp);
    }
    std::fclose(fp);
    return true;
}

bool ExportBomCsv(const core::BomToolState &state, std::wstring &file_path_out)
{
    return ExportBomExcel(state, file_path_out);
}

} // namespace autobbox::application
