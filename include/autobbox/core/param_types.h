#pragma once

#include <ProParamval.h>

#include <set>
#include <string>

namespace autobbox::core {

struct ParamPreviewEntry {
    std::wstring name;
    std::set<ProParamvalueType> types;
    int hit_count = 0;
    std::wstring sample_value;
    int designated_count = 0;
    int table_source_count = 0;
    std::wstring sample_description;
    std::wstring label;
    std::string item_name;
};

struct ParamAddSpec {
    std::wstring name;
    ProParamvalueType type = PRO_PARAM_NOT_SET;
    std::wstring raw_value;
    std::wstring string_value;
    int int_value = 0;
    double double_value = 0.0;
    short bool_value = 0;
};

struct ParamToolExecuteSummary {
    int delete_selected = 0;
    int add_input = 0;
    int delete_ok = 0;
    int delete_skip_missing = 0;
    int delete_fail = 0;
    int add_created = 0;
    int add_skip_existing = 0;
    int add_fail = 0;
};

} // namespace autobbox::core
