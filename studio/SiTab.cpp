#include "SiTab.h"

#include <algorithm>
#include <cmath>

#include <QAction>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QTableWidget>
#include <QTextStream>
#include <QUrl>
#include <QVBoxLayout>
#include <QDockWidget>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QLabel>
#include <QMessageBox>
#include <QScrollArea>
#include <QStatusBar>
#include <QToolBar>
#include <QToolButton>

#include "BoardModel.h"

#include "sikit/PcbCanvas.h"
#include "sikit/LayerPanel.h"
#include "sikit/EyeWindow.h"
#include "sikit/SParamPlotWindow.h"

#include "si/ChannelResponse.h"
#include "si/Overlay.h"
#include "si/Report.h"
#include "si/ReturnPath.h"
#include "si/SchematicTopology.h"
#include "si/SParam.h"
#include "si/Skew.h"
#include "si/SpiceExport.h"
#include "si/StatEye.h"
#include "si/Crosstalk.h"
#include "si/Connector.h"
#include "circuitcore/formats/kicad/NetlistParser.h"
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

sikit::analysis::ChannelSpec build_spec(const board::Board& board,
                                         const sikit::si::SiStackup& stackup,
                                         double trace_w, double length_m) {
    sikit::analysis::ChannelSpec spec;
    spec.trace_width   = trace_w;
    spec.layer_ordinal = 0;
    spec.length_m      = length_m;
    spec.stackup       =
        sikit::analysis::AnalysisStackup::from_board(board, stackup);
    spec.engine        = sikit::analysis::Engine::ClosedForm;
    return spec;
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
    add("HTML report...",         &SiTab::onSaveHtmlReport);
    add("De-embed...",            &SiTab::onDeembedTouchstone);
    add("Compare overlay...",     &SiTab::onCompareOverlay);
    add("Skew",                   &SiTab::onCheckSkew);
    add("Return path",            &SiTab::onCheckReturnPath);
    add("Topology from .net...",  &SiTab::onDeriveTopology);
    tb->addSeparator();
    auto* tools_menu = new QMenu(this);
    tools_menu->addAction("Statistical eye (PDA)", this, &SiTab::onStatEyePda);
    tools_menu->addAction("Crosstalk eye for victim net", this,
                            &SiTab::onCrosstalkVictim);
    tools_menu->addAction("Export channel as SPICE...", this,
                            &SiTab::onSpiceExport);
    tools_menu->addAction("Connector library...", this,
                            &SiTab::onConnectorLibrary);
    auto* tools_btn = new QToolButton(tb);
    tools_btn->setText("Tools");
    tools_btn->setMenu(tools_menu);
    tools_btn->setPopupMode(QToolButton::InstantPopup);
    tb->addWidget(tools_btn);
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



namespace {

// Pop a modal-ish results dialog with a read-only QTableWidget. Used by
// the skew / return-path / topology workflows that produce tabular
// output without needing to drive a plot widget.
void showResultsDialog(QWidget* parent, const QString& title,
                        const QStringList& headers,
                        const std::vector<QStringList>& rows) {
    auto* dlg = new QDialog(parent);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(title);
    dlg->resize(720, 360);
    auto* layout = new QVBoxLayout(dlg);
    auto* table = new QTableWidget(static_cast<int>(rows.size()),
                                     headers.size(), dlg);
    table->setHorizontalHeaderLabels(headers);
    table->verticalHeader()->setVisible(false);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->horizontalHeader()->setStretchLastSection(true);
    for (int r = 0; r < static_cast<int>(rows.size()); ++r) {
        const auto& row = rows[r];
        for (int c = 0; c < row.size(); ++c) {
            table->setItem(r, c, new QTableWidgetItem(row[c]));
        }
    }
    table->resizeColumnsToContents();
    layout->addWidget(table);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    QObject::connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    layout->addWidget(bb);
    dlg->show();
}

// Build a QStringList row from a parameter pack via operator<<.
template <typename... Args>
QStringList makeRow(Args&&... args) {
    QStringList r;
    (r << ... << QString(args));
    return r;
}

}  // namespace

void SiTab::onSaveHtmlReport() {
    if (!model_->board()) {
        QMessageBox::information(this, tr("HTML report"),
                                  tr("Load a board first."));
        return;
    }
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save board report"), "board_report.html",
        tr("HTML (*.html)"));
    if (path.isEmpty()) return;
    auto report = sikit::report::build_board_report(*model_->board(),
                                                      model_->siStackup());
    if (!model_->currentPath().isEmpty()) {
        report.board_path =
            QFileInfo(model_->currentPath()).fileName().toStdString();
    }
    const auto html = sikit::report::render_html(report);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QMessageBox::critical(this, tr("HTML report"),
                               tr("Cannot write %1").arg(path));
        return;
    }
    QTextStream(&f) << QString::fromStdString(html);
    f.close();
    status_label_->setText(tr("Wrote %1").arg(QFileInfo(path).fileName()));
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void SiTab::onDeembedTouchstone() {
    const QString meas = QFileDialog::getOpenFileName(
        this, tr("Measured DUT+fixtures"), QString(),
        tr("Touchstone (*.s2p)"));
    if (meas.isEmpty()) return;
    const QString left = QFileDialog::getOpenFileName(
        this, tr("Left fixture (.s2p)"), QString(),
        tr("Touchstone (*.s2p)"));
    if (left.isEmpty()) return;
    const QString right = QFileDialog::getOpenFileName(
        this, tr("Right fixture (.s2p)  -- Cancel to reuse left"),
        QString(), tr("Touchstone (*.s2p)"));

    using sikit::touchstone::TouchstoneFile;
    using sikit::touchstone::TouchstoneReader;
    TouchstoneFile measured, fl, fr, out;
    try {
        measured = TouchstoneReader::read_file(meas.toStdString());
        fl       = TouchstoneReader::read_file(left.toStdString());
        if (right.isEmpty()) {
            out = sikit::sparam::deembed_symmetric(measured, fl);
        } else {
            fr = TouchstoneReader::read_file(right.toStdString());
            out = sikit::sparam::deembed(measured, fl, fr);
        }
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("De-embed"), e.what());
        return;
    }
    auto* w = new SParamPlotWindow();
    w->setWindowFlag(Qt::Window);
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setData(out);
    w->setTitleSubtext(QString("De-embedded %1")
                            .arg(QFileInfo(meas).fileName()));
    w->show();
}

void SiTab::onCompareOverlay() {
    const QString a = QFileDialog::getOpenFileName(
        this, tr("Primary (simulated) Touchstone"), QString(),
        tr("Touchstone (*.s*p)"));
    if (a.isEmpty()) return;
    const QString b = QFileDialog::getOpenFileName(
        this, tr("Overlay (measured) Touchstone"), QString(),
        tr("Touchstone (*.s*p)"));
    if (b.isEmpty()) return;
    sikit::touchstone::TouchstoneFile ta, tb;
    try {
        ta = sikit::touchstone::TouchstoneReader::read_file(a.toStdString());
        tb = sikit::touchstone::TouchstoneReader::read_file(b.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Compare"), e.what());
        return;
    }
    auto* w = new SParamPlotWindow();
    w->setWindowFlag(Qt::Window);
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setData(ta);
    w->setOverlay(tb, QFileInfo(b).fileName().toStdString());
    w->setTitleSubtext(QString("Compare %1 vs %2")
                            .arg(QFileInfo(a).fileName())
                            .arg(QFileInfo(b).fileName()));
    w->show();
}

void SiTab::onCheckSkew() {
    if (!model_->board()) return;
    auto stackup = sikit::analysis::AnalysisStackup::from_board(
        *model_->board(), model_->siStackup());
    auto skews = sikit::si::compute_diff_pair_skews(
        *model_->board(), stackup, 5.0);
    if (skews.empty()) {
        QMessageBox::information(this, tr("Diff-pair skew"),
                                  tr("No diff pairs detected on this board."));
        return;
    }
    std::vector<QStringList> rows;
    rows.reserve(skews.size());
    for (const auto& s : skews) {
        QStringList r;
        r << QString::fromStdString(s.base_name)
          << QString::number(s.length_p_m * 1e3, 'f', 3)
          << QString::number(s.length_n_m * 1e3, 'f', 3)
          << QString::number(s.skew_m * 1e6, 'f', 1)
          << QString::number(s.skew_ps, 'f', 2)
          << (s.exceeds_budget ? "FAIL" : "ok");
        rows.push_back(std::move(r));
    }
    QStringList headers;
    headers << "Pair" << "P length mm" << "N length mm"
            << "skew um" << "skew ps" << "vs budget";
    showResultsDialog(this, tr("Diff-pair skew (budget 5 ps)"),
                       headers, rows);
}

void SiTab::onCheckReturnPath() {
    if (!model_->board()) return;
    auto vs = sikit::si::detect_return_path_violations(*model_->board(), 20,
                                                         0.05);
    if (vs.empty()) {
        QMessageBox::information(this, tr("Return path"),
                                  tr("No violations -- every signal trace "
                                      "has a continuous reference plane."));
        return;
    }
    std::vector<QStringList> rows;
    rows.reserve(vs.size());
    for (const auto& v : vs) {
        const auto* net = model_->board()->find_net(v.net_id);
        const QString net_name = net ? QString::fromStdString(net->name)
                                      : QString::number(v.net_id);
        const auto* sl = model_->board()->find_layer(v.signal_layer);
        const auto* rl = model_->board()->find_layer(v.reference_layer);
        QStringList r;
        r << QString::number(v.segment_index)
          << net_name
          << (sl ? QString::fromStdString(sl->name) : QString("?"))
          << (rl ? QString::fromStdString(rl->name) : QString("(none)"))
          << QString::number(v.segment_length_m * 1e3, 'f', 2)
          << QString::number(v.off_plane_fraction * 100.0, 'f', 1) + "%"
          << QString::number(v.severity_m * 1e3, 'f', 2);
        rows.push_back(std::move(r));
    }
    QStringList headers;
    headers << "Seg #" << "Net" << "Signal layer" << "Reference"
            << "Length mm" << "Off-plane" << "Severity mm";
    showResultsDialog(this, tr("Return-path violations"), headers, rows);
}

void SiTab::onDeriveTopology() {
    const QString path = QFileDialog::getOpenFileName(
        this, tr("Open KiCad .net"), QString(),
        tr("KiCad netlist (*.net)"));
    if (path.isEmpty()) return;
    auto r = circuitcore::formats::kicad::NetlistParser::parse_file(
        path.toStdString());
    if (!r.has_value()) {
        QMessageBox::critical(this, tr("Derive topology"),
                               QString::fromStdString(r.error().format()));
        return;
    }
    auto all = sikit::si::derive_all_topologies(r.value());
    std::vector<QStringList> rows;
    rows.reserve(all.size());
    for (const auto& t : all) {
        QStringList row;
        row << QString::fromStdString(t.net_name)
            << QString::number(t.endpoints.size())
            << QString::number(t.drivers().size())
            << QString::number(t.receivers().size())
            << QString::number(t.passives().size())
            << (t.has_driver_problem() ? "FLAG" : "ok");
        rows.push_back(std::move(row));
    }
    QStringList headers;
    headers << "Net" << "Endpoints" << "Drivers" << "Receivers"
            << "Passives" << "Sanity";
    showResultsDialog(this, tr("Schematic-derived topology"), headers, rows);
}



void SiTab::onStatEyePda() {
    if (!model_->board()) return;
    const int net_id = currentNetId();
    if (net_id <= 0) {
        QMessageBox::information(this, tr("Statistical eye"),
                                  tr("Pick a net first."));
        return;
    }
    auto [tw, tl] = net_geometry(*model_->board(), net_id);
    if (tw <= 0.0) {
        QMessageBox::warning(this, tr("Statistical eye"),
                              tr("Net has no F.Cu segments."));
        return;
    }
    bool ok = false;
    const double baud_gbps = QInputDialog::getDouble(
        this, tr("Statistical eye"), tr("Bit rate (Gbps):"),
        10.0, 0.01, 100.0, 2, &ok);
    if (!ok) return;
    const double baud = baud_gbps * 1e9;
    auto spec = build_spec(*model_->board(), model_->siStackup(), tw, tl);
    std::vector<double> freqs;
    constexpr int kSpu = 32;
    const double fs = baud * kSpu;
    for (double f = baud / 50.0; f <= fs / 2.0; f += baud / 50.0)
        freqs.push_back(f);
    sikit::touchstone::TouchstoneFile ch;
    sikit::eye::PdaEyeEnvelope env;
    try {
        ch  = sikit::analysis::synthesize_channel(spec, freqs, 50.0);
        auto sbr = sikit::eye::compute_sbr(ch, baud, kSpu, 64);
        env = sikit::eye::peak_distortion_eye(sbr, kSpu);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Statistical eye"), e.what());
        return;
    }
    QMessageBox::information(this, tr("Statistical eye -- PDA"),
        tr("<b>%1 at %2 Gbps</b><br><br>"
           "Eye height: <b>%3 mV</b><br>"
           "Eye width (zero-margin): <b>%4 %5 UI</b><br><br>"
           "Peak-distortion worst-case bound (no random ISI averaging).")
            .arg(net_combo_->currentText())
            .arg(baud_gbps, 0, 'f', 2)
            .arg(env.eye_height * 1000.0, 0, 'f', 1)
            .arg(env.eye_width * 100.0, 0, 'f', 1)
            .arg("%"));
}

void SiTab::onCrosstalkVictim() {
    if (!model_->board() || net_combo_->count() < 2) {
        QMessageBox::information(this, tr("Crosstalk eye"),
                                  tr("Need at least two high-speed nets."));
        return;
    }
    const int victim_id = currentNetId();
    if (victim_id <= 0) return;
    bool ok = false;
    QStringList agg_items;
    for (int i = 0; i < net_combo_->count(); ++i) {
        if (net_combo_->itemData(i).toInt() != victim_id)
            agg_items << net_combo_->itemText(i);
    }
    const QString agg_choice = QInputDialog::getItem(
        this, tr("Crosstalk eye"), tr("Pick the aggressor net:"),
        agg_items, 0, false, &ok);
    if (!ok) return;
    const int agg_id =
        model_->board()->find_net_by_name(agg_choice.toStdString())->id;
    auto [vw, vl] = net_geometry(*model_->board(), victim_id);
    auto [aw, al] = net_geometry(*model_->board(), agg_id);
    if (vw <= 0.0 || aw <= 0.0) {
        QMessageBox::warning(this, tr("Crosstalk eye"),
                              tr("Both nets need F.Cu segments."));
        return;
    }
    const double baud = 10e9;
    constexpr int kSpu = 32;
    auto vs = build_spec(*model_->board(), model_->siStackup(), vw, vl);
    auto as = build_spec(*model_->board(), model_->siStackup(), aw, al);
    std::vector<double> freqs;
    for (double f = baud / 50.0; f <= baud * kSpu / 2.0; f += baud / 50.0)
        freqs.push_back(f);
    sikit::eye::EyeGrid eye;
    try {
        sikit::analysis::CrosstalkScenario sc;
        sc.victim_thru =
            sikit::analysis::synthesize_channel(vs, freqs, 50.0);
        // Aggressor coupling proxy: the aggressor's own through-channel
        // scaled by a fixed -25 dB coupling (rough order-of-magnitude
        // when no measured FEXT is available; a real workflow would
        // load a measured S4P).
        auto agg_thru =
            sikit::analysis::synthesize_channel(as, freqs, 50.0);
        for (auto& s : agg_thru.s_matrices) for (auto& v : s) v *= 0.056;
        sc.aggressor_to_victim_coupling.push_back(std::move(agg_thru));
        eye = sikit::analysis::simulate_crosstalk_eye(sc, baud);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Crosstalk eye"), e.what());
        return;
    }
    auto* w = new EyeWindow();
    w->setWindowFlag(Qt::Window);
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setEye(eye);
    w->setTitleSubtext(QString("%1 victim, %2 aggressor (-25 dB coupling)")
                            .arg(net_combo_->currentText())
                            .arg(agg_choice));
    w->show();
}

void SiTab::onSpiceExport() {
    if (!model_->board()) return;
    const int net_id = currentNetId();
    if (net_id <= 0) {
        QMessageBox::information(this, tr("Export SPICE"),
                                  tr("Pick a net first."));
        return;
    }
    auto [tw, tl] = net_geometry(*model_->board(), net_id);
    if (tw <= 0.0) return;
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save SPICE subcircuit"),
        net_combo_->currentText() + ".sp", tr("SPICE (*.sp *.cir)"));
    if (path.isEmpty()) return;
    auto spec = build_spec(*model_->board(), model_->siStackup(), tw, tl);
    std::vector<double> freqs;
    for (int i = 0; i < 401; ++i) {
        const double t = static_cast<double>(i) / 400.0;
        freqs.push_back(10e6 + t * (40e9 - 10e6));
    }
    try {
        auto ts = sikit::analysis::synthesize_channel(spec, freqs, 50.0);
        sikit::si::SpiceExportOptions opts;
        opts.subckt_name = net_combo_->currentText().toStdString();
        sikit::si::write_spice_subckt(ts, path.toStdString(), opts);
        status_label_->setText(tr("Wrote %1")
                                    .arg(QFileInfo(path).fileName()));
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Export SPICE"), e.what());
    }
}

void SiTab::onConnectorLibrary() {
    auto names = sikit::si::available_connector_presets();
    QStringList items;
    for (const auto& n : names) items << QString::fromStdString(n);
    bool ok = false;
    const QString choice = QInputDialog::getItem(
        this, tr("Connector library"),
        tr("Pick a preset (Touchstone is generated, not measured):"),
        items, 0, false, &ok);
    if (!ok) return;
    sikit::si::ConnectorSpec spec;
    try {
        spec = sikit::si::connector_preset_by_name(choice.toStdString());
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Connector library"), e.what());
        return;
    }
    std::vector<double> freqs;
    for (int i = 0; i < 401; ++i) {
        const double t = static_cast<double>(i) / 400.0;
        freqs.push_back(10e6 + t * (40e9 - 10e6));
    }
    sikit::touchstone::TouchstoneFile ts;
    try {
        ts = sikit::si::generate_connector_touchstone(spec, freqs);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Connector library"), e.what());
        return;
    }
    auto* w = new SParamPlotWindow();
    w->setWindowFlag(Qt::Window);
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setData(ts);
    w->setTitleSubtext(QString("%1 (parametric placeholder)").arg(choice));
    w->show();
}

}  // namespace circuitcore::studio
