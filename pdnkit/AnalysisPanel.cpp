// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "AnalysisPanel.h"

#include <algorithm>
#include <set>
#include <vector>

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

namespace {

// Per-row spinbox lives in column 2 via setCellWidget. Helper for read-back.
QDoubleSpinBox* row_spin(QTableWidget* t, int row) {
    return qobject_cast<QDoubleSpinBox*>(t->cellWidget(row, 2));
}

QString row_name(QTableWidget* t, int row) {
    auto* item = t->item(row, 0);
    return item ? item->text() : QString();
}

}  // namespace

AnalysisPanel::AnalysisPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    auto* header = new QLabel("Analysis");
    QFont f = header->font();
    f.setBold(true);
    header->setFont(f);
    outer->addWidget(header);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(4);

    net_combo_ = new QComboBox();
    layer_combo_ = new QComboBox();

    default_current_spin_ = new QDoubleSpinBox();
    default_current_spin_->setRange(-1000.0, 1000.0);
    default_current_spin_->setDecimals(3);
    default_current_spin_->setSingleStep(100.0);
    default_current_spin_->setValue(1000.0);  // 1A
    default_current_spin_->setSuffix(" mA");

    cell_size_spin_ = new QDoubleSpinBox();
    cell_size_spin_->setRange(0.05, 5.0);
    cell_size_spin_->setDecimals(2);
    cell_size_spin_->setSingleStep(0.1);
    cell_size_spin_->setValue(0.5);
    cell_size_spin_->setSuffix(" mm");

    form->addRow("Net:",     net_combo_);
    form->addRow("Layer:",   layer_combo_);
    form->addRow("Default I:", default_current_spin_);
    form->addRow("Cell:",    cell_size_spin_);
    outer->addLayout(form);

    auto* extra_label = new QLabel("Include extra layers (vias wire them):");
    outer->addWidget(extra_label);
    extra_layers_ = new QListWidget();
    extra_layers_->setMaximumHeight(80);
    outer->addWidget(extra_layers_);

    auto* pads_label = new QLabel("Pad currents:");
    outer->addWidget(pads_label);

    pad_table_ = new QTableWidget(0, 3);
    pad_table_->setHorizontalHeaderLabels({"Pad", "Position (mm)", "Current (mA)"});
    pad_table_->verticalHeader()->setVisible(false);
    pad_table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    pad_table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    pad_table_->horizontalHeader()->setStretchLastSection(true);
    pad_table_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    pad_table_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    outer->addWidget(pad_table_, 1);

    sum_label_ = new QLabel("Sum: 0.000 mA");
    outer->addWidget(sum_label_);

    auto_btn_ = new QPushButton("Auto-balance");
    auto_btn_->setToolTip(
        "Set the first pad in the list to +Default I and the last to "
        "−Default I; clear the others. Useful starting point for one-source / "
        "one-sink scenarios.");
    outer->addWidget(auto_btn_);

    current_density_check_ = new QCheckBox("Show current density |J|");
    current_density_check_->setToolTip(
        "After running the solver, color the heat-map by sheet current "
        "density (A/m) instead of voltage. Reveals narrow copper sections "
        "where current crowds. Toggles without re-solving.");
    outer->addWidget(current_density_check_);

    thermal_check_ = new QCheckBox("With thermal coupling");
    thermal_check_->setToolTip(
        "Iterate the IR solve and copper resistivity until steady-state "
        "delta-T converges (alpha = 0.00393/C). Captures the fact that "
        "high-current rails heat up and copper resistance climbs.");
    outer->addWidget(thermal_check_);

    auto* th_form = new QFormLayout();
    th_form->setContentsMargins(0, 0, 0, 0);
    th_form->setSpacing(4);
    thermal_r_theta_spin_ = new QDoubleSpinBox();
    thermal_r_theta_spin_->setRange(1.0, 10000.0);
    thermal_r_theta_spin_->setDecimals(0);
    thermal_r_theta_spin_->setValue(100.0);
    thermal_r_theta_spin_->setSuffix(" K/W");
    thermal_r_theta_spin_->setEnabled(false);
    thermal_t_ambient_spin_ = new QDoubleSpinBox();
    thermal_t_ambient_spin_->setRange(-40.0, 200.0);
    thermal_t_ambient_spin_->setDecimals(1);
    thermal_t_ambient_spin_->setValue(25.0);
    thermal_t_ambient_spin_->setSuffix(" degC");
    thermal_t_ambient_spin_->setEnabled(false);
    th_form->addRow("R_theta:",  thermal_r_theta_spin_);
    th_form->addRow("T_ambient:", thermal_t_ambient_spin_);
    outer->addLayout(th_form);

    connect(thermal_check_, &QCheckBox::toggled, this, [this](bool on) {
        thermal_r_theta_spin_->setEnabled(on);
        thermal_t_ambient_spin_->setEnabled(on);
    });

    auto* btn_row = new QHBoxLayout();
    run_btn_ = new QPushButton("Run");
    clear_btn_ = new QPushButton("Clear");
    btn_row->addWidget(run_btn_);
    btn_row->addWidget(clear_btn_);
    outer->addLayout(btn_row);

    connect(net_combo_,   QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){
                onNetOrLayerChanged();
                if (net_combo_->count() > 0)
                    emit netChanged(net_combo_->currentData().toInt());
            });
    connect(layer_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AnalysisPanel::onNetOrLayerChanged);
    connect(pad_table_, &QTableWidget::cellChanged, this,
            [this](int, int){ updateSumLabel(); });
    connect(auto_btn_,    &QPushButton::clicked, this, &AnalysisPanel::onAutoBalance);
    connect(run_btn_,     &QPushButton::clicked, this, &AnalysisPanel::runRequested);
    connect(clear_btn_,   &QPushButton::clicked, this, &AnalysisPanel::clearRequested);
    connect(current_density_check_, &QCheckBox::toggled, this, [this](bool on) {
        emit viewModeChanged(static_cast<int>(on ? ViewMode::CurrentDensity
                                                  : ViewMode::Voltage));
    });
}

AnalysisPanel::ViewMode AnalysisPanel::viewMode() const {
    return current_density_check_->isChecked() ? ViewMode::CurrentDensity
                                                : ViewMode::Voltage;
}

void AnalysisPanel::setBoard(const circuitcore::board::Board* board) {
    board_ = board;

    QSignalBlocker bn(net_combo_);
    QSignalBlocker bl(layer_combo_);
    net_combo_->clear();
    layer_combo_->clear();

    if (!board_) {
        rebuildPadTable();
        return;
    }

    for (const auto& L : board_->stackup.layers) {
        if (!L.is_copper()) continue;
        layer_combo_->addItem(QString::fromStdString(L.name), L.ordinal);
    }

    std::set<int> nets_with_copper;
    for (const auto& z : board_->zones) {
        if (!z.filled.empty()) nets_with_copper.insert(z.net_id);
    }
    for (int id : nets_with_copper) {
        const auto* n = board_->find_net(id);
        const QString name = (n && !n->name.empty())
            ? QString::fromStdString(n->name)
            : QString("net %1").arg(id);
        net_combo_->addItem(QString("%1 (#%2)").arg(name).arg(id), id);
    }

    rebuildExtraLayers();
    rebuildPadTable();
}

void AnalysisPanel::onNetOrLayerChanged() {
    rebuildExtraLayers();
    rebuildPadTable();
}

void AnalysisPanel::rebuildExtraLayers() {
    // Remember which ordinals were checked so the user keeps their selection
    // when the primary layer changes.
    std::set<int> previously_checked;
    for (int i = 0; i < extra_layers_->count(); ++i) {
        auto* item = extra_layers_->item(i);
        if (item->checkState() == Qt::Checked) {
            previously_checked.insert(item->data(Qt::UserRole).toInt());
        }
    }

    extra_layers_->clear();
    if (!board_ || layer_combo_->count() == 0) return;
    const int primary = layer_combo_->currentData().toInt();
    for (const auto& L : board_->stackup.layers) {
        if (!L.is_copper()) continue;
        if (L.ordinal == primary) continue;
        auto* item = new QListWidgetItem(QString::fromStdString(L.name),
                                         extra_layers_);
        item->setData(Qt::UserRole, L.ordinal);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(previously_checked.count(L.ordinal)
                                ? Qt::Checked : Qt::Unchecked);
    }
}

void AnalysisPanel::rebuildPadTable() {
    QSignalBlocker blocker(pad_table_);
    pad_table_->setRowCount(0);
    if (!board_ || net_combo_->count() == 0 || layer_combo_->count() == 0) {
        return;
    }
    const int net = net_combo_->currentData().toInt();
    const int layer = layer_combo_->currentData().toInt();
    const double default_mA = default_current_spin_->value();

    // Collect target pads (preserve board insertion order).
    struct Row { QString name; double x, y; };
    std::vector<Row> rows;
    for (const auto& p : board_->pads) {
        if (p.net_id != net) continue;
        bool on_layer = false;
        for (int o : p.layer_ordinals) {
            if (o == layer) { on_layer = true; break; }
        }
        if (!on_layer) continue;
        rows.push_back({QString::fromStdString(
                            p.name.empty() ? std::string("(unnamed)") : p.name),
                        p.at.x * 1000.0, p.at.y * 1000.0});
    }

    pad_table_->setRowCount(static_cast<int>(rows.size()));
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& r = rows[i];
        pad_table_->setItem(i, 0, new QTableWidgetItem(r.name));
        pad_table_->setItem(i, 1,
            new QTableWidgetItem(QString("%1, %2")
                .arg(r.x, 0, 'f', 2).arg(r.y, 0, 'f', 2)));
        auto* spin = new QDoubleSpinBox();
        spin->setRange(-1.0e6, 1.0e6);
        spin->setDecimals(3);
        spin->setSingleStep(10.0);
        spin->setSuffix(" mA");
        // Default seed: first row = +default current source.
        // All other rows share the -default sink current evenly so the sum is
        // always zero -- works for any pad count, not just 2.
        double seed = 0.0;
        const int n = static_cast<int>(rows.size());
        if (n >= 2) {
            if (i == 0) seed = default_mA;
            else        seed = -default_mA / static_cast<double>(n - 1);
        } else if (n == 1 && i == 0) {
            seed = default_mA;  // pathological single-pad net
        }
        spin->setValue(seed);
        pad_table_->setCellWidget(i, 2, spin);
        QObject::connect(spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
                         this, [this](double){ updateSumLabel(); });
    }
    updateSumLabel();
}

void AnalysisPanel::onAutoBalance() {
    QSignalBlocker blocker(pad_table_);
    const int n = pad_table_->rowCount();
    if (n == 0) return;
    const double default_mA = default_current_spin_->value();
    for (int i = 0; i < n; ++i) {
        auto* s = row_spin(pad_table_, i);
        if (!s) continue;
        if (n >= 2) {
            if (i == 0) s->setValue(default_mA);
            else        s->setValue(-default_mA / static_cast<double>(n - 1));
        } else {
            s->setValue(default_mA);
        }
    }
    updateSumLabel();
}

void AnalysisPanel::updateSumLabel() {
    double sum_mA = 0.0;
    for (int r = 0; r < pad_table_->rowCount(); ++r) {
        auto* s = row_spin(pad_table_, r);
        if (s) sum_mA += s->value();
    }
    const bool balanced = std::abs(sum_mA) < 1e-6;
    sum_label_->setText(QString("Sum: %1 mA  %2")
        .arg(sum_mA, 0, 'f', 3)
        .arg(balanced ? "(balanced)" : "(unbalanced — solver will refuse)"));
    sum_label_->setStyleSheet(balanced
        ? "color: #88c088;"
        : "color: #d44; font-weight: bold;");
}

pdnkit::pi::MeshConfig AnalysisPanel::currentConfig() const {
    pdnkit::pi::MeshConfig cfg;
    cfg.cell_size = cell_size_spin_->value() * 1.0e-3;  // mm → m
    cfg.net_id = (net_combo_->count() > 0)
        ? net_combo_->currentData().toInt() : -1;
    cfg.layer_ordinal = (layer_combo_->count() > 0)
        ? layer_combo_->currentData().toInt() : 0;

    for (int i = 0; i < extra_layers_->count(); ++i) {
        auto* item = extra_layers_->item(i);
        if (item->checkState() == Qt::Checked) {
            cfg.extra_layer_ordinals.push_back(item->data(Qt::UserRole).toInt());
        }
    }

    for (int r = 0; r < pad_table_->rowCount(); ++r) {
        auto* s = row_spin(pad_table_, r);
        if (!s) continue;
        const double mA = s->value();
        if (mA == 0.0) continue;
        cfg.pad_currents[row_name(pad_table_, r).toStdString()] = mA * 1.0e-3;
    }
    return cfg;
}

void AnalysisPanel::setNetById(int net_id) {
    for (int i = 0; i < net_combo_->count(); ++i) {
        if (net_combo_->itemData(i).toInt() == net_id) {
            net_combo_->setCurrentIndex(i);
            break;
        }
    }
}

bool AnalysisPanel::thermalEnabled() const {
    return thermal_check_ && thermal_check_->isChecked();
}

double AnalysisPanel::thermalRThetaKw() const {
    return thermal_r_theta_spin_ ? thermal_r_theta_spin_->value() : 100.0;
}

double AnalysisPanel::thermalTAmbientC() const {
    return thermal_t_ambient_spin_ ? thermal_t_ambient_spin_->value() : 25.0;
}

