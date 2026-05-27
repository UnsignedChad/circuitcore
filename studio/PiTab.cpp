#include "PiTab.h"

#include <QDockWidget>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>
#include <QWidget>
#include <utility>

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
#include "pi/Mor.h"
#include "pi/Touchstone.h"
#include "pdnkit/StackupWriter.h"

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

    // --- File menu (export workflows) ---
    {
        auto* tb = addToolBar(tr("File"));
        tb->setMovable(false);
        auto* menu = new QMenu(this);
        menu->addAction(tr("Save modified stackup..."),
                         this, &PiTab::onSaveModifiedStackup);
        menu->addAction(tr("Export cavity Z(f) as Touchstone..."),
                         this, &PiTab::onExportCavityTouchstone);
        menu->addAction(tr("Export reduced SPICE subcircuit..."),
                         this, &PiTab::onExportReducedSpice);
        auto* btn = new QToolButton(tb);
        btn->setText(tr("File"));
        btn->setMenu(menu);
        btn->setPopupMode(QToolButton::InstantPopup);
        tb->addWidget(btn);
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



void PiTab::onSaveModifiedStackup() {
    if (!model_->board() || model_->currentPath().isEmpty()) {
        QMessageBox::information(this, tr("Save modified stackup"),
            tr("Open a KiCad PCB first."));
        return;
    }
    const QString src = model_->currentPath();
    const QString suggested =
        QFileInfo(src).completeBaseName() + "_pdnkit-stackup.kicad_pcb";
    const QString dst = QFileDialog::getSaveFileName(
        this, tr("Save modified stackup as"), suggested,
        tr("KiCad PCB (*.kicad_pcb)"));
    if (dst.isEmpty()) return;
    if (QFileInfo(dst) == QFileInfo(src)) {
        QMessageBox::warning(this, tr("Save modified stackup"),
            tr("Destination must differ from the source file."));
        return;
    }
    auto r = pdnkit::save_modified_stackup(
        src.toStdString(), dst.toStdString(), *model_->board());
    if (!r.ok) {
        QMessageBox::critical(this, tr("Save modified stackup"),
                               QString::fromStdString(r.error));
        return;
    }
    statusBar()->showMessage(
        tr("Wrote modified stackup to %1 (%2 layer(s) updated)")
            .arg(QFileInfo(dst).fileName()).arg(r.layers_updated),
        10000);
}

void PiTab::onExportCavityTouchstone() {
    if (!cavity_panel_->hasLastSweep()) {
        QMessageBox::information(this, tr("Export Touchstone"),
            tr("Run a cavity Z(f) sweep first."));
        return;
    }
    const QString src = model_->currentPath();
    const QString suggested = src.isEmpty()
        ? QString("pdnkit_zf.s1p")
        : QFileInfo(src).completeBaseName() + "_zf.s1p";
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export Touchstone Z(f)"), suggested,
        tr("Touchstone v1 (*.s1p)"));
    if (path.isEmpty()) return;

    const auto& freqs = cavity_panel_->lastSweepFreqs();
    const auto& zs    = cavity_panel_->lastSweepZ();
    std::vector<pdnkit::pi::TouchstoneSample> samples;
    samples.reserve(freqs.size());
    for (std::size_t i = 0; i < freqs.size(); ++i)
        samples.push_back({freqs[i], zs[i]});
    const std::string comment =
        std::string("circuitcore studio cavity Z(f) -- ") +
        QFileInfo(src).fileName().toStdString();
    if (!pdnkit::pi::write_touchstone_z1p(path.toStdString(),
                                           samples, comment)) {
        QMessageBox::critical(this, tr("Export Touchstone"),
            tr("Failed to write %1").arg(path));
        return;
    }
    statusBar()->showMessage(
        tr("Wrote %1 (%2 points)")
            .arg(QFileInfo(path).fileName()).arg(samples.size()),
        8000);
}

void PiTab::onExportReducedSpice() {
    if (last_mesh_.nodes.empty()) {
        QMessageBox::information(this, tr("Export reduced SPICE"),
            tr("Run an IR-drop analysis first; there is no mesh to reduce."));
        return;
    }
    std::vector<int> ports;
    for (int id : last_mesh_.source_node_ids) ports.push_back(id);
    for (int id : last_mesh_.sink_node_ids)   ports.push_back(id);
    if (ports.empty()) {
        QMessageBox::warning(this, tr("Export reduced SPICE"),
            tr("The last mesh has no source/sink nodes to use as ports."));
        return;
    }
    const QString src = model_->currentPath();
    const QString suggested = src.isEmpty()
        ? QString("pdnkit_reduced.sub")
        : QFileInfo(src).completeBaseName() + "_reduced.sub";
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Export reduced SPICE subcircuit"), suggested,
        tr("SPICE subcircuit (*.sub *.cir)"));
    if (path.isEmpty()) return;

    auto reduced = pdnkit::pi::reduce_to_ports(last_mesh_, ports);
    if (reduced.port_node_ids.empty()) {
        QMessageBox::critical(this, tr("Export reduced SPICE"),
            tr("Reduction failed (singular internal block?)."));
        return;
    }
    const std::string title =
        std::string("circuitcore studio reduced PDN -- ") +
        QFileInfo(src).fileName().toStdString() +
        " (" + std::to_string(last_mesh_.nodes.size()) + " nodes -> " +
        std::to_string(ports.size()) + " ports)";
    const auto netlist = pdnkit::pi::export_reduced_spice(reduced, title);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, tr("Export reduced SPICE"),
            tr("Failed to open %1").arg(path));
        return;
    }
    f.write(netlist.c_str());
    f.close();
    statusBar()->showMessage(
        tr("Wrote %1 (%2 nodes -> %3 ports)")
            .arg(QFileInfo(path).fileName())
            .arg(last_mesh_.nodes.size())
            .arg(ports.size()),
        10000);
}

}  // namespace circuitcore::studio
