// Studio Board tab.
//
// QPainter-based 2D PCB overview: outline, layers, segments, zones,
// vias, pads. Pan with middle-mouse drag, zoom with wheel,
// fit-to-board on the Home key or when a new board loads.
//
// Why QPainter and not the shared OpenGL canvas
//
//   sikit and pdnkit each subclass-flavour their OpenGL PcbCanvas with
//   very different overlays (impedance, IR-drop, decap markers,
//   cavity highlight, probe-R workflow). Promoting PcbCanvas itself
//   into a base + per-tool subclasses is its own task (#6). For the
//   skeleton, QPainter is plenty fast for an overview tab on any
//   board of typical size, keeps the dep graph trivial, and the swap
//   to the shared GL canvas later is a near-one-line change.

#pragma once

#include <QPointF>
#include <QWidget>

namespace circuitcore::studio {

class BoardModel;

class BoardTab : public QWidget {
    Q_OBJECT
public:
    explicit BoardTab(BoardModel* model, QWidget* parent = nullptr);

protected:
    void paintEvent(QPaintEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

private slots:
    void onBoardLoaded();

private:
    BoardModel* model_;

    // World -> screen transform. World units are metres (board frame).
    // zoom_ is pixels per metre.
    double zoom_ = 5000.0;
    QPointF center_world_{0.0, 0.0};

    bool   panning_ = false;
    QPoint last_mouse_;

    void fitToBoard();
    QPointF worldToScreen(double wx, double wy) const;
    QPointF screenToWorld(int sx, int sy) const;
};

}  // namespace circuitcore::studio
