#include "ColorLegend.h"

#include <algorithm>

#include <QPainter>
#include <QPaintEvent>

namespace {

// Same 5 viridis control points as the PcbCanvas fragment shader. Duplicated
// in C++ for the legend; the values must stay in sync.
QColor viridis(double t) {
    t = std::clamp(t, 0.0, 1.0);
    static const QColor c0(68,  1,  84);
    static const QColor c1(59,  81, 139);
    static const QColor c2(33,  145, 140);
    static const QColor c3(94,  201, 97);
    static const QColor c4(253, 231, 37);
    auto lerp = [](const QColor& a, const QColor& b, double u) {
        return QColor(
            static_cast<int>(a.red()   + (b.red()   - a.red())   * u),
            static_cast<int>(a.green() + (b.green() - a.green()) * u),
            static_cast<int>(a.blue()  + (b.blue()  - a.blue())  * u));
    };
    if (t < 0.25) return lerp(c0, c1, t * 4.0);
    if (t < 0.50) return lerp(c1, c2, (t - 0.25) * 4.0);
    if (t < 0.75) return lerp(c2, c3, (t - 0.50) * 4.0);
    return lerp(c3, c4, (t - 0.75) * 4.0);
}

QString fmt_voltage(double v) {
    const double mv = v * 1000.0;
    return QString::number(mv, 'f', mv < 1.0 ? 4 : 3) + " mV";
}

}  // namespace

ColorLegend::ColorLegend(QWidget* parent) : QWidget(parent) {
    setMinimumWidth(96);
    setMaximumWidth(96);
}

QSize ColorLegend::sizeHint() const { return {96, 240}; }

void ColorLegend::setRange(double v_min, double v_max) {
    v_min_ = v_min;
    v_max_ = v_max;
    update();
}

void ColorLegend::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(20, 20, 23));

    if (v_max_ <= v_min_) {
        p.setPen(QColor(130, 130, 130));
        p.drawText(rect(), Qt::AlignCenter, "no result");
        return;
    }

    const int bar_left   = 10;
    const int bar_right  = 34;
    const int bar_top    = 24;
    const int bar_bottom = height() - 24;
    const int bar_h      = bar_bottom - bar_top;
    if (bar_h < 4) return;

    // Top of the bar = v_max (warm yellow); bottom = v_min (deep purple).
    for (int y = 0; y < bar_h; ++y) {
        const double t = 1.0 - static_cast<double>(y) / static_cast<double>(bar_h - 1);
        p.fillRect(QRect(bar_left, bar_top + y, bar_right - bar_left, 1),
                   viridis(t));
    }
    p.setPen(QColor(80, 80, 90));
    p.drawRect(QRect(bar_left, bar_top, bar_right - bar_left, bar_h));

    p.setPen(QColor(230, 230, 230));
    const QFont f = p.font();
    QFont small = f;
    small.setPointSizeF(f.pointSizeF() - 1.0);
    p.setFont(small);

    p.drawText(bar_right + 6, bar_top + 4,                fmt_voltage(v_max_));
    p.drawText(bar_right + 6, (bar_top + bar_bottom) / 2, fmt_voltage((v_min_ + v_max_) / 2.0));
    p.drawText(bar_right + 6, bar_bottom + 4,             fmt_voltage(v_min_));
}
