// Studio EMI tab.
//
// emikit ships zero Qt widgets in v1 (everything is CLI), so this is
// the first GUI for the EMI analyzer. v1 wires the headline workflow:
//   - Mask combo (CISPR 22 / 32 / FCC Part 15, all four flavours)
//   - "Run compliance" button: emikit::emi::analyze_board with the
//     default trapezoidal spectrum (1 mA, 100 MHz, 1 ns rise) over
//     the canonical 30 MHz - 1 GHz grid
//   - Verdict label: PASS/FAIL/NoData + worst net + worst freq + margin
//
// Spectrum-vs-mask chart is a follow-up; the verdict number is the
// pre-compliance "are we close?" signal most users want first.

#pragma once

#include <QWidget>

class QComboBox;
class QLabel;

namespace circuitcore::ui {
class PcbCanvas;
}

namespace circuitcore::studio {

class BoardModel;

class EmiTab : public QWidget {
    Q_OBJECT
public:
    explicit EmiTab(BoardModel* model, QWidget* parent = nullptr);

private slots:
    void onBoardLoaded();
    void onRunCompliance();

private:
    BoardModel* model_;
    ui::PcbCanvas* canvas_;
    QComboBox* mask_combo_;
    QLabel* result_;
};

}  // namespace circuitcore::studio
