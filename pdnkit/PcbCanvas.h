// pdnkit's PCB canvas.
//
// Subclasses circuitcore::ui::PcbCanvas to inherit the shared 2D
// pipeline (grid, layer fills, outline, camera, pan/zoom, settings
// save/restore). Adds the pdnkit-specific overlays:
//   - IR-drop heat-map (viridis colormap on per-vertex scalar)
//   - Source / sink markers (green / red disks)
//   - Decap position markers (blue disks)
//   - Cavity highlight (dashed bbox + cyan port markers)
//   - Hotspot ring (yellow annulus around the worst node)
//   - Right-click probe-R workflow (pick two pads on a net, emit
//     probeRequested -- MainWindow runs the solver and shows the result)
//   - Hover voltage probe (sample IrSolver::Solution at cursor)
//
// All the analysis-agnostic plumbing lives in the base class; this
// file is just the PI overlays plus the probe interaction.

#pragma once

#include <vector>

#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>

#include "circuitcore/board/Board.h"
#include "circuitcore/ui/PcbCanvas.h"
#include "render/IrResultMesh.h"
#include "pi/IrSolver.h"

namespace pdnkit {

class PcbCanvas : public circuitcore::ui::PcbCanvas {
    Q_OBJECT
public:
    explicit PcbCanvas(QWidget* parent = nullptr);

    // Attach (or clear, with an empty mesh) an IR-drop heat-map overlay.
    void setIrResult(pdnkit::render::IrResultMesh result);

    // Optional probe source: hover voltage sampling against this mesh +
    // solution. Pass an empty mesh to clear.
    void setProbeSource(pdnkit::pi::IrMesh mesh,
                         pdnkit::pi::Solution solution);

    // Decoupling-cap position markers (world meters), rendered as blue
    // dots on top of everything.
    void setDecapMarkers(const std::vector<circuitcore::board::Point2>& positions);

    // Cavity highlight: dashed bbox + port markers.
    void setCavityHighlight(double lo_x, double lo_y,
                             double hi_x, double hi_y,
                             const std::vector<circuitcore::board::Point2>& ports);

signals:
    // Right-click probe-R workflow. Emitted on the second right-click
    // after the user has picked two pads on the same net.
    void probeRequested(int pad_a_index, int pad_b_index,
                         int net_id, int layer_ordinal);

    // Status hint as the user makes their pick (or cancels).
    void probeHint(const QString& msg);

protected:
    void initializeGLOverlays() override;
    void paintOverlays2D() override;
    void onBoardChanged() override;
    void onMousePressOverlay(QMouseEvent* e) override;

    // mouseMoveEvent is taken over entirely so we can augment the base
    // hover hit-test with the live voltage at the cursor; this means we
    // also manage our own pan state instead of leaning on the base's.
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;

private:
    void uploadIrResult();

    // Heat-map overlay.
    QOpenGLShaderProgram heat_prog_;
    QOpenGLBuffer heat_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer heat_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject heat_vao_;
    pdnkit::render::IrResultMesh pending_heat_;
    std::vector<pdnkit::render::IrResultMesh::LayerRange> heat_layer_ranges_;
    int heat_index_count_ = 0;
    bool heat_dirty_ = false;

    // Source / sink marker overlay -- single VBO, two index ranges.
    QOpenGLBuffer marker_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer marker_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject marker_vao_;
    int marker_source_index_start_ = 0;
    int marker_source_index_count_ = 0;
    int marker_sink_index_start_ = 0;
    int marker_sink_index_count_ = 0;

    // Decap markers -- own VBO so edits don't disturb the IR pipeline.
    QOpenGLBuffer decap_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer decap_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject decap_vao_;
    int decap_index_count_ = 0;
    std::vector<circuitcore::board::Point2> pending_decaps_;
    bool decaps_dirty_ = false;

    // Cavity highlight bbox + port markers.
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

    // Hotspot ring (drawn on top, yellow).
    QOpenGLBuffer hotspot_vbo_{QOpenGLBuffer::VertexBuffer};
    QOpenGLBuffer hotspot_ibo_{QOpenGLBuffer::IndexBuffer};
    QOpenGLVertexArrayObject hotspot_vao_;
    int hotspot_index_count_ = 0;
    bool hotspot_active_ = false;
    double hotspot_x_ = 0.0;
    double hotspot_y_ = 0.0;

    // Hover voltage probe: sampled from this IrMesh + Solution.
    pdnkit::pi::IrMesh probe_mesh_;
    pdnkit::pi::Solution probe_solution_;

    // Right-click probe-R: pad index of the first pick (-1 means none).
    int probe_pad_a_ = -1;

    // Own pan state (parallel to the base's; needed because we override
    // mouseMoveEvent fully for voltage-probe hover and can't share the
    // base's private flag).
    bool panning_ = false;
    QPoint last_mouse_;
};


}  // namespace pdnkit
