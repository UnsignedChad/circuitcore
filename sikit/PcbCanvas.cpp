#include "PcbCanvas.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QVector3D>
#include <QVector4D>
#include <QWheelEvent>

namespace sikit {

namespace {

constexpr auto kVcolVertSrc = R"(
#version 330 core
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec4 a_color;
out vec4 v_color;
uniform mat4 u_proj;
void main() {
    gl_Position = u_proj * vec4(a_pos, 0.0, 1.0);
    v_color = a_color;
}
)";

constexpr auto kVcolFragSrc = R"(
#version 330 core
in vec4 v_color;
out vec4 frag_color;
void main() {
    frag_color = v_color;
}
)";

// 3D lit shader -- same shape as sikit always had: pos + normal +
// rgba, ambient + Lambert with a fixed view-aligned light.
constexpr auto kLitVertSrc = R"(
#version 330 core
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_normal;
layout(location = 2) in vec4 a_color;
uniform mat4 u_vp;
out vec3 v_normal;
out vec4 v_color;
void main() {
    gl_Position = u_vp * vec4(a_pos, 1.0);
    v_normal = a_normal;
    v_color = a_color;
}
)";

constexpr auto kLitFragSrc = R"(
#version 330 core
in vec3 v_normal;
in vec4 v_color;
uniform vec3 u_light_dir;
out vec4 frag_color;
void main() {
    vec3 n = normalize(v_normal);
    float lambert = max(dot(n, normalize(u_light_dir)), 0.0);
    float ambient = 0.35;
    float intensity = ambient + (1.0 - ambient) * lambert;
    frag_color = vec4(v_color.rgb * intensity, v_color.a);
}
)";

}  // namespace

PcbCanvas::PcbCanvas(QWidget* parent)
    : circuitcore::ui::PcbCanvas(parent) {}

void PcbCanvas::setSiStackup(const sikit::si::SiStackup* s) {
    si_stackup_ = s;
    rebuildMesh3D();
    update();
}

void PcbCanvas::setViewMode(ViewMode mode) {
    if (view_mode_ == mode) return;
    view_mode_ = mode;
    if (mode == ViewMode::D3 && board()) {
        // Re-fit the 3D camera against the current board bbox.
        bool have_any = false;
        double lo_x = 0, lo_y = 0, hi_x = 0, hi_y = 0;
        auto include = [&](double x, double y) {
            if (!have_any) {
                lo_x = hi_x = x;
                lo_y = hi_y = y;
                have_any = true;
                return;
            }
            if (x < lo_x) lo_x = x;
            if (x > hi_x) hi_x = x;
            if (y < lo_y) lo_y = y;
            if (y > hi_y) hi_y = y;
        };
        for (const auto& s : board()->segments) {
            include(s.start.x, s.start.y);
            include(s.end.x, s.end.y);
        }
        for (const auto& p : board()->pads) include(p.at.x, p.at.y);
        for (const auto& z : board()->zones)
            for (const auto& pt : z.outline.outline) include(pt.x, pt.y);
        if (have_any) {
            const double z_mid = 0.5 * board()->stackup.total_thickness;
            camera3d_.fit_to_bounds({lo_x, lo_y}, {hi_x, hi_y},
                                     z_mid, width(), height(), 0.15);
        }
    }
    update();
}

void PcbCanvas::clearImpedanceOverlay() {
    pending_overlay_verts_.clear();
    pending_overlay_indices_.clear();
    overlay_dirty_ = true;
    update();
}

void PcbCanvas::setImpedanceOverlay(
    const std::vector<sikit::analysis::SegmentImpedance>& results,
    double target_z0) {
    pending_overlay_verts_.clear();
    pending_overlay_indices_.clear();
    if (!board() || results.empty()) {
        overlay_dirty_ = true;
        update();
        return;
    }
    // Inflate each segment trace by 60% so the overlay reads as a
    // visible band on top of the original copper fill.
    constexpr double kInflateFactor = 1.6;
    const double inflate = kInflateFactor;

    for (const auto& r : results) {
        if (r.segment_index >= board()->segments.size()) continue;
        const auto& s = board()->segments[r.segment_index];
        const double dx  = s.end.x - s.start.x;
        const double dy  = s.end.y - s.start.y;
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0.0 || s.width <= 0.0) continue;
        const double nx = -dy / len;
        const double ny =  dx / len;
        const double hw = 0.5 * s.width * inflate;

        const float ax1 = static_cast<float>(s.start.x + nx * hw);
        const float ay1 = static_cast<float>(s.start.y + ny * hw);
        const float ax2 = static_cast<float>(s.start.x - nx * hw);
        const float ay2 = static_cast<float>(s.start.y - ny * hw);
        const float bx1 = static_cast<float>(s.end.x   - nx * hw);
        const float by1 = static_cast<float>(s.end.y   - ny * hw);
        const float bx2 = static_cast<float>(s.end.x   + nx * hw);
        const float by2 = static_cast<float>(s.end.y   + ny * hw);

        const auto c = sikit::analysis::color_for_error(r.z0, target_z0);
        const auto base =
            static_cast<std::uint32_t>(pending_overlay_verts_.size() / 6);
        auto push = [&](float x, float y) {
            pending_overlay_verts_.insert(pending_overlay_verts_.end(),
                                            {x, y, c.r, c.g, c.b, c.a});
        };
        push(ax1, ay1); push(ax2, ay2);
        push(bx1, by1); push(bx2, by2);
        pending_overlay_indices_.insert(pending_overlay_indices_.end(),
            {base + 0, base + 1, base + 2,
             base + 0, base + 2, base + 3});
    }
    overlay_dirty_ = true;
    update();
}

void PcbCanvas::initializeGLOverlays() {
    vcol_prog_.addShaderFromSourceCode(QOpenGLShader::Vertex,   kVcolVertSrc);
    vcol_prog_.addShaderFromSourceCode(QOpenGLShader::Fragment, kVcolFragSrc);
    vcol_prog_.link();

    lit_prog_.addShaderFromSourceCode(QOpenGLShader::Vertex,   kLitVertSrc);
    lit_prog_.addShaderFromSourceCode(QOpenGLShader::Fragment, kLitFragSrc);
    lit_prog_.link();

    overlay_vao_.create();
    overlay_vbo_.create();
    overlay_ibo_.create();
    overlay_vao_.bind();
    overlay_vbo_.bind();
    overlay_ibo_.bind();
    vcol_prog_.enableAttributeArray(0);
    vcol_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2, 6 * sizeof(float));
    vcol_prog_.enableAttributeArray(1);
    vcol_prog_.setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 4,
                                   6 * sizeof(float));
    overlay_vao_.release();
    overlay_vbo_.release();
    overlay_ibo_.release();

    auto init_3d_mesh = [&](GpuMesh3D& m) {
        m.vao.create();
        m.vbo.create();
        m.ibo.create();
        m.vao.bind();
        m.vbo.bind();
        m.ibo.bind();
        const int stride = 10 * sizeof(float);
        lit_prog_.enableAttributeArray(0);
        lit_prog_.setAttributeBuffer(0, GL_FLOAT, 0,                 3, stride);
        lit_prog_.enableAttributeArray(1);
        lit_prog_.setAttributeBuffer(1, GL_FLOAT, 3 * sizeof(float), 3, stride);
        lit_prog_.enableAttributeArray(2);
        lit_prog_.setAttributeBuffer(2, GL_FLOAT, 6 * sizeof(float), 4, stride);
        m.vao.release();
        m.vbo.release();
        m.ibo.release();
    };
    init_3d_mesh(mesh3d_dielectric_);
    init_3d_mesh(mesh3d_copper_);
    init_3d_mesh(mesh3d_vias_);
}

void PcbCanvas::uploadOverlay() {
    overlay_index_count_ =
        static_cast<int>(pending_overlay_indices_.size());
    overlay_vao_.bind();
    overlay_vbo_.bind();
    overlay_vbo_.allocate(pending_overlay_verts_.data(),
                           static_cast<int>(pending_overlay_verts_.size() *
                                              sizeof(float)));
    overlay_ibo_.bind();
    overlay_ibo_.allocate(pending_overlay_indices_.data(),
                           static_cast<int>(pending_overlay_indices_.size() *
                                              sizeof(std::uint32_t)));
    vcol_prog_.enableAttributeArray(0);
    vcol_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2, 6 * sizeof(float));
    vcol_prog_.enableAttributeArray(1);
    vcol_prog_.setAttributeBuffer(1, GL_FLOAT, 2 * sizeof(float), 4,
                                   6 * sizeof(float));
    overlay_vao_.release();
    overlay_vbo_.release();
    overlay_ibo_.release();
    overlay_dirty_ = false;
}

void PcbCanvas::rebuildMesh3D() {
    if (!board()) {
        pending_mesh3d_ = {};
        mesh3d_dirty_ = true;
        return;
    }
    pending_mesh3d_ = sikit::render::build_board_mesh_3d(
        *board(), si_stackup_ ? *si_stackup_ : sikit::si::SiStackup{});
    mesh3d_dirty_ = true;
}

void PcbCanvas::onBoardChanged() {
    clearImpedanceOverlay();
    rebuildMesh3D();
}

namespace {
void upload_one_3d(QOpenGLBuffer& vbo, QOpenGLBuffer& ibo,
                    int& index_count,
                    const sikit::render::Mesh3D& mesh) {
    vbo.bind();
    vbo.allocate(mesh.vertices.data(),
                  static_cast<int>(mesh.vertices.size() * sizeof(float)));
    ibo.bind();
    ibo.allocate(mesh.indices.data(),
                  static_cast<int>(mesh.indices.size() *
                                    sizeof(std::uint32_t)));
    index_count = static_cast<int>(mesh.indices.size());
    vbo.release();
    ibo.release();
}
}  // namespace

void PcbCanvas::paintOverlays2D() {
    if (overlay_dirty_) uploadOverlay();
    if (overlay_index_count_ <= 0) return;
    const QMatrix4x4 proj = ortho_matrix();
    vcol_prog_.bind();
    vcol_prog_.setUniformValue("u_proj", proj);
    overlay_vao_.bind();
    glDrawElements(GL_TRIANGLES, overlay_index_count_,
                    GL_UNSIGNED_INT, nullptr);
    overlay_vao_.release();
    vcol_prog_.release();
}

void PcbCanvas::paintGL() {
    if (view_mode_ == ViewMode::D2) {
        // 2D: delegate everything to the base. paintOverlays2D fires
        // back into us for the impedance overlay.
        circuitcore::ui::PcbCanvas::paintGL();
        return;
    }
    // 3D path -- hand-rolled. Re-upload mesh if needed.
    if (mesh3d_dirty_) {
        mesh3d_dirty_ = false;
        upload_one_3d(mesh3d_dielectric_.vbo, mesh3d_dielectric_.ibo,
                       mesh3d_dielectric_.index_count,
                       pending_mesh3d_.dielectric);
        upload_one_3d(mesh3d_copper_.vbo, mesh3d_copper_.ibo,
                       mesh3d_copper_.index_count,
                       pending_mesh3d_.copper);
        upload_one_3d(mesh3d_vias_.vbo, mesh3d_vias_.ibo,
                       mesh3d_vias_.index_count,
                       pending_mesh3d_.vias);
    }
    glClearColor(0.06f, 0.07f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glEnable(GL_DEPTH_TEST);

    const auto vp = camera3d_.view_projection(width(), height());
    const QMatrix4x4 vpm(
        vp[0], vp[4], vp[8],  vp[12],
        vp[1], vp[5], vp[9],  vp[13],
        vp[2], vp[6], vp[10], vp[14],
        vp[3], vp[7], vp[11], vp[15]);

    lit_prog_.bind();
    lit_prog_.setUniformValue("u_vp", vpm);
    lit_prog_.setUniformValue("u_light_dir",
                                QVector3D(0.3f, -0.5f, 0.8f).normalized());

    glDisable(GL_BLEND);
    if (mesh3d_copper_.index_count > 0) {
        mesh3d_copper_.vao.bind();
        glDrawElements(GL_TRIANGLES, mesh3d_copper_.index_count,
                        GL_UNSIGNED_INT, nullptr);
        mesh3d_copper_.vao.release();
    }
    if (mesh3d_vias_.index_count > 0) {
        mesh3d_vias_.vao.bind();
        glDrawElements(GL_TRIANGLES, mesh3d_vias_.index_count,
                        GL_UNSIGNED_INT, nullptr);
        mesh3d_vias_.vao.release();
    }
    if (mesh3d_dielectric_.index_count > 0) {
        glEnable(GL_BLEND);
        glDepthMask(GL_FALSE);
        mesh3d_dielectric_.vao.bind();
        glDrawElements(GL_TRIANGLES, mesh3d_dielectric_.index_count,
                        GL_UNSIGNED_INT, nullptr);
        mesh3d_dielectric_.vao.release();
        glDepthMask(GL_TRUE);
    }
    lit_prog_.release();
    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
}

void PcbCanvas::mousePressEvent(QMouseEvent* e) {
    if (view_mode_ == ViewMode::D3) {
        if (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton) {
            panning_3d_ = true;
            last_mouse_3d_ = e->pos();
        }
        return;
    }
    circuitcore::ui::PcbCanvas::mousePressEvent(e);
}

void PcbCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (view_mode_ == ViewMode::D3) {
        if (panning_3d_) {
            const QPoint d = e->pos() - last_mouse_3d_;
            if (e->buttons() & Qt::MiddleButton) {
                camera3d_.pan_pixels(d.x(), d.y(), height());
            } else {
                camera3d_.orbit_pixels(d.x(), d.y(), height());
            }
            last_mouse_3d_ = e->pos();
            update();
        }
        // 3D hover-pick not implemented yet -- keep status bar empty
        // rather than reporting a meaningless 2D hit-test.
        emit hoverInfo(QString());
        return;
    }
    circuitcore::ui::PcbCanvas::mouseMoveEvent(e);
}

void PcbCanvas::mouseReleaseEvent(QMouseEvent* e) {
    if (view_mode_ == ViewMode::D3) {
        if (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton) {
            panning_3d_ = false;
        }
        return;
    }
    circuitcore::ui::PcbCanvas::mouseReleaseEvent(e);
}

void PcbCanvas::wheelEvent(QWheelEvent* e) {
    if (view_mode_ == ViewMode::D3) {
        const double factor = (e->angleDelta().y() > 0) ? 1.20 : (1.0 / 1.20);
        camera3d_.zoom(1.0 / factor);
        update();
        return;
    }
    circuitcore::ui::PcbCanvas::wheelEvent(e);
}


}  // namespace sikit
