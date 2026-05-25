#include "MainWindow.h"

#include <algorithm>

#include <QDockWidget>
#include <QCloseEvent>
#include <QFile>
#include <QMimeData>
#include <QDropEvent>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QSettings>
#include <QHBoxLayout>
#include <QScrollArea>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QImage>
#include <QMessageBox>
#include <QStatusBar>
#include <QTextStream>
#include <spdlog/spdlog.h>

#include "AnalysisPanel.h"
#include "ColorLegend.h"
#include "CavityPanel.h"
#include "TransientPanel.h"
#include "DrcPanel.h"
#include "NetStatsPanel.h"
#include "LayerPanel.h"
#include "StackupWriter.h"
#include "PcbCanvas.h"
#include "circuitcore/formats/kicad/PcbParser.h"
#include "pi/IrMesher.h"
#include "pi/IrSolver.h"
#include "pi/Mor.h"
#include "pi/Thermal.h"
#include "pi/Touchstone.h"
#include "render/IrResultMesh.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("pdnkit");
    // Allow the window to shrink small; docks each wrap their contents in
    // a QScrollArea below, so the panels' tall sizeHints don't force a
    // floor on the window height.
    setMinimumSize(480, 320);
    resize(1280, 800);
    setAcceptDrops(true);

    canvas_ = new pdnkit::PcbCanvas(this);
    legend_ = new ColorLegend(this);

    auto* central = new QWidget(this);
    auto* central_layout = new QHBoxLayout(central);
    central_layout->setContentsMargins(0, 0, 0, 0);
    central_layout->setSpacing(0);
    central_layout->addWidget(canvas_, 1);
    central_layout->addWidget(legend_);
    setCentralWidget(central);

    // Layer-visibility dock panel on the right.
    layer_panel_ = new pdnkit::LayerPanel(this);
    auto* layers_scroll = new QScrollArea(this);
    layers_scroll->setWidget(layer_panel_);
    layers_scroll->setWidgetResizable(true);
    layers_scroll->setFrameShape(QFrame::NoFrame);
    auto* dock = new QDockWidget("Layers", this);
    dock->setWidget(layers_scroll);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    connect(layer_panel_, &pdnkit::LayerPanel::visibility_changed,
            canvas_, &pdnkit::PcbCanvas::setLayerVisibility);

    // Analysis dock under Layers.
    analysis_panel_ = new AnalysisPanel(this);
    auto* an_scroll = new QScrollArea(this);
    an_scroll->setWidget(analysis_panel_);
    an_scroll->setWidgetResizable(true);
    an_scroll->setFrameShape(QFrame::NoFrame);
    auto* an_dock = new QDockWidget("Analysis", this);
    an_dock->setWidget(an_scroll);
    an_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, an_dock);
    connect(analysis_panel_, &AnalysisPanel::runRequested,
            this, &MainWindow::onAnalyzeStaticIrDrop);
    connect(analysis_panel_, &AnalysisPanel::netChanged, this,
            [this](int net_id) {
                if (current_board_path_.isEmpty()) return;
                QSettings settings("pdnkit", "pdnkit");
                const QString key = QString("board/%1/analysis/net")
                    .arg(current_board_path_);
                settings.setValue(key, net_id);
            });
    connect(analysis_panel_, &AnalysisPanel::clearRequested,
            canvas_, [this]() { canvas_->setIrResult({}); legend_->setRange(0, 0); });
    connect(analysis_panel_, &AnalysisPanel::viewModeChanged,
            this, &MainWindow::onViewModeChanged);

    // Net statistics dock, tabbed with Analysis on the right.
    netstats_panel_ = new NetStatsPanel(this);
    auto* nets_scroll = new QScrollArea(this);
    nets_scroll->setWidget(netstats_panel_);
    nets_scroll->setWidgetResizable(true);
    nets_scroll->setFrameShape(QFrame::NoFrame);
    auto* nets_dock = new QDockWidget("Net Stats", this);
    nets_dock->setWidget(nets_scroll);
    nets_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, nets_dock);
    tabifyDockWidget(an_dock, nets_dock);
    // Click a row in Net Stats -> propagate the selection to all analysis
    // panels so the user can jump straight from "this net has the most
    // copper" to running an analysis on it.
    connect(netstats_panel_, &NetStatsPanel::netSelected,
            analysis_panel_, &AnalysisPanel::setNetById);

    cavity_panel_ = new CavityPanel(this);
    auto* cav_scroll = new QScrollArea(this);
    cav_scroll->setWidget(cavity_panel_);
    cav_scroll->setWidgetResizable(true);
    cav_scroll->setFrameShape(QFrame::NoFrame);
    auto* cav_dock = new QDockWidget("Plane Z(f)", this);
    cav_dock->setWidget(cav_scroll);
    cav_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, cav_dock);
    tabifyDockWidget(an_dock, cav_dock);
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

    transient_panel_ = new TransientPanel(this);
    connect(netstats_panel_, &NetStatsPanel::netSelected,
            this, [this](int net_id) {
                if (transient_panel_) transient_panel_->setNetById(net_id);
            });
    auto* trn_scroll = new QScrollArea(this);
    trn_scroll->setWidget(transient_panel_);
    trn_scroll->setWidgetResizable(true);
    trn_scroll->setFrameShape(QFrame::NoFrame);
    auto* trn_dock = new QDockWidget("Transient", this);
    trn_dock->setWidget(trn_scroll);
    trn_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, trn_dock);
    tabifyDockWidget(an_dock, trn_dock);

    drc_panel_ = new DrcPanel(this);
    auto* drc_scroll = new QScrollArea(this);
    drc_scroll->setWidget(drc_panel_);
    drc_scroll->setWidgetResizable(true);
    drc_scroll->setFrameShape(QFrame::NoFrame);
    auto* drc_dock = new QDockWidget("DRC", this);
    drc_dock->setWidget(drc_scroll);
    drc_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, drc_dock);
    tabifyDockWidget(an_dock, drc_dock);
    connect(netstats_panel_, &NetStatsPanel::netSelected,
            drc_panel_, &DrcPanel::setNetById);

    auto* fileMenu = menuBar()->addMenu("&File");
    auto* openAct = fileMenu->addAction("&Open KiCad PCB...");
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpenKicadPcb);
    recent_menu_ = fileMenu->addMenu("Open &Recent");
    auto* reloadAct = fileMenu->addAction("&Reload");
    reloadAct->setShortcut(QKeySequence("Ctrl+R"));
    connect(reloadAct, &QAction::triggered, this, &MainWindow::onReloadBoard);
    fileMenu->addSeparator();
    auto* saveImgAct = fileMenu->addAction("&Save Canvas as Image...");
    saveImgAct->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(saveImgAct, &QAction::triggered, this, &MainWindow::onSaveCanvasImage);
    auto* exportCsvAct = fileMenu->addAction("&Export Results as CSV...");
    connect(exportCsvAct, &QAction::triggered, this, &MainWindow::onExportResultsCsv);
    auto* saveStackupAct = fileMenu->addAction(
        "Save Modified Stackup &As...");
    saveStackupAct->setToolTip(
        "Write a new .kicad_pcb file with the LayerPanel-edited "
        "thicknesses applied. Source file is never overwritten.");
    connect(saveStackupAct, &QAction::triggered, this,
            &MainWindow::onSaveModifiedStackup);
    auto* exportTouchstoneAct = fileMenu->addAction(
        "Export &Touchstone (.s1p)...");
    connect(exportTouchstoneAct, &QAction::triggered, this,
            &MainWindow::onExportTouchstone);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu("&View");
    auto* fitAct = viewMenu->addAction("&Fit to Board");
    fitAct->setShortcut(QKeySequence(Qt::Key_Home));
    connect(fitAct, &QAction::triggered, canvas_, &pdnkit::PcbCanvas::fitToBoard);
    viewMenu->addAction(dock->toggleViewAction());
    viewMenu->addAction(an_dock->toggleViewAction());
    viewMenu->addAction(nets_dock->toggleViewAction());
    viewMenu->addAction(cav_dock->toggleViewAction());
    viewMenu->addAction(trn_dock->toggleViewAction());
    viewMenu->addAction(drc_dock->toggleViewAction());

    auto* analyzeMenu = menuBar()->addMenu("&Analyze");
    auto* irAct = analyzeMenu->addAction("Static &IR drop on F.Cu");
    irAct->setShortcut(QKeySequence("Ctrl+I"));
    connect(irAct, &QAction::triggered, this, &MainWindow::onAnalyzeStaticIrDrop);
    auto* clearAct = analyzeMenu->addAction("&Clear overlay");
    connect(clearAct, &QAction::triggered, canvas_, [this]() {
        canvas_->setIrResult({});
        legend_->setRange(0, 0);
    });

    auto* reduceAct = analyzeMenu->addAction("Export Reduced SPICE Subcircuit...");
    connect(reduceAct, &QAction::triggered, this,
            &MainWindow::onExportReducedSpice);

    auto* helpMenu = menuBar()->addMenu("&Help");
    auto* shortcutsAct = helpMenu->addAction("&Keyboard Shortcuts...");
    connect(shortcutsAct, &QAction::triggered, this, &MainWindow::onShortcutsDialog);
    helpMenu->addSeparator();
    auto* aboutAct = helpMenu->addAction("&About pdnkit...");
    connect(aboutAct, &QAction::triggered, this, &MainWindow::onAboutDialog);

    // Permanent label on the right of the status bar for hover info.
    hover_label_ = new QLabel(this);
    hover_label_->setMinimumWidth(300);
    statusBar()->addPermanentWidget(hover_label_);
    connect(canvas_, &pdnkit::PcbCanvas::hoverInfo, hover_label_, &QLabel::setText);
    connect(canvas_, &pdnkit::PcbCanvas::probeHint, this, [this](const QString& m) {
        statusBar()->showMessage(m, 8000);
    });
    connect(canvas_, &pdnkit::PcbCanvas::probeRequested,
            this, &MainWindow::onProbeRequested);

    statusBar()->showMessage("Ready");

    // Restore previously-saved window geometry + dock state + camera.
    QSettings settings("pdnkit", "pdnkit");
    if (auto geom = settings.value("window/geometry").toByteArray(); !geom.isEmpty()) {
        restoreGeometry(geom);
    }
    if (auto st = settings.value("window/state").toByteArray(); !st.isEmpty()) {
        restoreState(st);
    }
    canvas_->restoreSettings(settings);
    recent_files_ = settings.value("recent/files").toStringList();
    updateRecentMenu();
}

void MainWindow::closeEvent(QCloseEvent* e) {
    QSettings settings("pdnkit", "pdnkit");
    settings.setValue("window/geometry", saveGeometry());
    settings.setValue("window/state", saveState());
    canvas_->saveSettings(settings);
    settings.setValue("recent/files", recent_files_);
    QMainWindow::closeEvent(e);
}

void MainWindow::onOpenKicadPcb() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open KiCad PCB", QString(),
        "KiCad PCB (*.kicad_pcb);;All files (*)");
    if (path.isEmpty()) return;
    loadKicadPcb(path);
}

void MainWindow::onAnalyzeStaticIrDrop() {
    if (!board_) {
        QMessageBox::information(this, "Static IR drop",
                                 "Open a KiCad PCB first.");
        return;
    }

    // Net, layer, source/sink, current, and cell size all come from the
    // AnalysisPanel — see currentConfig() / currentTotalCurrent().
    auto mc = analysis_panel_->currentConfig();
    if (mc.net_id < 0) {
        QMessageBox::warning(this, "Static IR drop",
                             "No net with copper zones available.");
        return;
    }
    if (mc.pad_currents.empty()) {
        QMessageBox::warning(this, "Static IR drop",
                             "Set at least one non-zero pad current. "
                             "Click Auto-balance for a default.");
        return;
    }
    auto mesh = pdnkit::pi::IrMesher::build(*board_, mc);
    if (mesh.nodes.empty()) {
        QMessageBox::warning(this, "Static IR drop",
                             "Mesher produced no nodes for the selected net "
                             "(check cell_size vs. zone size).");
        return;
    }
    if (mesh.source_node_ids.empty() || mesh.sink_node_ids.empty()) {
        QMessageBox::warning(this, "Static IR drop",
                             "Need at least two pads on the target net to set "
                             "source/sink. Found insufficient pads on F.Cu.");
        return;
    }

    pdnkit::pi::Solution sol;
    pdnkit::pi::ThermalResult tres;
    if (analysis_panel_->thermalEnabled()) {
        pdnkit::pi::ThermalConfig tc;
        tc.r_theta_total_kw = analysis_panel_->thermalRThetaKw();
        tc.t_ambient_c = analysis_panel_->thermalTAmbientC();
        tres = pdnkit::pi::solve_ir_with_thermal(*board_, mc, {}, tc);
        if (!tres.solution.ok) {
            QMessageBox::critical(this, "Static IR drop (thermal)",
                                  QString("Solver failed: %1")
                                      .arg(QString::fromStdString(tres.solution.error)));
            return;
        }
        // The thermal iteration produces its own final mesh + solution.
        mesh = std::move(tres.mesh);
        sol = std::move(tres.solution);
    } else {
        sol = pdnkit::pi::IrSolver::solve(mesh, {});
        if (!sol.ok) {
            QMessageBox::critical(this, "Static IR drop",
                                  QString("Solver failed: %1")
                                      .arg(QString::fromStdString(sol.error)));
            return;
        }
    }

    auto result_mesh = pdnkit::render::build_ir_result_mesh(mesh, sol,
                                                             mc.cell_size);
    canvas_->setIrResult(std::move(result_mesh));
    legend_->setRange(sol.min_v, sol.max_v);
    canvas_->setProbeSource(mesh, sol);
    last_mesh_ = std::move(mesh);
    last_solution_ = std::move(sol);
    last_cell_size_       = mc.cell_size;
    last_copper_thickness_ = mc.copper_thickness;
    last_copper_rho_       = mc.copper_rho;
    // Honor the panel's current view mode for the just-finished solve.
    if (analysis_panel_->viewMode() ==
        AnalysisPanel::ViewMode::CurrentDensity) {
        auto cd = pdnkit::render::build_current_density_mesh(
            last_mesh_, last_solution_, last_cell_size_,
            last_copper_thickness_, last_copper_rho_);
        legend_->setRange(cd.v_min, cd.v_max);
        canvas_->setIrResult(std::move(cd));
    }

    const auto* net = board_->find_net(mc.net_id);
    const QString net_name = (net && !net->name.empty())
        ? QString::fromStdString(net->name)
        : QString("net %1").arg(mc.net_id);
    const double v_drop_mv = (sol.max_v - sol.min_v) * 1000.0;
    const int reported_layer = (mesh.primary_layer_used >= 0)
        ? mesh.primary_layer_used : mc.layer_ordinal;
    const auto* layer = board_->find_layer(reported_layer);
    const QString layer_name = layer
        ? QString::fromStdString(layer->name) +
            (reported_layer != mc.layer_ordinal ? " (auto)" : "")
        : QString("layer %1").arg(reported_layer);
    double total_injected = 0.0;
    for (const auto& [_, cur] : mc.pad_currents) {
        if (cur > 0.0) total_injected += cur;
    }
    // Find the worst-drop node so we can name where the hotspot sits in
    // world (mm) coords. (build_ir_result_mesh already marks it for the
    // canvas, but the status bar still says only the V range -- this
    // tells the user *where* to look.)
    double worst_x = 0.0, worst_y = 0.0;
    {
        double v_min = 0.0;
        bool first = true;
        for (std::size_t i = 0; i < last_mesh_.nodes.size(); ++i) {
            const double v = last_solution_.voltages[i];
            if (first || v < v_min) {
                v_min = v;
                worst_x = last_mesh_.nodes[i].x;
                worst_y = last_mesh_.nodes[i].y;
                first = false;
            }
        }
    }
    statusBar()->showMessage(
        QString("IR drop on %1 (%2, %3 A injected): %4 nodes, %5 resistors, "
                "Vmax = %6 mV, Vmin = %7 mV  (drop %8 mV)  "
                "Hotspot at (%9, %10) mm")
            .arg(net_name)
            .arg(layer_name)
            .arg(total_injected, 0, 'f', 3)
            .arg(last_mesh_.nodes.size())
            .arg(last_mesh_.resistors.size())
            .arg(last_solution_.max_v * 1000.0, 0, 'f', 4)
            .arg(last_solution_.min_v * 1000.0, 0, 'f', 4)
            .arg(v_drop_mv, 0, 'f', 4)
            .arg(worst_x * 1000.0, 0, 'f', 2)
            .arg(worst_y * 1000.0, 0, 'f', 2));
    if (analysis_panel_->thermalEnabled() && tres.converged) {
        statusBar()->showMessage(
            statusBar()->currentMessage() +
            QString("  | thermal: deltaT=%1 C, iter=%2, rho=+%3%%")
                .arg(tres.final_delta_t_c, 0, 'f', 1)
                .arg(tres.iterations)
                .arg((tres.final_rho / mc.copper_rho - 1.0) * 100.0, 0, 'f', 2));
    }
    spdlog::info("IR drop on net {} ({}) layer {}: {} nodes, {} resistors, "
                 "Vmax={:.6f}V, Vmin={:.6f}V",
                 mc.net_id, net_name.toStdString(), mc.layer_ordinal,
                 mesh.nodes.size(), mesh.resistors.size(),
                 sol.max_v, sol.min_v);
}

void MainWindow::populateLayerPanel() {
    if (!board_) {
        layer_panel_->setLayers({});
        return;
    }
    std::vector<pdnkit::LayerPanel::Entry> entries;
    for (const auto& L : board_->stackup.layers) {
        if (!L.is_copper()) continue;
        pdnkit::LayerPanel::Entry e;
        e.ordinal = L.ordinal;
        e.name = QString::fromStdString(L.name);
        e.thickness_um = L.thickness * 1.0e6;  // m -> um
        entries.push_back(e);
    }
    layer_panel_->setLayers(entries);
}

bool MainWindow::loadKicadPcb(const QString& path) {
    auto parse_result = circuitcore::formats::kicad::PcbParser::parse_file(
        path.toStdString());
    if (!parse_result) {
        const auto msg = parse_result.error().format();
        QMessageBox::critical(this, "Open KiCad PCB failed",
                              QString::fromStdString(msg));
        spdlog::error("failed to load {}: {}", path.toStdString(), msg);
        return false;
    }
    try {
        auto board = std::make_unique<circuitcore::board::Board>(
            std::move(*parse_result));

        const auto net_count   = board->nets.size();
        const auto seg_count   = board->segments.size();
        const auto via_count   = board->vias.size();
        const auto pad_count   = board->pads.size();
        const auto zone_count  = board->zones.size();
        const auto layer_count = board->stackup.layers.size();
        const auto copper_layers = std::count_if(
            board->stackup.layers.begin(), board->stackup.layers.end(),
            [](const auto& l) { return l.is_copper(); });

        board_ = std::move(board);
        canvas_->setBoard(board_.get());
        populateLayerPanel();
        current_board_path_ = path;
        addRecent(path);
        analysis_panel_->setBoard(board_.get());
        netstats_panel_->setBoard(board_.get());
        cavity_panel_->setBoard(board_.get());
        transient_panel_->setBoard(board_.get());
        drc_panel_->setBoard(board_.get());

        // Restore per-board last-selected net for the Analysis panel.
        QSettings settings("pdnkit", "pdnkit");
        const QString key = QString("board/%1/analysis/net").arg(path);
        if (settings.contains(key)) {
            const int net_id = settings.value(key).toInt();
            analysis_panel_->setNetById(net_id);
        }

        spdlog::info("loaded {}: {} layers ({} copper), {} nets, {} segments, "
                     "{} vias, {} pads, {} zones",
                     path.toStdString(), layer_count, copper_layers,
                     net_count, seg_count, via_count, pad_count, zone_count);

        const QString summary = QString(
            "Loaded %1  —  %2 nets, %3 segments, %4 vias, %5 pads, %6 zones (%7 copper layers)")
            .arg(QFileInfo(path).fileName())
            .arg(net_count).arg(seg_count).arg(via_count)
            .arg(pad_count).arg(zone_count).arg(copper_layers);
        statusBar()->showMessage(summary);
        setWindowTitle(QString("pdnkit — %1").arg(QFileInfo(path).fileName()));
        return true;
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Open KiCad PCB failed", e.what());
        spdlog::error("failed to load {}: {}", path.toStdString(), e.what());
        return false;
    }
}

void MainWindow::onSaveCanvasImage() {
    if (!canvas_) return;
    QString path = QFileDialog::getSaveFileName(
        this, "Save canvas as image", QString(),
        "PNG (*.png);;JPEG (*.jpg);;BMP (*.bmp)");
    if (path.isEmpty()) return;
    QImage img = canvas_->grabFramebuffer();
    if (img.save(path)) {
        statusBar()->showMessage(
            QString("Saved %1  (%2 x %3)")
                .arg(QFileInfo(path).fileName())
                .arg(img.width())
                .arg(img.height()));
    } else {
        QMessageBox::warning(this, "Save failed",
                             QString("Could not write %1").arg(path));
    }
}

void MainWindow::onExportResultsCsv() {
    if (!last_solution_.ok || last_mesh_.nodes.empty() ||
        last_solution_.voltages.size() != last_mesh_.nodes.size()) {
        QMessageBox::information(this, "Export results",
            "No analysis result to export. Run Analyze first.");
        return;
    }
    QString path = QFileDialog::getSaveFileName(
        this, "Export results as CSV", QString(), "CSV (*.csv)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Export failed",
                             QString("Could not open %1 for writing").arg(path));
        return;
    }
    QTextStream out(&f);
    out << "node_id,x_mm,y_mm,voltage_mV\n";
    for (std::size_t i = 0; i < last_mesh_.nodes.size(); ++i) {
        const auto& n = last_mesh_.nodes[i];
        out << n.id << ','
            << QString::number(n.x * 1000.0, 'f', 4) << ','
            << QString::number(n.y * 1000.0, 'f', 4) << ','
            << QString::number(last_solution_.voltages[i] * 1000.0, 'f', 6)
            << '\n';
    }
    statusBar()->showMessage(
        QString("Exported %1 nodes to %2")
            .arg(last_mesh_.nodes.size())
            .arg(QFileInfo(path).fileName()));
}

void MainWindow::onReloadBoard() {
    if (current_board_path_.isEmpty()) {
        QMessageBox::information(this, "Reload", "No board loaded yet.");
        return;
    }
    if (!loadKicadPcb(current_board_path_)) {
        // loadKicadPcb already showed an error box; status bar will reflect it.
    }
}

void MainWindow::onAboutDialog() {
    QMessageBox::about(this, "About pdnkit",
        "<h3>pdnkit 0.0.1</h3>"
        "<p>Open-source Power Integrity analysis for KiCad PCBs.</p>"
        "<p><b>Pillars:</b></p>"
        "<ul>"
        "<li>Static IR drop (sparse Cholesky, multi-layer mesh, via wiring)</li>"
        "<li>Frequency-domain plane Z(f) (cavity model, decap network, "
        "greedy decap optimizer)</li>"
        "</ul>"
        "<p>Built with C++20, Qt 6, Eigen + SuiteSparse / CHOLMOD, earcut.hpp.</p>"
        "<p>License: GPL-3.0<br>"
        "Source: <a href=\"https://github.com/UnsignedChad/pdnkit\">"
        "github.com/UnsignedChad/pdnkit</a></p>");
}

void MainWindow::addRecent(const QString& path) {
    QFileInfo fi(path);
    const QString canon = fi.canonicalFilePath();
    if (canon.isEmpty()) return;
    recent_files_.removeAll(canon);
    recent_files_.prepend(canon);
    constexpr int kMaxRecent = 8;
    while (recent_files_.size() > kMaxRecent) recent_files_.removeLast();
    updateRecentMenu();
}

void MainWindow::updateRecentMenu() {
    if (!recent_menu_) return;
    recent_menu_->clear();

    // Drop entries that no longer exist on disk.
    QStringList alive;
    for (const QString& p : recent_files_) {
        if (QFileInfo::exists(p)) alive << p;
    }
    if (alive.size() != recent_files_.size()) recent_files_ = alive;

    if (recent_files_.isEmpty()) {
        auto* empty = recent_menu_->addAction("(no recent files)");
        empty->setEnabled(false);
        return;
    }
    for (const QString& p : recent_files_) {
        QFileInfo fi(p);
        const QString label = fi.fileName() + "  -  " + fi.path();
        auto* act = recent_menu_->addAction(label);
        connect(act, &QAction::triggered, this, [this, p]() {
            loadKicadPcb(p);
        });
    }
    recent_menu_->addSeparator();
    auto* clear = recent_menu_->addAction("Clear list");
    connect(clear, &QAction::triggered, this, [this]() {
        recent_files_.clear();
        updateRecentMenu();
    });
}

void MainWindow::onShortcutsDialog() {
    QMessageBox::information(this, "Keyboard shortcuts",
        "<table cellpadding='4'>"
        "<tr><th align='left'>File</th><th></th></tr>"
        "<tr><td><b>Ctrl+O</b></td><td>Open KiCad PCB</td></tr>"
        "<tr><td><b>Ctrl+R</b></td><td>Reload current board</td></tr>"
        "<tr><td><b>Ctrl+Shift+S</b></td><td>Save canvas as image</td></tr>"
        "<tr><th align='left'>View</th><th></th></tr>"
        "<tr><td><b>Home</b></td><td>Fit camera to board</td></tr>"
        "<tr><th align='left'>Analyze</th><th></th></tr>"
        "<tr><td><b>Ctrl+I</b></td><td>Static IR drop on current selection</td></tr>"
        "<tr><th align='left'>Canvas mouse</th><th></th></tr>"
        "<tr><td><b>Drag</b></td><td>Pan</td></tr>"
        "<tr><td><b>Wheel</b></td><td>Zoom toward cursor</td></tr>"
        "<tr><td><b>Hover</b></td><td>Net + layer (+ voltage if heatmap)</td></tr>"
        "<tr><td><b>Right-click pad</b></td><td>Probe R (click two pads on the same net)</td></tr>"
        "</table>");
}

void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    const QMimeData* m = e->mimeData();
    if (!m->hasUrls()) return;
    for (const QUrl& u : m->urls()) {
        if (u.isLocalFile() && u.toLocalFile().endsWith(".kicad_pcb",
                                                         Qt::CaseInsensitive)) {
            e->acceptProposedAction();
            return;
        }
    }
}

void MainWindow::dropEvent(QDropEvent* e) {
    const QMimeData* m = e->mimeData();
    if (!m->hasUrls()) return;
    for (const QUrl& u : m->urls()) {
        if (u.isLocalFile()) {
            const QString p = u.toLocalFile();
            if (p.endsWith(".kicad_pcb", Qt::CaseInsensitive)) {
                if (loadKicadPcb(p)) {
                    e->acceptProposedAction();
                    return;
                }
            }
        }
    }
}



void MainWindow::onProbeRequested(int pad_a, int pad_b,
                                  int net_id, int layer_ord) {
    if (!board_) return;
    const auto* net = board_->find_net(net_id);
    const QString net_name = (net && !net->name.empty())
        ? QString::fromStdString(net->name)
        : QString("(unnamed)");

    pdnkit::pi::MeshConfig mc;
    // Use the analysis panel's chosen cell size so the probe matches what
    // the user sees in the heat map. Fall back to 0.5 mm.
    {
        auto cfg = analysis_panel_->currentConfig();
        mc.cell_size = (cfg.cell_size > 0.0) ? cfg.cell_size : 0.5e-3;
    }
    mc.net_id = net_id;
    mc.layer_ordinal = layer_ord;
    mc.source_pad_indices = {pad_a};
    mc.sink_pad_indices   = {pad_b};

    auto mesh = pdnkit::pi::IrMesher::build(*board_, mc);
    if (mesh.nodes.empty()) {
        QMessageBox::warning(this, "Probe R",
            "Mesher produced no nodes on net " + net_name +
            ". Try a finer cell size or pick pads on a filled copper area.");
        return;
    }
    if (mesh.source_node_ids.empty() || mesh.sink_node_ids.empty()) {
        QMessageBox::warning(this, "Probe R",
            "Could not attach one of the probe pads to the mesh "
            "(pad may be outside any filled zone on the chosen layer).");
        return;
    }
    auto sol = pdnkit::pi::IrSolver::solve(mesh, {1.0});
    if (!sol.ok) {
        QMessageBox::critical(this, "Probe R",
            QString("Solver failed: %1")
                .arg(QString::fromStdString(sol.error)));
        return;
    }
    auto avg = [&](const std::vector<int>& ids) {
        double acc = 0.0;
        for (int id : ids)
            if (id >= 0 && id < static_cast<int>(sol.voltages.size()))
                acc += sol.voltages[id];
        return acc / static_cast<double>(ids.size());
    };
    const double v_src = avg(mesh.source_node_ids);
    const double v_snk = avg(mesh.sink_node_ids);
    const double r = v_src - v_snk;  // 1 A injection -> R = dV / 1
    const auto& pa = board_->pads[pad_a];
    const auto& pb = board_->pads[pad_b];
    const QString msg = QString(
        "Pad %1  ->  Pad %2   on net %3\n\n"
        "Effective resistance:\n"
        "    R = %4 Î©   (%5 mÎ©)\n\n"
        "(V_source = %6 V,  V_sink = %7 V at 1 A injection)")
        .arg(QString::fromStdString(pa.name))
        .arg(QString::fromStdString(pb.name))
        .arg(net_name)
        .arg(r, 0, 'e', 4)
        .arg(r * 1000.0, 0, 'f', 4)
        .arg(v_src, 0, 'f', 6)
        .arg(v_snk, 0, 'f', 6);
    QMessageBox::information(this, "Probe R", msg);
    statusBar()->showMessage(
        QString("Probe R %1 -> %2  on %3:  %4 mÎ©")
            .arg(QString::fromStdString(pa.name))
            .arg(QString::fromStdString(pb.name))
            .arg(net_name)
            .arg(r * 1000.0, 0, 'f', 4),
        12000);
}


void MainWindow::onViewModeChanged(int mode) {
    if (!last_solution_.ok || last_mesh_.nodes.empty() ||
        last_solution_.voltages.size() != last_mesh_.nodes.size()) {
        // No solution to recolor yet -- toggle takes effect on next Run.
        return;
    }
    if (mode == static_cast<int>(AnalysisPanel::ViewMode::CurrentDensity)) {
        auto cd = pdnkit::render::build_current_density_mesh(
            last_mesh_, last_solution_, last_cell_size_,
            last_copper_thickness_, last_copper_rho_);
        legend_->setRange(cd.v_min, cd.v_max);
        canvas_->setIrResult(std::move(cd));
        statusBar()->showMessage(
            QString("Current-density view:  |J| range %1 - %2 A/m")
                .arg(cd.v_min, 0, 'f', 1).arg(cd.v_max, 0, 'f', 1),
            10000);
    } else {
        auto rm = pdnkit::render::build_ir_result_mesh(
            last_mesh_, last_solution_, last_cell_size_);
        legend_->setRange(last_solution_.min_v, last_solution_.max_v);
        canvas_->setIrResult(std::move(rm));
        statusBar()->showMessage("Voltage-drop view restored.", 6000);
    }
}

void MainWindow::onExportTouchstone() {
    if (!cavity_panel_ || !cavity_panel_->hasLastSweep()) {
        QMessageBox::information(this, "Export Touchstone",
            "Run a Plane Z(f) sweep first; there is no data to export.");
        return;
    }
    const QString suggested = current_board_path_.isEmpty()
        ? QString("pdnkit_zf.s1p")
        : QFileInfo(current_board_path_).completeBaseName() + "_zf.s1p";
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Touchstone", suggested,
        "Touchstone v1 (*.s1p);;All files (*)");
    if (path.isEmpty()) return;

    const auto& freqs = cavity_panel_->lastSweepFreqs();
    const auto& zs    = cavity_panel_->lastSweepZ();
    std::vector<pdnkit::pi::TouchstoneSample> samples;
    samples.reserve(freqs.size());
    for (std::size_t i = 0; i < freqs.size(); ++i) {
        samples.push_back({freqs[i], zs[i]});
    }
    const std::string comment =
        std::string("pdnkit cavity Z(f) sweep -- ") +
        QFileInfo(current_board_path_).fileName().toStdString();
    if (!pdnkit::pi::write_touchstone_z1p(path.toStdString(),
                                          samples, comment)) {
        QMessageBox::critical(this, "Export Touchstone",
            "Failed to write " + path);
        return;
    }
    statusBar()->showMessage(
        QString("Wrote Touchstone to %1 (%2 frequency points)")
            .arg(path).arg(samples.size()),
        8000);
}

void MainWindow::onExportReducedSpice() {
    if (last_mesh_.nodes.empty()) {
        QMessageBox::information(this, "Export Reduced SPICE",
            "Run an IR-drop analysis first; there is no mesh to reduce.");
        return;
    }
    // Use the last solve's source + sink nodes as the kept ports.
    std::vector<int> ports;
    for (int id : last_mesh_.source_node_ids) ports.push_back(id);
    for (int id : last_mesh_.sink_node_ids)   ports.push_back(id);
    if (ports.empty()) {
        QMessageBox::warning(this, "Export Reduced SPICE",
            "The last mesh has no source/sink nodes to use as ports.");
        return;
    }

    const QString suggested = current_board_path_.isEmpty()
        ? QString("pdnkit_reduced.sub")
        : QFileInfo(current_board_path_).completeBaseName() + "_reduced.sub";
    const QString path = QFileDialog::getSaveFileName(
        this, "Export Reduced SPICE Subcircuit", suggested,
        "SPICE subcircuit (*.sub *.cir);;All files (*)");
    if (path.isEmpty()) return;

    auto reduced = pdnkit::pi::reduce_to_ports(last_mesh_, ports);
    if (reduced.port_node_ids.empty()) {
        QMessageBox::critical(this, "Export Reduced SPICE",
            "Reduction failed (singular internal block?).");
        return;
    }
    const std::string title =
        std::string("pdnkit reduced PDN -- ") +
        QFileInfo(current_board_path_).fileName().toStdString() +
        " (" + std::to_string(last_mesh_.nodes.size()) + " nodes -> " +
        std::to_string(ports.size()) + " ports)";
    const auto netlist = pdnkit::pi::export_reduced_spice(reduced, title);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::critical(this, "Export Reduced SPICE",
            "Failed to open " + path);
        return;
    }
    f.write(netlist.c_str());
    f.close();
    statusBar()->showMessage(
        QString("Wrote reduced subcircuit to %1 "
                "(%2 nodes -> %3 ports)")
            .arg(path).arg(last_mesh_.nodes.size()).arg(ports.size()),
        10000);
}

void MainWindow::onSaveModifiedStackup() {
    if (!board_ || current_board_path_.isEmpty()) {
        QMessageBox::information(this, "Save Modified Stackup",
            "Open a KiCad PCB first.");
        return;
    }
    const QString suggested =
        QFileInfo(current_board_path_).completeBaseName() +
        "_pdnkit-stackup.kicad_pcb";
    const QString out_path = QFileDialog::getSaveFileName(
        this, "Save Modified Stackup As", suggested,
        "KiCad PCB (*.kicad_pcb);;All files (*)");
    if (out_path.isEmpty()) return;
    if (QFileInfo(out_path) == QFileInfo(current_board_path_)) {
        QMessageBox::warning(this, "Save Modified Stackup",
            "Destination must differ from the source file.");
        return;
    }
    auto r = pdnkit::save_modified_stackup(
        current_board_path_.toStdString(), out_path.toStdString(), *board_);
    if (!r.ok) {
        QMessageBox::critical(this, "Save Modified Stackup",
            QString::fromStdString(r.error));
        return;
    }
    statusBar()->showMessage(
        QString("Wrote modified stackup to %1 (%2 layer(s) updated)")
            .arg(out_path).arg(r.layers_updated),
        10000);
}

