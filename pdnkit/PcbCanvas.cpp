#include "PcbCanvas.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QVector4D>

#include "circuitcore/board/HitTest.h"
#include "circuitcore/ui/CircleHelper.h"

namespace pdnkit {

namespace {

// Heat-map shader: per-vertex scalar t in [0,1] -> viridis colormap.
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

}  // namespace

PcbCanvas::PcbCanvas(QWidget* parent)
    : circuitcore::ui::PcbCanvas(parent) {}

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

void PcbCanvas::setDecapMarkers(
    const std::vector<circuitcore::board::Point2>& positions) {
    pending_decaps_ = positions;
    decaps_dirty_ = true;
    update();
}

void PcbCanvas::setCavityHighlight(
    double lo_x, double lo_y, double hi_x, double hi_y,
    const std::vector<circuitcore::board::Point2>& ports) {
    cavity_lo_x_ = lo_x; cavity_lo_y_ = lo_y;
    cavity_hi_x_ = hi_x; cavity_hi_y_ = hi_y;
    cavity_ports_ = ports;
    cavity_dirty_ = true;
    update();
}

void PcbCanvas::onBoardChanged() {
    setIrResult({});                          // clear heat
    pending_decaps_.clear();
    decaps_dirty_ = true;
    cavity_lo_x_ = cavity_lo_y_ = 0.0;
    cavity_hi_x_ = cavity_hi_y_ = 0.0;
    cavity_ports_.clear();
    cavity_dirty_ = true;
    probe_mesh_ = {};
    probe_solution_ = {};
    probe_pad_a_ = -1;
}

void PcbCanvas::initializeGLOverlays() {
    heat_prog_.addShaderFromSourceCode(QOpenGLShader::Vertex,   kHeatVertSrc);
    heat_prog_.addShaderFromSourceCode(QOpenGLShader::Fragment, kHeatFragSrc);
    heat_prog_.link();

    auto& flat = flat_program();

    heat_vao_.create();
    heat_vbo_.create();
    heat_ibo_.create();
    heat_vao_.bind();
    heat_vbo_.bind();
    heat_ibo_.bind();
    heat_prog_.enableAttributeArray(0);
    heat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2, 3 * sizeof(float));
    heat_prog_.enableAttributeArray(1);
    heat_prog_.setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 1,
                                   3 * sizeof(float));
    heat_vao_.release(); heat_vbo_.release(); heat_ibo_.release();

    auto bind_flat = [&](QOpenGLVertexArrayObject& vao,
                          QOpenGLBuffer& vbo, QOpenGLBuffer* ibo) {
        vao.create(); vbo.create();
        if (ibo) ibo->create();
        vao.bind(); vbo.bind();
        if (ibo) ibo->bind();
        flat.enableAttributeArray(0);
        flat.setAttributeBuffer(0, GL_FLOAT, 0, 2);
        vao.release(); vbo.release();
        if (ibo) ibo->release();
    };
    bind_flat(marker_vao_,      marker_vbo_,      &marker_ibo_);
    bind_flat(decap_vao_,       decap_vbo_,       &decap_ibo_);
    bind_flat(cavity_rect_vao_, cavity_rect_vbo_, nullptr);
    bind_flat(cavity_port_vao_, cavity_port_vbo_, &cavity_port_ibo_);
    bind_flat(hotspot_vao_,     hotspot_vbo_,     &hotspot_ibo_);
}

void PcbCanvas::uploadIrResult() {
    heat_index_count_ = static_cast<int>(pending_heat_.indices.size());
    heat_layer_ranges_ = pending_heat_.layer_ranges;

    // Source / sink marker geometry.
    {
        circuitcore::ui::LayerMesh src_mesh, snk_mesh;
        const double r = 0.5e-3;
        for (const auto& m : pending_heat_.markers) {
            if (m.current > 0.0)
                circuitcore::ui::append_disk(src_mesh, m.x, m.y, r, 24);
            else if (m.current < 0.0)
                circuitcore::ui::append_disk(snk_mesh, m.x, m.y, r, 24);
        }
        std::vector<float> verts;
        std::vector<std::uint32_t> idx;
        marker_source_index_start_ = 0;
        marker_source_index_count_ =
            static_cast<int>(src_mesh.indices.size());
        for (auto v : src_mesh.vertices) verts.push_back(v);
        for (auto i : src_mesh.indices)  idx.push_back(i);
        const std::uint32_t snk_vbase =
            static_cast<std::uint32_t>(src_mesh.vertex_count());
        marker_sink_index_start_ = marker_source_index_count_;
        marker_sink_index_count_ =
            static_cast<int>(snk_mesh.indices.size());
        for (auto v : snk_mesh.vertices) verts.push_back(v);
        for (auto i : snk_mesh.indices)  idx.push_back(snk_vbase + i);

        marker_vao_.bind();
        marker_vbo_.bind();
        marker_vbo_.allocate(verts.data(),
            static_cast<int>(verts.size() * sizeof(float)));
        marker_ibo_.bind();
        marker_ibo_.allocate(idx.data(),
            static_cast<int>(idx.size() * sizeof(std::uint32_t)));
        flat_program().enableAttributeArray(0);
        flat_program().setAttributeBuffer(0, GL_FLOAT, 0, 2);
        marker_vao_.release(); marker_vbo_.release(); marker_ibo_.release();
    }

    // Hotspot ring (24-segment annulus, yellow).
    if (pending_heat_.hotspot.valid) {
        hotspot_active_ = true;
        hotspot_x_ = pending_heat_.hotspot.x;
        hotspot_y_ = pending_heat_.hotspot.y;
        const double r_outer = 1.2e-3, r_inner = 0.7e-3;
        constexpr int kSeg = 24;
        std::vector<float> verts;
        std::vector<std::uint32_t> idx;
        verts.reserve(kSeg * 8);
        idx.reserve(kSeg * 6);
        for (int k = 0; k < kSeg; ++k) {
            const double a0 = (2.0 * M_PI) * k / kSeg;
            const double a1 = (2.0 * M_PI) * (k + 1) / kSeg;
            const float ox0 = static_cast<float>(hotspot_x_ + r_outer * std::cos(a0));
            const float oy0 = static_cast<float>(hotspot_y_ + r_outer * std::sin(a0));
            const float ix0 = static_cast<float>(hotspot_x_ + r_inner * std::cos(a0));
            const float iy0 = static_cast<float>(hotspot_y_ + r_inner * std::sin(a0));
            const float ox1 = static_cast<float>(hotspot_x_ + r_outer * std::cos(a1));
            const float oy1 = static_cast<float>(hotspot_y_ + r_outer * std::sin(a1));
            const float ix1 = static_cast<float>(hotspot_x_ + r_inner * std::cos(a1));
            const float iy1 = static_cast<float>(hotspot_y_ + r_inner * std::sin(a1));
            const std::uint32_t base =
                static_cast<std::uint32_t>(verts.size() / 2);
            verts.insert(verts.end(),
                          {ox0, oy0, ix0, iy0, ix1, iy1, ox1, oy1});
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
        flat_program().enableAttributeArray(0);
        flat_program().setAttributeBuffer(0, GL_FLOAT, 0, 2);
        hotspot_vao_.release(); hotspot_vbo_.release(); hotspot_ibo_.release();
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
    heat_vao_.release(); heat_vbo_.release(); heat_ibo_.release();

    heat_dirty_ = false;
}

void PcbCanvas::paintOverlays2D() {
    if (heat_dirty_) uploadIrResult();

    const QMatrix4x4 proj = ortho_matrix();
    auto& flat = flat_program();

    // Heat-map.
    if (heat_index_count_ > 0) {
        heat_prog_.bind();
        heat_prog_.setUniformValue("u_proj", proj);
        heat_vao_.bind();
        if (heat_layer_ranges_.empty()) {
            glDrawElements(GL_TRIANGLES, heat_index_count_,
                            GL_UNSIGNED_INT, nullptr);
        } else {
            for (const auto& r : heat_layer_ranges_) {
                // Layer visibility is the base's; we don't have direct
                // access to its private map, so honour the heat layer
                // range unconditionally for now. (A virtual hook for
                // "is layer ordinal visible?" would close this gap in
                // a follow-up.)
                glDrawElements(GL_TRIANGLES, r.index_count, GL_UNSIGNED_INT,
                                reinterpret_cast<const void*>(
                                    static_cast<std::uintptr_t>(
                                        r.index_start * sizeof(std::uint32_t))));
            }
        }
        heat_vao_.release();
        heat_prog_.release();
    }

    // Source / sink markers.
    if (marker_source_index_count_ > 0 || marker_sink_index_count_ > 0) {
        flat.bind();
        flat.setUniformValue("u_proj", proj);
        marker_vao_.bind();
        if (marker_source_index_count_ > 0) {
            flat.setUniformValue("u_color",
                                  QVector4D(0.20f, 0.95f, 0.30f, 1.0f));
            glDrawElements(GL_TRIANGLES, marker_source_index_count_,
                            GL_UNSIGNED_INT,
                            reinterpret_cast<const void*>(
                                static_cast<std::uintptr_t>(
                                    marker_source_index_start_ *
                                      sizeof(std::uint32_t))));
        }
        if (marker_sink_index_count_ > 0) {
            flat.setUniformValue("u_color",
                                  QVector4D(0.95f, 0.20f, 0.20f, 1.0f));
            glDrawElements(GL_TRIANGLES, marker_sink_index_count_,
                            GL_UNSIGNED_INT,
                            reinterpret_cast<const void*>(
                                static_cast<std::uintptr_t>(
                                    marker_sink_index_start_ *
                                      sizeof(std::uint32_t))));
        }
        marker_vao_.release();
        flat.release();
    }

    // Decap dots -- lazy rebuild here, GL context is current.
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
        flat.enableAttributeArray(0);
        flat.setAttributeBuffer(0, GL_FLOAT, 0, 2);
        decap_vao_.release(); decap_vbo_.release(); decap_ibo_.release();
        decaps_dirty_ = false;
    }
    if (decap_index_count_ > 0) {
        flat.bind();
        flat.setUniformValue("u_proj", proj);
        flat.setUniformValue("u_color",
                              QVector4D(0.30f, 0.55f, 0.98f, 1.0f));
        decap_vao_.bind();
        glDrawElements(GL_TRIANGLES, decap_index_count_,
                        GL_UNSIGNED_INT, nullptr);
        decap_vao_.release();
        flat.release();
    }

    // Cavity highlight.
    if (cavity_dirty_) {
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
            flat.enableAttributeArray(0);
            flat.setAttributeBuffer(0, GL_FLOAT, 0, 2);
        }
        cavity_rect_vao_.release(); cavity_rect_vbo_.release();

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
            flat.enableAttributeArray(0);
            flat.setAttributeBuffer(0, GL_FLOAT, 0, 2);
        }
        cavity_port_vao_.release();
        cavity_port_vbo_.release();
        cavity_port_ibo_.release();
        cavity_dirty_ = false;
    }
    if (cavity_rect_vertex_count_ > 0) {
        flat.bind();
        flat.setUniformValue("u_proj", proj);
        flat.setUniformValue("u_color",
                              QVector4D(0.30f, 0.85f, 0.95f, 0.85f));
        glLineWidth(2.0f);
        cavity_rect_vao_.bind();
        glDrawArrays(GL_LINE_STRIP, 0, cavity_rect_vertex_count_);
        cavity_rect_vao_.release();
        glLineWidth(1.0f);
        flat.release();
    }
    if (cavity_port_index_count_ > 0) {
        flat.bind();
        flat.setUniformValue("u_proj", proj);
        flat.setUniformValue("u_color",
                              QVector4D(0.10f, 0.95f, 0.95f, 1.0f));
        cavity_port_vao_.bind();
        glDrawElements(GL_TRIANGLES, cavity_port_index_count_,
                        GL_UNSIGNED_INT, nullptr);
        cavity_port_vao_.release();
        flat.release();
    }

    // Hotspot ring on top.
    if (hotspot_active_ && hotspot_index_count_ > 0) {
        flat.bind();
        flat.setUniformValue("u_proj", proj);
        flat.setUniformValue("u_color",
                              QVector4D(1.0f, 0.95f, 0.10f, 1.0f));
        hotspot_vao_.bind();
        glDrawElements(GL_TRIANGLES, hotspot_index_count_,
                        GL_UNSIGNED_INT, nullptr);
        hotspot_vao_.release();
        flat.release();
    }

    // Voltage labels at source/sink markers, via QPainter on top of GL.
    if (probe_solution_.ok && !probe_mesh_.nodes.empty() &&
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
            camera().world_to_screen({n.x, n.y}, width(), height(), sx, sy);
            const double v = probe_solution_.voltages[node_id];
            const QString text = (std::abs(v) >= 1.0)
                ? QString::number(v, 'f', 4) + " V"
                : QString::number(v * 1000.0, 'f', 3) + " mV";
            const QPoint anchor(static_cast<int>(sx) + 10,
                                 static_cast<int>(sy) - 4);
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
}

void PcbCanvas::onMousePressOverlay(QMouseEvent* e) {
    if (e->button() != Qt::RightButton || !board()) return;
    // Right-click probe-R: first pad sets the source; second pad on the
    // same net emits probeRequested. Right-click empty space cancels.
    const auto world = camera().screen_to_world(
        e->pos().x(), e->pos().y(), width(), height());
    const double tol = 6.0 / camera().pixels_per_meter;
    const auto hit = circuitcore::board::hittest::at_point(*board(), world, tol);
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
        const auto& pad = board()->pads[probe_pad_a_];
        const auto* net = board()->find_net(pad.net_id);
        const QString net_name = (net && !net->name.empty())
            ? QString::fromStdString(net->name) : QString("(unnamed)");
        emit probeHint(QString("Probe R: source pad '%1' on net %2.  "
                                "Right-click another pad on the same net "
                                "to measure.")
                            .arg(QString::fromStdString(pad.name))
                            .arg(net_name));
        return;
    }
    const int a = probe_pad_a_;
    const int b = hit.element_index;
    probe_pad_a_ = -1;
    if (a == b) {
        emit probeHint("Probe R: same pad picked twice -- cancelled.");
        return;
    }
    const auto& pa = board()->pads[a];
    const auto& pb = board()->pads[b];
    if (pa.net_id != pb.net_id) {
        emit probeHint("Probe R: pads are on different nets -- cancelled.");
        return;
    }
    const int layer_ord = pa.layer_ordinals.empty()
        ? hit.layer_ordinal : pa.layer_ordinals.front();
    emit probeRequested(a, b, pa.net_id, layer_ord);
}

// --- mouse handlers ---
//
// We override mouseMoveEvent entirely to augment the base hit-test
// with the live voltage at the cursor. That means we also manage our
// own pan state -- the base's panning_/last_mouse_ are private and
// the base never gets a chance to update them when our handlers run.

void PcbCanvas::mousePressEvent(QMouseEvent* e) {
    // Let the base run first so the probe-R hook fires before we
    // start the pan flag.
    circuitcore::ui::PcbCanvas::mousePressEvent(e);
    if (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton) {
        panning_ = true;
        last_mouse_ = e->pos();
    }
}

void PcbCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton) {
        panning_ = false;
    }
    circuitcore::ui::PcbCanvas::mouseReleaseEvent(e);
}

void PcbCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (panning_) {
        const QPoint d = e->pos() - last_mouse_;
        camera_mut().pan_pixels(d.x(), d.y());
        last_mouse_ = e->pos();
        update();
    }
    if (board()) {
        const auto world = camera().screen_to_world(
            e->pos().x(), e->pos().y(), width(), height());
        emit hoverPos(world.x, world.y);

        const double tol = 4.0 / camera().pixels_per_meter;
        const auto hit = circuitcore::board::hittest::at_point(*board(), world, tol);

        QString info;
        if (hit.kind != circuitcore::board::hittest::Hit::Kind::None) {
            const auto* net = board()->find_net(hit.net_id);
            const auto* layer = board()->find_layer(hit.layer_ordinal);
            const QString net_name = (net && !net->name.empty())
                ? QString::fromStdString(net->name) : QString("(unnamed)");
            const QString layer_name = layer
                ? QString::fromStdString(layer->name) : QString("?");
            info = QString("%1   net %2 (%3)   layer %4")
                       .arg(circuitcore::board::hittest::name(hit.kind))
                       .arg(net_name).arg(hit.net_id).arg(layer_name);
        }
        // Voltage probe augment.
        if (probe_solution_.ok && !probe_mesh_.nodes.empty() &&
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


}  // namespace pdnkit
