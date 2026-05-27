// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
// IPC-2152 power-aware DRC dock panel.
//
// Net combo + per-net current + allowable temperature rise. Click Run,
// see each violating segment with actual width vs. required width and
// a human-readable explanation. Lives next to the other analysis docks
// (tabified with Analysis / Net Stats / Plane Z(f) / Transient).

#pragma once

#include <QWidget>

#include "circuitcore/board/Board.h"

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QTableWidget;

class DrcPanel : public QWidget {
    Q_OBJECT
public:
    explicit DrcPanel(QWidget* parent = nullptr);

    void setBoard(const circuitcore::board::Board* board);
    void setNetById(int net_id);

private slots:
    void onRun();
    void onNetChanged();

private:
    void populateNets();

    const circuitcore::board::Board* board_ = nullptr;

    QComboBox*       net_combo_;
    QDoubleSpinBox*  current_spin_;
    QDoubleSpinBox*  temp_rise_spin_;
    QPushButton*     run_btn_;
    QTableWidget*    results_;
};
