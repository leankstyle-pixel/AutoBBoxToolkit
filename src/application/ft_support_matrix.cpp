#include "autobbox/application/ft_support_matrix.h"

namespace autobbox::application {

std::vector<core::FtSupportMatrixEntry> BuildDefaultFtSupportMatrix()
{
    using core::FtSupportStatus;
    std::vector<core::FtSupportMatrixEntry> rows;
    auto add = [&](const wchar_t *key, const wchar_t *label, FtSupportStatus status, const wchar_t *remark) {
        rows.push_back({key, label, status, remark});
    };
    add(L"MULTI_LEVEL_BROWSER", L"Tree/Tab/Sheet browser", FtSupportStatus::Full, L"ProFamtableInstanceVisit + ProUITree/ProUITab");
    add(L"EXCEL_ROUNDTRIP", L"Excel 2003 XML roundtrip", FtSupportStatus::Full, L"Import updates only the dialog workspace until Apply is confirmed");
    add(L"DIM", L"Dimension column", FtSupportStatus::Full, L"ProModelitemToFamtableItem / ProFaminstanceValueSet");
    add(L"PARAM", L"Parameter column", FtSupportStatus::Full, L"ProParameterToFamtableItem / ProFaminstanceValueSet");
    add(L"FEAT", L"Feature column", FtSupportStatus::Full, L"Add-column resolves feature id or model-tree/property name; Apply for native-semantic edits should use official family-table editor");
    add(L"MEMBER", L"Assembly member column", FtSupportStatus::Full, L"Add-column resolves component/member id or model-tree/property name; Apply for native-semantic edits should use official family-table editor");
    add(L"SYS_PARAM", L"System parameter", FtSupportStatus::Full, L"ProParameterToFamtableItem / ProFaminstanceValueSet");
    add(L"UDF", L"UDF", FtSupportStatus::Full, L"Add-column resolves UDF/group id or UDF name; Apply for native-semantic edits should use official family-table editor");
    add(L"REF_MODEL", L"Reference model", FtSupportStatus::Full, L"Add-column resolves feature id or model-tree/property name; Apply for native-semantic edits should use official family-table editor");
    add(L"PATTERN", L"Pattern Table", FtSupportStatus::Todo, L"Reserved");
    add(L"MERGE", L"Merge Part", FtSupportStatus::Full, L"Add-column resolves feature id or model-tree/property name; Apply for native-semantic edits should use official family-table editor");
    add(L"VERIFY_LOCK", L"Verify/Lock", FtSupportStatus::BridgeNative, L"Lock is attempted where safe; verify keeps a native bridge path");
    add(L"NATIVE_BRIDGE", L"Native family table bridge", FtSupportStatus::BridgeNative, L"ProFamtableShow / ProFamtableEdit");
    return rows;
}

core::FtSupportStatus SupportForColumnCategory(core::FtColumnCategory category)
{
    using core::FtColumnCategory;
    using core::FtSupportStatus;
    switch (category) {
    case FtColumnCategory::Fixed:
    case FtColumnCategory::Dimension:
    case FtColumnCategory::Parameter:
    case FtColumnCategory::SystemParameter:
        return FtSupportStatus::Full;
    case FtColumnCategory::Feature:
    case FtColumnCategory::AssemblyMember:
        return FtSupportStatus::Full;
    case FtColumnCategory::Udf:
    case FtColumnCategory::ReferenceModel:
    case FtColumnCategory::MergePart:
        return FtSupportStatus::Full;
    case FtColumnCategory::PatternTable:
    case FtColumnCategory::Unknown:
    default:
        return FtSupportStatus::Todo;
    }
}

bool IsFtStatusEditable(core::FtSupportStatus status)
{
    return status == core::FtSupportStatus::Full;
}

bool IsFtColumnEditable(core::FtColumnCategory category, core::FtSupportStatus status)
{
    if (!IsFtStatusEditable(status)) {
        return false;
    }
    return category == core::FtColumnCategory::Dimension ||
           category == core::FtColumnCategory::Parameter ||
           category == core::FtColumnCategory::SystemParameter ||
           category == core::FtColumnCategory::Feature ||
           category == core::FtColumnCategory::AssemblyMember ||
           category == core::FtColumnCategory::Udf ||
           category == core::FtColumnCategory::ReferenceModel ||
           category == core::FtColumnCategory::MergePart ||
           category == core::FtColumnCategory::Fixed;
}

} // namespace autobbox::application
