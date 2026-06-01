#pragma once

#include <ProMdl.h>
#include <ProView.h>

#include <array>
#include <string>

namespace autobbox::core {

struct Dwg3Candidate {
    ProMdl mdl = nullptr;
    ProMdlType type = PRO_MDL_UNUSED;
    int occurrence_count = 1;
    std::wstring model_name;
    std::wstring common_name;
    std::wstring label;
    std::wstring front_view_name;
    std::wstring right_view_name;
    std::wstring left_view_name;
    std::wstring top_view_name;
    std::wstring bottom_view_name;
    std::wstring back_view_name;
    std::wstring iso_view_name;
    std::string item_name;
};

struct Dwg3SimprepOption {
    std::wstring display_label;
    std::wstring rep_name;
    bool use_current_active = false;
    bool use_master_rep = false;
    bool is_active = false;
};

enum class Dwg3ViewType {
    Front = 0,
    Right = 1,
    Left = 2,
    Top = 3,
    Bottom = 4,
    Back = 5,
    Iso = 6,
    Count = 7
};

enum class Dwg3ProjectionType {
    FirstAngle,
    ThirdAngle
};

enum class Dwg3FrameMode {
    AutoDraft,
    Symbol
};

struct Dwg3FrameOptions {
    Dwg3FrameMode mode = Dwg3FrameMode::AutoDraft;
    std::wstring symbol_label;
    std::wstring symbol_file_name;
    int symbol_version = -1;
};

using Dwg3ViewMask = unsigned int;

struct Dwg3SheetLayout {
    double width = 0.0;
    double height = 0.0;
    double start_x = 0.0;
    double start_y = 0.0;
    double cell_w = 0.0;
    double cell_h = 0.0;
    int cols = 0;
    int rows = 0;
};

struct Dwg3CreatedViews {
    std::array<ProView, static_cast<size_t>(Dwg3ViewType::Count)> items = {};
};

struct Dwg3GroupOutline {
    double min_x = 0.0;
    double min_y = 0.0;
    double max_x = 0.0;
    double max_y = 0.0;
};

struct Dwg3PendingDecoration {
    Dwg3Candidate cand;
    size_t index = 0;
    size_t total = 0;
    Dwg3CreatedViews views = {};
    Dwg3GroupOutline outline = {};
};

struct Dwg3Spacing {
    double side_dx = 120.0;
    double iso_dx = 260.0;
    double vertical_dy = 120.0;
    double gap_x = 20.0;
};

static constexpr size_t kDwg3ViewCount = static_cast<size_t>(Dwg3ViewType::Count);

inline size_t Dwg3ViewIndex(Dwg3ViewType type)
{
    return static_cast<size_t>(type);
}

inline Dwg3ViewMask Dwg3ViewBit(Dwg3ViewType type)
{
    return 1u << static_cast<unsigned int>(type);
}

} // namespace autobbox::core
