#include "DrcPanel.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "pi/PowerDrc.h"

DrcPanel::DrcPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    auto* header = new QLabel("IPC-2152 power-aware DRC");
    QFont f = header->font(); f.setBold(true); header->setFont(f);
    outer->addWidget(header);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(4);

    net_combo_ = new QComboBox();
    current_spin_ = new QDoubleSpinBox();
    current_spin_->setRange(0.001, 1000.0);
    current_spin_->setDecimals(3);
    current_spin_->setValue(1.0);
    current_spin_->setSuffix(" A");
    temp_rise_spin_ = new QDoubleSpinBox();
    temp_rise_spin_->setRange(1.0, 100.0);
    temp_rise_spin_->setDecimals(0);
    temp_rise_spin_->setValue(10.0);
    temp_rise_spin_->setSuffix(" Â°""C");

    form->addRow("Net:",        net_combo_);
    form->addRow("Current:",    current_spin_);
    form->addRow("Allowable ÎT:", temp_rise_spin_);
    outer->addLayout(form);

    run_btn_ = new QPushButton("Run DRC");
    outer->addWidget(run_btn_);

    results_ = new QTableWidget(0, 5);
    results_->setHorizontalHeaderLabels(
        {"Seg", "Layer", "Actual (mm)", "Required (mm)", "Message"});
    results_->verticalHeader()->setVisible(false);
    results_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    results_->setSelectionBehavior(QAbstractItemView::SelectRows);
    results_->horizontalHeader()->setStretchLastSection(true);
    outer->addWidget(results_, 1);

    connect(run_btn_, &QPushButton::clicked, this, &DrcPanel::onRun);
    connect(net_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ onNetChanged(); });
}

void DrcPanel::setBoard(const circuitcore::board::Board* board) {
    board_ = board;
    populateNets();
    results_->setRowCount(0);
}

void DrcPanel::setNetById(int net_id) {
    for (int i = 0; i < net_combo_->count(); ++i) {
        if (net_combo_->itemData(i).toInt() == net_id) {
            net_combo_->setCurrentIndex(i);
            return;
        }
    }
}

void DrcPanel::onNetChanged() {
    // No-op for now -- the table is only refilled when Run is clicked.
}

void DrcPanel::populateNets() {
    net_combo_->blockSignals(true);
    net_combo_->clear();
    if (board_) {
        for (const auto& n : board_->nets) {
            if (n.name.empty()) continue;
            net_combo_->addItem(QString::fromStdString(n.name), n.id);
        }
    }
    net_combo_->blockSignals(false);
}

void DrcPanel::onRun() {
    results_->setRowCount(0);
    if (!board_ || net_combo_->count() == 0) return;
    const int net_id = net_combo_->currentData().toInt();

    pdnkit::pi::DrcRule rule;
    rule.net_id = net_id;
    rule.current_amps = current_spin_->value();
    rule.temp_rise_c = temp_rise_spin_->value();

    auto report = pdnkit::pi::check_ipc2152(*board_, {rule});

    results_->setRowCount(static_cast<int>(report.violations.size()));
    for (std::size_t i = 0; i < report.violations.size(); ++i) {
        const auto& v = report.violations[i];
        const auto* layer = board_->find_layer(v.layer_ordinal);
        results_->setItem(static_cast<int>(i), 0,
            new QTableWidgetItem(QString::number(v.segment_index)));
        results_->setItem(static_cast<int>(i), 1,
            new QTableWidgetItem(
                layer ? QString::fromStdString(layer->name)
                      : QString::number(v.layer_ordinal)));
        results_->setItem(static_cast<int>(i), 2,
            new QTableWidgetItem(
                QString::number(v.width_actual_m * 1000.0, 'f', 3)));
        results_->setItem(static_cast<int>(i), 3,
            new QTableWidgetItem(
                QString::number(v.width_required_m * 1000.0, 'f', 3)));
        results_->setItem(static_cast<int>(i), 4,
            new QTableWidgetItem(QString::fromStdString(v.message)));
    }
    results_->resizeColumnsToContents();
}
