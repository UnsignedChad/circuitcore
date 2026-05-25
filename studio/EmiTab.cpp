#include "EmiTab.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>
#include <QVariant>

#include "BoardModel.h"
#include "circuitcore/ui/PcbCanvas.h"

#include "emi/BoardAnalysis.h"
#include "emi/Masks.h"
#include "emi/Spectrum.h"

namespace circuitcore::studio {

EmiTab::EmiTab(BoardModel* model, QWidget* parent)
    : QWidget(parent),
      model_(model),
      canvas_(new ui::PcbCanvas(this)),
      mask_combo_(new QComboBox(this)),
      result_(new QLabel(this)) {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(canvas_, 3);

    auto* panel = new QWidget(this);
    panel->setMaximumWidth(320);
    auto* col = new QVBoxLayout(panel);
    col->addWidget(new QLabel(tr("<b>EMI / EMC pre-compliance</b>")));

    col->addWidget(new QLabel(tr("Limit mask:")));
    mask_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    // Populate up-front -- masks are compile-time constants, not
    // board-dependent.
    auto masks = emikit::emi::all_masks();
    for (std::size_t i = 0; i < masks.size(); ++i) {
        mask_combo_->addItem(QString::fromStdString(masks[i]->name),
                              static_cast<int>(i));
    }
    col->addWidget(mask_combo_);

    auto* run_btn = new QPushButton(tr("Run compliance"), panel);
    col->addWidget(run_btn);

    result_->setWordWrap(true);
    result_->setStyleSheet("color: #d0d0d0;");
    result_->setText(tr("Load a board, pick a mask, then "
                          "Run compliance.<br><br>"
                          "Uses the default trapezoidal drive "
                          "(1 mA peak, 100 MHz clock, 1 ns rise) over "
                          "the 30 MHz - 1 GHz CISPR/FCC band."));
    col->addWidget(result_);
    col->addStretch(1);
    root->addWidget(panel, 1);

    connect(model_, &BoardModel::boardLoaded, this, &EmiTab::onBoardLoaded);
    connect(run_btn, &QPushButton::clicked, this, &EmiTab::onRunCompliance);
}

void EmiTab::onBoardLoaded() {
    canvas_->setBoard(model_->board());
}

void EmiTab::onRunCompliance() {
    if (!model_->board()) {
        QMessageBox::information(this, tr("Run compliance"),
                                  tr("Load a board first (File > Open)."));
        return;
    }
    auto masks = emikit::emi::all_masks();
    const int idx = mask_combo_->currentData().toInt();
    if (idx < 0 || idx >= static_cast<int>(masks.size())) {
        QMessageBox::warning(this, tr("Run compliance"),
                              tr("No mask selected."));
        return;
    }
    const auto& mask = *masks[idx];

    emikit::emi::AnalysisConfig cfg;
    cfg.drive          = emikit::emi::TrapezoidalSpec{};  // 1 mA, 100 MHz, 1 ns
    cfg.loop_height_m  = 0.2e-3;
    cfg.test_distance_m = 3.0;
    cfg.freq_hz        = emikit::emi::default_cispr_freq_grid();

    const auto r = emikit::emi::analyze_board(*model_->board(), mask, cfg);

    using S = emikit::emi::Verdict::Status;
    if (r.verdict.status == S::NoData) {
        result_->setText(
            tr("<b>%1</b><br><br>"
               "<b>NO DATA</b> -- no routed nets matched. "
               "Nothing to score against the mask.")
                .arg(QString::fromStdString(mask.name)));
        return;
    }
    const char* tag = (r.verdict.status == S::Pass) ? "PASS" : "FAIL";
    const QString tag_color = (r.verdict.status == S::Pass) ? "#6bb56b" : "#d96b6b";

    result_->setText(
        tr("<b>%1</b><br><br>"
           "<span style='color:%2; font-size:16pt'><b>%3</b></span>"
           "  &nbsp; %4 nets evaluated<br><br>"
           "Worst: <b>%5</b><br>"
           "at %6 MHz, margin <b>%7%8 dB</b>")
            .arg(QString::fromStdString(mask.name))
            .arg(tag_color)
            .arg(tag)
            .arg(r.nets.size())
            .arg(QString::fromStdString(r.verdict.worst_net))
            .arg(r.verdict.worst_freq_hz / 1e6, 0, 'f', 1)
            .arg(r.verdict.worst_margin_db >= 0 ? "+" : "")
            .arg(r.verdict.worst_margin_db, 0, 'f', 1));
}

}  // namespace circuitcore::studio
