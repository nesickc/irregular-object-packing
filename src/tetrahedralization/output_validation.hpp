#pragma once

#include "irop/tetrahedralization/tetrahedralization.hpp"

namespace irop::detail {

// Checks each backend cell before appending it. False leaves the output and
// omission count unchanged; allocation failure propagates to the adapter guard.
[[nodiscard]] bool append_validated_tetrahedron(const Tetrahedron& tetrahedron, TetrahedralMesh& mesh,
                                                const TetrahedralizationOptions& options, TetrahedralizationWork& work);

}  // namespace irop::detail
