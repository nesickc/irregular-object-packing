#pragma once

#include <filesystem>

#include "irop/cat/cat.hpp"
#include "irop/tetrahedralization/tetrahedralization.hpp"

namespace irop {

// Writes project-owned tetrahedralization data as a VTK XML unstructured grid.
// The output path must not already exist.
void write_tetrahedralization_vtu(const std::filesystem::path& output_path, const TetrahedralMesh& mesh);

// Writes CAT face geometry as VTK XML polydata. Participant, source
// tetrahedron, polygon, and constraint-count metadata are stored per cell.
// The output path must not already exist.
void write_cat_vtp(const std::filesystem::path& output_path, const CatConstructionResult& cat);

}  // namespace irop
