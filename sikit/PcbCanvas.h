// sikit's PCB canvas.
//
// Subclasses circuitcore::ui::PcbCanvas to inherit the shared 2D
// pipeline (grid, layer fills, outline, camera, pan/zoom, hover hit-
// test, settings save/restore). Adds the sikit-specific features:
//   - 3D stackup view (Camera3D + Mesher3D). Ctrl+D toggle.
//   - Impedance error overlay (per-segment colored rectangles drawn
//     over the trace fills via a per-vertex-color shader).
//   - SiStackup pointer used by the 3D mesh builder for accurate
//     per-layer thicknesses + epsilon_r.
//
// All the analysis-agnostic plumbing lives in the base class; this
// file is just the sikit overlays plus the 3D paint path that bypasses
// the base's 2D paintGL.

#pragma once

#include <vector>

#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>

#include "circuitcore/ui/PcbCanvas.h"
#include "circuitcore/board/Board.h"
#include "si/SiStackup.h"
#include "si/TraceImpedance.h"
#include "render/Camera3D.h"
#include "render/Mesher3D.h"

// sikit's flavoured canvas. Lives in the global namespace so existing
// MainWindow code that just says "PcbCanvas* canvas_;" keeps working
// without a namespace touch.
namespace sikit {

class PcbCanvas : public circuitcore::ui::PcbCanvas {
    Q_OBJECT
public:
    enum class ViewMode { D2, D3 };

    explicit PcbCanvas(QWidget* parent = nullptr);

    void setSiStackup(const sikit::si::SiStackup* s);

    void setViewMode(ViewMode mode);
    ViewMode viewMode() const { return view_mode_; }

    // Build and upload a colored impedance-error overlay over the
    // board's segments. Pass an empty results vector to clear.
    void setImpedanceOverlay(
        const std::vector<sikit::analysis::SegmentImpedance>& results,
        double target_z0);
    void clearImpedanceOverlay();

protected:
    // We override paintGL outright -- the base does 2D, we hand-roll
    // the 3D path. paintOverlays2D handles the impedance overlay when
    // we ARE in 2D mode (called back into by base::paintGL).
    void paintGL() override;

    void initializeGLOverlays() override;
    void paintOverlays2D() override;
    void onBoardChanged() override;

    // 3D mode needs custom orbit / pan / zoom -- in 2D mode we
    // delegate back to the base.
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    void uploadOverlay();
    void rebuildMesh3D();
    void drawGizmo();


    ViewMode view_mode_ = ViewMode::D2;
    const sikit::si::SiStackup* si_stackup_ = nullptr;

    // 2D impedance overlay.
    QOpenGLShaderProgram vcol_prog_;
    QOpenGLBuffer overlay_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer overlay_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject overlay_vao_;
    std::vector<float> pending_overlay_verts_;
    std::vector<std::uint32_t> pending_overlay_indices_;
    int overlay_index_count_ = 0;
    bool overlay_dirty_ = false;

    // 3D pipeline.
    sikit::render::Camera3D camera3d_;
    QOpenGLShaderProgram lit_prog_;
    struct GpuMesh3D {
        QOpenGLBuffer vbo{QOpenGLBuffer::VertexBuffer};
        QOpenGLBuffer ibo{QOpenGLBuffer::IndexBuffer};
        QOpenGLVertexArrayObject vao;
        int index_count = 0;
    };
    GpuMesh3D mesh3d_dielectric_;
    GpuMesh3D mesh3d_copper_;
    GpuMesh3D mesh3d_vias_;
    sikit::render::BoardMesh3D pending_mesh3d_;
    bool mesh3d_dirty_ = false;

    // 3D-mode pan/orbit state. The base owns the 2D pan state; we
    // mirror it for 3D so the two modes don't share a stale flag.
    bool panning_3d_ = false;
    QPoint last_mouse_3d_;
};


}  // namespace sikit
