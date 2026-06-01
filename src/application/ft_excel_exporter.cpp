#include "autobbox/application/ft_excel_exporter.h"

#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdio>
#include <map>
#include <set>

namespace autobbox::application {
namespace {

std::wstring XmlEscape(const std::wstring &value)
{
    std::wstring out;
    for (wchar_t ch : value) {
        switch (ch) {
        case L'&': out += L"&amp;"; break;
        case L'<': out += L"&lt;"; break;
        case L'>': out += L"&gt;"; break;
        case L'\"': out += L"&quot;"; break;
        case L'\'': out += L"&apos;"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

std::wstring SanitizeSheetName(std::wstring text)
{
    for (wchar_t &ch : text) {
        if (ch == L'/' || ch == L'\\' || ch == L':' || ch == L'*' || ch == L'?' || ch == L'[' || ch == L']') ch = L'_';
    }
    if (text.empty()) text = L"FT";
    if (text.size() > 28) text = text.substr(0, 20) + L"_" + std::to_wstring(static_cast<unsigned long>(std::hash<std::wstring>{}(text) % 1000000));
    return text;
}

std::map<std::wstring, std::wstring> BuildSheetMap(const std::vector<const core::FtLevelNode*> &levels)
{
    std::map<std::wstring, std::wstring> result;
    std::set<std::wstring> used;
    for (const auto *level : levels) {
        std::wstring base = L"FT_" + SanitizeSheetName(level->level_path);
        std::wstring name = base;
        int suffix = 1;
        while (used.find(name) != used.end()) name = base.substr(0, std::min<size_t>(24, base.size())) + L"_" + std::to_wstring(suffix++);
        used.insert(name);
        result[level->level_path] = name;
    }
    return result;
}

bool BuildExportPath(const wchar_t *prefix, std::wstring &out)
{
    const std::wstring cwd = autobbox::common::CurrentWorkingDirectoryW();
    if (cwd.empty()) return false;
    const std::wstring dir = autobbox::common::JoinPath(cwd, L"family_table");
    if (!autobbox::common::EnsureDirectoryW(dir)) return false;
    SYSTEMTIME st = {};
    GetLocalTime(&st);
    wchar_t name[128] = {0};
    std::swprintf(name, 127, L"%s_%04d%02d%02d_%02d%02d%02d.xls", prefix, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    out = autobbox::common::JoinPath(dir, name);
    return true;
}

void Cell(std::wstring &xml, const std::wstring &value)
{
    xml += L"<Cell><Data ss:Type=\"String\">";
    xml += XmlEscape(value);
    xml += L"</Data></Cell>";
}

void CellWithAttrs(std::wstring &xml, const std::wstring &value, const std::wstring &attrs)
{
    xml += L"<Cell";
    if (!attrs.empty()) {
        xml += L" ";
        xml += attrs;
    }
    xml += L"><Data ss:Type=\"String\">";
    xml += XmlEscape(value);
    xml += L"</Data></Cell>";
}

std::wstring EscapeSheetNameForReference(const std::wstring &sheet_name)
{
    std::wstring out;
    out.reserve(sheet_name.size());
    for (wchar_t ch : sheet_name) {
        if (ch == L'\'') out += L"''";
        else out.push_back(ch);
    }
    return out;
}

std::wstring SheetHyperlinkAttrs(const std::wstring &sheet_name)
{
    const std::wstring target = L"#'" + EscapeSheetNameForReference(sheet_name) + L"'!A1";
    return L"ss:HRef=\"" + XmlEscape(target) + L"\" ss:StyleID=\"Hyperlink\"";
}

void Row(std::wstring &xml, const std::vector<std::wstring> &values)
{
    xml += L"<Row>";
    for (const auto &v : values) Cell(xml, v);
    xml += L"</Row>\n";
}

void BeginSheet(std::wstring &xml, const std::wstring &name)
{
    xml += L"<Worksheet ss:Name=\"" + XmlEscape(name) + L"\"><Table>\n";
}

void EndSheet(std::wstring &xml)
{
    xml += L"</Table></Worksheet>\n";
}

void WriteStyles(std::wstring &xml)
{
    xml += L"<Styles>\n";
    xml += L"<Style ss:ID=\"Hyperlink\"><Font ss:Color=\"#0000FF\" ss:Underline=\"Single\"/></Style>\n";
    xml += L"</Styles>\n";
}

void WriteBackToIndexRow(std::wstring &xml)
{
    xml += L"<Row>";
    CellWithAttrs(xml, L"返回索引", SheetHyperlinkAttrs(L"INDEX"));
    xml += L"</Row>\n";
}

const core::FtCell *FindCell(const core::FtRow &row, const std::wstring &key)
{
    for (const auto &cell : row.cells) if (cell.column_key == key) return &cell;
    return nullptr;
}

void WriteIndex(std::wstring &xml, const std::vector<const core::FtLevelNode*> &levels, const std::map<std::wstring, std::wstring> &sheet_map)
{
    BeginSheet(xml, L"INDEX");
    Row(xml, {L"LEVEL_PATH", L"SHEET_NAME", L"GENERIC_NAME", L"PARENT_GENERIC_NAME", L"PARENT_INSTANCE_NAME", L"LEVEL_DEPTH", L"MODEL_TYPE"});
    for (const auto *level : levels) {
        const std::wstring &sheet_name = sheet_map.at(level->level_path);
        xml += L"<Row>";
        Cell(xml, level->level_path);
        CellWithAttrs(xml, sheet_name, SheetHyperlinkAttrs(sheet_name));
        Cell(xml, level->generic_name);
        Cell(xml, level->parent_generic_name);
        Cell(xml, level->parent_instance_name);
        Cell(xml, std::to_wstring(level->level_depth));
        Cell(xml, std::to_wstring(static_cast<int>(level->model_type)));
        xml += L"</Row>\n";
    }
    EndSheet(xml);
}

void WriteLevelSheet(std::wstring &xml, const core::FtLevelNode &level, const std::wstring &sheet_name)
{
    BeginSheet(xml, sheet_name);
    WriteBackToIndexRow(xml);
    std::vector<std::wstring> header;
    for (const auto &col : level.columns) if (col.visible) header.push_back(col.column_key);
    Row(xml, header);
    for (const auto &row : level.rows) {
        std::vector<std::wstring> values;
        for (const auto &col : level.columns) {
            if (!col.visible) continue;
            const core::FtCell *cell = FindCell(row, col.column_key);
            values.push_back(cell == nullptr ? L"" : cell->value);
        }
        Row(xml, values);
    }
    EndSheet(xml);
}

void WriteSchema(std::wstring &xml, const std::vector<const core::FtLevelNode*> &levels)
{
    BeginSheet(xml, L"FT_SCHEMA");
    WriteBackToIndexRow(xml);
    Row(xml, {L"LEVEL_PATH", L"COLUMN_KEY", L"COLUMN_DISPLAY_NAME", L"COLUMN_CATEGORY", L"VALUE_TYPE", L"IS_EDITABLE", L"IS_REQUIRED", L"SUPPORT_STATUS", L"SOURCE_SCOPE", L"REMARK"});
    for (const auto *level : levels) {
        for (const auto &col : level->columns) {
            Row(xml, {level->level_path, col.column_key, col.column_display_name, core::FtColumnCategoryName(col.column_category), col.value_type_name, col.editable ? L"TRUE" : L"FALSE", col.required ? L"TRUE" : L"FALSE", core::FtSupportStatusName(col.support_status), col.has_creo_item ? L"CREO" : L"PLUGIN", col.famtab_string});
        }
    }
    EndSheet(xml);
}

void WriteLog(std::wstring &xml, const core::FtWorkspace &workspace)
{
    BeginSheet(xml, L"FT_LOG");
    WriteBackToIndexRow(xml);
    Row(xml, {L"LEVEL_PATH", L"SEVERITY", L"OPERATION", L"STATUS", L"MESSAGE"});
    for (const auto &entry : workspace.logs) {
        Row(xml, {entry.level_path, entry.severity, entry.operation, std::to_wstring(static_cast<int>(entry.status)), entry.message});
    }
    EndSheet(xml);
}

bool WriteWorkbook(const core::FtWorkspace &workspace, const std::vector<const core::FtLevelNode*> &levels, const wchar_t *prefix, std::wstring &file_path_out)
{
    if (levels.empty() || !BuildExportPath(prefix, file_path_out)) return false;
    const auto sheet_map = BuildSheetMap(levels);
    std::wstring xml;
    xml += L"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    xml += L"<Workbook xmlns=\"urn:schemas-microsoft-com:office:spreadsheet\" xmlns:ss=\"urn:schemas-microsoft-com:office:spreadsheet\">\n";
    WriteStyles(xml);
    WriteIndex(xml, levels, sheet_map);
    for (const auto *level : levels) WriteLevelSheet(xml, *level, sheet_map.at(level->level_path));
    WriteSchema(xml, levels);
    WriteLog(xml, workspace);
    xml += L"</Workbook>\n";
    const std::string utf8 = autobbox::common::WideToUtf8(xml);
    std::FILE *fp = autobbox::common::OpenFile(autobbox::common::WToA(file_path_out.c_str()), "wb");
    if (fp == nullptr) return false;
    const unsigned char bom[3] = {0xEF, 0xBB, 0xBF};
    std::fwrite(bom, 1, sizeof(bom), fp);
    if (!utf8.empty()) std::fwrite(utf8.data(), 1, utf8.size(), fp);
    std::fclose(fp);
    return true;
}

} // namespace

bool ExportFtCurrentLevelExcel(const core::FtWorkspace &workspace, std::wstring &file_path_out)
{
    std::vector<const core::FtLevelNode*> levels;
    for (const auto &level : workspace.level_nodes) {
        if (level.level_path == workspace.active_level_path) {
            if (!level.pending_resolve) {
                levels.push_back(&level);
            }
            break;
        }
    }
    return WriteWorkbook(workspace, levels, L"family_table_current", file_path_out);
}

bool ExportFtAllLevelsExcel(const core::FtWorkspace &workspace, std::wstring &file_path_out)
{
    std::vector<const core::FtLevelNode*> levels;
    for (const auto &level : workspace.level_nodes) {
        if (level.pending_resolve) continue;
        levels.push_back(&level);
    }
    return WriteWorkbook(workspace, levels, L"family_table_all", file_path_out);
}

} // namespace autobbox::application
