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

// A drawing view of one part's cut solid, in the part's local frame. Front looks
// along +Y with X to the right and Z up. Top looks down -Z with X to the right
// and Y up. Right looks along -X with Y to the right and Z up.
enum class ViewDirection { Front, Top, Right };

struct Segment2 {
  double u0 = 0;
  double v0 = 0;
  double u1 = 0;
  double v1 = 0;
};

// Hidden-line removal on the exact solid, not on its tessellation. Visible are
// the sharp edges and the outlines of curved faces that the observer sees.
// Hidden are the ones behind a face. Curved edges are split into segments with
// a 0.02 mm deflection. Millimetres.
struct ViewLines {
  bool ok = false;
  std::string error;
  std::vector<Segment2> visible;
  std::vector<Segment2> hidden;
};

ViewLines hiddenLineView(const Component& component, const std::vector<Joint>& joints,
                         const std::vector<Component>& parts, ViewDirection view);

// Whole-project exports. onlyId names one part, or is empty for every visible
// part. world places each part by its placement, otherwise the part sits in its
// own frame. Parts are the cut solids, in millimetres. STL is binary and
// little-endian, and its triangles are the fine tessellation. STEP is AP214 text
// written by OCCT's STEP writer with one named product per part, and it needs an
// OCCT built with the data-exchange modules: without them the error is
// export.unavailable.
struct ExportResult {
  bool ok = false;
  std::string error;
  std::string data;
  int partCount = 0;
};

ExportResult exportStl(const std::vector<Component>& parts, const std::vector<Joint>& joints,
                       const std::string& onlyId, bool world);
ExportResult exportStep(const std::vector<Component>& parts, const std::vector<Joint>& joints,
                        const std::string& onlyId, bool world);
bool stepAvailable();

}  // namespace timberlab::geometry
