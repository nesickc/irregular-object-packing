#pragma once

#include <array>
#include <cstddef>

#include "irop/model/triangle_mesh.hpp"

namespace irop {

struct EulerRotationRadians {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct Transform {
    // The Python reference stores a volume scale. The corresponding linear
    // mesh scale is cbrt(volume_scale).
    double volume_scale = 1.0;
    EulerRotationRadians rotation;
    Point3 translation;
};

struct Matrix4 {
    std::array<double, 16> values {};

    [[nodiscard]] double operator()(std::size_t row, std::size_t column) const noexcept;
};

[[nodiscard]] Matrix4 identity_matrix() noexcept;
[[nodiscard]] Matrix4 matrix_for(const Transform& transform);

// Returns the matrix that applies `before` first and `after` second.
[[nodiscard]] Matrix4 compose(const Matrix4& after, const Matrix4& before);

[[nodiscard]] Point3 transform_point(const Matrix4& matrix, const Point3& point);
[[nodiscard]] TriangleMesh transform_mesh(const TriangleMesh& mesh, const Matrix4& matrix);
[[nodiscard]] TriangleMesh transform_mesh(const TriangleMesh& mesh, const Transform& transform);

}  // namespace irop
