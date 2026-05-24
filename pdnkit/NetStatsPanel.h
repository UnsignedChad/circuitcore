#pragma once

#include <QWidget>

#include "circuitcore/board/Board.h"

class QTableWidget;

// Dockable table showing per-net statistics computed from the loaded board:
// pad count, segment count + total length, zone count + total filled area.
// Sortable by clicking column headers. Useful for picking analysis targets
// and seeing PCB structure at a glance.
class NetStatsPanel : public QWidget {
    Q_OBJECT
public:
    explicit NetStatsPanel(QWidget* parent = nullptr);

    void setBoard(const circuitcore::board::Board* board);

signals:
    void netSelected(int net_id);

private:
    void rebuild();

    const circuitcore::board::Board* board_ = nullptr;
    QTableWidget* table_;
};
