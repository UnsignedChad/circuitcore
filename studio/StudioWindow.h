// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
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
#include <QStringList>
#include <memory>

class QLabel;
class QMenu;
class QTabWidget;
class QDragEnterEvent;
class QDropEvent;

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

protected:
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dropEvent(QDropEvent* event) override;

private slots:
    void onOpenKicadPcb();
    void onParseFailed(const QString& message);
    void onBoardLoaded();
    void onHover(double x_m, double y_m);
    void onAbout();
    void onShowCalculators();
    void onSaveCanvasAsImage();
    void onShowShortcuts();
    void onShowDonate();
    void onOpenRecentFromAction();

private:
    void rebuildRecentMenu();
    void pushRecent(const QString& path);
    QStringList loadRecentList() const;
    void saveRecentList(const QStringList& list);

    std::unique_ptr<BoardModel> model_;
    QTabWidget* tabs_ = nullptr;
    BoardTab*   board_tab_ = nullptr;
    QLabel* status_path_ = nullptr;
    QLabel* status_hover_ = nullptr;
    class CalculatorsDialog* calculators_ = nullptr;
    QMenu*  recent_menu_  = nullptr;
};

}  // namespace circuitcore::studio
