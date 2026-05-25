// Studio SI tab.
//
// Horizontal split: circuitcore::ui::PcbCanvas on the left (board
// overview), an SI analysis panel on the right with buttons that
// open sikit's existing widgets as popup windows:
//   - "S-parameter plot"   -> sikit::SParamPlotWindow
//   - "Eye diagram"        -> sikit::EyeWindow
//
// The buttons synthesize their inputs from the currently-loaded
// Board + SiStackup using sikit::analysis::synthesize_channel and
// sikit::eye::generate_eye (the same pipelines sikit/MainWindow.cpp
// drives from its menu). The studio doesn't re-implement the math
// pipelines -- it just funnels Board + selection state into the
// existing sikit code.

#pragma once

#include <QWidget>

class QComboBox;
class QLabel;

namespace circuitcore::ui {
class PcbCanvas;
}

namespace circuitcore::studio {

class BoardModel;

class SiTab : public QWidget {
    Q_OBJECT
public:
    explicit SiTab(BoardModel* model, QWidget* parent = nullptr);

private slots:
    void onBoardLoaded();
    void onPlotSParam();
    void onPlotEye();

private:
    void refreshNetList();

    BoardModel* model_;
    ui::PcbCanvas* canvas_;
    QComboBox* net_combo_;
    QLabel* status_;
};

}  // namespace circuitcore::studio
