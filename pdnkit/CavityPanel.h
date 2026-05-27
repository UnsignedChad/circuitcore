// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#pragma once

#include <complex>
#include <QWidget>

#include <vector>

#include "circuitcore/board/Board.h"
#include "render/IrResultMesh.h"

class QComboBox;
class QDoubleSpinBox;
class QPushButton;
class QSpinBox;
class QTableWidget;
class ZfPlotWidget;

// Dockable panel that runs a cavity-model Z(f) sweep on the loaded board.
// Plane dimensions are auto-fit to the bounding box of the selected net's
// filled-zone copper on the selected layer. Ports are given as (x, y) in mm
// relative to that bbox. Plot is a log-log |Z(f)| curve.
class CavityPanel : public QWidget {
    Q_OBJECT
public:
    explicit CavityPanel(QWidget* parent = nullptr);

    void setBoard(const circuitcore::board::Board* board);
    void setNetById(int net_id);

    // After the user runs a Z(f) sweep, these expose the complex sweep
    // result so the main window can write it out as Touchstone. Empty
    // until onRun() has fired at least once.
    bool hasLastSweep() const { return !last_sweep_freqs_.empty(); }
    const std::vector<double>& lastSweepFreqs() const {
        return last_sweep_freqs_;
    }
    const std::vector<std::complex<double>>& lastSweepZ() const {
        return last_sweep_z_;
    }

signals:
    // Fires whenever the decap list changes (add / remove / cell edit).
    void decapsChanged(const std::vector<circuitcore::board::Point2>& positions);

    // Fires whenever the cavity bbox or port positions change (net swap,
    // layer swap, or port spinbox edit).
    void cavityChanged(double lo_x, double lo_y, double hi_x, double hi_y,
                       const std::vector<circuitcore::board::Point2>& ports);

private slots:
    void onRun();
    void onClear();
    void onSaveCsv();
    void onAddDecap();
    void onRemoveDecap();
    void onAutoSuggest();
    void onShowModeShape();

signals:
    void modeShapeMesh(pdnkit::render::IrResultMesh mesh);

private:
    void emitCavity();
    void updatePlaneInfo();
    void rebuildNetCombo();

    const circuitcore::board::Board* board_ = nullptr;

    QComboBox* net_combo_;
    QDoubleSpinBox* eps_r_spin_;
    QDoubleSpinBox* tan_delta_spin_;
    QDoubleSpinBox* thickness_spin_;
    QDoubleSpinBox* port1_x_, * port1_y_;
    QDoubleSpinBox* port2_x_, * port2_y_;
    QDoubleSpinBox* f_min_spin_;
    QDoubleSpinBox* f_max_spin_;
    QSpinBox* points_spin_;
    QSpinBox* modes_spin_;
    QDoubleSpinBox* target_z_spin_;
    class QCheckBox* overlay_bare_check_;
    QPushButton* run_btn_;
    QPushButton* clear_btn_;
    QPushButton* save_btn_;
    QPushButton* add_decap_btn_;
    QPushButton* remove_decap_btn_;
    class QLabel* plane_info_label_;
    QPushButton* auto_decap_btn_;
    QTableWidget* decap_table_;
    ZfPlotWidget* plot_;

    // Cache of the latest sweep for CSV export.
    std::vector<double> last_freqs_;
    std::vector<double> last_mags_;
    // Cached complex Z(f) sweep from the most recent onRun(). Used by
    // MainWindow::onExportTouchstone to dump a .s1p file.
    std::vector<double> last_sweep_freqs_;
    std::vector<std::complex<double>> last_sweep_z_;

    // VRM overlay on the Z(f) plot.
    class QCheckBox*      vrm_check_;
    class QDoubleSpinBox* vrm_r_mohm_spin_;
    class QDoubleSpinBox* vrm_l_uH_spin_;

};
