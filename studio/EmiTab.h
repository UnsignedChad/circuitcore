// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// Studio EMI tab.
//
// Drive spectrum + loop-height + test-distance controls on the right,
// per-net emissions table at the bottom, spectrum-vs-mask chart as
// the central widget (canvas only shown when no analysis has run --
// the chart takes over once a Run completes).

#pragma once

#include <QWidget>

#include "emi/BoardAnalysis.h"
#include "emi/Masks.h"

class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QRadioButton;
class QStackedWidget;
class QTableWidget;

namespace circuitcore::ui {
class PcbCanvas;
}

namespace circuitcore::studio {

class BoardModel;
class EmiSpectrumWidget;  // forward decl, defined in EmiTab.cpp

class EmiTab : public QWidget {
    Q_OBJECT
public:
    explicit EmiTab(BoardModel* model, QWidget* parent = nullptr);

private slots:
    void onBoardLoaded();
    void onRunCompliance();

private:
    BoardModel* model_;
    ui::PcbCanvas* canvas_ = nullptr;
    EmiSpectrumWidget* chart_ = nullptr;
    QStackedWidget* center_ = nullptr;

    // Controls
    QComboBox*       mask_combo_ = nullptr;
    QDoubleSpinBox*  i_peak_ma_  = nullptr;
    QDoubleSpinBox*  clock_mhz_  = nullptr;
    QDoubleSpinBox*  duty_pct_   = nullptr;
    QDoubleSpinBox*  rise_ns_    = nullptr;
    QDoubleSpinBox*  loop_mm_    = nullptr;
    QRadioButton*    dist_3m_    = nullptr;
    QRadioButton*    dist_10m_   = nullptr;

    QTableWidget*    nets_table_ = nullptr;
    QLabel*          verdict_    = nullptr;
};

}  // namespace circuitcore::studio
