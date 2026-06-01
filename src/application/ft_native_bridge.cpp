#include "autobbox/application/ft_native_bridge.h"

#include <ProFamtable.h>
#include <ProFaminstance.h>
#include <ProMdl.h>
#include <cwchar>

namespace autobbox::application {
namespace {

ProError InitFamilyInstance(core::FtLevelNode &level, const std::wstring &instance_name, ProFaminstance &instance)
{
    if (!level.has_family_table || instance_name.empty()) {
        return PRO_TK_BAD_INPUTS;
    }
    ProName name = {0};
    wcsncpy_s(name, instance_name.c_str(), _TRUNCATE);
    return ProFaminstanceInit(name, &level.famtable, &instance);
}

ProError RetrieveAndDisplayInstance(core::FtLevelNode &level, const std::wstring &instance_name, ProMdl *opened_model)
{
    ProFaminstance instance = {};
    ProError st = InitFamilyInstance(level, instance_name, instance);
    if (st != PRO_TK_NO_ERROR) return st;

    ProMdl child_model = nullptr;
    st = ProFaminstanceRetrieve(&instance, &child_model);
    if (st != PRO_TK_NO_ERROR || child_model == nullptr) return st;

    // Per the official "Operations on Family Table Instances" guidance,
    // ProFaminstanceRetrieve() gets the model handle and the instance must be
    // displayed separately.
    st = ProMdlDisplay(child_model);
    if (opened_model != nullptr && st == PRO_TK_NO_ERROR) *opened_model = child_model;
    return st;
}

} // namespace

ProError ShowNativeFamilyTable(core::FtLevelNode &level)
{
    if (!level.has_family_table) {
        return PRO_TK_E_NOT_FOUND;
    }
    return ProFamtableShow(&level.famtable);
}

ProError EditNativeFamilyTable(core::FtLevelNode &level)
{
    if (!level.has_family_table) {
        return PRO_TK_E_NOT_FOUND;
    }
    return ProFamtableEdit(&level.famtable);
}

ProError EditChildInstanceFamilyTable(core::FtLevelNode &parent_level, const std::wstring &instance_name)
{
    ProFaminstance instance = {};
    ProError st = InitFamilyInstance(parent_level, instance_name, instance);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }

    ProMdl child_model = nullptr;
    st = ProFaminstanceRetrieve(&instance, &child_model);
    if (st != PRO_TK_NO_ERROR || child_model == nullptr) {
        return st;
    }

    ProFamtable child_ft = {};
    st = ProFamtableInit(child_model, &child_ft);
    if (st != PRO_TK_NO_ERROR) {
        return st;
    }

    // Bridge to native Family Table editor. If the instance model has no child
    // table yet, Creo decides whether a table can be created for that model.
    return ProFamtableEdit(&child_ft);
}

ProError OpenFamilyTableInstance(core::FtLevelNode &level, const std::wstring &instance_name, ProMdl *opened_model)
{
    return RetrieveAndDisplayInstance(level, instance_name, opened_model);
}

ProError PreviewFamilyTableInstance(core::FtLevelNode &level, const std::wstring &instance_name, ProMdl *opened_model)
{
    return RetrieveAndDisplayInstance(level, instance_name, opened_model);
}

ProError BridgeNativeFamilyTableAction(core::FtLevelNode &level, const std::wstring &action_key, const std::wstring &instance_name)
{
    if (action_key == L"show") return ShowNativeFamilyTable(level);
    if (action_key == L"edit") return EditNativeFamilyTable(level);
    if (action_key == L"open-instance") return OpenFamilyTableInstance(level, instance_name, nullptr);
    if (action_key == L"preview-instance") return PreviewFamilyTableInstance(level, instance_name, nullptr);
    if (action_key == L"edit-child") return EditChildInstanceFamilyTable(level, instance_name);
    return PRO_TK_BAD_INPUTS;
}

ProError EraseLevelFamilyTable(core::FtLevelNode &level)
{
    if (!level.has_family_table) {
        return PRO_TK_E_NOT_FOUND;
    }
    return ProFamtableErase(&level.famtable);
}

} // namespace autobbox::application
