#include "StudioWindow.h"

#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QWidget>

#include "BoardModel.h"
#include "BoardTab.h"
#include "SiTab.h"
#include "PiTab.h"
#include "EmiTab.h"

namespace circuitcore::studio {



StudioWindow::StudioWindow(QWidget* parent)
    : QMainWindow(parent),
      model_(std::make_unique<BoardModel>()) {
    setWindowTitle("circuitcore studio");
    resize(1280, 800);

    // --- Tabs ---
    tabs_ = new QTabWidget(this);
    board_tab_ = new BoardTab(model_.get(), tabs_);
    tabs_->addTab(board_tab_, "Board");
    tabs_->addTab(new SiTab(model_.get(), tabs_), "SI");
    tabs_->addTab(new PiTab(model_.get(), tabs_), "PI");
    tabs_->addTab(new EmiTab(model_.get(), tabs_), "EMI");
    setCentralWidget(tabs_);

    // --- Menu ---
    auto* fileMenu = menuBar()->addMenu("&File");
    auto* openAct = fileMenu->addAction("&Open .kicad_pcb...");
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &StudioWindow::onOpenKicadPcb);
    fileMenu->addSeparator();
    auto* quitAct = fileMenu->addAction("E&xit");
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

    auto* helpMenu = menuBar()->addMenu("&Help");
    auto* aboutAct = helpMenu->addAction("&About");
    connect(aboutAct, &QAction::triggered, this, &StudioWindow::onAbout);

    // --- Status bar ---
    status_path_  = new QLabel("(no board loaded)");
    status_hover_ = new QLabel;
    statusBar()->addWidget(status_path_, 1);
    statusBar()->addPermanentWidget(status_hover_);

    // --- Model wiring ---
    connect(model_.get(), &BoardModel::boardLoaded,
            this, &StudioWindow::onBoardLoaded);
    connect(model_.get(), &BoardModel::parseFailed,
            this, &StudioWindow::onParseFailed);
    connect(model_.get(), &BoardModel::hover,
            this, &StudioWindow::onHover);
}

StudioWindow::~StudioWindow() = default;

bool StudioWindow::loadKicadPcb(const QString& path) {
    return model_->loadKicadPcb(path);
}

void StudioWindow::onOpenKicadPcb() {
    const QString path = QFileDialog::getOpenFileName(
        this, "Open KiCad PCB", QString(),
        "KiCad PCB (*.kicad_pcb)");
    if (path.isEmpty()) return;
    model_->loadKicadPcb(path);
}

void StudioWindow::onParseFailed(const QString& message) {
    QMessageBox::critical(this, "Open failed",
                          QString("Could not load the board:\n\n%1").arg(message));
}

void StudioWindow::onBoardLoaded() {
    status_path_->setText(QFileInfo(model_->currentPath()).fileName());
}

void StudioWindow::onHover(double x_m, double y_m) {
    status_hover_->setText(
        QString("hover: %1, %2 mm")
            .arg(x_m * 1e3, 0, 'f', 3)
            .arg(y_m * 1e3, 0, 'f', 3));
}

void StudioWindow::onAbout() {
    QMessageBox::about(this, "About circuitcore studio",
        "<b>circuitcore studio</b><br>"
        "Unified shell for sikit (SI), pdnkit (PI), and emikit (EMI).<br><br>"
        "Browser-tab UI: one window, one tab per analysis. The Board tab "
        "is shared geometry; each analysis tab brings its own toolbar "
        "and panels.");
}

}  // namespace circuitcore::studio
