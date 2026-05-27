// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#pragma once

#include <QWidget>

// Vertical color bar showing the viridis gradient with min / mid / max
// voltage labels. Lives in the central widget next to the PcbCanvas;
// stays empty until setRange is called with v_max > v_min.
class ColorLegend : public QWidget {
    Q_OBJECT
public:
    explicit ColorLegend(QWidget* parent = nullptr);

    // Set the voltage range to display (volts). Empty if v_max <= v_min.
    void setRange(double v_min, double v_max);

protected:
    QSize sizeHint() const override;
    void paintEvent(QPaintEvent* e) override;

private:
    double v_min_ = 0.0;
    double v_max_ = 0.0;
};
