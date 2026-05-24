#pragma once

#include <vector>

#include <QColor>
#include <QString>
#include <QWidget>

// Custom log-log plot for |Z(f)|. No QtCharts dependency — paints axes,
// gridlines, and the data curve in paintEvent. Frequency on X (Hz log10),
// |Z| on Y (ohms log10).
class ZfPlotWidget : public QWidget {
    Q_OBJECT
public:
    explicit ZfPlotWidget(QWidget* parent = nullptr);

    struct Curve {
        std::vector<double> freqs;       // Hz
        std::vector<double> mags;        // ohms
        QColor color = QColor(0xfd, 0xe7, 0x25);  // default viridis-yellow
        QString label;
    };

    // Single-curve convenience (backward compatible).
    void setData(std::vector<double> freqs_hz, std::vector<double> z_mag_ohm);

    // Replace all curves at once.
    void setCurves(std::vector<Curve> curves);

    // Horizontal target-impedance line (ohms). <= 0 disables it.
    void setTargetImpedance(double z_ohm);

    void clear();

protected:
    QSize sizeHint() const override;
    void paintEvent(QPaintEvent* e) override;

private:
    std::vector<Curve> curves_;
    double target_z_ = 0.0;
};
