#pragma once

#include "timberlab/core/component.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace timberlab::geometry {

enum class MeshQuality { Draft, Standard, Fine };

double deflectionMm(MeshQuality quality);

struct SolidMesh {
  bool ok = false;
  std::string error;
  double volumeMm3 = 0;
  double bboxMin[3] = {0, 0, 0};
  double bboxMax[3] = {0, 0, 0};
  // Interleaved position (mm) and face normal. One unique copy per corner
  // so adjacent faces do not share smoothed normals.
  std::vector<float> interleaved;
  std::vector<std::uint32_t> indices;
};

// OCCT is the solid authority. The returned mesh is only a display tessellation.
// Calls are not thread-safe: OCCT is used from the thread that calls these functions.
SolidMesh makeBoxMesh(double widthMm, double thicknessMm, double heightMm, MeshQuality quality);
SolidMesh makeCylinderMesh(double radiusMm, double heightMm, MeshQuality quality);
SolidMesh meshFor(const Component& component, MeshQuality quality);

}  // namespace timberlab::geometry
