#include "MpTab.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include "BoardModel.h"

#ifdef MPKIT_HAS_WIDGETS
#include "mpkit/widgets/FieldViewer.h"
#endif

#include "mp/Grid.h"
#include "mp/JouleCoupling.h"
#include "mp/MaterialLibrary.h"
#include "mp/SteadyHeat.h"
#include "mp/Voxelizer.h"

#include "pi/IrMesher.h"
#include "pi/IrSolver.h"

namespace circuitcore::studio {

MpTab::MpTab(BoardModel* model, QWidget* parent)
    : QMainWindow(parent), model_(model) {
    setWindowFlag(Qt::Widget);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);

    // --- Central widget: the 3D field viewer ----------------------
#ifdef MPKIT_HAS_WIDGETS
    viewer_ = new mpkit::widgets::FieldViewer(this);
    setCentralWidget(viewer_);
#else
    auto* placeholder = new QLabel(
        tr("mpkit::widgets was not compiled in this build.\n\n"
           "Reconfigure with -DCIRCUITCORE_BUILD_MPKIT_WIDGETS=ON to\n"
           "enable the 3D field viewer (requires VTK build, ~15 min)."),
        this);
    placeholder->setAlignment(Qt::AlignCenter);
    setCentralWidget(placeholder);
#endif

    // --- Toolbar ---------------------------------------------------
    auto* tb = addToolBar(tr("Mp"));
    tb->setMovable(false);
    {
        auto* run = tb->addAction(tr("Run PDN -> thermal demo"));
        connect(run, &QAction::triggered, this, &MpTab::onRunPdnThermalDemo);
    }
    tb->addSeparator();
    {
        auto* load = tb->addAction(tr("Load study..."));
        connect(load, &QAction::triggered, this, &MpTab::onLoadStudy);
        auto* save = tb->addAction(tr("Save study as..."));
        connect(save, &QAction::triggered, this, &MpTab::onSaveStudyAs);
    }
    tb->addSeparator();
    {
        auto* reset = tb->addAction(tr("Reset view"));
        connect(reset, &QAction::triggered, this, &MpTab::onResetCamera);
    }
    {
        tb->addWidget(new QLabel(tr("  Colormap "), this));
        auto* cmap = new QComboBox(this);
        cmap->addItems({"coolwarm", "viridis", "jet", "grayscale"});
        connect(cmap, &QComboBox::currentTextChanged,
                this, &MpTab::onColormapChanged);
        tb->addWidget(cmap);
    }
    {
        tb->addWidget(new QLabel(tr("  Slice axis "), this));
        slice_axis_ = new QComboBox(this);
        slice_axis_->addItems({"X", "Y", "Z"});
        slice_axis_->setCurrentIndex(2);
        connect(slice_axis_,
                qOverload<int>(&QComboBox::currentIndexChanged),
                this, &MpTab::onSliceAxisChanged);
        tb->addWidget(slice_axis_);

        slice_index_ = new QSpinBox(this);
        slice_index_->setRange(0, 0);
        slice_index_->setValue(0);
        connect(slice_index_, qOverload<int>(&QSpinBox::valueChanged),
                this, &MpTab::onSliceIndexChanged);
        tb->addWidget(slice_index_);
    }

    // --- Left dock: study tree placeholder -------------------------
    study_tree_ = new QListWidget(this);
    study_tree_->addItem(tr("(no study loaded)"));
    auto* dock = new QDockWidget(tr("Study"), this);
    dock->setWidget(study_tree_);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, dock);

    // --- Status bar ------------------------------------------------
    status_ = new QLabel(tr("Load a board to run a study"), this);
    statusBar()->addWidget(status_);

    connect(model_, &BoardModel::boardLoaded, this, &MpTab::onBoardLoaded);
}

MpTab::~MpTab() = default;

void MpTab::onBoardLoaded() {
    status_->setText(tr("Board loaded. Click Run PDN -> thermal demo to "
                         "voxelise + IR solve + heat solve in one step."));
#ifdef MPKIT_HAS_WIDGETS
    if (model_->board()) viewer_->setBoard(*model_->board());
#endif
}

void MpTab::onLoadStudy() {
    QMessageBox::information(this, tr("Load study"),
        tr("Study load/save lands once the StudySerial PR merges."));
}

void MpTab::onSaveStudyAs() {
    QMessageBox::information(this, tr("Save study"),
        tr("Study load/save lands once the StudySerial PR merges."));
}

void MpTab::onRunPdnThermalDemo() {
    if (!model_->board()) {
        QMessageBox::information(this, tr("Run demo"),
                                  tr("Load a .kicad_pcb first."));
        return;
    }
    status_->setText(tr("Voxelising board..."));
    QApplication::processEvents();

    // 1. Voxelise.
    mpkit::VoxelizerConfig vc;
    vc.cell_xy_m = 1.0e-3;
    vc.cell_z_m  = 2.0e-4;
    auto vf = mpkit::voxelize_board(*model_->board(), vc);
    if (vf.grid.voxel_count() == 0) {
        QMessageBox::warning(this, tr("Run demo"),
                              tr("Voxeliser returned an empty grid."));
        return;
    }

    // 2. Pick a candidate power net for the IR drop.
    int net_id = 0;
    for (const auto& n : model_->board()->nets) {
        if (n.name == "GND" || n.name == "+3V3" || n.name == "+5V" ||
            n.name == "VCC" || n.name == "Vdd") {
            net_id = n.id;
            break;
        }
    }
    if (net_id == 0 && !model_->board()->nets.empty()) {
        net_id = model_->board()->nets.front().id;
    }
    pdnkit::pi::MeshConfig mc;
    mc.net_id        = net_id;
    mc.cell_size     = vc.cell_xy_m;
    mc.layer_ordinal = 0;
    auto mesh = pdnkit::pi::IrMesher::build(*model_->board(), mc);
    if (mesh.nodes.empty()) {
        QMessageBox::warning(this, tr("Run demo"),
                              tr("IR mesher returned an empty mesh."));
        return;
    }
    pdnkit::pi::SolveConfig sc;
    sc.total_current = 1.0;
    auto sol = pdnkit::pi::IrSolver::solve(mesh, sc);
    if (!sol.ok) {
        QMessageBox::critical(this, tr("Run demo"),
                               tr("IR solver failed: %1")
                                  .arg(QString::fromStdString(sol.error)));
        return;
    }

    // 3. Joule heat source.
    status_->setText(tr("Computing Joule source + solving 3D heat..."));
    QApplication::processEvents();
    auto joule = mpkit::ir_solution_to_joule_source(mesh, sol, vf);
    if (!joule.ok) {
        QMessageBox::critical(this, tr("Run demo"),
                               tr("Joule coupling failed: %1")
                                  .arg(QString::fromStdString(joule.error)));
        return;
    }

    // 4. Steady heat solve.
    mpkit::SteadyHeatConfig hc;
    hc.material_field    = vf;
    hc.volumetric_source = joule.source;
    hc.material_table.resize(3);
    hc.material_table[mpkit::kAirMaterialId]       = mpkit::air();
    hc.material_table[mpkit::kSubstrateMaterialId] = mpkit::fr4();
    hc.material_table[mpkit::kCopperMaterialId]    = mpkit::copper();
    mpkit::BoundaryCondition zmin, zmax, pin;
    zmin.target = mpkit::BcTarget::FaceZmin;
    zmin.kind   = mpkit::BcKind::Robin;
    zmin.h      = 10.0;
    zmin.u_ref  = 25.0;
    zmax.target = mpkit::BcTarget::FaceZmax;
    zmax.kind   = mpkit::BcKind::Robin;
    zmax.h      = 10.0;
    zmax.u_ref  = 25.0;
    pin.target = mpkit::BcTarget::FaceXmin;
    pin.kind   = mpkit::BcKind::Dirichlet;
    pin.value  = 25.0;
    hc.bcs = {zmin, zmax, pin};

    auto th = mpkit::solve_steady_heat(hc);
    if (!th.ok) {
        QMessageBox::critical(this, tr("Run demo"),
                               tr("Heat solver failed: %1")
                                  .arg(QString::fromStdString(th.error)));
        return;
    }

    // 5. Display: color the copper surface by the sampled temperature
    //    (the COMSOL-style surface plot), and also push the volumetric
    //    field through setField so the slice plane is available as a
    //    secondary indicator.
#ifdef MPKIT_HAS_WIDGETS
    viewer_->setFieldOnCopperSurface(vf.grid, th.temperature,
                                       tr("Temperature (degC)"));
    viewer_->setField(vf.grid, th.temperature, tr("Temperature (degC)"));
    viewer_->setSliceVisible(false);  // surface plot is the primary view
    slice_index_->setRange(0, vf.grid.nz() - 1);
    slice_index_->setValue(vf.grid.nz() / 2);
#else
    (void)th;
#endif
    status_->setText(
        tr("Done -- %1 voxels, %2 W total dissipated.")
            .arg(vf.grid.voxel_count())
            .arg(joule.total_power_w, 0, 'f', 3));
}

void MpTab::onResetCamera() {
#ifdef MPKIT_HAS_WIDGETS
    viewer_->resetCamera();
#endif
}

void MpTab::onColormapChanged(const QString& name) {
#ifdef MPKIT_HAS_WIDGETS
    viewer_->setColormap(name);
#else
    (void)name;
#endif
}

void MpTab::onSliceAxisChanged(int axis) {
#ifdef MPKIT_HAS_WIDGETS
    viewer_->setSlice(axis, slice_index_->value());
#else
    (void)axis;
#endif
}

void MpTab::onSliceIndexChanged(int index) {
#ifdef MPKIT_HAS_WIDGETS
    viewer_->setSlice(slice_axis_->currentIndex(), index);
#else
    (void)index;
#endif
}

}  // namespace circuitcore::studio
