#include "PcbCanvas.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <QMatrix4x4>
#include <QPainter>
#include <QMouseEvent>
#include <QVector4D>
#include <QSettings>
#include <QWheelEvent>

#include "circuitcore/board/HitTest.h"
#include "circuitcore/ui/CircleHelper.h"
#include "circuitcore/ui/LayerColors.h"

namespace {

// Flat-color shader: position only, single color uniform.
constexpr auto kFlatVertSrc = R"(
#version 330 core
layout(location = 0) in vec2 a_pos;
uniform mat4 u_proj;
void main() {
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
}
)";

constexpr auto kFlatFragSrc = R"(
#version 330 core
uniform vec4 u_color;
out vec4 frag_color;
void main() {
    frag_color = u_color;
}
)";

// Heat-map shader: per-vertex scalar t in [0,1] → viridis-style colormap.
constexpr auto kHeatVertSrc = R"(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in float a_t;
out float v_t;
uniform mat4 u_proj;
void main() {
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
    v_t = a_t;
}
)";

constexpr auto kHeatFragSrc = R"(
#version 330 core
in float v_t;
out vec4 frag_color;
vec3 viridis(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c0 = vec3(0.267, 0.005, 0.329);
    vec3 c1 = vec3(0.231, 0.318, 0.545);
    vec3 c2 = vec3(0.127, 0.567, 0.550);
    vec3 c3 = vec3(0.369, 0.789, 0.382);
    vec3 c4 = vec3(0.992, 0.906, 0.144);
    if (t < 0.25) return mix(c0, c1, t * 4.0);
    if (t < 0.50) return mix(c1, c2, (t - 0.25) * 4.0);
    if (t < 0.75) return mix(c2, c3, (t - 0.50) * 4.0);
    return mix(c3, c4, (t - 0.75) * 4.0);
}
void main() {
    frag_color = vec4(viridis(v_t), 0.65);
}
)";

int render_priority(int ord) {
    if (ord == 0) return 1000;
    if (ord == 31) return 500;
    return 100 - ord;
}

QVector4D toQVec(const std::array<float, 4>& c) {
    return {c[0], c[1], c[2], c[3]};
}

}  // namespace

PcbCanvas::PcbCanvas(QWidget* parent) : QOpenGLWidget(parent) {
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
}

void PcbCanvas::setBoard(const circuitcore::board::Board* board) {
    board_ = board;
    pending_meshes_.clear();
    layer_visible_.clear();
    setIrResult({});  // clear any heat-map from a previous board

    if (board_) {
        pending_meshes_ = circuitcore::ui::build_all_meshes(*board_);
        meshes_dirty_ = true;
        fitToBoard();
    }
    buildOutline();
    update();
}

void PcbCanvas::setLayerVisibility(int ordinal, bool visible) {
    layer_visible_[ordinal] = visible;
    update();
}

void PcbCanvas::fitToBoard() {
    if (!board_) return;
    bool have_any = false;
    double lo_x = 0, lo_y = 0, hi_x = 0, hi_y = 0;
    auto include = [&](double x, double y) {
        if (!have_any) { lo_x = hi_x = x; lo_y = hi_y = y; have_any = true; }
        else {
            if (x < lo_x) lo_x = x;
            if (x > hi_x) hi_x = x;
            if (y < lo_y) lo_y = y;
            if (y > hi_y) hi_y = y;
        }
    };
    for (const auto& s : board_->segments) {
        include(s.start.x, s.start.y);
        include(s.end.x, s.end.y);
    }
    for (const auto& p : board_->pads) include(p.at.x, p.at.y);
    for (const auto& z : board_->zones) {
        for (const auto& pt : z.outline.outline) include(pt.x, pt.y);
        for (const auto& fp : z.filled)
            for (const auto& pt : fp.outline) include(pt.x, pt.y);
    }
    if (have_any) {
        camera_.fit_to_bounds({lo_x, lo_y}, {hi_x, hi_y},
                               width(), height(), 0.10);
        update();
    }
}

void PcbCanvas::setIrResult(pdnkit::render::IrResultMesh result) {
    pending_heat_ = std::move(result);
    heat_dirty_ = true;
    update();
}

void PcbCanvas::setProbeSource(pdnkit::pi::IrMesh mesh,
                                pdnkit::pi::Solution solution) {
    probe_mesh_ = std::move(mesh);
    probe_solution_ = std::move(solution);
    update();
}

void PcbCanvas::setDecapMarkers(const std::vector<circuitcore::board::Point2>& positions) {
    pending_decaps_ = positions;
    decaps_dirty_ = true;
    update();
}

void PcbCanvas::setCavityHighlight(double lo_x, double lo_y,
                                    double hi_x, double hi_y,
                                    const std::vector<circuitcore::board::Point2>& ports) {
    cavity_lo_x_ = lo_x;
    cavity_lo_y_ = lo_y;
    cavity_hi_x_ = hi_x;
    cavity_hi_y_ = hi_y;
    cavity_ports_ = ports;
    cavity_dirty_ = true;
    update();
}


void PcbCanvas::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    flat_prog_.addShaderFromSourceCode(QOpenGLShader::Vertex,   kFlatVertSrc);
    flat_prog_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFlatFragSrc);
    flat_prog_.link();

    heat_prog_.addShaderFromSourceCode(QOpenGLShader::Vertex,   kHeatVertSrc);
    heat_prog_.addShaderFromSourceCode(QOpenGLShader::Fragment, kHeatFragSrc);
    heat_prog_.link();

    grid_vao_.create();
    grid_vbo_.create();
    buildGrid();

    outline_vao_.create();
    outline_vbo_.create();
    // Outline geometry comes from the loaded board; setBoard triggers buildOutline.

    board_vao_.create();
    board_vbo_.create();
    board_ibo_.create();
    board_vao_.bind();
    board_vbo_.bind();
    board_ibo_.bind();
    flat_prog_.enableAttributeArray(0);
    flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    board_vao_.release();
    board_vbo_.release();
    board_ibo_.release();

    heat_vao_.create();
    heat_vbo_.create();
    heat_ibo_.create();
    heat_vao_.bind();
    heat_vbo_.bind();
    heat_ibo_.bind();
    // Stride = 3 floats (x, y, t).
    heat_prog_.enableAttributeArray(0);
    heat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2, 3 * sizeof(float));
    heat_prog_.enableAttributeArray(1);
    heat_prog_.setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 1,
                                  3 * sizeof(float));
    heat_vao_.release();
    heat_vbo_.release();
    heat_ibo_.release();

    marker_vao_.create();
    marker_vbo_.create();
    marker_ibo_.create();
    marker_vao_.bind();
    marker_vbo_.bind();
    marker_ibo_.bind();
    flat_prog_.enableAttributeArray(0);
    flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    marker_vao_.release();
    marker_vbo_.release();
    marker_ibo_.release();

    decap_vao_.create();
    decap_vbo_.create();
    decap_ibo_.create();
    decap_vao_.bind();
    decap_vbo_.bind();
    decap_ibo_.bind();
    flat_prog_.enableAttributeArray(0);
    flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    decap_vao_.release();
    decap_vbo_.release();
    decap_ibo_.release();

    cavity_rect_vao_.create();
    cavity_rect_vbo_.create();
    cavity_rect_vao_.bind();
    cavity_rect_vbo_.bind();
    flat_prog_.enableAttributeArray(0);
    flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    cavity_rect_vao_.release();
    cavity_rect_vbo_.release();

    cavity_port_vao_.create();
    cavity_port_vbo_.create();
    cavity_port_ibo_.create();

    hotspot_vao_.create();
    hotspot_vbo_.create();
    hotspot_ibo_.create();
    hotspot_vao_.bind();
    hotspot_vbo_.bind();
    hotspot_ibo_.bind();
    flat_prog_.enableAttributeArray(0);
    flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    hotspot_vao_.release();
    hotspot_vbo_.release();
    hotspot_ibo_.release();
    cavity_port_vao_.bind();
    cavity_port_vbo_.bind();
    cavity_port_ibo_.bind();
    flat_prog_.enableAttributeArray(0);
    flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    cavity_port_vao_.release();
    cavity_port_vbo_.release();
    cavity_port_ibo_.release();
}

void PcbCanvas::buildGrid() {
    std::vector<float> verts;
    const float lo = -0.5f, hi = 0.5f;
    const float step = 0.010f;
    for (float v = lo; v <= hi + 1e-6f; v += step) {
        verts.insert(verts.end(), {v, lo, v, hi});
        verts.insert(verts.end(), {lo, v, hi, v});
    }
    grid_vertex_count_ = static_cast<int>(verts.size() / 2);

    grid_vao_.bind();
    grid_vbo_.bind();
    grid_vbo_.allocate(verts.data(),
                       static_cast<int>(verts.size() * sizeof(float)));
    flat_prog_.enableAttributeArray(0);
    flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    grid_vbo_.release();
    grid_vao_.release();
}

void PcbCanvas::buildOutline() {
    outline_vertex_count_ = 0;
    if (!board_ || board_->outline.empty()) return;
    std::vector<float> verts;
    verts.reserve(board_->outline.size() * 4);
    for (const auto& seg : board_->outline) {
        verts.push_back(static_cast<float>(seg.start.x));
        verts.push_back(static_cast<float>(seg.start.y));
        verts.push_back(static_cast<float>(seg.end.x));
        verts.push_back(static_cast<float>(seg.end.y));
    }
    outline_vertex_count_ = static_cast<int>(verts.size() / 2);

    outline_vao_.bind();
    outline_vbo_.bind();
    outline_vbo_.allocate(verts.data(),
                          static_cast<int>(verts.size() * sizeof(float)));
    flat_prog_.enableAttributeArray(0);
    flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    outline_vbo_.release();
    outline_vao_.release();
}

void PcbCanvas::uploadBoardMeshes() {
    std::sort(pending_meshes_.begin(), pending_meshes_.end(),
              [](const auto& a, const auto& b) {
                  return render_priority(a.layer_ordinal) <
                         render_priority(b.layer_ordinal);
              });

    std::vector<float> all_verts;
    std::vector<std::uint32_t> all_indices;
    layer_ranges_.clear();
    layer_ranges_.reserve(pending_meshes_.size());

    std::uint32_t vbase = 0;
    int ibase = 0;
    for (const auto& m : pending_meshes_) {
        LayerRange r;
        r.ordinal = m.layer_ordinal;
        r.index_start = ibase;
        r.index_count = static_cast<int>(m.indices.size());
        layer_ranges_.push_back(r);

        all_verts.insert(all_verts.end(), m.vertices.begin(), m.vertices.end());
        for (auto idx : m.indices) all_indices.push_back(vbase + idx);
        vbase += static_cast<std::uint32_t>(m.vertex_count());
        ibase += static_cast<int>(m.indices.size());
    }

    board_vao_.bind();
    board_vbo_.bind();
    board_vbo_.allocate(all_verts.data(),
                        static_cast<int>(all_verts.size() * sizeof(float)));
    board_ibo_.bind();
    board_ibo_.allocate(all_indices.data(),
                        static_cast<int>(all_indices.size() * sizeof(std::uint32_t)));
    flat_prog_.enableAttributeArray(0);
    flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
    board_vao_.release();
    board_vbo_.release();
    board_ibo_.release();

    meshes_dirty_ = false;
}

void PcbCanvas::uploadIrResult() {
    heat_index_count_ = static_cast<int>(pending_heat_.indices.size());
    heat_layer_ranges_ = pending_heat_.layer_ranges;

    // Build marker geometry: small filled disks at each source/sink position.
    // The radius is in world units; choose ~3 cells worth so they stay
    // visible at typical zooms.
    {
        circuitcore::ui::LayerMesh src_mesh, snk_mesh;
        const double r = 0.5e-3;  // 0.5mm marker radius
        for (const auto& m : pending_heat_.markers) {
            if (m.current > 0.0)      circuitcore::ui::append_disk(src_mesh, m.x, m.y, r, 24);
            else if (m.current < 0.0) circuitcore::ui::append_disk(snk_mesh, m.x, m.y, r, 24);
        }
        std::vector<float> verts;
        std::vector<std::uint32_t> idx;
        marker_source_index_start_ = 0;
        marker_source_index_count_ = static_cast<int>(src_mesh.indices.size());
        for (auto v : src_mesh.vertices) verts.push_back(v);
        for (auto i : src_mesh.indices)  idx.push_back(i);
        const std::uint32_t snk_vbase = static_cast<std::uint32_t>(src_mesh.vertex_count());
        marker_sink_index_start_ = marker_source_index_count_;
        marker_sink_index_count_ = static_cast<int>(snk_mesh.indices.size());
        for (auto v : snk_mesh.vertices) verts.push_back(v);
        for (auto i : snk_mesh.indices)  idx.push_back(snk_vbase + i);

        marker_vao_.bind();
        marker_vbo_.bind();
        marker_vbo_.allocate(verts.data(),
            static_cast<int>(verts.size() * sizeof(float)));
        marker_ibo_.bind();
        marker_ibo_.allocate(idx.data(),
            static_cast<int>(idx.size() * sizeof(std::uint32_t)));
        flat_prog_.enableAttributeArray(0);
        flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
        marker_vao_.release();
        marker_vbo_.release();
        marker_ibo_.release();
    }

    // Hotspot ring: yellow 16-segment annulus around the worst point.
    if (pending_heat_.hotspot.valid) {
        hotspot_active_ = true;
        hotspot_x_ = pending_heat_.hotspot.x;
        hotspot_y_ = pending_heat_.hotspot.y;
        const double r_outer = 1.2e-3;  // 1.2 mm world-space ring
        const double r_inner = 0.7e-3;
        constexpr int kSeg = 24;
        std::vector<float> verts;
        std::vector<std::uint32_t> idx;
        verts.reserve(kSeg * 4);
        idx.reserve(kSeg * 6);
        for (int k = 0; k < kSeg; ++k) {
            const double a0 = (2.0 * M_PI) * static_cast<double>(k) / kSeg;
            const double a1 = (2.0 * M_PI) * static_cast<double>(k + 1) / kSeg;
            const float ox0 = static_cast<float>(hotspot_x_ + r_outer * std::cos(a0));
            const float oy0 = static_cast<float>(hotspot_y_ + r_outer * std::sin(a0));
            const float ix0 = static_cast<float>(hotspot_x_ + r_inner * std::cos(a0));
            const float iy0 = static_cast<float>(hotspot_y_ + r_inner * std::sin(a0));
            const float ox1 = static_cast<float>(hotspot_x_ + r_outer * std::cos(a1));
            const float oy1 = static_cast<float>(hotspot_y_ + r_outer * std::sin(a1));
            const float ix1 = static_cast<float>(hotspot_x_ + r_inner * std::cos(a1));
            const float iy1 = static_cast<float>(hotspot_y_ + r_inner * std::sin(a1));
            const std::uint32_t base = static_cast<std::uint32_t>(verts.size() / 2);
            verts.insert(verts.end(), {ox0, oy0, ix0, iy0, ix1, iy1, ox1, oy1});
            idx.insert(idx.end(),
                {base + 0, base + 1, base + 2,
                 base + 0, base + 2, base + 3});
        }
        hotspot_index_count_ = static_cast<int>(idx.size());
        hotspot_vao_.bind();
        hotspot_vbo_.bind();
        hotspot_vbo_.allocate(verts.data(),
            static_cast<int>(verts.size() * sizeof(float)));
        hotspot_ibo_.bind();
        hotspot_ibo_.allocate(idx.data(),
            static_cast<int>(idx.size() * sizeof(std::uint32_t)));
        flat_prog_.enableAttributeArray(0);
        flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
        hotspot_vao_.release();
        hotspot_vbo_.release();
        hotspot_ibo_.release();
    } else {
        hotspot_active_ = false;
        hotspot_index_count_ = 0;
    }

    heat_vao_.bind();
    heat_vbo_.bind();
    heat_vbo_.allocate(pending_heat_.vertices.data(),
                       static_cast<int>(pending_heat_.vertices.size() *
                                        sizeof(float)));
    heat_ibo_.bind();
    heat_ibo_.allocate(pending_heat_.indices.data(),
                       static_cast<int>(pending_heat_.indices.size() *
                                        sizeof(std::uint32_t)));
    heat_prog_.enableAttributeArray(0);
    heat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2, 3 * sizeof(float));
    heat_prog_.enableAttributeArray(1);
    heat_prog_.setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 1,
                                  3 * sizeof(float));
    heat_vao_.release();
    heat_vbo_.release();
    heat_ibo_.release();

    heat_dirty_ = false;
}

void PcbCanvas::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void PcbCanvas::paintGL() {
    if (meshes_dirty_) uploadBoardMeshes();
    if (heat_dirty_) uploadIrResult();

    glClear(GL_COLOR_BUFFER_BIT);

    const auto m = camera_.ortho_matrix(width(), height());
    const QMatrix4x4 proj(
        m[0], m[4], m[8],  m[12],
        m[1], m[5], m[9],  m[13],
        m[2], m[6], m[10], m[14],
        m[3], m[7], m[11], m[15]);

    flat_prog_.bind();
    flat_prog_.setUniformValue("u_proj", proj);

    flat_prog_.setUniformValue("u_color", QVector4D(0.22f, 0.22f, 0.28f, 1.0f));
    grid_vao_.bind();
    glDrawArrays(GL_LINES, 0, grid_vertex_count_);
    grid_vao_.release();

    if (outline_vertex_count_ > 0) {
        flat_prog_.setUniformValue("u_color", QVector4D(0.85f, 0.85f, 0.88f, 1.0f));
        outline_vao_.bind();
        glDrawArrays(GL_LINES, 0, outline_vertex_count_);
        outline_vao_.release();
    }

    if (!layer_ranges_.empty()) {
        board_vao_.bind();
        for (const auto& r : layer_ranges_) {
            auto vis_it = layer_visible_.find(r.ordinal);
            const bool visible = (vis_it == layer_visible_.end()) || vis_it->second;
            if (!visible) continue;
            flat_prog_.setUniformValue("u_color",
                                       toQVec(circuitcore::ui::layer_color(r.ordinal)));
            glDrawElements(GL_TRIANGLES, r.index_count, GL_UNSIGNED_INT,
                           reinterpret_cast<const void*>(
                               static_cast<std::uintptr_t>(r.index_start *
                                                            sizeof(std::uint32_t))));
        }
        board_vao_.release();
    }
    flat_prog_.release();

    // IR-drop heat-map overlay, on top of the board. Per-layer ranges so
    // we honor the same layer visibility map that filters board geometry.
    if (heat_index_count_ > 0) {
        heat_prog_.bind();
        heat_prog_.setUniformValue("u_proj", proj);
        heat_vao_.bind();
        if (heat_layer_ranges_.empty()) {
            // Single-layer or pre-grouped legacy result — draw the whole batch.
            glDrawElements(GL_TRIANGLES, heat_index_count_, GL_UNSIGNED_INT, nullptr);
        } else {
            for (const auto& r : heat_layer_ranges_) {
                auto vis_it = layer_visible_.find(r.ordinal);
                const bool visible = (vis_it == layer_visible_.end()) || vis_it->second;
                if (!visible) continue;
                glDrawElements(GL_TRIANGLES, r.index_count, GL_UNSIGNED_INT,
                               reinterpret_cast<const void*>(
                                   static_cast<std::uintptr_t>(
                                       r.index_start * sizeof(std::uint32_t))));
            }
        }
        heat_vao_.release();
        heat_prog_.release();
    }

    // Markers: bright source = lime green, sink = red, drawn opaque over the
    // heat-map so the user can see where current is injected/drawn.
    if (marker_source_index_count_ > 0 || marker_sink_index_count_ > 0) {
        flat_prog_.bind();
        flat_prog_.setUniformValue("u_proj", proj);
        marker_vao_.bind();
        if (marker_source_index_count_ > 0) {
            flat_prog_.setUniformValue("u_color", QVector4D(0.20f, 0.95f, 0.30f, 1.0f));
            glDrawElements(GL_TRIANGLES, marker_source_index_count_, GL_UNSIGNED_INT,
                           reinterpret_cast<const void*>(
                               static_cast<std::uintptr_t>(
                                   marker_source_index_start_ * sizeof(std::uint32_t))));
        }
        if (marker_sink_index_count_ > 0) {
            flat_prog_.setUniformValue("u_color", QVector4D(0.95f, 0.20f, 0.20f, 1.0f));
            glDrawElements(GL_TRIANGLES, marker_sink_index_count_, GL_UNSIGNED_INT,
                           reinterpret_cast<const void*>(
                               static_cast<std::uintptr_t>(
                                   marker_sink_index_start_ * sizeof(std::uint32_t))));
        }
        marker_vao_.release();
        flat_prog_.release();
    }

    // Decap markers: blue dots over everything (rebuilt lazily here so the
    // GL context is current when we touch buffers).
    if (decaps_dirty_) {
        circuitcore::ui::LayerMesh dm;
        const double r = 0.6e-3;
        for (const auto& p : pending_decaps_) {
            circuitcore::ui::append_disk(dm, p.x, p.y, r, 24);
        }
        decap_index_count_ = static_cast<int>(dm.indices.size());

        decap_vao_.bind();
        decap_vbo_.bind();
        decap_vbo_.allocate(dm.vertices.data(),
                            static_cast<int>(dm.vertices.size() * sizeof(float)));
        decap_ibo_.bind();
        decap_ibo_.allocate(dm.indices.data(),
                            static_cast<int>(dm.indices.size() * sizeof(std::uint32_t)));
        flat_prog_.enableAttributeArray(0);
        flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
        decap_vao_.release();
        decap_vbo_.release();
        decap_ibo_.release();
        decaps_dirty_ = false;
    }

    if (decap_index_count_ > 0) {
        flat_prog_.bind();
        flat_prog_.setUniformValue("u_proj", proj);
        flat_prog_.setUniformValue("u_color", QVector4D(0.30f, 0.55f, 0.98f, 1.0f));
        decap_vao_.bind();
        glDrawElements(GL_TRIANGLES, decap_index_count_, GL_UNSIGNED_INT, nullptr);
        decap_vao_.release();
        flat_prog_.release();
    }

    // Welcome overlay: shown when no board is loaded.
    if (!board_) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(QColor(150, 160, 175));
        QFont f = painter.font();
        f.setPointSizeF(f.pointSizeF() + 6);
        f.setBold(true);
        painter.setFont(f);
        painter.drawText(rect(), Qt::AlignCenter,
            "Drop a .kicad_pcb file here\nor use File > Open KiCad PCB...");
    }

    // Voltage labels at source/sink markers (when a probe solution is loaded).
    if (probe_solution_.ok &&
        !probe_mesh_.nodes.empty() &&
        probe_solution_.voltages.size() == probe_mesh_.nodes.size() &&
        (!probe_mesh_.source_node_ids.empty() ||
         !probe_mesh_.sink_node_ids.empty())) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        QFont lf = painter.font();
        lf.setPointSizeF(lf.pointSizeF() - 1);
        lf.setBold(true);
        painter.setFont(lf);

        auto draw_label = [&](int node_id, QColor color) {
            if (node_id < 0 ||
                node_id >= static_cast<int>(probe_mesh_.nodes.size())) return;
            const auto& n = probe_mesh_.nodes[node_id];
            double sx = 0, sy = 0;
            camera_.world_to_screen({n.x, n.y}, width(), height(), sx, sy);
            const double v = probe_solution_.voltages[node_id];
            const QString text = (std::abs(v) >= 1.0)
                ? QString::number(v, 'f', 4) + " V"
                : QString::number(v * 1000.0, 'f', 3) + " mV";
            const QPoint anchor(static_cast<int>(sx) + 10,
                                static_cast<int>(sy) - 4);
            // Dark outline + colored text -- legible over the heat-map.
            painter.setPen(QColor(0, 0, 0, 200));
            for (int dx = -1; dx <= 1; ++dx)
                for (int dy = -1; dy <= 1; ++dy)
                    if (dx || dy)
                        painter.drawText(anchor + QPoint(dx, dy), text);
            painter.setPen(color);
            painter.drawText(anchor, text);
        };
        for (int nid : probe_mesh_.source_node_ids)
            draw_label(nid, QColor(180, 255, 200));
        for (int nid : probe_mesh_.sink_node_ids)
            draw_label(nid, QColor(255, 200, 200));
    }

    // Cavity overlay: lazily upload, then draw rect + port markers.
    if (cavity_dirty_) {
        // Rect as a 5-vertex line strip (uses GL_LINE_STRIP) drawing the
        // four edges; if the rect is degenerate (hi <= lo) we skip.
        std::vector<float> rverts;
        if (cavity_hi_x_ > cavity_lo_x_ && cavity_hi_y_ > cavity_lo_y_) {
            rverts = {
                static_cast<float>(cavity_lo_x_), static_cast<float>(cavity_lo_y_),
                static_cast<float>(cavity_hi_x_), static_cast<float>(cavity_lo_y_),
                static_cast<float>(cavity_hi_x_), static_cast<float>(cavity_hi_y_),
                static_cast<float>(cavity_lo_x_), static_cast<float>(cavity_hi_y_),
                static_cast<float>(cavity_lo_x_), static_cast<float>(cavity_lo_y_),
            };
        }
        cavity_rect_vertex_count_ = static_cast<int>(rverts.size() / 2);

        cavity_rect_vao_.bind();
        cavity_rect_vbo_.bind();
        if (!rverts.empty()) {
            cavity_rect_vbo_.allocate(rverts.data(),
                static_cast<int>(rverts.size() * sizeof(float)));
            flat_prog_.enableAttributeArray(0);
            flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
        }
        cavity_rect_vao_.release();
        cavity_rect_vbo_.release();

        // Port markers (small filled disks).
        circuitcore::ui::LayerMesh pm;
        const double r = 0.8e-3;
        for (const auto& p : cavity_ports_) {
            circuitcore::ui::append_disk(pm, p.x, p.y, r, 24);
        }
        cavity_port_index_count_ = static_cast<int>(pm.indices.size());

        cavity_port_vao_.bind();
        cavity_port_vbo_.bind();
        if (!pm.vertices.empty()) {
            cavity_port_vbo_.allocate(pm.vertices.data(),
                static_cast<int>(pm.vertices.size() * sizeof(float)));
        }
        cavity_port_ibo_.bind();
        if (!pm.indices.empty()) {
            cavity_port_ibo_.allocate(pm.indices.data(),
                static_cast<int>(pm.indices.size() * sizeof(std::uint32_t)));
        }
        if (!pm.vertices.empty()) {
            flat_prog_.enableAttributeArray(0);
            flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
        }
        cavity_port_vao_.release();
        cavity_port_vbo_.release();
        cavity_port_ibo_.release();

        cavity_dirty_ = false;
    }

    if (cavity_rect_vertex_count_ > 0) {
        flat_prog_.bind();
        flat_prog_.setUniformValue("u_proj", proj);
        flat_prog_.setUniformValue("u_color", QVector4D(0.30f, 0.85f, 0.95f, 0.85f));
        glLineWidth(2.0f);
        cavity_rect_vao_.bind();
        glDrawArrays(GL_LINE_STRIP, 0, cavity_rect_vertex_count_);
        cavity_rect_vao_.release();
        glLineWidth(1.0f);
        flat_prog_.release();
    }
    if (cavity_port_index_count_ > 0) {
        flat_prog_.bind();
        flat_prog_.setUniformValue("u_proj", proj);
        flat_prog_.setUniformValue("u_color", QVector4D(0.10f, 0.95f, 0.95f, 1.0f));
        cavity_port_vao_.bind();
        glDrawElements(GL_TRIANGLES, cavity_port_index_count_, GL_UNSIGNED_INT, nullptr);
        cavity_port_vao_.release();
        flat_prog_.release();
    }

    // Hotspot ring on top of everything else, in saturated yellow.
    if (hotspot_active_ && hotspot_index_count_ > 0) {
        flat_prog_.bind();
        flat_prog_.setUniformValue("u_proj", proj);
        flat_prog_.setUniformValue("u_color", QVector4D(1.0f, 0.95f, 0.10f, 1.0f));
        hotspot_vao_.bind();
        glDrawElements(GL_TRIANGLES, hotspot_index_count_,
                       GL_UNSIGNED_INT, nullptr);
        hotspot_vao_.release();
        flat_prog_.release();
    }
}

void PcbCanvas::mousePressEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton) {
        panning_ = true;
        last_mouse_ = e->pos();
        return;
    }
    if (e->button() == Qt::RightButton && board_) {
        // Right-click probe-R: first pad sets the source; second pad on the
        // same net emits probeRequested. Right-click on empty space cancels.
        const auto world = camera_.screen_to_world(
            e->pos().x(), e->pos().y(), width(), height());
        const double tol = 6.0 / camera_.pixels_per_meter;
        const auto hit = circuitcore::board::hittest::at_point(*board_, world, tol);
        if (hit.kind != circuitcore::board::hittest::Hit::Kind::Pad ||
            hit.element_index < 0) {
            if (probe_pad_a_ >= 0) {
                probe_pad_a_ = -1;
                emit probeHint("Probe R: selection cancelled.");
            }
            return;
        }
        if (probe_pad_a_ < 0) {
            probe_pad_a_ = hit.element_index;
            const auto& pad = board_->pads[probe_pad_a_];
            const auto* net = board_->find_net(pad.net_id);
            const QString net_name = (net && !net->name.empty())
                ? QString::fromStdString(net->name)
                : QString("(unnamed)");
            emit probeHint(QString("Probe R: source pad '%1' on net %2."
                                   "  Right-click another pad on the same "
                                   "net to measure.")
                               .arg(QString::fromStdString(pad.name))
                               .arg(net_name));
            return;
        }
        // Second pick.
        const int a = probe_pad_a_;
        const int b = hit.element_index;
        probe_pad_a_ = -1;
        if (a == b) {
            emit probeHint("Probe R: same pad picked twice -- cancelled.");
            return;
        }
        const auto& pa = board_->pads[a];
        const auto& pb = board_->pads[b];
        if (pa.net_id != pb.net_id) {
            emit probeHint("Probe R: pads are on different nets -- "
                           "cancelled.");
            return;
        }
        const int layer_ord = pa.layer_ordinals.empty()
            ? hit.layer_ordinal
            : pa.layer_ordinals.front();
        emit probeRequested(a, b, pa.net_id, layer_ord);
    }
}

void PcbCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (panning_) {
        const QPoint d = e->pos() - last_mouse_;
        camera_.pan_pixels(d.x(), d.y());
        last_mouse_ = e->pos();
        update();
    }

    if (board_) {
        const auto world = camera_.screen_to_world(
            e->pos().x(), e->pos().y(), width(), height());
        const double tol = 4.0 / camera_.pixels_per_meter;
        const auto hit = circuitcore::board::hittest::at_point(*board_, world, tol);

        QString info;
        if (hit.kind != circuitcore::board::hittest::Hit::Kind::None) {
            const auto* net = board_->find_net(hit.net_id);
            const auto* layer = board_->find_layer(hit.layer_ordinal);
            const QString net_name = (net && !net->name.empty())
                ? QString::fromStdString(net->name)
                : QString("(unnamed)");
            const QString layer_name = layer
                ? QString::fromStdString(layer->name)
                : QString("?");
            info = QString("%1   net %2 (%3)   layer %4")
                       .arg(circuitcore::board::hittest::name(hit.kind))
                       .arg(net_name)
                       .arg(hit.net_id)
                       .arg(layer_name);
        }

        // Voltage probe: if an IR-drop solution is loaded, find the nearest
        // mesh node to the cursor and append its voltage to the hover info.
        if (probe_solution_.ok &&
            !probe_mesh_.nodes.empty() &&
            probe_solution_.voltages.size() == probe_mesh_.nodes.size()) {
            int best = -1;
            double best_d2 = 1e30;
            for (std::size_t i = 0; i < probe_mesh_.nodes.size(); ++i) {
                const double dx = probe_mesh_.nodes[i].x - world.x;
                const double dy = probe_mesh_.nodes[i].y - world.y;
                const double d2 = dx * dx + dy * dy;
                if (d2 < best_d2) { best_d2 = d2; best = static_cast<int>(i); }
            }
            if (best >= 0) {
                const double v = probe_solution_.voltages[best];
                const QString v_str = (std::abs(v) >= 1.0)
                    ? QString::number(v, 'f', 4) + " V"
                    : QString::number(v * 1000.0, 'f', 4) + " mV";
                if (!info.isEmpty()) info += "   ";
                info += "V = " + v_str;
            }
        }
        emit hoverInfo(info);
    }
}

void PcbCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton) {
        panning_ = false;
    }
}

void PcbCanvas::wheelEvent(QWheelEvent* e) {
    const double factor = (e->angleDelta().y() > 0) ? 1.20 : 1.0 / 1.20;
    const QPointF pos = e->position();
    camera_.zoom_at(pos.x(), pos.y(), factor, width(), height());
    update();
}

void PcbCanvas::saveSettings(QSettings& settings) const {
    settings.setValue("canvas/center_x", camera_.center.x);
    settings.setValue("canvas/center_y", camera_.center.y);
    settings.setValue("canvas/pixels_per_meter", camera_.pixels_per_meter);
}

void PcbCanvas::restoreSettings(QSettings& settings) {
    bool ok_x = false, ok_y = false, ok_z = false;
    const double cx = settings.value("canvas/center_x").toDouble(&ok_x);
    const double cy = settings.value("canvas/center_y").toDouble(&ok_y);
    const double ppm = settings.value("canvas/pixels_per_meter").toDouble(&ok_z);
    if (ok_x && ok_y && ok_z && ppm > 0.0) {
        camera_.center = {cx, cy};
        camera_.pixels_per_meter = ppm;
        update();
    }
}
