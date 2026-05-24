#include "CavityPanel.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <set>
#include <vector>

#include <QFile>
#include <QFileDialog>
#include <QMessageBox>
#include <QTextStream>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>
#include <QInputDialog>
#include <QHeaderView>
#include <QSpinBox>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>

#include "ZfPlotWidget.h"
#include "pi/CavityModel.h"
#include "pi/TargetZ.h"
#include "pi/DecapOptimizer.h"

namespace {

std::vector<circuitcore::board::Point2> read_decap_positions(QTableWidget* t,
                                                        double offset_x_m,
                                                        double offset_y_m) {
    std::vector<circuitcore::board::Point2> out;
    for (int r = 0; r < t->rowCount(); ++r) {
        auto* xi = t->item(r, 0);
        auto* yi = t->item(r, 1);
        if (!xi || !yi) continue;
        out.push_back({xi->text().toDouble() * 1.0e-3 + offset_x_m,
                       yi->text().toDouble() * 1.0e-3 + offset_y_m});
    }
    return out;
}

// Bounding box of the filled zones on (net, primary copper layer 0). Empty
// returned when there is no zone fill matching the filter — caller decides.
struct Bbox {
    bool ok = false;
    double lo_x = 0.0, lo_y = 0.0, hi_x = 0.0, hi_y = 0.0;
};

Bbox zone_bbox(const circuitcore::board::Board& b, int net_id, int layer) {
    Bbox box;
    for (const auto& z : b.zones) {
        if (z.net_id != net_id || z.layer_ordinal != layer) continue;
        for (const auto& fp : z.filled) {
            for (const auto& p : fp.outline) {
                if (!box.ok) {
                    box.lo_x = box.hi_x = p.x;
                    box.lo_y = box.hi_y = p.y;
                    box.ok = true;
                } else {
                    if (p.x < box.lo_x) box.lo_x = p.x;
                    if (p.x > box.hi_x) box.hi_x = p.x;
                    if (p.y < box.lo_y) box.lo_y = p.y;
                    if (p.y > box.hi_y) box.hi_y = p.y;
                }
            }
        }
    }
    return box;
}

}  // namespace

CavityPanel::CavityPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    auto* header = new QLabel("Plane Z(f)  (cavity model)");
    QFont f = header->font();
    f.setBold(true);
    header->setFont(f);
    outer->addWidget(header);

    auto* form = new QFormLayout();
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(4);

    net_combo_ = new QComboBox();

    eps_r_spin_ = new QDoubleSpinBox();
    eps_r_spin_->setRange(1.0, 20.0);
    eps_r_spin_->setDecimals(2);
    eps_r_spin_->setValue(4.3);

    tan_delta_spin_ = new QDoubleSpinBox();
    tan_delta_spin_->setRange(0.0, 0.5);
    tan_delta_spin_->setDecimals(4);
    tan_delta_spin_->setSingleStep(0.001);
    tan_delta_spin_->setValue(0.020);

    thickness_spin_ = new QDoubleSpinBox();
    thickness_spin_->setRange(0.05, 10.0);
    thickness_spin_->setDecimals(3);
    thickness_spin_->setValue(1.6);
    thickness_spin_->setSuffix(" mm");

    port1_x_ = new QDoubleSpinBox(); port1_x_->setRange(0.0, 1000.0); port1_x_->setDecimals(2); port1_x_->setSuffix(" mm");
    port1_y_ = new QDoubleSpinBox(); port1_y_->setRange(0.0, 1000.0); port1_y_->setDecimals(2); port1_y_->setSuffix(" mm");
    port2_x_ = new QDoubleSpinBox(); port2_x_->setRange(0.0, 1000.0); port2_x_->setDecimals(2); port2_x_->setSuffix(" mm");
    port2_y_ = new QDoubleSpinBox(); port2_y_->setRange(0.0, 1000.0); port2_y_->setDecimals(2); port2_y_->setSuffix(" mm");

    f_min_spin_ = new QDoubleSpinBox();
    f_min_spin_->setRange(1.0, 1.0e10);
    f_min_spin_->setDecimals(0);
    f_min_spin_->setValue(1.0e6);
    f_min_spin_->setSuffix(" Hz");

    f_max_spin_ = new QDoubleSpinBox();
    f_max_spin_->setRange(1.0e3, 1.0e12);
    f_max_spin_->setDecimals(0);
    f_max_spin_->setValue(5.0e9);
    f_max_spin_->setSuffix(" Hz");

    points_spin_ = new QSpinBox();
    points_spin_->setRange(10, 5000);
    points_spin_->setValue(300);

    modes_spin_ = new QSpinBox();
    modes_spin_->setRange(5, 200);
    modes_spin_->setValue(30);

    form->addRow("Net:",       net_combo_);
    form->addRow("eps_r:",     eps_r_spin_);
    form->addRow("tan delta:", tan_delta_spin_);
    form->addRow("d:",         thickness_spin_);
    form->addRow("Port1 X:",   port1_x_);
    form->addRow("Port1 Y:",   port1_y_);
    form->addRow("Port2 X:",   port2_x_);
    form->addRow("Port2 Y:",   port2_y_);
    form->addRow("f_min:",     f_min_spin_);
    form->addRow("f_max:",     f_max_spin_);
    form->addRow("Points:",    points_spin_);
    form->addRow("Modes:",     modes_spin_);

    target_z_spin_ = new QDoubleSpinBox();
    target_z_spin_->setRange(0.0, 1000.0);
    target_z_spin_->setDecimals(3);
    target_z_spin_->setValue(0.025);  // 25 mOhm, a common PDN target
    target_z_spin_->setSuffix(" ohm");
    target_z_spin_->setSingleStep(0.005);

    auto* tz_row = new QHBoxLayout();
    tz_row->setContentsMargins(0, 0, 0, 0);
    tz_row->addWidget(target_z_spin_, 1);
    auto* tz_btn = new QPushButton("From spec...");
    tz_btn->setToolTip(
        "Compute Z_target = V_nom * V_tol / I_step from the load's "
        "datasheet (Larry Smith flat-target form).");
    tz_row->addWidget(tz_btn);
    form->addRow("Target Z:", tz_row);

    connect(tz_btn, &QPushButton::clicked, this, [this]() {
        bool ok = false;
        const double v_nom = QInputDialog::getDouble(
            this, "Target Z from spec",
            "Nominal supply voltage (V):", 3.3, 0.001, 100.0, 3, &ok);
        if (!ok) return;
        const double v_tol_pct = QInputDialog::getDouble(
            this, "Target Z from spec",
            "Allowable voltage tolerance (%):", 5.0, 0.01, 50.0, 2, &ok);
        if (!ok) return;
        const double i_step = QInputDialog::getDouble(
            this, "Target Z from spec",
            "Peak step current (A):", 1.0, 0.001, 1000.0, 3, &ok);
        if (!ok) return;
        pdnkit::pi::TargetZSpec spec{v_nom, v_tol_pct / 100.0, i_step};
        const double z = pdnkit::pi::target_impedance_flat(spec);
        target_z_spin_->setValue(z);
    });

    overlay_bare_check_ = new QCheckBox("Overlay bare plane");
    overlay_bare_check_->setChecked(true);
    form->addRow("", overlay_bare_check_);
    outer->addLayout(form);

    plane_info_label_ = new QLabel("Plane: (no board)");
    plane_info_label_->setStyleSheet("color: #aaa;");
    plane_info_label_->setWordWrap(true);
    outer->addWidget(plane_info_label_);

    auto* decaps_label = new QLabel("Decoupling capacitors:");
    outer->addWidget(decaps_label);
    decap_table_ = new QTableWidget(0, 5);
    decap_table_->setHorizontalHeaderLabels(
        {"X (mm)", "Y (mm)", "C (uF)", "ESR (mOhm)", "ESL (nH)"});
    decap_table_->verticalHeader()->setVisible(false);
    decap_table_->setMaximumHeight(120);
    decap_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    outer->addWidget(decap_table_);

    auto* dec_btn_row = new QHBoxLayout();
    add_decap_btn_    = new QPushButton("Add decap");
    remove_decap_btn_ = new QPushButton("Remove selected");
    auto_decap_btn_   = new QPushButton("Auto-suggest");
    auto_decap_btn_->setToolTip(
        "Greedy decap selection from a small library. Adds capacitors near "
        "port 1 until the target impedance is met or the cap budget is hit.");
    dec_btn_row->addWidget(add_decap_btn_);
    dec_btn_row->addWidget(remove_decap_btn_);
    dec_btn_row->addWidget(auto_decap_btn_);
    outer->addLayout(dec_btn_row);

    auto* mode_btn = new QPushButton("Show mode shape at peak");
    mode_btn->setToolTip(
        "After running a sweep, click to overlay the standing-wave voltage "
        "pattern at the largest |Z(f)| peak on the canvas.");
    outer->addWidget(mode_btn);
    connect(mode_btn, &QPushButton::clicked, this, &CavityPanel::onShowModeShape);

    auto* btn_row = new QHBoxLayout();
    run_btn_   = new QPushButton("Run sweep");
    save_btn_  = new QPushButton("Save CSV...");
    clear_btn_ = new QPushButton("Clear");
    btn_row->addWidget(run_btn_);
    btn_row->addWidget(save_btn_);
    btn_row->addWidget(clear_btn_);
    outer->addLayout(btn_row);

    plot_ = new ZfPlotWidget(this);
    outer->addWidget(plot_, 1);

    connect(net_combo_, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int){ emitCavity(); updatePlaneInfo(); });
    connect(port1_x_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){ emitCavity(); });
    connect(port1_y_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){ emitCavity(); });
    connect(port2_x_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){ emitCavity(); });
    connect(port2_y_, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double){ emitCavity(); });
    connect(run_btn_,   &QPushButton::clicked, this, &CavityPanel::onRun);
    connect(save_btn_,  &QPushButton::clicked, this, &CavityPanel::onSaveCsv);
    connect(clear_btn_, &QPushButton::clicked, this, &CavityPanel::onClear);
    connect(add_decap_btn_,    &QPushButton::clicked, this, &CavityPanel::onAddDecap);
    connect(remove_decap_btn_, &QPushButton::clicked, this, &CavityPanel::onRemoveDecap);
    connect(auto_decap_btn_,   &QPushButton::clicked, this, &CavityPanel::onAutoSuggest);
    connect(decap_table_, &QTableWidget::itemChanged, this, [this](QTableWidgetItem*) {
        emit decapsChanged(read_decap_positions(decap_table_, 0.0, 0.0));
    });
}

void CavityPanel::onAddDecap() {
    const int row = decap_table_->rowCount();
    decap_table_->insertRow(row);
    // Defaults: at plane center, 1uF, 5 mOhm ESR, 0.5nH ESL.
    decap_table_->setItem(row, 0, new QTableWidgetItem(QString::number(0.0)));
    decap_table_->setItem(row, 1, new QTableWidgetItem(QString::number(0.0)));
    decap_table_->setItem(row, 2, new QTableWidgetItem(QString::number(1.0)));     // 1 uF
    decap_table_->setItem(row, 3, new QTableWidgetItem(QString::number(5.0)));     // 5 mOhm
    decap_table_->setItem(row, 4, new QTableWidgetItem(QString::number(0.5)));     // 0.5 nH
    emit decapsChanged(read_decap_positions(decap_table_, 0.0, 0.0));
}

void CavityPanel::onRemoveDecap() {
    auto rows = decap_table_->selectionModel()->selectedRows();
    // Remove in descending order so indices stay valid.
    std::vector<int> idx;
    for (auto& r : rows) idx.push_back(r.row());
    std::sort(idx.rbegin(), idx.rend());
    for (int r : idx) decap_table_->removeRow(r);
    emit decapsChanged(read_decap_positions(decap_table_, 0.0, 0.0));
}

namespace {

// Shoelace polygon area; needed for fill-ratio diagnostic.
double polygon_ring_area(const std::vector<circuitcore::board::Point2>& ring) {
    if (ring.size() < 3) return 0.0;
    double a = 0.0;
    for (std::size_t i = 0; i < ring.size(); ++i) {
        const std::size_t j = (i + 1) % ring.size();
        a += ring[i].x * ring[j].y - ring[j].x * ring[i].y;
    }
    return std::abs(a) * 0.5;
}

// Total filled-zone area for (net, layer) in m^2 (outline minus holes).
double zone_filled_area(const circuitcore::board::Board& b, int net, int layer) {
    double total = 0.0;
    for (const auto& z : b.zones) {
        if (z.net_id != net || z.layer_ordinal != layer) continue;
        for (const auto& fp : z.filled) {
            double a = polygon_ring_area(fp.outline);
            for (const auto& h : fp.holes) a -= polygon_ring_area(h);
            total += std::max(0.0, a);
        }
    }
    return total;
}

std::vector<circuitcore::board::Point2> port_positions(double p1x_mm, double p1y_mm,
                                                   double p2x_mm, double p2y_mm,
                                                   double offset_x_m, double offset_y_m) {
    return {
        {p1x_mm * 1.0e-3 + offset_x_m, p1y_mm * 1.0e-3 + offset_y_m},
        {p2x_mm * 1.0e-3 + offset_x_m, p2y_mm * 1.0e-3 + offset_y_m},
    };
}

}  // namespace

void CavityPanel::emitCavity() {
    if (!board_ || net_combo_->count() == 0) {
        emit cavityChanged(0, 0, 0, 0, {});
        return;
    }
    const int net = net_combo_->currentData().toInt();
    constexpr int kPrimaryLayer = 0;
    const Bbox bb = zone_bbox(*board_, net, kPrimaryLayer);
    if (!bb.ok) {
        emit cavityChanged(0, 0, 0, 0, {});
        return;
    }
    // Port positions are panel inputs in plane-local mm; translate to world
    // coords by adding the bbox lo corner.
    auto ports = port_positions(port1_x_->value(), port1_y_->value(),
                                port2_x_->value(), port2_y_->value(),
                                bb.lo_x, bb.lo_y);
    emit cavityChanged(bb.lo_x, bb.lo_y, bb.hi_x, bb.hi_y, ports);
}

void CavityPanel::setBoard(const circuitcore::board::Board* board) {
    board_ = board;
    rebuildNetCombo();
    plot_->clear();
    emitCavity();
    updatePlaneInfo();
}

void CavityPanel::updatePlaneInfo() {
    if (!board_ || net_combo_->count() == 0) {
        plane_info_label_->setText("Plane: (no net selected)");
        plane_info_label_->setStyleSheet("color: #aaa;");
        return;
    }
    const int net = net_combo_->currentData().toInt();
    constexpr int kPrimaryLayer = 0;
    const Bbox bb = zone_bbox(*board_, net, kPrimaryLayer);
    if (!bb.ok) {
        plane_info_label_->setText("Plane: no filled zone on F.Cu for this net");
        plane_info_label_->setStyleSheet("color: #d80;");
        return;
    }
    const double a_mm = (bb.hi_x - bb.lo_x) * 1000.0;
    const double b_mm = (bb.hi_y - bb.lo_y) * 1000.0;
    const double bbox_area = (bb.hi_x - bb.lo_x) * (bb.hi_y - bb.lo_y);
    const double fill_area = zone_filled_area(*board_, net, kPrimaryLayer);
    const double ratio = (bbox_area > 0.0) ? (fill_area / bbox_area) : 0.0;
    const int pct = static_cast<int>(std::round(ratio * 100.0));

    QString msg = QString("Plane: %1 x %2 mm  fill %3%%  ")
                      .arg(a_mm, 0, 'f', 1)
                      .arg(b_mm, 0, 'f', 1)
                      .arg(pct);
    if (ratio >= 0.85) {
        msg += "(cavity model OK)";
        plane_info_label_->setStyleSheet("color: #8c8;");
    } else if (ratio >= 0.55) {
        msg += "(approximate — cavity assumes rectangular plane)";
        plane_info_label_->setStyleSheet("color: #d80; font-weight: bold;");
    } else {
        msg += "(NON-RECTANGULAR — Z(f) is a rough bound only)";
        plane_info_label_->setStyleSheet("color: #d44; font-weight: bold;");
    }
    plane_info_label_->setText(msg);
}

void CavityPanel::rebuildNetCombo() {
    net_combo_->clear();
    if (!board_) return;
    std::set<int> nets;
    for (const auto& z : board_->zones) {
        if (!z.filled.empty()) nets.insert(z.net_id);
    }
    for (int id : nets) {
        const auto* n = board_->find_net(id);
        const QString name = (n && !n->name.empty())
            ? QString::fromStdString(n->name)
            : QString("net %1").arg(id);
        net_combo_->addItem(QString("%1 (#%2)").arg(name).arg(id), id);
    }
}

void CavityPanel::onRun() {
    if (!board_ || net_combo_->count() == 0) return;
    const int net = net_combo_->currentData().toInt();
    constexpr int kPrimaryLayer = 0;  // F.Cu for v0

    const Bbox bb = zone_bbox(*board_, net, kPrimaryLayer);
    if (!bb.ok) return;

    pdnkit::pi::CavityConfig cfg;
    cfg.a = bb.hi_x - bb.lo_x;
    cfg.b = bb.hi_y - bb.lo_y;
    cfg.d = thickness_spin_->value() * 1.0e-3;
    cfg.eps_r = eps_r_spin_->value();
    cfg.tan_delta = tan_delta_spin_->value();
    cfg.max_modes = modes_spin_->value();

    // Port positions are panel inputs in mm, relative to the plane corner
    // (which sits at world (bb.lo_x, bb.lo_y) — but the cavity formula uses
    // local coordinates so we just use them directly).
    const double x1 = port1_x_->value() * 1.0e-3;
    const double y1 = port1_y_->value() * 1.0e-3;
    const double x2 = port2_x_->value() * 1.0e-3;
    const double y2 = port2_y_->value() * 1.0e-3;

    const int N = points_spin_->value();
    const double f_lo = f_min_spin_->value();
    const double f_hi = f_max_spin_->value();
    std::vector<double> freqs;
    freqs.reserve(N);
    const double log_lo = std::log10(f_lo);
    const double log_hi = std::log10(f_hi);
    for (int i = 0; i < N; ++i) {
        const double t = (N == 1) ? 0.0 : static_cast<double>(i) / (N - 1);
        freqs.push_back(std::pow(10.0, log_lo + t * (log_hi - log_lo)));
    }
    // Collect decaps from the table; treat blank cells as 0.
    std::vector<pdnkit::pi::Decap> decaps;
    for (int row = 0; row < decap_table_->rowCount(); ++row) {
        auto read = [&](int col) -> double {
            auto* item = decap_table_->item(row, col);
            return item ? item->text().toDouble() : 0.0;
        };
        pdnkit::pi::Decap d;
        d.x   = read(0) * 1.0e-3;   // mm -> m
        d.y   = read(1) * 1.0e-3;
        d.C   = read(2) * 1.0e-6;   // uF -> F
        d.esr = read(3) * 1.0e-3;   // mOhm -> Ohm
        d.esl = read(4) * 1.0e-9;   // nH -> H
        if (d.C > 0.0) decaps.push_back(d);
    }

    std::vector<ZfPlotWidget::Curve> curves;

    std::vector<double> mags_main;
    if (decaps.empty()) {
        mags_main = pdnkit::pi::cavity_impedance_magnitude_sweep(
            cfg, x1, y1, x2, y2, freqs);
        ZfPlotWidget::Curve c;
        c.freqs = freqs;
        c.mags  = mags_main;
        c.color = QColor(0xfd, 0xe7, 0x25);   // viridis yellow
        c.label = "Z(f)";
        curves.push_back(std::move(c));
    } else {
        mags_main = pdnkit::pi::cavity_impedance_with_decaps_magnitude_sweep(
            cfg, x1, y1, decaps, freqs);
        ZfPlotWidget::Curve c;
        c.freqs = freqs;
        c.mags  = mags_main;
        c.color = QColor(0xfd, 0xe7, 0x25);
        c.label = "with decaps";
        curves.push_back(std::move(c));

        // Overlay bare plane Z if requested.
        if (overlay_bare_check_->isChecked()) {
            auto mags_bare = pdnkit::pi::cavity_impedance_magnitude_sweep(
                cfg, x1, y1, x1, y1, freqs);
            ZfPlotWidget::Curve b;
            b.freqs = freqs;
            b.mags  = mags_bare;
            b.color = QColor(0x77, 0x88, 0xaa);  // muted blue-gray
            b.label = "bare plane";
            curves.push_back(std::move(b));
        }
    }

    last_freqs_ = freqs;
    last_mags_  = mags_main;
    plot_->setTargetImpedance(target_z_spin_->value());
    plot_->setCurves(std::move(curves));
}

void CavityPanel::onSaveCsv() {
    if (last_freqs_.empty() || last_freqs_.size() != last_mags_.size()) {
        QMessageBox::information(this, "Z(f) export",
            "No sweep result to export. Click Run sweep first.");
        return;
    }
    QString path = QFileDialog::getSaveFileName(this, "Export Z(f) sweep as CSV",
                                                  QString(), "CSV (*.csv)");
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, "Export failed",
                             QString("Could not open %1 for writing").arg(path));
        return;
    }
    QTextStream out(&f);
    out << "freq_hz,abs_z_ohm\n";
    for (std::size_t i = 0; i < last_freqs_.size(); ++i) {
        out << QString::number(last_freqs_[i], 'g', 8) << ','
            << QString::number(last_mags_[i],  'g', 8) << '\n';
    }
}

void CavityPanel::onClear() {
    last_freqs_.clear();
    last_mags_.clear();
    plot_->clear();
}

void CavityPanel::onAutoSuggest() {
    if (!board_ || net_combo_->count() == 0) {
        QMessageBox::information(this, "Auto-suggest decaps",
            "Load a board and pick a net first.");
        return;
    }
    const int net = net_combo_->currentData().toInt();
    constexpr int kPrimaryLayer = 0;
    const Bbox bb = zone_bbox(*board_, net, kPrimaryLayer);
    if (!bb.ok) {
        QMessageBox::warning(this, "Auto-suggest decaps",
            "No filled zones for the selected net on F.Cu.");
        return;
    }

    pdnkit::pi::CavityConfig cfg;
    cfg.a = bb.hi_x - bb.lo_x;
    cfg.b = bb.hi_y - bb.lo_y;
    cfg.d = thickness_spin_->value() * 1.0e-3;
    cfg.eps_r = eps_r_spin_->value();
    cfg.tan_delta = tan_delta_spin_->value();
    cfg.max_modes = modes_spin_->value();

    pdnkit::pi::DecapOptimizerConfig opt;
    opt.target_z = target_z_spin_->value();
    opt.f_min = f_min_spin_->value();
    opt.f_max = f_max_spin_->value();
    opt.n_points = std::min(points_spin_->value(), 80);  // cap for search speed
    opt.max_caps = 30;
    opt.cap_x = port1_x_->value() * 1.0e-3;
    opt.cap_y = port1_y_->value() * 1.0e-3;

    auto result = pdnkit::pi::optimize_decaps(
        cfg, port1_x_->value() * 1.0e-3, port1_y_->value() * 1.0e-3, opt);

    // Replace the table with the suggested decaps.
    decap_table_->setRowCount(0);
    for (const auto& d : result.decaps) {
        const int row = decap_table_->rowCount();
        decap_table_->insertRow(row);
        decap_table_->setItem(row, 0, new QTableWidgetItem(QString::number(d.x * 1e3, 'f', 2)));
        decap_table_->setItem(row, 1, new QTableWidgetItem(QString::number(d.y * 1e3, 'f', 2)));
        decap_table_->setItem(row, 2, new QTableWidgetItem(QString::number(d.C * 1e6, 'g', 4)));
        decap_table_->setItem(row, 3, new QTableWidgetItem(QString::number(d.esr * 1e3, 'f', 1)));
        decap_table_->setItem(row, 4, new QTableWidgetItem(QString::number(d.esl * 1e9, 'f', 2)));
    }
    emit decapsChanged(read_decap_positions(decap_table_, 0.0, 0.0));

    const QString msg = QString(
        "Suggested %1 decap%2.  Final max |Z| over sweep: %3 mOhm.  "
        "Target %4 mOhm: %5")
        .arg(result.decaps.size())
        .arg(result.decaps.size() == 1 ? "" : "s")
        .arg(result.final_max_z * 1000.0, 0, 'f', 3)
        .arg(opt.target_z * 1000.0, 0, 'f', 3)
        .arg(result.target_met ? "met" : "NOT met (cap budget exhausted)");
    QMessageBox::information(this, "Auto-suggest decaps", msg);
}

void CavityPanel::onShowModeShape() {
    if (last_freqs_.empty() || last_mags_.empty() ||
        last_freqs_.size() != last_mags_.size()) {
        QMessageBox::information(this, "Mode shape",
            "Run a Z(f) sweep first.");
        return;
    }
    // Find peak frequency.
    std::size_t peak_i = 0;
    double peak_v = last_mags_[0];
    for (std::size_t i = 1; i < last_mags_.size(); ++i) {
        if (last_mags_[i] > peak_v) { peak_v = last_mags_[i]; peak_i = i; }
    }
    const double f_peak = last_freqs_[peak_i];
    const double omega = 2.0 * 3.141592653589793 * f_peak;

    if (!board_ || net_combo_->count() == 0) return;
    const int net = net_combo_->currentData().toInt();
    constexpr int kPrimaryLayer = 0;
    const Bbox bb = zone_bbox(*board_, net, kPrimaryLayer);
    if (!bb.ok) return;

    pdnkit::pi::CavityConfig cfg;
    cfg.a = bb.hi_x - bb.lo_x;
    cfg.b = bb.hi_y - bb.lo_y;
    cfg.d = thickness_spin_->value() * 1.0e-3;
    cfg.eps_r = eps_r_spin_->value();
    cfg.tan_delta = tan_delta_spin_->value();
    cfg.max_modes = modes_spin_->value();

    const double obs_x = port1_x_->value() * 1.0e-3;
    const double obs_y = port1_y_->value() * 1.0e-3;

    // Grid resolution: ~80 cells across the longer dimension.
    constexpr int kGridLong = 80;
    const int nx = std::max(8, static_cast<int>(kGridLong * cfg.a / std::max(cfg.a, cfg.b)));
    const int ny = std::max(8, static_cast<int>(kGridLong * cfg.b / std::max(cfg.a, cfg.b)));

    auto mags = pdnkit::pi::cavity_mode_shape_grid(cfg, obs_x, obs_y, omega, nx, ny);
    auto mesh = pdnkit::render::build_grid_mesh(
        mags, nx, ny, cfg.a / nx, cfg.b / ny, bb.lo_x, bb.lo_y);

    const QString units = (peak_v >= 1.0) ? "ohm"
                                          : (peak_v >= 1.0e-3) ? "mohm" : "uohm";
    const double scaled = (peak_v >= 1.0) ? peak_v
                                          : (peak_v >= 1.0e-3) ? peak_v * 1000.0
                                                                 : peak_v * 1.0e6;
    QString msg = QString("Mode shape at peak %1 MHz (|Z| %2 %3)")
                      .arg(f_peak / 1.0e6, 0, 'f', 1)
                      .arg(scaled, 0, 'f', 3)
                      .arg(units);
    QMessageBox::information(this, "Mode shape", msg);

    emit modeShapeMesh(std::move(mesh));
}

void CavityPanel::setNetById(int net_id) {
    for (int i = 0; i < net_combo_->count(); ++i) {
        if (net_combo_->itemData(i).toInt() == net_id) {
            net_combo_->setCurrentIndex(i);
            break;
        }
    }
}
