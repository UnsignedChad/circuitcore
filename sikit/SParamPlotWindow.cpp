// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "SParamPlotWindow.h"

#include "si/SParam.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QVBoxLayout>
#include <utility>

namespace {

constexpr int kPlotMarginL = 60;
constexpr int kPlotMarginR = 20;
constexpr int kPlotMarginT = 16;
constexpr int kPlotMarginB = 36;

// Cycle of distinguishable colors for up to ~16 curves.
QColor curve_color(int idx) {
    static const QColor palette[] = {
        QColor(99,  179, 237),   // light blue
        QColor(245, 130, 49),    // orange
        QColor(72,  207, 173),   // teal
        QColor(237, 100, 149),   // pink
        QColor(202, 178, 214),   // lavender
        QColor(253, 218, 36),    // yellow
        QColor(166, 86,  40),    // brown
        QColor(141, 211, 199),   // mint
        QColor(190, 186, 218),   // light purple
        QColor(251, 128, 114),   // salmon
        QColor(128, 177, 211),   // sky
        QColor(253, 180, 98),    // peach
        QColor(179, 222, 105),   // lime
        QColor(252, 205, 229),   // rose
        QColor(217, 217, 217),   // grey
        QColor(188, 128, 189),   // mauve
    };
    constexpr int n = sizeof(palette) / sizeof(palette[0]);
    return palette[idx % n];
}

// Unwrap phase to remove 2π jumps so group-delay computation behaves.
std::vector<double> unwrap_phase(const std::vector<double>& phase) {
    std::vector<double> out = phase;
    for (std::size_t i = 1; i < out.size(); ++i) {
        double d = out[i] - out[i - 1];
        while (d > std::numbers::pi)  { out[i] -= 2.0 * std::numbers::pi; d = out[i] - out[i - 1]; }
        while (d < -std::numbers::pi) { out[i] += 2.0 * std::numbers::pi; d = out[i] - out[i - 1]; }
    }
    return out;
}

// Format a Hz value as a short label, switching unit by magnitude.
QString freq_label(double hz) {
    if (hz >= 1e9) return QString("%1G").arg(hz / 1e9, 0, 'g', 3);
    if (hz >= 1e6) return QString("%1M").arg(hz / 1e6, 0, 'g', 3);
    if (hz >= 1e3) return QString("%1k").arg(hz / 1e3, 0, 'g', 3);
    return QString("%1").arg(hz, 0, 'g', 3);
}

// Inner canvas widget. paintEvent delegates to the owning window so the
// window can pull from its own state without exposing it publicly.
class SParamPlotCanvas : public QWidget {
public:
    explicit SParamPlotCanvas(SParamPlotWindow* owner)
        : owner_(owner) {
        setMinimumSize(360, 200);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        owner_->paintPlotInto(this);
    }

private:
    SParamPlotWindow* owner_;
};

}  // namespace

SParamPlotWindow::SParamPlotWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle("S-parameters");
    setMinimumSize(480, 320);
    resize(880, 560);
    setAttribute(Qt::WA_DeleteOnClose);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    // Top: mode combo + (spacer eventually).
    auto* top = new QHBoxLayout();
    top->setSpacing(8);
    top->addWidget(new QLabel("Y axis:"));
    mode_combo_ = new QComboBox(this);
    mode_combo_->addItem("Magnitude (dB)");
    mode_combo_->addItem("Phase (deg, unwrapped)");
    mode_combo_->addItem("Group delay (ns)");
    mode_combo_->addItem("TDR — impedance (Ω) vs time");
    mode_combo_->addItem("TDT — step response vs time");
    connect(mode_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SParamPlotWindow::onModeChanged);
    top->addWidget(mode_combo_);
    mixed_mode_check_ = new QCheckBox("Mixed-mode (Sdd / Sdc / Scd / Scc)", this);
    mixed_mode_check_->setVisible(false);
    connect(mixed_mode_check_, &QCheckBox::toggled,
            this, &SParamPlotWindow::onMixedModeToggled);
    top->addSpacing(16);
    top->addWidget(mixed_mode_check_);
    top->addStretch();
    outer->addLayout(top);

    // Middle: per-curve checkbox grid (rebuilt when data is set).
    curves_holder_ = new QWidget(this);
    curves_grid_ = new QGridLayout(curves_holder_);
    curves_grid_->setContentsMargins(0, 0, 0, 0);
    curves_grid_->setHorizontalSpacing(10);
    curves_grid_->setVerticalSpacing(2);
    outer->addWidget(curves_holder_);

    // Body: plot canvas, expanding.
    plot_canvas_ = new SParamPlotCanvas(this);
    plot_canvas_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    outer->addWidget(plot_canvas_, /*stretch=*/1);

    // Bottom: caption with peak / range info.
    caption_ = new QLabel(this);
    caption_->setStyleSheet("color: #b0b0b8;");
    outer->addWidget(caption_);
}

void SParamPlotWindow::setData(const sikit::touchstone::TouchstoneFile& ts) {
    ts_ = ts;
    ts_se_ = ts;       // remember the single-ended baseline for later toggle
    if (mixed_mode_check_) mixed_mode_check_->setChecked(false);
    rebuildCurveCheckboxes();
    plot_canvas_->update();
}


void SParamPlotWindow::setOverlay(const sikit::touchstone::TouchstoneFile& ts,
                                    const std::string& label) {
    overlay_ts_ = ts;
    overlay_label_ = label;
    plot_canvas_->update();
}

void SParamPlotWindow::clearOverlay() {
    overlay_ts_ = {};
    overlay_label_.clear();
    plot_canvas_->update();
}

void SParamPlotWindow::setTitleSubtext(const QString& text) {
    setWindowTitle(QString("S-parameters — %1").arg(text));
}

void SParamPlotWindow::onModeChanged(int idx) {
    mode_ = static_cast<YMode>(idx);
    plot_canvas_->update();
}

void SParamPlotWindow::onCurveToggled() {
    plot_canvas_->update();
}

void SParamPlotWindow::rebuildCurveCheckboxes() {
    // Tear down old.
    for (auto* c : curve_checks_) c->deleteLater();
    curve_checks_.clear();
    while (auto* item = curves_grid_->takeAt(0)) delete item;

    const int N = ts_.num_ports;
    if (N <= 0) return;

    // Lay the boxes out as an N×N grid so port-to-port relations are
    // visually obvious (row = output, col = input -- same convention as
    // Touchstone storage).
    int curveIdx = 0;
    for (int r = 0; r < N; ++r) {
        for (int c = 0; c < N; ++c) {
            // In mixed-mode the rows/cols map to (d1, d2, c1, c2).
            // Label them as Sxx_ij where xx in {dd, dc, cd, cc}.
            QString label;
            if (mixed_mode_check_ && mixed_mode_check_->isChecked() &&
                mm_avail_ != MixedModeAvailability::Unavailable && N == 4) {
                static const char* names[4] = {"d1", "d2", "c1", "c2"};
                const int rt = r / 2;  // 0=d-block, 1=c-block on output side
                const int ct = c / 2;  // 0=d-block, 1=c-block on input side
                static const char* blk[2][2] = {{"dd", "dc"}, {"cd", "cc"}};
                const int ri = r % 2 + 1;  // port index within block
                const int ci = c % 2 + 1;
                label = QString("S%1%2%3").arg(blk[rt][ct]).arg(ri).arg(ci);
                (void)names;
            } else {
                label = QString("S%1%2").arg(r + 1).arg(c + 1);
            }
            auto* cb = new QCheckBox(label, curves_holder_);
            // Default visibility: diagonal + S21 + S12 (the usual interesting
            // 2-port set). For larger files leave them unchecked so the plot
            // doesn't start as a 16-curve mess.
            if (N <= 2 || r == c || (r == 1 && c == 0) || (r == 0 && c == 1)) {
                cb->setChecked(true);
            }
            QColor col = curve_color(curveIdx);
            cb->setStyleSheet(QString("color: rgb(%1,%2,%3); font-weight: bold;")
                                  .arg(col.red()).arg(col.green()).arg(col.blue()));
            connect(cb, &QCheckBox::toggled, this, &SParamPlotWindow::onCurveToggled);
            curves_grid_->addWidget(cb, r, c);
            curve_checks_.push_back(cb);
            ++curveIdx;
        }
    }
}

void SParamPlotWindow::paintPlotInto(QWidget* target) {
    if (mode_ == YMode::TdrImpedance || mode_ == YMode::TdtAmplitude) {
        paintTimeDomainInto(target);
        return;
    }
    QPainter p(target);
    p.fillRect(target->rect(), QColor(20, 20, 26));
    if (ts_.num_ports <= 0 || ts_.frequencies.empty()) {
        p.setPen(Qt::white);
        p.drawText(target->rect(), Qt::AlignCenter, "No S-parameter data");
        return;
    }

    const int N = ts_.num_ports;
    const QRect plot(kPlotMarginL, kPlotMarginT,
                     target->width()  - kPlotMarginL - kPlotMarginR,
                     target->height() - kPlotMarginT - kPlotMarginB);
    if (plot.width() < 20 || plot.height() < 20) return;

    // ----- X axis: log10(freq). Skip any non-positive frequencies. -----
    double fmin = 0, fmax = 0;
    bool have_pos = false;
    for (double f : ts_.frequencies) {
        if (f <= 0.0) continue;
        if (!have_pos) { fmin = fmax = f; have_pos = true; }
        else { fmin = std::min(fmin, f); fmax = std::max(fmax, f); }
    }
    if (!have_pos || fmin == fmax) {
        p.setPen(Qt::white);
        p.drawText(target->rect(), Qt::AlignCenter, "No usable frequency points");
        return;
    }
    const double lx_min = std::log10(fmin);
    const double lx_max = std::log10(fmax);
    auto x_of = [&](double f) {
        const double lx = std::log10(std::max(f, fmin));
        const double t = (lx - lx_min) / (lx_max - lx_min);
        return plot.left() + t * plot.width();
    };

    // ----- Compute per-curve sample arrays (only enabled curves). -----
    struct Curve {
        int idx;            // r*N+c
        std::vector<double> y;
    };
    auto build_curves = [&](const sikit::touchstone::TouchstoneFile& src) {
        std::vector<Curve> out;
        if (src.num_ports != N || src.frequencies.empty()) return out;
        for (std::size_t i = 0; i < curve_checks_.size(); ++i) {
            if (!curve_checks_[i] || !curve_checks_[i]->isChecked()) continue;
            Curve cv;
            cv.idx = static_cast<int>(i);
            const int r = cv.idx / N;
            const int c = cv.idx % N;
            const std::size_t K = src.frequencies.size();
            cv.y.reserve(K);

            if (mode_ == YMode::MagnitudeDb) {
                for (std::size_t k = 0; k < K; ++k) {
                    const auto z = src.s_matrices[k][r + c * N];
                    const double m = std::abs(z);
                    cv.y.push_back(20.0 * std::log10(std::max(m, 1e-18)));
                }
            } else if (mode_ == YMode::PhaseDeg) {
                std::vector<double> phase(K);
                for (std::size_t k = 0; k < K; ++k) {
                    const auto z = src.s_matrices[k][r + c * N];
                    phase[k] = std::arg(z);
                }
                auto up = unwrap_phase(phase);
                for (double v : up) cv.y.push_back(v * 180.0 / std::numbers::pi);
            } else {
                std::vector<double> phase(K);
                for (std::size_t k = 0; k < K; ++k) {
                    const auto z = src.s_matrices[k][r + c * N];
                    phase[k] = std::arg(z);
                }
                auto up = unwrap_phase(phase);
                cv.y.assign(K, 0.0);
                for (std::size_t k = 1; k + 1 < K; ++k) {
                    const double dphi = up[k + 1] - up[k - 1];
                    const double dw   = 2.0 * std::numbers::pi *
                                        (src.frequencies[k + 1] -
                                          src.frequencies[k - 1]);
                    if (std::abs(dw) > 0.0) {
                        cv.y[k] = -dphi / dw * 1e9;
                    }
                }
                if (K >= 2) {
                    cv.y[0]     = cv.y[std::min<std::size_t>(1, K - 1)];
                    cv.y[K - 1] = cv.y[K - 2];
                }
            }
            out.push_back(std::move(cv));
        }
        return out;
    };
    std::vector<Curve> curves = build_curves(ts_);
    std::vector<Curve> overlay_curves = build_curves(overlay_ts_);

    if (curves.empty()) {
        p.setPen(QColor(140, 140, 150));
        p.drawText(plot, Qt::AlignCenter, "No curves selected");
        // Still draw the axes box.
    }

    // ----- Y axis: auto-range across selected curves, with a sensible
    //       floor for the dB view so a passive S21 never sinks to -200dB. ---
    double y_min = +1e300, y_max = -1e300;
    auto scan_for_range = [&](const std::vector<Curve>& cs) {
        for (const auto& cv : cs) {
            for (double v : cv.y) {
                if (!std::isfinite(v)) continue;
                y_min = std::min(y_min, v);
                y_max = std::max(y_max, v);
            }
        }
    };
    scan_for_range(curves);
    scan_for_range(overlay_curves);
    if ((curves.empty() && overlay_curves.empty()) ||
        !std::isfinite(y_min) || !std::isfinite(y_max) ||
        y_min == y_max) {
        // Default windows so the empty axes are still nicely labeled.
        if (mode_ == YMode::MagnitudeDb)  { y_min = -60; y_max = 0; }
        else if (mode_ == YMode::PhaseDeg){ y_min = -180; y_max = 180; }
        else                               { y_min = -1; y_max = 1; }
    } else {
        // Padding.
        const double pad = 0.05 * (y_max - y_min);
        y_min -= pad;
        y_max += pad;
        if (mode_ == YMode::MagnitudeDb) {
            y_max = std::min(y_max, 6.0);     // S-params don't exceed 0 dB by much
            y_min = std::max(y_min, -120.0);  // and -120 dB is below dynamic range
        }
    }
    auto y_of = [&](double v) {
        v = std::clamp(v, y_min, y_max);
        const double t = (v - y_min) / (y_max - y_min);
        return plot.bottom() - t * plot.height();
    };

    // ----- Gridlines + axis labels. -----
    p.setPen(QColor(50, 50, 60));
    // Vertical decade lines.
    const int dec_lo = static_cast<int>(std::floor(lx_min));
    const int dec_hi = static_cast<int>(std::ceil(lx_max));
    for (int d = dec_lo; d <= dec_hi; ++d) {
        for (int sub = 1; sub <= 9; ++sub) {
            const double f = sub * std::pow(10.0, d);
            if (f < fmin || f > fmax) continue;
            const double x = x_of(f);
            p.setPen(sub == 1 ? QColor(70, 70, 80) : QColor(40, 40, 50));
            p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
        }
    }
    // Horizontal gridlines: 5 divisions.
    p.setPen(QColor(50, 50, 60));
    for (int i = 0; i <= 5; ++i) {
        const double y = plot.top() + plot.height() * (i / 5.0);
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    // Frame.
    p.setPen(QColor(140, 140, 150));
    p.drawRect(plot);

    // X labels at decade boundaries.
    p.setPen(QColor(200, 200, 210));
    for (int d = dec_lo; d <= dec_hi; ++d) {
        const double f = std::pow(10.0, d);
        if (f < fmin * 0.999 || f > fmax * 1.001) continue;
        const double x = x_of(f);
        p.drawText(QRectF(x - 30, plot.bottom() + 4, 60, 18),
                   Qt::AlignHCenter | Qt::AlignTop, freq_label(f));
    }
    p.drawText(QRectF(plot.left(), plot.bottom() + 20, plot.width(), 16),
               Qt::AlignHCenter, "Frequency (Hz)");

    // Y labels.
    for (int i = 0; i <= 5; ++i) {
        const double v = y_max - (y_max - y_min) * (i / 5.0);
        const double y = plot.top() + plot.height() * (i / 5.0);
        QString lbl = QString::number(v, 'f', (mode_ == YMode::GroupDelayNs) ? 2 : 1);
        p.drawText(QRectF(2, y - 8, kPlotMarginL - 6, 16),
                   Qt::AlignRight | Qt::AlignVCenter, lbl);
    }
    QString y_title;
    switch (mode_) {
        case YMode::MagnitudeDb:   y_title = "|S| (dB)"; break;
        case YMode::PhaseDeg:      y_title = "phase (deg)"; break;
        case YMode::GroupDelayNs:  y_title = "τg (ns)"; break;
        case YMode::TdrImpedance:  y_title = "Z (Ω)"; break;
        case YMode::TdtAmplitude:  y_title = "step"; break;
    }
    // Rotated y-axis title.
    p.save();
    p.translate(14, plot.center().y());
    p.rotate(-90);
    p.drawText(QRectF(-80, -8, 160, 16), Qt::AlignHCenter, y_title);
    p.restore();

    // ----- Curves. Primary solid; overlay dashed in the same colors. -----
    auto paint_curves = [&](const std::vector<Curve>& cs,
                              const sikit::touchstone::TouchstoneFile& src,
                              Qt::PenStyle style) {
        for (const auto& cv : cs) {
            QPen pen(curve_color(cv.idx));
            pen.setWidthF(1.6);
            pen.setStyle(style);
            p.setPen(pen);
            QPolygonF poly;
            poly.reserve(static_cast<int>(cv.y.size()));
            for (std::size_t k = 0; k < cv.y.size(); ++k) {
                const double f = src.frequencies[k];
                if (f <= 0.0) continue;
                if (!std::isfinite(cv.y[k])) continue;
                poly << QPointF(x_of(f), y_of(cv.y[k]));
            }
            if (poly.size() >= 2) p.drawPolyline(poly);
        }
    };
    paint_curves(curves, ts_, Qt::SolidLine);
    paint_curves(overlay_curves, overlay_ts_, Qt::DashLine);

    // ----- Caption: N ports, K frequencies, freq range, current Y range. ---
    QString cap = QString("%1-port, %2 freq pts, [%3 .. %4]")
                      .arg(N)
                      .arg(ts_.frequencies.size())
                      .arg(freq_label(fmin))
                      .arg(freq_label(fmax));
    cap += QString("    Y ∈ [%1, %2] %3")
               .arg(y_min, 0, 'f', 2).arg(y_max, 0, 'f', 2).arg(y_title);
    if (!overlay_label_.empty()) {
        cap += QString("    overlay (dashed): %1")
                   .arg(QString::fromStdString(overlay_label_));
    }
    caption_->setText(cap);
}

// ---------- Mixed-mode (Tier 1.2) ---------------------------------------

void SParamPlotWindow::setMixedModeAvailable(MixedModeAvailability v) {
    mm_avail_ = v;
    if (mixed_mode_check_) {
        mixed_mode_check_->setVisible(v != MixedModeAvailability::Unavailable);
        if (v == MixedModeAvailability::Unavailable) {
            mixed_mode_check_->setChecked(false);
        }
    }
}

void SParamPlotWindow::onMixedModeToggled(bool /*on*/) {
    applyMixedModeIfRequested();
    rebuildCurveCheckboxes();
    plot_canvas_->update();
}

void SParamPlotWindow::applyMixedModeIfRequested() {
    if (!mixed_mode_check_ || !mixed_mode_check_->isChecked() ||
        mm_avail_ == MixedModeAvailability::Unavailable ||
        ts_se_.num_ports != 4) {
        // Restore the original single-ended data.
        ts_ = ts_se_;
        return;
    }
    const auto order = (mm_avail_ == MixedModeAvailability::PortOrderPPNN)
                           ? sikit::sparam::PortOrder::PPNN
                           : sikit::sparam::PortOrder::PNPN;
    try {
        ts_ = sikit::sparam::to_mixed_mode(ts_se_, order);
    } catch (...) {
        ts_ = ts_se_;  // give up silently; checkbox stays checked but unused
    }
}

// ---------- Time-domain plot (Tier 1.3) ---------------------------------

void SParamPlotWindow::paintTimeDomainInto(QWidget* target) {
    QPainter p(target);
    p.fillRect(target->rect(), QColor(20, 20, 26));
    if (ts_.num_ports <= 0 || ts_.frequencies.size() < 2) {
        p.setPen(Qt::white);
        p.drawText(target->rect(), Qt::AlignCenter, "Need at least two freq points for time-domain view");
        return;
    }

    const int N = ts_.num_ports;
    const QRect plot(kPlotMarginL, kPlotMarginT,
                     target->width()  - kPlotMarginL - kPlotMarginR,
                     target->height() - kPlotMarginT - kPlotMarginB);
    if (plot.width() < 20 || plot.height() < 20) return;

    // Compute curves for every checked S_ij. In TDR mode we skip off-
    // diagonals (no meaningful Z-impedance from S21). In TDT mode we
    // skip the diagonals.
    struct Curve {
        int idx;
        std::vector<double> t;     // seconds
        std::vector<double> y;     // ohms or amplitude
        QString label;
    };
    std::vector<Curve> curves;
    const std::size_t K = ts_.frequencies.size();
    for (std::size_t i = 0; i < curve_checks_.size(); ++i) {
        if (!curve_checks_[i] || !curve_checks_[i]->isChecked()) continue;
        const int r = static_cast<int>(i) / N;
        const int c = static_cast<int>(i) % N;
        const bool diag = (r == c);
        if (mode_ == YMode::TdrImpedance && !diag) continue;
        if (mode_ == YMode::TdtAmplitude &&  diag) continue;

        std::vector<sikit::sparam::Complex> sij(K);
        for (std::size_t k = 0; k < K; ++k) {
            sij[k] = ts_.s_matrices[k][r + c * N];
        }
        sikit::sparam::TdrResult tr;
        if (mode_ == YMode::TdrImpedance) {
            tr = sikit::sparam::tdr_step_response(
                ts_.frequencies, sij, ts_.reference_impedance);
        } else {
            tr = sikit::sparam::tdt_step_response(ts_.frequencies, sij);
        }
        if (tr.time.empty()) continue;

        Curve cv;
        cv.idx = static_cast<int>(i);
        cv.t   = std::move(tr.time);
        cv.y   = std::move(tr.value);
        cv.label = curve_checks_[i]->text();
        curves.push_back(std::move(cv));
    }

    // X-axis: linear time, in nanoseconds. Cap the max time to where the
    // step response has settled — first ~10 ns is what people look at for
    // SI; let the auto-range stretch if the trace is long.
    double t_max = 0.0;
    for (const auto& cv : curves) {
        if (!cv.t.empty()) t_max = std::max(t_max, cv.t.back());
    }
    if (t_max <= 0.0) t_max = 1e-9;
    // Display from 0 to t_max, rounded up to a tidy ns value.
    const double t_max_ns = t_max * 1e9;
    auto x_of = [&](double t_sec) {
        const double t_ns = t_sec * 1e9;
        const double tt = t_ns / t_max_ns;
        return plot.left() + tt * plot.width();
    };

    // Y-axis: auto-range.
    double y_min = +1e300, y_max = -1e300;
    for (const auto& cv : curves) {
        for (double v : cv.y) {
            if (!std::isfinite(v)) continue;
            y_min = std::min(y_min, v);
            y_max = std::max(y_max, v);
        }
    }
    if (curves.empty() || !std::isfinite(y_min) || y_min == y_max) {
        if (mode_ == YMode::TdrImpedance) { y_min = 30; y_max = 80; }
        else                               { y_min = -0.1; y_max = 1.1; }
    } else {
        const double pad = 0.08 * (y_max - y_min);
        y_min -= pad;
        y_max += pad;
    }
    auto y_of = [&](double v) {
        v = std::clamp(v, y_min, y_max);
        const double t = (v - y_min) / (y_max - y_min);
        return plot.bottom() - t * plot.height();
    };

    // Grid + axes.
    p.setPen(QColor(50, 50, 60));
    for (int i = 0; i <= 5; ++i) {
        const double y = plot.top() + plot.height() * (i / 5.0);
        p.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
    }
    for (int i = 0; i <= 10; ++i) {
        const double x = plot.left() + plot.width() * (i / 10.0);
        p.setPen(i == 0 || i == 10 || i == 5 ? QColor(70, 70, 80) : QColor(40, 40, 50));
        p.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
    }
    p.setPen(QColor(140, 140, 150));
    p.drawRect(plot);

    // X labels — divide t_max_ns into ~10 ticks.
    p.setPen(QColor(200, 200, 210));
    for (int i = 0; i <= 10; ++i) {
        const double tn = t_max_ns * (i / 10.0);
        const double x = plot.left() + plot.width() * (i / 10.0);
        p.drawText(QRectF(x - 30, plot.bottom() + 4, 60, 18),
                   Qt::AlignHCenter | Qt::AlignTop,
                   QString::number(tn, 'f', 2));
    }
    p.drawText(QRectF(plot.left(), plot.bottom() + 20, plot.width(), 16),
               Qt::AlignHCenter, "time (ns)");

    // Y labels.
    for (int i = 0; i <= 5; ++i) {
        const double v = y_max - (y_max - y_min) * (i / 5.0);
        const double y = plot.top() + plot.height() * (i / 5.0);
        QString lbl = QString::number(v, 'f', (mode_ == YMode::TdtAmplitude) ? 2 : 1);
        p.drawText(QRectF(2, y - 8, kPlotMarginL - 6, 16),
                   Qt::AlignRight | Qt::AlignVCenter, lbl);
    }
    // Y-axis title rotated.
    p.save();
    p.translate(14, plot.center().y());
    p.rotate(-90);
    p.drawText(QRectF(-80, -8, 160, 16), Qt::AlignHCenter,
               (mode_ == YMode::TdrImpedance) ? "Z (Ω)" : "step");
    p.restore();

    // Curves.
    for (const auto& cv : curves) {
        QPen pen(curve_color(cv.idx));
        pen.setWidthF(1.6);
        p.setPen(pen);
        QPolygonF poly;
        poly.reserve(static_cast<int>(cv.y.size()));
        for (std::size_t i = 0; i < cv.y.size(); ++i) {
            if (!std::isfinite(cv.y[i])) continue;
            if (cv.t[i] > t_max) break;
            poly << QPointF(x_of(cv.t[i]), y_of(cv.y[i]));
        }
        if (poly.size() >= 2) p.drawPolyline(poly);
    }

    // Caption.
    QString cap = QString("%1-port, %2 freq pts → IFFT step response, "
                          "t ∈ [0, %3 ns]")
                      .arg(N)
                      .arg(ts_.frequencies.size())
                      .arg(t_max_ns, 0, 'f', 1);
    if (mode_ == YMode::TdrImpedance) {
        cap += QString("    Z ∈ [%1, %2] Ω  (ref Z₀=%3Ω)")
                   .arg(y_min, 0, 'f', 1)
                   .arg(y_max, 0, 'f', 1)
                   .arg(ts_.reference_impedance, 0, 'f', 1);
        cap += "    note: absolute level may float by a DC offset (no DC point in measured band)";
    } else {
        cap += QString("    step ∈ [%1, %2]")
                   .arg(y_min, 0, 'f', 3)
                   .arg(y_max, 0, 'f', 3);
    }
    caption_->setText(cap);
}
