#pragma once

#include <ProMdl.h>
#include <ProObjects.h>

#include <string>
#include <vector>

namespace autobbox::core {

enum class RandomColorMode {
    Random,
    Parameter
};

enum class RandomColorMatchSource {
    None,
    Preset,
    Library
};

enum class RandomColorSkipReason {
    None,
    MissingParameter,
    EmptyParameter,
    NoMatch,
    ReadError
};

struct RandomColorEntry {
    std::string id;
    std::wstring material_name;
    std::wstring display_name;
    std::wstring description;
    std::wstring keywords;
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    double highlight_red = 1.0;
    double highlight_green = 1.0;
    double highlight_blue = 1.0;
    double ambient = 0.6;
    double diffuse = 0.9;
    double highlite = 0.6;
    double shininess = 0.6;
    double transparency = 0.0;
    double reflection = 0.3;
    bool has_highlight_color = false;
};

struct RandomColorOccurrence {
    ProAsmcomppath path = {};
    int feat_id = -1;
};

struct RandomColorCandidate {
    ProMdl mdl = nullptr;
    ProMdlType type = PRO_MDL_UNUSED;
    std::wstring model_name;
    std::wstring label;
    std::string item_name;
    std::vector<RandomColorOccurrence> occurrences;
    RandomColorEntry current_appearance;
    std::wstring current_appearance_label;
    bool has_current_appearance = false;
    bool has_mixed_current_appearance = false;
};

struct RandomColorAssignment {
    RandomColorCandidate candidate;
    RandomColorEntry color;
};

struct RandomColorParameterPreview {
    RandomColorCandidate candidate;
    std::wstring parameter_value;
    RandomColorEntry color;
    RandomColorMatchSource match_source = RandomColorMatchSource::None;
    RandomColorSkipReason skip_reason = RandomColorSkipReason::None;
    std::wstring status_text;
};

} // namespace autobbox::core
