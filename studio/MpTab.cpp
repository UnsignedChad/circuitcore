// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "MpTab.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSpinBox>
#include <QStatusBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QToolBar>
#include <QVBoxLayout>
#include <QWidget>

#include "BoardModel.h"

#ifdef MPKIT_HAS_WIDGETS
#include "mpkit/widgets/FieldViewer.h"
#endif

#include "mp/Grid.h"
#include "mp/ComponentCoupling.h"
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

    // --- Left dock: per-component heat sources ---------------------
    // The Power [W] column is editable; everything else is informational.
    // Edits get stored in component_power_w_ keyed by reference, and at
    // Run time they're added to the IR-derived Joule source so the heat
    // solve sees chip dissipation as well as trace I^2R losses.
    components_table_ = new QTableWidget(this);
    components_table_->setColumnCount(4);
    components_table_->setHorizontalHeaderLabels(
        {tr("Ref"), tr("Value"), tr("Footprint"), tr("Power [W]")});
    components_table_->verticalHeader()->setVisible(false);
    components_table_->horizontalHeader()->setStretchLastSection(false);
    components_table_->horizontalHeader()->setSectionResizeMode(
        1, QHeaderView::Stretch);
    components_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    components_table_->setEditTriggers(QAbstractItemView::DoubleClicked |
                                         QAbstractItemView::SelectedClicked);
    connect(components_table_, &QTableWidget::cellChanged,
            this, &MpTab::onComponentPowerEdited);
    auto* comp_dock = new QDockWidget(tr("Components"), this);
    comp_dock->setWidget(components_table_);
    comp_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, comp_dock);

    // --- Status bar ------------------------------------------------
    status_ = new QLabel(tr("Load a board to run a study"), this);
    statusBar()->addWidget(status_);

    connect(model_, &BoardModel::boardLoaded, this, &MpTab::onBoardLoaded);
}

MpTab::~MpTab() = default;

void MpTab::onBoardLoaded() {
    QString msg = tr("Board loaded. Click Run PDN -> thermal demo to "
                       "voxelise + IR solve + heat solve in one step.");
    if (model_->board()) {
        const auto cs = mpkit::compute_component_summary(*model_->board());
        if (cs.n_components > 0) {
            msg += tr("  |  %1 components, %2 g total")
                       .arg(cs.n_components)
                       .arg(cs.total_mass_kg * 1000.0, 0, 'f', 2);
            if (cs.total_power_w > 0.0) {
                msg += tr(", %1 W spec'd (hottest: %2 @ %3 W)")
                           .arg(cs.total_power_w, 0, 'f', 2)
                           .arg(QString::fromStdString(cs.hottest_reference))
                           .arg(cs.hottest_power_w, 0, 'f', 2);
            }
        }
    }
    status_->setText(msg);
    refreshComponentsTable();
#ifdef MPKIT_HAS_WIDGETS
    if (model_->board()) viewer_->setBoard(*model_->board());
#endif
}

void MpTab::refreshComponentsTable() {
    if (!components_table_) return;
    components_table_->blockSignals(true);
    components_table_->clearContents();
    if (!model_->board()) {
        components_table_->setRowCount(0);
        components_table_->blockSignals(false);
        return;
    }
    const auto& comps = model_->board()->components;
    components_table_->setRowCount(static_cast<int>(comps.size()));
    int row = 0;
    for (const auto& c : comps) {
        const QString ref = QString::fromStdString(c.reference);
        auto* ref_item = new QTableWidgetItem(ref);
        ref_item->setFlags(ref_item->flags() & ~Qt::ItemIsEditable);
        auto* val_item = new QTableWidgetItem(QString::fromStdString(c.value));
        val_item->setFlags(val_item->flags() & ~Qt::ItemIsEditable);
        auto* fp_item  = new QTableWidgetItem(QString::fromStdString(c.name));
        fp_item->setFlags(fp_item->flags() & ~Qt::ItemIsEditable);
        // Honour any value the user already typed for this ref in a prior
        // session of this run; otherwise show whatever the parser knew
        // (currently always 0 -- no .kicad_pcb property carries it yet).
        const double init_p =
            component_power_w_.value(ref, c.dissipated_power_w);
        auto* p_item = new QTableWidgetItem(QString::number(init_p, 'f', 3));
        p_item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
        components_table_->setItem(row, 0, ref_item);
        components_table_->setItem(row, 1, val_item);
        components_table_->setItem(row, 2, fp_item);
        components_table_->setItem(row, 3, p_item);
        ++row;
    }
    components_table_->resizeColumnToContents(0);
    components_table_->resizeColumnToContents(3);
    components_table_->blockSignals(false);
}

void MpTab::onComponentPowerEdited(int row, int col) {
    if (col != 3 || !components_table_) return;
    auto* ref_item = components_table_->item(row, 0);
    auto* p_item   = components_table_->item(row, 3);
    if (!ref_item || !p_item) return;
    bool ok = false;
    const double p = p_item->text().toDouble(&ok);
    if (!ok || p < 0.0) {
        // Reject -- restore the previously stored value (or 0).
        components_table_->blockSignals(true);
        p_item->setText(QString::number(
            component_power_w_.value(ref_item->text(), 0.0), 'f', 3));
        components_table_->blockSignals(false);
        return;
    }
    component_power_w_[ref_item->text()] = p;
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

    // 3b. Add per-component dissipation from the user's table on top of
    //     the IR-derived Joule source. Without this the heat solve only
    //     sees trace I^2R losses (usually milliwatts) and never the
    //     chips (usually watts), so the temperature map ends up
    //     dominated by hot traces near the regulator instead of the
    //     parts that actually run hot.
    double component_total_w = 0.0;
    if (!component_power_w_.isEmpty()) {
        auto board_copy = *model_->board();
        for (auto& c : board_copy.components) {
            auto it = component_power_w_.find(
                QString::fromStdString(c.reference));
            if (it != component_power_w_.end()) {
                c.dissipated_power_w = it.value();
            }
        }
        auto comp = mpkit::component_power_to_joule_source(board_copy, vf);
        if (comp.ok && comp.source.size() == joule.source.size()) {
            for (std::size_t i = 0; i < joule.source.size(); ++i) {
                joule.source.data()[i] += comp.source.data()[i];
            }
            component_total_w = comp.total_power_w;
        }
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
    const double total_w = joule.total_power_w + component_total_w;
    status_->setText(
        tr("Done -- %1 voxels, %2 W total (%3 W traces + %4 W components).")
            .arg(vf.grid.voxel_count())
            .arg(total_w,             0, 'f', 3)
            .arg(joule.total_power_w, 0, 'f', 3)
            .arg(component_total_w,   0, 'f', 3));
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
