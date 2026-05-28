// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (c) 2026 Charles Kennedy <https://github.com/UnsignedChad>
#include "circuitcore/ui/PcbCanvas.h"

#include <algorithm>
#include <cstdint>
#include <vector>

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QPainter>
#include <QSettings>
#include <QString>
#include <QVector4D>
#include <QWheelEvent>
#include <utility>

#include "circuitcore/board/HitTest.h"
#include "circuitcore/ui/GraphicsMesher.h"
#include "circuitcore/ui/LayerColors.h"
#include "circuitcore/ui/SegmentMesher.h"
#include "circuitcore/ui/ViaMesher.h"
#include "circuitcore/ui/ZoneMesher.h"

namespace circuitcore::ui {

namespace {

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

// Order layers so the front copper (0) ends up on top and back (31) at
// the bottom, with inner layers stacked by descending ordinal in
// between. This is purely a paint-order choice -- the depth test is
// disabled in 2D mode.
int render_priority(int ord) {
    if (ord == kDrillOrdinal) return 9999;  // drill holes on top of all copper
    if (ord == 0)  return 1000;
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

PcbCanvas::~PcbCanvas() = default;

void PcbCanvas::setBoard(const board::Board* board) {
    board_ = board;
    pending_meshes_.clear();
    layer_visible_.clear();
    if (board_) {
        auto meshes = build_board_meshes(*board_);
        pending_meshes_.clear();
        pending_meshes_.reserve(meshes.zones.size() + meshes.tracks.size());
        // Zones first so they sort before tracks within a layer at the
        // same render_priority -- the paint loop draws zones with a
        // reduced-alpha tint, then tracks opaque on top.
        for (auto& m : meshes.zones)  pending_meshes_.push_back(std::move(m));
        for (auto& m : meshes.tracks) pending_meshes_.push_back(std::move(m));
        meshes_dirty_ = true;
        graphics_dirty_ = true;
        if (isValid()) {
            // initializeGL has already run -- safe to refresh outline now.
            makeCurrent();
            buildOutline();
            doneCurrent();
        }
        // If the canvas hasn't been laid out yet (eg this tab is
        // still hidden), width()/height() return Qt's pre-layout
        // defaults and the fit picks a wrong scale. Defer to the
        // first paint with a real viewport.
        if (width() > 50 && height() > 50) {
            fitToBoard();
        } else {
            fit_pending_ = true;
        }
    }
    onBoardChanged();
    update();
}

void PcbCanvas::setLayerVisibility(int ordinal, bool visible) {
    layer_visible_[ordinal] = visible;
    update();
}

void PcbCanvas::setSilkVisible(bool visible) {
    if (silk_visible_ == visible) return;
    silk_visible_ = visible;
    update();
}

void PcbCanvas::fitToBoard() {
    if (!board_) return;
    bool have_any = false;
    double lo_x = 0, lo_y = 0, hi_x = 0, hi_y = 0;
    auto include = [&](double x, double y) {
        if (!have_any) {
            lo_x = hi_x = x; lo_y = hi_y = y; have_any = true;
        } else {
            if (x < lo_x) lo_x = x;
            if (x > hi_x) hi_x = x;
            if (y < lo_y) lo_y = y;
            if (y > hi_y) hi_y = y;
        }
    };
    for (const auto& s : board_->segments) {
        include(s.start.x, s.start.y);
        include(s.end.x,   s.end.y);
    }
    for (const auto& p : board_->pads) {
        const double hw = 0.5 * p.size.x;
        const double hh = 0.5 * p.size.y;
        include(p.at.x - hw, p.at.y - hh);
        include(p.at.x + hw, p.at.y + hh);
    }
    for (const auto& v : board_->vias) {
        const double r = 0.5 * v.outer_diameter;
        include(v.at.x - r, v.at.y - r);
        include(v.at.x + r, v.at.y + r);
    }
    for (const auto& seg : board_->outline) {
        include(seg.start.x, seg.start.y);
        include(seg.end.x,   seg.end.y);
    }
    for (const auto& g : board_->graphics) {
        for (const auto& pt : g.points) include(pt.x, pt.y);
    }
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

void PcbCanvas::saveSettings(QSettings& settings) const {
    settings.setValue("canvas/center_x", camera_.center.x);
    settings.setValue("canvas/center_y", camera_.center.y);
    settings.setValue("canvas/pixels_per_meter", camera_.pixels_per_meter);
}

void PcbCanvas::restoreSettings(QSettings& settings) {
    bool ok_x = false, ok_y = false, ok_z = false;
    const double cx  = settings.value("canvas/center_x").toDouble(&ok_x);
    const double cy  = settings.value("canvas/center_y").toDouble(&ok_y);
    const double ppm = settings.value("canvas/pixels_per_meter").toDouble(&ok_z);
    if (ok_x && ok_y && ok_z && ppm > 0.0) {
        camera_.center           = {cx, cy};
        camera_.pixels_per_meter = ppm;
        update();
    }
}

QMatrix4x4 PcbCanvas::ortho_matrix() const {
    const auto m = camera_.ortho_matrix(width(), height());
    return QMatrix4x4(
        m[0], m[4], m[8],  m[12],
        m[1], m[5], m[9],  m[13],
        m[2], m[6], m[10], m[14],
        m[3], m[7], m[11], m[15]);
}

void PcbCanvas::initializeGL() {
    initializeOpenGLFunctions();
    glClearColor(0.10f, 0.10f, 0.12f, 1.0f);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    flat_prog_.addShaderFromSourceCode(QOpenGLShader::Vertex,   kFlatVertSrc);
    flat_prog_.addShaderFromSourceCode(QOpenGLShader::Fragment, kFlatFragSrc);
    flat_prog_.link();

    grid_vao_.create();
    grid_vbo_.create();
    buildGrid();

    outline_vao_.create();
    outline_vbo_.create();
    buildOutline();

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

    auto bind_flat = [&](QOpenGLVertexArrayObject& vao,
                          QOpenGLBuffer& vbo, QOpenGLBuffer& ibo) {
        vao.create(); vbo.create(); ibo.create();
        vao.bind(); vbo.bind(); ibo.bind();
        flat_prog_.enableAttributeArray(0);
        flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
        vao.release(); vbo.release(); ibo.release();
    };
    bind_flat(silk_vao_, silk_vbo_, silk_ibo_);
    bind_flat(mask_vao_, mask_vbo_, mask_ibo_);
    bind_flat(cyd_vao_,  cyd_vbo_,  cyd_ibo_);

    initializeGLOverlays();
}

void PcbCanvas::buildGrid() {
    std::vector<float> verts;
    const float lo = -0.5f, hi = 0.5f;
    const float step = 0.010f;  // 10 mm
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
        r.is_zone = m.is_zone;
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

void PcbCanvas::uploadGraphics() {
    if (!board_) {
        silk_index_count_ = mask_index_count_ = cyd_index_count_ = 0;
        pending_text_.clear();
        graphics_dirty_ = false;
        return;
    }
    auto bundle = GraphicsMesher::build(*board_);
    auto upload = [&](LayerMesh& src, QOpenGLBuffer& vbo,
                       QOpenGLBuffer& ibo, QOpenGLVertexArrayObject& vao,
                       int& out_index_count) {
        out_index_count = static_cast<int>(src.indices.size());
        if (out_index_count == 0) return;
        vao.bind(); vbo.bind();
        vbo.allocate(src.vertices.data(),
                      static_cast<int>(src.vertices.size() * sizeof(float)));
        ibo.bind();
        ibo.allocate(src.indices.data(),
                      static_cast<int>(src.indices.size() *
                                        sizeof(std::uint32_t)));
        flat_prog_.enableAttributeArray(0);
        flat_prog_.setAttributeBuffer(0, GL_FLOAT, 0, 2);
        vao.release(); vbo.release(); ibo.release();
    };
    upload(bundle.silk,      silk_vbo_, silk_ibo_, silk_vao_, silk_index_count_);
    upload(bundle.mask,      mask_vbo_, mask_ibo_, mask_vao_, mask_index_count_);
    upload(bundle.courtyard, cyd_vbo_,  cyd_ibo_,  cyd_vao_,  cyd_index_count_);
    pending_text_.clear();
    pending_text_.reserve(bundle.silk_text.size());
    for (auto& t : bundle.silk_text) {
        SilkText st;
        st.x = t.x; st.y = t.y;
        st.size_m = t.size; st.angle = t.angle;
        // KiCad layer-name convention: anything starting with "B." lives
        // on the back side (B.SilkS / B.Fab / B.Cu / ...). Mark it so
        // the renderer can mirror the glyphs when drawing the top view.
        if (const auto* L = board_->find_layer(t.layer_ordinal)) {
            st.mirrored = (L->name.rfind("B.", 0) == 0);
        }
        st.text = std::move(t.text);
        pending_text_.push_back(std::move(st));
    }
    graphics_dirty_ = false;
}

void PcbCanvas::resizeGL(int w, int h) {
    glViewport(0, 0, w, h);
}

void PcbCanvas::paintGL() {
    if (fit_pending_ && width() > 50 && height() > 50 && board_) {
        fit_pending_ = false;
        fitToBoard();
    }
    if (meshes_dirty_) uploadBoardMeshes();

    glClear(GL_COLOR_BUFFER_BIT);

    const QMatrix4x4 proj = ortho_matrix();

    flat_prog_.bind();
    flat_prog_.setUniformValue("u_proj", proj);

    // Grid
    flat_prog_.setUniformValue("u_color",
                                QVector4D(0.22f, 0.22f, 0.28f, 1.0f));
    grid_vao_.bind();
    glDrawArrays(GL_LINES, 0, grid_vertex_count_);
    grid_vao_.release();

    // Layer fills: two passes so zones render translucent and tracks /
    // pads / vias on the same layer stay readable on top of them. With
    // a single opaque pass, a GND pour on F.Cu obscures every F.Cu trace
    // running through it -- the board just reads as a solid red mass.
    auto draw_range = [&](const LayerRange& r) {
        glDrawElements(GL_TRIANGLES, r.index_count, GL_UNSIGNED_INT,
                        reinterpret_cast<const void*>(
                            static_cast<std::uintptr_t>(r.index_start *
                                                          sizeof(std::uint32_t))));
    };
    if (!layer_ranges_.empty()) {
        board_vao_.bind();
        // Pass 1: zone fills, alpha * 0.45 -- mimics KiCad's hatched pour.
        for (const auto& r : layer_ranges_) {
            if (!r.is_zone) continue;
            auto vis_it = layer_visible_.find(r.ordinal);
            const bool visible =
                (vis_it == layer_visible_.end()) || vis_it->second;
            if (!visible) continue;
            auto c = layer_color(r.ordinal);
            c[3] *= 0.45f;
            flat_prog_.setUniformValue("u_color", toQVec(c));
            draw_range(r);
        }
        // Pass 2: tracks / vias / pads at the layer's native alpha.
        for (const auto& r : layer_ranges_) {
            if (r.is_zone) continue;
            auto vis_it = layer_visible_.find(r.ordinal);
            const bool visible =
                (vis_it == layer_visible_.end()) || vis_it->second;
            if (!visible) continue;
            flat_prog_.setUniformValue("u_color",
                                        toQVec(layer_color(r.ordinal)));
            draw_range(r);
        }
        board_vao_.release();
    }

    // Outline (Edge.Cuts) -- bright yellow line on top of the fills.
    if (outline_vertex_count_ > 0) {
        flat_prog_.setUniformValue("u_color",
                                    QVector4D(0.86f, 0.78f, 0.32f, 1.0f));
        outline_vao_.bind();
        glDrawArrays(GL_LINES, 0, outline_vertex_count_);
        outline_vao_.release();
    }

    // Silk / mask / courtyard from the parser's Board::graphics.
    if (graphics_dirty_) { flat_prog_.release(); uploadGraphics();
                            flat_prog_.bind();
                            flat_prog_.setUniformValue("u_proj", proj); }
    // Mask: translucent green, under silk but on top of copper.
    if (mask_index_count_ > 0 && silk_visible_) {
        flat_prog_.setUniformValue("u_color",
                                    QVector4D(0.18f, 0.50f, 0.22f, 0.45f));
        mask_vao_.bind();
        glDrawElements(GL_TRIANGLES, mask_index_count_, GL_UNSIGNED_INT, nullptr);
        mask_vao_.release();
    }
    // Silk: opaque white-ish on top of everything (except courtyard).
    if (silk_index_count_ > 0 && silk_visible_) {
        flat_prog_.setUniformValue("u_color",
                                    QVector4D(0.92f, 0.92f, 0.92f, 1.0f));
        silk_vao_.bind();
        glDrawElements(GL_TRIANGLES, silk_index_count_, GL_UNSIGNED_INT, nullptr);
        silk_vao_.release();
    }
    // Courtyard: thin magenta -- usually a debug aid, drawn last.
    if (cyd_index_count_ > 0 && silk_visible_) {
        flat_prog_.setUniformValue("u_color",
                                    QVector4D(0.78f, 0.30f, 0.78f, 0.60f));
        cyd_vao_.bind();
        glDrawElements(GL_TRIANGLES, cyd_index_count_, GL_UNSIGNED_INT, nullptr);
        cyd_vao_.release();
    }

    flat_prog_.release();

    paintOverlays2D();

    // Silkscreen text via QPainter -- the GL pipeline doesn't rasterize
    // text. Only attempt this when we have something to draw to avoid
    // the QPainter setup cost on bare boards.
    if (!pending_text_.empty() && silk_visible_) {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::TextAntialiasing);
        painter.setPen(QColor(235, 235, 235));
        for (const auto& t : pending_text_) {
            double sx = 0, sy = 0;
            camera_.world_to_screen({t.x, t.y}, width(), height(), sx, sy);
            // Map text size from world metres to screen pixels via the
            // current zoom. Floor at 6 px so small labels are still
            // readable when zoomed out.
            const double px = std::max(6.0, t.size_m * camera_.pixels_per_meter);
            QFont f = painter.font();
            f.setPixelSize(static_cast<int>(px));
            painter.setFont(f);
            painter.save();
            painter.translate(sx, sy);
            // Back-side text gets a horizontal flip so the top-view shows
            // it mirrored -- matches KiCad's convention where B.SilkS
            // reads correctly only when you flip the board over to the
            // back. The flip happens before rotate so the rotation
            // direction inverts automatically with the mirror.
            if (t.mirrored) painter.scale(-1.0, 1.0);
            if (std::abs(t.angle) > 1e-6) {
                painter.rotate(-t.angle * 180.0 / 3.141592653589793);
            }
            painter.drawText(QPointF(-px * 0.5 * t.text.size() / 2.0, px * 0.5),
                              QString::fromStdString(t.text));
            painter.restore();
        }
    }
}

void PcbCanvas::mousePressEvent(QMouseEvent* e) {
    onMousePressOverlay(e);
    if (e->button() == Qt::MiddleButton || e->button() == Qt::LeftButton) {
        panning_ = true;
        last_mouse_ = e->pos();
    }
}

void PcbCanvas::mouseMoveEvent(QMouseEvent* e) {
    if (panning_) {
        const QPoint d = e->pos() - last_mouse_;
        camera_.pan_pixels(d.x(), d.y());
        last_mouse_ = e->pos();
        update();
    }
    {
        const auto world = camera_.screen_to_world(
            e->pos().x(), e->pos().y(), width(), height());
        emit hoverPos(world.x, world.y);
    }
    if (board_) {
        const auto world = camera_.screen_to_world(
            e->pos().x(), e->pos().y(), width(), height());
        const double tol = 4.0 / camera_.pixels_per_meter;
        const auto hit = board::hittest::at_point(*board_, world, tol);

        QString info;
        if (hit.kind != board::hittest::Hit::Kind::None) {
            const auto* net   = board_->find_net(hit.net_id);
            const auto* layer = board_->find_layer(hit.layer_ordinal);
            const QString net_name = (net && !net->name.empty())
                ? QString::fromStdString(net->name) : QString("(unnamed)");
            const QString layer_name = layer
                ? QString::fromStdString(layer->name) : QString("?");
            info = QString("%1   net %2 (%3)   layer %4")
                       .arg(board::hittest::name(hit.kind))
                       .arg(net_name).arg(hit.net_id).arg(layer_name);
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

}  // namespace circuitcore::ui
