// circuitcore::studio shared board model.
//
// Owns the currently-loaded Board + SiStackup and broadcasts changes
// to every tab via Qt signals. Tabs never read the file system
// directly -- they react to boardLoaded() and pull from the model.
//
// Why a separate model and not a member of StudioWindow
//
//   Studio has four tabs (Board, SI, PI, EMI), each potentially with
//   its own canvas and analysis panels. Holding the state in
//   StudioWindow would mean every tab needs a back-pointer and every
//   "active net changed" event would have to be hand-wired in N
//   places. A QObject with signals is the standard Qt way to glue
//   loosely-coupled widgets; tabs can come and go without touching
//   the model.
//
// What lives in the model vs. in a tab
//
//   In  -- parsed Board, parsed SiStackup, current PCB path, the
//          "active net" / "active layer" selection, hover position
//          in board-space metres.
//   Out -- anything tab-specific (PI solver state, SI eye grid,
//          EMI verdicts). Each tab owns its own analysis state.

#pragma once

#include <QObject>
#include <QString>
#include <memory>

#include "circuitcore/board/Board.h"
#include "si/SiStackup.h"

namespace circuitcore::studio {

class BoardModel : public QObject {
    Q_OBJECT
public:
    explicit BoardModel(QObject* parent = nullptr);

    // Read-only accessors. board() returns nullptr until a file has
    // been loaded successfully.
    const board::Board* board() const { return board_.get(); }
    const sikit::si::SiStackup& siStackup() const { return si_stackup_; }
    QString currentPath() const { return current_path_; }

    int activeNetId()         const { return active_net_id_; }
    int activeLayerOrdinal()  const { return active_layer_; }

public slots:
    // Load a .kicad_pcb from disk. Parses + caches the SI stackup.
    // Emits boardLoaded() on success, parseFailed() on error. Returns
    // true iff loaded.
    bool loadKicadPcb(const QString& path);

    // Selection setters. Emit activeNetChanged / activeLayerChanged
    // when the value actually changes.
    void setActiveNetId(int id);
    void setActiveLayerOrdinal(int ord);

    // Hover-position update, broadcast to whichever tab cares (status
    // bar, canvas crosshair, etc.). Coords are board-space metres.
    void setHover(double x_m, double y_m);

signals:
    // A new board has been parsed and is in board(). Tabs should
    // re-fit their canvases and rebuild any cached state.
    void boardLoaded();

    // Disk parse failed. message is a human-readable error.
    void parseFailed(const QString& message);

    void activeNetChanged(int new_id);
    void activeLayerChanged(int new_ord);
    void hover(double x_m, double y_m);

private:
    std::unique_ptr<board::Board> board_;
    sikit::si::SiStackup si_stackup_;
    QString current_path_;
    int active_net_id_ = -1;
    int active_layer_  = -1;
};

}  // namespace circuitcore::studio
