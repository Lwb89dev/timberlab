#include "timberlab/core/joint.hpp"

#include "timberlab/units/units.hpp"

#include <algorithm>

namespace timberlab {
namespace {

bool boxPart(const Component& component, std::string& error) {
  if (component.type == ComponentType::Cylinder) {
    error = "joint.not_box";
    return false;
  }
  return true;
}

bool inRange(std::int64_t nm) { return nm > 0 && nm <= units::kMaxPartNm; }

bool inside(std::int64_t offset, std::int64_t size, std::int64_t extent) {
  return offset >= 0 && size > 0 && offset <= extent && size <= extent - offset;
}

void endSlab(const FaceAxes& axes, std::int64_t length, std::int64_t extent, std::int64_t& low, std::int64_t& high) {
  if (axes.atMax) {
    low = extent - length;
    high = extent;
    return;
  }
  low = 0;
  high = length;
}

LocalCut ranged(const Component& part, const FaceAxes& axes, std::int64_t n0, std::int64_t n1, std::int64_t u0,
                std::int64_t u1, std::int64_t v0, std::int64_t v1) {
  LocalCut cut;
  cut.x1 = part.widthNm;
  cut.y1 = part.thicknessNm;
  cut.z1 = part.heightNm;
  auto put = [&](int axis, std::int64_t low, std::int64_t high) {
    if (axis == 0) {
      cut.x0 = low;
      cut.x1 = high;
    } else if (axis == 1) {
      cut.y0 = low;
      cut.y1 = high;
    } else {
      cut.z0 = low;
      cut.z1 = high;
    }
  };
  put(axes.normal, n0, n1);
  put(axes.u, u0, u1);
  put(axes.v, v0, v1);
  return cut;
}

void pushIfOpen(std::vector<LocalCut>& cuts, const LocalCut& cut) {
  if (cut.x1 > cut.x0 && cut.y1 > cut.y0 && cut.z1 > cut.z0) cuts.push_back(cut);
}

bool fitMortise(const Joint& joint, const Component& mortise, const Component& tenon, std::string& error) {
  const FaceAxes mortiseFace = faceAxes(joint.faceA);
  const FaceAxes tenonEnd = faceAxes(joint.faceB);
  const std::int64_t tenonNormal = axisNm(tenon, tenonEnd.normal);
  const std::int64_t tenonU = axisNm(tenon, tenonEnd.u);
  const std::int64_t tenonV = axisNm(tenon, tenonEnd.v);
  const std::int64_t mortiseNormal = axisNm(mortise, mortiseFace.normal);
  const std::int64_t mortiseU = axisNm(mortise, mortiseFace.u);
  const std::int64_t mortiseV = axisNm(mortise, mortiseFace.v);
  if (!inRange(joint.lengthNm) || joint.lengthNm >= tenonNormal) {
    error = "joint.parameter";
    return false;
  }
  if (!inRange(joint.depthNm) || joint.depthNm > mortiseNormal) {
    error = "joint.parameter";
    return false;
  }
  if (!inRange(joint.thicknessNm) || !inRange(joint.widthNm)) {
    error = "joint.parameter";
    return false;
  }
  if (joint.thicknessNm > tenonU || joint.widthNm > tenonV) {
    error = "joint.parameter";
    return false;
  }
  if (joint.thicknessNm >= tenonU && joint.widthNm >= tenonV) {
    error = "joint.parameter";
    return false;
  }
  if (!inside(joint.offsetNm, joint.thicknessNm, mortiseU) || !inside(joint.offset2Nm, joint.widthNm, mortiseV)) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

bool fitHalfLap(const Joint& joint, const Component& partA, const Component& partB, std::string& error) {
  const FaceAxes faceA = faceAxes(joint.faceA);
  const FaceAxes faceB = faceAxes(joint.faceB);
  const std::int64_t normalA = axisNm(partA, faceA.normal);
  const std::int64_t normalB = axisNm(partB, faceB.normal);
  const std::int64_t uA = axisNm(partA, faceA.u);
  const std::int64_t uB = axisNm(partB, faceB.u);
  if (!inRange(joint.lengthNm) || joint.lengthNm > normalA || joint.lengthNm > normalB || uA < 2 || uB < 2) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

bool fitDado(const Joint& joint, const Component& host, std::string& error) {
  const FaceAxes face = faceAxes(joint.faceA);
  const std::int64_t normal = axisNm(host, face.normal);
  const std::int64_t along = axisNm(host, face.u);
  if (!inRange(joint.widthNm) || !inRange(joint.depthNm) || joint.depthNm >= normal) {
    error = "joint.parameter";
    return false;
  }
  if (!inside(joint.offsetNm, joint.widthNm, along)) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

bool fitGroove(const Joint& joint, const Component& host, std::string& error) {
  const FaceAxes face = faceAxes(joint.faceA);
  const std::int64_t normal = axisNm(host, face.normal);
  const std::int64_t alongU = axisNm(host, face.u);
  const std::int64_t alongV = axisNm(host, face.v);
  if (!inRange(joint.widthNm) || !inRange(joint.lengthNm) || !inRange(joint.depthNm) || joint.depthNm >= normal) {
    error = "joint.parameter";
    return false;
  }
  if (!inside(joint.offsetNm, joint.widthNm, alongU) || !inside(joint.offset2Nm, joint.lengthNm, alongV)) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

bool fitRabbet(const Joint& joint, const Component& host, std::string& error) {
  const FaceAxes face = faceAxes(joint.faceA);
  const std::int64_t normal = axisNm(host, face.normal);
  const int insetAxis = (joint.edge == FaceEdge::U0 || joint.edge == FaceEdge::U1) ? face.u : face.v;
  const std::int64_t insetExtent = axisNm(host, insetAxis);
  if (!inRange(joint.widthNm) || !inRange(joint.depthNm) || joint.depthNm >= normal || joint.widthNm >= insetExtent) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

void addTenonCuts(const Joint& joint, const Component& tenon, std::vector<LocalCut>& cuts) {
  const FaceAxes face = faceAxes(joint.faceB);
  const std::int64_t normal = axisNm(tenon, face.normal);
  const std::int64_t uSize = axisNm(tenon, face.u);
  const std::int64_t vSize = axisNm(tenon, face.v);
  std::int64_t n0 = 0;
  std::int64_t n1 = 0;
  endSlab(face, joint.lengthNm, normal, n0, n1);
  const std::int64_t u0 = (uSize - joint.thicknessNm) / 2;
  const std::int64_t v0 = (vSize - joint.widthNm) / 2;
  const std::int64_t u1 = u0 + joint.thicknessNm;
  const std::int64_t v1 = v0 + joint.widthNm;
  pushIfOpen(cuts, ranged(tenon, face, n0, n1, 0, u0, 0, vSize));
  pushIfOpen(cuts, ranged(tenon, face, n0, n1, u1, uSize, 0, vSize));
  pushIfOpen(cuts, ranged(tenon, face, n0, n1, u0, u1, 0, v0));
  pushIfOpen(cuts, ranged(tenon, face, n0, n1, u0, u1, v1, vSize));
}

void addMortiseCut(const Joint& joint, const Component& mortise, std::vector<LocalCut>& cuts) {
  const FaceAxes face = faceAxes(joint.faceA);
  const std::int64_t normal = axisNm(mortise, face.normal);
  std::int64_t n0 = 0;
  std::int64_t n1 = 0;
  endSlab(face, joint.depthNm, normal, n0, n1);
  const std::int64_t u0 = joint.offsetNm;
  const std::int64_t v0 = joint.offset2Nm;
  pushIfOpen(cuts, ranged(mortise, face, n0, n1, u0, u0 + joint.thicknessNm, v0, v0 + joint.widthNm));
}

void addLapCut(const Component& part, PartFace face, std::int64_t length, bool removeMin, std::vector<LocalCut>& cuts) {
  const FaceAxes axes = faceAxes(face);
  const std::int64_t normal = axisNm(part, axes.normal);
  const std::int64_t uSize = axisNm(part, axes.u);
  const std::int64_t vSize = axisNm(part, axes.v);
  const std::int64_t half = uSize / 2;
  const std::int64_t u0 = removeMin ? 0 : half;
  const std::int64_t u1 = removeMin ? half : uSize;
  std::int64_t n0 = 0;
  std::int64_t n1 = 0;
  endSlab(axes, length, normal, n0, n1);
  pushIfOpen(cuts, ranged(part, axes, n0, n1, u0, u1, 0, vSize));
}

void addFaceSlot(const Component& host, const FaceAxes& face, std::int64_t depth, std::int64_t u0, std::int64_t u1,
                 std::int64_t v0, std::int64_t v1, std::vector<LocalCut>& cuts) {
  const std::int64_t normal = axisNm(host, face.normal);
  std::int64_t n0 = 0;
  std::int64_t n1 = 0;
  endSlab(face, depth, normal, n0, n1);
  pushIfOpen(cuts, ranged(host, face, n0, n1, u0, u1, v0, v1));
}

std::int64_t shrinkTo(std::int64_t value, std::int64_t limit) {
  if (limit <= 1) return 0;
  if (value > 0 && value < limit) return value;
  return std::max<std::int64_t>(1, limit / 3);
}

void faceDepth(const FaceAxes& face, std::int64_t extent, std::int64_t depth, std::int64_t& outer, std::int64_t& inner) {
  if (face.atMax) {
    outer = extent;
    inner = extent - depth;
    return;
  }
  outer = 0;
  inner = depth;
}

struct ProfilePoint {
  std::int64_t x = 0;
  std::int64_t y = 0;
  std::int64_t z = 0;
};

void putAxis(ProfilePoint& point, int axis, std::int64_t value) {
  if (axis == 0) point.x = value;
  else if (axis == 1) point.y = value;
  else point.z = value;
}

ProfilePoint profileAt(int normalAxis, std::int64_t normal, int uAxis, std::int64_t u, int vAxis, std::int64_t v) {
  ProfilePoint point;
  putAxis(point, normalAxis, normal);
  putAxis(point, uAxis, u);
  putAxis(point, vAxis, v);
  return point;
}

void pushPrism(std::vector<LocalCut>& cuts, int axis, std::int64_t end, const ProfilePoint* points, int count) {
  if (count < 3 || count > 4) return;
  LocalCut cut;
  cut.prismAxis = axis;
  cut.prismEnd = end;
  cut.pointCount = count;
  for (int i = 0; i < count; ++i) {
    cut.px[i] = points[i].x;
    cut.py[i] = points[i].y;
    cut.pz[i] = points[i].z;
  }
  cuts.push_back(cut);
}

bool fitBridle(const Joint& joint, const Component& female, const Component& male, std::string& error) {
  const FaceAxes faceA = faceAxes(joint.faceA);
  const FaceAxes faceB = faceAxes(joint.faceB);
  const std::int64_t uA = axisNm(female, faceA.u);
  const std::int64_t uB = axisNm(male, faceB.u);
  const std::int64_t nA = axisNm(female, faceA.normal);
  const std::int64_t nB = axisNm(male, faceB.normal);
  if (!inRange(joint.lengthNm) || !inRange(joint.thicknessNm)) {
    error = "joint.parameter";
    return false;
  }
  if (joint.lengthNm >= nA || joint.lengthNm >= nB || joint.thicknessNm >= uA || joint.thicknessNm >= uB) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

bool fitCastle(const Joint& joint, const Component& female, const Component& male, std::string& error) {
  if (joint.count < 2 || joint.count > 8) {
    error = "joint.parameter";
    return false;
  }
  if (!fitBridle(joint, female, male, error)) return false;
  const std::int64_t bands = static_cast<std::int64_t>(joint.count) * 2 - 1;
  const std::int64_t uA = axisNm(female, faceAxes(joint.faceA).u);
  const std::int64_t uB = axisNm(male, faceAxes(joint.faceB).u);
  if (joint.thicknessNm > uA / bands || joint.thicknessNm > uB / bands) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

bool fitMiter(const Joint& joint, const Component& partA, const Component& partB, std::string& error) {
  const std::int64_t nA = axisNm(partA, faceAxes(joint.faceA).normal);
  const std::int64_t nB = axisNm(partB, faceAxes(joint.faceB).normal);
  if (!inRange(joint.lengthNm) || joint.lengthNm >= nA || joint.lengthNm >= nB) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

bool fitDovetail(const Joint& joint, const Component& pins, const Component& tails, std::string& error) {
  const FaceAxes pinFace = faceAxes(joint.faceA);
  const FaceAxes tailFace = faceAxes(joint.faceB);
  const std::int64_t uPins = axisNm(pins, pinFace.u);
  const std::int64_t uTails = axisNm(tails, tailFace.u);
  const std::int64_t nPins = axisNm(pins, pinFace.normal);
  const std::int64_t nTails = axisNm(tails, tailFace.normal);
  if (joint.count < 1 || joint.count > 8) {
    error = "joint.parameter";
    return false;
  }
  if (!inRange(joint.thicknessNm) || !inRange(joint.widthNm) || !inRange(joint.lengthNm) || !inRange(joint.depthNm) ||
      !inRange(joint.offsetNm)) {
    error = "joint.parameter";
    return false;
  }
  if (joint.lengthNm >= nTails || joint.depthNm > nPins) {
    error = "joint.parameter";
    return false;
  }
  if (joint.offsetNm * 2 >= joint.thicknessNm || joint.offsetNm * 2 >= joint.widthNm) {
    error = "joint.parameter";
    return false;
  }
  const std::int64_t span = static_cast<std::int64_t>(joint.count) * (joint.thicknessNm + joint.widthNm);
  if (span > uPins || span > uTails) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

bool fitSliding(const Joint& joint, const Component& host, const Component& tongue, std::string& error) {
  const FaceAxes hostFace = faceAxes(joint.faceA);
  const FaceAxes tongueFace = faceAxes(joint.faceB);
  const std::int64_t hostU = axisNm(host, hostFace.u);
  const std::int64_t hostN = axisNm(host, hostFace.normal);
  const std::int64_t tongueU = axisNm(tongue, tongueFace.u);
  const std::int64_t tongueN = axisNm(tongue, tongueFace.normal);
  if (!inRange(joint.widthNm) || !inRange(joint.depthNm) || !inRange(joint.offset2Nm)) {
    error = "joint.parameter";
    return false;
  }
  if (joint.depthNm >= hostN || joint.depthNm >= tongueN) {
    error = "joint.parameter";
    return false;
  }
  if (joint.offsetNm < joint.offset2Nm || joint.offsetNm > hostU) {
    error = "joint.parameter";
    return false;
  }
  if (hostU - joint.offsetNm < joint.widthNm + joint.offset2Nm) {
    error = "joint.parameter";
    return false;
  }
  if (joint.widthNm + 2 * joint.offset2Nm > tongueU) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

void addBridleCuts(const Joint& joint, const Component& part, std::vector<LocalCut>& cuts) {
  const bool female = part.id == joint.partA;
  const FaceAxes face = faceAxes(female ? joint.faceA : joint.faceB);
  const std::int64_t uSize = axisNm(part, face.u);
  const std::int64_t vSize = axisNm(part, face.v);
  const std::int64_t normal = axisNm(part, face.normal);
  const std::int64_t u0 = (uSize - joint.thicknessNm) / 2;
  const std::int64_t u1 = u0 + joint.thicknessNm;
  std::int64_t n0 = 0;
  std::int64_t n1 = 0;
  endSlab(face, joint.lengthNm, normal, n0, n1);
  if (female) {
    addFaceSlot(part, face, joint.lengthNm, u0, u1, 0, vSize, cuts);
    return;
  }
  pushIfOpen(cuts, ranged(part, face, n0, n1, 0, u0, 0, vSize));
  pushIfOpen(cuts, ranged(part, face, n0, n1, u1, uSize, 0, vSize));
}

void addCastleCuts(const Joint& joint, const Component& part, std::vector<LocalCut>& cuts) {
  const bool female = part.id == joint.partA;
  const FaceAxes face = faceAxes(female ? joint.faceA : joint.faceB);
  const std::int64_t uSize = axisNm(part, face.u);
  const std::int64_t vSize = axisNm(part, face.v);
  const std::int64_t normal = axisNm(part, face.normal);
  const std::int64_t bands = static_cast<std::int64_t>(joint.count) * 2 - 1;
  const std::int64_t origin = (uSize - bands * joint.thicknessNm) / 2;
  std::int64_t n0 = 0;
  std::int64_t n1 = 0;
  endSlab(face, joint.lengthNm, normal, n0, n1);
  for (int band = 0; band < joint.count * 2 - 1; ++band) {
    const bool slotOnFemale = (band % 2) == 1;
    if (female != slotOnFemale) continue;
    const std::int64_t u0 = origin + static_cast<std::int64_t>(band) * joint.thicknessNm;
    pushIfOpen(cuts, ranged(part, face, n0, n1, u0, u0 + joint.thicknessNm, 0, vSize));
  }
}

void addMiterCut(const Component& part, PartFace which, std::int64_t length, bool keepMin, std::vector<LocalCut>& cuts) {
  const FaceAxes face = faceAxes(which);
  const std::int64_t uSize = axisNm(part, face.u);
  const std::int64_t vSize = axisNm(part, face.v);
  const std::int64_t normal = axisNm(part, face.normal);
  std::int64_t outer = 0;
  std::int64_t inner = 0;
  faceDepth(face, normal, length, outer, inner);
  const std::int64_t cutU = keepMin ? uSize : 0;
  const std::int64_t keepU = keepMin ? 0 : uSize;
  const ProfilePoint points[3] = {
      profileAt(face.normal, outer, face.u, cutU, face.v, 0),
      profileAt(face.normal, inner, face.u, cutU, face.v, 0),
      profileAt(face.normal, outer, face.u, keepU, face.v, 0),
  };
  pushPrism(cuts, face.v, vSize, points, 3);
}

void pushTrapezoid(std::vector<LocalCut>& cuts, const FaceAxes& face, std::int64_t outer, std::int64_t inner,
                   std::int64_t uOuter0, std::int64_t uOuter1, std::int64_t uInner0, std::int64_t uInner1,
                   std::int64_t vSize) {
  const ProfilePoint points[4] = {
      profileAt(face.normal, outer, face.u, uOuter0, face.v, 0),
      profileAt(face.normal, outer, face.u, uOuter1, face.v, 0),
      profileAt(face.normal, inner, face.u, uInner1, face.v, 0),
      profileAt(face.normal, inner, face.u, uInner0, face.v, 0),
  };
  pushPrism(cuts, face.v, vSize, points, 4);
}

void addDovetailCuts(const Joint& joint, const Component& part, std::vector<LocalCut>& cuts) {
  const bool pins = part.id == joint.partA;
  const FaceAxes face = faceAxes(pins ? joint.faceA : joint.faceB);
  const std::int64_t uSize = axisNm(part, face.u);
  const std::int64_t vSize = axisNm(part, face.v);
  const std::int64_t normal = axisNm(part, face.normal);
  const std::int64_t reach = pins ? joint.depthNm : joint.lengthNm;
  const std::int64_t slope = joint.offsetNm;
  std::int64_t outer = 0;
  std::int64_t inner = 0;
  faceDepth(face, normal, reach, outer, inner);
  const std::int64_t span = static_cast<std::int64_t>(joint.count) * (joint.thicknessNm + joint.widthNm);
  const std::int64_t extra = uSize - span;
  std::int64_t pos = joint.widthNm / 2 + extra / 2;
  if (!pins) pushTrapezoid(cuts, face, outer, inner, 0, pos, 0, pos + slope, vSize);
  for (int tail = 0; tail < joint.count; ++tail) {
    const std::int64_t tail0 = pos;
    const std::int64_t tail1 = pos + joint.thicknessNm;
    pos = tail1;
    if (pins) {
      pushTrapezoid(cuts, face, outer, inner, tail0, tail1, tail0 + slope, tail1 - slope, vSize);
    }
    if (tail + 1 < joint.count) {
      const std::int64_t pin1 = pos + joint.widthNm;
      if (!pins) pushTrapezoid(cuts, face, outer, inner, pos, pin1, pos - slope, pin1 + slope, vSize);
      pos = pin1;
    }
  }
  if (!pins) pushTrapezoid(cuts, face, outer, inner, pos, uSize, pos - slope, uSize, vSize);
}

void addSlidingCuts(const Joint& joint, const Component& part, std::vector<LocalCut>& cuts) {
  const bool host = part.id == joint.partA;
  const FaceAxes face = faceAxes(host ? joint.faceA : joint.faceB);
  const std::int64_t uSize = axisNm(part, face.u);
  const std::int64_t vSize = axisNm(part, face.v);
  const std::int64_t normal = axisNm(part, face.normal);
  const std::int64_t slope = joint.offset2Nm;
  std::int64_t outer = 0;
  std::int64_t inner = 0;
  faceDepth(face, normal, joint.depthNm, outer, inner);
  if (host) {
    const std::int64_t u0 = joint.offsetNm;
    const std::int64_t u1 = u0 + joint.widthNm;
    pushTrapezoid(cuts, face, outer, inner, u0, u1, u0 - slope, u1 + slope, vSize);
    return;
  }
  const std::int64_t wide = joint.widthNm + 2 * slope;
  const std::int64_t center = uSize / 2;
  const std::int64_t tip0 = center - wide / 2;
  const std::int64_t tip1 = tip0 + wide;
  const std::int64_t shoulder0 = center - joint.widthNm / 2;
  const std::int64_t shoulder1 = shoulder0 + joint.widthNm;
  pushTrapezoid(cuts, face, outer, inner, 0, tip0, 0, shoulder0, vSize);
  pushTrapezoid(cuts, face, outer, inner, tip1, uSize, shoulder1, uSize, vSize);
}

}  // namespace

FaceAxes faceAxes(PartFace face) {
  switch (face) {
    case PartFace::X0:
      return {0, false, 1, 2};
    case PartFace::X1:
      return {0, true, 1, 2};
    case PartFace::Y0:
      return {1, false, 0, 2};
    case PartFace::Y1:
      return {1, true, 0, 2};
    case PartFace::Z0:
      return {2, false, 0, 1};
    case PartFace::Z1:
      return {2, true, 0, 1};
  }
  return {0, false, 1, 2};
}

std::int64_t axisNm(const Component& component, int axis) {
  if (axis == 0) return component.widthNm;
  if (axis == 1) return component.thicknessNm;
  return component.heightNm;
}

const char* jointTypeKey(JointType type) {
  switch (type) {
    case JointType::MortiseTenon:
      return "joint.mortise_tenon";
    case JointType::HalfLap:
      return "joint.half_lap";
    case JointType::Dado:
      return "joint.dado";
    case JointType::Groove:
      return "joint.groove";
    case JointType::Rabbet:
      return "joint.rabbet";
    case JointType::Bridle:
      return "joint.bridle";
    case JointType::Castle:
      return "joint.castle";
    case JointType::Miter:
      return "joint.miter";
    case JointType::Dovetail:
      return "joint.dovetail";
    case JointType::SlidingDovetail:
      return "joint.sliding_dovetail";
  }
  return "joint.mortise_tenon";
}

bool jointTypeFromKey(std::string_view key, JointType& out) {
  if (key == "joint.mortise_tenon") {
    out = JointType::MortiseTenon;
    return true;
  }
  if (key == "joint.half_lap") {
    out = JointType::HalfLap;
    return true;
  }
  if (key == "joint.dado") {
    out = JointType::Dado;
    return true;
  }
  if (key == "joint.groove") {
    out = JointType::Groove;
    return true;
  }
  if (key == "joint.rabbet") {
    out = JointType::Rabbet;
    return true;
  }
  if (key == "joint.bridle") {
    out = JointType::Bridle;
    return true;
  }
  if (key == "joint.castle") {
    out = JointType::Castle;
    return true;
  }
  if (key == "joint.miter") {
    out = JointType::Miter;
    return true;
  }
  if (key == "joint.dovetail") {
    out = JointType::Dovetail;
    return true;
  }
  if (key == "joint.sliding_dovetail") {
    out = JointType::SlidingDovetail;
    return true;
  }
  return false;
}

const char* faceKey(PartFace face) {
  switch (face) {
    case PartFace::X0:
      return "x0";
    case PartFace::X1:
      return "x1";
    case PartFace::Y0:
      return "y0";
    case PartFace::Y1:
      return "y1";
    case PartFace::Z0:
      return "z0";
    case PartFace::Z1:
      return "z1";
  }
  return "x0";
}

bool faceFromKey(std::string_view key, PartFace& out) {
  if (key == "x0") {
    out = PartFace::X0;
    return true;
  }
  if (key == "x1") {
    out = PartFace::X1;
    return true;
  }
  if (key == "y0") {
    out = PartFace::Y0;
    return true;
  }
  if (key == "y1") {
    out = PartFace::Y1;
    return true;
  }
  if (key == "z0") {
    out = PartFace::Z0;
    return true;
  }
  if (key == "z1") {
    out = PartFace::Z1;
    return true;
  }
  return false;
}

const char* edgeKey(FaceEdge edge) {
  switch (edge) {
    case FaceEdge::U0:
      return "u0";
    case FaceEdge::U1:
      return "u1";
    case FaceEdge::V0:
      return "v0";
    case FaceEdge::V1:
      return "v1";
  }
  return "u0";
}

bool edgeFromKey(std::string_view key, FaceEdge& out) {
  if (key == "u0") {
    out = FaceEdge::U0;
    return true;
  }
  if (key == "u1") {
    out = FaceEdge::U1;
    return true;
  }
  if (key == "v0") {
    out = FaceEdge::V0;
    return true;
  }
  if (key == "v1") {
    out = FaceEdge::V1;
    return true;
  }
  return false;
}

bool jointCutsPart(const Joint& joint, const std::string& partId) {
  if (joint.type == JointType::Dado || joint.type == JointType::Groove || joint.type == JointType::Rabbet) {
    return joint.partA == partId;
  }
  return joint.partA == partId || joint.partB == partId;
}

bool jointFits(const Joint& joint, const Component& partA, const Component& partB, std::string& error) {
  if (partA.id.empty() || partB.id.empty() || partA.id == partB.id) {
    error = "joint.same_part";
    return false;
  }
  if (!boxPart(partA, error) || !boxPart(partB, error)) return false;
  switch (joint.type) {
    case JointType::MortiseTenon:
      return fitMortise(joint, partA, partB, error);
    case JointType::HalfLap:
      return fitHalfLap(joint, partA, partB, error);
    case JointType::Dado:
      return fitDado(joint, partA, error);
    case JointType::Groove:
      return fitGroove(joint, partA, error);
    case JointType::Rabbet:
      return fitRabbet(joint, partA, error);
    case JointType::Bridle:
      return fitBridle(joint, partA, partB, error);
    case JointType::Castle:
      return fitCastle(joint, partA, partB, error);
    case JointType::Miter:
      return fitMiter(joint, partA, partB, error);
    case JointType::Dovetail:
      return fitDovetail(joint, partA, partB, error);
    case JointType::SlidingDovetail:
      return fitSliding(joint, partA, partB, error);
  }
  error = "joint.parameter";
  return false;
}

bool cutsForPart(const Joint& joint, const Component& part, std::vector<LocalCut>& cuts, std::string& error) {
  if (!jointCutsPart(joint, part.id)) return true;
  const std::size_t before = cuts.size();
  switch (joint.type) {
    case JointType::MortiseTenon:
      if (part.id == joint.partA) addMortiseCut(joint, part, cuts);
      if (part.id == joint.partB) addTenonCuts(joint, part, cuts);
      break;
    case JointType::HalfLap:
      if (part.id == joint.partA) addLapCut(part, joint.faceA, joint.lengthNm, joint.removeMinA, cuts);
      if (part.id == joint.partB) addLapCut(part, joint.faceB, joint.lengthNm, joint.removeMinB, cuts);
      break;
    case JointType::Dado:
      addFaceSlot(part, faceAxes(joint.faceA), joint.depthNm, joint.offsetNm, joint.offsetNm + joint.widthNm, 0,
                  axisNm(part, faceAxes(joint.faceA).v), cuts);
      break;
    case JointType::Groove:
      addFaceSlot(part, faceAxes(joint.faceA), joint.depthNm, joint.offsetNm, joint.offsetNm + joint.widthNm,
                  joint.offset2Nm, joint.offset2Nm + joint.lengthNm, cuts);
      break;
    case JointType::Rabbet: {
      const FaceAxes face = faceAxes(joint.faceA);
      const std::int64_t uSize = axisNm(part, face.u);
      const std::int64_t vSize = axisNm(part, face.v);
      std::int64_t u0 = 0;
      std::int64_t u1 = uSize;
      std::int64_t v0 = 0;
      std::int64_t v1 = vSize;
      if (joint.edge == FaceEdge::U0) u1 = joint.widthNm;
      else if (joint.edge == FaceEdge::U1) u0 = uSize - joint.widthNm;
      else if (joint.edge == FaceEdge::V0) v1 = joint.widthNm;
      else v0 = vSize - joint.widthNm;
      addFaceSlot(part, face, joint.depthNm, u0, u1, v0, v1, cuts);
      break;
    }
    case JointType::Bridle:
      addBridleCuts(joint, part, cuts);
      break;
    case JointType::Castle:
      addCastleCuts(joint, part, cuts);
      break;
    case JointType::Miter:
      if (part.id == joint.partA) addMiterCut(part, joint.faceA, joint.lengthNm, joint.removeMinA, cuts);
      if (part.id == joint.partB) addMiterCut(part, joint.faceB, joint.lengthNm, joint.removeMinB, cuts);
      break;
    case JointType::Dovetail:
      addDovetailCuts(joint, part, cuts);
      break;
    case JointType::SlidingDovetail:
      addSlidingCuts(joint, part, cuts);
      break;
  }
  if (cuts.size() == before) {
    error = "joint.no_effect";
    return false;
  }
  return true;
}

bool defaultJoint(JointType type, const Component& selected, const Component& mate, Joint& out, std::string& error) {
  out = Joint{};
  out.type = type;
  if (type == JointType::MortiseTenon) {
    out.partA = mate.id;
    out.partB = selected.id;
    out.faceA = PartFace::X1;
    out.faceB = PartFace::X0;
    out.thicknessNm = shrinkTo(selected.thicknessNm / 3, std::min(selected.thicknessNm, mate.thicknessNm));
    out.widthNm = shrinkTo((selected.heightNm * 2) / 3, std::min(selected.heightNm, mate.heightNm));
    const std::int64_t reach = std::min(selected.widthNm / 3, mate.widthNm / 2);
    out.lengthNm = shrinkTo(reach, selected.widthNm);
    out.depthNm = shrinkTo(out.lengthNm, mate.widthNm);
    out.offsetNm = std::max<std::int64_t>(0, (mate.thicknessNm - out.thicknessNm) / 2);
    out.offset2Nm = std::max<std::int64_t>(0, (mate.heightNm - out.widthNm) / 2);
  } else if (type == JointType::HalfLap) {
    out.partA = selected.id;
    out.partB = mate.id;
    out.faceA = PartFace::X0;
    out.faceB = PartFace::X0;
    out.lengthNm = shrinkTo(std::min(selected.widthNm, mate.widthNm) / 4, std::min(selected.widthNm, mate.widthNm));
    out.removeMinB = true;
  } else if (type == JointType::Bridle || type == JointType::Castle) {
    out.partA = mate.id;
    out.partB = selected.id;
    out.faceA = PartFace::X1;
    out.faceB = PartFace::X0;
    const std::int64_t u = std::min(selected.thicknessNm, mate.thicknessNm);
    out.lengthNm = shrinkTo(std::min(selected.widthNm, mate.widthNm) / 4, std::min(selected.widthNm, mate.widthNm));
    if (type == JointType::Castle) {
      out.count = 3;
      out.thicknessNm = shrinkTo(u / 5, u);
    } else {
      out.thicknessNm = shrinkTo(u / 3, u);
    }
  } else if (type == JointType::Miter) {
    out.partA = selected.id;
    out.partB = mate.id;
    out.faceA = PartFace::X0;
    out.faceB = PartFace::X0;
    out.removeMinA = true;
    out.removeMinB = true;
    const std::int64_t leg = std::min(selected.thicknessNm, mate.thicknessNm);
    const std::int64_t room = std::min(selected.widthNm, mate.widthNm);
    out.lengthNm = shrinkTo(leg, room);
  } else if (type == JointType::Dovetail) {
    out.partA = mate.id;
    out.partB = selected.id;
    out.faceA = PartFace::X1;
    out.faceB = PartFace::X0;
    out.count = 1;
    const std::int64_t u = std::min(selected.thicknessNm, mate.thicknessNm);
    out.thicknessNm = shrinkTo(u / 3, u);
    out.widthNm = shrinkTo(u / 3, u);
    out.lengthNm = shrinkTo(selected.widthNm / 3, selected.widthNm);
    out.depthNm = shrinkTo(out.lengthNm, mate.widthNm);
    out.offsetNm = shrinkTo(std::min(out.thicknessNm, out.widthNm) / 4, std::min(out.thicknessNm, out.widthNm) / 2);
  } else if (type == JointType::SlidingDovetail) {
    out.partA = selected.id;
    out.partB = mate.id;
    out.faceA = PartFace::Z1;
    out.faceB = PartFace::X0;
    out.widthNm = shrinkTo(mate.thicknessNm / 2, std::min(mate.thicknessNm, selected.widthNm));
    out.offset2Nm = shrinkTo(out.widthNm / 8, out.widthNm / 2);
    out.depthNm = shrinkTo(selected.heightNm / 4, std::min(selected.heightNm, mate.widthNm));
    const std::int64_t wide = out.widthNm + 2 * out.offset2Nm;
    out.offsetNm = std::max(out.offset2Nm, (selected.widthNm - wide) / 2);
  } else {
    out.partA = selected.id;
    out.partB = mate.id;
    out.faceA = PartFace::Z1;
    out.depthNm = shrinkTo(selected.heightNm / 3, selected.heightNm);
    if (type == JointType::Rabbet) {
      out.edge = FaceEdge::U0;
      out.widthNm = shrinkTo(selected.widthNm / 10, selected.widthNm);
    } else if (type == JointType::Groove) {
      out.widthNm = shrinkTo(selected.widthNm / 10, selected.widthNm);
      out.lengthNm = shrinkTo((selected.thicknessNm * 2) / 3, selected.thicknessNm);
      out.offsetNm = std::max<std::int64_t>(0, (selected.widthNm - out.widthNm) / 2);
      out.offset2Nm = std::max<std::int64_t>(0, (selected.thicknessNm - out.lengthNm) / 2);
    } else {
      out.widthNm = shrinkTo(std::min(mate.thicknessNm, selected.widthNm / 3), selected.widthNm);
      out.offsetNm = std::max<std::int64_t>(0, (selected.widthNm - out.widthNm) / 2);
    }
  }
  const Component& partA = out.partA == selected.id ? selected : mate;
  const Component& partB = out.partB == selected.id ? selected : mate;
  return jointFits(out, partA, partB, error);
}

}  // namespace timberlab
