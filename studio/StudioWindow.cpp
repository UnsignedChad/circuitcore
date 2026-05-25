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

namespace circuitcore::studio {

namespace {

// Plain placeholder tab. Three lines of intent text centred on a dark
// background. Replaced by real widgets in Tasks #3 / #4 / #5.
QWidget* makePlaceholderTab(const QString& heading,
                              const QString& details) {
    auto* page = new QWidget;
    page->setAutoFillBackground(true);
    QPalette pal = page->palette();
    pal.setColor(QPalette::Window, QColor(28, 30, 34));
    page->setPalette(pal);
    auto* layout = new QVBoxLayout(page);
    layout->setAlignment(Qt::AlignCenter);

    auto* h = new QLabel(heading);
    h->setAlignment(Qt::AlignCenter);
    QFont f = h->font();
    f.setPointSize(f.pointSize() + 6);
    f.setBold(true);
    h->setFont(f);
    h->setStyleSheet("color: #d0d0d0;");

    auto* d = new QLabel(details);
    d->setAlignment(Qt::AlignCenter);
    d->setWordWrap(true);
    d->setStyleSheet("color: #909090;");

    layout->addWidget(h);
    layout->addWidget(d);
    return page;
}

}  // namespace

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
    tabs_->addTab(
        makePlaceholderTab(
            "EMI / EMC",
            "Radiated-emissions verdict, spectrum vs. CISPR mask.\n"
            "Wired in by Task #5."),
        "EMI");
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
