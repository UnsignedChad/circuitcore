#pragma once

#include <vector>

#include <QString>
#include <QWidget>

class QVBoxLayout;

// Dockable panel listing copper layers in the loaded board with a
// per-layer visibility checkbox and color swatch. Emits visibility
// changes; the canvas listens to redraw with the new set of layers.
class LayerPanel : public QWidget {
    Q_OBJECT
public:
    struct Entry {
        int ordinal = 0;
        QString name;
        double thickness_um = 0.0;  // 0 = unknown
    };

    explicit LayerPanel(QWidget* parent = nullptr);

    // Rebuild the rows to match `layers`. All layers default to visible.
    void setLayers(const std::vector<Entry>& layers);

signals:
    void visibility_changed(int ordinal, bool visible);

    // Emitted when the user edits a layer's thickness spinbox. The
    // value is in meters (the panel does the um -> m conversion).
    // MainWindow listens and writes board_->stackup.layers[i].thickness
    // so the next analysis uses the override.
    void thickness_changed(int ordinal, double thickness_m);

private:
    void clearRows();

    QVBoxLayout* rows_layout_ = nullptr;
};
