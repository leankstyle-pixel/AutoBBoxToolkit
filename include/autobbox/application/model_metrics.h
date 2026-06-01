#pragma once

#include <ProMdl.h>

#include <string>

namespace autobbox::application {

bool ComputeBBoxLwh(ProMdl mdl,
                    bool include_surface,
                    bool include_curve,
                    double &length_out,
                    double &width_out,
                    double &height_out);
bool ComputeBBoxAxes(ProMdl mdl,
                     bool include_surface,
                     bool include_curve,
                     double &size_x_out,
                     double &size_y_out,
                     double &size_z_out);
bool ComputeVolumeM3(ProMdl mdl, double &volume_out);
std::wstring IntLwhString(double length, double width, double height);
std::wstring FormatVol(double value);
bool ShouldSkipModel(ProMdl mdl);
bool HasFailedRegeneration(ProMdl mdl);

} // namespace autobbox::application
