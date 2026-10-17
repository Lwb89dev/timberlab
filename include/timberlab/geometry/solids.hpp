#pragma once

#include "timberlab/core/component.hpp"
#include "timberlab/core/joint.hpp"

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

enum class PointSide { In, Out, On, Unknown };

struct SolidProbe {
  bool ok = false;
  std::string error;
  double volumeMm3 = 0;
};

// OCCT is the solid authority. The returned mesh is only a display tessellation.
// meshFor(component, quality) builds the plain part. The overload cuts every joint
// that removes material from that part and rebuilds the solid from stored sizes.
// A dado, groove, or rabbet cuts partA only. probeSolid and classifyLocal use the
// same solid and do not tessellate it. classifyLocal is in the part's local frame,
// in millimetres. Calls are not thread-safe: OCCT is used from the calling thread.
SolidMesh makeBoxMesh(double widthMm, double thicknessMm, double heightMm, MeshQuality quality);
SolidMesh makeCylinderMesh(double radiusMm, double heightMm, MeshQuality quality);
SolidMesh meshFor(const Component& component, MeshQuality quality);
SolidMesh meshFor(const Component& component, const std::vector<Joint>& joints, const std::vector<Component>& parts,
                  MeshQuality quality);
SolidProbe probeSolid(const Component& component, const std::vector<Joint>& joints, const std::vector<Component>& parts);
PointSide classifyLocal(const Component& component, const std::vector<Joint>& joints, const std::vector<Component>& parts,
                        double xMm, double yMm, double zMm);

}  // namespace timberlab::geometry
