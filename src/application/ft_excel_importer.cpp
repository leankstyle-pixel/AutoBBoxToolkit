#include "autobbox/application/ft_excel_importer.h"

#include "autobbox/application/ft_diff_engine.h"
#include "autobbox/application/ft_logger.h"
#include "autobbox/application/ft_support_matrix.h"
#include "autobbox/common/files.h"
#include "autobbox/common/strings.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <map>
#include <sstream>
#include <set>

namespace autobbox::application {
namespace {

std::wstring Unescape(std::wstring v)
{
    auto rep=[&](const wchar_t *a,const wchar_t *b){ size_t p=0; while((p=v.find(a,p))!=std::wstring::npos){ v.replace(p,wcslen(a),b); p+=wcslen(b);} };
    rep(L"&lt;",L"<"); rep(L"&gt;",L">"); rep(L"&quot;",L"\""); rep(L"&apos;",L"'"); rep(L"&amp;",L"&");
    return v;
}

std::string ReadAll(const std::wstring &path)
{
    std::FILE *fp = autobbox::common::OpenFile(autobbox::common::WToA(path.c_str()), "rb");
    if (fp == nullptr) return {};
    std::fseek(fp, 0, SEEK_END);
    long n = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    std::string data;
    if (n > 0) {
        data.resize(static_cast<size_t>(n));
        std::fread(&data[0], 1, data.size(), fp);
    }
    std::fclose(fp);
    if (data.size() >= 3 && (unsigned char)data[0] == 0xEF && (unsigned char)data[1] == 0xBB && (unsigned char)data[2] == 0xBF) data.erase(0,3);
    return data;
}

std::wstring ToWideLossy(const std::string &s)
{
    if (s.empty()) return {};

    const unsigned char *bytes = reinterpret_cast<const unsigned char *>(s.data());
    const size_t size = s.size();
    if (size >= 2 && bytes[0] == 0xFF && bytes[1] == 0xFE) {
        const size_t wchar_count = (size - 2) / 2;
        std::wstring out(wchar_count, L'\0');
        if (wchar_count > 0) {
            std::memcpy(&out[0], s.data() + 2, wchar_count * sizeof(wchar_t));
        }
        return out;
    }
    if (size >= 2 && bytes[0] == 0xFE && bytes[1] == 0xFF) {
        std::wstring out;
        out.reserve((size - 2) / 2);
        for (size_t i = 2; i + 1 < size; i += 2) {
            wchar_t ch = static_cast<wchar_t>((bytes[i] << 8) | bytes[i + 1]);
            out.push_back(ch);
        }
        return out;
    }

    auto decode_multibyte = [&](UINT codepage, DWORD flags) -> std::wstring {
        const int needed = MultiByteToWideChar(codepage, flags, s.data(), static_cast<int>(s.size()), nullptr, 0);
        if (needed <= 0) return {};
        std::wstring out(static_cast<size_t>(needed), L'\0');
        const int converted = MultiByteToWideChar(codepage, flags, s.data(), static_cast<int>(s.size()), &out[0], needed);
        if (converted <= 0) return {};
        return out;
    };

    std::wstring out = decode_multibyte(CP_UTF8, MB_ERR_INVALID_CHARS);
    if (!out.empty()) return out;
    out = decode_multibyte(CP_ACP, 0);
    if (!out.empty()) return out;
    return {};
}

std::vector<std::wstring> ExtractCells(const std::wstring &row_xml)
{
    std::vector<std::wstring> cells;
    size_t pos = 0;
    while (true) {
        size_t data_begin = row_xml.find(L"<Data", pos);
        if (data_begin == std::wstring::npos) break;
        data_begin = row_xml.find(L">", data_begin);
        if (data_begin == std::wstring::npos) break;
        ++data_begin;
        size_t data_end = row_xml.find(L"</Data>", data_begin);
        if (data_end == std::wstring::npos) break;
        cells.push_back(Unescape(row_xml.substr(data_begin, data_end - data_begin)));
        pos = data_end + 7;
    }
    return cells;
}

std::map<std::wstring, std::vector<std::vector<std::wstring>>> ParseSheets(const std::wstring &xml)
{
    std::map<std::wstring, std::vector<std::vector<std::wstring>>> sheets;
    size_t pos = 0;
    while (true) {
        size_t ws = xml.find(L"<Worksheet", pos);
        if (ws == std::wstring::npos) break;
        size_t name_attr = xml.find(L"ss:Name=\"", ws);
        if (name_attr == std::wstring::npos) break;
        name_attr += 9;
        size_t name_end = xml.find(L"\"", name_attr);
        if (name_end == std::wstring::npos) break;
        std::wstring name = Unescape(xml.substr(name_attr, name_end - name_attr));
        size_t ws_end = xml.find(L"</Worksheet>", name_end);
        if (ws_end == std::wstring::npos) break;
        std::wstring body = xml.substr(name_end, ws_end - name_end);
        std::vector<std::vector<std::wstring>> rows;
        size_t rpos = 0;
        while (true) {
            size_t rb = body.find(L"<Row", rpos);
            if (rb == std::wstring::npos) break;
            rb = body.find(L">", rb);
            if (rb == std::wstring::npos) break;
            ++rb;
            size_t re = body.find(L"</Row>", rb);
            if (re == std::wstring::npos) break;
            rows.push_back(ExtractCells(body.substr(rb, re - rb)));
            rpos = re + 6;
        }
        sheets[name] = rows;
        pos = ws_end + 12;
    }
    return sheets;
}

core::FtRow *FindRow(core::FtLevelNode &level, const std::wstring &name)
{
    for (auto &row : level.rows) if (row.instance_name == name) return &row;
    return nullptr;
}
core::FtCell *FindCell(core::FtRow &row, const std::wstring &key)
{
    for (auto &cell : row.cells) if (cell.column_key == key) return &cell;
    return nullptr;
}
core::FtColumn *FindColumn(core::FtLevelNode &level, const std::wstring &key)
{
    for (auto &col : level.columns) if (col.column_key == key) return &col;
    return nullptr;
}
core::FtLevelNode *FindLevel(core::FtWorkspace &workspace, const std::wstring &path)
{
    for (auto &level : workspace.level_nodes) if (level.level_path == path) return &level;
    return nullptr;
}

size_t FirstDataHeaderRow(const std::vector<std::vector<std::wstring>> &rows)
{
    if (!rows.empty() && !rows.front().empty() && rows.front().front() == L"返回索引") return 1;
    return 0;
}

void EnsureColumn(core::FtLevelNode &level, const std::wstring &key)
{
    if (FindColumn(level, key) != nullptr) return;
    core::FtColumn col;
    col.column_key = key;
    col.column_display_name = key;
    col.column_category = core::FtColumnCategory::Unknown;
    col.support_status = core::FtSupportStatus::Todo;
    col.editable = false;
    col.visible = true;
    col.order_index = static_cast<int>(level.columns.size());
    level.columns.push_back(col);
    for (auto &row : level.rows) {
        core::FtCell cell;
        cell.column_key = key;
        cell.support_status = core::FtSupportStatus::Todo;
        cell.editable = false;
        row.cells.push_back(cell);
    }
}

} // namespace

bool ImportFtExcelToWorkspace(const std::wstring &file_path, core::FtWorkspace &workspace, std::wstring &error_out)
{
    error_out.clear();
    const std::string bytes = ReadAll(file_path);
    if (bytes.empty()) { error_out = L"Cannot read Excel XML file"; return false; }
    const std::wstring xml = ToWideLossy(bytes);
    if (xml.empty()) { error_out = L"Cannot decode Excel file text"; return false; }
    if (xml.find(L"<Workbook") == std::wstring::npos || xml.find(L"<Worksheet") == std::wstring::npos) {
        error_out = L"Unsupported Excel format. Please import the XML workbook exported by AutoBBoxToolkit.";
        return false;
    }
    const auto sheets = ParseSheets(xml);
    auto index_it = sheets.find(L"INDEX");
    if (index_it == sheets.end()) { error_out = L"Missing INDEX sheet"; return false; }

    std::map<std::wstring, std::wstring> level_to_sheet;
    for (size_t i = 1; i < index_it->second.size(); ++i) {
        const auto &r = index_it->second[i];
        if (r.size() >= 2) level_to_sheet[r[0]] = r[1];
    }

    for (auto &pair : level_to_sheet) {
        core::FtLevelNode *level = FindLevel(workspace, pair.first);
        if (level == nullptr) continue;
        auto sheet_it = sheets.find(pair.second);
        if (sheet_it == sheets.end() || sheet_it->second.empty()) continue;
        const size_t header_row = FirstDataHeaderRow(sheet_it->second);
        if (header_row >= sheet_it->second.size()) continue;
        const std::vector<std::wstring> header = sheet_it->second[header_row];
        for (const auto &key : header) EnsureColumn(*level, key);

        std::set<std::wstring> seen_instances;
        for (size_t r = header_row + 1; r < sheet_it->second.size(); ++r) {
            const auto &values = sheet_it->second[r];
            std::wstring inst_name;
            for (size_t c = 0; c < header.size() && c < values.size(); ++c) if (header[c] == L"INSTANCE_NAME") inst_name = values[c];
            if (inst_name.empty()) continue;
            seen_instances.insert(inst_name);
            core::FtRow *row = FindRow(*level, inst_name);
            if (row == nullptr) {
                core::FtRow new_row;
                new_row.row_kind = core::FtRowKind::Instance;
                new_row.instance_name = inst_name;
                new_row.verify_status = L"NEW_FROM_EXCEL";
                new_row.action = core::FtRowAction::New;
                new_row.change_kind = core::FtChangeKind::New;
                for (const auto &col : level->columns) {
                    core::FtCell cell;
                    cell.column_key = col.column_key;
                    cell.editable = col.editable;
                    cell.support_status = col.support_status;
                    new_row.cells.push_back(cell);
                }
                level->rows.push_back(new_row);
                row = &level->rows.back();
            }
            for (size_t c = 0; c < header.size() && c < values.size(); ++c) {
                core::FtCell *cell = FindCell(*row, header[c]);
                if (cell != nullptr && cell->value != values[c]) {
                    cell->value = values[c];
                    cell->changed = true;
                    if (row->action == core::FtRowAction::Keep) row->action = core::FtRowAction::Modify;
                }
                if (header[c] == L"INSTANCE_NAME") row->instance_name = values[c];
                if (header[c] == L"IS_LOCKED") row->is_locked = (values[c] == L"TRUE" || values[c] == L"1");
            }
        }
        for (auto &row : level->rows) {
            if (row.row_kind == core::FtRowKind::Instance && row.action != core::FtRowAction::New && seen_instances.find(row.instance_name) == seen_instances.end()) {
                row.action = core::FtRowAction::Delete;
                row.change_kind = core::FtChangeKind::Delete;
            }
        }
        FtLog(workspace, level->level_path, L"INFO", L"excel-import", L"Imported sheet " + pair.second, PRO_TK_NO_ERROR);
    }
    RefreshFtWorkspaceDiff(workspace);
    FtLog(workspace, workspace.active_level_path, L"INFO", L"excel-import", L"Excel data loaded into dialog workspace; Creo is unchanged until Apply", PRO_TK_NO_ERROR);
    return true;
}

} // namespace autobbox::application
