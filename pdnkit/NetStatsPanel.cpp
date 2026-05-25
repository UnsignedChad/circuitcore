#include "NetStatsPanel.h"

#include <cmath>
#include <unordered_map>

#include <QHeaderView>
#include <QLabel>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QVBoxLayout>
#include <algorithm>

namespace {

// Signed area (shoelace). Returns absolute value.
double polygon_area(const std::vector<circuitcore::board::Point2>& pts) {
    if (pts.size() < 3) return 0.0;
    double a = 0.0;
    for (std::size_t i = 0; i < pts.size(); ++i) {
        const std::size_t j = (i + 1) % pts.size();
        a += pts[i].x * pts[j].y - pts[j].x * pts[i].y;
    }
    return std::abs(a) * 0.5;
}

// Polygon area minus hole areas.
double polygon_with_holes_area(const circuitcore::board::Polygon& p) {
    double a = polygon_area(p.outline);
    for (const auto& h : p.holes) a -= polygon_area(h);
    return std::max(0.0, a);
}

// Numeric table item that sorts by value, not text.
class NumberItem : public QTableWidgetItem {
public:
    explicit NumberItem(double v, int decimals = 2)
        : QTableWidgetItem(QString::number(v, 'f', decimals)), value_(v) {}
    bool operator<(const QTableWidgetItem& other) const override {
        if (auto* o = dynamic_cast<const NumberItem*>(&other)) {
            return value_ < o->value_;
        }
        return QTableWidgetItem::operator<(other);
    }
private:
    double value_;
};

}  // namespace

NetStatsPanel::NetStatsPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(6);

    auto* header = new QLabel("Net statistics");
    QFont f = header->font();
    f.setBold(true);
    header->setFont(f);
    outer->addWidget(header);

    table_ = new QTableWidget(0, 6);
    table_->setHorizontalHeaderLabels(
        {"Net", "ID", "Pads", "Segs", "Length (mm)", "Area (mm²)"});
    table_->verticalHeader()->setVisible(false);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSortingEnabled(true);
    table_->horizontalHeader()->setStretchLastSection(false);
    table_->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    outer->addWidget(table_, 1);

    // Click a row -> emit the net's id. The ID lives in column 1 as a
    // NumberItem; read its text and parse to int. -1 on parse failure
    // (filtered before emission).
    QObject::connect(table_, &QTableWidget::itemSelectionChanged, this, [this]() {
        auto rows = table_->selectionModel()->selectedRows();
        if (rows.isEmpty()) return;
        auto* item = table_->item(rows.first().row(), 1);
        if (!item) return;
        bool ok = false;
        const int id = item->text().toInt(&ok);
        if (ok) emit netSelected(id);
    });
}

void NetStatsPanel::setBoard(const circuitcore::board::Board* board) {
    board_ = board;
    rebuild();
}

void NetStatsPanel::rebuild() {
    table_->setSortingEnabled(false);  // freeze during populate
    table_->setRowCount(0);

    if (!board_) {
        table_->setSortingEnabled(true);
        return;
    }

    struct Acc {
        int pads = 0;
        int segs = 0;
        double length_m = 0.0;
        double area_m2 = 0.0;
    };
    std::unordered_map<int, Acc> by_net;

    for (const auto& p : board_->pads) by_net[p.net_id].pads += 1;
    for (const auto& s : board_->segments) {
        Acc& a = by_net[s.net_id];
        a.segs += 1;
        a.length_m += std::hypot(s.end.x - s.start.x, s.end.y - s.start.y);
    }
    for (const auto& z : board_->zones) {
        Acc& a = by_net[z.net_id];
        for (const auto& fp : z.filled) a.area_m2 += polygon_with_holes_area(fp);
    }

    // Add a row for every net in the board, even if it has no copper, so the
    // user sees the full list. Skip the empty-name net 0 if it has nothing.
    int row = 0;
    for (const auto& n : board_->nets) {
        Acc a;
        if (auto it = by_net.find(n.id); it != by_net.end()) a = it->second;
        if (n.id == 0 && a.pads == 0 && a.segs == 0 && a.area_m2 == 0.0) continue;

        table_->insertRow(row);
        auto* name_item = new QTableWidgetItem(n.name.empty()
                                                ? QString("(unnamed)")
                                                : QString::fromStdString(n.name));
        // Power-rail-like names (+3V3 / VCC / GND / etc.) get bold so the
        // typical analysis target stands out at a glance.
        const std::string& nm = n.name;
        const bool is_power_rail =
            !nm.empty() && (nm[0] == "+"[0] ||
                            nm == "GND" || nm == "VCC" ||
                            nm == "VDD" || nm == "VBUS" ||
                            nm == "VEE" || nm == "VSS");
        if (is_power_rail) {
            QFont f = name_item->font();
            f.setBold(true);
            name_item->setFont(f);
        }
        table_->setItem(row, 0, name_item);
        table_->setItem(row, 1, new NumberItem(n.id, 0));
        table_->setItem(row, 2, new NumberItem(a.pads, 0));
        table_->setItem(row, 3, new NumberItem(a.segs, 0));
        table_->setItem(row, 4, new NumberItem(a.length_m * 1000.0, 2));
        table_->setItem(row, 5, new NumberItem(a.area_m2 * 1.0e6, 2));
        ++row;
    }

    table_->setSortingEnabled(true);
    // Default sort: largest area first (good proxy for "this is a power rail").
    table_->sortItems(5, Qt::DescendingOrder);
}
