#pragma once

#include <ProDrawing.h>
#include <ProDimension.h>
#include <ProModelitem.h>
#include <ProPoint.h>
#include <ProSelection.h>
#include <ProToolkit.h>
#include <ProView.h>

#include <string>
#include <vector>

namespace autobbox::core {

enum class SmartDimensionKind {
    Linear,
    Aligned,
    Angle,
    Radius,
    Diameter,
    AutoOrdinate,
};

enum class SmartDimensionStage {
    Idle,
    AwaitReferences,
    InferCandidate,
    AwaitPlacement,
    CreateDimension,
    ShowAnnotation,
    ContinueLoop,
    Finished,
    Failed,
};

struct SmartDimensionReference {
    ProSelection selection = nullptr;
    ProView view = nullptr;
    ProModelitem model_item = {};
    bool has_model_item = false;
};

struct SmartDimensionSelectionSet {
    SmartDimensionReference first = {};
    SmartDimensionReference second = {};
    ProError status = PRO_TK_GENERAL_ERROR;
    bool cancelled = false;
    bool complete = false;
    bool same_view = false;
};

struct SmartDimensionCandidate {
    SmartDimensionKind kind = SmartDimensionKind::Linear;
    int priority = 0;
    const wchar_t *label = L"";
    bool supported_in_v1 = true;
    std::wstring rationale;
};

struct SmartDimensionInferenceResult {
    std::vector<SmartDimensionCandidate> candidates;
    bool has_supported_candidate = false;
};

struct SmartDimensionPlacement {
    ProPoint3d location = {0.0, 0.0, 0.0};
    ProView view = nullptr;
    bool confirmed = false;
    bool cancelled = false;
    std::wstring note;
};

struct SmartDimensionCreateInput {
    ProDrawing drawing = nullptr;
    SmartDimensionSelectionSet selections = {};
    SmartDimensionCandidate candidate = {};
    SmartDimensionPlacement placement = {};
    bool ready_for_official_create = false;
};

struct SmartDimensionCreateResult {
    ProDimension dimension = {};
    ProError create_status = PRO_TK_GENERAL_ERROR;
    ProError show_status = PRO_TK_GENERAL_ERROR;
    bool created = false;
    bool shown = false;
    std::wstring note;
};

struct SmartDimensionLoopSummary {
    SmartDimensionStage last_stage = SmartDimensionStage::Idle;
    int cycles_started = 0;
    int cycles_completed = 0;
    int cycles_cancelled = 0;
    int dimensions_created = 0;
    int dimensions_shown = 0;
    std::wstring last_note;
};

} // namespace autobbox::core
