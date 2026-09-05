#include "viewport.hpp"

#include <vtkActor.h>
#include <vtkCallbackCommand.h>
#include <vtkCamera.h>
#include <vtkCellArray.h>
#include <vtkCommand.h>
#include <vtkErrorCode.h>
#include <vtkInteractorStyleTrackballCamera.h>
#include <vtkNew.h>
#include <vtkPNGWriter.h>
#include <vtkPoints.h>
#include <vtkPolyData.h>
#include <vtkPolyDataMapper.h>
#include <vtkProperty.h>
#include <vtkRenderer.h>
#include <vtkSmartPointer.h>
#include <vtkUnsignedCharArray.h>
#include <vtkWin32OpenGLRenderWindow.h>
#include <vtkWin32RenderWindowInteractor.h>
#include <vtkWindowToImageFilter.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

#include "irop/error.hpp"
#include "irop/model/mesh_validation.hpp"

namespace irop::studio {
namespace {

constexpr std::uint64_t maximum_snapshot_pixels = 16'777'216;
constexpr vtkIdType maximum_snapshot_bytes = 256 * 1024 * 1024;

[[nodiscard]] vtkSmartPointer<vtkPolyData> make_poly_data(const TriangleMesh& mesh)
{
    auto result = vtkSmartPointer<vtkPolyData>::New();
    if (mesh.vertices.empty() && mesh.triangles.empty()) {
        return result;
    }

    // Structural validation is independent of prior loading. The viewer does
    // not require a combined result mesh to be a single connected surface.
    static_cast<void>(validate_and_measure_mesh(mesh, MeshLimits {}));
    constexpr auto maximum_id = static_cast<std::uint64_t>(std::numeric_limits<vtkIdType>::max());
    if (mesh.vertices.size() > maximum_id / 3 || mesh.triangles.size() > (maximum_id - 1) / 3) {
        throw Error(ErrorCategory::resource_limit, "scene geometry exceeds the VTK rendering index range");
    }

    vtkNew<vtkPoints> points;
    points->SetDataTypeToDouble();
    points->SetNumberOfPoints(static_cast<vtkIdType>(mesh.vertices.size()));
    for (std::size_t index = 0; index < mesh.vertices.size(); ++index) {
        const Point3& point = mesh.vertices[index];
        points->SetPoint(static_cast<vtkIdType>(index), point.x, point.y, point.z);
    }

    vtkNew<vtkCellArray> triangles;
    if (!triangles->AllocateEstimate(static_cast<vtkIdType>(mesh.triangles.size()), 3)) {
        throw Error(ErrorCategory::resource_limit, "cannot allocate scene triangle connectivity");
    }
    for (const Triangle& triangle : mesh.triangles) {
        const std::array<vtkIdType, 3> ids {
            static_cast<vtkIdType>(triangle[0]),
            static_cast<vtkIdType>(triangle[1]),
            static_cast<vtkIdType>(triangle[2]),
        };
        triangles->InsertNextCell(3, ids.data());
    }
    result->SetPoints(points);
    result->SetPolys(triangles);
    return result;
}

class SnapshotFile final {
public:
    explicit SnapshotFile(const std::filesystem::path& path) :
        handle_(CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                            nullptr))
    {
        if (handle_ == INVALID_HANDLE_VALUE) {
            throw Error(ErrorCategory::output_io, "cannot create a new snapshot file; choose a new PNG path");
        }
    }

    ~SnapshotFile() { static_cast<void>(CloseHandle(handle_)); }
    SnapshotFile(const SnapshotFile&) = delete;
    SnapshotFile& operator=(const SnapshotFile&) = delete;

    void write(const unsigned char* data, const vtkIdType size)
    {
        vtkIdType offset = 0;
        while (offset < size) {
            const auto chunk = static_cast<DWORD>(std::min<vtkIdType>(size - offset, 1024 * 1024));
            DWORD written = 0;
            if (!WriteFile(handle_, data + offset, chunk, &written, nullptr) || written == 0) {
                throw Error(ErrorCategory::output_io, "failed while writing the PNG snapshot");
            }
            offset += static_cast<vtkIdType>(written);
        }
        if (!FlushFileBuffers(handle_)) {
            throw Error(ErrorCategory::output_io, "failed while flushing the PNG snapshot");
        }
    }

private:
    HANDLE handle_;
};

}  // namespace

struct Viewport::Impl {
    bool rendering_error = false;
    bool have_objects = false;
    bool have_container = false;
    bool container_visible = true;
    bool suspended = false;
    vtkNew<vtkRenderer> renderer;
    vtkNew<vtkWin32OpenGLRenderWindow> window;
    vtkNew<vtkWin32RenderWindowInteractor> interactor;
    vtkNew<vtkInteractorStyleTrackballCamera> style;
    vtkNew<vtkPolyDataMapper> object_mapper;
    vtkNew<vtkPolyDataMapper> container_mapper;
    vtkNew<vtkActor> objects;
    vtkNew<vtkActor> container_surface;
    vtkNew<vtkActor> container_edges;
    vtkNew<vtkProperty> back_faces;
    vtkNew<vtkCallbackCommand> error_observer;
    vtkNew<vtkCallbackCommand> keyboard_filter;

    explicit Impl(const HWND parent)
    {
        if (!IsWindow(parent)) {
            throw Error(ErrorCategory::invalid_configuration, "the viewport requires a valid parent window");
        }
        error_observer->SetClientData(&rendering_error);
        error_observer->SetCallback([](vtkObject*, unsigned long, void* data, void*) {
            *static_cast<bool*>(data) = true;
        });
        window->AddObserver(vtkCommand::ErrorEvent, error_observer);
        renderer->AddObserver(vtkCommand::ErrorEvent, error_observer);
        object_mapper->AddObserver(vtkCommand::ErrorEvent, error_observer);
        container_mapper->AddObserver(vtkCommand::ErrorEvent, error_observer);

        renderer->SetBackground(0.055, 0.075, 0.10);
        renderer->SetBackground2(0.14, 0.19, 0.24);
        renderer->GradientBackgroundOn();
        renderer->LightFollowCameraOn();

        object_mapper->ScalarVisibilityOff();
        objects->SetMapper(object_mapper);
        objects->GetProperty()->SetColor(0.20, 0.73, 0.66);
        objects->GetProperty()->SetAmbient(0.20);
        objects->GetProperty()->SetDiffuse(0.75);
        objects->GetProperty()->SetSpecular(0.25);
        objects->GetProperty()->SetSpecularPower(24.0);
        objects->GetProperty()->SetLineWidth(1.0F);
        back_faces->SetColor(0.91, 0.69, 0.30);
        back_faces->SetAmbient(0.20);
        objects->SetBackfaceProperty(back_faces);
        objects->VisibilityOff();
        renderer->AddActor(objects);

        container_mapper->ScalarVisibilityOff();
        container_surface->SetMapper(container_mapper);
        container_surface->GetProperty()->SetColor(0.68, 0.77, 0.84);
        container_surface->GetProperty()->SetOpacity(0.055);
        container_surface->GetProperty()->LightingOff();
        container_surface->VisibilityOff();
        renderer->AddActor(container_surface);
        container_edges->SetMapper(container_mapper);
        container_edges->GetProperty()->SetRepresentationToWireframe();
        container_edges->GetProperty()->SetColor(0.70, 0.82, 0.88);
        container_edges->GetProperty()->SetOpacity(0.38);
        container_edges->GetProperty()->SetLineWidth(1.0F);
        container_edges->GetProperty()->LightingOff();
        container_edges->VisibilityOff();
        renderer->AddActor(container_edges);

        // VTK owns this child HWND and its message-procedure storage. The outer
        // application owns the message loop and never calls interactor.Start().
        window->SetParentId(parent);
        window->SetSize(640, 480);
        window->SetMultiSamples(0);
        window->SetAlphaBitPlanes(1);
        window->AddRenderer(renderer);
        style->SetDefaultRenderer(renderer);
        interactor->SetRenderWindow(window);
        interactor->SetInteractorStyle(style);
        // The application owns close/cancel/join. Consume VTK's standalone
        // exit shortcuts before the style invokes ExitCallback/TerminateApp;
        // every other character and all mouse gestures retain VTK behavior.
        keyboard_filter->SetClientData(keyboard_filter.GetPointer());
        keyboard_filter->SetCallback([](vtkObject* caller, unsigned long, void* data, void*) {
            auto* command = static_cast<vtkCallbackCommand*>(data);
            auto* input = vtkWin32RenderWindowInteractor::SafeDownCast(caller);
            if (input) {
                const char key = input->GetKeyCode();
                command->SetAbortFlag(key == 'q' || key == 'Q' || key == 'e' || key == 'E');
            }
        });
        interactor->AddObserver(vtkCommand::CharEvent, keyboard_filter, 1.0F);
        interactor->Initialize();
        if (!window->GetWindowId() || rendering_error) {
            shutdown();
            throw Error(ErrorCategory::dependency_failure, "cannot initialize the native OpenGL viewport");
        }
    }

    ~Impl() { shutdown(); }

    void shutdown() noexcept
    {
        interactor->Disable();
        interactor->SetRenderWindow(nullptr);
        window->Finalize();
    }

    void render()
    {
        if (suspended) {
            return;
        }
        window->Render();
        if (rendering_error) {
            rendering_error = false;
            throw Error(ErrorCategory::dependency_failure, "VTK could not render the scene on this graphics device");
        }
    }

    void reset_camera()
    {
        if (have_objects || (have_container && container_visible)) {
            renderer->ResetCamera();
            renderer->ResetCameraClippingRange();
        }
    }
};

Viewport::Viewport(const HWND parent) : impl_(std::make_unique<Impl>(parent)) {}
Viewport::~Viewport() = default;

bool Viewport::interaction_active() const
{
    return impl_->interactor->GetInitialized() && impl_->interactor->GetEnabled() && !impl_->interactor->GetDone();
}

void Viewport::show_scene(const TriangleMesh& objects, const TriangleMesh& container)
{
    // Convert both before changing the visible scene, so rejected geometry
    // leaves the previously loaded result available for inspection.
    const auto object_data = make_poly_data(objects);
    const auto container_data = make_poly_data(container);
    impl_->object_mapper->SetInputData(object_data);
    impl_->container_mapper->SetInputData(container_data);
    impl_->have_objects = !objects.triangles.empty();
    impl_->have_container = !container.triangles.empty();
    impl_->objects->SetVisibility(impl_->have_objects);
    const bool show_container = impl_->have_container && impl_->container_visible;
    impl_->container_surface->SetVisibility(show_container);
    impl_->container_edges->SetVisibility(show_container);
    set_view(-1);
}

void Viewport::clear()
{
    vtkNew<vtkPolyData> empty;
    impl_->object_mapper->SetInputData(empty);
    impl_->container_mapper->SetInputData(empty);
    impl_->have_objects = false;
    impl_->have_container = false;
    impl_->objects->VisibilityOff();
    impl_->container_surface->VisibilityOff();
    impl_->container_edges->VisibilityOff();
    impl_->render();
}

void Viewport::resize(const int x, const int y, const int width, const int height)
{
    impl_->suspended = width <= 0 || height <= 0;
    if (impl_->suspended) {
        return;
    }
    impl_->window->SetPosition(x, y);
    impl_->window->SetSize(width, height);
    impl_->interactor->UpdateSize(width, height);
    impl_->renderer->ResetCameraClippingRange();
    impl_->render();
}

Point3 Viewport::camera_position() const
{
    const double* position = impl_->renderer->GetActiveCamera()->GetPosition();
    return { position[0], position[1], position[2] };
}

void Viewport::fit()
{
    impl_->reset_camera();
    impl_->render();
}

void Viewport::set_container_visible(const bool visible)
{
    impl_->container_visible = visible;
    const bool show = visible && impl_->have_container;
    impl_->container_surface->SetVisibility(show);
    impl_->container_edges->SetVisibility(show);
    impl_->renderer->ResetCameraClippingRange();
    impl_->render();
}

void Viewport::set_objects_wireframe(const bool wireframe)
{
    if (wireframe) {
        impl_->objects->GetProperty()->SetRepresentationToWireframe();
    }
    else {
        impl_->objects->GetProperty()->SetRepresentationToSurface();
    }
    impl_->render();
}

void Viewport::set_view(const int axis)
{
    if (axis < -1 || axis > 2) {
        throw Error(ErrorCategory::invalid_configuration, "camera axis must be isometric, X, Y, or Z");
    }
    vtkCamera* camera = impl_->renderer->GetActiveCamera();
    camera->SetFocalPoint(0.0, 0.0, 0.0);
    camera->SetViewUp(0.0, 0.0, 1.0);
    if (axis == -1) {
        camera->SetPosition(1.0, -1.0, 0.8);
    }
    else if (axis == 0) {
        camera->SetPosition(1.0, 0.0, 0.0);
    }
    else if (axis == 1) {
        camera->SetPosition(0.0, -1.0, 0.0);
    }
    else {
        camera->SetPosition(0.0, 0.0, 1.0);
        camera->SetViewUp(0.0, 1.0, 0.0);
    }
    camera->OrthogonalizeViewUp();
    impl_->reset_camera();
    impl_->render();
}

void Viewport::save_snapshot(const std::filesystem::path& path)
{
    const int* size = impl_->window->GetSize();
    if (impl_->suspended || size[0] <= 0 || size[1] <= 0) {
        throw Error(ErrorCategory::invalid_configuration, "restore the viewport before saving a snapshot");
    }
    if (static_cast<std::uint64_t>(size[0]) * static_cast<std::uint64_t>(size[1]) > maximum_snapshot_pixels) {
        throw Error(ErrorCategory::resource_limit, "the viewport exceeds the 16-megapixel snapshot limit");
    }
    impl_->render();
    vtkNew<vtkWindowToImageFilter> capture;
    capture->AddObserver(vtkCommand::ErrorEvent, impl_->error_observer);
    capture->SetInput(impl_->window);
    capture->SetInputBufferTypeToRGB();
    capture->ReadFrontBufferOff();
    capture->ShouldRerenderOff();
    capture->Update();

    vtkNew<vtkPNGWriter> writer;
    writer->AddObserver(vtkCommand::ErrorEvent, impl_->error_observer);
    writer->SetInputConnection(capture->GetOutputPort());
    writer->WriteToMemoryOn();
    writer->Write();
    vtkUnsignedCharArray* png = writer->GetResult();
    if (impl_->rendering_error || writer->GetErrorCode() != vtkErrorCode::NoError || !png ||
        png->GetNumberOfValues() <= 0 || png->GetNumberOfValues() > maximum_snapshot_bytes) {
        impl_->rendering_error = false;
        throw Error(ErrorCategory::dependency_failure, "VTK could not encode the viewport snapshot");
    }
    SnapshotFile output(path);
    output.write(png->GetPointer(0), png->GetNumberOfValues());
}

}  // namespace irop::studio
