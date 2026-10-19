#pragma once

#include "timberlab/core/joint.hpp"

#include <cstddef>
#include <vector>

namespace timberlab {

// World axis-aligned box, millimetres. For a rotated part this is the bounding
// box of the oriented solid, not the rotated faces themselves.
struct WorldBox {
  double minX = 0;
  double minY = 0;
  double minZ = 0;
  double maxX = 0;
  double maxY = 0;
  double maxZ = 0;
};

// An edge or a center within this distance snaps. A contact may leave a gap
// up to this distance on one axis.
inline constexpr double kAlignMm = 8.0;
inline constexpr double kContactNearMm = 8.0;

WorldBox worldBox(const Component& component);

struct AxisSnap {
  double deltaMm[3] = {0, 0, 0};
  bool used[3] = {false, false, false};
  double targetMm[3] = {0, 0, 0};
  int targetEdge[3] = {-1, -1, -1};  // 0 = min, 1 = max, 2 = center
};

// Each axis snaps on its own to the nearest edge or center of another box.
// The first target at an equal distance wins. A zero delta still counts as
// used when the box is already on a target.
AxisSnap snapTranslation(const WorldBox& moving, const std::vector<WorldBox>& others, double thresholdMm);

struct PartContact {
  std::size_t indexA = 0;
  std::size_t indexB = 0;
  WorldBox zone;
  bool facesKnown = false;
  PartFace faceA = PartFace::X0;
  PartFace faceB = PartFace::X0;
};

// Visible boards and boxes. Two axes must overlap by more than 1 mm, and the
// third must overlap or leave a gap of at most nearMm. Faces are known only
// when both parts are unrotated and that third axis is the only thin one,
// with neither box nested inside the other.
std::vector<PartContact> partContacts(const std::vector<Component>& parts, double nearMm);

// Faces belong to the selected part and the mate, before defaultJoint swaps
// which one is part A.
struct ContactFaces {
  bool known = false;
  PartFace faceOfSelected = PartFace::X0;
  PartFace faceOfMate = PartFace::X0;
};

// The same joint defaultJoint would build. When faces are known, those faces
// replace the defaults and the fit is checked again. Sizes stay the starting
// sizes. This does not cut a solid.
bool prepareContactJoint(JointType type, const Component& selected, const Component& mate,
                         const ContactFaces& faces, Joint& out, std::string& error);

// tEnter is the entry distance along direction. A ray that starts inside the
// box reports 0. A miss, or a box entirely behind the origin, returns false.
bool rayHitsBox(const double origin[3], const double direction[3], const WorldBox& box, double& tEnter);

}  // namespace timberlab
