#pragma once

#include <vector>

#include <QString>
#include <QWidget>

class QVBoxLayout;

namespace sikit {

// Dockable panel listing copper layers in the loaded board with a
// per-layer visibility checkbox and color swatch. Emits visibility
// changes; the canvas listens to redraw with the new set of layers.
class LayerPanel : public QWidget {
    Q_OBJECT
public:
    struct Entry {
        int ordinal = 0;
        QString name;
    };

    explicit LayerPanel(QWidget* parent = nullptr);

    // Rebuild the rows to match `layers`. All layers default to visible.
    void setLayers(const std::vector<Entry>& layers);

signals:
    void visibility_changed(int ordinal, bool visible);

private:
    void clearRows();

    QVBoxLayout* rows_layout_ = nullptr;
};


}  // namespace sikit
