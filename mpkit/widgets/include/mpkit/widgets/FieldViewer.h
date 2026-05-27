// COMSOL-style 3D scalar-field viewer.
//
// Two distinct visualisation modes:
//
//   1. Field-on-geometry (the COMSOL default): render the board's 3D
//      stackup as actors (dielectric translucent FR-4 amber, copper
//      opaque, vias as solid cylinders) and color the copper surface
//      by a sampled scalar field. Call setBoard() once with the parsed
//      board, then setFieldOnCopperSurface() whenever a new field
//      lands. This is what you want for "where on my board is it hot".
//
//   2. Volumetric slice plane: render the voxel grid's bounding box
//      outline + an axis-aligned slice plane coloured by the field.
//      Useful for cross-section through the dielectric (where does
//      heat get trapped between layers?). Toggle via setSliceVisible.
//
// Both modes share one camera, one XYZ axes triad, one colour-bar
// legend, and one colour-map lookup table.

#pragma once

#include <QString>
#include <memory>

#include <QVTKOpenGLNativeWidget.h>

#include "circuitcore/board/Board.h"
#include "circuitcore/field/Field3D.h"
#include "mp/Grid.h"

namespace sikit::si { struct SiStackup; }

namespace mpkit::widgets {

class FieldViewer : public QVTKOpenGLNativeWidget {
    Q_OBJECT
public:
    explicit FieldViewer(QWidget* parent = nullptr);
    ~FieldViewer() override;

    // ---- mode 1: board geometry + field-on-copper ------------------

    // Build the 3D board mesh (dielectric / copper / vias) via
    // sikit::render::Mesher3D and add it to the scene. Replaces any
    // previously-set geometry. Pass a non-default si_stackup to honour
    // an existing SI stackup overlay; otherwise the board's own
    // stackup is used.
    void setBoard(const circuitcore::board::Board& board);

    // Sample a scalar field at every copper-mesh vertex and re-color
    // the copper actor with the supplied colour-map. Requires setBoard
    // to have been called first; otherwise this is a no-op.
    void setFieldOnCopperSurface(const Grid& grid,
                                  const circuitcore::field::Field3D& field,
                                  const QString& label = "value");

    // ---- mode 2: volumetric slice (kept from v1) -------------------

    // Replace the displayed field. Triggers a re-render. The viewer
    // chooses a colour-map range from the data min/max unless
    // setColormapRange has already been called with explicit bounds.
    void setField(const Grid& grid,
                  const circuitcore::field::Field3D& field,
                  const QString& field_label = "value");

    // Toggle the volumetric slice plane.
    void setSliceVisible(bool visible);

    // ---- shared controls -------------------------------------------

    void setColormapRange(double v_min, double v_max);
    void setColormap(const QString& name);    // "coolwarm", "viridis", "jet", "grayscale"

    void setAxesVisible(bool visible);
    void setColorBarVisible(bool visible);
    void setOutlineVisible(bool visible);

    void setSlice(int axis, int index);

    void setParallelProjection(bool parallel);

    void resetCamera();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace mpkit::widgets
