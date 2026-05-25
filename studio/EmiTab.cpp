#include "EmiTab.h"

#include <algorithm>
#include <cmath>

#include <QButtonGroup>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPainter>
#include <QPaintEvent>
#include <QPushButton>
#include <QRadioButton>
#include <QStackedWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <utility>

#include "BoardModel.h"
#include "circuitcore/ui/PcbCanvas.h"

#include "emi/BoardAnalysis.h"
#include "emi/Masks.h"
#include "emi/Spectrum.h"

namespace circuitcore::studio {

// ---------------- Spectrum vs mask chart ----------------
class EmiSpectrumWidget : public QWidget {
public:
    explicit EmiSpectrumWidget(QWidget* parent = nullptr) : QWidget(parent) {
        setAutoFillBackground(true);
        QPalette pal = palette();
        pal.setColor(QPalette::Window, QColor(20, 22, 26));
        setPalette(pal);
        setMinimumHeight(280);
    }

    void setData(std::vector<double> freqs, std::vector<double> worst_dbuv,
                  const emikit::emi::EmissionsMask* mask) {
        freqs_ = std::move(freqs);
        dbuv_  = std::move(worst_dbuv);
        mask_  = mask;
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.fillRect(rect(), QColor(20, 22, 26));

        if (freqs_.empty()) {
            p.setPen(QColor(170, 170, 170));
            p.drawText(rect(), Qt::AlignCenter,
                        tr("Run Compliance to see the spectrum."));
            return;
        }
        const int W = width(), H = height();
        const int ml = 56, mr = 16, mt = 18, mb = 32;
        const int pw = W - ml - mr, ph = H - mt - mb;
        if (pw < 50 || ph < 50) return;

        // Y range: encompass both worst-case and the mask if present.
        double y_lo = 0.0, y_hi = 60.0;
        for (double v : dbuv_) {
            y_lo = std::min(y_lo, v);
            y_hi = std::max(y_hi, v);
        }
        if (mask_) {
            for (const auto& mp : mask_->points) {
                y_hi = std::max(y_hi, mp.limit_dbuv);
                y_lo = std::min(y_lo, mp.limit_dbuv);
            }
        }
        if (y_hi - y_lo < 10.0) y_hi = y_lo + 10.0;
        y_lo -= 5.0; y_hi += 5.0;

        // Log-X mapping over freq range.
        const double f_lo = std::max(1e6, freqs_.front());
        const double f_hi = std::max(f_lo * 1.001, freqs_.back());
        auto x_of = [&](double f) {
            const double t = (std::log10(std::max(f, 1.0)) - std::log10(f_lo))
                            / (std::log10(f_hi) - std::log10(f_lo));
            return ml + t * pw;
        };
        auto y_of = [&](double v) {
            const double t = (v - y_lo) / (y_hi - y_lo);
            return mt + (1.0 - t) * ph;
        };

        // Grid + axis labels.
        p.setPen(QColor(60, 64, 72));
        for (int dec_f = 6; dec_f <= 10; ++dec_f) {
            const double f = std::pow(10.0, dec_f);
            if (f < f_lo || f > f_hi) continue;
            const double x = x_of(f);
            p.drawLine(QPointF(x, mt), QPointF(x, mt + ph));
        }
        for (double v = 20.0; v <= 80.0; v += 10.0) {
            if (v < y_lo || v > y_hi) continue;
            const double y = y_of(v);
            p.drawLine(QPointF(ml, y), QPointF(ml + pw, y));
        }
        p.setPen(QColor(180, 180, 180));
        for (int dec_f = 6; dec_f <= 10; ++dec_f) {
            const double f = std::pow(10.0, dec_f);
            if (f < f_lo || f > f_hi) continue;
            const QString lbl = (f >= 1e9) ?
                QString::number(f / 1e9, 'f', 0) + " GHz" :
                QString::number(f / 1e6, 'f', 0) + " MHz";
            p.drawText(QPointF(x_of(f) - 18, mt + ph + 18), lbl);
        }
        for (double v = 20.0; v <= 80.0; v += 20.0) {
            if (v < y_lo || v > y_hi) continue;
            p.drawText(QPointF(8, y_of(v) + 4),
                        QString::number(v, 'f', 0) + " dBuV/m");
        }

        // Mask: step plot in red.
        if (mask_ && !mask_->points.empty()) {
            QVector<QPointF> step;
            for (const auto& mp : mask_->points) {
                if (mp.f_hz < f_lo || mp.f_hz > f_hi) continue;
                step.append(QPointF(x_of(mp.f_hz), y_of(mp.limit_dbuv)));
            }
            p.setPen(QPen(QColor(220, 90, 90), 2.0));
            for (int i = 1; i < step.size(); ++i) {
                p.drawLine(QPointF(step[i - 1].x(), step[i - 1].y()),
                            QPointF(step[i].x(),    step[i - 1].y()));
                p.drawLine(QPointF(step[i].x(),    step[i - 1].y()),
                            QPointF(step[i].x(),    step[i].y()));
            }
        }

        // Worst-case spectrum: smooth line in cyan.
        p.setPen(QPen(QColor(90, 200, 220), 2.0));
        for (std::size_t i = 1; i < freqs_.size(); ++i) {
            p.drawLine(QPointF(x_of(freqs_[i - 1]), y_of(dbuv_[i - 1])),
                        QPointF(x_of(freqs_[i]),     y_of(dbuv_[i])));
        }
    }

private:
    std::vector<double> freqs_, dbuv_;
    const emikit::emi::EmissionsMask* mask_ = nullptr;
};

// ---------------- EmiTab ----------------
EmiTab::EmiTab(BoardModel* model, QWidget* parent)
    : QWidget(parent), model_(model) {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);

    // Center: stack of canvas (when no analysis) and chart.
    center_ = new QStackedWidget(this);
    canvas_ = new ui::PcbCanvas(center_);
    chart_  = new EmiSpectrumWidget(center_);
    center_->addWidget(canvas_);
    center_->addWidget(chart_);
    root->addWidget(center_, 3);

    // Right: controls + verdict + table.
    auto* right = new QWidget(this);
    right->setMaximumWidth(360);
    auto* rcol = new QVBoxLayout(right);

    rcol->addWidget(new QLabel(tr("<b>EMI / EMC pre-compliance</b>")));

    rcol->addWidget(new QLabel(tr("Limit mask:")));
    mask_combo_ = new QComboBox(right);
    for (std::size_t i = 0; i < emikit::emi::all_masks().size(); ++i) {
        const auto* m = emikit::emi::all_masks()[i];
        mask_combo_->addItem(QString::fromStdString(m->name),
                              static_cast<int>(i));
    }
    rcol->addWidget(mask_combo_);

    // Drive spectrum group.
    auto* drv = new QGroupBox(tr("Drive spectrum"), right);
    auto* dform = new QFormLayout(drv);
    i_peak_ma_ = new QDoubleSpinBox(drv);
    i_peak_ma_->setRange(0.001, 10000.0);
    i_peak_ma_->setSuffix(" mA"); i_peak_ma_->setDecimals(3);
    i_peak_ma_->setValue(1.0);
    clock_mhz_ = new QDoubleSpinBox(drv);
    clock_mhz_->setRange(0.1, 5000.0);
    clock_mhz_->setSuffix(" MHz"); clock_mhz_->setDecimals(2);
    clock_mhz_->setValue(100.0);
    duty_pct_ = new QDoubleSpinBox(drv);
    duty_pct_->setRange(1.0, 99.0); duty_pct_->setSuffix(" %");
    duty_pct_->setValue(50.0);
    rise_ns_ = new QDoubleSpinBox(drv);
    rise_ns_->setRange(0.01, 100.0); rise_ns_->setSuffix(" ns");
    rise_ns_->setDecimals(2); rise_ns_->setValue(1.0);
    dform->addRow(tr("Peak current:"), i_peak_ma_);
    dform->addRow(tr("Clock:"),        clock_mhz_);
    dform->addRow(tr("Duty:"),         duty_pct_);
    dform->addRow(tr("Rise / fall:"),  rise_ns_);
    rcol->addWidget(drv);

    // Geometry / setup group.
    auto* geo = new QGroupBox(tr("Setup"), right);
    auto* gform = new QFormLayout(geo);
    loop_mm_ = new QDoubleSpinBox(geo);
    loop_mm_->setRange(0.01, 100.0); loop_mm_->setSuffix(" mm");
    loop_mm_->setDecimals(3); loop_mm_->setValue(0.2);
    gform->addRow(tr("Loop height:"), loop_mm_);
    auto* dist_row = new QWidget(geo);
    auto* drow = new QHBoxLayout(dist_row);
    drow->setContentsMargins(0, 0, 0, 0);
    dist_3m_  = new QRadioButton(tr("3 m"),  dist_row);
    dist_10m_ = new QRadioButton(tr("10 m"), dist_row);
    dist_3m_->setChecked(true);
    auto* dgrp = new QButtonGroup(this);
    dgrp->addButton(dist_3m_);
    dgrp->addButton(dist_10m_);
    drow->addWidget(dist_3m_);
    drow->addWidget(dist_10m_);
    drow->addStretch(1);
    gform->addRow(tr("Test distance:"), dist_row);
    rcol->addWidget(geo);

    auto* run = new QPushButton(tr("Run compliance"), right);
    rcol->addWidget(run);

    verdict_ = new QLabel(right);
    verdict_->setWordWrap(true);
    verdict_->setStyleSheet("color: #d0d0d0;");
    verdict_->setText(tr("Load a board, set drive + setup, then Run."));
    rcol->addWidget(verdict_);

    rcol->addWidget(new QLabel(tr("Per-net emissions:")));
    nets_table_ = new QTableWidget(0, 4, right);
    nets_table_->setHorizontalHeaderLabels(
        {tr("Net"), tr("Length mm"), tr("Loop mm²"), tr("Peak dBuV/m")});
    nets_table_->horizontalHeader()->setStretchLastSection(true);
    nets_table_->verticalHeader()->setVisible(false);
    nets_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    rcol->addWidget(nets_table_, 1);

    root->addWidget(right, 1);

    connect(model_, &BoardModel::boardLoaded, this, &EmiTab::onBoardLoaded);
    connect(run, &QPushButton::clicked, this, &EmiTab::onRunCompliance);
}

void EmiTab::onBoardLoaded() {
    canvas_->setBoard(model_->board());
    center_->setCurrentWidget(canvas_);
}

void EmiTab::onRunCompliance() {
    if (!model_->board()) {
        QMessageBox::information(this, tr("Run compliance"),
                                  tr("Load a board first."));
        return;
    }
    const auto& masks = emikit::emi::all_masks();
    const int idx = mask_combo_->currentData().toInt();
    if (idx < 0 || idx >= static_cast<int>(masks.size())) return;
    const auto& mask = *masks[idx];

    emikit::emi::AnalysisConfig cfg;
    cfg.drive.i_peak_a    = i_peak_ma_->value() * 1e-3;
    cfg.drive.period_s    = 1.0 / (clock_mhz_->value() * 1e6);
    cfg.drive.duty_cycle  = duty_pct_->value() / 100.0;
    cfg.drive.rise_time_s = rise_ns_->value() * 1e-9;
    cfg.loop_height_m     = loop_mm_->value() * 1e-3;
    cfg.test_distance_m   = dist_10m_->isChecked() ? 10.0 : 3.0;
    cfg.freq_hz           = emikit::emi::default_cispr_freq_grid();

    const auto r = emikit::emi::analyze_board(*model_->board(), mask, cfg);

    using S = emikit::emi::Verdict::Status;
    if (r.verdict.status == S::NoData) {
        verdict_->setText(tr("<b>%1</b><br><b>NO DATA</b> -- no routed nets.")
                              .arg(QString::fromStdString(mask.name)));
    } else {
        const char* tag = (r.verdict.status == S::Pass) ? "PASS" : "FAIL";
        const QString color = (r.verdict.status == S::Pass) ? "#6bb56b" : "#d96b6b";
        verdict_->setText(
            tr("<b>%1</b><br><br>"
               "<span style='color:%2; font-size:16pt'><b>%3</b></span>"
               "  %4 nets<br><br>"
               "Worst: <b>%5</b><br>at %6 MHz, margin <b>%7%8 dB</b>")
                .arg(QString::fromStdString(mask.name))
                .arg(color).arg(tag).arg(r.nets.size())
                .arg(QString::fromStdString(r.verdict.worst_net))
                .arg(r.verdict.worst_freq_hz / 1e6, 0, 'f', 1)
                .arg(r.verdict.worst_margin_db >= 0 ? "+" : "")
                .arg(r.verdict.worst_margin_db, 0, 'f', 1));
    }

    // Populate per-net table.
    nets_table_->setRowCount(static_cast<int>(r.nets.size()));
    for (std::size_t i = 0; i < r.nets.size(); ++i) {
        const auto& n = r.nets[i];
        double peak_db = 0.0;
        for (double v : n.e_dbuv) peak_db = std::max(peak_db, v);
        nets_table_->setItem(static_cast<int>(i), 0,
            new QTableWidgetItem(QString::fromStdString(n.net_name)));
        nets_table_->setItem(static_cast<int>(i), 1,
            new QTableWidgetItem(QString::number(n.total_length_m * 1e3, 'f', 2)));
        nets_table_->setItem(static_cast<int>(i), 2,
            new QTableWidgetItem(QString::number(n.loop_area_m2 * 1e6, 'f', 3)));
        nets_table_->setItem(static_cast<int>(i), 3,
            new QTableWidgetItem(QString::number(peak_db, 'f', 1)));
    }

    // Chart.
    chart_->setData(cfg.freq_hz, r.worst_case_dbuv, &mask);
    center_->setCurrentWidget(chart_);
}

}  // namespace circuitcore::studio
