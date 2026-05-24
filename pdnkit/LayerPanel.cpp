#include "LayerPanel.h"

#include <QCheckBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "render/LayerColors.h"

namespace {

// Build a small (14x14) flat-colored swatch label.
QLabel* makeSwatch(int ordinal) {
    auto rgba = pdnkit::render::layer_color(ordinal);
    const int r = static_cast<int>(rgba[0] * 255.0f);
    const int g = static_cast<int>(rgba[1] * 255.0f);
    const int b = static_cast<int>(rgba[2] * 255.0f);
    auto* w = new QLabel();
    w->setFixedSize(14, 14);
    w->setStyleSheet(QString("background-color: rgb(%1,%2,%3); border: 1px solid #303030;")
                         .arg(r).arg(g).arg(b));
    return w;
}

}  // namespace

LayerPanel::LayerPanel(QWidget* parent) : QWidget(parent) {
    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(8, 8, 8, 8);
    outer->setSpacing(4);

    auto* header = new QLabel("Layers");
    QFont f = header->font();
    f.setBold(true);
    header->setFont(f);
    outer->addWidget(header);

    rows_layout_ = new QVBoxLayout();
    rows_layout_->setContentsMargins(0, 0, 0, 0);
    rows_layout_->setSpacing(2);
    outer->addLayout(rows_layout_);
    outer->addStretch();
}

void LayerPanel::clearRows() {
    while (auto* item = rows_layout_->takeAt(0)) {
        if (auto* w = item->widget()) w->deleteLater();
        delete item;
    }
}

void LayerPanel::setLayers(const std::vector<Entry>& layers) {
    clearRows();
    for (const auto& L : layers) {
        auto* row = new QWidget();
        auto* h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(6);

        auto* cb = new QCheckBox();
        cb->setChecked(true);
        const int ord = L.ordinal;
        connect(cb, &QCheckBox::toggled, this, [this, ord](bool on) {
            emit visibility_changed(ord, on);
        });
        h->addWidget(cb);

        h->addWidget(makeSwatch(L.ordinal));

        auto* name = new QLabel(L.name);
        name->setMinimumWidth(70);
        h->addWidget(name);

        // Thickness label -- pulled from the LayerPanel Entry. Empty if the
        // (setup (stackup ...)) block did not supply a value for this layer.
        if (L.thickness_um > 0.0) {
            auto* t = new QLabel(QString::number(L.thickness_um, 'f', 1) + " um");
            t->setStyleSheet("color: #888;");
            h->addWidget(t);
        }
        h->addStretch();

        rows_layout_->addWidget(row);
    }
}
