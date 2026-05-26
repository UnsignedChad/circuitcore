#include "mpkit/widgets/FieldViewer.h"

#include <algorithm>
#include <limits>

#include <QVBoxLayout>

#include <vtkActor.h>
#include <vtkCamera.h>
#include <vtkAxesActor.h>
#include <vtkColorTransferFunction.h>
#include <vtkCubeAxesActor.h>
#include <vtkGenericOpenGLRenderWindow.h>
#include <vtkImageData.h>
#include <vtkImageMapToColors.h>
#include <vtkImageSlice.h>
#include <vtkImageSliceMapper.h>
#include <vtkLookupTable.h>
#include <vtkNew.h>
#include <vtkOrientationMarkerWidget.h>
#include <vtkOutlineFilter.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderWindow.h>
#include <vtkRenderWindowInteractor.h>
#include <vtkRenderer.h>
#include <vtkScalarBarActor.h>
#include <vtkSmartPointer.h>

namespace mpkit::widgets {

namespace {

// Standard coolwarm transfer-function preset (the COMSOL / ParaView
// default). 7 control points across 0..1 normalised input.
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

}  // namespace

struct FieldViewer::Impl {
    vtkNew<vtkGenericOpenGLRenderWindow>     render_window;
    vtkNew<vtkRenderer>                      renderer;
    vtkNew<vtkColorTransferFunction>         lut;
    vtkNew<vtkScalarBarActor>                color_bar;
    vtkNew<vtkAxesActor>                     axes;
    vtkSmartPointer<vtkOrientationMarkerWidget> axes_marker;
    vtkSmartPointer<vtkImageData>            image;
    vtkSmartPointer<vtkActor>                outline_actor;
    vtkSmartPointer<vtkImageSlice>           slice_actor;

    QString colormap_name = "coolwarm";
    bool    user_range    = false;
    double  v_min         = 0.0;
    double  v_max         = 1.0;

    int slice_axis  = 2;   // z by default
    int slice_index = 0;   // updated to mid-slice when a field arrives

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
};

FieldViewer::FieldViewer(QWidget* parent)
    : QVTKOpenGLNativeWidget(parent),
      impl_(std::make_unique<Impl>()) {
    setRenderWindow(impl_->render_window);
    apply_coolwarm(impl_->lut, 0.0, 1.0);

    // XYZ triad in the bottom-left, hooked into the QVTKWidget's
    // interactor so it rotates with the camera.
    impl_->axes_marker = vtkSmartPointer<vtkOrientationMarkerWidget>::New();
    impl_->axes_marker->SetOrientationMarker(impl_->axes);
    impl_->axes_marker->SetInteractor(impl_->render_window->GetInteractor());
    impl_->axes_marker->SetViewport(0.0, 0.0, 0.18, 0.22);
    impl_->axes_marker->SetEnabled(1);
    impl_->axes_marker->InteractiveOff();

    impl_->renderer->ResetCamera();
}

FieldViewer::~FieldViewer() = default;

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
    // Copy the field. vtkImageData stores i, j, k with i fastest -- same
    // order Field3D uses internally so this is one memcpy.
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
        setColormap(impl_->colormap_name);  // re-apply with new range
    }

    impl_->color_bar->SetTitle(field_label.toUtf8().constData());

    // Outline
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

    // Slice plane (axis-aligned, mid-z by default).
    if (!impl_->slice_actor) {
        impl_->slice_actor = vtkSmartPointer<vtkImageSlice>::New();
        impl_->renderer->AddActor(impl_->slice_actor);
    }
    impl_->slice_index = grid.nz() / 2;
    setSlice(impl_->slice_axis, impl_->slice_index);

    impl_->renderer->ResetCamera();
    impl_->render_window->Render();
}

void FieldViewer::setColormapRange(double v_min, double v_max) {
    impl_->user_range = true;
    impl_->v_min = v_min;
    impl_->v_max = v_max;
    setColormap(impl_->colormap_name);
    if (impl_->image) impl_->render_window->Render();
}

void FieldViewer::setColormap(const QString& name) {
    impl_->colormap_name = name;
    if (name == "viridis")        apply_viridis  (impl_->lut, impl_->v_min, impl_->v_max);
    else if (name == "jet")       apply_jet      (impl_->lut, impl_->v_min, impl_->v_max);
    else if (name == "grayscale") apply_grayscale(impl_->lut, impl_->v_min, impl_->v_max);
    else                          apply_coolwarm (impl_->lut, impl_->v_min, impl_->v_max);
    if (impl_->image) impl_->render_window->Render();
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
