// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#pragma once

#include <vector>

#include <QWidget>

// Linear-X linear-Y plot for transient V(t) curves.
// Two curves: observation node voltage (yellow) and mesh max abs voltage
// (muted blue). X axis in microseconds, Y axis in millivolts.
class TransientPlotWidget : public QWidget {
    Q_OBJECT
public:
    explicit TransientPlotWidget(QWidget* parent = nullptr);

    void setData(std::vector<double> times_s,
                 std::vector<double> v_obs_v,
                 std::vector<double> v_max_v);
    void clear();

protected:
    QSize sizeHint() const override;
    void paintEvent(QPaintEvent* e) override;

private:
    std::vector<double> t_;
    std::vector<double> vobs_;
    std::vector<double> vmax_;
};
