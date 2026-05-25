#include "PiTab.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "BoardModel.h"
#include "circuitcore/ui/PcbCanvas.h"

#include "pi/IrMesher.h"
#include "pi/IrSolver.h"

namespace circuitcore::studio {

PiTab::PiTab(BoardModel* model, QWidget* parent)
    : QWidget(parent),
      model_(model),
      canvas_(new ui::PcbCanvas(this)),
      net_combo_(new QComboBox(this)),
      layer_combo_(new QComboBox(this)),
      result_(new QLabel(this)) {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->addWidget(canvas_, 3);

    auto* panel = new QWidget(this);
    panel->setMaximumWidth(320);
    auto* col = new QVBoxLayout(panel);
    col->addWidget(new QLabel(tr("<b>Power Integrity</b>")));

    col->addWidget(new QLabel(tr("Net (power rail):")));
    net_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    col->addWidget(net_combo_);

    col->addWidget(new QLabel(tr("Layer:")));
    layer_combo_->setSizeAdjustPolicy(QComboBox::AdjustToContents);
    col->addWidget(layer_combo_);

    auto* run_btn = new QPushButton(tr("Run IR-drop (1 A)"), panel);
    col->addWidget(run_btn);

    result_->setWordWrap(true);
    result_->setStyleSheet("color: #d0d0d0;");
    result_->setText(tr("Load a board, pick a net + layer, then "
                          "Run IR-drop."));
    col->addWidget(result_);
    col->addStretch(1);
    root->addWidget(panel, 1);

    connect(model_, &BoardModel::boardLoaded, this, &PiTab::onBoardLoaded);
    connect(run_btn, &QPushButton::clicked, this, &PiTab::onRunIrDrop);
}

void PiTab::onBoardLoaded() {
    canvas_->setBoard(model_->board());
    refreshNetAndLayerLists();
}

void PiTab::refreshNetAndLayerLists() {
    net_combo_->clear();
    layer_combo_->clear();
    if (!model_->board()) return;

    // Net combo: every named net, but prefer power-rail-shaped names at
    // the top of the list so the obvious pick (VCC, VDD, 3V3, ...) is
    // pre-selected.
    auto looks_like_power = [](const std::string& n) {
        return n.find("VCC") != std::string::npos ||
                n.find("VDD") != std::string::npos ||
                n.find("3V3") != std::string::npos ||
                n.find("5V")  != std::string::npos ||
                n.find("GND") != std::string::npos ||
                (!n.empty() && (n.front() == '+' || n.front() == '-'));
    };
    std::vector<const board::Net*> power, other;
    for (const auto& n : model_->board()->nets) {
        if (n.id <= 0) continue;
        (looks_like_power(n.name) ? power : other).push_back(&n);
    }
    for (const auto* n : power) {
        net_combo_->addItem(QString::fromStdString(n->name), n->id);
    }
    if (!power.empty() && !other.empty()) net_combo_->insertSeparator(power.size());
    for (const auto* n : other) {
        net_combo_->addItem(QString::fromStdString(n->name), n->id);
    }

    for (const auto& L : model_->board()->stackup.layers) {
        if (!L.is_copper()) continue;
        layer_combo_->addItem(QString::fromStdString(L.name), L.ordinal);
    }
}

void PiTab::onRunIrDrop() {
    if (!model_->board()) {
        QMessageBox::information(this, tr("Run IR-drop"),
                                  tr("Load a board first (File > Open)."));
        return;
    }
    const int net_id = net_combo_->currentData().toInt();
    const int layer  = layer_combo_->currentData().toInt();
    if (net_id <= 0 || layer_combo_->currentIndex() < 0) {
        QMessageBox::information(this, tr("Run IR-drop"),
                                  tr("Pick a net + layer first."));
        return;
    }

    pdnkit::pi::MeshConfig mc;
    mc.cell_size = 0.5e-3;
    mc.net_id = net_id;
    mc.layer_ordinal = layer;
    mc.auto_select_layer = true;

    auto mesh = pdnkit::pi::IrMesher::build(*model_->board(), mc);
    if (mesh.nodes.empty()) {
        result_->setText(tr("Mesher produced no nodes -- net '%1' has no "
                              "filled copper on layer '%2'.")
                            .arg(net_combo_->currentText())
                            .arg(layer_combo_->currentText()));
        return;
    }
    if (mesh.source_node_ids.empty() || mesh.sink_node_ids.empty()) {
        result_->setText(tr("Need at least 2 pads on (net, layer) so the "
                              "auto-picker can place source and sink. "
                              "Found %1 source and %2 sink candidates.")
                            .arg(mesh.source_node_ids.size())
                            .arg(mesh.sink_node_ids.size()));
        return;
    }

    auto sol = pdnkit::pi::IrSolver::solve(mesh, {/*total_current=*/1.0});
    if (!sol.ok) {
        result_->setText(tr("Solver failed: %1").arg(
            QString::fromStdString(sol.error)));
        return;
    }

    const double drop_mv = (sol.max_v - sol.min_v) * 1e3;
    const auto* L = model_->board()->find_layer(mesh.primary_layer_used);
    QString reported_layer = layer_combo_->currentText();
    if (L && mesh.primary_layer_used != layer) {
        reported_layer = QString("%1 (auto)").arg(QString::fromStdString(L->name));
    }

    result_->setText(
        tr("<b>%1</b> on <b>%2</b><br>"
           "%3 mesh nodes, %4 resistors<br>"
           "Vmax = %5 mV, Vmin = %6 mV<br>"
           "<b>Drop = %7 mV</b> at 1 A")
            .arg(net_combo_->currentText())
            .arg(reported_layer)
            .arg(mesh.nodes.size())
            .arg(mesh.resistors.size())
            .arg(sol.max_v * 1e3, 0, 'f', 4)
            .arg(sol.min_v * 1e3, 0, 'f', 4)
            .arg(drop_mv,        0, 'f', 4));
}

}  // namespace circuitcore::studio
