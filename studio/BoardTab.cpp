// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "BoardTab.h"

#include <QVBoxLayout>

#include "circuitcore/ui/PcbCanvas.h"
#include "BoardModel.h"

namespace circuitcore::studio {

BoardTab::BoardTab(BoardModel* model, QWidget* parent)
    : QWidget(parent), model_(model), canvas_(new ui::PcbCanvas(this)) {
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(canvas_);

    connect(model_, &BoardModel::boardLoaded, this, &BoardTab::onBoardLoaded);
    connect(canvas_, &ui::PcbCanvas::hoverInfo,
            this, &BoardTab::onCanvasHover);
    // World-coord hover -> BoardModel so the status bar updates.
    connect(canvas_, &ui::PcbCanvas::hoverPos,
            model_, &BoardModel::setHover);
}

void BoardTab::onBoardLoaded() {
    canvas_->setBoard(model_->board());
}

void BoardTab::onCanvasHover(const QString& /*info*/) {
    // The base canvas already reports its own hover info -- forward
    // to the BoardModel so the status bar updates. Hit-test net+layer
    // strings flow through hoverInfo signal; world-coord hover (which
    // the BoardModel signal expects) comes from extracting from the
    // canvas in a future refinement. For now we don't double-update
    // the status bar; the canvas's hoverInfo will surface as a richer
    // line via a future StudioWindow connection.
}

}  // namespace circuitcore::studio
