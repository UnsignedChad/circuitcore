// Studio Board tab.
//
// Thin wrapper around circuitcore::ui::PcbCanvas. The canvas does all
// the OpenGL work; this widget exists so the BoardModel signals can
// be funneled into setBoard() (and so a future overlay panel for the
// Board tab itself has somewhere to live).

#pragma once

#include <QWidget>

namespace circuitcore::ui {
class PcbCanvas;
}

namespace circuitcore::studio {

class BoardModel;

class BoardTab : public QWidget {
    Q_OBJECT
public:
    explicit BoardTab(BoardModel* model, QWidget* parent = nullptr);

private slots:
    void onBoardLoaded();
    void onCanvasHover(const QString& info);

private:
    BoardModel* model_;
    ui::PcbCanvas* canvas_;
};

}  // namespace circuitcore::studio
