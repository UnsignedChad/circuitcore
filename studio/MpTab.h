// Studio Mp tab -- COMSOL-style multiphysics workspace.
//
// Hosts the mpkit::widgets::FieldViewer in the centre with a left
// dock for the study tree and toolbar buttons to run the demo
// workflow + tweak the colour-map and slice plane.
//
// The viewer is compiled in only when CIRCUITCORE_BUILD_MPKIT_WIDGETS
// is ON (default) -- daily CI on slow runners can pass it OFF to skip
// the ~15-minute VTK FetchContent build, in which case this tab shows
// an explanatory placeholder instead of the 3D viewer.

#pragma once

#include <QMainWindow>

class QAction;
class QComboBox;
class QLabel;
class QListWidget;
class QSpinBox;

#ifdef MPKIT_HAS_WIDGETS
namespace mpkit::widgets { class FieldViewer; }
#endif

namespace circuitcore::studio {

class BoardModel;

class MpTab : public QMainWindow {
    Q_OBJECT
public:
    explicit MpTab(BoardModel* model, QWidget* parent = nullptr);
    ~MpTab() override;

private slots:
    void onBoardLoaded();

    // File menu (study persistence). Stubbed until #105 lands.
    void onLoadStudy();
    void onSaveStudyAs();

    // Quick-start: voxelise the loaded board, run pdnkit IR drop, run
    // the Joule coupling, run the steady-heat solver and display the
    // resulting temperature field.
    void onRunPdnThermalDemo();

    // Viewer controls (no-ops when widgets are not compiled in).
    void onResetCamera();
    void onColormapChanged(const QString& name);
    void onSliceAxisChanged(int axis);
    void onSliceIndexChanged(int index);

private:
    BoardModel* model_;

#ifdef MPKIT_HAS_WIDGETS
    mpkit::widgets::FieldViewer* viewer_ = nullptr;
#endif
    QListWidget* study_tree_  = nullptr;
    QLabel*      status_      = nullptr;
    QComboBox*   slice_axis_  = nullptr;
    QSpinBox*    slice_index_ = nullptr;
};

}  // namespace circuitcore::studio
