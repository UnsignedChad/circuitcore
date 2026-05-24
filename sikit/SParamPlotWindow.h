#pragma once

#include <QWidget>
#include <vector>

#include "si/Touchstone.h"

class QCheckBox;
class QComboBox;
class QGridLayout;
class QLabel;

// Frequency-domain S-parameter plot. Renders one or more S_ij curves vs
// frequency on a log-frequency X axis, with the Y axis switchable between
// magnitude (dB), unwrapped phase (degrees), and group delay (ns).
//
// Usage:
//   auto* w = new SParamPlotWindow();
//   w->setData(touchstone_file);
//   w->show();
//
// The widget owns its own controls (Y-axis mode combo + per-curve check-
// boxes). For an .s2p that's four checkboxes (S11, S12, S21, S22); for an
// .s4p that's 16.
class SParamPlotWindow : public QWidget {
    Q_OBJECT
public:
    enum class YMode {
        MagnitudeDb,
        PhaseDeg,
        GroupDelayNs,
        TdrImpedance,   // time-domain reflectometry (diagonals only)
        TdtAmplitude,   // time-domain transmission step (off-diagonals only)
    };

    explicit SParamPlotWindow(QWidget* parent = nullptr);

    void setData(const sikit::touchstone::TouchstoneFile& ts);
    void setTitleSubtext(const QString& text);

    // For a 4-port file (.s4p only), set whether the conversion to mixed-
    // mode S-parameters is supported. Caller supplies the single-ended
    // port order so the M matrix is built correctly. Default: not
    // available (the toggle is hidden).
    enum class MixedModeAvailability { Unavailable, PortOrderPNPN, PortOrderPPNN };
    void setMixedModeAvailable(MixedModeAvailability v);

    // The plot canvas calls this with itself as the paint target.
    // Public so the in-cpp PlotCanvas helper (anonymous namespace) can
    // reach it without needing a matching forward declaration.
    void paintPlotInto(QWidget* target);
    // Internal: time-domain branch (TDR/TDT modes).
    void paintTimeDomainInto(QWidget* target);

private slots:
    void onModeChanged(int idx);
    void onCurveToggled();
    void onMixedModeToggled(bool on);

private:
    void rebuildCurveCheckboxes();
    void applyMixedModeIfRequested();

    sikit::touchstone::TouchstoneFile ts_;
    YMode mode_ = YMode::MagnitudeDb;

    // One checkbox per (row, col) S-parameter entry. Index is row*N+col.
    std::vector<QCheckBox*> curve_checks_;

    QComboBox* mode_combo_ = nullptr;
    class QCheckBox* mixed_mode_check_ = nullptr;
    MixedModeAvailability mm_avail_ = MixedModeAvailability::Unavailable;
    sikit::touchstone::TouchstoneFile ts_se_;  // original single-ended (if any)
    QWidget* curves_holder_ = nullptr;
    QGridLayout* curves_grid_ = nullptr;
    QLabel* caption_ = nullptr;
    QWidget* plot_canvas_ = nullptr;
};
