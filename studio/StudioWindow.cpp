#include "StudioWindow.h"

#include <QApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPixmap>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTextBrowser>
#include <QVBoxLayout>
#include <QWidget>

#include "BoardModel.h"
#include "BoardTab.h"
#include "SiTab.h"
#include "PiTab.h"
#include "CalculatorsDialog.h"
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
    setAcceptDrops(true);

    auto* fileMenu = menuBar()->addMenu("&File");
    auto* openAct = fileMenu->addAction("&Open .kicad_pcb...");
    openAct->setShortcut(QKeySequence::Open);
    connect(openAct, &QAction::triggered, this, &StudioWindow::onOpenKicadPcb);
    recent_menu_ = fileMenu->addMenu("Open &recent");
    rebuildRecentMenu();
    fileMenu->addSeparator();
    auto* saveImgAct = fileMenu->addAction("Save current tab as &image...");
    saveImgAct->setShortcut(QKeySequence("Ctrl+Shift+S"));
    connect(saveImgAct, &QAction::triggered,
            this, &StudioWindow::onSaveCanvasAsImage);
    fileMenu->addSeparator();
    auto* quitAct = fileMenu->addAction("E&xit");
    quitAct->setShortcut(QKeySequence::Quit);
    connect(quitAct, &QAction::triggered, qApp, &QApplication::quit);

    auto* toolsMenu = menuBar()->addMenu("&Tools");
    auto* calcAct = toolsMenu->addAction("&Calculators...");
    connect(calcAct, &QAction::triggered,
            this, &StudioWindow::onShowCalculators);

    auto* helpMenu = menuBar()->addMenu("&Help");
    auto* shortcutsAct = helpMenu->addAction("&Keyboard shortcuts");
    connect(shortcutsAct, &QAction::triggered,
            this, &StudioWindow::onShowShortcuts);
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
    pushRecent(model_->currentPath());
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


void StudioWindow::onShowCalculators() {
    if (!calculators_) calculators_ = new CalculatorsDialog(this);
    calculators_->show();
    calculators_->raise();
    calculators_->activateWindow();
}


void StudioWindow::dragEnterEvent(QDragEnterEvent* event) {
    if (!event->mimeData()->hasUrls()) return;
    for (const QUrl& u : event->mimeData()->urls()) {
        if (u.toLocalFile().endsWith(".kicad_pcb", Qt::CaseInsensitive)) {
            event->acceptProposedAction();
            return;
        }
    }
}

void StudioWindow::dropEvent(QDropEvent* event) {
    for (const QUrl& u : event->mimeData()->urls()) {
        const QString p = u.toLocalFile();
        if (p.endsWith(".kicad_pcb", Qt::CaseInsensitive)) {
            model_->loadKicadPcb(p);
            event->acceptProposedAction();
            return;
        }
    }
}

void StudioWindow::onSaveCanvasAsImage() {
    QWidget* current = tabs_->currentWidget();
    if (!current) return;
    const QString suggested =
        QFileInfo(model_->currentPath()).completeBaseName() + "_"
        + tabs_->tabText(tabs_->currentIndex()).toLower() + ".png";
    const QString path = QFileDialog::getSaveFileName(
        this, tr("Save tab as image"), suggested,
        tr("PNG (*.png);;JPEG (*.jpg)"));
    if (path.isEmpty()) return;
    QPixmap pm = current->grab();
    if (!pm.save(path)) {
        QMessageBox::critical(this, tr("Save image"),
                               tr("Could not write %1").arg(path));
        return;
    }
    statusBar()->showMessage(tr("Wrote %1").arg(QFileInfo(path).fileName()), 6000);
}

void StudioWindow::onShowShortcuts() {
    auto* dlg = new QDialog(this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setWindowTitle(tr("Keyboard shortcuts"));
    dlg->resize(440, 360);
    auto* layout = new QVBoxLayout(dlg);
    auto* text = new QTextBrowser(dlg);
    text->setOpenExternalLinks(true);
    text->setHtml(tr(
        "<h3>Shell</h3>"
        "<table cellpadding=4>"
        "<tr><td><b>Ctrl+O</b></td><td>Open .kicad_pcb</td></tr>"
        "<tr><td><b>Ctrl+Shift+S</b></td><td>Save current tab as image</td></tr>"
        "<tr><td><b>Ctrl+Q</b></td><td>Quit</td></tr>"
        "<tr><td><b>Drag .kicad_pcb onto window</b></td>"
            "<td>Load the dropped board</td></tr>"
        "</table>"
        "<h3>Canvas (all tabs)</h3>"
        "<table cellpadding=4>"
        "<tr><td><b>Left-drag</b></td><td>Pan</td></tr>"
        "<tr><td><b>Wheel</b></td><td>Zoom toward cursor</td></tr>"
        "<tr><td><b>F</b></td><td>Fit board to view</td></tr>"
        "</table>"
        "<h3>SI tab (3D view)</h3>"
        "<table cellpadding=4>"
        "<tr><td><b>3D toggle button</b></td>"
            "<td>Swap between 2D layer view and 3D board view</td></tr>"
        "<tr><td><b>Left-drag (3D)</b></td><td>Orbit camera</td></tr>"
        "<tr><td><b>Right-drag (3D)</b></td><td>Pan camera</td></tr>"
        "<tr><td><b>Wheel (3D)</b></td><td>Dolly camera</td></tr>"
        "</table>"
        "<h3>PI tab</h3>"
        "<table cellpadding=4>"
        "<tr><td><b>Right-click pad</b></td>"
            "<td>Start two-pad probe-R measurement</td></tr>"
        "</table>"));
    layout->addWidget(text);
    auto* bb = new QDialogButtonBox(QDialogButtonBox::Close, dlg);
    QObject::connect(bb, &QDialogButtonBox::rejected, dlg, &QDialog::close);
    layout->addWidget(bb);
    dlg->show();
}

void StudioWindow::onOpenRecentFromAction() {
    auto* act = qobject_cast<QAction*>(sender());
    if (!act) return;
    const QString path = act->data().toString();
    if (path.isEmpty()) return;
    model_->loadKicadPcb(path);
}

QStringList StudioWindow::loadRecentList() const {
    QSettings s;
    return s.value("recent_files").toStringList();
}

void StudioWindow::saveRecentList(const QStringList& list) {
    QSettings s;
    s.setValue("recent_files", list);
}

void StudioWindow::pushRecent(const QString& path) {
    if (path.isEmpty()) return;
    QStringList list = loadRecentList();
    list.removeAll(path);
    list.prepend(path);
    while (list.size() > 8) list.removeLast();
    saveRecentList(list);
    rebuildRecentMenu();
}

void StudioWindow::rebuildRecentMenu() {
    if (!recent_menu_) return;
    recent_menu_->clear();
    QStringList list = loadRecentList();
    if (list.isEmpty()) {
        auto* a = recent_menu_->addAction(tr("(none yet)"));
        a->setEnabled(false);
        return;
    }
    for (const QString& p : list) {
        auto* a = recent_menu_->addAction(QFileInfo(p).fileName());
        a->setData(p);
        a->setToolTip(p);
        connect(a, &QAction::triggered,
                this, &StudioWindow::onOpenRecentFromAction);
    }
    recent_menu_->addSeparator();
    auto* clear = recent_menu_->addAction(tr("Clear list"));
    connect(clear, &QAction::triggered, this, [this]() {
        saveRecentList({});
        rebuildRecentMenu();
    });
}

}  // namespace circuitcore::studio
