#pragma once

#include <ProToolkit.h>

#include <string>
#include <vector>

namespace autobbox::core {

enum class DrawingExportFormat {
    Dwg,
    Pdf,
    Dxf
};

enum class DwgExportMode {
    PerSheetFiles,
    MultiLayoutFile
};

struct DrawingExportSheetChoice {
    int sheet = 0;
    std::wstring name;
    std::wstring display_name;
};

struct DrawingExportRequest {
    DrawingExportFormat format = DrawingExportFormat::Pdf;
    DwgExportMode dwg_mode = DwgExportMode::PerSheetFiles;
    int selected_sheet = 1;
    std::vector<int> selected_sheets;
};

struct DrawingExportedFile {
    std::wstring path;
    int sheet = 0;
    ProError status = PRO_TK_NO_ERROR;
};

struct DrawingExportResult {
    bool success = false;
    ProError status = PRO_TK_GENERAL_ERROR;
    std::wstring output_dir;
    std::vector<DrawingExportedFile> files;
    int sheets_total = 0;
    int exports_attempted = 0;
    int exports_succeeded = 0;
};

} // namespace autobbox::core
