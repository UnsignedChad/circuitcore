// COMSOL-style 3D scalar-field viewer.
//
// Holds a single circuitcore::field::Field3D + the Grid it lives on
// and renders it as a vtkImageData inside a QVTKOpenGLNativeWidget.
// v1 displays:
//
//   * the volume bounding box as a wireframe outline
//   * an axis-aligned slice plane (XY plane through the z-mid by
//     default) coloured by the field
//   * a colour-bar legend mapping the slice colours back to data values
//   * the XYZ axes triad in the bottom-left corner
//
// Future passes will add iso-surfaces (vtkContourFilter), GPU volume
// rendering (vtkGPUVolumeRayCastMapper), arbitrary plane slicing
// (vtkCutter), vector arrows (vtkGlyph3D + vtkArrowSource) and
// streamlines (vtkStreamTracer). The widget API stays compatible so the
// studio Mp tab does not need to change.

#pragma once

#include <QString>
#include <memory>

#include <QVTKOpenGLNativeWidget.h>

#include "circuitcore/field/Field3D.h"
#include "mp/Grid.h"

namespace mpkit::widgets {

class FieldViewer : public QVTKOpenGLNativeWidget {
    Q_OBJECT
public:
    explicit FieldViewer(QWidget* parent = nullptr);
    ~FieldViewer() override;

    // Replace the displayed field. Triggers a re-render. The viewer
    // chooses a colour-map range from the data min/max unless
    // setColormapRange has already been called with explicit bounds.
    void setField(const Grid& grid,
                  const circuitcore::field::Field3D& field,
                  const QString& field_label = "value");

    // Override the colour-map range (otherwise auto-fit to data).
    void setColormapRange(double v_min, double v_max);

    // Switch the colour-map. Recognised names: "coolwarm" (default),
    // "viridis", "jet", "grayscale". Unknown names fall back to
    // coolwarm.
    void setColormap(const QString& name);

    // Toggle the XYZ axes triad in the bottom-left.
    void setAxesVisible(bool visible);

    // Toggle the colour-bar legend on the right.
    void setColorBarVisible(bool visible);

    // Toggle the bounding-box outline.
    void setOutlineVisible(bool visible);

    // Slice plane index along the chosen axis. Set axis to one of
    // {0 = YZ plane / x slice, 1 = XZ plane / y slice, 2 = XY plane /
    // z slice}. Index is the 0-based voxel index in that axis.
    void setSlice(int axis, int index);

    // Perspective vs parallel (orthographic) projection.
    void setParallelProjection(bool parallel);

    // Snap the camera to fit the dataset.
    void resetCamera();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mpkit::widgets
