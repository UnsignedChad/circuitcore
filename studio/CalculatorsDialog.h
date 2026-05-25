// Board-independent calculator collection.
//
// pdnkit and emikit ship a handful of physics-formula helpers that
// have no .kicad_pcb dependency: PDN target impedance, via barrel
// inductance, VRM output impedance, Djordjevic-Sarkar dielectric
// dispersion, Hammerstad-Jensen surface-roughness multiplier, and the
// cable common-mode E-field formula. CLI exposes each via a flag; the
// studio surfaces them as a tabbed dialog opened from the Tools menu.
//
// All math is delegated to the kit headers -- this file is glue and
// unit conversion. The dialog itself is non-modal so the user can
// keep it open next to a tab and iterate.

#pragma once

#include <QDialog>

class QTabWidget;

namespace circuitcore::studio {

class CalculatorsDialog : public QDialog {
    Q_OBJECT
public:
    explicit CalculatorsDialog(QWidget* parent = nullptr);

private:
    QTabWidget* tabs_ = nullptr;
};

}  // namespace circuitcore::studio
