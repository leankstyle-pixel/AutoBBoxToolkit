#pragma once

#include <ProAsmcomppath.h>
#include <ProFamtable.h>
#include <ProMdl.h>
#include <ProParamDriver.h>
#include <ProParamval.h>
#include <ProToolkit.h>

#include <functional>
#include <string>
#include <vector>

namespace autobbox::application {

using ModelStructureLogSink = std::function<void(const std::string &line)>;

struct ModelStructureConstraint {
    int node_index = -1;
    int index = -1;
    ProError read_status = PRO_TK_GENERAL_ERROR;
    int type = -1;
    std::wstring type_label;
    ProError asm_ref_status = PRO_TK_GENERAL_ERROR;
    ProError comp_ref_status = PRO_TK_GENERAL_ERROR;
    std::wstring asm_reference;
    std::wstring comp_reference;
    int asm_orientation = 0;
    int comp_orientation = 0;
    ProError offset_status = PRO_TK_GENERAL_ERROR;
    double offset = 0.0;
    ProError attributes_status = PRO_TK_GENERAL_ERROR;
    int attributes = 0;
};

struct ModelStructureParameter {
    int node_index = -1;
    std::wstring name;
    ProParamvalueType type = PRO_PARAM_NOT_SET;
    std::wstring type_label;
    std::wstring value;
    bool designated = false;
    bool table_driven = false;
    ProParameterDriver driver = PRO_PARAMDRIVER_NONE;
    ProLockstatus lock_status = PRO_PARAMLOCKSTATUS_UNLOCKED;
    std::wstring description;
    std::wstring owner_scope;
    bool exists = true;
    bool writable = false;
};

struct ModelStructureReference {
    int node_index = -1;
    std::wstring source;
    std::wstring owner_model;
    std::wstring name;
    std::wstring type_label;
    int type = 0;
    int id = -1;
    bool unique = false;
};

struct ModelStructureDimension {
    int node_index = -1;
    int feature_id = -1;
    std::wstring feature_name;
    std::wstring symbol;
    double value = 0.0;
    std::wstring unit;
    int type = 0;
    bool family_table_column = false;
    bool pattern_quantity = false;
};

struct ModelStructureFeatureInfo {
    int node_index = -1;
    int id = -1;
    std::wstring name;
    std::wstring type_name;
    int status = 0;
    bool suppressed = false;
    bool visible = true;
};

struct ModelStructureFamilyColumn {
    std::wstring key;
    int type = 0;
    std::wstring type_label;
};

struct ModelStructureFamilyCell {
    std::wstring column_key;
    ProParamvalueType type = PRO_PARAM_NOT_SET;
    std::wstring value;
    ProError status = PRO_TK_GENERAL_ERROR;
    bool editable = false;
};

struct ModelStructureFamilyRow {
    std::wstring instance_name;
    std::wstring row_kind;
    std::wstring common_name;
    std::wstring verify_status_text;
    bool locked = false;
    bool ext_locked = false;
    int verify_status = -1;
    std::vector<ModelStructureFamilyCell> cells;
};

struct ModelStructureFamilyTable {
    int node_index = -1;
    ProFamtable famtable = {};
    bool has_family_table = false;
    bool source_is_selected_model = true;
    bool source_is_generic = false;
    int generation = 0;
    std::wstring table_model_name;
    std::wstring table_model_type_label;
    std::wstring selected_instance_name;
    std::wstring immediate_generic_name;
    std::wstring top_generic_name;
    std::wstring level_path;
    std::wstring parent_generic_name;
    std::wstring parent_instance_name;
    int level_depth = 0;
    ProError init_status = PRO_TK_GENERAL_ERROR;
    ProError check_status = PRO_TK_GENERAL_ERROR;
    ProError immediate_generic_status = PRO_TK_GENERAL_ERROR;
    ProError top_generic_status = PRO_TK_GENERAL_ERROR;
    ProError item_visit_status = PRO_TK_GENERAL_ERROR;
    ProError instance_visit_status = PRO_TK_GENERAL_ERROR;
    std::vector<ModelStructureFamilyColumn> columns;
    std::vector<ModelStructureFamilyRow> rows;
};

struct ModelStructureRelationLine {
    int line_number = 0;
    std::wstring text;
    bool blank = false;
    bool comment = false;
    bool conditional = false;
};

struct ModelStructureRelationSet {
    int node_index = -1;
    bool has_main_relset = false;
    ProError visit_status = PRO_TK_GENERAL_ERROR;
    ProError read_status = PRO_TK_GENERAL_ERROR;
    std::vector<ModelStructureRelationLine> lines;
};

struct ModelStructureNode {
    int index = -1;
    int parent_index = -1;
    int depth = 0;
    ProMdl mdl = nullptr;
    ProMdlType model_type = PRO_MDL_UNUSED;
    std::wstring model_name;
    std::wstring display_name;
    std::wstring custom_name;
    std::wstring model_path;
    std::wstring model_type_label;
    std::wstring unit_system;
    std::wstring length_unit;
    std::wstring mass_unit;
    std::wstring occurrence_path;
    std::wstring parent_assembly;
    bool has_component_path = false;
    ProAsmcomppath component_path = {};
    int component_feature_id = -1;
    ProError component_init_status = PRO_TK_GENERAL_ERROR;
    bool packaged = false;
    bool underconstrained = false;
    bool frozen = false;
    bool suppressed = false;
    bool hidden = false;
    bool family_instance = false;
    bool duplicate_assembly = false;
    ProError packaged_status = PRO_TK_GENERAL_ERROR;
    ProError underconstrained_status = PRO_TK_GENERAL_ERROR;
    ProError frozen_status = PRO_TK_GENERAL_ERROR;
    ProError transform_status = PRO_TK_GENERAL_ERROR;
    double transform[4][4] = {{0.0}};
    int parameter_count = 0;
    int constraint_count = 0;
    bool has_family_table = false;
    int family_column_count = 0;
    int family_row_count = 0;
    int relation_line_count = 0;
    int feature_count = 0;
    int dimension_count = 0;
};

struct ModelStructureSelectedOccurrence {
    bool has_selection_path = false;
    std::wstring selected_model_name;
    std::wstring selected_model_type_label;
    std::wstring selected_display_name;
    std::wstring selected_model_path;
    std::wstring context_root_model;
    std::wstring context_root_type_label;
    std::wstring parent_assembly;
    std::wstring occurrence_path;
    std::wstring parent_occurrence_path;
    int component_feature_id = -1;
    int level = 0;
    ProError transform_status = PRO_TK_GENERAL_ERROR;
    double transform[4][4] = {{0.0}};
    bool packaged = false;
    bool underconstrained = false;
    bool frozen = false;
    bool suppressed = false;
    bool hidden = false;
};

struct ModelStructureReport {
    ProMdl root = nullptr;
    std::wstring root_name;
    ModelStructureSelectedOccurrence selected_occurrence;
    std::vector<ModelStructureNode> nodes;
    std::vector<ModelStructureConstraint> constraints;
    std::vector<ModelStructureConstraint> selected_occurrence_constraints;
    std::vector<ModelStructureParameter> parameters;
    std::vector<ModelStructureFamilyTable> family_tables;
    std::vector<ModelStructureRelationSet> relation_sets;
    std::vector<ModelStructureReference> references;
    std::vector<ModelStructureFeatureInfo> features;
    std::vector<ModelStructureDimension> dimensions;
};

ModelStructureReport CollectModelStructureAnalysis(ProMdl current,
                                                   const ModelStructureLogSink &log_sink);
ModelStructureReport CollectModelStructureAnalysis(ProMdl current,
                                                   const ProAsmcomppath *selected_path,
                                                   const ModelStructureLogSink &log_sink);
ModelStructureReport CollectModelStructureAnalysis(ProMdl analysis_root,
                                                   ProMdl selected_model,
                                                   const ProAsmcomppath *selected_path,
                                                   const ModelStructureLogSink &log_sink);

std::wstring BuildModelStructureSummary(const ModelStructureReport &report);
std::string BuildModelStructureJson(const ModelStructureReport &report);
bool SaveModelStructureJson(const ModelStructureReport &report,
                            const std::wstring &path,
                            std::string &error_out);
const wchar_t *ModelStructureModelTypeLabel(ProMdlType type);

} // namespace autobbox::application
