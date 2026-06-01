#include "autobbox/core/family_table_types.h"

namespace autobbox::core {

const wchar_t *FtSupportStatusName(FtSupportStatus status)
{
    switch (status) {
    case FtSupportStatus::Full: return L"FULL";
    case FtSupportStatus::ReadOnly: return L"READ_ONLY";
    case FtSupportStatus::BridgeNative: return L"BRIDGE_NATIVE";
    case FtSupportStatus::Todo: default: return L"TODO";
    }
}

const wchar_t *FtColumnCategoryName(FtColumnCategory category)
{
    switch (category) {
    case FtColumnCategory::Fixed: return L"FIXED";
    case FtColumnCategory::Dimension: return L"DIM";
    case FtColumnCategory::Parameter: return L"PARAM";
    case FtColumnCategory::SystemParameter: return L"SYS_PARAM";
    case FtColumnCategory::Feature: return L"FEAT";
    case FtColumnCategory::Udf: return L"UDF";
    case FtColumnCategory::ReferenceModel: return L"REF_MODEL";
    case FtColumnCategory::PatternTable: return L"PATTERN";
    case FtColumnCategory::AssemblyMember: return L"MEMBER";
    case FtColumnCategory::MergePart: return L"MERGE";
    case FtColumnCategory::Unknown: default: return L"UNKNOWN";
    }
}

const wchar_t *FtRowActionName(FtRowAction action)
{
    switch (action) {
    case FtRowAction::Keep: return L"KEEP";
    case FtRowAction::Modify: return L"MODIFY";
    case FtRowAction::New: return L"NEW";
    case FtRowAction::Delete: return L"DELETE";
    default: return L"KEEP";
    }
}

const wchar_t *FtChangeKindName(FtChangeKind kind)
{
    switch (kind) {
    case FtChangeKind::None: return L"";
    case FtChangeKind::New: return L"NEW";
    case FtChangeKind::Modify: return L"MODIFY";
    case FtChangeKind::Delete: return L"DELETE";
    case FtChangeKind::Moved: return L"MOVED";
    default: return L"";
    }
}

} // namespace autobbox::core
