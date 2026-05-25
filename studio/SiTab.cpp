#include "SiTab.h"

#include <algorithm>
#include <cmath>
#include <exception>
#include <vector>

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "BoardModel.h"
#include "sikit/EyeWindow.h"
#include "sikit/SParamPlotWindow.h"
#include "circuitcore/ui/PcbCanvas.h"

#include "si/ChannelResponse.h"  // sikit::dsp::apply_channel
#include "si/ChannelSynthesis.h" // ChannelSpec, AnalysisStackup, synthesize_channel
#include "si/DiffPair.h"         // find_high_speed_nets
#include "si/Eye.h"

namespace circuitcore::studio {

SiTab::SiTab(BoardModel* model, QWidget* parent)
    : QWidget(parent),
      model_(model),
      canvas_(new ui::PcbCanvas(this)),
      net_combo_(new QComboBox(this)),
      status_(new QLabel(this)) {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    // Left: canvas
    root->addWidget(canvas_, 3);

    // Right: analysis panel
    auto* panel = new QWidget(this);
    panel->setMaximumWidth(320);
    auto* col = new QVBoxLayout(panel);

    col->addWidget(new QLabel(tr("<b>Signal Integrity</b>")));
    col->addWidget(new QLabel(tr("Net (high-speed):")));
    net_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    col->addWidget(net_combo_);

    auto* plot_btn = new QPushButton(tr("Plot S-parameters"), panel);
    auto* eye_btn  = new QPushButton(tr("Eye diagram (10 Gbps NRZ)"), panel);
    col->addWidget(plot_btn);
    col->addWidget(eye_btn);

    status_->setWordWrap(true);
    status_->setStyleSheet("color: #808080;");
    col->addWidget(status_);
    col->addStretch(1);

    root->addWidget(panel, 1);

    connect(model_, &BoardModel::boardLoaded, this, &SiTab::onBoardLoaded);
    connect(plot_btn, &QPushButton::clicked, this, &SiTab::onPlotSParam);
    connect(eye_btn,  &QPushButton::clicked, this, &SiTab::onPlotEye);
}

void SiTab::onBoardLoaded() {
    canvas_->setBoard(model_->board());
    refreshNetList();
}

void SiTab::refreshNetList() {
    net_combo_->clear();
    if (!model_->board()) return;
    // High-speed-net detector uses diff-pair + length / fan-out heuristics;
    // same call sikit's MainWindow uses to populate its "Plot net
    // S-parameters" picker.
    auto hs_ids = sikit::highspeed::find_high_speed_nets(*model_->board());
    for (int nid : hs_ids) {
        if (const auto* n = model_->board()->find_net(nid)) {
            net_combo_->addItem(QString::fromStdString(n->name), nid);
        }
    }
    if (net_combo_->count() == 0) {
        status_->setText(tr("No high-speed nets detected on this board."));
    } else {
        status_->setText(
            tr("%1 high-speed net%2 detected.")
                .arg(net_combo_->count())
                .arg(net_combo_->count() == 1 ? "" : "s"));
    }
}

namespace {

// Walk a net's F.Cu segments and return (median trace width, total length).
// Returns (0, 0) if the net has nothing on F.Cu. Same shape sikit's
// MainWindow uses internally.
std::pair<double, double> net_geometry(
    const board::Board& board, int net_id) {
    std::vector<double> widths;
    double total_length = 0.0;
    for (const auto& seg : board.segments) {
        if (seg.net_id != net_id) continue;
        if (seg.layer_ordinal != 0) continue;
        widths.push_back(seg.width);
        const double dx = seg.end.x - seg.start.x;
        const double dy = seg.end.y - seg.start.y;
        total_length += std::sqrt(dx * dx + dy * dy);
    }
    if (widths.empty()) return {0.0, 0.0};
    std::sort(widths.begin(), widths.end());
    return {widths[widths.size() / 2], total_length};
}

sikit::analysis::ChannelSpec build_spec(
    const board::Board& board,
    const sikit::si::SiStackup& sis,
    double trace_width_m, double length_m) {
    sikit::analysis::ChannelSpec spec;
    spec.trace_width   = trace_width_m;
    spec.layer_ordinal = 0;
    spec.length_m      = length_m;
    spec.stackup       = sikit::analysis::AnalysisStackup::from_board(board, sis);
    spec.engine        = sikit::analysis::Engine::ClosedForm;
    return spec;
}

}  // namespace

void SiTab::onPlotSParam() {
    if (!model_->board()) {
        QMessageBox::information(this, tr("Plot S-parameters"),
                                  tr("Load a board first (File > Open)."));
        return;
    }
    const int net_id = net_combo_->currentData().toInt();
    if (net_id <= 0) {
        QMessageBox::information(this, tr("Plot S-parameters"),
                                  tr("Pick a net first."));
        return;
    }
    auto [tw, tl] = net_geometry(*model_->board(), net_id);
    if (tw <= 0.0 || tl <= 0.0) {
        QMessageBox::warning(this, tr("Plot S-parameters"),
                              tr("Net has no F.Cu segments to model."));
        return;
    }

    auto spec = build_spec(*model_->board(), model_->siStackup(), tw, tl);

    // 200-point linear sweep, 10 MHz to 20 GHz -- same grid the sikit
    // MainWindow uses for its "Plot net S-parameters" action.
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
        QMessageBox::critical(this, tr("Plot S-parameters"), e.what());
        return;
    }

    auto* w = new SParamPlotWindow();
    w->setWindowFlag(Qt::Window);
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setData(ts);
    w->setTitleSubtext(
        QString("%1 (synthesised, W=%2 mm, L=%3 mm)")
            .arg(net_combo_->currentText())
            .arg(tw * 1e3, 0, 'f', 3)
            .arg(tl * 1e3, 0, 'f', 1));
    w->show();
}

void SiTab::onPlotEye() {
    if (!model_->board()) {
        QMessageBox::information(this, tr("Eye diagram"),
                                  tr("Load a board first (File > Open)."));
        return;
    }
    const int net_id = net_combo_->currentData().toInt();
    if (net_id <= 0) {
        QMessageBox::information(this, tr("Eye diagram"),
                                  tr("Pick a net first."));
        return;
    }
    auto [tw, tl] = net_geometry(*model_->board(), net_id);
    if (tw <= 0.0 || tl <= 0.0) {
        QMessageBox::warning(this, tr("Eye diagram"),
                              tr("Net has no F.Cu segments to model."));
        return;
    }

    auto spec = build_spec(*model_->board(), model_->siStackup(), tw, tl);

    // PRBS-7 at 10 Gbps NRZ. Same pipeline shape as sikit::MainWindow's
    // onSynthesizeEye: synthesize_channel -> nrz_waveform -> apply_channel
    // -> build_eye.
    constexpr int    kBits = 2000;
    constexpr int    kSpu  = 32;
    constexpr double kBaud = 10e9;
    const double fs = kBaud * kSpu;

    std::vector<double> freqs;
    const double f_step = kBaud / 50.0;
    const double f_max  = fs / 2.0;
    for (double f = f_step; f <= f_max; f += f_step) freqs.push_back(f);

    sikit::touchstone::TouchstoneFile channel;
    std::vector<double> tx, rx;
    sikit::eye::EyeGrid grid;
    try {
        channel = sikit::analysis::synthesize_channel(spec, freqs, 50.0);
        const auto bits = sikit::eye::prbs7(kBits);
        tx = sikit::eye::nrz_waveform(bits, kSpu);
        rx = sikit::dsp::apply_channel(tx, fs, channel);
        grid = sikit::eye::build_eye(rx, kSpu, 128, 96, /*warmup=*/8);
    } catch (const std::exception& e) {
        QMessageBox::critical(this, tr("Eye diagram"), e.what());
        return;
    }

    auto* w = new EyeWindow();
    w->setWindowFlag(Qt::Window);
    w->setAttribute(Qt::WA_DeleteOnClose);
    w->setEye(grid);
    w->setTitleSubtext(
        QString("%1 at 10 Gbps NRZ  (W=%2 mm, L=%3 mm)")
            .arg(net_combo_->currentText())
            .arg(tw * 1e3, 0, 'f', 3)
            .arg(tl * 1e3, 0, 'f', 1));
    w->show();
}

}  // namespace circuitcore::studio
