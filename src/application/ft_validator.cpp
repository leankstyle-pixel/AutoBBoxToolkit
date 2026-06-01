#include "autobbox/application/ft_validator.h"

#include <algorithm>
#include <cwctype>
#include <set>

namespace autobbox::application {
namespace {

const core::FtLevelNode *FindLevel(const std::vector<core::FtLevelNode> &levels, const std::wstring &path)
{
    for (const auto &level : levels) if (level.level_path == path) return &level;
    return nullptr;
}

const core::FtRow *FindRow(const core::FtLevelNode &level, const std::wstring &name)
{
    for (const auto &row : level.rows) {
        if (row.instance_name == name || row.original_instance_name == name) return &row;
    }
    return nullptr;
}

const core::FtColumn *FindColumn(const core::FtLevelNode &level, const std::wstring &key)
{
    for (const auto &col : level.columns) if (col.column_key == key) return &col;
    return nullptr;
}

const core::FtCell *FindCell(const core::FtRow &row, const std::wstring &key)
{
    for (const auto &cell : row.cells) if (cell.column_key == key) return &cell;
    return nullptr;
}

bool IsYesNoInstanceColumn(core::FtColumnCategory cat)
{
    return cat == core::FtColumnCategory::Feature ||
           cat == core::FtColumnCategory::AssemblyMember ||
           cat == core::FtColumnCategory::Udf ||
           cat == core::FtColumnCategory::ReferenceModel ||
           cat == core::FtColumnCategory::MergePart;
}

bool RequiresNativeFamilyTableBridge(core::FtColumnCategory cat)
{
    return cat == core::FtColumnCategory::Feature ||
           cat == core::FtColumnCategory::AssemblyMember ||
           cat == core::FtColumnCategory::Udf ||
           cat == core::FtColumnCategory::ReferenceModel ||
           cat == core::FtColumnCategory::MergePart;
}

std::wstring Trim(std::wstring text)
{
    const auto is_space = [](wchar_t ch) { return std::iswspace(ch) != 0; };
    text.erase(text.begin(), std::find_if(text.begin(), text.end(), [&](wchar_t ch) { return !is_space(ch); }));
    text.erase(std::find_if(text.rbegin(), text.rend(), [&](wchar_t ch) { return !is_space(ch); }).base(), text.end());
    return text;
}

bool IsSpecialValuePlaceholder(const std::wstring &value)
{
    return value == L"*" || value == L"<GENERIC>" || value == L"<UNREADABLE>";
}

bool IsAllowedCloneSeedWrite(const core::FtRow &row,
                             const core::FtCell &cell,
                             const core::FtCell *orig_cell)
{
    if (orig_cell != nullptr) return false;
    if (row.action != core::FtRowAction::New) return false;
    if (!cell.clone_seeded) return false;
    return Trim(cell.value) == Trim(cell.clone_seed_value);
}

std::wstring ToUpper(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) { return static_cast<wchar_t>(std::towupper(ch)); });
    return text;
}

bool LooksLikeCreoInstanceName(const std::wstring &text)
{
    if (text.empty()) return false;
    if (text.front() == L'<' || text == L"*" || text.find(L' ') != std::wstring::npos) return false;
    for (wchar_t ch : text) {
        if ((ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'_' || ch == L'-' || ch == L'.') {
            continue;
        }
        return false;
    }
    return true;
}

bool IsAllowedYesNoInstanceValue(const std::wstring &value)
{
    const std::wstring trimmed = Trim(value);
    const std::wstring upper = ToUpper(trimmed);
    if (upper == L"Y" || upper == L"N" || upper == L"YES" || upper == L"NO" ||
        upper == L"TRUE" || upper == L"FALSE" || upper == L"X" ||
        trimmed == L"1" || trimmed == L"0") {
        return true;
    }
    if (!trimmed.empty() &&
        trimmed.size() <= 2 &&
        std::find_if(trimmed.begin(), trimmed.end(), [](wchar_t ch) { return ch > 127; }) != trimmed.end()) {
        return true;
    }
    return LooksLikeCreoInstanceName(trimmed);
}

std::wstring BuildCellIssueContext(const core::FtRow &row,
                                   const core::FtColumn &col,
                                   const core::FtCell *orig_cell,
                                   const core::FtCell &cell)
{
    const std::wstring old_value = orig_cell == nullptr ? L"" : Trim(orig_cell->value);
    const std::wstring new_value = Trim(cell.value);
    return row.instance_name +
           L" | col=" + col.column_display_name +
           L" key=" + col.column_key +
           L" category=" + std::wstring(core::FtColumnCategoryName(col.column_category)) +
           L" old=[" + old_value + L"]" +
           L" new=[" + new_value + L"]";
}

} // namespace

bool ValidateFtWorkspaceForApply(const core::FtWorkspace &workspace, std::vector<std::wstring> &issues)
{
    issues.clear();
    for (const auto &level : workspace.level_nodes) {
        const core::FtLevelNode *orig_level = FindLevel(workspace.original_snapshot, level.level_path);
        std::set<std::wstring> names;
        for (const auto &row : level.rows) {
            if (row.row_kind != core::FtRowKind::Instance) continue;
            const std::wstring lookup_name = row.original_instance_name.empty() ? row.instance_name : row.original_instance_name;
            const core::FtRow *orig_row = orig_level == nullptr ? nullptr : FindRow(*orig_level, lookup_name);
            const bool is_new_row = row.action == core::FtRowAction::New || orig_row == nullptr;
            if (row.instance_name.empty()) issues.push_back(level.level_path + L": empty instance name");
            if (!names.insert(row.instance_name).second) issues.push_back(level.level_path + L": duplicate instance " + row.instance_name);
            if (row.enhanced_clone &&
                !row.original_instance_name.empty() &&
                row.instance_name != row.original_instance_name) {
                issues.push_back(level.level_path + L"/" + row.instance_name +
                                 L": enhanced-clone instance rename must use the official family-table editor");
            }
            for (const auto &cell : row.cells) {
                const core::FtCell *orig_cell = orig_row == nullptr ? nullptr : FindCell(*orig_row, cell.column_key);
                const bool value_differs = orig_cell == nullptr ? !cell.value.empty() : orig_cell->value != cell.value;
                const bool deferred_default = is_new_row && IsSpecialValuePlaceholder(cell.value);
                if (cell.changed && cell.support_status != core::FtSupportStatus::Full) {
                    if (!is_new_row && value_differs) {
                        const core::FtColumn *col = FindColumn(level, cell.column_key);
                        if (col != nullptr) {
                            issues.push_back(level.level_path + L"/" +
                                             BuildCellIssueContext(row, *col, orig_cell, cell) +
                                             L" | reason=non-FULL cell cannot be written automatically");
                        } else {
                            issues.push_back(level.level_path + L"/" + row.instance_name +
                                             L": non-FULL cell cannot be written automatically: " + cell.column_key);
                        }
                    }
                }
                const core::FtColumn *col = FindColumn(level, cell.column_key);
                if (cell.changed &&
                    !deferred_default &&
                    col != nullptr &&
                    IsYesNoInstanceColumn(col->column_category) &&
                    !IsAllowedYesNoInstanceValue(cell.value)) {
                    issues.push_back(level.level_path + L"/" +
                                     BuildCellIssueContext(row, *col, orig_cell, cell) +
                                     L" | reason=invalid Y/N-or-instance value");
                }
                if (col != nullptr &&
                    RequiresNativeFamilyTableBridge(col->column_category) &&
                    !deferred_default &&
                    value_differs &&
                    !IsAllowedCloneSeedWrite(row, cell, orig_cell)) {
                    issues.push_back(level.level_path + L"/" +
                                     BuildCellIssueContext(row, *col, orig_cell, cell) +
                                     L" | reason=native-semantic column change must use official family-table editor");
                }
            }
        }
        for (const auto &col : level.columns) {
            if ((col.change_kind == core::FtChangeKind::New || col.change_kind == core::FtChangeKind::Delete || col.change_kind == core::FtChangeKind::Moved) &&
                col.support_status != core::FtSupportStatus::Full) {
                issues.push_back(level.level_path + L": non-FULL column cannot be changed automatically: " + col.column_key);
            }
            if ((col.change_kind == core::FtChangeKind::New || col.change_kind == core::FtChangeKind::Delete || col.change_kind == core::FtChangeKind::Moved) &&
                RequiresNativeFamilyTableBridge(col.column_category)) {
                issues.push_back(level.level_path +
                                 L": native-semantic column structure change must use official family-table editor: " +
                                 col.column_key);
            }
            if (level.enhanced_clone &&
                (col.change_kind == core::FtChangeKind::New || col.change_kind == core::FtChangeKind::Delete || col.change_kind == core::FtChangeKind::Moved)) {
                issues.push_back(level.level_path +
                                 L": enhanced-clone subtree structure change must use official family-table editor: " +
                                 col.column_key);
            }
        }
    }
    return issues.empty();
}

} // namespace autobbox::application
