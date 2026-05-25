// Studio PI tab -- full pdnkit feature parity.
//
// The tab is a QMainWindow internally so it can host QDockWidgets for
// the pdnkit panels (AnalysisPanel, NetStatsPanel, CavityPanel,
// TransientPanel, DrcPanel, LayerPanel) the same way pdnkit's own
// MainWindow does. The central widget is pdnkit::PcbCanvas (heat-map,
// markers, decap dots, cavity highlight, hotspot ring, probe-R).
//
// Wiring mirrors pdnkit/MainWindow.cpp -- studio is glue, not new
// analysis code. The duplication with the standalone pdnkit GUI is
// acceptable for v1; future cleanup can extract a shared PdnController.

#pragma once

#include <QMainWindow>
#include <memory>

#include "pi/IrMesher.h"
#include "pi/IrSolver.h"

// Forward declarations -- everything below comes from pdnkit_widgets.
class PcbCanvas;          // pdnkit's subclass of circuitcore::ui::PcbCanvas
class AnalysisPanel;
class NetStatsPanel;
class CavityPanel;
class TransientPanel;
class DrcPanel;
class LayerPanel;
class ColorLegend;
class QLabel;

namespace circuitcore::studio {

class BoardModel;

class PiTab : public QMainWindow {
    Q_OBJECT
public:
    explicit PiTab(BoardModel* model, QWidget* parent = nullptr);
    ~PiTab() override;

private slots:
    void onBoardLoaded();
    void onAnalyzeStaticIrDrop();
    void onProbeRequested(int pad_a, int pad_b, int net_id, int layer_ord);

private:
    void populateLayerPanel();

    BoardModel* model_;

    PcbCanvas*       canvas_ = nullptr;
    ColorLegend*     legend_ = nullptr;
    AnalysisPanel*   analysis_panel_ = nullptr;
    NetStatsPanel*   netstats_panel_ = nullptr;
    CavityPanel*     cavity_panel_   = nullptr;
    TransientPanel*  transient_panel_ = nullptr;
    DrcPanel*        drc_panel_      = nullptr;
    LayerPanel*      layer_panel_    = nullptr;
    QLabel*          hover_label_    = nullptr;

    // Cache the last analysis result so the probe-R workflow can reuse
    // the mesh + solution without re-running the solver.
    pdnkit::pi::IrMesh   last_mesh_;
    pdnkit::pi::Solution last_solution_;
    double last_cell_size_       = 5.0e-4;
    double last_copper_thickness_ = 35.0e-6;
    double last_copper_rho_      = 1.68e-8;
};

}  // namespace circuitcore::studio
