#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <filesystem>
#include <memory>

#include "irop/model/triangle_mesh.hpp"

namespace irop::studio {

// Owns the rendering child window. All methods, including destruction, run on
// the parent's UI thread; destroy this object before destroying the parent HWND.
class Viewport final {
public:
    explicit Viewport(HWND parent);
    ~Viewport();

    Viewport(const Viewport&) = delete;
    Viewport& operator=(const Viewport&) = delete;

    void show_scene(const TriangleMesh& objects, const TriangleMesh& container);
    void clear();
    void resize(int x, int y, int width, int height);
    void fit();
    [[nodiscard]] Point3 camera_position() const;
    [[nodiscard]] bool interaction_active() const;
    void set_container_visible(bool visible);
    void set_objects_wireframe(bool wireframe);
    // -1 selects an isometric view; 0, 1 and 2 look along X, Y and Z.
    void set_view(int axis);
    // Writes a new PNG file and refuses to replace an existing path.
    void save_snapshot(const std::filesystem::path& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace irop::studio
