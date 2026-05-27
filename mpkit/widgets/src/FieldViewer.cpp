#include "mpkit/widgets/FieldViewer.h"

#include <algorithm>
#include <cstdint>
#include <limits>

#include <QVBoxLayout>

#include <vtkActor.h>
#include <vtkAxesActor.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkColorTransferFunction.h>
#include <vtkCubeAxesActor.h>
#include <vtkDoubleArray.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkImageMapToColors.h>
#include <vtkImageSlice.h>
#include <vtkImageSliceMapper.h>
#include <vtkLookupTable.h>
#include <vtkNew.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkOutlineFilter.h>
#include <vtkPointData.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkScalarBarActor.h>
#include <vtkSmartPointer.h>

#include "render/Mesher3D.h"
#include "si/SiStackup.h"

namespace mpkit::widgets {

namespace {

// Standard coolwarm / viridis / jet / grayscale presets.
void apply_coolwarm(vtkColorTransferFunction* lut,
                     double vmin, double vmax) {
    lut->RemoveAllPoints();
    const double r = vmax - vmin;
    auto add = [&](double t, double rr, double gg, double bb) {
        lut->AddRGBPoint(vmin + t * r, rr, gg, bb);
    };
    add(0.000, 0.231, 0.298, 0.753);
    add(0.166, 0.404, 0.500, 0.882);
    add(0.333, 0.612, 0.706, 0.957);
    add(0.500, 0.866, 0.866, 0.866);
    add(0.666, 0.957, 0.706, 0.620);
    add(0.833, 0.882, 0.461, 0.388);
    add(1.000, 0.706, 0.016, 0.149);
}
void apply_viridis(vtkColorTransferFunction* lut,
                    double vmin, double vmax) {
    lut->RemoveAllPoints();
    const double r = vmax - vmin;
    auto add = [&](double t, double rr, double gg, double bb) {
        lut->AddRGBPoint(vmin + t * r, rr, gg, bb);
    };
    add(0.00, 0.267, 0.005, 0.329);
    add(0.25, 0.231, 0.318, 0.545);
    add(0.50, 0.128, 0.566, 0.551);
    add(0.75, 0.369, 0.789, 0.382);
    add(1.00, 0.993, 0.906, 0.144);
}
void apply_jet(vtkColorTransferFunction* lut,
                double vmin, double vmax) {
    lut->RemoveAllPoints();
    const double r = vmax - vmin;
    auto add = [&](double t, double rr, double gg, double bb) {
        lut->AddRGBPoint(vmin + t * r, rr, gg, bb);
    };
    add(0.000, 0.000, 0.000, 0.502);
    add(0.125, 0.000, 0.000, 1.000);
    add(0.375, 0.000, 1.000, 1.000);
    add(0.625, 1.000, 1.000, 0.000);
    add(0.875, 1.000, 0.000, 0.000);
    add(1.000, 0.502, 0.000, 0.000);
}
void apply_grayscale(vtkColorTransferFunction* lut,
                      double vmin, double vmax) {
    lut->RemoveAllPoints();
    lut->AddRGBPoint(vmin, 0.0, 0.0, 0.0);
    lut->AddRGBPoint(vmax, 1.0, 1.0, 1.0);
}

// Convert one of sikit::render::Mesher3D's interleaved vertex+index
// buffers into a vtkPolyData. Vertex layout is 10 floats:
// (x, y, z, nx, ny, nz, r, g, b, a). We only need position + topology;
// colors come from materials / scalars later.
vtkSmartPointer<vtkPolyData> mesh3d_to_polydata(
    const sikit::render::Mesh3D& m) {
    auto pd = vtkSmartPointer<vtkPolyData>::New();
    if (m.indices.empty() || m.vertices.empty()) return pd;
    constexpr std::size_t kStride = 10;
    const std::size_t n_verts = m.vertices.size() / kStride;

    auto points = vtkSmartPointer<vtkPoints>::New();
    points->SetNumberOfPoints(static_cast<vtkIdType>(n_verts));
    for (std::size_t i = 0; i < n_verts; ++i) {
        points->SetPoint(static_cast<vtkIdType>(i),
                          m.vertices[kStride * i + 0],
                          m.vertices[kStride * i + 1],
                          m.vertices[kStride * i + 2]);
    }
    pd->SetPoints(points);

    auto cells = vtkSmartPointer<vtkCellArray>::New();
    for (std::size_t i = 0; i + 2 < m.indices.size(); i += 3) {
        vtkIdType tri[3] = {
            static_cast<vtkIdType>(m.indices[i]),
            static_cast<vtkIdType>(m.indices[i + 1]),
            static_cast<vtkIdType>(m.indices[i + 2])
        };
        cells->InsertNextCell(3, tri);
    }
    pd->SetPolys(cells);
    return pd;
}

}  // namespace

struct FieldViewer::Impl {
    vtkNew<vtkGenericOpenGLRenderWindow>     render_window;
    vtkNew<vtkRenderer>                      renderer;
    vtkNew<vtkColorTransferFunction>         lut;
    vtkNew<vtkScalarBarActor>                color_bar;
    vtkNew<vtkAxesActor>                     axes;
    vtkSmartPointer<vtkOrientationMarkerWidget> axes_marker;

    // Slice-plane mode (mode 2)
    vtkSmartPointer<vtkImageData>            image;
    vtkSmartPointer<vtkActor>                outline_actor;
    vtkSmartPointer<vtkImageSlice>           slice_actor;

    // Board-geometry mode (mode 1)
    vtkSmartPointer<vtkActor>                diel_actor;
    vtkSmartPointer<vtkActor>                copper_actor;
    vtkSmartPointer<vtkActor>                vias_actor;
    vtkSmartPointer<vtkPolyData>             copper_pd;   // kept to re-scalar

    QString colormap_name = "coolwarm";
    bool    user_range    = false;
    double  v_min         = 0.0;
    double  v_max         = 1.0;

    int slice_axis  = 2;
    int slice_index = 0;

    Impl() {
        renderer->SetBackground(0.16, 0.16, 0.18);
        renderer->SetBackground2(0.04, 0.04, 0.06);
        renderer->SetGradientBackground(true);
        render_window->AddRenderer(renderer);

        color_bar->SetLookupTable(lut);
        color_bar->SetTitle("value");
        color_bar->SetNumberOfLabels(5);
        color_bar->SetOrientationToVertical();
        color_bar->SetWidth(0.06);
        color_bar->SetHeight(0.6);
        color_bar->SetPosition(0.92, 0.2);
        renderer->AddActor2D(color_bar);

        axes->SetTotalLength(1.0, 1.0, 1.0);
        axes->SetShaftType(0);
        axes->SetAxisLabels(1);
    }

    void reapply_lut() {
        if (colormap_name == "viridis")        apply_viridis  (lut, v_min, v_max);
        else if (colormap_name == "jet")       apply_jet      (lut, v_min, v_max);
        else if (colormap_name == "grayscale") apply_grayscale(lut, v_min, v_max);
        else                                   apply_coolwarm (lut, v_min, v_max);
    }
};

FieldViewer::FieldViewer(QWidget* parent)
    : QVTKOpenGLNativeWidget(parent),
      impl_(std::make_unique<Impl>()) {
    setRenderWindow(impl_->render_window);
    apply_coolwarm(impl_->lut, 0.0, 1.0);

    impl_->axes_marker = vtkSmartPointer<vtkOrientationMarkerWidget>::New();
    impl_->axes_marker->SetOrientationMarker(impl_->axes);
    impl_->axes_marker->SetInteractor(impl_->render_window->GetInteractor());
    impl_->axes_marker->SetViewport(0.0, 0.0, 0.18, 0.22);
    impl_->axes_marker->SetEnabled(1);
    impl_->axes_marker->InteractiveOff();

    impl_->renderer->ResetCamera();
}

FieldViewer::~FieldViewer() = default;

// ---- mode 1: board geometry + field on copper --------------------------

void FieldViewer::setBoard(const circuitcore::board::Board& board) {
    // Remove any previously-added geometry actors.
    auto drop = [&](vtkSmartPointer<vtkActor>& a) {
        if (a) { impl_->renderer->RemoveActor(a); a = nullptr; }
    };
    drop(impl_->diel_actor);
    drop(impl_->copper_actor);
    drop(impl_->vias_actor);
    impl_->copper_pd = nullptr;

    sikit::si::SiStackup empty_stack;
    auto bm = sikit::render::build_board_mesh_3d(board, empty_stack);

    auto add_actor = [&](const sikit::render::Mesh3D& m,
                          double r, double g, double b, double opacity)
                          -> vtkSmartPointer<vtkActor> {
        auto pd = mesh3d_to_polydata(m);
        if (pd->GetNumberOfPoints() == 0) return nullptr;
        auto mapper = vtkSmartPointer<vtkPolyDataMapper>::New();
        mapper->SetInputData(pd);
        mapper->ScalarVisibilityOff();
        auto actor = vtkSmartPointer<vtkActor>::New();
        actor->SetMapper(mapper);
        actor->GetProperty()->SetColor(r, g, b);
        actor->GetProperty()->SetOpacity(opacity);
        actor->GetProperty()->SetAmbient(0.18);
        actor->GetProperty()->SetDiffuse(0.78);
        actor->GetProperty()->SetSpecular(0.10);
        impl_->renderer->AddActor(actor);
        return actor;
    };
    // Translucent FR-4 amber so the slice plane + copper inside remain
    // visible.
    impl_->diel_actor   = add_actor(bm.dielectric, 0.55, 0.50, 0.18, 0.28);
    impl_->copper_actor = add_actor(bm.copper,     0.72, 0.45, 0.20, 1.00);
    impl_->vias_actor   = add_actor(bm.vias,       0.55, 0.35, 0.15, 1.00);

    if (impl_->copper_actor) {
        impl_->copper_pd = vtkPolyData::SafeDownCast(
            impl_->copper_actor->GetMapper()->GetInput());
    }
    impl_->renderer->ResetCamera();
    impl_->render_window->Render();
}

void FieldViewer::setFieldOnCopperSurface(
    const Grid& grid,
    const circuitcore::field::Field3D& field,
    const QString& label) {
    if (!impl_->copper_actor || !impl_->copper_pd) return;
    if (field.size() !=
        static_cast<std::size_t>(grid.nx()) * grid.ny() * grid.nz()) {
        return;
    }
    const vtkIdType n = impl_->copper_pd->GetNumberOfPoints();
    auto scalars = vtkSmartPointer<vtkDoubleArray>::New();
    scalars->SetName(label.toUtf8().constData());
    scalars->SetNumberOfValues(n);

    double vmin =  std::numeric_limits<double>::infinity();
    double vmax = -std::numeric_limits<double>::infinity();
    for (vtkIdType i = 0; i < n; ++i) {
        double p[3];
        impl_->copper_pd->GetPoint(i, p);
        // Nearest-cell sample. World -> voxel index via the grid;
        // clamp out-of-bounds points to the nearest interior voxel so
        // they still get a colour rather than the default value.
        auto idx = grid.world_to_index(p[0], p[1], p[2]);
        int ii = (idx[0] < 0) ? 0 : std::min(idx[0], grid.nx() - 1);
        int jj = (idx[1] < 0) ? 0 : std::min(idx[1], grid.ny() - 1);
        int kk = (idx[2] < 0) ? 0 : std::min(idx[2], grid.nz() - 1);
        const double v = field.at(ii, jj, kk);
        scalars->SetValue(i, v);
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }
    impl_->copper_pd->GetPointData()->SetScalars(scalars);

    if (!impl_->user_range) {
        if (!(vmin < vmax)) { vmin = 0.0; vmax = 1.0; }
        impl_->v_min = vmin;
        impl_->v_max = vmax;
        impl_->reapply_lut();
    }
    auto* mapper = vtkPolyDataMapper::SafeDownCast(
        impl_->copper_actor->GetMapper());
    if (mapper) {
        mapper->ScalarVisibilityOn();
        mapper->SetLookupTable(impl_->lut);
        mapper->SetScalarRange(impl_->v_min, impl_->v_max);
        mapper->SetColorModeToMapScalars();
    }
    impl_->color_bar->SetTitle(label.toUtf8().constData());
    impl_->render_window->Render();
}

// ---- mode 2: volumetric slice (unchanged) ------------------------------

void FieldViewer::setField(const Grid& grid,
                            const circuitcore::field::Field3D& field,
                            const QString& field_label) {
    if (field.size() !=
        static_cast<std::size_t>(grid.nx()) * grid.ny() * grid.nz()) {
        return;
    }
    auto image = vtkSmartPointer<vtkImageData>::New();
    image->SetDimensions(grid.nx(), grid.ny(), grid.nz());
    image->SetSpacing(grid.dx(), grid.dy(), grid.dz());
    image->SetOrigin(grid.x0, grid.y0, grid.z0);
    image->AllocateScalars(VTK_DOUBLE, 1);
    auto* dst = static_cast<double*>(image->GetScalarPointer());
    std::copy(field.data(), field.data() + field.size(), dst);
    impl_->image = image;

    if (!impl_->user_range) {
        double vmin =  std::numeric_limits<double>::infinity();
        double vmax = -std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < field.size(); ++i) {
            const double v = field.data()[i];
            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
        }
        if (!(vmin < vmax)) { vmin = 0.0; vmax = 1.0; }
        impl_->v_min = vmin;
        impl_->v_max = vmax;
        impl_->reapply_lut();
    }
    impl_->color_bar->SetTitle(field_label.toUtf8().constData());

    if (!impl_->outline_actor) {
        impl_->outline_actor = vtkSmartPointer<vtkActor>::New();
        impl_->renderer->AddActor(impl_->outline_actor);
    }
    vtkNew<vtkOutlineFilter> outline;
    outline->SetInputData(impl_->image);
    vtkNew<vtkPolyDataMapper> ol_mapper;
    ol_mapper->SetInputConnection(outline->GetOutputPort());
    impl_->outline_actor->SetMapper(ol_mapper);
    impl_->outline_actor->GetProperty()->SetColor(0.9, 0.9, 0.9);
    impl_->outline_actor->GetProperty()->SetLineWidth(1.5);

    if (!impl_->slice_actor) {
        impl_->slice_actor = vtkSmartPointer<vtkImageSlice>::New();
        impl_->renderer->AddActor(impl_->slice_actor);
    }
    impl_->slice_index = grid.nz() / 2;
    setSlice(impl_->slice_axis, impl_->slice_index);

    impl_->renderer->ResetCamera();
    impl_->render_window->Render();
}

void FieldViewer::setSliceVisible(bool visible) {
    if (impl_->slice_actor)
        impl_->slice_actor->SetVisibility(visible ? 1 : 0);
    if (impl_->outline_actor)
        impl_->outline_actor->SetVisibility(visible ? 1 : 0);
    impl_->render_window->Render();
}

// ---- shared --------------------------------------------------------------

void FieldViewer::setColormapRange(double v_min, double v_max) {
    impl_->user_range = true;
    impl_->v_min = v_min;
    impl_->v_max = v_max;
    impl_->reapply_lut();
    if (auto* mapper = (impl_->copper_actor
                            ? vtkPolyDataMapper::SafeDownCast(
                                  impl_->copper_actor->GetMapper())
                            : nullptr)) {
        mapper->SetScalarRange(v_min, v_max);
    }
    impl_->render_window->Render();
}

void FieldViewer::setColormap(const QString& name) {
    impl_->colormap_name = name;
    impl_->reapply_lut();
    if (impl_->image) impl_->render_window->Render();
    else if (impl_->copper_actor) impl_->render_window->Render();
}

void FieldViewer::setAxesVisible(bool visible) {
    if (impl_->axes_marker)
        impl_->axes_marker->SetEnabled(visible ? 1 : 0);
    impl_->render_window->Render();
}

void FieldViewer::setColorBarVisible(bool visible) {
    impl_->color_bar->SetVisibility(visible ? 1 : 0);
    impl_->render_window->Render();
}

void FieldViewer::setOutlineVisible(bool visible) {
    if (impl_->outline_actor)
        impl_->outline_actor->SetVisibility(visible ? 1 : 0);
    impl_->render_window->Render();
}

void FieldViewer::setSlice(int axis, int index) {
    if (!impl_->image || !impl_->slice_actor) return;
    impl_->slice_axis  = std::clamp(axis, 0, 2);
    int extent[6];
    impl_->image->GetExtent(extent);
    const int axis_size = extent[2 * impl_->slice_axis + 1]
                        - extent[2 * impl_->slice_axis + 0] + 1;
    impl_->slice_index = std::clamp(index, 0, axis_size - 1);

    vtkNew<vtkImageMapToColors> colorize;
    colorize->SetInputData(impl_->image);
    colorize->SetLookupTable(impl_->lut);
    colorize->SetOutputFormatToRGBA();
    colorize->Update();

    vtkNew<vtkImageSliceMapper> mapper;
    mapper->SetInputConnection(colorize->GetOutputPort());
    mapper->SetOrientation(impl_->slice_axis);
    mapper->SetSliceNumber(impl_->slice_index);
    impl_->slice_actor->SetMapper(mapper);
    impl_->render_window->Render();
}

void FieldViewer::setParallelProjection(bool parallel) {
    impl_->renderer->GetActiveCamera()->SetParallelProjection(parallel ? 1 : 0);
    impl_->render_window->Render();
}

void FieldViewer::resetCamera() {
    impl_->renderer->ResetCamera();
    impl_->render_window->Render();
}

}  // namespace mpkit::widgets
