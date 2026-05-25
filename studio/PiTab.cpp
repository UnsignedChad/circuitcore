#include "PiTab.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QStatusBar>
#include <QWidget>

#include "BoardModel.h"

// pdnkit widgets, all part of pdnkit::widgets
#include "pdnkit/AnalysisPanel.h"
#include "pdnkit/CavityPanel.h"
#include "pdnkit/ColorLegend.h"
#include "pdnkit/DrcPanel.h"
#include "pdnkit/LayerPanel.h"
#include "pdnkit/NetStatsPanel.h"
#include "pdnkit/PcbCanvas.h"
#include "pdnkit/TransientPanel.h"

#include "pi/IrMesher.h"
#include "pi/IrSolver.h"
#include "pi/Thermal.h"
#include "render/IrResultMesh.h"

namespace circuitcore::studio {

PiTab::PiTab(BoardModel* model, QWidget* parent)
    : QMainWindow(parent), model_(model) {
    // Nesting a QMainWindow inside another widget needs the window flag
    // cleared so Qt treats it as a child, not a top-level window.
    setWindowFlag(Qt::Widget);
    setDockOptions(QMainWindow::AnimatedDocks | QMainWindow::AllowTabbedDocks);

    // --- Central widget: pdnkit's flavoured canvas + color legend ---
    canvas_ = new pdnkit::PcbCanvas(this);
    legend_ = new ColorLegend(this);
    auto* central = new QWidget(this);
    auto* central_layout = new QHBoxLayout(central);
    central_layout->setContentsMargins(0, 0, 0, 0);
    central_layout->setSpacing(0);
    central_layout->addWidget(canvas_, 1);
    central_layout->addWidget(legend_);
    setCentralWidget(central);

    // --- Layers dock (right) ---
    layer_panel_ = new pdnkit::LayerPanel(this);
    {
        auto* scroll = new QScrollArea(this);
        scroll->setWidget(layer_panel_);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* dock = new QDockWidget("Layers", this);
        dock->setWidget(scroll);
        dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        addDockWidget(Qt::RightDockWidgetArea, dock);
        connect(layer_panel_, &pdnkit::LayerPanel::visibility_changed,
                canvas_, &pdnkit::PcbCanvas::setLayerVisibility);
    }

    // --- Analysis dock (right) ---
    analysis_panel_ = new AnalysisPanel(this);
    QDockWidget* an_dock = nullptr;
    {
        auto* scroll = new QScrollArea(this);
        scroll->setWidget(analysis_panel_);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        an_dock = new QDockWidget("Analysis", this);
        an_dock->setWidget(scroll);
        an_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        addDockWidget(Qt::RightDockWidgetArea, an_dock);
        connect(analysis_panel_, &AnalysisPanel::runRequested,
                this, &PiTab::onAnalyzeStaticIrDrop);
        connect(analysis_panel_, &AnalysisPanel::clearRequested,
                canvas_, [this]() {
                    canvas_->setIrResult({});
                    legend_->setRange(0, 0);
                });
    }

    // --- Net Stats dock, tabbed with Analysis ---
    netstats_panel_ = new NetStatsPanel(this);
    {
        auto* scroll = new QScrollArea(this);
        scroll->setWidget(netstats_panel_);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* dock = new QDockWidget("Net Stats", this);
        dock->setWidget(scroll);
        dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        addDockWidget(Qt::RightDockWidgetArea, dock);
        if (an_dock) tabifyDockWidget(an_dock, dock);
        connect(netstats_panel_, &NetStatsPanel::netSelected,
                analysis_panel_, &AnalysisPanel::setNetById);
    }

    // --- Cavity dock, tabbed with Analysis ---
    cavity_panel_ = new CavityPanel(this);
    {
        auto* scroll = new QScrollArea(this);
        scroll->setWidget(cavity_panel_);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* dock = new QDockWidget("Plane Z(f)", this);
        dock->setWidget(scroll);
        dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        addDockWidget(Qt::RightDockWidgetArea, dock);
        if (an_dock) tabifyDockWidget(an_dock, dock);
        connect(cavity_panel_, &CavityPanel::decapsChanged,
                canvas_, &pdnkit::PcbCanvas::setDecapMarkers);
        connect(cavity_panel_, &CavityPanel::cavityChanged,
                canvas_, &pdnkit::PcbCanvas::setCavityHighlight);
        connect(netstats_panel_, &NetStatsPanel::netSelected,
                cavity_panel_, &CavityPanel::setNetById);
        connect(cavity_panel_, &CavityPanel::modeShapeMesh, this,
                [this](pdnkit::render::IrResultMesh m) {
                    legend_->setRange(m.v_min, m.v_max);
                    canvas_->setIrResult(std::move(m));
                });
    }

    // --- Transient dock, tabbed with Analysis ---
    transient_panel_ = new TransientPanel(this);
    {
        auto* scroll = new QScrollArea(this);
        scroll->setWidget(transient_panel_);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* dock = new QDockWidget("Transient", this);
        dock->setWidget(scroll);
        dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        addDockWidget(Qt::RightDockWidgetArea, dock);
        if (an_dock) tabifyDockWidget(an_dock, dock);
        connect(netstats_panel_, &NetStatsPanel::netSelected,
                transient_panel_, &TransientPanel::setNetById);
    }

    // --- DRC dock, tabbed with Analysis ---
    drc_panel_ = new DrcPanel(this);
    {
        auto* scroll = new QScrollArea(this);
        scroll->setWidget(drc_panel_);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* dock = new QDockWidget("DRC", this);
        dock->setWidget(scroll);
        dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        addDockWidget(Qt::RightDockWidgetArea, dock);
        if (an_dock) tabifyDockWidget(an_dock, dock);
        connect(netstats_panel_, &NetStatsPanel::netSelected,
                drc_panel_, &DrcPanel::setNetById);
    }

    // --- Status bar hover line + probe hint ---
    hover_label_ = new QLabel(this);
    hover_label_->setMinimumWidth(300);
    statusBar()->addPermanentWidget(hover_label_);
    connect(canvas_, &pdnkit::PcbCanvas::hoverInfo, hover_label_, &QLabel::setText);
    connect(canvas_, &pdnkit::PcbCanvas::probeHint, this, [this](const QString& m) {
        statusBar()->showMessage(m, 8000);
    });
    connect(canvas_, &pdnkit::PcbCanvas::probeRequested,
            this, &PiTab::onProbeRequested);

    // --- Model wiring -- BoardModel is the single source of truth ---
    connect(model_, &BoardModel::boardLoaded, this, &PiTab::onBoardLoaded);
}

PiTab::~PiTab() = default;

void PiTab::onBoardLoaded() {
    const auto* b = model_->board();
    if (!b) return;
    canvas_->setBoard(b);
    populateLayerPanel();
    analysis_panel_->setBoard(b);
    netstats_panel_->setBoard(b);
    cavity_panel_->setBoard(b);
    transient_panel_->setBoard(b);
    drc_panel_->setBoard(b);
}

void PiTab::populateLayerPanel() {
    if (!layer_panel_) return;
    const auto* b = model_->board();
    if (!b) {
        layer_panel_->setLayers({});
        return;
    }
    // Same shape pdnkit/MainWindow.cpp uses: copper layers only, with
    // thickness converted to micrometres.
    std::vector<pdnkit::LayerPanel::Entry> entries;
    for (const auto& L : b->stackup.layers) {
        if (!L.is_copper()) continue;
        pdnkit::LayerPanel::Entry e;
        e.ordinal      = L.ordinal;
        e.name         = QString::fromStdString(L.name);
        e.thickness_um = L.thickness * 1.0e6;
        entries.push_back(e);
    }
    layer_panel_->setLayers(entries);
}

void PiTab::onAnalyzeStaticIrDrop() {
    const auto* b = model_->board();
    if (!b) {
        QMessageBox::information(this, tr("Static IR drop"),
                                  tr("Load a board first (File > Open)."));
        return;
    }
    auto mc = analysis_panel_->currentConfig();
    if (mc.net_id < 0) {
        QMessageBox::warning(this, tr("Static IR drop"),
                              tr("No net with copper zones available."));
        return;
    }
    if (mc.pad_currents.empty()) {
        QMessageBox::warning(this, tr("Static IR drop"),
                              tr("Set at least one non-zero pad current. "
                                  "Click Auto-balance for a default."));
        return;
    }
    auto mesh = pdnkit::pi::IrMesher::build(*b, mc);
    if (mesh.nodes.empty()) {
        QMessageBox::warning(this, tr("Static IR drop"),
                              tr("Mesher produced no nodes (check cell_size "
                                  "vs. zone size)."));
        return;
    }
    if (mesh.source_node_ids.empty() || mesh.sink_node_ids.empty()) {
        QMessageBox::warning(this, tr("Static IR drop"),
                              tr("Need at least two pads on the target net for "
                                  "source/sink. Auto-pick found insufficient pads."));
        return;
    }
    pdnkit::pi::Solution sol;
    if (analysis_panel_->thermalEnabled()) {
        pdnkit::pi::ThermalConfig tc;
        tc.r_theta_total_kw = analysis_panel_->thermalRThetaKw();
        tc.t_ambient_c      = analysis_panel_->thermalTAmbientC();
        auto tres = pdnkit::pi::solve_ir_with_thermal(*b, mc, {}, tc);
        if (!tres.solution.ok) {
            QMessageBox::critical(this, tr("Static IR drop (thermal)"),
                                   tr("Solver failed: %1").arg(
                                       QString::fromStdString(tres.solution.error)));
            return;
        }
        mesh = std::move(tres.mesh);
        sol  = std::move(tres.solution);
    } else {
        sol = pdnkit::pi::IrSolver::solve(mesh, {});
        if (!sol.ok) {
            QMessageBox::critical(this, tr("Static IR drop"),
                                   tr("Solver failed: %1").arg(
                                       QString::fromStdString(sol.error)));
            return;
        }
    }

    auto rm = pdnkit::render::build_ir_result_mesh(mesh, sol, mc.cell_size);
    canvas_->setIrResult(std::move(rm));
    legend_->setRange(sol.min_v, sol.max_v);
    canvas_->setProbeSource(mesh, sol);
    last_mesh_             = std::move(mesh);
    last_solution_         = std::move(sol);
    last_cell_size_        = mc.cell_size;
    last_copper_thickness_ = mc.copper_thickness;
    last_copper_rho_       = mc.copper_rho;
}

void PiTab::onProbeRequested(int pad_a, int pad_b, int net_id,
                              int layer_ord) {
    const auto* b = model_->board();
    if (!b) return;
    // Right-click probe-R: rerun IR-drop with only those two pads as
    // source/sink, then report the effective resistance in the status bar.
    pdnkit::pi::MeshConfig mc;
    mc.net_id            = net_id;
    mc.layer_ordinal     = layer_ord;
    mc.cell_size         = last_cell_size_;
    mc.copper_thickness  = last_copper_thickness_;
    mc.copper_rho        = last_copper_rho_;
    mc.source_pad_indices = {pad_a};
    mc.sink_pad_indices   = {pad_b};
    auto mesh = pdnkit::pi::IrMesher::build(*b, mc);
    if (mesh.nodes.empty() || mesh.source_node_ids.empty() ||
        mesh.sink_node_ids.empty()) {
        statusBar()->showMessage(tr("Probe R: mesh failed for picked pads."),
                                  6000);
        return;
    }
    auto sol = pdnkit::pi::IrSolver::solve(mesh, {1.0});
    if (!sol.ok) {
        statusBar()->showMessage(
            tr("Probe R: solver failed: %1").arg(
                QString::fromStdString(sol.error)), 6000);
        return;
    }
    const double r_eff = (sol.max_v - sol.min_v);  // R = V/I, I=1A
    statusBar()->showMessage(
        tr("Probe R: %1 mOhm between pads %2 and %3 on net %4")
            .arg(r_eff * 1e3, 0, 'f', 3)
            .arg(pad_a).arg(pad_b).arg(net_id), 12000);
}

}  // namespace circuitcore::studio
