#include "autobbox/application/ft_writer.h"

#include "autobbox/application/ft_logger.h"
#include "autobbox/application/ft_validator.h"
#include "autobbox/creo/parameter_api.h"

#include <ProFamtable.h>
#include <ProFaminstance.h>
#include <ProMdl.h>
#include <ProModelitem.h>
#include <ProParamval.h>
#include <ProParameter.h>

#include <algorithm>
#include <cwchar>
#include <cwctype>
#include <cstring>
#include <sstream>

namespace autobbox::application {
namespace {

void CopyW(wchar_t *dest, size_t cap, const std::wstring &src)
{
    if (dest == nullptr || cap == 0) return;
    wcsncpy_s(dest, cap, src.c_str(), _TRUNCATE);
}

core::FtLevelNode *FindLevel(std::vector<core::FtLevelNode> &levels, const std::wstring &path)
{
    for (auto &level : levels) if (level.level_path == path) return &level;
    return nullptr;
}
const core::FtLevelNode *FindLevel(const std::vector<core::FtLevelNode> &levels, const std::wstring &path)
{
    for (const auto &level : levels) if (level.level_path == path) return &level;
    return nullptr;
}
const core::FtColumn *FindColumn(const core::FtLevelNode &level, const std::wstring &key)
{
    for (const auto &col : level.columns) if (col.column_key == key) return &col;
    return nullptr;
}
const core::FtRow *FindRow(const core::FtLevelNode &level, const std::wstring &name)
{
    for (const auto &row : level.rows) if (row.instance_name == name || row.original_instance_name == name) return &row;
    return nullptr;
}
const core::FtCell *FindCell(const core::FtRow &row, const std::wstring &key)
{
    for (const auto &cell : row.cells) if (cell.column_key == key) return &cell;
    return nullptr;
}

bool IsSpecialValuePlaceholder(const std::wstring &value)
{
    return value == L"*" || value == L"<GENERIC>" || value == L"<UNREADABLE>";
}

bool CellNeedsWrite(const core::FtCell &cell, const core::FtCell *orig_cell)
{
    return orig_cell == nullptr ? cell.changed : (orig_cell->value != cell.value || cell.changed);
}

ProError InitOrRetrieveInstanceModel(core::FtLevelNode &level,
                                     const std::wstring &instance_name,
                                     ProFaminstance *inst_out,
                                     ProMdl *mdl_out)
{
    if (inst_out == nullptr || mdl_out == nullptr) return PRO_TK_BAD_INPUTS;
    ProName inst_name = {0};
    CopyW(inst_name, sizeof(inst_name) / sizeof(inst_name[0]), instance_name);
    std::memset(inst_out, 0, sizeof(*inst_out));
    *mdl_out = nullptr;
    ProError st = ProFaminstanceInit(inst_name, &level.famtable, inst_out);
    if (st != PRO_TK_NO_ERROR) return st;
    st = ProFaminstanceMdlGet(inst_out, mdl_out);
    if (st == PRO_TK_NO_ERROR && *mdl_out != nullptr) return PRO_TK_NO_ERROR;
    st = ProFaminstanceRetrieve(inst_out, mdl_out);
    if (st == PRO_TK_NO_ERROR && *mdl_out != nullptr) return PRO_TK_NO_ERROR;
    return st == PRO_TK_NO_ERROR ? PRO_TK_GENERAL_ERROR : st;
}

bool HasLevelChanges(const core::FtLevelNode &level, const core::FtLevelNode *orig)
{
    if (orig == nullptr) {
        for (const auto &row : level.rows) {
            if (row.row_kind != core::FtRowKind::Instance) continue;
            if (row.action == core::FtRowAction::New || row.change_kind == core::FtChangeKind::New) {
                return true;
            }
            for (const auto &cell : row.cells) {
                if (cell.changed || cell.change_kind != core::FtChangeKind::None) {
                    return true;
                }
            }
        }
        for (const auto &col : level.columns) {
            if (col.change_kind != core::FtChangeKind::None) {
                return true;
            }
        }
        return false;
    }

    for (const auto &col : level.columns) {
        const core::FtColumn *orig_col = FindColumn(*orig, col.column_key);
        if (orig_col == nullptr || col.change_kind != core::FtChangeKind::None) {
            return true;
        }
        if (orig_col->order_index != col.order_index) {
            return true;
        }
    }
    for (const auto &orig_col : orig->columns) {
        if (FindColumn(level, orig_col.column_key) == nullptr) {
            return true;
        }
    }
    for (const auto &row : level.rows) {
        if (row.row_kind != core::FtRowKind::Instance) {
            continue;
        }
        const core::FtRow *orig_row = FindRow(*orig, row.original_instance_name.empty() ? row.instance_name : row.original_instance_name);
        if (orig_row == nullptr || row.action == core::FtRowAction::New || row.action == core::FtRowAction::Delete ||
            row.instance_name != (row.original_instance_name.empty() ? row.instance_name : row.original_instance_name) ||
            row.is_locked != (orig_row == nullptr ? row.is_locked : orig_row->is_locked)) {
            return true;
        }
        for (const auto &cell : row.cells) {
            const core::FtCell *orig_cell = orig_row == nullptr ? nullptr : FindCell(*orig_row, cell.column_key);
            if (orig_cell == nullptr) {
                if (!cell.value.empty() || cell.changed || cell.change_kind != core::FtChangeKind::None) {
                    return true;
                }
                continue;
            }
            if (cell.value != orig_cell->value || cell.changed || cell.change_kind != core::FtChangeKind::None) {
                return true;
            }
        }
    }
    for (const auto &orig_row : orig->rows) {
        if (orig_row.row_kind == core::FtRowKind::Generic) {
            continue;
        }
        if (FindRow(level, orig_row.instance_name) == nullptr) {
            return true;
        }
    }
    return false;
}

std::wstring ParentPath(const std::wstring &path)
{
    const size_t pos = path.rfind(L'/');
    if (pos == std::wstring::npos) return L"";
    return path.substr(0, pos);
}

bool HasUsableFamtable(ProMdl mdl, ProFamtable *out)
{
    if (mdl == nullptr || out == nullptr) return false;
    std::memset(out, 0, sizeof(*out));
    if (ProFamtableInit(mdl, out) != PRO_TK_NO_ERROR) return false;
    const ProError st = ProFamtableCheck(out);
    return st == PRO_TK_NO_ERROR || st == PRO_TK_EMPTY;
}

bool RefreshLevelFamtableHandle(core::FtLevelNode &level)
{
    if (level.generic_mdl == nullptr) return false;
    ProFamtable refreshed = {};
    if (!HasUsableFamtable(level.generic_mdl, &refreshed)) {
        return false;
    }
    level.famtable = refreshed;
    level.has_family_table = true;
    return true;
}

ProModelitem MdlAsModelitem(ProMdl mdl)
{
    ProModelitem item = {};
    ProMdlToModelitem(mdl, &item);
    return item;
}

bool IsParameterFamtableColumn(core::FtColumnCategory category)
{
    return category == core::FtColumnCategory::Parameter ||
           category == core::FtColumnCategory::SystemParameter;
}

bool IsPatternQuantityFamtableName(const std::wstring &name)
{
    if (name.size() < 2) return false;
    if (name[0] != L'P' && name[0] != L'p') return false;
    return std::all_of(name.begin() + 1, name.end(), [](wchar_t ch) {
        return ch >= L'0' && ch <= L'9';
    });
}

bool BuildFamtableItemFromColumn(const core::FtLevelNode &level, const core::FtColumn &col, ProFamtableItem &item)
{
    std::memset(&item, 0, sizeof(item));
    if (!col.has_creo_item || col.famtab_string.empty()) return false;
    item.type = col.famtab_type;
    item.owner = col.creo_item_owner != nullptr ? col.creo_item_owner : level.generic_mdl;
    CopyW(item.string, sizeof(item.string) / sizeof(item.string[0]), col.famtab_string);
    return true;
}

bool BuildWriteFamtableItemFromColumn(const core::FtLevelNode &level, const core::FtColumn &col, ProFamtableItem &item)
{
    // Use the exact family-table item identity captured from ProFamtableItemVisit.
    //
    // For P### pattern-quantity columns, round-tripping through
    // ProFamtableItemToModelitem()/ProModelitemToFamtableItem() can lose the
    // original family-table item identity that Creo expects for
    // ProFaminstanceValueGet/Set. The official samples operate directly on the
    // visited ProFamtableItem when reading and writing instance values.
    return BuildFamtableItemFromColumn(level, col, item);
}

bool ParseBool(const std::wstring &text, ProBoolean &out)
{
    if (text == L"1" || text == L"\u662f" || text == L"TRUE" || text == L"true" || text == L"YES" || text == L"yes" || text == L"Y" || text == L"y" || text == L"X" || text == L"x") { out = PRO_B_TRUE; return true; }
    if (text == L"0" || text == L"\u5426" || text == L"FALSE" || text == L"false" || text == L"NO" || text == L"no" || text == L"N" || text == L"n") { out = PRO_B_FALSE; return true; }
    return false;
}

bool IsYesNoInstanceColumn(core::FtColumnCategory cat)
{
    return cat == core::FtColumnCategory::Feature ||
           cat == core::FtColumnCategory::AssemblyMember ||
           cat == core::FtColumnCategory::Udf ||
           cat == core::FtColumnCategory::ReferenceModel ||
           cat == core::FtColumnCategory::MergePart;
}

std::wstring ToUpper(std::wstring text)
{
    std::transform(text.begin(), text.end(), text.begin(), towupper);
    return text;
}

std::wstring ParamValueTypeName(ProParamvalueType type)
{
    switch (type) {
    case PRO_PARAM_DOUBLE: return L"DOUBLE";
    case PRO_PARAM_STRING: return L"STRING";
    case PRO_PARAM_INTEGER: return L"INTEGER";
    case PRO_PARAM_BOOLEAN: return L"BOOLEAN";
    case PRO_PARAM_NOTE_ID: return L"NOTE_ID";
    case PRO_PARAM_VOID: return L"VOID";
    case PRO_PARAM_NOT_SET:
    default:
        return L"NOT_SET";
    }
}

std::wstring ParamValueToText(ProParamvalue *value)
{
    if (value == nullptr) return L"";
    ProParamvalueType type = PRO_PARAM_NOT_SET;
    if (ProParamvalueTypeGet(value, &type) != PRO_TK_NO_ERROR) {
        type = value->type;
    }
    switch (type) {
    case PRO_PARAM_DOUBLE: {
        double v = 0.0;
        if (ProParamvalueValueGet(value, PRO_PARAM_DOUBLE, &v) == PRO_TK_NO_ERROR) {
            wchar_t buf[128] = {0};
            std::swprintf(buf, 127, L"%.12g", v);
            return buf;
        }
        break;
    }
    case PRO_PARAM_STRING: {
        ProLine s = {0};
        if (ProParamvalueValueGet(value, PRO_PARAM_STRING, s) == PRO_TK_NO_ERROR) {
            return s;
        }
        break;
    }
    case PRO_PARAM_INTEGER:
    case PRO_PARAM_NOTE_ID: {
        int v = 0;
        if (ProParamvalueValueGet(value, type, &v) == PRO_TK_NO_ERROR) {
            return std::to_wstring(v);
        }
        break;
    }
    case PRO_PARAM_BOOLEAN: {
        ProBoolean v = PRO_B_FALSE;
        if (ProParamvalueValueGet(value, PRO_PARAM_BOOLEAN, &v) == PRO_TK_NO_ERROR) {
            return v == PRO_B_TRUE ? L"TRUE" : L"FALSE";
        }
        break;
    }
    case PRO_PARAM_VOID:
        return L"";
    default:
        break;
    }
    return L"<UNREADABLE>";
}

std::wstring BuildCellLogMessage(const core::FtRow &row,
                                 const core::FtColumn &col,
                                 const std::wstring &old_value,
                                 const std::wstring &new_value,
                                 ProParamvalueType effective_type,
                                 ProError current_value_status)
{
    std::wstringstream ss;
    ss << row.instance_name
       << L" | col=" << col.column_display_name
       << L" key=" << col.column_key
       << L" fam=" << col.famtab_string
       << L" category=" << core::FtColumnCategoryName(col.column_category)
       << L" type=" << ParamValueTypeName(effective_type)
       << L" old=[" << old_value << L"]"
       << L" new=[" << new_value << L"]"
       << L" current_get_status=" << static_cast<int>(current_value_status);
    return ss.str();
}

std::wstring NativeCellTextForCreo(const core::FtColumn &col, const std::wstring &text)
{
    if (!IsYesNoInstanceColumn(col.column_category)) return text;
    const std::wstring upper = ToUpper(text);
    if (text == L"\u662f" || text == L"1" || upper == L"Y" || upper == L"YES" || upper == L"TRUE" || upper == L"X") return L"Y";
    if (text == L"\u5426" || text == L"0" || upper == L"N" || upper == L"NO" || upper == L"FALSE") return L"N";
    // For assembly component model / reference model cells, non-boolean text is
    // a native family-table instance name and must be passed through unchanged.
    return text;
}

bool BuildParamValue(const core::FtColumn &col, const std::wstring &text, ProParamvalue &value)
{
    std::memset(&value, 0, sizeof(value));
    const std::wstring creo_text = NativeCellTextForCreo(col, text);
    ProParamvalueType type = col.value_type;
    if (type == PRO_PARAM_NOT_SET || type == PRO_PARAM_VOID || type == PRO_PARAM_NOTE_ID) {
        type = PRO_PARAM_STRING;
    }
    try {
        switch (type) {
        case PRO_PARAM_DOUBLE: {
            double v = std::stod(creo_text.empty() ? L"0" : creo_text);
            return ProParamvalueSet(&value, &v, PRO_PARAM_DOUBLE) == PRO_TK_NO_ERROR;
        }
        case PRO_PARAM_INTEGER: {
            int v = std::stoi(creo_text.empty() ? L"0" : creo_text);
            return ProParamvalueSet(&value, &v, PRO_PARAM_INTEGER) == PRO_TK_NO_ERROR;
        }
        case PRO_PARAM_BOOLEAN: {
            ProBoolean b = PRO_B_FALSE;
            if (!ParseBool(creo_text, b)) return false;
            return ProParamvalueSet(&value, &b, PRO_PARAM_BOOLEAN) == PRO_TK_NO_ERROR;
        }
        case PRO_PARAM_STRING:
        default: {
            ProLine line = {0};
            CopyW(line, sizeof(line) / sizeof(line[0]), creo_text);
            return ProParamvalueSet(&value, line, PRO_PARAM_STRING) == PRO_TK_NO_ERROR;
        }
        }
    } catch (...) {
        return false;
    }
}

bool LooksLikeIntegerText(const std::wstring &text)
{
    if (text.empty()) return false;
    size_t pos = 0;
    if (text[0] == L'+' || text[0] == L'-') {
        pos = 1;
    }
    if (pos >= text.size()) return false;
    return std::all_of(text.begin() + static_cast<std::ptrdiff_t>(pos), text.end(), [](wchar_t ch) {
        return ch >= L'0' && ch <= L'9';
    });
}

bool LooksLikeNumericText(const std::wstring &text)
{
    if (text.empty()) return false;
    try {
        size_t consumed = 0;
        (void)std::stod(text, &consumed);
        return consumed == text.size();
    } catch (...) {
        return false;
    }
}

ProParamvalueType InferNumericValueType(const core::FtColumn &col, const std::wstring &text)
{
    if (IsPatternQuantityFamtableName(col.famtab_string)) {
        return PRO_PARAM_INTEGER;
    }
    if (LooksLikeIntegerText(text)) {
        return PRO_PARAM_INTEGER;
    }
    if (LooksLikeNumericText(text)) {
        return PRO_PARAM_DOUBLE;
    }
    return PRO_PARAM_STRING;
}

ProError AddColumnIfNeeded(core::FtWorkspace &workspace, core::FtLevelNode &level, const core::FtLevelNode *original, const core::FtColumn &col)
{
    if ((original != nullptr && FindColumn(*original, col.column_key) != nullptr) || col.column_category == core::FtColumnCategory::Fixed) return PRO_TK_NO_ERROR;
    if (col.support_status != core::FtSupportStatus::Full) {
        FtLog(workspace, level.level_path, L"WARN", L"add-column", L"Skipped non-FULL column " + col.column_key, PRO_TK_NO_CHANGE);
        return PRO_TK_NO_CHANGE;
    }

    ProFamtableItem item = {};
    bool ok = BuildFamtableItemFromColumn(level, col, item);
    if (ok && IsParameterFamtableColumn(col.column_category)) {
        ProModelitem owner = MdlAsModelitem(level.generic_mdl);
        ProParameter param;
        ProName pname = {0};
        CopyW(pname, sizeof(pname) / sizeof(pname[0]), col.famtab_string);
        ProError pst = ProParameterInit(&owner, pname, &param);
        if (pst != PRO_TK_NO_ERROR && col.column_category == core::FtColumnCategory::Parameter) {
            std::wstring empty;
            autobbox::creo::SetStringParamOnOwner(&owner, col.famtab_string.c_str(), empty);
            pst = ProParameterInit(&owner, pname, &param);
        }
        if (pst == PRO_TK_NO_ERROR) {
            ok = ProParameterToFamtableItem(&param, &item) == PRO_TK_NO_ERROR;
        }
    }
    if (!ok) {
        FtLog(workspace, level.level_path, L"WARN", L"add-column", L"Failed to build ProFamtableItem for " + col.column_key, PRO_TK_BAD_INPUTS);
        return PRO_TK_BAD_INPUTS;
    }
    ProError st = ProFamtableItemAdd(&level.famtable, &item);
    FtLog(workspace, level.level_path, st == PRO_TK_NO_ERROR || st == PRO_TK_NO_CHANGE ? L"INFO" : L"ERROR", L"add-column", col.column_key, st);
    return st;
}

ProError RemoveColumnIfNeeded(core::FtWorkspace &workspace, core::FtLevelNode &level, const core::FtColumn &orig_col)
{
    if (orig_col.column_category == core::FtColumnCategory::Fixed) return PRO_TK_NO_ERROR;
    ProFamtableItem item = {};
    if (!BuildFamtableItemFromColumn(level, orig_col, item)) return PRO_TK_BAD_INPUTS;
    ProError st = ProFamtableItemRemove(&level.famtable, &item);
    FtLog(workspace, level.level_path, st == PRO_TK_NO_ERROR || st == PRO_TK_E_NOT_FOUND ? L"INFO" : L"ERROR", L"remove-column", orig_col.column_key, st);
    return st;
}

ProError ApplyRowDelete(core::FtWorkspace &workspace, core::FtLevelNode &level, const std::wstring &name)
{
    ProName inst_name = {0};
    CopyW(inst_name, sizeof(inst_name) / sizeof(inst_name[0]), name);
    ProFaminstance inst = {};
    ProError st = ProFaminstanceInit(inst_name, &level.famtable, &inst);
    if (st == PRO_TK_NO_ERROR) {
        ProFaminstanceErase(&inst);
        st = ProFaminstanceRemove(&inst);
    }
    FtLog(workspace, level.level_path, st == PRO_TK_NO_ERROR || st == PRO_TK_E_NOT_FOUND ? L"INFO" : L"ERROR", L"delete-row", name, st);
    return st;
}

ProError ApplyRowAdd(core::FtWorkspace &workspace, core::FtLevelNode &level, const core::FtRow &row)
{
    ProName inst_name = {0};
    CopyW(inst_name, sizeof(inst_name) / sizeof(inst_name[0]), row.instance_name);
    ProFaminstance inst = {};
    ProError st = ProFaminstanceInit(inst_name, &level.famtable, &inst);
    if (st == PRO_TK_NO_ERROR) st = ProFaminstanceAdd(&inst);
    FtLog(workspace, level.level_path, st == PRO_TK_NO_ERROR || st == PRO_TK_E_FOUND ? L"INFO" : L"ERROR", L"add-row", row.instance_name, st);
    return st;
}

ProError ApplyCommonName(core::FtWorkspace &workspace,
                         core::FtLevelNode &level,
                         const core::FtRow &row,
                         const core::FtCell &cell)
{
    ProFaminstance inst = {};
    ProMdl inst_mdl = nullptr;
    ProError st = InitOrRetrieveInstanceModel(level, row.instance_name, &inst, &inst_mdl);
    if (st != PRO_TK_NO_ERROR || inst_mdl == nullptr) {
        FtLog(workspace,
              level.level_path,
              L"ERROR",
              L"set-common-name-init",
              row.instance_name + L" | init/retrieve instance model failed",
              st);
        return st;
    }
    st = autobbox::creo::SetStringParamOnModel(inst_mdl, L"PTC_COMMON_NAME", cell.value);
    FtLog(workspace,
          level.level_path,
          st == PRO_TK_NO_ERROR ? L"INFO" : L"ERROR",
          L"set-common-name",
          row.instance_name + L" | value=[" + cell.value + L"]",
          st);
    if (st != PRO_TK_NO_ERROR) return st;
    const ProError save_st = ProMdlSave(inst_mdl);
    FtLog(workspace,
          level.level_path,
          save_st == PRO_TK_NO_ERROR ? L"INFO" : L"WARN",
          L"save-instance",
          row.instance_name + L" | common name",
          save_st);
    return save_st;
}

ProError ApplyCell(core::FtWorkspace &workspace, core::FtLevelNode &level, const core::FtRow &row, const core::FtColumn &col, const core::FtCell &cell)
{
    if (row.row_kind != core::FtRowKind::Instance || col.column_category == core::FtColumnCategory::Fixed) return PRO_TK_NO_ERROR;
    if (col.support_status != core::FtSupportStatus::Full || !cell.editable) {
        FtLog(workspace, level.level_path, L"WARN", L"set-cell", L"Skipped non-FULL or read-only cell " + row.instance_name + L"/" + col.column_key, PRO_TK_NO_CHANGE);
        return PRO_TK_NO_CHANGE;
    }
    ProFamtableItem item = {};
    if (!BuildWriteFamtableItemFromColumn(level, col, item)) return PRO_TK_BAD_INPUTS;
    ProName inst_name = {0};
    CopyW(inst_name, sizeof(inst_name) / sizeof(inst_name[0]), row.instance_name);
    ProFaminstance inst = {};
    ProError st = ProFaminstanceInit(inst_name, &level.famtable, &inst);
    if (st != PRO_TK_NO_ERROR) {
        FtLog(workspace,
              level.level_path,
              L"ERROR",
              L"set-cell-init",
              row.instance_name + L"/" + col.column_key + L" | ProFaminstanceInit failed",
              st);
        return st;
    }
    core::FtColumn effective_col = col;
    ProError current_value_status = PRO_TK_NO_CHANGE;
    std::wstring current_value_text;
    if (col.column_category == core::FtColumnCategory::Dimension || col.value_type == PRO_PARAM_NOT_SET) {
        ProParamvalue current = {};
        current_value_status = ProFaminstanceValueGet(&inst, &item, &current);
        if (current_value_status == PRO_TK_NO_ERROR) {
            current_value_text = ParamValueToText(&current);
            ProParamvalueType current_type = PRO_PARAM_NOT_SET;
            if (ProParamvalueTypeGet(&current, &current_type) == PRO_TK_NO_ERROR &&
                current_type != PRO_PARAM_NOT_SET &&
                current_type != PRO_PARAM_VOID &&
                current_type != PRO_PARAM_NOTE_ID) {
                effective_col.value_type = current_type;
            }
        }
    }
    if ((effective_col.value_type == PRO_PARAM_NOT_SET ||
         effective_col.value_type == PRO_PARAM_VOID ||
         effective_col.value_type == PRO_PARAM_NOTE_ID) &&
        col.column_category == core::FtColumnCategory::Dimension) {
        effective_col.value_type = InferNumericValueType(col, cell.value);
    }
    if (IsPatternQuantityFamtableName(col.famtab_string)) {
        FtLog(workspace,
              level.level_path,
              L"INFO",
              L"set-cell-pattern-prepare",
              BuildCellLogMessage(row,
                                  effective_col,
                                  current_value_text,
                                  cell.value,
                                  effective_col.value_type,
                                  current_value_status),
              PRO_TK_NO_ERROR);
    }
    ProParamvalue value = {};
    if (!BuildParamValue(effective_col, cell.value, value)) {
        FtLog(workspace,
              level.level_path,
              L"ERROR",
              L"set-cell-build",
              BuildCellLogMessage(row,
                                  effective_col,
                                  current_value_text,
                                  cell.value,
                                  effective_col.value_type,
                                  current_value_status) +
                  L" | reason=BuildParamValue failed",
              PRO_TK_BAD_INPUTS);
        return PRO_TK_BAD_INPUTS;
    }
    st = ProFaminstanceValueSet(&inst, &item, &value);
    FtLog(workspace,
          level.level_path,
          st == PRO_TK_NO_ERROR ? L"INFO" : L"ERROR",
          L"set-cell",
          BuildCellLogMessage(row,
                              effective_col,
                              current_value_text,
                              cell.value,
                              effective_col.value_type,
                              current_value_status),
          st);
    return st;
}

ProError ApplyLockIfChanged(core::FtWorkspace &workspace, core::FtLevelNode &level, const core::FtRow &row, const core::FtRow *orig_row)
{
    if (orig_row == nullptr || row.is_locked == orig_row->is_locked) return PRO_TK_NO_ERROR;
    ProName inst_name = {0};
    CopyW(inst_name, sizeof(inst_name) / sizeof(inst_name[0]), row.instance_name);
    ProFaminstance inst = {};
    ProError st = ProFaminstanceInit(inst_name, &level.famtable, &inst);
    if (st == PRO_TK_NO_ERROR) st = ProFaminstanceLock(&inst, row.is_locked ? 1 : 0);
    FtLog(workspace, level.level_path, st == PRO_TK_NO_ERROR ? L"INFO" : L"ERROR", L"lock-row", row.instance_name, st);
    return st;
}

ProError ApplyLockState(core::FtWorkspace &workspace, core::FtLevelNode &level, const core::FtRow &row)
{
    ProName inst_name = {0};
    CopyW(inst_name, sizeof(inst_name) / sizeof(inst_name[0]), row.instance_name);
    ProFaminstance inst = {};
    ProError st = ProFaminstanceInit(inst_name, &level.famtable, &inst);
    if (st == PRO_TK_NO_ERROR) st = ProFaminstanceLock(&inst, row.is_locked ? 1 : 0);
    FtLog(workspace, level.level_path, st == PRO_TK_NO_ERROR ? L"INFO" : L"ERROR", L"lock-row", row.instance_name, st);
    return st;
}

ProError ApplyGenericParameterCells(core::FtWorkspace &workspace, core::FtLevelNode &level, const core::FtLevelNode *orig)
{
    const core::FtRow *generic_row = nullptr;
    const core::FtRow *orig_generic_row = nullptr;
    for (const auto &row : level.rows) {
        if (row.row_kind == core::FtRowKind::Generic) {
            generic_row = &row;
            break;
        }
    }
    if (orig != nullptr) {
        for (const auto &row : orig->rows) {
            if (row.row_kind == core::FtRowKind::Generic) {
                orig_generic_row = &row;
                break;
            }
        }
    }
    if (generic_row == nullptr || level.generic_mdl == nullptr) return PRO_TK_NO_ERROR;

    ProModelitem owner = MdlAsModelitem(level.generic_mdl);
    ProError overall = PRO_TK_NO_ERROR;
    for (const auto &cell : generic_row->cells) {
        const core::FtColumn *col = FindColumn(level, cell.column_key);
        if (col == nullptr) continue;
        if (col->column_category != core::FtColumnCategory::Parameter) continue;
        if (col->support_status != core::FtSupportStatus::Full || !col->editable) continue;
        if (cell.value == L"<GENERIC>" || cell.value == L"<UNREADABLE>" || cell.value == L"*") continue;

        const core::FtCell *orig_cell = orig_generic_row == nullptr ? nullptr : FindCell(*orig_generic_row, cell.column_key);
        const bool changed = orig_cell == nullptr ? !cell.value.empty() : orig_cell->value != cell.value;
        if (!changed) continue;

        ProError st = PRO_TK_BAD_INPUTS;
        try {
            switch (col->value_type) {
            case PRO_PARAM_DOUBLE:
                st = autobbox::creo::SetDoubleParamOnOwner(&owner, col->famtab_string.c_str(), std::stod(cell.value));
                break;
            case PRO_PARAM_INTEGER: {
                const int value = std::stoi(cell.value.empty() ? L"0" : cell.value);
                st = autobbox::creo::SetIntegerParamOnOwner(&owner, col->famtab_string.c_str(), value);
                break;
            }
            case PRO_PARAM_BOOLEAN: {
                ProBoolean value = PRO_B_FALSE;
                if (!ParseBool(cell.value, value)) {
                    st = PRO_TK_BAD_INPUTS;
                } else {
                    st = autobbox::creo::SetBooleanParamOnOwner(&owner, col->famtab_string.c_str(), static_cast<short>(value));
                }
                break;
            }
            case PRO_PARAM_STRING:
            case PRO_PARAM_NOT_SET:
            case PRO_PARAM_VOID:
            case PRO_PARAM_NOTE_ID:
            default:
                st = autobbox::creo::SetStringParamOnOwner(&owner, col->famtab_string.c_str(), cell.value);
                break;
            }
        } catch (...) {
            st = PRO_TK_BAD_INPUTS;
        }

        FtLog(workspace,
              level.level_path,
              st == PRO_TK_NO_ERROR ? L"INFO" : L"ERROR",
              L"set-generic-param",
              generic_row->instance_name + L"/" + col->column_key,
              st);
        if (st != PRO_TK_NO_ERROR && overall == PRO_TK_NO_ERROR) overall = st;
    }
    return overall;
}

bool ResolvePendingLevel(core::FtWorkspace &workspace, core::FtLevelNode &level, ProError &status_out)
{
    status_out = PRO_TK_NO_ERROR;
    const std::wstring parent_level_path = level.pending_parent_level_path.empty() ? ParentPath(level.level_path) : level.pending_parent_level_path;
    core::FtLevelNode *parent_level = FindLevel(workspace.level_nodes, parent_level_path);
    if (parent_level == nullptr || !parent_level->has_family_table) {
        status_out = PRO_TK_E_NOT_FOUND;
        FtLog(workspace, level.level_path, L"WARN", L"resolve-level", L"Parent level is not ready: " + parent_level_path, status_out);
        return false;
    }

    ProName inst_name = {0};
    CopyW(inst_name, sizeof(inst_name) / sizeof(inst_name[0]), level.parent_instance_name);
    ProFaminstance inst = {};
    ProMdl child_model = nullptr;
    ProError st = ProFaminstanceInit(inst_name, &parent_level->famtable, &inst);
    if (st == PRO_TK_NO_ERROR) {
        st = ProFaminstanceMdlGet(&inst, &child_model);
        if (st != PRO_TK_NO_ERROR || child_model == nullptr) {
            st = ProFaminstanceRetrieve(&inst, &child_model);
        }
    }
    if (st != PRO_TK_NO_ERROR || child_model == nullptr) {
        if (RefreshLevelFamtableHandle(*parent_level)) {
            std::memset(&inst, 0, sizeof(inst));
            child_model = nullptr;
            st = ProFaminstanceInit(inst_name, &parent_level->famtable, &inst);
            if (st == PRO_TK_NO_ERROR) {
                st = ProFaminstanceMdlGet(&inst, &child_model);
                if (st != PRO_TK_NO_ERROR || child_model == nullptr) {
                    st = ProFaminstanceRetrieve(&inst, &child_model);
                }
            }
        }
    }
    if (st != PRO_TK_NO_ERROR || child_model == nullptr) {
        status_out = st == PRO_TK_NO_ERROR ? PRO_TK_GENERAL_ERROR : st;
        FtLog(workspace, level.level_path, L"WARN", L"resolve-level", L"Cannot retrieve child model for " + level.parent_instance_name, status_out);
        return false;
    }

    ProFamtable famtable = {};
    if (!HasUsableFamtable(child_model, &famtable)) {
        status_out = PRO_TK_E_NOT_FOUND;
        FtLog(workspace, level.level_path, L"WARN", L"resolve-level", L"Resolved child model has no family table: " + level.parent_instance_name, status_out);
        return false;
    }

    level.generic_mdl = child_model;
    level.famtable = famtable;
    level.has_family_table = true;
    level.pending_resolve = false;
    level.pending_parent_level_path = parent_level_path;
    for (auto &column : level.columns) {
        if (!column.has_creo_item) continue;
        column.creo_item_owner = child_model;
    }
    FtLog(workspace, level.level_path, L"INFO", L"resolve-level", L"Resolved cloned child level from parent instance " + level.parent_instance_name, PRO_TK_NO_ERROR);
    return true;
}

ProError ApplyLevelChanges(core::FtWorkspace &workspace, core::FtLevelNode &level, const core::FtLevelNode *orig)
{
    ProError overall = PRO_TK_NO_ERROR;
    bool touched = false;

    if (!HasLevelChanges(level, orig)) {
        FtLog(workspace, level.level_path, L"INFO", L"apply-skip", L"No added/modified changes on this level", PRO_TK_NO_CHANGE);
        return PRO_TK_NO_CHANGE;
    }

    if (orig != nullptr) {
        for (const auto &orig_col : orig->columns) {
            if (FindColumn(level, orig_col.column_key) == nullptr) {
                ProError st = RemoveColumnIfNeeded(workspace, level, orig_col);
                if (st == PRO_TK_NO_ERROR || st == PRO_TK_E_NOT_FOUND) touched = true;
                if (st != PRO_TK_NO_ERROR && st != PRO_TK_E_NOT_FOUND) overall = st;
            }
        }
    }

    for (const auto &col : level.columns) {
        ProError st = AddColumnIfNeeded(workspace, level, orig, col);
        if (st == PRO_TK_NO_ERROR || st == PRO_TK_NO_CHANGE || st == PRO_TK_E_FOUND) {
            const core::FtColumn *orig_col = orig == nullptr ? nullptr : FindColumn(*orig, col.column_key);
            if (orig_col == nullptr) {
                touched = true;
            }
        }
        if (st != PRO_TK_NO_ERROR && st != PRO_TK_NO_CHANGE && st != PRO_TK_E_FOUND) overall = st;
    }

    {
        ProError st = ApplyGenericParameterCells(workspace, level, orig);
        if (st == PRO_TK_NO_ERROR) {
            for (const auto &row : level.rows) {
                if (row.row_kind != core::FtRowKind::Generic) continue;
                const core::FtRow *orig_row = orig == nullptr ? nullptr : FindRow(*orig, row.instance_name);
                for (const auto &cell : row.cells) {
                    const core::FtColumn *col = FindColumn(level, cell.column_key);
                    if (col == nullptr || col->column_category != core::FtColumnCategory::Parameter) continue;
                    const core::FtCell *orig_cell = orig_row == nullptr ? nullptr : FindCell(*orig_row, cell.column_key);
                    const bool changed = orig_cell == nullptr ? !cell.value.empty() : orig_cell->value != cell.value;
                    if (changed) {
                        touched = true;
                        break;
                    }
                }
            }
        }
        if (st != PRO_TK_NO_ERROR && overall == PRO_TK_NO_ERROR) overall = st;
    }

    if (orig != nullptr) {
        for (const auto &orig_row : orig->rows) {
            if (orig_row.row_kind == core::FtRowKind::Generic) continue;
            const core::FtRow *edited_row = FindRow(level, orig_row.instance_name);
            if (edited_row == nullptr || edited_row->action == core::FtRowAction::Delete) {
                ProError st = ApplyRowDelete(workspace, level, orig_row.instance_name);
                if (st == PRO_TK_NO_ERROR || st == PRO_TK_E_NOT_FOUND) touched = true;
                if (st != PRO_TK_NO_ERROR && st != PRO_TK_E_NOT_FOUND) overall = st;
            }
        }
    }

    for (const auto &row : level.rows) {
        if (row.row_kind == core::FtRowKind::Generic || row.action == core::FtRowAction::Delete) continue;
        const core::FtRow *orig_row = orig == nullptr ? nullptr : FindRow(*orig, row.original_instance_name.empty() ? row.instance_name : row.original_instance_name);
        const bool renamed_row = (orig_row != nullptr &&
                                  !row.original_instance_name.empty() &&
                                  row.instance_name != row.original_instance_name);
        const core::FtCell *common_name_cell = FindCell(row, L"COMMON_NAME");
        const core::FtCell *orig_common_name_cell = orig_row == nullptr ? nullptr : FindCell(*orig_row, L"COMMON_NAME");
        if (row.action == core::FtRowAction::New || orig_row == nullptr) {
            ProError st = ApplyRowAdd(workspace, level, row);
            if (st == PRO_TK_NO_ERROR || st == PRO_TK_E_FOUND) touched = true;
            if (st != PRO_TK_NO_ERROR && st != PRO_TK_E_FOUND) overall = st;
            if ((st == PRO_TK_NO_ERROR || st == PRO_TK_E_FOUND) && common_name_cell != nullptr && CellNeedsWrite(*common_name_cell, orig_common_name_cell)) {
                st = ApplyCommonName(workspace, level, row, *common_name_cell);
                if (st == PRO_TK_NO_ERROR || st == PRO_TK_NO_CHANGE) touched = true;
                if (st != PRO_TK_NO_ERROR && st != PRO_TK_NO_CHANGE && overall == PRO_TK_NO_ERROR) overall = st;
            }
            for (const auto &cell : row.cells) {
                const core::FtColumn *col = FindColumn(level, cell.column_key);
                if (col == nullptr || col->column_category == core::FtColumnCategory::Fixed) continue;
                if (IsSpecialValuePlaceholder(cell.value)) continue;
                const core::FtCell *orig_cell = orig_row == nullptr ? nullptr : FindCell(*orig_row, cell.column_key);
                if (!CellNeedsWrite(cell, orig_cell)) continue;
                st = ApplyCell(workspace, level, row, *col, cell);
                if (st == PRO_TK_NO_ERROR || st == PRO_TK_NO_CHANGE) touched = true;
                if (st != PRO_TK_NO_ERROR && st != PRO_TK_NO_CHANGE && overall == PRO_TK_NO_ERROR) overall = st;
            }
        } else if (renamed_row) {
            ProError st = ApplyRowAdd(workspace, level, row);
            if (st == PRO_TK_NO_ERROR || st == PRO_TK_E_FOUND) touched = true;
            if (st != PRO_TK_NO_ERROR && st != PRO_TK_E_FOUND) overall = st;
            if (row.is_locked) {
                st = ApplyLockState(workspace, level, row);
                if (st == PRO_TK_NO_ERROR) touched = true;
                if (st != PRO_TK_NO_ERROR && overall == PRO_TK_NO_ERROR) overall = st;
            }
            for (const auto &cell : row.cells) {
                const core::FtColumn *col = FindColumn(level, cell.column_key);
                if (col == nullptr || col->column_category == core::FtColumnCategory::Fixed) continue;
                if (IsSpecialValuePlaceholder(cell.value)) continue;
                const core::FtCell *orig_cell = FindCell(*orig_row, cell.column_key);
                const bool changed = CellNeedsWrite(cell, orig_cell);
                if (!changed) continue;
                st = ApplyCell(workspace, level, row, *col, cell);
                if (st == PRO_TK_NO_ERROR || st == PRO_TK_NO_CHANGE) touched = true;
                if (st != PRO_TK_NO_ERROR && st != PRO_TK_NO_CHANGE && overall == PRO_TK_NO_ERROR) overall = st;
            }
            if (common_name_cell != nullptr && CellNeedsWrite(*common_name_cell, orig_common_name_cell)) {
                st = ApplyCommonName(workspace, level, row, *common_name_cell);
                if (st == PRO_TK_NO_ERROR || st == PRO_TK_NO_CHANGE) touched = true;
                if (st != PRO_TK_NO_ERROR && st != PRO_TK_NO_CHANGE && overall == PRO_TK_NO_ERROR) overall = st;
            }
            st = ApplyRowDelete(workspace, level, row.original_instance_name);
            if (st == PRO_TK_NO_ERROR || st == PRO_TK_E_NOT_FOUND) touched = true;
            if (st != PRO_TK_NO_ERROR && st != PRO_TK_E_NOT_FOUND && overall == PRO_TK_NO_ERROR) overall = st;
            continue;
        }
        ProError lock_st = ApplyLockIfChanged(workspace, level, row, orig_row);
        if (lock_st == PRO_TK_NO_ERROR && orig_row != nullptr && row.is_locked != orig_row->is_locked) touched = true;
        if (lock_st != PRO_TK_NO_ERROR && overall == PRO_TK_NO_ERROR) overall = lock_st;
        if (common_name_cell != nullptr && CellNeedsWrite(*common_name_cell, orig_common_name_cell)) {
            const ProError st = ApplyCommonName(workspace, level, row, *common_name_cell);
            if (st == PRO_TK_NO_ERROR || st == PRO_TK_NO_CHANGE) touched = true;
            if (st != PRO_TK_NO_ERROR && st != PRO_TK_NO_CHANGE && overall == PRO_TK_NO_ERROR) overall = st;
        }
        for (const auto &cell : row.cells) {
            const core::FtColumn *col = FindColumn(level, cell.column_key);
            if (col == nullptr || col->column_category == core::FtColumnCategory::Fixed) continue;
            if (IsSpecialValuePlaceholder(cell.value)) continue;
            const core::FtCell *orig_cell = orig_row == nullptr ? nullptr : FindCell(*orig_row, cell.column_key);
            const bool changed = CellNeedsWrite(cell, orig_cell);
            if (!changed) continue;
            ProError st = ApplyCell(workspace, level, row, *col, cell);
            if (st == PRO_TK_NO_ERROR || st == PRO_TK_NO_CHANGE) touched = true;
            if (st != PRO_TK_NO_ERROR && st != PRO_TK_NO_CHANGE) overall = st;
        }
    }

    if (touched) {
        ProError save_st = ProMdlSave(level.generic_mdl);
        FtLog(workspace, level.level_path, save_st == PRO_TK_NO_ERROR ? L"INFO" : L"WARN", L"save", level.generic_name, save_st);
        if (save_st == PRO_TK_NO_ERROR) {
            RefreshLevelFamtableHandle(level);
        }
        if (save_st != PRO_TK_NO_ERROR && overall == PRO_TK_NO_ERROR) overall = save_st;
    } else {
        FtLog(workspace, level.level_path, L"INFO", L"save-skip", level.generic_name + L" | no added/modified writes executed", PRO_TK_NO_CHANGE);
    }
    return overall;
}

} // namespace

ProError ApplyFtWorkspaceToCreo(core::FtWorkspace &workspace)
{
    FtLog(workspace,
          workspace.active_level_path,
          L"INFO",
          L"apply",
          L"Begin apply: dirty=" + std::to_wstring(workspace.dirty ? 1 : 0) +
              L" added_rows=" + std::to_wstring(workspace.diff_result.added_rows.size()) +
              L" removed_rows=" + std::to_wstring(workspace.diff_result.removed_rows.size()) +
              L" modified_cells=" + std::to_wstring(workspace.diff_result.modified_cells.size()) +
              L" added_columns=" + std::to_wstring(workspace.diff_result.added_columns.size()) +
              L" removed_columns=" + std::to_wstring(workspace.diff_result.removed_columns.size()) +
              L" moved_columns=" + std::to_wstring(workspace.diff_result.moved_columns.size()),
          PRO_TK_NO_ERROR);

    std::vector<std::wstring> issues;
    if (!ValidateFtWorkspaceForApply(workspace, issues)) {
        for (const auto &issue : issues) FtLog(workspace, L"", L"ERROR", L"validate", issue, PRO_TK_BAD_INPUTS);
        FtLog(workspace,
              workspace.active_level_path,
              L"ERROR",
              L"apply",
              L"Apply blocked by validation. issue_count=" + std::to_wstring(issues.size()),
              PRO_TK_BAD_INPUTS);
        return PRO_TK_BAD_INPUTS;
    }

    ProError overall = PRO_TK_NO_ERROR;
    std::vector<core::FtLevelNode *> ordered;
    for (auto &level : workspace.level_nodes) ordered.push_back(&level);
    std::sort(ordered.begin(), ordered.end(), [](const core::FtLevelNode *a, const core::FtLevelNode *b){ return a->level_depth < b->level_depth; });

    for (core::FtLevelNode *level : ordered) {
        if (level == nullptr || !level->has_family_table || level->pending_resolve) continue;
        const core::FtLevelNode *orig = FindLevel(workspace.original_snapshot, level->level_path);
        if (orig == nullptr) continue;
        ProError st = ApplyLevelChanges(workspace, *level, orig);
        if (st != PRO_TK_NO_ERROR && overall == PRO_TK_NO_ERROR) overall = st;
    }

    for (core::FtLevelNode *level : ordered) {
        if (level == nullptr || !level->pending_resolve) continue;
        if (!HasLevelChanges(*level, nullptr)) {
            FtLog(workspace,
                  level->level_path,
                  L"INFO",
                  L"resolve-skip",
                  L"Pending placeholder has no added/modified changes; not retrieving instance model",
                  PRO_TK_NO_CHANGE);
            continue;
        }
        ProError resolve_st = PRO_TK_NO_ERROR;
        if (!ResolvePendingLevel(workspace, *level, resolve_st)) {
            if (resolve_st != PRO_TK_NO_ERROR && overall == PRO_TK_NO_ERROR) overall = resolve_st;
            continue;
        }
        ProError st = ApplyLevelChanges(workspace, *level, nullptr);
        if (st != PRO_TK_NO_ERROR && overall == PRO_TK_NO_ERROR) overall = st;
    }
    FtLog(workspace,
          workspace.active_level_path,
          overall == PRO_TK_NO_ERROR ? L"INFO" : L"ERROR",
          L"apply",
          L"Apply finished. status=" + std::to_wstring(static_cast<int>(overall)),
          overall);
    return overall;
}

} // namespace autobbox::application
