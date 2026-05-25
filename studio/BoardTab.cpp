#include "BoardTab.h"

#include <algorithm>
#include <limits>

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QWheelEvent>

#include "circuitcore/board/Board.h"
#include "circuitcore/ui/LayerColors.h"
#include "BoardModel.h"

namespace circuitcore::studio {

namespace {

// circuitcore::ui::layer_color returns RGBA in [0,1]; QColor takes 0-255.
QColor toQColor(std::array<float, 4> c) {
    return QColor(
        static_cast<int>(c[0] * 255.0f + 0.5f),
        static_cast<int>(c[1] * 255.0f + 0.5f),
        static_cast<int>(c[2] * 255.0f + 0.5f),
        static_cast<int>(c[3] * 255.0f + 0.5f));
}

}  // namespace

BoardTab::BoardTab(BoardModel* model, QWidget* parent)
    : QWidget(parent), model_(model) {
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAutoFillBackground(true);
    QPalette pal = palette();
    pal.setColor(QPalette::Window, QColor(20, 22, 26));
    setPalette(pal);
    connect(model_, &BoardModel::boardLoaded, this, &BoardTab::onBoardLoaded);
}

void BoardTab::onBoardLoaded() {
    fitToBoard();
    update();
}

void BoardTab::fitToBoard() {
    if (!model_->board()) return;
    double xmin = std::numeric_limits<double>::infinity();
    double ymin = std::numeric_limits<double>::infinity();
    double xmax = -std::numeric_limits<double>::infinity();
    double ymax = -std::numeric_limits<double>::infinity();
    const auto& b = *model_->board();
    for (const auto& s : b.segments) {
        xmin = std::min({xmin, s.start.x, s.end.x});
        ymin = std::min({ymin, s.start.y, s.end.y});
        xmax = std::max({xmax, s.start.x, s.end.x});
        ymax = std::max({ymax, s.start.y, s.end.y});
    }
    for (const auto& v : b.vias) {
        xmin = std::min(xmin, v.at.x); ymin = std::min(ymin, v.at.y);
        xmax = std::max(xmax, v.at.x); ymax = std::max(ymax, v.at.y);
    }
    for (const auto& z : b.zones) {
        for (const auto& p : z.outline.outline) {
            xmin = std::min(xmin, p.x); ymin = std::min(ymin, p.y);
            xmax = std::max(xmax, p.x); ymax = std::max(ymax, p.y);
        }
    }
    if (xmax <= xmin || ymax <= ymin) return;
    center_world_ = QPointF(0.5 * (xmin + xmax), 0.5 * (ymin + ymax));
    const double board_w = xmax - xmin;
    const double board_h = ymax - ymin;
    const double margin = 1.1;  // 10% padding
    const double zx = width()  / (board_w * margin);
    const double zy = height() / (board_h * margin);
    zoom_ = std::min(zx, zy);
}

QPointF BoardTab::worldToScreen(double wx, double wy) const {
    // World y grows up in board space; flip so screen y grows down.
    const double sx = (wx - center_world_.x()) * zoom_ + 0.5 * width();
    const double sy = (center_world_.y() - wy) * zoom_ + 0.5 * height();
    return QPointF(sx, sy);
}

QPointF BoardTab::screenToWorld(int sx, int sy) const {
    const double wx = (sx - 0.5 * width())  / zoom_ + center_world_.x();
    const double wy = center_world_.y() - (sy - 0.5 * height()) / zoom_;
    return QPointF(wx, wy);
}

void BoardTab::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(20, 22, 26));

    const auto* b = model_->board();
    if (!b) {
        p.setPen(QColor(180, 180, 180));
        p.drawText(rect(), Qt::AlignCenter,
                   tr("File > Open... to load a .kicad_pcb"));
        return;
    }

    // Outline (Edge.Cuts) -- thin yellow line.
    p.setPen(QPen(QColor(220, 200, 80), 1.0));
    for (const auto& e : b->outline) {
        p.drawLine(worldToScreen(e.start.x, e.start.y),
                   worldToScreen(e.end.x,   e.end.y));
    }

    // Zones first (filled polygons), per-layer color.
    for (const auto& z : b->zones) {
        const auto color = toQColor(circuitcore::ui::layer_color(z.layer_ordinal));
        QColor fill = color;
        fill.setAlpha(80);
        p.setBrush(fill);
        p.setPen(Qt::NoPen);
        const auto& polys = !z.filled.empty()
                              ? z.filled
                              : std::vector{z.outline};
        for (const auto& poly : polys) {
            QPainterPath path;
            if (poly.outline.empty()) continue;
            path.moveTo(worldToScreen(poly.outline[0].x, poly.outline[0].y));
            for (std::size_t i = 1; i < poly.outline.size(); ++i) {
                path.lineTo(worldToScreen(poly.outline[i].x, poly.outline[i].y));
            }
            path.closeSubpath();
            p.drawPath(path);
        }
    }

    // Segments on top of zones.
    for (const auto& s : b->segments) {
        const auto color = toQColor(circuitcore::ui::layer_color(s.layer_ordinal));
        const double w_px = std::max(1.0, s.width * zoom_);
        p.setPen(QPen(color, w_px, Qt::SolidLine, Qt::RoundCap));
        p.drawLine(worldToScreen(s.start.x, s.start.y),
                   worldToScreen(s.end.x,   s.end.y));
    }

    // Vias -- pad ring + hole.
    p.setBrush(Qt::NoBrush);
    for (const auto& v : b->vias) {
        const double r_pad = 0.5 * v.outer_diameter * zoom_;
        const double r_drill = 0.5 * v.drill * zoom_;
        const auto c = worldToScreen(v.at.x, v.at.y);
        p.setPen(QPen(QColor(200, 200, 200), 1.0));
        if (r_pad > 1.0)   p.drawEllipse(c, r_pad,  r_pad);
        if (r_drill > 1.0) p.drawEllipse(c, r_drill, r_drill);
    }

    // Hover-coords overlay (top-left, small).
    if (b) {
        p.setPen(QColor(180, 180, 180));
        p.drawText(8, 18,
                   tr("%1 nets  |  %2 segments  |  %3 vias  |  %4 zones")
                       .arg(b->nets.size())
                       .arg(b->segments.size())
                       .arg(b->vias.size())
                       .arg(b->zones.size()));
    }
}

void BoardTab::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) {
        panning_ = true;
        last_mouse_ = e->pos();
        setCursor(Qt::ClosedHandCursor);
    }
}

void BoardTab::mouseMoveEvent(QMouseEvent* e) {
    if (panning_) {
        const QPoint dp = e->pos() - last_mouse_;
        center_world_ -= QPointF(dp.x() / zoom_, -dp.y() / zoom_);
        last_mouse_ = e->pos();
        update();
    }
    const auto w = screenToWorld(e->pos().x(), e->pos().y());
    model_->setHover(w.x(), w.y());
}

void BoardTab::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton) {
        panning_ = false;
        setCursor(Qt::ArrowCursor);
    }
}

void BoardTab::wheelEvent(QWheelEvent* e) {
    // Zoom toward the cursor: keep the world-point under the cursor fixed.
    const auto pos  = e->position();
    const auto wpre = screenToWorld(pos.x(), pos.y());
    const double step = (e->angleDelta().y() > 0) ? 1.15 : (1.0 / 1.15);
    zoom_ *= step;
    const auto wpost = screenToWorld(pos.x(), pos.y());
    center_world_ += (wpre - wpost);
    update();
}

void BoardTab::keyPressEvent(QKeyEvent* e) {
    if (e->key() == Qt::Key_Home) {
        fitToBoard();
        update();
        e->accept();
    } else {
        QWidget::keyPressEvent(e);
    }
}

void BoardTab::resizeEvent(QResizeEvent*) {
    // Recompute fit if no board yet (so the "open a file" message stays
    // centered); otherwise leave the current view alone so a resize
    // doesn't surprise the user.
    if (!model_->board()) update();
}

}  // namespace circuitcore::studio
