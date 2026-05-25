#include "ZfPlotWidget.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>
#include <utility>

ZfPlotWidget::ZfPlotWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(220, 120);
}

QSize ZfPlotWidget::sizeHint() const { return {480, 280}; }

void ZfPlotWidget::setData(std::vector<double> freqs_hz,
                            std::vector<double> z_mag_ohm) {
    Curve c;
    c.freqs = std::move(freqs_hz);
    c.mags  = std::move(z_mag_ohm);
    c.color = QColor(0xfd, 0xe7, 0x25);
    curves_.clear();
    curves_.push_back(std::move(c));
    update();
}

void ZfPlotWidget::setCurves(std::vector<Curve> curves) {
    curves_ = std::move(curves);
    update();
}

void ZfPlotWidget::setTargetImpedance(double z_ohm) {
    target_z_ = z_ohm;
    update();
}

void ZfPlotWidget::clear() {
    curves_.clear();
    update();
}

namespace {

QString fmt_freq(double f) {
    if (f >= 1.0e9) return QString::number(f / 1.0e9, 'g', 3) + " GHz";
    if (f >= 1.0e6) return QString::number(f / 1.0e6, 'g', 3) + " MHz";
    if (f >= 1.0e3) return QString::number(f / 1.0e3, 'g', 3) + " kHz";
    return QString::number(f, 'g', 3) + " Hz";
}

QString fmt_z(double z) {
    if (z >= 1.0)    return QString::number(z, 'g', 3) + " \xCE\xA9";       // Ω
    if (z >= 1.0e-3) return QString::number(z * 1.0e3, 'g', 3) + " m\xCE\xA9";
    if (z >= 1.0e-6) return QString::number(z * 1.0e6, 'g', 3) + " \xC2\xB5\xCE\xA9";
    return QString::number(z, 'g', 3) + " \xCE\xA9";
}

}  // namespace

void ZfPlotWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(15, 15, 18));

    constexpr int margin_left   = 70;
    constexpr int margin_right  = 18;
    constexpr int margin_top    = 18;
    constexpr int margin_bottom = 28;
    const int plot_w = width() - margin_left - margin_right;
    const int plot_h = height() - margin_top - margin_bottom;
    if (plot_w < 50 || plot_h < 40) return;

    bool any = false;
    for (const auto& c : curves_) {
        if (c.freqs.size() >= 2 && c.freqs.size() == c.mags.size()) { any = true; break; }
    }
    if (!any) {
        p.setPen(QColor(140, 140, 140));
        p.drawText(rect(), Qt::AlignCenter, "no Z(f) data");
        return;
    }

    // Log-log axis ranges across all curves (and target line if set).
    double f_min = std::numeric_limits<double>::infinity();
    double f_max = -std::numeric_limits<double>::infinity();
    double z_min = std::numeric_limits<double>::infinity();
    double z_max = -std::numeric_limits<double>::infinity();
    for (const auto& c : curves_) {
        for (std::size_t i = 0; i < c.freqs.size(); ++i) {
            const double f = c.freqs[i], z = c.mags[i];
            if (f <= 0.0 || z <= 0.0) continue;
            f_min = std::min(f_min, f);
            f_max = std::max(f_max, f);
            z_min = std::min(z_min, z);
            z_max = std::max(z_max, z);
        }
    }
    if (target_z_ > 0.0) {
        z_min = std::min(z_min, target_z_);
        z_max = std::max(z_max, target_z_);
    }
    if (!(f_max > f_min) || !(z_max > z_min)) return;

    const double lf_min = std::log10(f_min);
    const double lf_max = std::log10(f_max);
    const double lz_min = std::log10(z_min) - 0.05 * (std::log10(z_max) - std::log10(z_min) + 1e-12);
    const double lz_max = std::log10(z_max) + 0.05 * (std::log10(z_max) - std::log10(z_min) + 1e-12);

    auto map_x = [&](double f) {
        return margin_left + plot_w * (std::log10(f) - lf_min) / (lf_max - lf_min);
    };
    auto map_y = [&](double z) {
        return margin_top + plot_h * (1.0 - (std::log10(z) - lz_min) / (lz_max - lz_min));
    };

    // Plot frame.
    p.setPen(QColor(80, 80, 90));
    p.drawRect(QRect(margin_left, margin_top, plot_w, plot_h));

    // Decade gridlines.
    p.setPen(QColor(40, 40, 48));
    for (int e = static_cast<int>(std::floor(lf_min));
         e <= static_cast<int>(std::ceil(lf_max)); ++e) {
        const double x = map_x(std::pow(10.0, e));
        if (x >= margin_left - 1 && x <= margin_left + plot_w + 1) {
            p.drawLine(QPointF(x, margin_top), QPointF(x, margin_top + plot_h));
        }
    }
    for (int e = static_cast<int>(std::floor(lz_min));
         e <= static_cast<int>(std::ceil(lz_max)); ++e) {
        const double y = map_y(std::pow(10.0, e));
        if (y >= margin_top - 1 && y <= margin_top + plot_h + 1) {
            p.drawLine(QPointF(margin_left, y), QPointF(margin_left + plot_w, y));
        }
    }

    // Axis tick labels (just decade endpoints).
    p.setPen(QColor(210, 210, 215));
    QFont f = p.font();
    f.setPointSizeF(f.pointSizeF() - 1.0);
    p.setFont(f);
    p.drawText(QRect(margin_left - 60, margin_top - 12, 56, 16),
               Qt::AlignRight | Qt::AlignVCenter, fmt_z(std::pow(10.0, lz_max)));
    p.drawText(QRect(margin_left - 60, margin_top + plot_h - 4, 56, 16),
               Qt::AlignRight | Qt::AlignVCenter, fmt_z(std::pow(10.0, lz_min)));
    p.drawText(QRect(margin_left - 30, margin_top + plot_h + 4, 80, 16),
               Qt::AlignLeft, fmt_freq(f_min));
    p.drawText(QRect(margin_left + plot_w - 80, margin_top + plot_h + 4, 80, 16),
               Qt::AlignRight, fmt_freq(f_max));

    // Target line.
    if (target_z_ > 0.0) {
        const double y = map_y(target_z_);
        p.setPen(QPen(QColor(220, 80, 80), 1.0, Qt::DashLine));
        p.drawLine(QPointF(margin_left, y), QPointF(margin_left + plot_w, y));
        p.setPen(QColor(220, 80, 80));
        p.drawText(QRectF(margin_left + plot_w - 80, y - 14, 76, 12),
                   Qt::AlignRight | Qt::AlignVCenter,
                   QString("target ") + fmt_z(target_z_));
    }

    // Curves.
    for (const auto& c : curves_) {
        if (c.freqs.size() < 2 || c.freqs.size() != c.mags.size()) continue;
        p.setPen(QPen(c.color, 1.5));
        QPainterPath path;
        bool started = false;
        for (std::size_t i = 0; i < c.freqs.size(); ++i) {
            const double freq = c.freqs[i], z = c.mags[i];
            if (freq <= 0.0 || z <= 0.0) continue;
            const QPointF pt(map_x(freq), map_y(z));
            if (!started) { path.moveTo(pt); started = true; }
            else            path.lineTo(pt);
        }
        p.drawPath(path);
    }

    // Legend in top-right if any curve carries a label.
    int legend_y = margin_top + 4;
    for (const auto& c : curves_) {
        if (c.label.isEmpty()) continue;
        const int x = margin_left + plot_w - 130;
        p.setPen(QPen(c.color, 2.0));
        p.drawLine(x, legend_y + 6, x + 22, legend_y + 6);
        p.setPen(QColor(225, 225, 225));
        p.drawText(QRect(x + 26, legend_y, 110, 14), Qt::AlignLeft | Qt::AlignVCenter, c.label);
        legend_y += 14;
    }
}
