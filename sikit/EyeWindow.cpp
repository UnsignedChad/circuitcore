#include "EyeWindow.h"

#include <algorithm>
#include <cmath>

#include <QColor>
#include <QImage>
#include <QPainter>
#include <QPolygonF>
#include "si/EyeMetrics.h"

namespace {

QColor viridis(double t) {
    t = std::clamp(t, 0.0, 1.0);
    constexpr struct { double r, g, b; } stops[5] = {
        {0.267, 0.005, 0.329},
        {0.231, 0.318, 0.545},
        {0.127, 0.567, 0.550},
        {0.369, 0.789, 0.382},
        {0.992, 0.906, 0.144},
    };
    const double seg = t * 4.0;
    int i = static_cast<int>(std::floor(seg));
    if (i >= 4) i = 3;
    const double f = seg - i;
    const auto a = stops[i];
    const auto b = stops[i + 1];
    return QColor::fromRgbF(a.r + f * (b.r - a.r),
                            a.g + f * (b.g - a.g),
                            a.b + f * (b.b - a.b),
                            1.0);
}

}  // namespace

EyeWindow::EyeWindow(QWidget* parent) : QWidget(parent) {
    setWindowTitle("Eye Diagram");
    // Window may be freely resized; below this floor the axis labels and
    // caption start clipping but the diagram itself still renders.
    setMinimumSize(320, 240);
    resize(640, 480);
    setAttribute(Qt::WA_DeleteOnClose);
}

void EyeWindow::setEye(const sikit::eye::EyeGrid& grid) {
    eye_ = grid;
    update();
}

void EyeWindow::setMask(const sikit::specs::EyeMask* mask) {
    mask_ = mask;
    update();
}

void EyeWindow::setTitleSubtext(const QString& text) {
    setWindowTitle(QString("Eye Diagram — %1").arg(text));
}

void EyeWindow::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), QColor(20, 20, 26));

    if (eye_.time_bins <= 0 || eye_.volt_bins <= 0 ||
        eye_.counts.empty()) {
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter, "No eye data");
        return;
    }

    QImage img(eye_.time_bins, eye_.volt_bins, QImage::Format_RGB32);
    const int peak = std::max(1, eye_.max_count());
    for (int v = 0; v < eye_.volt_bins; ++v) {
        for (int t = 0; t < eye_.time_bins; ++t) {
            const int c = eye_.at(t, v);
            const int img_y = eye_.volt_bins - 1 - v;
            if (c == 0) {
                img.setPixelColor(t, img_y, QColor(20, 20, 26));
            } else {
                const double t01 = std::log1p(c) / std::log1p(peak);
                img.setPixelColor(t, img_y, viridis(t01));
            }
        }
    }

    constexpr int margin = 50;
    const QRect plot(margin, margin / 2,
                     width() - 2 * margin, height() - margin - margin / 2);
    p.drawImage(plot, img);

    // Mask overlay (in normalized (t, v) ∈ ([0,1], [-1,1]) → plot coords).
    if (mask_ && !mask_->polygon.empty()) {
        // Map normalized v ∈ [-1, 1] → plot Y. Use the data range as the half-span.
        QPolygonF qpoly;
        for (const auto& [tn, vn] : mask_->polygon) {
            const double x = plot.left() + tn * plot.width();
            // Normalized v=+1 → top of plot; v=-1 → bottom.
            const double y = plot.center().y() - vn * (plot.height() / 2.0);
            qpoly << QPointF(x, y);
        }
        const int violations = sikit::specs::count_violations(eye_, *mask_);
        const QColor edge = (violations == 0)
                                ? QColor(80, 220, 90, 220)
                                : QColor(230, 70, 60, 220);
        QPen pen(edge);
        pen.setWidth(2);
        p.setPen(pen);
        p.setBrush(QColor(edge.red(), edge.green(), edge.blue(), 35));
        p.drawPolygon(qpoly);
    }

    p.setPen(QColor(140, 140, 150));
    p.drawRect(plot);

    p.setPen(Qt::white);
    p.drawText(plot.left(), plot.bottom() + 18, "0");
    p.drawText(plot.right() - 24, plot.bottom() + 18, "1 UI");
    p.drawText(plot.center().x() - 4, plot.bottom() + 18, "½");
    p.drawText(plot.left() - 36, plot.top() + 10,
               QString::number(eye_.v_max, 'f', 2));
    p.drawText(plot.left() - 36, plot.bottom(),
               QString::number(eye_.v_min, 'f', 2));

    const auto eye_metrics = sikit::eye::measure_eye(eye_);
    QString caption = QString("H=%6 V  W=%7 UI  J=%8 UI  ·  range=[%4, %5]  ·  peak=%3  ·  %1×%2 bins")
                          .arg(eye_.time_bins).arg(eye_.volt_bins).arg(peak)
                          .arg(eye_.v_min, 0, 'f', 3)
                          .arg(eye_.v_max, 0, 'f', 3)
                          .arg(eye_metrics.height_v, 0, 'f', 3)
                          .arg(eye_metrics.width_ui, 0, 'f', 3)
                          .arg(eye_metrics.jitter_pp_ui, 0, 'f', 3);
    if (mask_) {
        const int v = sikit::specs::count_violations(eye_, *mask_);
        caption += QString("  ·  mask: %1  (%2 violations)")
                       .arg(QString::fromStdString(mask_->name))
                       .arg(v);
        caption += (v == 0) ? "  ✓ PASS" : "  ✗ FAIL";
    }
    p.drawText(plot.left(), plot.top() - 6, caption);
}
