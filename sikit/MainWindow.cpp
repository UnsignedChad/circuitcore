// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "MainWindow.h"

#include "si/SiStackup.h"

#include <algorithm>

#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QScrollArea>
#include <QFrame>
#include <QStatusBar>
#include <spdlog/spdlog.h>

#include <cmath>
#include <memory>
#include <utility>

#include "EyeWindow.h"
#include "SParamPlotWindow.h"
#include "LayerPanel.h"
#include "PcbCanvas.h"
#include "si/ChannelSynthesis.h"
#include "si/DiffSynth.h"
#include "si/ViaModel.h"
#include "si/TraceImpedance.h"
#include "si/ChannelResponse.h"
#include "si/Eye.h"
#include "si/DiffPair.h"
#include "si/Ibis.h"
#include "si/Ami.h"
#include "circuitcore/formats/kicad/PcbParser.h"
#include "si/EyeMask.h"
#include "si/Touchstone.h"
#include "si/TouchstoneCsv.h"
#include "si/TouchstoneWriter.h"

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("sikit");
    // Allow the window to shrink small; the layer dock wraps its panel in a
    // QScrollArea so its sizeHint doesn't pin a floor on the window size.
    setMinimumSize(480, 320);
    resize(1280, 800);

    canvas_ = new sikit::PcbCanvas(this);
    setCentralWidget(canvas_);

    layer_panel_ = new sikit::LayerPanel(this);
    auto* layers_scroll = new QScrollArea(this);
    layers_scroll->setWidget(layer_panel_);
    layers_scroll->setWidgetResizable(true);
    layers_scroll->setFrameShape(QFrame::NoFrame);
    auto* dock = new QDockWidget("Layers", this);
    dock->setWidget(layers_scroll);
    dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    connect(layer_panel_, &sikit::LayerPanel::visibility_changed,
            canvas_, &sikit::PcbCanvas::setLayerVisibility);

    auto* fileMenu = menuBar()->addMenu("&File");
    auto* openAct = fileMenu->addAction("&Open KiCad PCB...");
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &MainWindow::onOpenKicadPcb);
    auto* openTs = fileMenu->addAction("Open &Touchstone for eye...");
    openTs->setShortcut(QKeySequence("Ctrl+T"));
    connect(openTs, &QAction::triggered, this, &MainWindow::onOpenTouchstoneEye);
    auto* openIbis = fileMenu->addAction("Open &IBIS file...");
    openIbis->setShortcut(QKeySequence("Ctrl+I"));
    connect(openIbis, &QAction::triggered, this, &MainWindow::onOpenIbis);
    auto* openAmi = fileMenu->addAction("Open A&MI model...");
    openAmi->setShortcut(QKeySequence("Ctrl+M"));
    connect(openAmi, &QAction::triggered, this, &MainWindow::onOpenAmi);
    fileMenu->addSeparator();
    auto* openProj = fileMenu->addAction("Open pr&oject...");
    openProj->setShortcut(QKeySequence("Ctrl+Shift+O"));
    connect(openProj, &QAction::triggered, this, &MainWindow::onOpenProject);
    auto* saveProj = fileMenu->addAction("&Save project as...");
    saveProj->setShortcut(QKeySequence::Save);
    connect(saveProj, &QAction::triggered, this, &MainWindow::onSaveProject);
    auto* openSPlot = fileMenu->addAction("&Plot S-parameters from file...");
    openSPlot->setShortcut(QKeySequence("Ctrl+P"));
    connect(openSPlot, &QAction::triggered, this, &MainWindow::onOpenSParamPlot);
    fileMenu->addSeparator();
    auto* exportTs = fileMenu->addAction("Export &net as Touchstone .s2p...");
    exportTs->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(exportTs, &QAction::triggered, this, &MainWindow::onExportNetTouchstone);
    auto* exportCsv = fileMenu->addAction("Export net frequency sweep &CSV...");
    exportCsv->setShortcut(QKeySequence("Ctrl+Shift+C"));
    connect(exportCsv, &QAction::triggered, this, &MainWindow::onExportNetCsv);
    auto* exportS4p = fileMenu->addAction("Export &diff pair as Touchstone .s4p...");
    exportS4p->setShortcut(QKeySequence("Ctrl+Shift+D"));
    connect(exportS4p, &QAction::triggered, this, &MainWindow::onExportDiffPairS4p);
    fileMenu->addSeparator();
    fileMenu->addAction("E&xit", this, &QWidget::close);

    auto* viewMenu = menuBar()->addMenu("&View");
    auto* fitAct = viewMenu->addAction("&Fit to Board");
    fitAct->setShortcut(QKeySequence(Qt::Key_Home));
    connect(fitAct, &QAction::triggered, canvas_, &sikit::PcbCanvas::fitToBoard);
    auto* threeDAct = viewMenu->addAction("&3D mode");
    threeDAct->setShortcut(QKeySequence("Ctrl+D"));
    threeDAct->setCheckable(true);
    connect(threeDAct, &QAction::toggled, canvas_, [this](bool on) {
        canvas_->setViewMode(on ? sikit::PcbCanvas::ViewMode::D3
                                : sikit::PcbCanvas::ViewMode::D2);
    });
    viewMenu->addAction(dock->toggleViewAction());

    auto* analyzeMenu = menuBar()->addMenu("&Analyze");
    auto* z50 = analyzeMenu->addAction("Trace impedance overlay (50 Ω)");
    z50->setShortcut(QKeySequence("Ctrl+1"));
    connect(z50, &QAction::triggered, this,
            [this]() { showImpedanceOverlay(50.0); });
    auto* z90 = analyzeMenu->addAction("Trace impedance overlay (90 Ω, USB-style)");
    z90->setShortcut(QKeySequence("Ctrl+2"));
    connect(z90, &QAction::triggered, this,
            [this]() { showImpedanceOverlay(90.0); });
    auto* z100 = analyzeMenu->addAction("Trace impedance overlay (100 Ω, PCIe/HDMI-style)");
    z100->setShortcut(QKeySequence("Ctrl+3"));
    connect(z100, &QAction::triggered, this,
            [this]() { showImpedanceOverlay(100.0); });
    analyzeMenu->addSeparator();
    auto* zd90 = analyzeMenu->addAction("Diff-pair impedance overlay (90 Ω, USB)");
    zd90->setShortcut(QKeySequence("Ctrl+Shift+2"));
    connect(zd90, &QAction::triggered, this,
            [this]() { showDiffPairOverlay(90.0); });
    auto* zd100 = analyzeMenu->addAction("Diff-pair impedance overlay (100 Ω, PCIe/HDMI)");
    zd100->setShortcut(QKeySequence("Ctrl+Shift+3"));
    connect(zd100, &QAction::triggered, this,
            [this]() { showDiffPairOverlay(100.0); });
    auto* clearAct = analyzeMenu->addAction("&Clear overlay");
    clearAct->setShortcut(QKeySequence("Ctrl+0"));
    connect(clearAct, &QAction::triggered, canvas_, &sikit::PcbCanvas::clearImpedanceOverlay);
    analyzeMenu->addSeparator();
    use_fdm_action_ = analyzeMenu->addAction("Use FDM solver for impedance (slower, more accurate)");
    use_fdm_action_->setCheckable(true);
    use_fdm_action_->setChecked(false);
    analyzeMenu->addSeparator();
    auto* eyeOpen = analyzeMenu->addAction("Eye diagram — clean RC channel (demo)");
    eyeOpen->setShortcut(QKeySequence("Ctrl+E"));
    connect(eyeOpen, &QAction::triggered, this,
            [this]() { showEyeDiagramDemo(/*severe_isi=*/false); });
    auto* eyeISI = analyzeMenu->addAction("Eye diagram — heavy ISI RC channel (demo)");
    eyeISI->setShortcut(QKeySequence("Ctrl+Shift+E"));
    connect(eyeISI, &QAction::triggered, this,
            [this]() { showEyeDiagramDemo(/*severe_isi=*/true); });
    auto* eyeSynth = analyzeMenu->addAction("Eye diagram — &synthesized from trace geometry...");
    eyeSynth->setShortcut(QKeySequence("Ctrl+Y"));
    connect(eyeSynth, &QAction::triggered, this,
            &MainWindow::onSynthesizeEye);
    analyzeMenu->addSeparator();
    auto* plotNetS = analyzeMenu->addAction("Plot S-parameters for &net (synthesised)...");
    plotNetS->setShortcut(QKeySequence("Ctrl+Shift+P"));
    connect(plotNetS, &QAction::triggered, this, &MainWindow::onPlotNetSParam);
    auto* plotDpS = analyzeMenu->addAction("Plot S-parameters for diff pair (synthesised)...");
    connect(plotDpS, &QAction::triggered, this, &MainWindow::onPlotDiffPairSParam);
    auto* plotViaS = analyzeMenu->addAction("Plot S-parameters for via (lumped model)...");
    connect(plotViaS, &QAction::triggered, this, &MainWindow::onPlotViaSParam);

    hover_label_ = new QLabel(this);
    hover_label_->setMinimumWidth(0);
    statusBar()->addPermanentWidget(hover_label_);
    connect(canvas_, &sikit::PcbCanvas::hoverInfo, hover_label_, &QLabel::setText);

    statusBar()->showMessage("Ready");
}

void MainWindow::onOpenKicadPcb() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open KiCad PCB", QString(),
        "KiCad PCB (*.kicad_pcb);;All files (*)");
    if (path.isEmpty()) return;
    loadKicadPcb(path);
}

void MainWindow::onOpenTouchstoneEye() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open Touchstone (.s2p) for eye analysis", QString(),
        "Touchstone 2-port (*.s2p);;All files (*)");
    if (path.isEmpty()) return;

    bool ok = false;
    const double baud_gbps = QInputDialog::getDouble(
        this, "Baud rate", "Bit rate (Gbps):", 1.0, 0.01, 100.0, 2, &ok);
    if (!ok) return;

    sikit::touchstone::TouchstoneFile channel;
    try {
        channel = sikit::touchstone::TouchstoneReader::read_file(path.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Open Touchstone failed", e.what());
        return;
    }
    if (channel.num_ports != 2) {
        QMessageBox::warning(this, "Open Touchstone",
                             QString("Expected 2-port file; got %1 ports.")
                                 .arg(channel.num_ports));
        return;
    }

    // TX waveform sized to give us enough cycles for a stable eye and a
    // sample rate well above the Touchstone's Nyquist so the interpolation
    // is bounded.
    constexpr int kBitCount = 2000;
    constexpr int kSpu = 32;
    const double baud = baud_gbps * 1e9;
    const double fs = baud * kSpu;

    // Use IBIS ramp if a model is loaded; otherwise step NRZ.
    auto bits = sikit::eye::prbs7(kBitCount);
    double ramp_frac = 0.0;
    QString tx_label = "step NRZ";
    if (ibis_file_) {
        for (const auto& m : ibis_file_->models) {
            if (m.name == active_ibis_model_) {
                ramp_frac = sikit::eye::ramp_fraction_from_ibis(m, baud);
                tx_label = QString("IBIS %1 ramp")
                               .arg(QString::fromStdString(m.name));
                break;
            }
        }
    }
    auto tx = (ramp_frac > 0.0)
                  ? sikit::eye::nrz_with_ramp(bits, kSpu, ramp_frac)
                  : sikit::eye::nrz_waveform(bits, kSpu);
    if (ami_model_ && ami_file_) {
        tx_label += QString(" → AMI %1")
                        .arg(QString::fromStdString(ami_file_->model_name));
    }

    std::vector<double> rx;
    try {
        rx = sikit::dsp::apply_channel(tx, fs, channel);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Apply channel failed", e.what());
        return;
    }

    auto eye = sikit::eye::build_eye(rx, kSpu, 128, 96, /*warmup=*/8);
    const auto& mask = sikit::specs::usb20_hs_template1();
    const int violations = sikit::specs::count_violations(eye, mask);

    auto* w = new EyeWindow(this);
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setTitleSubtext(
        QString("%1 · %2 Gbps PRBS-7 · TX: %3")
            .arg(QFileInfo(path).fileName())
            .arg(baud_gbps, 0, 'f', 2)
            .arg(tx_label));
    w->setEye(eye);
    w->setMask(&mask);
    w->show();

    statusBar()->showMessage(
        QString("Eye from %1 @ %2 Gbps: mask=%3 · violations=%4 %5")
            .arg(QFileInfo(path).fileName())
            .arg(baud_gbps, 0, 'f', 2)
            .arg(QString::fromStdString(mask.name))
            .arg(violations)
            .arg(violations == 0 ? "PASS" : "FAIL"));
    spdlog::info("touchstone eye: {} @ {} Gbps, {} freq points, {} violations",
                 path.toStdString(), baud_gbps,
                 channel.frequencies.size(), violations);
}

void MainWindow::showImpedanceOverlay(double target_z0) {
    if (!board_) {
        QMessageBox::information(this, "Impedance overlay",
                                 "Open a KiCad PCB first.");
        return;
    }
    const auto stackup = sikit::analysis::AnalysisStackup::from_board(*board_, si_stackup_);
    const auto engine = use_fdm_action_ && use_fdm_action_->isChecked()
                            ? sikit::analysis::Engine::Fdm
                            : sikit::analysis::Engine::ClosedForm;
    auto results = sikit::analysis::compute_all(*board_, stackup, engine);
    canvas_->setImpedanceOverlay(results, target_z0);

    int on_spec = 0, warn = 0, fail = 0;
    for (const auto& r : results) {
        const double err = std::abs(r.z0 - target_z0) / target_z0;
        if (err < 0.05) ++on_spec;
        else if (err < 0.10) ++warn;
        else ++fail;
    }
    const QString stackup_src = stackup.from_real_stackup
                                    ? "real stackup"
                                    : "default FR-4";
    const QString engine_name =
        (engine == sikit::analysis::Engine::Fdm) ? "FDM" : "closed-form";
    statusBar()->showMessage(
        QString("Impedance @ %1 Ω · %2 · %3: %4 on-spec (<5%), %5 warn (<10%), %6 fail (≥10%)")
            .arg(target_z0, 0, 'f', 0)
            .arg(engine_name)
            .arg(stackup_src)
            .arg(on_spec).arg(warn).arg(fail));
    spdlog::info("impedance overlay target={}Ω engine={} stackup={} εr={:.2f}: "
                 "on-spec={} warn={} fail={}",
                 target_z0, engine_name.toStdString(),
                 stackup.from_real_stackup ? "real" : "default",
                 stackup.epsilon_r,
                 on_spec, warn, fail);
}

void MainWindow::onExportNetTouchstone() {
    if (!board_) {
        QMessageBox::information(this, "Export net Touchstone",
                                 "Open a KiCad PCB first.");
        return;
    }
    auto hs_ids = sikit::highspeed::find_high_speed_nets(*board_);
    if (hs_ids.empty()) {
        QMessageBox::information(this, "Export net Touchstone",
                                 "No high-speed nets detected on this board "
                                 "(no diff-pair suffixes or protocol keywords).");
        return;
    }

    QStringList items;
    for (int nid : hs_ids) {
        if (const auto* n = board_->find_net(nid)) {
            items << QString::fromStdString(n->name);
        }
    }
    bool ok = false;
    const QString choice = QInputDialog::getItem(
        this, "Export net Touchstone", "Net to export:", items, 0, false, &ok);
    if (!ok) return;

    int target_net = -1;
    if (const auto* n = board_->find_net_by_name(choice.toStdString())) {
        target_net = n->id;
    }
    if (target_net < 0) return;

    std::vector<double> widths;
    double total_length = 0.0;
    for (const auto& s : board_->segments) {
        if (s.net_id != target_net) continue;
        if (s.layer_ordinal != 0) continue;
        widths.push_back(s.width);
        const double dx = s.end.x - s.start.x;
        const double dy = s.end.y - s.start.y;
        total_length += std::sqrt(dx * dx + dy * dy);
    }
    if (widths.empty() || total_length <= 0.0) {
        QMessageBox::warning(this, "Export net Touchstone",
                              "Net has no F.Cu segments to model.");
        return;
    }
    std::sort(widths.begin(), widths.end());
    const double trace_width = widths[widths.size() / 2];

    const QString path = QFileDialog::getSaveFileName(
        this, "Save Touchstone", QString("%1.s2p").arg(choice),
        "Touchstone 2-port (*.s2p)");
    if (path.isEmpty()) return;

    sikit::analysis::ChannelSpec spec;
    spec.trace_width = trace_width;
    spec.layer_ordinal = 0;
    spec.length_m = total_length;
    spec.stackup = sikit::analysis::AnalysisStackup::from_board(*board_, si_stackup_);
    spec.engine = (use_fdm_action_ && use_fdm_action_->isChecked())
                      ? sikit::analysis::Engine::Fdm
                      : sikit::analysis::Engine::ClosedForm;

    // Standard 200-point linear sweep from 10 MHz to 20 GHz — wide enough
    // to cover any protocol up to USB4 / PCIe Gen6 fundamentals.
    std::vector<double> freqs;
    freqs.reserve(200);
    const double f_lo = 10e6, f_hi = 20e9;
    for (int i = 0; i < 200; ++i) {
        const double t = static_cast<double>(i) / 199.0;
        freqs.push_back(f_lo + t * (f_hi - f_lo));
    }

    sikit::touchstone::TouchstoneFile ts;
    try {
        ts = sikit::analysis::synthesize_channel(spec, freqs, 50.0);
        sikit::touchstone::TouchstoneWriter::write_file(ts, path.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Export failed", e.what());
        return;
    }

    statusBar()->showMessage(
        QString("Exported %1 → %2 (%3 pts, W=%4mm, L=%5mm)")
            .arg(choice)
            .arg(QFileInfo(path).fileName())
            .arg(freqs.size())
            .arg(trace_width * 1e3, 0, 'f', 3)
            .arg(total_length * 1e3, 0, 'f', 1));
    spdlog::info("exported {} to {} (W={:.3f}mm L={:.1f}mm)",
                 choice.toStdString(), path.toStdString(),
                 trace_width * 1e3, total_length * 1e3);
}

void MainWindow::onExportDiffPairS4p() {
    if (!board_) {
        QMessageBox::information(this, "Export diff-pair S4P",
                                 "Open a KiCad PCB first.");
        return;
    }
    auto pairs = sikit::highspeed::find_diff_pairs(*board_);
    if (pairs.empty()) {
        QMessageBox::information(this, "Export diff-pair S4P",
                                 "No diff pairs detected on this board.");
        return;
    }

    QStringList items;
    for (const auto& dp : pairs) {
        items << QString::fromStdString(dp.base_name);
    }
    bool ok = false;
    const QString choice = QInputDialog::getItem(
        this, "Pick diff pair", "Diff pair to export:", items, 0, false, &ok);
    if (!ok) return;

    // Look up the chosen pair to get its segment widths + spacing.
    const sikit::highspeed::DiffPair* picked = nullptr;
    for (const auto& dp : pairs) {
        if (QString::fromStdString(dp.base_name) == choice) {
            picked = &dp;
            break;
        }
    }
    if (!picked) return;

    // Derive median width across F.Cu segments on either net, and use
    // the geometry-derived spacing from compute_diff_pairs (which already
    // does the parallel-overlap analysis).
    sikit::analysis::AnalysisStackup stackup =
        sikit::analysis::AnalysisStackup::from_board(*board_, si_stackup_);
    auto dpz = sikit::analysis::compute_diff_pairs(
        *board_, stackup, sikit::analysis::Engine::ClosedForm);
    sikit::analysis::DiffPairImpedance dp_meta;
    bool found = false;
    for (const auto& d : dpz) {
        if (d.base_name == picked->base_name) {
            dp_meta = d;
            found = true;
            break;
        }
    }
    if (!found || dp_meta.trace_width <= 0.0) {
        QMessageBox::warning(this, "Export diff-pair S4P",
                              "Diff pair has no F.Cu geometry to model.");
        return;
    }
    // Sum the segment lengths of both nets on F.Cu for total channel length.
    double total_length = 0.0;
    for (const auto& s : board_->segments) {
        if (s.layer_ordinal != 0) continue;
        if (s.net_id != picked->net_p_id && s.net_id != picked->net_n_id) continue;
        const double dx = s.end.x - s.start.x;
        const double dy = s.end.y - s.start.y;
        total_length += std::sqrt(dx * dx + dy * dy);
    }
    // Divide by 2 since we summed BOTH legs of the pair.
    total_length *= 0.5;
    if (total_length <= 0.0) {
        QMessageBox::warning(this, "Export diff-pair S4P",
                              "Diff pair has no F.Cu length to model.");
        return;
    }

    const QString path = QFileDialog::getSaveFileName(
        this, "Save diff-pair Touchstone", QString("%1.s4p").arg(choice),
        "Touchstone 4-port (*.s4p)");
    if (path.isEmpty()) return;

    sikit::analysis::DiffChannelSpec spec;
    spec.trace_width = dp_meta.trace_width;
    spec.spacing     = dp_meta.spacing;
    spec.layer_ordinal = 0;
    spec.length_m  = total_length;
    spec.stackup   = stackup;

    std::vector<double> freqs;
    freqs.reserve(200);
    const double f_lo = 10e6, f_hi = 20e9;
    for (int i = 0; i < 200; ++i) {
        const double t = static_cast<double>(i) / 199.0;
        freqs.push_back(f_lo + t * (f_hi - f_lo));
    }

    try {
        auto ts = sikit::analysis::synthesize_diff_channel(spec, freqs, 50.0);
        sikit::touchstone::TouchstoneWriter::write_file(ts, path.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Export failed", e.what());
        return;
    }

    statusBar()->showMessage(
        QString("Exported %1 (4-port) → %2  ·  W=%3mm  S=%4mm  L=%5mm")
            .arg(choice)
            .arg(QFileInfo(path).fileName())
            .arg(dp_meta.trace_width * 1e3, 0, 'f', 3)
            .arg(dp_meta.spacing     * 1e3, 0, 'f', 3)
            .arg(total_length        * 1e3, 0, 'f', 1));
    spdlog::info("exported diff pair {} → {} (W={:.3f}mm S={:.3f}mm L={:.1f}mm)",
                 choice.toStdString(), path.toStdString(),
                 dp_meta.trace_width * 1e3, dp_meta.spacing * 1e3,
                 total_length * 1e3);
}

void MainWindow::onOpenIbis() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open IBIS file", QString(),
        "IBIS (*.ibs);;All files (*)");
    if (path.isEmpty()) return;

    sikit::ibis::IbisFile f;
    try {
        f = sikit::ibis::IbisReader::read_file(path.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Open IBIS failed", e.what());
        return;
    }
    if (f.models.empty()) {
        QMessageBox::warning(this, "Open IBIS",
                              "IBIS file contains no [Model] sections.");
        return;
    }

    QStringList items;
    for (const auto& m : f.models) items << QString::fromStdString(m.name);

    bool ok = false;
    const QString choice = (items.size() == 1)
        ? items.first()
        : QInputDialog::getItem(this, "Pick IBIS model",
                                  "Select the buffer model to drive future eyes:",
                                  items, 0, false, &ok);
    if (items.size() > 1 && !ok) return;

    ibis_file_ = std::move(f);
    active_ibis_model_ = choice.toStdString();
    ibis_source_path_ = path;

    statusBar()->showMessage(
        QString("IBIS loaded: %1 — model %2 active. Eye diagrams will use this buffer.")
            .arg(QFileInfo(path).fileName())
            .arg(choice));
    spdlog::info("ibis: loaded {} with model {} active",
                 path.toStdString(), active_ibis_model_);
}

void MainWindow::onExportNetCsv() {
    if (!board_) {
        QMessageBox::information(this, "Export net CSV",
                                 "Open a KiCad PCB first.");
        return;
    }
    auto hs_ids = sikit::highspeed::find_high_speed_nets(*board_);
    if (hs_ids.empty()) {
        QMessageBox::information(this, "Export net CSV",
                                 "No high-speed nets detected on this board.");
        return;
    }

    QStringList items;
    for (int nid : hs_ids) {
        if (const auto* n = board_->find_net(nid)) {
            items << QString::fromStdString(n->name);
        }
    }
    bool ok = false;
    const QString choice = QInputDialog::getItem(
        this, "Export net CSV", "Net to export:", items, 0, false, &ok);
    if (!ok) return;

    int target_net = -1;
    if (const auto* n = board_->find_net_by_name(choice.toStdString())) {
        target_net = n->id;
    }
    if (target_net < 0) return;

    std::vector<double> widths;
    double total_length = 0.0;
    for (const auto& s : board_->segments) {
        if (s.net_id != target_net) continue;
        if (s.layer_ordinal != 0) continue;
        widths.push_back(s.width);
        const double dx = s.end.x - s.start.x;
        const double dy = s.end.y - s.start.y;
        total_length += std::sqrt(dx * dx + dy * dy);
    }
    if (widths.empty() || total_length <= 0.0) {
        QMessageBox::warning(this, "Export net CSV",
                              "Net has no F.Cu segments to model.");
        return;
    }
    std::sort(widths.begin(), widths.end());
    const double trace_width = widths[widths.size() / 2];

    const QString path = QFileDialog::getSaveFileName(
        this, "Save frequency-sweep CSV", QString("%1.csv").arg(choice),
        "CSV (*.csv)");
    if (path.isEmpty()) return;

    sikit::analysis::ChannelSpec spec;
    spec.trace_width = trace_width;
    spec.layer_ordinal = 0;
    spec.length_m = total_length;
    spec.stackup = sikit::analysis::AnalysisStackup::from_board(*board_, si_stackup_);
    spec.engine = (use_fdm_action_ && use_fdm_action_->isChecked())
                      ? sikit::analysis::Engine::Fdm
                      : sikit::analysis::Engine::ClosedForm;

    std::vector<double> freqs;
    freqs.reserve(200);
    const double f_lo = 10e6, f_hi = 20e9;
    for (int i = 0; i < 200; ++i) {
        const double t = static_cast<double>(i) / 199.0;
        freqs.push_back(f_lo + t * (f_hi - f_lo));
    }

    try {
        auto ts = sikit::analysis::synthesize_channel(spec, freqs, 50.0);
        sikit::touchstone::TouchstoneCsv::write_file(ts, path.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Export failed", e.what());
        return;
    }

    statusBar()->showMessage(
        QString("Exported %1 frequency sweep → %2 (200 pts, W=%3mm, L=%4mm)")
            .arg(choice)
            .arg(QFileInfo(path).fileName())
            .arg(trace_width * 1e3, 0, 'f', 3)
            .arg(total_length * 1e3, 0, 'f', 1));
    spdlog::info("exported {} CSV to {}",
                 choice.toStdString(), path.toStdString());
}

void MainWindow::onSynthesizeEye() {
    bool ok = false;
    double width_mm = 0.20;
    double length_mm = 50.0;
    QString net_label;  // for the title bar

    // If a board is loaded, offer to pre-fill from a high-speed net.
    if (board_) {
        auto hs_ids = sikit::highspeed::find_high_speed_nets(*board_);
        if (!hs_ids.empty()) {
            QStringList items;
            items << "(enter width/length manually)";
            for (int nid : hs_ids) {
                if (const auto* n = board_->find_net(nid)) {
                    items << QString::fromStdString(n->name);
                }
            }
            const QString choice = QInputDialog::getItem(
                this, "Synthesize eye",
                "Pick a high-speed net (or enter manually):",
                items, 0, /*editable=*/false, &ok);
            if (!ok) return;

            if (choice != items.first()) {
                // Find the selected net id by name.
                int target_net = -1;
                if (const auto* n = board_->find_net_by_name(choice.toStdString())) {
                    target_net = n->id;
                }
                if (target_net >= 0) {
                    // Compute median trace width and total length on F.Cu.
                    std::vector<double> widths;
                    double total_length = 0.0;
                    for (const auto& s : board_->segments) {
                        if (s.net_id != target_net) continue;
                        if (s.layer_ordinal != 0) continue;  // F.Cu only for v0
                        widths.push_back(s.width);
                        const double dx = s.end.x - s.start.x;
                        const double dy = s.end.y - s.start.y;
                        total_length += std::sqrt(dx * dx + dy * dy);
                    }
                    if (!widths.empty()) {
                        std::sort(widths.begin(), widths.end());
                        width_mm = widths[widths.size() / 2] * 1e3;
                    }
                    if (total_length > 0.0) length_mm = total_length * 1e3;
                    net_label = choice;
                }
            }
        }
    }

    width_mm = QInputDialog::getDouble(
        this, "Synthesize eye", "Trace width (mm):",
        width_mm, 0.05, 5.0, 3, &ok);
    if (!ok) return;
    length_mm = QInputDialog::getDouble(
        this, "Synthesize eye", "Trace length (mm):",
        length_mm, 1.0, 5000.0, 1, &ok);
    if (!ok) return;
    const double baud_gbps = QInputDialog::getDouble(
        this, "Synthesize eye", "Bit rate (Gbps):",
        1.0, 0.01, 100.0, 2, &ok);
    if (!ok) return;

    // Use the loaded board's stackup if available, else generic FR-4.
    sikit::analysis::AnalysisStackup stackup =
        board_ ? sikit::analysis::AnalysisStackup::from_board(*board_, si_stackup_)
               : sikit::analysis::AnalysisStackup{};

    sikit::analysis::ChannelSpec spec;
    spec.trace_width = width_mm * 1e-3;
    spec.layer_ordinal = 0;            // F.Cu microstrip by default
    spec.length_m = length_mm * 1e-3;
    spec.stackup = stackup;
    spec.engine = (use_fdm_action_ && use_fdm_action_->isChecked())
                      ? sikit::analysis::Engine::Fdm
                      : sikit::analysis::Engine::ClosedForm;

    // Frequency grid wide enough to cover the TX signal's spectrum.
    // PRBS-7 NRZ at B Gbps has significant content up to ~5B.
    constexpr int kBitCount = 2000;
    constexpr int kSpu = 32;
    const double baud = baud_gbps * 1e9;
    const double fs = baud * kSpu;
    std::vector<double> freqs;
    const double f_step = baud / 50.0;
    const double f_max = fs / 2.0;       // up to Nyquist of the TX waveform
    for (double f = f_step; f <= f_max; f += f_step) freqs.push_back(f);

    sikit::touchstone::TouchstoneFile channel;
    try {
        channel = sikit::analysis::synthesize_channel(spec, freqs, 50.0);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Synthesize eye failed", e.what());
        return;
    }

    auto bits = sikit::eye::prbs7(kBitCount);
    double ramp_frac = 0.0;
    QString tx_label = "NRZ";
    if (ibis_file_) {
        for (const auto& m : ibis_file_->models) {
            if (m.name == active_ibis_model_) {
                ramp_frac = sikit::eye::ramp_fraction_from_ibis(m, baud);
                tx_label = QString("IBIS %1 ramp")
                               .arg(QString::fromStdString(m.name));
                break;
            }
        }
    }
    auto tx = (ramp_frac > 0.0)
                  ? sikit::eye::nrz_with_ramp(bits, kSpu, ramp_frac)
                  : sikit::eye::nrz_waveform(bits, kSpu);
    std::vector<double> rx;
    try {
        rx = sikit::dsp::apply_channel(tx, fs, channel);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Apply synthesized channel failed", e.what());
        return;
    }
    // Apply RX-side AMI equalization, if a model has been loaded.
    const double bit_time = 1.0 / baud;
    applyAmiIfLoaded(channel, fs, bit_time, rx);
    auto eye = sikit::eye::build_eye(rx, kSpu, 128, 96, /*warmup=*/8);
    const auto& mask = sikit::specs::usb20_hs_template1();

    // Pull Z0 / v_phase for the caption.
    auto imp = sikit::analysis::compute_one(spec.trace_width, spec.layer_ordinal,
                                             spec.stackup);

    auto* w = new EyeWindow(this);
    w->setAttribute(Qt::WA_DeleteOnClose);
    const QString head = net_label.isEmpty()
                             ? QString("synthesized trace")
                             : QString("net %1").arg(net_label);
    w->setTitleSubtext(
        QString("%1 · W=%2mm  L=%3mm  Z₀=%4Ω  %5 Gbps · TX: %6")
            .arg(head)
            .arg(width_mm, 0, 'f', 3).arg(length_mm, 0, 'f', 1)
            .arg(imp.z0, 0, 'f', 1).arg(baud_gbps, 0, 'f', 2)
            .arg(tx_label));
    w->setEye(eye);
    w->setMask(&mask);
    w->show();

    const int violations = sikit::specs::count_violations(eye, mask);
    statusBar()->showMessage(
        QString("Synthesized eye: %1mm × %2mm trace · Z₀=%3Ω · %4 Gbps · violations=%5 %6")
            .arg(width_mm, 0, 'f', 3).arg(length_mm, 0, 'f', 1)
            .arg(imp.z0, 0, 'f', 1).arg(baud_gbps, 0, 'f', 2)
            .arg(violations)
            .arg(violations == 0 ? "PASS" : "FAIL"));
    spdlog::info("synthesized eye W={:.3f}mm L={:.1f}mm Z0={:.1f}Ω "
                 "{} Gbps {} violations",
                 width_mm, length_mm, imp.z0, baud_gbps, violations);
}

void MainWindow::showDiffPairOverlay(double target_z_diff) {
    if (!board_) {
        QMessageBox::information(this, "Diff-pair overlay",
                                 "Open a KiCad PCB first.");
        return;
    }
    const auto stackup = sikit::analysis::AnalysisStackup::from_board(*board_, si_stackup_);
    const auto engine = use_fdm_action_ && use_fdm_action_->isChecked()
                            ? sikit::analysis::Engine::Fdm
                            : sikit::analysis::Engine::ClosedForm;
    auto pairs = sikit::analysis::compute_diff_pairs(*board_, stackup, engine);

    if (pairs.empty()) {
        canvas_->clearImpedanceOverlay();
        statusBar()->showMessage(
            "No diff pairs detected (no nets with matching _P/_N / +/- suffixes)");
        return;
    }

    // Flatten into per-segment results so we can reuse setImpedanceOverlay's
    // colour-by-error renderer. Each segment of every pair gets the pair's
    // Z_diff value as its "z0".
    std::vector<sikit::analysis::SegmentImpedance> rs;
    int on_spec = 0, warn = 0, fail = 0;
    for (const auto& dp : pairs) {
        const double err = (target_z_diff > 0.0 && dp.z_diff > 0.0)
                               ? std::abs(dp.z_diff - target_z_diff) / target_z_diff
                               : 1.0;
        if (err < 0.05) ++on_spec;
        else if (err < 0.10) ++warn;
        else ++fail;
        for (auto idx : dp.segment_indices) {
            if (idx >= board_->segments.size()) continue;
            const auto& seg = board_->segments[idx];
            sikit::analysis::SegmentImpedance r;
            r.segment_index = idx;
            r.layer_ordinal = seg.layer_ordinal;
            r.net_id = seg.net_id;
            r.trace_width = seg.width;
            r.z0 = dp.z_diff;
            rs.push_back(r);
        }
    }
    canvas_->setImpedanceOverlay(rs, target_z_diff);

    const QString engine_name =
        (engine == sikit::analysis::Engine::Fdm) ? "FDM" : "closed-form";
    statusBar()->showMessage(
        QString("Diff-pair Z @ %1 Ω · %2: %3 pairs (%4 on-spec, %5 warn, %6 fail)")
            .arg(target_z_diff, 0, 'f', 0)
            .arg(engine_name)
            .arg(pairs.size())
            .arg(on_spec).arg(warn).arg(fail));
    spdlog::info("diff-pair overlay target={}Ω engine={} pairs={} on-spec={} warn={} fail={}",
                 target_z_diff, engine_name.toStdString(), pairs.size(),
                 on_spec, warn, fail);
}

void MainWindow::showEyeDiagramDemo(bool severe_isi) {
    constexpr int kBitCount = 2000;
    constexpr int kSpu = 32;
    constexpr double kBaud = 1.0e9;
    const double dt = 1.0 / (kBaud * kSpu);
    const double fc = severe_isi ? kBaud / 3.0 : kBaud * 2.0;

    auto bits = sikit::eye::prbs7(kBitCount);
    double ramp_frac = 0.0;
    QString tx_label = "NRZ";
    if (ibis_file_) {
        for (const auto& m : ibis_file_->models) {
            if (m.name == active_ibis_model_) {
                ramp_frac = sikit::eye::ramp_fraction_from_ibis(m, kBaud);
                tx_label = QString("IBIS %1 ramp")
                               .arg(QString::fromStdString(m.name));
                break;
            }
        }
    }
    auto tx = (ramp_frac > 0.0)
                  ? sikit::eye::nrz_with_ramp(bits, kSpu, ramp_frac)
                  : sikit::eye::nrz_waveform(bits, kSpu);
    auto rx = sikit::eye::rc_lowpass(tx, dt, fc);
    auto eye = sikit::eye::build_eye(rx, kSpu, 128, 96, /*warmup=*/8);

    const auto& mask = sikit::specs::usb20_hs_template1();

    auto* w = new EyeWindow(this);
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setTitleSubtext(
        QString("PRBS-7 · %1 Gbps %4 · RC channel fc=%2 %3")
            .arg(kBaud / 1e9, 0, 'f', 1)
            .arg(fc / 1e6, 0, 'f', 0)
            .arg(severe_isi ? "MHz (heavy ISI)" : "MHz (clean)")
            .arg(tx_label));
    w->setEye(eye);
    w->setMask(&mask);
    w->show();

    const int violations = sikit::specs::count_violations(eye, mask);
    statusBar()->showMessage(
        QString("Eye: %1 bits, %2 samples/UI, fc=%3 MHz · mask=%4 · violations=%5 %6")
            .arg(kBitCount).arg(kSpu).arg(fc / 1e6, 0, 'f', 0)
            .arg(QString::fromStdString(mask.name))
            .arg(violations)
            .arg(violations == 0 ? "PASS" : "FAIL"));
}

void MainWindow::populateLayerPanel() {
    if (!board_) {
        layer_panel_->setLayers({});
        return;
    }
    std::vector<sikit::LayerPanel::Entry> entries;
    for (const auto& L : board_->stackup.layers) {
        if (!L.is_copper()) continue;
        entries.push_back({L.ordinal, QString::fromStdString(L.name)});
    }
    layer_panel_->setLayers(entries);
}

bool MainWindow::loadKicadPcb(const QString& path) {
    auto result = circuitcore::formats::kicad::PcbParser::parse_file(path.toStdString());
    if (!result) {
        const auto err = result.error().format();
        QMessageBox::critical(this, "Open KiCad PCB failed",
                              QString::fromStdString(err));
        spdlog::error("failed to load {}: {}", path.toStdString(), err);
        return false;
    }
    auto board = std::make_unique<circuitcore::board::Board>(std::move(*result));

    // Load the SI-specific dielectric items list separately. Missing
    // stackup section just means we'll fall back to generic FR-4 in
    // AnalysisStackup::from_board.
    if (auto sis = sikit::si::load_si_stackup(path.toStdString()); sis) {
        si_stackup_ = std::move(*sis);
    } else {
        si_stackup_ = {};
        spdlog::warn("si_stackup load: {}", sis.error().message);
    }

    try {
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
        canvas_->setSiStackup(&si_stackup_);
        populateLayerPanel();
        current_pcb_path_ = path;

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
        setWindowTitle(QString("sikit — %1").arg(QFileInfo(path).fileName()));
        return true;
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Open KiCad PCB failed", e.what());
        spdlog::error("failed to load {}: {}", path.toStdString(), e.what());
        return false;
    }
}


// ---------- S-parameter plot slots (Tier 1.1) ---------------------------

void MainWindow::onOpenSParamPlot() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open Touchstone file", QString(),
        "Touchstone (*.s1p *.s2p *.s4p *.s8p);;All files (*)");
    if (path.isEmpty()) return;
    sikit::touchstone::TouchstoneFile ts;
    try {
        ts = sikit::touchstone::TouchstoneReader::read_file(path.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Plot S-parameters", e.what());
        return;
    }
    auto* w = new SParamPlotWindow(this);
    w->setWindowFlag(Qt::Window);
    w->setData(ts);
    if (ts.num_ports == 4) {
        // We do not know the port-order convention of an arbitrary
        // .s4p file; assume the more common [P1, N1, P2, N2] (PNPN).
        // Users with PPNN files can re-synthesise via the Analyze menu.
        w->setMixedModeAvailable(
            SParamPlotWindow::MixedModeAvailability::PortOrderPNPN);
    }
    w->setTitleSubtext(QFileInfo(path).fileName());
    w->show();
}

void MainWindow::onPlotNetSParam() {
    if (!board_) {
        QMessageBox::information(this, "Plot net S-parameters",
                                 "Open a KiCad PCB first.");
        return;
    }
    auto hs_ids = sikit::highspeed::find_high_speed_nets(*board_);
    if (hs_ids.empty()) {
        QMessageBox::information(this, "Plot net S-parameters",
                                 "No high-speed nets detected.");
        return;
    }
    QStringList items;
    for (int nid : hs_ids) {
        if (const auto* n = board_->find_net(nid)) {
            items << QString::fromStdString(n->name);
        }
    }
    bool ok = false;
    const QString choice = QInputDialog::getItem(
        this, "Plot net S-parameters", "Net:", items, 0, false, &ok);
    if (!ok) return;

    int target_net = -1;
    if (const auto* n = board_->find_net_by_name(choice.toStdString())) {
        target_net = n->id;
    }
    if (target_net < 0) return;

    std::vector<double> widths;
    double total_length = 0.0;
    for (const auto& seg : board_->segments) {
        if (seg.net_id != target_net) continue;
        if (seg.layer_ordinal != 0) continue;
        widths.push_back(seg.width);
        const double dx = seg.end.x - seg.start.x;
        const double dy = seg.end.y - seg.start.y;
        total_length += std::sqrt(dx * dx + dy * dy);
    }
    if (widths.empty() || total_length <= 0.0) {
        QMessageBox::warning(this, "Plot net S-parameters",
                              "Net has no F.Cu segments to model.");
        return;
    }
    std::sort(widths.begin(), widths.end());
    const double trace_width = widths[widths.size() / 2];

    sikit::analysis::ChannelSpec spec;
    spec.trace_width = trace_width;
    spec.layer_ordinal = 0;
    spec.length_m = total_length;
    spec.stackup = sikit::analysis::AnalysisStackup::from_board(*board_, si_stackup_);
    spec.engine = (use_fdm_action_ && use_fdm_action_->isChecked())
                      ? sikit::analysis::Engine::Fdm
                      : sikit::analysis::Engine::ClosedForm;

    std::vector<double> freqs;
    freqs.reserve(200);
    const double f_lo = 10e6, f_hi = 20e9;
    for (int i = 0; i < 200; ++i) {
        const double t = static_cast<double>(i) / 199.0;
        freqs.push_back(f_lo + t * (f_hi - f_lo));
    }
    sikit::touchstone::TouchstoneFile ts;
    try {
        ts = sikit::analysis::synthesize_channel(spec, freqs, 50.0);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Plot net S-parameters", e.what());
        return;
    }

    auto* w = new SParamPlotWindow(this);
    w->setWindowFlag(Qt::Window);
    w->setData(ts);
    w->setTitleSubtext(QString("%1 (synthesised, W=%2mm L=%3mm)")
                           .arg(choice)
                           .arg(trace_width * 1e3, 0, 'f', 3)
                           .arg(total_length * 1e3, 0, 'f', 1));
    w->show();
}

void MainWindow::onPlotDiffPairSParam() {
    if (!board_) {
        QMessageBox::information(this, "Plot diff-pair S-parameters",
                                 "Open a KiCad PCB first.");
        return;
    }
    auto pairs = sikit::highspeed::find_diff_pairs(*board_);
    if (pairs.empty()) {
        QMessageBox::information(this, "Plot diff-pair S-parameters",
                                 "No diff pairs detected on this board.");
        return;
    }
    QStringList items;
    for (const auto& dp : pairs) {
        items << QString::fromStdString(dp.base_name);
    }
    bool ok = false;
    const QString choice = QInputDialog::getItem(
        this, "Plot diff-pair S-parameters", "Diff pair:", items, 0, false, &ok);
    if (!ok) return;

    const sikit::highspeed::DiffPair* picked = nullptr;
    for (const auto& dp : pairs) {
        if (QString::fromStdString(dp.base_name) == choice) { picked = &dp; break; }
    }
    if (!picked) return;

    sikit::analysis::AnalysisStackup stackup =
        sikit::analysis::AnalysisStackup::from_board(*board_, si_stackup_);
    auto dpz = sikit::analysis::compute_diff_pairs(
        *board_, stackup, sikit::analysis::Engine::ClosedForm);
    sikit::analysis::DiffPairImpedance meta;
    bool found = false;
    for (const auto& d : dpz) {
        if (d.base_name == picked->base_name) { meta = d; found = true; break; }
    }
    if (!found || meta.trace_width <= 0.0) {
        QMessageBox::warning(this, "Plot diff-pair S-parameters",
                              "Diff pair has no F.Cu geometry to model.");
        return;
    }
    double total_length = 0.0;
    for (const auto& seg : board_->segments) {
        if (seg.layer_ordinal != 0) continue;
        if (seg.net_id != picked->net_p_id && seg.net_id != picked->net_n_id) continue;
        const double dx = seg.end.x - seg.start.x;
        const double dy = seg.end.y - seg.start.y;
        total_length += std::sqrt(dx * dx + dy * dy);
    }
    total_length *= 0.5;
    if (total_length <= 0.0) {
        QMessageBox::warning(this, "Plot diff-pair S-parameters",
                              "Diff pair has no F.Cu length to model.");
        return;
    }

    sikit::analysis::DiffChannelSpec spec;
    spec.trace_width = meta.trace_width;
    spec.spacing     = meta.spacing;
    spec.layer_ordinal = 0;
    spec.length_m  = total_length;
    spec.stackup   = stackup;

    std::vector<double> freqs;
    freqs.reserve(200);
    const double f_lo = 10e6, f_hi = 20e9;
    for (int i = 0; i < 200; ++i) {
        const double t = static_cast<double>(i) / 199.0;
        freqs.push_back(f_lo + t * (f_hi - f_lo));
    }

    sikit::touchstone::TouchstoneFile ts;
    try {
        ts = sikit::analysis::synthesize_diff_channel(spec, freqs, 50.0);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Plot diff-pair S-parameters", e.what());
        return;
    }

    auto* w = new SParamPlotWindow(this);
    w->setWindowFlag(Qt::Window);
    w->setData(ts);
    // DiffSynth emits port order [P1, P2, N1, N2] -- PPNN.
    w->setMixedModeAvailable(
        SParamPlotWindow::MixedModeAvailability::PortOrderPPNN);
    w->setTitleSubtext(QString("%1 (diff, W=%2mm S=%3mm L=%4mm)")
                           .arg(choice)
                           .arg(meta.trace_width * 1e3, 0, 'f', 3)
                           .arg(meta.spacing     * 1e3, 0, 'f', 3)
                           .arg(total_length     * 1e3, 0, 'f', 1));
    w->show();
}


// ---------- AMI (Tier 1.4) ----------------------------------------------

void MainWindow::onOpenAmi() {
    // Step 1: pick the .ami parameter file.
    const QString ami_path = QFileDialog::getOpenFileName(
        this, "Open IBIS-AMI parameter file (.ami)", QString(),
        "AMI parameter (*.ami);;All files (*)");
    if (ami_path.isEmpty()) return;

    sikit::ibis::ami::AmiFile f;
    try {
        f = sikit::ibis::ami::AmiParser::read_file(ami_path.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Open AMI parameter file failed", e.what());
        return;
    }

    // Step 2: pick the matching shared library. Default the suggested
    // filename to the same stem as the .ami so users with vendor packages
    // (where the two live next to each other) get a one-click pick.
    const QString stem = QFileInfo(ami_path).completeBaseName();
    const QString lib_dir = QFileInfo(ami_path).absolutePath();
    const QString lib_path = QFileDialog::getOpenFileName(
        this, "Open IBIS-AMI library (.so / .dll / .dylib)",
        lib_dir + "/" + stem,
        "AMI library (*.so *.dll *.dylib);;All files (*)");
    if (lib_path.isEmpty()) return;

    std::unique_ptr<sikit::ibis::ami::AmiModel> mdl;
    try {
        mdl = std::make_unique<sikit::ibis::ami::AmiModel>(
            std::filesystem::path(lib_path.toStdString()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Load AMI library failed", e.what());
        return;
    }
    if (!mdl->init_available()) {
        QMessageBox::warning(this, "Open AMI",
                              "Loaded library does not export AMI_Init. "
                              "AMI integration disabled.");
        return;
    }

    ami_file_  = std::move(f);
    ami_model_ = std::move(mdl);
    ami_params_path_  = ami_path;
    ami_library_path_ = lib_path;
    statusBar()->showMessage(
        QString("AMI loaded: %1 + %2 — eye pipeline will apply this model as RX.")
            .arg(QFileInfo(ami_path).fileName())
            .arg(QFileInfo(lib_path).fileName()));
    spdlog::info("ami: loaded params={} lib={} model={}",
                 ami_path.toStdString(), lib_path.toStdString(),
                 ami_file_->model_name);
}

void MainWindow::applyAmiIfLoaded(
    const sikit::touchstone::TouchstoneFile& channel,
    double sample_rate_hz, double bit_time_s,
    std::vector<double>& wave) {
    if (!ami_model_ || !ami_model_->init_available() || wave.empty()) return;

    // Build the channel impulse response by passing a unit delta through the
    // same apply_channel path the waveform took. Cap to 1024 samples — vendor
    // models typically choke on very long impulses, and the post-channel UI
    // is already band-limited to a few ns of memory.
    const std::size_t row_size = std::min<std::size_t>(wave.size(), 1024);
    std::vector<double> impulse(row_size, 0.0);
    impulse[0] = 1.0;
    std::vector<double> h;
    try {
        h = sikit::dsp::apply_channel(impulse, sample_rate_hz, channel);
    } catch (...) {
        return;
    }
    if (h.size() < row_size) {
        // Pad if the channel returned fewer samples than asked.
        h.resize(row_size, 0.0);
    }
    std::vector<double> impulse_matrix(h.begin(), h.begin() + row_size);

    const double dt = 1.0 / sample_rate_hz;
    const std::string params_in = "(" + (ami_file_ ? ami_file_->model_name
                                                   : std::string("model")) + ")";
    try {
        auto ir = ami_model_->init(impulse_matrix,
                                   static_cast<long>(row_size), /*aggressors=*/0,
                                   dt, bit_time_s, params_in);
        if (ir.return_code != 1) {
            spdlog::warn("AMI_Init returned {} — skipping equalization",
                         ir.return_code);
            return;
        }
    } catch (const std::exception& e) {
        spdlog::warn("AMI_Init threw: {}", e.what());
        return;
    }

    if (!ami_model_->has_get_wave()) return;

    std::vector<double> clock_times;
    try {
        auto gr = ami_model_->get_wave(wave, clock_times);
        if (gr.return_code != 1) {
            spdlog::warn("AMI_GetWave returned {}", gr.return_code);
        }
    } catch (const std::exception& e) {
        spdlog::warn("AMI_GetWave threw: {}", e.what());
    }
}


// ---------- Project file (Tier 1.6) -------------------------------------

void MainWindow::onSaveProject() {
    const QString suggested = current_project_path_.isEmpty()
        ? (current_pcb_path_.isEmpty()
               ? QString("untitled.sikitproj")
               : QFileInfo(current_pcb_path_).baseName() + ".sikitproj")
        : current_project_path_;
    const QString path = QFileDialog::getSaveFileName(
        this, "Save sikit project", suggested,
        "sikit project (*.sikitproj);;All files (*)");
    if (path.isEmpty()) return;

    sikit::project::Project p;
    p.kicad_pcb = current_pcb_path_.toStdString();
    if (ibis_file_) {
        p.ibis = sikit::project::IbisRef{
            ibis_source_path_.toStdString(),
            active_ibis_model_};
    }
    if (ami_file_) {
        p.ami = sikit::project::AmiRef{
            ami_params_path_.toStdString(),
            ami_library_path_.toStdString()};
    }
    p.use_fdm = use_fdm_action_ && use_fdm_action_->isChecked();

    try {
        sikit::project::save_project(p, path.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Save project failed", e.what());
        return;
    }
    current_project_path_ = path;
    statusBar()->showMessage(QString("Project saved → %1")
                                 .arg(QFileInfo(path).fileName()));
    spdlog::info("project: saved to {}", path.toStdString());
}

void MainWindow::onOpenProject() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open sikit project", QString(),
        "sikit project (*.sikitproj);;All files (*)");
    if (path.isEmpty()) return;

    sikit::project::Project p;
    try {
        p = sikit::project::load_project(path.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Open project failed", e.what());
        return;
    }
    current_project_path_ = path;

    // 1) PCB
    if (!p.kicad_pcb.empty()) {
        loadKicadPcb(QString::fromStdString(p.kicad_pcb));
    }

    // 2) IBIS
    if (p.ibis) {
        try {
            ibis_file_ = sikit::ibis::IbisReader::read_file(p.ibis->file);
            active_ibis_model_ = p.ibis->model;
            ibis_source_path_  = QString::fromStdString(p.ibis->file);
        } catch (const std::exception& e) {
            spdlog::warn("project: IBIS load failed: {}", e.what());
        }
    }

    // 3) AMI
    if (p.ami) {
        try {
            ami_file_ = sikit::ibis::ami::AmiParser::read_file(p.ami->params);
            ami_params_path_ = QString::fromStdString(p.ami->params);
            if (!p.ami->library.empty()) {
                ami_model_ = std::make_unique<sikit::ibis::ami::AmiModel>(
                    std::filesystem::path(p.ami->library));
                ami_library_path_ = QString::fromStdString(p.ami->library);
            }
        } catch (const std::exception& e) {
            spdlog::warn("project: AMI load failed: {}", e.what());
        }
    }

    // 4) FDM toggle
    if (use_fdm_action_) use_fdm_action_->setChecked(p.use_fdm);

    statusBar()->showMessage(QString("Project loaded ← %1")
                                 .arg(QFileInfo(path).fileName()));
    spdlog::info("project: loaded from {}", path.toStdString());
}

// ---------- Via S-parameter plot (Tier 2.7) -----------------------------

void MainWindow::onPlotViaSParam() {
    // If a board is loaded, offer to pre-fill drill/pad/length from a via on it.
    sikit::analysis::ViaSpec spec;
    spec.drill_diameter   = 0.30e-3;
    spec.pad_diameter     = 0.60e-3;
    spec.antipad_diameter = 1.00e-3;
    spec.total_length     = 1.6e-3;
    spec.pad_to_plane_h   = 0.20e-3;
    spec.stub_length      = 0.0;
    spec.epsilon_r        = 4.3;
    spec.tan_delta        = 0.02;

    if (board_ && !board_->vias.empty()) {
        QStringList items;
        items << "(enter via dimensions manually)";
        for (std::size_t i = 0; i < board_->vias.size(); ++i) {
            const auto& v = board_->vias[i];
            items << QString("via #%1  at (%2, %3) mm  drill %4 mm")
                         .arg(i)
                         .arg(v.at.x * 1e3, 0, 'f', 2)
                         .arg(v.at.y * 1e3, 0, 'f', 2)
                         .arg(v.drill * 1e3, 0, 'f', 3);
        }
        bool ok = false;
        const QString choice = QInputDialog::getItem(
            this, "Plot via S-parameters",
            "Pick a via from the board (or enter manually):",
            items, 0, false, &ok);
        if (!ok) return;
        if (choice != items.first()) {
            // Parse out the index "via #N ..." and look up board_->vias[N].
            const QString tag = choice.section(' ', 1, 1);  // "#N"
            const int idx = tag.mid(1).toInt();
            if (idx >= 0 && idx < static_cast<int>(board_->vias.size())) {
                const auto& v = board_->vias[idx];
                spec.drill_diameter = v.drill;
                spec.pad_diameter   = v.outer_diameter;
                // Antipad isn't in the .kicad_pcb; default to 1.5x pad.
                spec.antipad_diameter = std::max(v.outer_diameter * 1.5,
                                                 v.drill + 0.2e-3);
                spec.total_length = board_->stackup.total_thickness;
                // Stub: distance from to_layer to the bottom layer (very rough).
                // For through-hole vias terminating on F.Cu (signal lands on F.Cu),
                // stub = total length. We don't yet know which layer the signal
                // lands on, so leave stub at 0 by default.
            }
        }
    }

    bool ok = false;
    spec.drill_diameter = QInputDialog::getDouble(
        this, "Via", "Drill diameter (mm):",
        spec.drill_diameter * 1e3, 0.05, 5.0, 3, &ok) * 1e-3;
    if (!ok) return;
    spec.pad_diameter = QInputDialog::getDouble(
        this, "Via", "Pad diameter (mm):",
        spec.pad_diameter * 1e3, 0.05, 5.0, 3, &ok) * 1e-3;
    if (!ok) return;
    spec.antipad_diameter = QInputDialog::getDouble(
        this, "Via", "Antipad diameter (mm):",
        spec.antipad_diameter * 1e3, 0.05, 5.0, 3, &ok) * 1e-3;
    if (!ok) return;
    spec.total_length = QInputDialog::getDouble(
        this, "Via", "Total via length (mm):",
        spec.total_length * 1e3, 0.1, 10.0, 3, &ok) * 1e-3;
    if (!ok) return;
    spec.stub_length = QInputDialog::getDouble(
        this, "Via", "Stub length (mm; 0 = backdrilled):",
        spec.stub_length * 1e3, 0.0, 10.0, 3, &ok) * 1e-3;
    if (!ok) return;

    std::vector<double> freqs;
    freqs.reserve(200);
    const double f_lo = 10e6, f_hi = 50e9;   // higher than usual to capture stub notch
    for (int i = 0; i < 200; ++i) {
        const double t = static_cast<double>(i) / 199.0;
        freqs.push_back(f_lo + t * (f_hi - f_lo));
    }

    sikit::touchstone::TouchstoneFile ts;
    try {
        ts = sikit::analysis::compute_via_s2p(spec, freqs, 50.0);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Plot via S-parameters", e.what());
        return;
    }

    const auto lumped = sikit::analysis::via_lumped(spec);

    auto* w = new SParamPlotWindow(this);
    w->setWindowFlag(Qt::Window);
    w->setData(ts);
    w->setTitleSubtext(
        QString("via lumped model · d=%1mm D=%2mm h=%3mm  ·  L=%4nH C=%5pF"
                "%6")
            .arg(spec.drill_diameter   * 1e3, 0, 'f', 3)
            .arg(spec.antipad_diameter * 1e3, 0, 'f', 3)
            .arg(spec.total_length     * 1e3, 0, 'f', 3)
            .arg(lumped.L_barrel * 1e9, 0, 'f', 2)
            .arg(lumped.C_pad    * 1e12, 0, 'f', 2)
            .arg(spec.stub_length > 0
                     ? QString("  stub %1mm → f_res≈%2GHz")
                           .arg(spec.stub_length * 1e3, 0, 'f', 2)
                           .arg(lumped.stub_resonance_hz / 1e9, 0, 'f', 1)
                     : ""));
    w->show();
}
