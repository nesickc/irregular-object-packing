#include "irop/geometry/transform.hpp"

#include <Eigen/Core>
#include <cmath>
#include <cstddef>
#include <limits>

#include "irop/error.hpp"

namespace irop {
namespace {

[[nodiscard]] bool is_finite(const Point3& point) noexcept
{
    return std::isfinite(point.x) && std::isfinite(point.y) && std::isfinite(point.z);
}

[[nodiscard]] bool is_finite(const EulerRotationRadians& rotation) noexcept
{
    return std::isfinite(rotation.x) && std::isfinite(rotation.y) && std::isfinite(rotation.z);
}

[[nodiscard]] Eigen::Matrix4d to_eigen(const Matrix4& matrix)
{
    Eigen::Matrix4d result;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            const double value = matrix(row, column);
            if (!std::isfinite(value)) {
                throw Error(ErrorCategory::invalid_configuration, "transform matrix contains a non-finite value");
            }
            result(static_cast<Eigen::Index>(row), static_cast<Eigen::Index>(column)) = value;
        }
    }
    return result;
}

[[nodiscard]] Matrix4 from_eigen(const Eigen::Matrix4d& matrix)
{
    Matrix4 result;
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            result.values[row * 4 + column] = matrix(static_cast<Eigen::Index>(row), static_cast<Eigen::Index>(column));
        }
    }
    return result;
}

}  // namespace

double Matrix4::operator()(const std::size_t row, const std::size_t column) const noexcept
{
    return values[row * 4 + column];
}

Matrix4 identity_matrix() noexcept
{
    Matrix4 result;
    result.values[0] = 1.0;
    result.values[5] = 1.0;
    result.values[10] = 1.0;
    result.values[15] = 1.0;
    return result;
}

Matrix4 matrix_for(const Transform& transform)
{
    if (!std::isfinite(transform.volume_scale) || transform.volume_scale <= 0.0) {
        throw Error(ErrorCategory::invalid_configuration, "transform volume scale must be finite and positive");
    }
    if (!is_finite(transform.rotation) || !is_finite(transform.translation)) {
        throw Error(ErrorCategory::invalid_configuration, "transform rotation and translation must be finite");
    }

    const double cosine_x = std::cos(transform.rotation.x);
    const double cosine_y = std::cos(transform.rotation.y);
    const double cosine_z = std::cos(transform.rotation.z);
    const double sine_x = std::sin(transform.rotation.x);
    const double sine_y = std::sin(transform.rotation.y);
    const double sine_z = std::sin(transform.rotation.z);

    // The reference applies column vectors with R = Ry * Rz * Rx.
    Eigen::Matrix3d rotation;
    rotation << cosine_y * cosine_z, -cosine_y * sine_z * cosine_x + sine_y * sine_x,
        cosine_y * sine_z * sine_x + sine_y * cosine_x, sine_z, cosine_z * cosine_x, -cosine_z * sine_x,
        -sine_y * cosine_z, sine_y * sine_z * cosine_x + cosine_y * sine_x,
        -sine_y * sine_z * sine_x + cosine_y * cosine_x;

    const double linear_scale = std::cbrt(transform.volume_scale);
    Eigen::Matrix4d matrix = Eigen::Matrix4d::Identity();
    matrix.block<3, 3>(0, 0) = rotation * linear_scale;
    matrix(0, 3) = transform.translation.x;
    matrix(1, 3) = transform.translation.y;
    matrix(2, 3) = transform.translation.z;
    if (!matrix.allFinite()) {
        throw Error(ErrorCategory::invalid_configuration, "transform parameters overflowed the derived matrix");
    }
    return from_eigen(matrix);
}

Matrix4 compose(const Matrix4& after, const Matrix4& before)
{
    const Eigen::Matrix4d result = to_eigen(after) * to_eigen(before);
    if (!result.allFinite()) {
        throw Error(ErrorCategory::invalid_configuration, "transform composition overflowed");
    }
    return from_eigen(result);
}

Point3 transform_point(const Matrix4& matrix, const Point3& point)
{
    if (!is_finite(point)) {
        throw Error(ErrorCategory::invalid_mesh, "cannot transform a non-finite point");
    }

    const Eigen::Vector4d input(point.x, point.y, point.z, 1.0);
    const Eigen::Vector4d transformed = to_eigen(matrix) * input;
    const double homogeneous = transformed(3);
    if (!transformed.allFinite() || std::abs(homogeneous) <= std::numeric_limits<double>::min()) {
        throw Error(ErrorCategory::invalid_configuration, "transform produced an invalid homogeneous point");
    }
    const Point3 result {
        transformed(0) / homogeneous,
        transformed(1) / homogeneous,
        transformed(2) / homogeneous,
    };
    if (!is_finite(result)) {
        throw Error(ErrorCategory::invalid_configuration, "homogeneous division produced a non-finite point");
    }
    return result;
}

TriangleMesh transform_mesh(const TriangleMesh& mesh, const Matrix4& matrix)
{
    TriangleMesh transformed = mesh;
    for (Point3& vertex : transformed.vertices) {
        vertex = transform_point(matrix, vertex);
    }
    return transformed;
}

TriangleMesh transform_mesh(const TriangleMesh& mesh, const Transform& transform)
{
    return transform_mesh(mesh, matrix_for(transform));
}

}  // namespace irop
