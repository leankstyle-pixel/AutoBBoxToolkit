#include "autobbox/ui/family_table_add_column_dialog.h"

#include <ProUIDialog.h>
#include <ProUIInputpanel.h>
#include <ProUIOptionmenu.h>
#include <ProUIPushbutton.h>
#include <ProUtil.h>

#include <cwctype>
#include <string>
#include <vector>

namespace autobbox::ui {
namespace {

constexpr const char *kDialog = "autobbox_ft_add_column_inst";
constexpr const char *kResource = "autobbox_ft_add_column";
constexpr const char *kType = "ColumnTypeMenu";
constexpr const char *kObject = "ObjectInput";
constexpr const char *kInsert = "InsertInput";
constexpr const char *kOk = "OKBtn";
constexpr const char *kCancel = "CancelBtn";

void OnOk(char *dialog, char *, ProAppData)
{
    ProUIDialogExit(dialog, 1);
}

void OnCancel(char *dialog, char *, ProAppData)
{
    ProUIDialogExit(dialog, 0);
}

std::wstring Upper(std::wstring text)
{
    for (wchar_t &ch : text) ch = static_cast<wchar_t>(std::towupper(ch));
    return text;
}

core::FtColumnCategory ParseType(const std::wstring &value)
{
    const std::wstring v = Upper(value);
    if (v.find(L"DIM") != std::wstring::npos) return core::FtColumnCategory::Dimension;
    if (v.find(L"PARAM") != std::wstring::npos && v.find(L"SYS") == std::wstring::npos) return core::FtColumnCategory::Parameter;
    if (v.find(L"SYS_PARAM") != std::wstring::npos || v.find(L"SYSTEM") != std::wstring::npos) return core::FtColumnCategory::SystemParameter;
    if (v.find(L"FEAT") != std::wstring::npos) return core::FtColumnCategory::Feature;
    if (v.find(L"MEMBER") != std::wstring::npos || v.find(L"ASM") != std::wstring::npos) return core::FtColumnCategory::AssemblyMember;
    if (v.find(L"UDF") != std::wstring::npos) return core::FtColumnCategory::Udf;
    if (v.find(L"REF") != std::wstring::npos) return core::FtColumnCategory::ReferenceModel;
    if (v.find(L"PATTERN") != std::wstring::npos) return core::FtColumnCategory::PatternTable;
    if (v.find(L"MERGE") != std::wstring::npos) return core::FtColumnCategory::MergePart;
    return core::FtColumnCategory::Unknown;
}

int ParseInsertIndex(const std::wstring &text)
{
    if (text.empty()) return -1;
    try { return std::stoi(text); } catch (...) { return -1; }
}

bool GetInput(const char *comp, std::wstring &out)
{
    wchar_t *raw = nullptr;
    if (ProUIInputpanelValueGet(const_cast<char *>(kDialog), const_cast<char *>(comp), &raw) != PRO_TK_NO_ERROR || raw == nullptr) {
        return false;
    }
    out = raw;
    ProWstringFree(raw);
    return true;
}

} // namespace

bool PromptFamilyTableAddColumnDialog(application::FtAddColumnSpec &spec_io,
                                      std::wstring &error_out)
{
    error_out.clear();
    ProError st = ProUIDialogCreate(const_cast<char *>(kDialog), const_cast<char *>(kResource));
    if (st != PRO_TK_NO_ERROR) {
        st = ProUIDialogCreate(const_cast<char *>(kDialog), const_cast<char *>("resource/autobbox_ft_add_column.res"));
    }
    if (st != PRO_TK_NO_ERROR) {
        error_out = L"Cannot load autobbox_ft_add_column.res";
        return false;
    }

    std::vector<std::string> names_store = {"PARAM", "DIM", "FEATURE", "MEMBER", "SYS_PARAM", "UDF", "REF_MODEL", "PATTERN", "MERGE"};
    std::vector<std::wstring> labels_store = {L"PARAM", L"DIM", L"FEATURE", L"MEMBER", L"SYS_PARAM", L"UDF", L"REF_MODEL", L"PATTERN", L"MERGE"};
    std::vector<char *> names;
    std::vector<wchar_t *> labels;
    for (auto &name : names_store) names.push_back(const_cast<char *>(name.c_str()));
    for (auto &label : labels_store) labels.push_back(const_cast<wchar_t *>(label.c_str()));
    ProUIOptionmenuNamesSet(const_cast<char *>(kDialog), const_cast<char *>(kType), static_cast<int>(names.size()), names.data());
    ProUIOptionmenuLabelsSet(const_cast<char *>(kDialog), const_cast<char *>(kType), static_cast<int>(labels.size()), labels.data());
    ProUIOptionmenuColumnsSet(const_cast<char *>(kDialog), const_cast<char *>(kType), 28);
    ProUIOptionmenuVisiblerowsSet(const_cast<char *>(kDialog), const_cast<char *>(kType), static_cast<int>(names.size()));

    const wchar_t *default_type = L"PARAM";
    if (spec_io.category == core::FtColumnCategory::Dimension) default_type = L"DIM";
    else if (spec_io.category == core::FtColumnCategory::Feature) default_type = L"FEATURE";
    else if (spec_io.category == core::FtColumnCategory::AssemblyMember) default_type = L"MEMBER";
    ProUIOptionmenuValueSet(const_cast<char *>(kDialog), const_cast<char *>(kType), const_cast<wchar_t *>(default_type));
    ProUIInputpanelValueSet(const_cast<char *>(kDialog), const_cast<char *>(kObject), const_cast<wchar_t *>(spec_io.object_name.c_str()));
    const std::wstring insert_text = spec_io.insert_index >= 0 ? std::to_wstring(spec_io.insert_index) : L"";
    ProUIInputpanelValueSet(const_cast<char *>(kDialog), const_cast<char *>(kInsert), const_cast<wchar_t *>(insert_text.c_str()));

    ProUIPushbuttonActivateActionSet(const_cast<char *>(kDialog), const_cast<char *>(kOk), OnOk, nullptr);
    ProUIPushbuttonActivateActionSet(const_cast<char *>(kDialog), const_cast<char *>(kCancel), OnCancel, nullptr);
    ProUIDialogCloseActionSet(const_cast<char *>(kDialog), OnCancel, nullptr);
    ProUIDialogDefaultbuttonSet(const_cast<char *>(kDialog), const_cast<char *>(kOk));

    int status = 0;
    st = ProUIDialogActivate(const_cast<char *>(kDialog), &status);
    if (st != PRO_TK_NO_ERROR || status != 1) {
        ProUIDialogDestroy(const_cast<char *>(kDialog));
        return false;
    }

    wchar_t *type_raw = nullptr;
    if (ProUIOptionmenuValueGet(const_cast<char *>(kDialog), const_cast<char *>(kType), &type_raw) != PRO_TK_NO_ERROR || type_raw == nullptr) {
        ProUIDialogDestroy(const_cast<char *>(kDialog));
        error_out = L"Cannot read item type";
        return false;
    }
    std::wstring object_name;
    std::wstring insert_value;
    const bool got_object = GetInput(kObject, object_name);
    GetInput(kInsert, insert_value);

    application::FtAddColumnSpec parsed;
    parsed.category = ParseType(type_raw);
    parsed.object_name = object_name;
    parsed.insert_index = ParseInsertIndex(insert_value);
    ProWstringFree(type_raw);
    ProUIDialogDestroy(const_cast<char *>(kDialog));

    if (!got_object || parsed.object_name.empty()) {
        error_out = L"Object name/id is required";
        return false;
    }
    if (parsed.category == core::FtColumnCategory::Unknown) {
        error_out = L"Unknown family table item type";
        return false;
    }

    spec_io = parsed;
    return true;
}

} // namespace autobbox::ui
