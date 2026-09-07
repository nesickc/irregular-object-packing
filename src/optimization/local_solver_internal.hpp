#pragma once

#include <span>

#include "irop/optimization/local_solver.hpp"

namespace irop::detail {

[[nodiscard]] bool evaluate_local_constraints_unchecked(std::span<const LocalPlaneConstraint> constraints,
                                                        const Point3& object_center, double padding,
                                                        const LocalTransformStep& step,
                                                        std::span<double> values) noexcept;

[[nodiscard]] bool evaluate_local_jacobian_unchecked(std::span<const LocalPlaneConstraint> constraints,
                                                     const Point3& object_center, const LocalTransformStep& step,
                                                     std::span<double> values) noexcept;

[[nodiscard]] bool evaluate_local_hessian_unchecked(std::span<const LocalPlaneConstraint> constraints,
                                                    std::span<const double> multipliers, const Point3& object_center,
                                                    const LocalTransformStep& step, std::span<double> values) noexcept;

[[nodiscard]] bool evaluate_applied_constraints_unchecked(std::span<const LocalPlaneConstraint> constraints,
                                                          const Transform& current, const Transform& candidate,
                                                          double padding, std::span<double> values) noexcept;

}  // namespace irop::detail
