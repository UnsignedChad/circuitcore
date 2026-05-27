// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "TransientPlotWidget.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <utility>

TransientPlotWidget::TransientPlotWidget(QWidget* parent) : QWidget(parent) {
    setMinimumSize(220, 120);
}

QSize TransientPlotWidget::sizeHint() const { return {480, 280}; }

void TransientPlotWidget::setData(std::vector<double> times_s,
                                   std::vector<double> v_obs_v,
                                   std::vector<double> v_max_v) {
    t_ = std::move(times_s);
    vobs_ = std::move(v_obs_v);
    vmax_ = std::move(v_max_v);
    update();
}

void TransientPlotWidget::clear() {
    t_.clear();
    vobs_.clear();
    vmax_.clear();
    update();
}

void TransientPlotWidget::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(15, 15, 18));

    constexpr int margin_left = 70;
    constexpr int margin_right = 18;
    constexpr int margin_top = 18;
    constexpr int margin_bottom = 28;
    const int plot_w = width() - margin_left - margin_right;
    const int plot_h = height() - margin_top - margin_bottom;
    if (plot_w < 50 || plot_h < 40) return;

    if (t_.size() < 2 || vobs_.size() != t_.size() || vmax_.size() != t_.size()) {
        p.setPen(QColor(140, 140, 140));
        p.drawText(rect(), Qt::AlignCenter, "no transient data");
        return;
    }

    const double t_min = t_.front();
    const double t_max = t_.back();
    double v_lo = std::numeric_limits<double>::infinity();
    double v_hi = -std::numeric_limits<double>::infinity();
    for (double v : vobs_) { v_lo = std::min(v_lo, v); v_hi = std::max(v_hi, v); }
    for (double v : vmax_) { v_lo = std::min(v_lo, v); v_hi = std::max(v_hi, v); }
    if (!(t_max > t_min) || !(v_hi > v_lo)) {
        // degenerate -- pad y so the line is visible
        v_lo = (v_lo > 0.0) ? v_lo * 0.9 : -1.0e-6;
        v_hi = (v_hi > 0.0) ? v_hi * 1.1 :  1.0e-6;
        if (v_hi <= v_lo) v_hi = v_lo + 1.0e-9;
    }
    // Pad y range a touch for legibility.
    const double pad = 0.05 * (v_hi - v_lo);
    const double y_lo = v_lo - pad;
    const double y_hi = v_hi + pad;

    auto map_x = [&](double t) {
        return margin_left + plot_w * (t - t_min) / (t_max - t_min);
    };
    auto map_y = [&](double v) {
        return margin_top + plot_h * (1.0 - (v - y_lo) / (y_hi - y_lo));
    };

    // Plot frame.
    p.setPen(QColor(80, 80, 90));
    p.drawRect(QRect(margin_left, margin_top, plot_w, plot_h));

    // Gridlines: 5 divisions each axis.
    p.setPen(QColor(40, 40, 48));
    for (int k = 1; k < 5; ++k) {
        const double x = margin_left + plot_w * k / 5;
        p.drawLine(QPointF(x, margin_top), QPointF(x, margin_top + plot_h));
        const double y = margin_top + plot_h * k / 5;
        p.drawLine(QPointF(margin_left, y), QPointF(margin_left + plot_w, y));
    }

    // Axis tick labels.
    auto fmt_t = [](double t) {
        if (t >= 1.0e-3) return QString::number(t * 1.0e3, 'g', 3) + " ms";
        if (t >= 1.0e-6) return QString::number(t * 1.0e6, 'g', 3) + " us";
        if (t >= 1.0e-9) return QString::number(t * 1.0e9, 'g', 3) + " ns";
        return QString::number(t, 'g', 3) + " s";
    };
    auto fmt_v = [](double v) {
        if (std::abs(v) >= 1.0) return QString::number(v, 'f', 3) + " V";
        if (std::abs(v) >= 1.0e-3) return QString::number(v * 1.0e3, 'f', 3) + " mV";
        if (std::abs(v) >= 1.0e-6) return QString::number(v * 1.0e6, 'f', 3) + " uV";
        return QString::number(v, 'g', 3) + " V";
    };
    p.setPen(QColor(210, 210, 215));
    QFont f = p.font();
    QFont small = f;
    small.setPointSizeF(f.pointSizeF() - 1.0);
    p.setFont(small);
    p.drawText(QRect(margin_left - 60, margin_top - 12, 56, 16),
               Qt::AlignRight | Qt::AlignVCenter, fmt_v(y_hi));
    p.drawText(QRect(margin_left - 60, margin_top + plot_h - 4, 56, 16),
               Qt::AlignRight | Qt::AlignVCenter, fmt_v(y_lo));
    p.drawText(QRect(margin_left - 30, margin_top + plot_h + 4, 80, 16),
               Qt::AlignLeft, fmt_t(t_min));
    p.drawText(QRect(margin_left + plot_w - 80, margin_top + plot_h + 4, 80, 16),
               Qt::AlignRight, fmt_t(t_max));

    // Curve helper.
    auto draw_curve = [&](const std::vector<double>& y, QColor color) {
        p.setPen(QPen(color, 1.5));
        QPainterPath path;
        bool started = false;
        for (std::size_t i = 0; i < t_.size(); ++i) {
            const QPointF pt(map_x(t_[i]), map_y(y[i]));
            if (!started) { path.moveTo(pt); started = true; }
            else            path.lineTo(pt);
        }
        p.drawPath(path);
    };

    draw_curve(vmax_, QColor(0x77, 0x88, 0xaa));
    draw_curve(vobs_, QColor(0xfd, 0xe7, 0x25));

    // Legend top-right.
    p.setPen(QPen(QColor(0xfd, 0xe7, 0x25), 2.0));
    p.drawLine(margin_left + plot_w - 130, margin_top + 8,
               margin_left + plot_w - 108, margin_top + 8);
    p.setPen(QColor(225, 225, 225));
    p.drawText(QRect(margin_left + plot_w - 104, margin_top, 100, 14),
               Qt::AlignLeft | Qt::AlignVCenter, "observation");
    p.setPen(QPen(QColor(0x77, 0x88, 0xaa), 2.0));
    p.drawLine(margin_left + plot_w - 130, margin_top + 22,
               margin_left + plot_w - 108, margin_top + 22);
    p.setPen(QColor(225, 225, 225));
    p.drawText(QRect(margin_left + plot_w - 104, margin_top + 14, 100, 14),
               Qt::AlignLeft | Qt::AlignVCenter, "max |V|");
}
