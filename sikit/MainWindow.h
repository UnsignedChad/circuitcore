#pragma once

#include <QMainWindow>
#include <QString>
#include <memory>
#include <optional>
#include <vector>

#include "si/Ibis.h"
#include "si/Ami.h"
#include "circuitcore/board/Board.h"
#include "si/SiStackup.h"
#include "si/Touchstone.h"
#include "si/Project.h"

namespace sikit { class PcbCanvas; }
namespace sikit { class LayerPanel; }
class QAction;
class QLabel;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

    bool loadKicadPcb(const QString& path);

private slots:
    void onOpenKicadPcb();
    void onOpenTouchstoneEye();
    void onSynthesizeEye();
    void onExportNetTouchstone();
    void onExportNetCsv();
    void onExportDiffPairS4p();
    void onOpenIbis();
    void onOpenSParamPlot();
    void onPlotNetSParam();
    void onPlotDiffPairSParam();
    void onPlotViaSParam();
    void onOpenAmi();
    void onOpenProject();
    void onSaveProject();

private:
    void populateLayerPanel();
    void showImpedanceOverlay(double target_z0);
    void showDiffPairOverlay(double target_z_diff);
    void showEyeDiagramDemo(bool severe_isi);

    // Loaded IBIS file + currently-active model name (if any). Used by
    // the eye-diagram pipeline to derive a realistic TX ramp time.
    std::optional<sikit::ibis::IbisFile> ibis_file_;
    std::string active_ibis_model_;

    // AMI runtime: parsed .ami parameter file + dynamic library loader.
    std::optional<sikit::ibis::ami::AmiFile> ami_file_;
    std::unique_ptr<sikit::ibis::ami::AmiModel> ami_model_;
    QString ibis_source_path_;
    QString ami_params_path_;
    QString ami_library_path_;

    // Run a synthesised channel waveform through the loaded AMI model
    // (if any) as an RX equaliser. Edits  in place. No-op when no
    // .so is loaded or the model lacks AMI_Init.
    void applyAmiIfLoaded(
        const sikit::touchstone::TouchstoneFile& channel,
        double sample_rate_hz, double bit_time_s,
        std::vector<double>& wave);

    sikit::PcbCanvas* canvas_;
    sikit::LayerPanel* layer_panel_;
    QLabel* hover_label_;
    QAction* use_fdm_action_;
    std::unique_ptr<circuitcore::board::Board> board_;
    sikit::si::SiStackup si_stackup_;   // dielectric items[] for SI math
    QString current_pcb_path_;
    QString current_project_path_;
};
