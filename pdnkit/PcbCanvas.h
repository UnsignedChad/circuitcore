#pragma once

#include <unordered_map>
#include <vector>

#include <QOpenGLBuffer>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLWidget>
#include <QPoint>

#include "circuitcore/board/Board.h"
#include "circuitcore/ui/Camera2D.h"
#include "render/IrResultMesh.h"
#include "pi/IrSolver.h"
#include "circuitcore/ui/SegmentMesher.h"

class PcbCanvas : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    explicit PcbCanvas(QWidget* parent = nullptr);

    void setBoard(const circuitcore::board::Board* board);
    void setLayerVisibility(int ordinal, bool visible);
    void fitToBoard();

    // Persist / restore the cameras center and zoom across launches.
    // QSettings is passed by reference so MainWindow can group with its own
    // geometry/state save under a common organisation/app namespace.
    void saveSettings(class QSettings& settings) const;
    void restoreSettings(class QSettings& settings);

    // Attach (or clear, with an empty mesh) an IR-drop heat-map overlay.
    // Uploaded lazily on the next paintGL.
    void setIrResult(pdnkit::render::IrResultMesh result);

    // Optionally pass the underlying IR-drop mesh + solution so the hover
    // probe can sample voltage at the cursor location. Pass an empty
    // mesh/solution (mesh.nodes.empty()) to clear.
    void setProbeSource(pdnkit::pi::IrMesh mesh,
                        pdnkit::pi::Solution solution);

    // Decoupling-cap position markers (world coords in meters), rendered
    // as blue dots over everything. CavityPanel pushes this list whenever
    // the user adds/removes/edits a decap.
    void setDecapMarkers(const std::vector<circuitcore::board::Point2>& positions);

    // Cavity highlight: dashed bbox around the meshed plane, plus port
    // markers (cyan dots). Pass an empty Point2 list to clear ports;
    // pass hi <= lo on bbox to clear the rectangle.
    void setCavityHighlight(double lo_x, double lo_y, double hi_x, double hi_y,
                            const std::vector<circuitcore::board::Point2>& ports);

signals:
    void hoverInfo(const QString& info);

    // Right-click probe-R workflow. Emitted on the second right-click,
    // after the user has picked two pads on the same net. MainWindow
    // runs the solver and shows the result.
    void probeRequested(int pad_a_index, int pad_b_index,
                        int net_id, int layer_ordinal);

    // Status hint emitted when the first probe pad is selected (or the
    // selection is cancelled). MainWindow forwards to the status bar.
    void probeHint(const QString& msg);

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
    void buildOutline();
    void uploadBoardMeshes();
    void uploadIrResult();

    struct LayerRange {
        int ordinal = 0;
        int index_start = 0;
        int index_count = 0;
    };

    circuitcore::ui::Camera2D camera_;
    const circuitcore::board::Board* board_ = nullptr;

    QOpenGLShaderProgram flat_prog_;  // grid + board layer fills
    QOpenGLShaderProgram heat_prog_;  // IR-drop overlay (viridis)

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
    std::vector<circuitcore::ui::LayerMesh> pending_meshes_;
    bool meshes_dirty_ = false;

    QOpenGLBuffer heat_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer heat_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject heat_vao_;
    pdnkit::render::IrResultMesh pending_heat_;
    std::vector<pdnkit::render::IrResultMesh::LayerRange> heat_layer_ranges_;
    int heat_index_count_ = 0;
    bool heat_dirty_ = false;

    // Mesh + solution backing the active heat overlay, used for cursor-probe
    // voltage sampling. Empty when no IR-drop result is loaded.
    pdnkit::pi::IrMesh probe_mesh_;
    pdnkit::pi::Solution probe_solution_;

    // Marker overlay for sources/sinks (rendered with flat shader after heat).
    QOpenGLBuffer marker_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer marker_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject marker_vao_;
    int marker_source_index_start_ = 0;
    int marker_source_index_count_ = 0;
    int marker_sink_index_start_ = 0;
    int marker_sink_index_count_ = 0;

    // Decap position markers (separate buffer so cap edits don't disturb
    // the IR-drop pipeline).
    QOpenGLBuffer decap_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer decap_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject decap_vao_;
    int decap_index_count_ = 0;
    std::vector<circuitcore::board::Point2> pending_decaps_;
    bool decaps_dirty_ = false;

    // Cavity overlay: bbox outline (dashed) + port markers (cyan).
    double cavity_lo_x_ = 0.0, cavity_lo_y_ = 0.0;
    double cavity_hi_x_ = 0.0, cavity_hi_y_ = 0.0;
    std::vector<circuitcore::board::Point2> cavity_ports_;
    QOpenGLBuffer cavity_rect_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLVertexArrayObject cavity_rect_vao_;
    QOpenGLBuffer cavity_port_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer cavity_port_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject cavity_port_vao_;
    int cavity_rect_vertex_count_ = 0;
    int cavity_port_index_count_ = 0;
    bool cavity_dirty_ = false;

    // Hotspot ring (yellow, drawn on top of heat-map markers).
    QOpenGLBuffer hotspot_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer hotspot_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject hotspot_vao_;
    int hotspot_index_count_ = 0;
    bool hotspot_active_ = false;
    double hotspot_x_ = 0.0;
    double hotspot_y_ = 0.0;

    std::unordered_map<int, bool> layer_visible_;

    bool panning_ = false;
    QPoint last_mouse_;

    // Right-click probe-R: index into board_->pads after the first
    // right-click; -1 means "no pad selected yet."
    int probe_pad_a_ = -1;
};
