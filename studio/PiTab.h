// Studio PI tab.
//
// Same horizontal split as the SI tab: circuitcore::ui::PcbCanvas on
// the left, an analysis panel on the right. v1 wires the headline
// pdnkit workflow:
//   - Net (typically a power rail) + layer pickers
//   - "Run IR-drop" button: pdnkit::pi::IrMesher::build -> IrSolver::solve
//   - Display Vmax/Vmin/drop in the panel
//
// pdnkit's richer panels (AnalysisPanel, CavityPanel, TransientPanel,
// DrcPanel, etc.) are not embedded yet -- they live in pdnkit/main.cpp
// today and pulling them in needs the same pdnkit_widgets extraction
// the sikit refactor in PR #65 did. That comes when we want more than
// the IR-drop number on screen.

#pragma once

#include <QWidget>

class QComboBox;
class QLabel;

namespace circuitcore::ui {
class PcbCanvas;
}

namespace circuitcore::studio {

class BoardModel;

class PiTab : public QWidget {
    Q_OBJECT
public:
    explicit PiTab(BoardModel* model, QWidget* parent = nullptr);

private slots:
    void onBoardLoaded();
    void onRunIrDrop();

private:
    void refreshNetAndLayerLists();

    BoardModel* model_;
    ui::PcbCanvas* canvas_;
    QComboBox* net_combo_;
    QComboBox* layer_combo_;
    QLabel* result_;
};

}  // namespace circuitcore::studio
