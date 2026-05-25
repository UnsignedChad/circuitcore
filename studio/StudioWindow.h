// Top-level studio window.
//
// One QMainWindow, one QTabWidget with four tabs (Board, SI, PI, EMI),
// one shared BoardModel that every tab watches. File menu drives PCB
// loading -- tabs never touch the file system directly.
//
// The shell is intentionally browser-tab thin: no docking, no MDI,
// no floating panels. Each tab brings its own toolbar and panels.
// This file should stay small as analyses are wired in; growth goes
// into the per-tab widgets (SiTab, PiTab, EmiTab).

#pragma once

#include <QMainWindow>
#include <QString>
#include <memory>

class QLabel;
class QTabWidget;

namespace circuitcore::studio {

class BoardModel;
class BoardTab;

class StudioWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit StudioWindow(QWidget* parent = nullptr);
    // Out-of-line so unique_ptr<BoardModel> sees the full type in the .cpp.
    ~StudioWindow() override;

    // Convenience: load a board from CLI argv.
    bool loadKicadPcb(const QString& path);

private slots:
    void onOpenKicadPcb();
    void onParseFailed(const QString& message);
    void onBoardLoaded();
    void onHover(double x_m, double y_m);
    void onAbout();
    void onShowCalculators();

private:
    std::unique_ptr<BoardModel> model_;
    QTabWidget* tabs_ = nullptr;
    BoardTab*   board_tab_ = nullptr;
    QLabel* status_path_ = nullptr;
    QLabel* status_hover_ = nullptr;
    class CalculatorsDialog* calculators_ = nullptr;
};

}  // namespace circuitcore::studio
