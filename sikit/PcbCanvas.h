#pragma once

#include <unordered_map>
#include <vector>

#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>

#include "si/TraceImpedance.h"
#include "circuitcore/board/Board.h"
#include "si/SiStackup.h"
#include "circuitcore/ui/Camera2D.h"
#include "render/Camera3D.h"
#include "render/Mesher3D.h"
#include "circuitcore/ui/SegmentMesher.h"

class PcbCanvas : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    enum class ViewMode { D2, D3 };

    explicit PcbCanvas(QWidget* parent = nullptr);

    void setBoard(const circuitcore::board::Board* board);
    void setSiStackup(const sikit::si::SiStackup* s);
    void setLayerVisibility(int ordinal, bool visible);
    void fitToBoard();

    void setViewMode(ViewMode mode);
    ViewMode viewMode() const { return view_mode_; }

    // Build and upload a colored impedance-error overlay over the board's
    // segments. Pass an empty results vector to clear.
    void setImpedanceOverlay(const std::vector<sikit::analysis::SegmentImpedance>& results,
                              double target_z0);
    void clearImpedanceOverlay();

signals:
    void hoverInfo(const QString& info);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;

    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void wheelEvent(QWheelEvent* e) override;

private:
    void buildGrid();
    void uploadBoardMeshes();
    void uploadOverlay();

    struct LayerRange {
        int ordinal = 0;
        int index_start = 0;
        int index_count = 0;
    };

    circuitcore::ui::Camera2D camera_;
    sikit::render::Camera3D camera3d_;
    ViewMode view_mode_ = ViewMode::D2;
    const circuitcore::board::Board* board_ = nullptr;
    const sikit::si::SiStackup* si_stackup_ = nullptr;

    // Flat-color shader: grid + per-layer board fills.
    QOpenGLShaderProgram flat_prog_;
    // Per-vertex color shader: impedance overlay (and any future scalar overlay).
    QOpenGLShaderProgram vcol_prog_;

    QOpenGLBuffer grid_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject grid_vao_;
    int grid_vertex_count_ = 0;

    QOpenGLBuffer board_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer board_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject board_vao_;
    std::vector<LayerRange> layer_ranges_;
    std::vector<circuitcore::ui::LayerMesh> pending_meshes_;
    bool meshes_dirty_ = false;

    // Impedance overlay: 6 floats per vertex (x, y, r, g, b, a), 2 triangles
    // per segment. Stored at vertex level so a single draw call colors every
    // segment by its own deviation from target Z0.
    QOpenGLBuffer overlay_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer overlay_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject overlay_vao_;
    std::vector<float> pending_overlay_verts_;
    std::vector<std::uint32_t> pending_overlay_indices_;
    int overlay_index_count_ = 0;
    bool overlay_dirty_ = false;

    std::unordered_map<int, bool> layer_visible_;

    bool panning_ = false;
    QPoint last_mouse_;

    // 3D pipeline. One lit shader, one VAO per mesh category so the paint
    // path can swap depth/blend state between opaque (copper, vias) and
    // translucent (dielectric) passes.
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
};
