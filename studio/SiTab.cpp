#include "SiTab.h"

#include <algorithm>
#include <cmath>

#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QStatusBar>
#include <QToolBar>

#include "BoardModel.h"

#include "sikit/PcbCanvas.h"
#include "sikit/LayerPanel.h"
#include "sikit/EyeWindow.h"
#include "sikit/SParamPlotWindow.h"

#include "si/ChannelResponse.h"
#include "si/ChannelSynthesis.h"
#include "si/DiffPair.h"
#include "si/DiffSynth.h"
#include "si/Eye.h"
#include "si/EyeMask.h"
#include "si/Ibis.h"
#include "si/Touchstone.h"
#include "si/TouchstoneWriter.h"
#include "si/TouchstoneCsv.h"
#include "si/TraceImpedance.h"
#include "si/ViaModel.h"

namespace circuitcore::studio {

namespace {

std::pair<double, double> net_geometry(const board::Board& board, int net_id) {
    std::vector<double> widths;
    double total_length = 0.0;
    for (const auto& s : board.segments) {
        if (s.net_id != net_id) continue;
        if (s.layer_ordinal != 0) continue;
        widths.push_back(s.width);
        const double dx = s.end.x - s.start.x;
        const double dy = s.end.y - s.start.y;
        total_length += std::sqrt(dx * dx + dy * dy);
    }
    if (widths.empty()) return {0.0, 0.0};
    std::sort(widths.begin(), widths.end());
    return {widths[widths.size() / 2], total_length};
}

}  // namespace

SiTab::SiTab(BoardModel* model, QWidget* parent)
    : QMainWindow(parent), model_(model) {
    setWindowFlag(Qt::Widget);

    // Central canvas.
    canvas_ = new sikit::PcbCanvas(this);
    setCentralWidget(canvas_);

    // Layers dock.
    layer_panel_ = new sikit::LayerPanel(this);
    {
        auto* scroll = new QScrollArea(this);
        scroll->setWidget(layer_panel_);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        auto* dock = new QDockWidget("Layers", this);
        dock->setWidget(scroll);
        dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
        addDockWidget(Qt::RightDockWidgetArea, dock);
        connect(layer_panel_, &sikit::LayerPanel::visibility_changed,
                canvas_, &sikit::PcbCanvas::setLayerVisibility);
    }

    // Toolbar with the SI workflows.
    auto* tb = addToolBar("SI");
    tb->setMovable(false);

    tb->addWidget(new QLabel(" Net: "));
    net_combo_ = new QComboBox(this);
    net_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    net_combo_->setMinimumWidth(140);
    tb->addWidget(net_combo_);

    auto add = [&](const QString& name, auto slot) {
        auto* a = tb->addAction(name);
        connect(a, &QAction::triggered, this, slot);
        return a;
    };
    add("Plot S-param", &SiTab::onPlotNetSParam);
    add("Plot diff-pair", &SiTab::onPlotDiffPairSParam);
    add("Plot via", &SiTab::onPlotViaSParam);
    tb->addSeparator();
    add("Synth eye", &SiTab::onSynthesizeEye);
    add("Open IBIS...", &SiTab::onOpenIbis);
    add("Open AMI...", &SiTab::onOpenAmi);
    tb->addSeparator();
    auto* z50  = add(QString::fromUtf8("Z₀=50"),  [this]() { onImpedanceOverlay(50.0); });
    auto* z90  = add(QString::fromUtf8("Z₀=90"),  [this]() { onImpedanceOverlay(90.0); });
    auto* z100 = add(QString::fromUtf8("Z₀=100"), [this]() { onImpedanceOverlay(100.0); });
    auto* zd90  = add(QString::fromUtf8("Z₝₀=90"),  [this]() { onImpedanceDiffOverlay(90.0); });
    auto* zd100 = add(QString::fromUtf8("Z₝₀=100"), [this]() { onImpedanceDiffOverlay(100.0); });
    add("Clear", &SiTab::onClearOverlay);
    (void)z50; (void)z90; (void)z100; (void)zd90; (void)zd100;
    tb->addSeparator();
    add("Export .s2p", &SiTab::onExportTouchstone);
    add("Export .csv", &SiTab::onExportCsv);
    add("Export .s4p", &SiTab::onExportDiffPairS4p);
    tb->addSeparator();
    fdm_action_ = tb->addAction("FDM");
    fdm_action_->setCheckable(true);
    fdm_action_->setToolTip("Use FDM solver (vs. closed-form Wadell)");
    view3d_action_ = tb->addAction("3D");
    view3d_action_->setCheckable(true);
    view3d_action_->setToolTip("Toggle 3D stackup view");
    connect(view3d_action_, &QAction::toggled, this, &SiTab::onView3DToggled);

    // Status bar -- hover + analysis messages.
    status_label_ = new QLabel(this);
    status_label_->setMinimumWidth(360);
    statusBar()->addPermanentWidget(status_label_);
    connect(canvas_, &sikit::PcbCanvas::hoverInfo,
            status_label_, &QLabel::setText);

    // Model wiring.
    connect(model_, &BoardModel::boardLoaded, this, &SiTab::onBoardLoaded);
}

SiTab::~SiTab() = default;

void SiTab::onBoardLoaded() {
    const auto* b = model_->board();
    if (!b) return;
    canvas_->setBoard(b);
    canvas_->setSiStackup(&model_->siStackup());
    // Populate layer panel from the stackup's copper layers.
    std::vector<sikit::LayerPanel::Entry> entries;
    for (const auto& L : b->stackup.layers) {
        if (!L.is_copper()) continue;
        sikit::LayerPanel::Entry e;
        e.ordinal = L.ordinal;
        e.name    = QString::fromStdString(L.name);
        entries.push_back(e);
    }
    layer_panel_->setLayers(entries);
    refreshNetList();
}

void SiTab::refreshNetList() {
    net_combo_->clear();
    if (!model_->board()) return;
    const auto hs = sikit::highspeed::find_high_speed_nets(*model_->board());
    for (int nid : hs) {
        if (const auto* n = model_->board()->find_net(nid)) {
            net_combo_->addItem(QString::fromStdString(n->name), nid);
        }
    }
}

int SiTab::currentNetId() const {
    if (net_combo_->currentIndex() < 0) return -1;
    return net_combo_->currentData().toInt();
}

bool SiTab::useFdm() const {
    return fdm_action_ && fdm_action_->isChecked();
}

void SiTab::onPlotNetSParam() {
    if (!model_->board()) return;
    const int net_id = currentNetId();
    if (net_id <= 0) {
        QMessageBox::information(this, "Plot S-parameters", "Pick a net first.");
        return;
    }
    auto [tw, tl] = net_geometry(*model_->board(), net_id);
    if (tw <= 0.0 || tl <= 0.0) {
        QMessageBox::warning(this, "Plot S-parameters",
                              "Net has no F.Cu segments to model.");
        return;
    }
    sikit::analysis::ChannelSpec spec;
    spec.trace_width   = tw;
    spec.layer_ordinal = 0;
    spec.length_m      = tl;
    spec.stackup       = sikit::analysis::AnalysisStackup::from_board(
        *model_->board(), model_->siStackup());
    spec.engine        = useFdm() ? sikit::analysis::Engine::Fdm
                                  : sikit::analysis::Engine::ClosedForm;
    std::vector<double> freqs;
    freqs.reserve(200);
    for (int i = 0; i < 200; ++i) {
        const double t = static_cast<double>(i) / 199.0;
        freqs.push_back(10e6 + t * (20e9 - 10e6));
    }
    sikit::touchstone::TouchstoneFile ts;
    try {
        ts = sikit::analysis::synthesize_channel(spec, freqs, 50.0);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Plot S-parameters", e.what());
        return;
    }
    auto* w = new SParamPlotWindow();
    w->setWindowFlag(Qt::Window);
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setData(ts);
    w->setTitleSubtext(QString("%1 (synth, W=%2 mm, L=%3 mm)")
                            .arg(net_combo_->currentText())
                            .arg(tw * 1e3, 0, 'f', 3)
                            .arg(tl * 1e3, 0, 'f', 1));
    w->show();
}

void SiTab::onPlotDiffPairSParam() {
    if (!model_->board()) return;
    auto pairs = sikit::highspeed::find_diff_pairs(*model_->board());
    if (pairs.empty()) {
        QMessageBox::information(this, "Plot diff-pair S-parameters",
                                  "No diff pairs detected on this board.");
        return;
    }
    QStringList items;
    for (const auto& dp : pairs) items << QString::fromStdString(dp.base_name);
    bool ok = false;
    const QString choice = QInputDialog::getItem(
        this, "Plot diff-pair", "Diff pair:", items, 0, false, &ok);
    if (!ok) return;
    const sikit::highspeed::DiffPair* picked = nullptr;
    for (const auto& dp : pairs) {
        if (QString::fromStdString(dp.base_name) == choice) { picked = &dp; break; }
    }
    if (!picked) return;
    auto stackup = sikit::analysis::AnalysisStackup::from_board(
        *model_->board(), model_->siStackup());
    auto dpz = sikit::analysis::compute_diff_pairs(
        *model_->board(), stackup, sikit::analysis::Engine::ClosedForm);
    sikit::analysis::DiffPairImpedance meta{};
    for (const auto& d : dpz) {
        if (d.base_name == picked->base_name) { meta = d; break; }
    }
    if (meta.trace_width <= 0.0) {
        QMessageBox::warning(this, "Plot diff-pair", "No geometry for picked pair.");
        return;
    }
    std::vector<double> freqs;
    for (int i = 0; i < 200; ++i) {
        const double t = static_cast<double>(i) / 199.0;
        freqs.push_back(10e6 + t * (20e9 - 10e6));
    }
    sikit::analysis::DiffChannelSpec dspec;
    dspec.trace_width   = meta.trace_width;
    dspec.spacing       = meta.spacing;
    dspec.layer_ordinal = meta.layer_ordinal;
    dspec.stackup       = stackup;
    // length_m isn't stored on DiffPairImpedance -- recompute from the
    // segment_indices it carries.
    double pair_len = 0.0;
    for (auto si : meta.segment_indices) {
        if (si >= model_->board()->segments.size()) continue;
        const auto& sg = model_->board()->segments[si];
        const double dx = sg.end.x - sg.start.x;
        const double dy = sg.end.y - sg.start.y;
        pair_len += std::sqrt(dx * dx + dy * dy);
    }
    // segment_indices includes both legs, so divide by 2 for "per-leg" length.
    dspec.length_m = 0.5 * pair_len;
    sikit::touchstone::TouchstoneFile ts;
    try {
        ts = sikit::analysis::synthesize_diff_channel(dspec, freqs);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Plot diff-pair", e.what());
        return;
    }
    auto* w = new SParamPlotWindow();
    w->setWindowFlag(Qt::Window);
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setData(ts);
    w->setTitleSubtext(QString("%1 diff pair (W=%2mm S=%3mm L=%4mm)")
                            .arg(choice)
                            .arg(meta.trace_width * 1e3, 0, 'f', 3)
                            .arg(meta.spacing * 1e3, 0, 'f', 3)
                            .arg(dspec.length_m * 1e3, 0, 'f', 1));
    w->show();
}

void SiTab::onPlotViaSParam() {
    if (!model_->board() || model_->board()->vias.empty()) {
        QMessageBox::information(this, "Plot via S-parameters",
                                  "Board has no vias.");
        return;
    }
    // Pick the first via for a quick demo; a real picker UI would be a
    // follow-up. Use ViaModel to synthesize S-params.
    const auto& v = model_->board()->vias.front();
    sikit::analysis::ViaSpec spec;
    spec.pad_diameter     = v.outer_diameter;
    spec.drill_diameter   = v.drill;
    spec.antipad_diameter = 2.0 * v.outer_diameter;  // default clearance
    spec.total_length     = model_->board()->stackup.total_thickness;
    spec.pad_to_plane_h   = 0.5 * spec.total_length;
    std::vector<double> freqs;
    for (int i = 0; i < 200; ++i) {
        const double t = static_cast<double>(i) / 199.0;
        freqs.push_back(10e6 + t * (20e9 - 10e6));
    }
    sikit::touchstone::TouchstoneFile ts;
    try {
        ts = sikit::analysis::compute_via_s2p(spec, freqs);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Plot via", e.what());
        return;
    }
    auto* w = new SParamPlotWindow();
    w->setWindowFlag(Qt::Window);
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setData(ts);
    w->setTitleSubtext(QString("Via D=%1mm / drill=%2mm")
                            .arg(spec.pad_diameter * 1e3, 0, 'f', 3)
                            .arg(spec.drill_diameter * 1e3, 0, 'f', 3));
    w->show();
}

void SiTab::onSynthesizeEye() {
    if (!model_->board()) return;
    const int net_id = currentNetId();
    if (net_id <= 0) {
        QMessageBox::information(this, "Synth eye", "Pick a net first.");
        return;
    }
    auto [tw, tl] = net_geometry(*model_->board(), net_id);
    if (tw <= 0.0 || tl <= 0.0) {
        QMessageBox::warning(this, "Synth eye", "Net has no F.Cu segments.");
        return;
    }
    bool ok = false;
    const double baud_gbps = QInputDialog::getDouble(
        this, "Synth eye", "Bit rate (Gbps):", 10.0, 0.01, 100.0, 2, &ok);
    if (!ok) return;
    const double baud = baud_gbps * 1e9;

    sikit::analysis::ChannelSpec spec;
    spec.trace_width   = tw;
    spec.layer_ordinal = 0;
    spec.length_m      = tl;
    spec.stackup       = sikit::analysis::AnalysisStackup::from_board(
        *model_->board(), model_->siStackup());
    spec.engine        = useFdm() ? sikit::analysis::Engine::Fdm
                                  : sikit::analysis::Engine::ClosedForm;

    constexpr int kBits = 2000, kSpu = 32;
    const double fs = baud * kSpu;
    std::vector<double> freqs;
    const double f_step = baud / 50.0;
    for (double f = f_step; f <= fs / 2.0; f += f_step) freqs.push_back(f);

    sikit::touchstone::TouchstoneFile ch;
    std::vector<double> tx, rx;
    sikit::eye::EyeGrid eye;
    try {
        ch = sikit::analysis::synthesize_channel(spec, freqs, 50.0);
        auto bits = sikit::eye::prbs7(kBits);
        double ramp = 0.0;
        if (ibis_file_) {
            for (const auto& m : ibis_file_->models) {
                if (m.name == active_ibis_model_) {
                    ramp = sikit::eye::ramp_fraction_from_ibis(m, baud);
                    break;
                }
            }
        }
        tx = (ramp > 0.0) ? sikit::eye::nrz_with_ramp(bits, kSpu, ramp)
                          : sikit::eye::nrz_waveform(bits, kSpu);
        rx = sikit::dsp::apply_channel(tx, fs, ch);
        eye = sikit::eye::build_eye(rx, kSpu, 128, 96, 8);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Synth eye", e.what());
        return;
    }
    auto* w = new EyeWindow();
    w->setWindowFlag(Qt::Window);
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setEye(eye);
    w->setTitleSubtext(QString("%1 at %2 Gbps  (W=%3mm L=%4mm)")
                            .arg(net_combo_->currentText())
                            .arg(baud_gbps, 0, 'f', 2)
                            .arg(tw * 1e3, 0, 'f', 3)
                            .arg(tl * 1e3, 0, 'f', 1));
    w->show();
}

void SiTab::onOpenIbis() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open IBIS", QString(), "IBIS (*.ibs);;All (*)");
    if (path.isEmpty()) return;
    try {
        ibis_file_ = sikit::ibis::IbisReader::read_file(path.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Open IBIS", e.what());
        return;
    }
    if (!ibis_file_ || ibis_file_->models.empty()) {
        QMessageBox::warning(this, "Open IBIS", "No [Model] sections.");
        return;
    }
    QStringList items;
    for (const auto& m : ibis_file_->models)
        items << QString::fromStdString(m.name);
    bool ok = true;
    const QString choice = (items.size() == 1)
        ? items.first()
        : QInputDialog::getItem(this, "IBIS model", "Pick:", items, 0, false, &ok);
    if (!ok) return;
    active_ibis_model_ = choice.toStdString();
    status_label_->setText(QString("IBIS: %1 (model %2)")
                                .arg(QFileInfo(path).fileName()).arg(choice));
}

void SiTab::onOpenAmi() {
    const QString params = QFileDialog::getOpenFileName(
        this, "Open AMI parameter file", QString(), "AMI (*.ami);;All (*)");
    if (params.isEmpty()) return;
    const QString lib = QFileDialog::getOpenFileName(
        this, "Open AMI shared library", QString(),
        "Shared lib (*.so *.dll);;All (*)");
    if (lib.isEmpty()) return;
    try {
        ami_file_ = sikit::ibis::ami::AmiParser::read_file(params.toStdString());
        ami_model_ = std::make_unique<sikit::ibis::ami::AmiModel>(
            lib.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Open AMI", e.what());
        return;
    }
    status_label_->setText(QString("AMI: %1 + %2")
                                .arg(QFileInfo(params).fileName())
                                .arg(QFileInfo(lib).fileName()));
}

void SiTab::onExportTouchstone() {
    if (!model_->board()) return;
    const int net_id = currentNetId();
    if (net_id <= 0) {
        QMessageBox::information(this, "Export .s2p", "Pick a net first.");
        return;
    }
    auto [tw, tl] = net_geometry(*model_->board(), net_id);
    if (tw <= 0.0) {
        QMessageBox::warning(this, "Export .s2p", "Net has no F.Cu segments.");
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, "Save .s2p", net_combo_->currentText() + ".s2p",
        "Touchstone 2-port (*.s2p)");
    if (path.isEmpty()) return;
    sikit::analysis::ChannelSpec spec;
    spec.trace_width = tw; spec.layer_ordinal = 0; spec.length_m = tl;
    spec.stackup = sikit::analysis::AnalysisStackup::from_board(
        *model_->board(), model_->siStackup());
    spec.engine  = useFdm() ? sikit::analysis::Engine::Fdm
                            : sikit::analysis::Engine::ClosedForm;
    std::vector<double> freqs;
    for (int i = 0; i < 401; ++i) {
        const double t = static_cast<double>(i) / 400.0;
        freqs.push_back(10e6 + t * (40e9 - 10e6));
    }
    try {
        auto ts = sikit::analysis::synthesize_channel(spec, freqs, 50.0);
        sikit::touchstone::TouchstoneWriter::write_file(ts, path.toStdString());
        status_label_->setText(QString("Wrote %1").arg(QFileInfo(path).fileName()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Export .s2p", e.what());
    }
}

void SiTab::onExportCsv() {
    if (!model_->board()) return;
    const int net_id = currentNetId();
    if (net_id <= 0) {
        QMessageBox::information(this, "Export .csv", "Pick a net first.");
        return;
    }
    auto [tw, tl] = net_geometry(*model_->board(), net_id);
    if (tw <= 0.0) return;
    const QString path = QFileDialog::getSaveFileName(
        this, "Save CSV", net_combo_->currentText() + ".csv", "CSV (*.csv)");
    if (path.isEmpty()) return;
    sikit::analysis::ChannelSpec spec;
    spec.trace_width = tw; spec.layer_ordinal = 0; spec.length_m = tl;
    spec.stackup = sikit::analysis::AnalysisStackup::from_board(
        *model_->board(), model_->siStackup());
    std::vector<double> freqs;
    for (int i = 0; i < 401; ++i) {
        const double t = static_cast<double>(i) / 400.0;
        freqs.push_back(10e6 + t * (40e9 - 10e6));
    }
    try {
        auto ts = sikit::analysis::synthesize_channel(spec, freqs, 50.0);
        sikit::touchstone::TouchstoneCsv::write_file(ts, path.toStdString());
        status_label_->setText(QString("Wrote %1").arg(QFileInfo(path).fileName()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Export .csv", e.what());
    }
}

void SiTab::onExportDiffPairS4p() {
    if (!model_->board()) return;
    auto pairs = sikit::highspeed::find_diff_pairs(*model_->board());
    if (pairs.empty()) {
        QMessageBox::information(this, "Export .s4p", "No diff pairs.");
        return;
    }
    QStringList items;
    for (const auto& dp : pairs) items << QString::fromStdString(dp.base_name);
    bool ok = false;
    const QString choice = QInputDialog::getItem(
        this, "Export .s4p", "Diff pair:", items, 0, false, &ok);
    if (!ok) return;
    const sikit::highspeed::DiffPair* picked = nullptr;
    for (const auto& dp : pairs) {
        if (QString::fromStdString(dp.base_name) == choice) { picked = &dp; break; }
    }
    if (!picked) return;
    auto stackup = sikit::analysis::AnalysisStackup::from_board(
        *model_->board(), model_->siStackup());
    auto dpz = sikit::analysis::compute_diff_pairs(
        *model_->board(), stackup, sikit::analysis::Engine::ClosedForm);
    sikit::analysis::DiffPairImpedance meta{};
    for (const auto& d : dpz) {
        if (d.base_name == picked->base_name) { meta = d; break; }
    }
    if (meta.trace_width <= 0.0) return;
    const QString path = QFileDialog::getSaveFileName(
        this, "Save .s4p", choice + ".s4p", "Touchstone 4-port (*.s4p)");
    if (path.isEmpty()) return;
    std::vector<double> freqs;
    for (int i = 0; i < 401; ++i) {
        const double t = static_cast<double>(i) / 400.0;
        freqs.push_back(10e6 + t * (40e9 - 10e6));
    }
    sikit::analysis::DiffChannelSpec dspec;
    dspec.trace_width   = meta.trace_width;
    dspec.spacing       = meta.spacing;
    dspec.layer_ordinal = meta.layer_ordinal;
    dspec.stackup       = stackup;
    // length_m isn't stored on DiffPairImpedance -- recompute from the
    // segment_indices it carries.
    double pair_len = 0.0;
    for (auto si : meta.segment_indices) {
        if (si >= model_->board()->segments.size()) continue;
        const auto& sg = model_->board()->segments[si];
        const double dx = sg.end.x - sg.start.x;
        const double dy = sg.end.y - sg.start.y;
        pair_len += std::sqrt(dx * dx + dy * dy);
    }
    // segment_indices includes both legs, so divide by 2 for "per-leg" length.
    dspec.length_m = 0.5 * pair_len;
    try {
        auto ts = sikit::analysis::synthesize_diff_channel(dspec, freqs);
        sikit::touchstone::TouchstoneWriter::write_file(ts, path.toStdString());
        status_label_->setText(QString("Wrote %1").arg(QFileInfo(path).fileName()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, "Export .s4p", e.what());
    }
}

void SiTab::onImpedanceOverlay(double target_z0) {
    if (!model_->board()) return;
    auto stackup = sikit::analysis::AnalysisStackup::from_board(
        *model_->board(), model_->siStackup());
    auto engine = useFdm() ? sikit::analysis::Engine::Fdm
                            : sikit::analysis::Engine::ClosedForm;
    auto results = sikit::analysis::compute_all(*model_->board(), stackup, engine);
    canvas_->setImpedanceOverlay(results, target_z0);
    int on = 0, warn = 0, fail = 0;
    for (const auto& r : results) {
        const double err = std::abs(r.z0 - target_z0) / target_z0;
        if (err < 0.05) ++on;
        else if (err < 0.10) ++warn;
        else ++fail;
    }
    status_label_->setText(
        QString("Z @ %1 Ohm: %2 on-spec, %3 warn, %4 fail")
            .arg(target_z0, 0, 'f', 0).arg(on).arg(warn).arg(fail));
}

void SiTab::onImpedanceDiffOverlay(double target_z_diff) {
    if (!model_->board()) return;
    auto stackup = sikit::analysis::AnalysisStackup::from_board(
        *model_->board(), model_->siStackup());
    auto dpz = sikit::analysis::compute_diff_pairs(
        *model_->board(), stackup, sikit::analysis::Engine::ClosedForm);
    // Re-use the single-ended overlay rendering: paint each P/N segment
    // colored by how far its diff_z is from target.
    std::vector<sikit::analysis::SegmentImpedance> mapped;
    for (const auto& d : dpz) {
        sikit::analysis::SegmentImpedance si;
        si.z0 = d.z_diff;
        for (auto i : d.segment_indices) {
            si.segment_index = i;
            mapped.push_back(si);
        }
    }
    canvas_->setImpedanceOverlay(mapped, target_z_diff);
    status_label_->setText(
        QString("Diff Z @ %1 Ohm: %2 pairs evaluated")
            .arg(target_z_diff, 0, 'f', 0).arg(dpz.size()));
}

void SiTab::onClearOverlay() {
    canvas_->clearImpedanceOverlay();
    status_label_->setText(QString());
}

void SiTab::onView3DToggled(bool on) {
    canvas_->setViewMode(on ? sikit::PcbCanvas::ViewMode::D3 : sikit::PcbCanvas::ViewMode::D2);
}

}  // namespace circuitcore::studio
