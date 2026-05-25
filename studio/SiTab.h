// Studio SI tab -- full sikit feature set.
//
// QMainWindow internally so the toolbar + status bar + dock widgets
// behave like a real app within the QTabWidget cell. Central widget
// is sikit::PcbCanvas (gives impedance overlay + D2/D3 view modes).

#pragma once

#include <QMainWindow>
#include <optional>

#include "si/Ami.h"
#include "si/Ibis.h"

namespace sikit { class PcbCanvas; }
namespace sikit { class LayerPanel; }
class QAction;
class QComboBox;
class QLabel;

namespace circuitcore::studio {

class BoardModel;

class SiTab : public QMainWindow {
    Q_OBJECT
public:
    explicit SiTab(BoardModel* model, QWidget* parent = nullptr);
    ~SiTab() override;

private slots:
    void onBoardLoaded();

    void onPlotNetSParam();
    void onPlotDiffPairSParam();
    void onPlotViaSParam();

    void onSynthesizeEye();
    void onOpenIbis();
    void onOpenAmi();

    void onExportTouchstone();
    void onExportCsv();
    void onExportDiffPairS4p();

    void onImpedanceOverlay(double target_z0);
    void onImpedanceDiffOverlay(double target_z_diff);
    void onClearOverlay();

    void onView3DToggled(bool on);

    void onSaveHtmlReport();
    void onDeembedTouchstone();
    void onCompareOverlay();
    void onCheckSkew();
    void onCheckReturnPath();
    void onDeriveTopology();

private:
    void refreshNetList();
    int  currentNetId() const;
    bool useFdm() const;

    BoardModel* model_;
    sikit::PcbCanvas*  canvas_ = nullptr;
    sikit::LayerPanel* layer_panel_ = nullptr;
    QComboBox*  net_combo_ = nullptr;
    QAction*    fdm_action_ = nullptr;
    QAction*    view3d_action_ = nullptr;
    QLabel*     status_label_ = nullptr;

    // IBIS + AMI state for the eye pipeline.
    std::optional<sikit::ibis::IbisFile> ibis_file_;
    std::string active_ibis_model_;
    std::optional<sikit::ibis::ami::AmiFile> ami_file_;
    std::unique_ptr<sikit::ibis::ami::AmiModel> ami_model_;
};

}  // namespace circuitcore::studio
