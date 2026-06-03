cmake_minimum_required(VERSION 3.20)
project(autobbox_toolkit LANGUAGES CXX)

set(CREO_TOOLKIT_ROOT "D:/Program Files/PTC/Creo 10.0.8.0/Common Files/protoolkit" CACHE PATH "Creo TOOLKIT root")

set(CREO_TK_INC "${CREO_TOOLKIT_ROOT}/includes")
set(CREO_TK_LIBDIR "${CREO_TOOLKIT_ROOT}/x86e_win64/obj")
set(CREO_REALDWG_DIR "D:/Program Files/PTC/Creo 10.0.8.0/Common Files/applications/REALDWG" CACHE PATH "Creo bundled RealDWG runtime")

if(NOT EXISTS "${CREO_TK_INC}/ProToolkit.h")
  message(FATAL_ERROR "Creo TOOLKIT headers not found: ${CREO_TK_INC}")
endif()

if(NOT EXISTS "${CREO_TK_LIBDIR}/protk_dllmd_NU.lib")
  message(FATAL_ERROR "Creo TOOLKIT lib not found: ${CREO_TK_LIBDIR}/protk_dllmd_NU.lib")
endif()

set(DWG_LAYOUT_RENAMER_EXE "${CMAKE_CURRENT_SOURCE_DIR}/deploy/AutoBBoxToolkit/AutobboxDwgLayoutRenamer_v2.exe")
set(DWG_LAYOUT_RENAMER_SRC "${CMAKE_CURRENT_SOURCE_DIR}/tools/dwg_layout_renamer/AutobboxDwgLayoutRenamer.cs")
set(DWG_LAYOUT_RENAMER_CSC "C:/Windows/Microsoft.NET/Framework64/v4.0.30319/csc.exe")
if(EXISTS "${DWG_LAYOUT_RENAMER_CSC}" AND EXISTS "${CREO_REALDWG_DIR}/acdbmgd.dll")
  add_custom_command(
    OUTPUT "${DWG_LAYOUT_RENAMER_EXE}"
    COMMAND "${CMAKE_COMMAND}" -E make_directory "$<SHELL_PATH:${CMAKE_CURRENT_SOURCE_DIR}/deploy/AutoBBoxToolkit>"
    COMMAND "${DWG_LAYOUT_RENAMER_CSC}" /nologo /platform:x64 /target:exe
            "/out:$<SHELL_PATH:${DWG_LAYOUT_RENAMER_EXE}>"
            "/reference:$<SHELL_PATH:${CREO_REALDWG_DIR}/acdbmgd.dll>"
            "$<SHELL_PATH:${DWG_LAYOUT_RENAMER_SRC}>"
    DEPENDS "${DWG_LAYOUT_RENAMER_SRC}"
    VERBATIM)
  add_custom_target(dwg_layout_renamer ALL DEPENDS "${DWG_LAYOUT_RENAMER_EXE}")
else()
  message(WARNING "DWG layout renamer helper will not be built: missing csc.exe or RealDWG acdbmgd.dll")
endif()

add_library(autobbox_toolkit SHARED
  src/autobbox_toolkit.cpp
  src/common/log.cpp
  src/common/strings.cpp
  src/common/files.cpp
  src/application/batch_rename.cpp
  src/application/balloon_arrange.cpp
  src/application/bom_actions.cpp
  src/application/bom_export.cpp
  src/application/drawing_arrange.cpp
  src/application/drawing_export.cpp
  src/application/dimension_create.cpp
  src/application/dimension_inference.cpp
  src/application/dimension_preview.cpp
  src/application/drawing_page_scale_sync.cpp
  src/application/drawing_view_brush.cpp
  src/application/drawing3_views.cpp
  src/application/force_open_drawing.cpp
  src/application/feature_visibility.cpp
  src/application/afx_library_search.cpp
  src/application/drawing_view_layout.cpp
  src/application/model_metrics.cpp
  src/application/model_diagnostics.cpp
  src/application/model_structure_analyzer.cpp
  src/application/model_run_tasks.cpp
  src/application/ordinate_mode.cpp
  src/application/param_tool.cpp
  src/application/quick_simprep.cpp
  src/application/sheetmetal_flat_batch.cpp
  src/application/quick_rename.cpp
  src/application/save_copy_to_workdir.cpp
  src/application/random_color.cpp
  src/application/relations.cpp
  src/application/selection_capture.cpp
  src/application/smart_dim_command.cpp
  src/application/split_instances.cpp
  src/application/target_collectors.cpp
  src/application/bom_state.cpp
  src/application/bom_update.cpp
  src/application/ft_types.cpp
  src/application/ft_support_matrix.cpp
  src/application/ft_logger.cpp
  src/application/ft_discovery.cpp
  src/application/ft_reader.cpp
  src/application/ft_excel_exporter.cpp
  src/application/ft_excel_importer.cpp
  src/application/ft_diff_engine.cpp
  src/application/ft_column_ops.cpp
  src/application/ft_row_ops.cpp
  src/application/ft_replica_view.cpp
  src/application/ft_validator.cpp
  src/application/ft_writer.cpp
  src/application/ft_native_bridge.cpp
  src/creo/family_table_api.cpp
  src/creo/model_info.cpp
  src/creo/parameter_api.cpp
  src/main/command_registry.cpp
  src/main/afx_library_dialog_hook.cpp
  src/main/open_perf_trace.cpp
  src/main/plugin_balloon_arrange.cpp
  src/main/plugin_batch_rename.cpp
  src/main/plugin_command_callbacks.cpp
  src/main/plugin_drawing_arrange.cpp
  src/main/plugin_drawing_export.cpp
  src/main/plugin_drawing3.cpp
  src/main/plugin_drawing_scale_sync.cpp
  src/main/plugin_drawing_view_brush.cpp
  src/main/plugin_force_open_drawing.cpp
  src/main/plugin_main_run.cpp
  src/main/plugin_model_diagnostics.cpp
  src/main/plugin_model_structure_analyzer.cpp
  src/main/plugin_param_tool.cpp
  src/main/plugin_quick_simprep.cpp
  src/main/plugin_sheetmetal_flat_batch.cpp
  src/main/plugin_quick_rename.cpp
  src/main/plugin_save_copy_to_workdir.cpp
  src/main/plugin_random_color.cpp
  src/main/plugin_relations.cpp
  src/main/plugin_runtime_bridge.cpp
  src/main/plugin_smart_dimension.cpp
  src/main/plugin_split.cpp
  src/main/plugin_family_table_manager.cpp
  src/main/plugin_feature_visibility.cpp
  src/main/plugin_entry.cpp
  src/ui/bom_dialog.cpp
  src/ui/balloon_arrange_dialog.cpp
  src/ui/batch_rename_dialog.cpp
  src/ui/delete_params_dialog.cpp
  src/ui/drawing_arrange_dialog.cpp
  src/ui/drawing_export_dialog.cpp
  src/ui/drawing_scale_sync_dialog.cpp
  src/ui/drawing_view_brush_dialog.cpp
  src/ui/drawing3_dialog.cpp
  src/ui/message_dialog.cpp
  src/ui/model_diagnostics_dialog.cpp
  src/ui/model_structure_analyzer_dialog.cpp
  src/ui/param_add_dialog.cpp
  src/ui/quick_simprep_dialog.cpp
  src/ui/sheetmetal_flat_batch_dialog.cpp
  src/ui/quick_rename_dialog.cpp
  src/ui/save_copy_to_workdir_dialog.cpp
  src/ui/random_color_dialog.cpp
  src/ui/relations_text_dialog.cpp
  src/ui/family_table_grid.cpp
  src/ui/family_table_add_column_dialog.cpp
  src/ui/family_table_manager_dialog.cpp
  src/ui/split_dialog.cpp
)

target_include_directories(autobbox_toolkit PRIVATE
  ${CREO_TK_INC}
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_compile_features(autobbox_toolkit PRIVATE cxx_std_17)

if(MSVC)
  target_compile_options(autobbox_toolkit PRIVATE /utf-8 /EHsc /W3)
endif()

target_link_directories(autobbox_toolkit PRIVATE ${CREO_TK_LIBDIR})

target_link_libraries(autobbox_toolkit PRIVATE
  protk_dllmd_NU.lib
  ucore.lib
  udata.lib
  ws2_32.lib
  wsock32.lib
  mpr.lib
  netapi32.lib
)

set_target_properties(autobbox_toolkit PROPERTIES
  PREFIX ""
  OUTPUT_NAME "autobbox_toolkit"
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/deploy/AutoBBoxToolkit"
  RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/deploy/AutoBBoxToolkit"
  RUNTIME_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/deploy/AutoBBoxToolkit/Debug"
  RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/deploy/AutoBBoxToolkit/RelWithDebInfo"
  RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/deploy/AutoBBoxToolkit/MinSizeRel"
  LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/deploy/AutoBBoxToolkit"
  LIBRARY_OUTPUT_DIRECTORY_RELEASE "${CMAKE_CURRENT_SOURCE_DIR}/deploy/AutoBBoxToolkit"
  LIBRARY_OUTPUT_DIRECTORY_DEBUG "${CMAKE_CURRENT_SOURCE_DIR}/deploy/AutoBBoxToolkit/Debug"
  LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${CMAKE_CURRENT_SOURCE_DIR}/deploy/AutoBBoxToolkit/RelWithDebInfo"
  LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${CMAKE_CURRENT_SOURCE_DIR}/deploy/AutoBBoxToolkit/MinSizeRel"
)

if(TARGET dwg_layout_renamer)
  add_dependencies(autobbox_toolkit dwg_layout_renamer)
endif()

