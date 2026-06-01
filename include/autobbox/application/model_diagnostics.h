#pragma once

#include <ProAsmcomppath.h>
#include <ProMdl.h>
#include <ProToolkit.h>

#include <functional>
#include <string>
#include <vector>

namespace autobbox::application {

enum class ModelDiagnosticSeverity {
    Info = 0,
    Warning = 1,
    Error = 2
};

struct ModelDiagnosticFeature {
    int id = -1;
    unsigned int status_flags = 0;
    std::wstring kind;
    std::wstring type_name;
    std::wstring tree_name;
};

struct ModelDiagnosticItem {
    ProMdl mdl = nullptr;
    ProAsmcomppath component_path = {};
    bool has_component_path = false;
    std::wstring model_name;
    std::wstring model_type_label;
    ModelDiagnosticSeverity severity = ModelDiagnosticSeverity::Info;
    std::wstring reason;
    std::wstring suggestion;
    std::vector<ModelDiagnosticFeature> failed_features;
    std::vector<ModelDiagnosticFeature> child_failed_features;
    std::vector<ModelDiagnosticFeature> external_child_failed_features;
    std::vector<ModelDiagnosticFeature> flagged_features;
    ProError regen_status_get = PRO_TK_GENERAL_ERROR;
    int regen_status = -1;
    ProError failed_features_status = PRO_TK_GENERAL_ERROR;
    ProError status_flags_status = PRO_TK_GENERAL_ERROR;
    ProError deep_regen_first = PRO_TK_GENERAL_ERROR;
    ProError deep_regen_second = PRO_TK_GENERAL_ERROR;
    bool deep_checked = false;
    bool unstable_after_regen = false;
    std::wstring deep_summary;
};

using ModelDiagnosticsLogSink = std::function<void(const std::string &line)>;

std::vector<ModelDiagnosticItem> CollectModelDiagnostics(bool deep_check,
                                                        const ModelDiagnosticsLogSink &log_sink);

ProError LocateModelDiagnosticItem(const ModelDiagnosticItem &item,
                                   const ModelDiagnosticsLogSink &log_sink);

std::wstring BuildModelDiagnosticsSummary(const std::vector<ModelDiagnosticItem> &items);
void LogModelDiagnosticsReport(const std::vector<ModelDiagnosticItem> &items,
                               bool deep_check,
                               const ModelDiagnosticsLogSink &log_sink);

const wchar_t *ModelDiagnosticSeverityLabel(ModelDiagnosticSeverity severity);

} // namespace autobbox::application
