// Shared OpenGL PCB canvas.
//
// Base widget that draws the geometry common to every analysis view:
//   - background grid (10 mm spacing, faint)
//   - per-layer board fills (filled zones + segments + via pads, from
//     SegmentMesher / ViaMesher / ZoneMesher in this same library)
//   - board outline (Edge.Cuts, yellow)
// Plus the camera, pan/zoom mouse handling, fit-to-board, layer
// visibility toggling, settings save/restore, and a hover signal.
//
// Subclasses extend the canvas with analysis-specific overlays via
// the virtual hooks documented in the protected section. The base
// has zero knowledge of impedance plots, IR-drop heat-maps, eye
// diagrams, etc. -- those live in subclasses (sikit::PcbCanvas,
// pdnkit::PcbCanvas) or in tab-specific direct uses (the studio
// Board tab uses this class bare).
//
// Why a base class and not composition
//
//   Every subclass needs to interleave its own draws with the base's
//   draws (overlay shaders bound after grid+layers, before/after hover
//   feedback, etc.) AND share the OpenGL context, the camera, and the
//   board pointer. Composition would force the overlay code to grab
//   all of those through forwarding accessors. Inheritance + virtual
//   paintOverlays hooks is the standard Qt widget pattern and keeps
//   subclasses short.

#pragma once

#include <unordered_map>
#include <string>
#include <vector>

#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>

#include "circuitcore/board/Board.h"
#include "circuitcore/ui/Camera2D.h"
#include "circuitcore/ui/SegmentMesher.h"  // LayerMesh

class QSettings;

namespace circuitcore::ui {

class PcbCanvas : public QOpenGLWidget,
                  protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit PcbCanvas(QWidget* parent = nullptr);
    ~PcbCanvas() override;

    void setBoard(const board::Board* board);
    void setLayerVisibility(int ordinal, bool visible);
    void fitToBoard();
    // Defer a fitToBoard() call to the first paint after the
    // widget has a non-trivial viewport size. Used when setBoard
    // is invoked while the tab is still hidden.
    void requestFitOnFirstPaint() { fit_pending_ = true; }

    // Persist / restore the camera center + zoom across launches.
    // The QSettings instance is passed in so the caller can group it
    // alongside its own window-geometry state under a shared org/app
    // namespace.
    void saveSettings(QSettings& settings) const;
    void restoreSettings(QSettings& settings);

signals:
    // Emitted as the cursor moves over the board. Subclass overlays
    // can compose a richer line via the same signal (the connect on
    // the receiving side is unchanged).
    void hoverInfo(const QString& info);

    // Emitted every mouse-move with the world-space cursor position
    // in board metres. Useful for status-bar coordinate readouts.
    void hoverPos(double x_m, double y_m);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

    // --- Virtual extension points for subclasses ---
    //
    // initializeGLOverlays: called from initializeGL after the base
    //   has compiled its flat shader and set up the grid / board /
    //   outline buffers. Subclass-side shader programs and VBOs go
    //   here.
    //
    // paintOverlays2D: called from paintGL after the grid + layers +
    //   outline have been drawn. The base's flat_prog_ is unbound at
    //   this point. The current camera ortho matrix is accessible via
    //   ortho_matrix(); subclasses bind their own shaders.
    //
    // onBoardChanged: called from setBoard whenever board_ has been
    //   swapped. Subclasses should invalidate any per-board caches
    //   they keep (e.g. an IR-drop overlay tied to the previous board).
    //
    // onMousePressOverlay: called from mousePressEvent BEFORE the
    //   base's pan handling. Subclasses use this for right-click
    //   probe-pick or selection workflows. Returning early from the
    //   handler is not supported -- the base always considers the
    //   event for panning afterward. Subclasses that want to
    //   suppress panning should accept the event themselves.
    virtual void initializeGLOverlays() {}
    virtual void paintOverlays2D() {}
    virtual void onBoardChanged() {}
    virtual void onMousePressOverlay(QMouseEvent* /*e*/) {}

    // --- Accessors for subclasses ---
    const board::Board* board() const { return board_; }
    const Camera2D& camera() const { return camera_; }
    Camera2D& camera_mut() { return camera_; }
    QOpenGLShaderProgram& flat_program() { return flat_prog_; }
    QMatrix4x4 ortho_matrix() const;

private:
    void buildGrid();
    void buildOutline();
    void uploadBoardMeshes();
    void uploadGraphics();

    struct LayerRange {
        int ordinal = 0;
        int index_start = 0;
        int index_count = 0;
    };

    Camera2D camera_;
    const board::Board* board_ = nullptr;

    QOpenGLShaderProgram flat_prog_;  // grid, layer fills, outline

    QOpenGLBuffer grid_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject grid_vao_;
    int grid_vertex_count_ = 0;

    QOpenGLBuffer outline_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject outline_vao_;
    int outline_vertex_count_ = 0;

    QOpenGLBuffer board_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer board_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject board_vao_;
    std::vector<LayerRange> layer_ranges_;
    std::vector<LayerMesh> pending_meshes_;
    bool meshes_dirty_ = false;
    bool fit_pending_  = false;

    // Silkscreen / mask / courtyard meshes (one VBO + IBO per category).
    // Built from Board::graphics by GraphicsMesher; uploaded lazily in
    // paintGL when the board changes. Drawn with the flat shader at
    // category-specific colors (mask translucent green, silk opaque
    // white, courtyard dim magenta).
    QOpenGLBuffer silk_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer silk_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject silk_vao_;
    int silk_index_count_ = 0;
    QOpenGLBuffer mask_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer mask_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject mask_vao_;
    int mask_index_count_ = 0;
    QOpenGLBuffer cyd_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer cyd_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject cyd_vao_;
    int cyd_index_count_ = 0;
    bool graphics_dirty_ = false;
    // Pending text items rendered via QPainter on top of the GL frame
    // (silkscreen reference designators + labels).
    struct SilkText {
        double x = 0, y = 0, size_m = 0, angle = 0;
        std::string text;
    };
    std::vector<struct SilkText> pending_text_;

    std::unordered_map<int, bool> layer_visible_;

    bool panning_ = false;
    QPoint last_mouse_;
};

}  // namespace circuitcore::ui
