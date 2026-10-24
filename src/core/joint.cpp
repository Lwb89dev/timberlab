#include "timberlab/core/joint.hpp"

#include "timberlab/units/units.hpp"

#include <algorithm>
#include <cmath>

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

bool fitFinger(const Joint& joint, const Component& partA, const Component& partB, std::string& error) {
  if (joint.count < 2 || joint.count > 8) {
    error = "joint.parameter";
    return false;
  }
  const FaceAxes faceA = faceAxes(joint.faceA);
  const FaceAxes faceB = faceAxes(joint.faceB);
  const std::int64_t uA = axisNm(partA, faceA.u);
  const std::int64_t uB = axisNm(partB, faceB.u);
  const std::int64_t nA = axisNm(partA, faceA.normal);
  const std::int64_t nB = axisNm(partB, faceB.normal);
  if (uA != uB || uA <= 0 || (uA % joint.count) != 0) {
    error = "joint.parameter";
    return false;
  }
  if (!inRange(joint.lengthNm) || joint.lengthNm >= nA || joint.lengthNm >= nB) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

bool fitHalfBlind(const Joint& joint, const Component& pins, const Component& tails, std::string& error) {
  if (!fitDovetail(joint, pins, tails, error)) return false;
  const std::int64_t nPins = axisNm(pins, faceAxes(joint.faceA).normal);
  if (joint.depthNm >= nPins) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

bool fitTongue(const Joint& joint, const Component& host, const Component& tongue, std::string& error) {
  const FaceAxes hostFace = faceAxes(joint.faceA);
  const FaceAxes tongueFace = faceAxes(joint.faceB);
  const std::int64_t hostU = axisNm(host, hostFace.u);
  const std::int64_t hostN = axisNm(host, hostFace.normal);
  const std::int64_t tongueU = axisNm(tongue, tongueFace.u);
  const std::int64_t tongueN = axisNm(tongue, tongueFace.normal);
  if (!inRange(joint.thicknessNm) || !inRange(joint.depthNm)) {
    error = "joint.parameter";
    return false;
  }
  if (joint.thicknessNm >= hostU || joint.thicknessNm >= tongueU) {
    error = "joint.parameter";
    return false;
  }
  if (tongueU - joint.thicknessNm < 2) {
    error = "joint.parameter";
    return false;
  }
  if (joint.depthNm >= hostN || joint.depthNm >= tongueN) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

bool fitDowel(const Joint& joint, const Component& partA, const Component& partB, std::string& error) {
  if (joint.thicknessNm < 2 || (joint.thicknessNm % 2) != 0 || !inRange(joint.thicknessNm) || !inRange(joint.depthNm)) {
    error = "joint.parameter";
    return false;
  }
  const FaceAxes faceA = faceAxes(joint.faceA);
  const FaceAxes faceB = faceAxes(joint.faceB);
  const std::int64_t radius = joint.thicknessNm / 2;
  const std::int64_t limits[] = {axisNm(partA, faceA.u) / 2, axisNm(partA, faceA.v) / 2, axisNm(partB, faceB.u) / 2,
                                 axisNm(partB, faceB.v) / 2};
  for (const std::int64_t limit : limits) {
    if (radius >= limit) {
      error = "joint.parameter";
      return false;
    }
  }
  const std::int64_t nA = axisNm(partA, faceA.normal);
  const std::int64_t nB = axisNm(partB, faceB.normal);
  if (joint.depthNm > nA || joint.depthNm > nB) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

void addFingerCuts(const Joint& joint, const Component& part, std::vector<LocalCut>& cuts) {
  const bool oddBoard = part.id == joint.partA;
  const FaceAxes face = faceAxes(oddBoard ? joint.faceA : joint.faceB);
  const std::int64_t uSize = axisNm(part, face.u);
  const std::int64_t vSize = axisNm(part, face.v);
  const std::int64_t normal = axisNm(part, face.normal);
  const std::int64_t finger = uSize / joint.count;
  std::int64_t n0 = 0;
  std::int64_t n1 = 0;
  endSlab(face, joint.lengthNm, normal, n0, n1);
  for (int band = 0; band < joint.count; ++band) {
    const bool odd = (band % 2) == 1;
    if (odd != oddBoard) continue;
    const std::int64_t u0 = static_cast<std::int64_t>(band) * finger;
    pushIfOpen(cuts, ranged(part, face, n0, n1, u0, u0 + finger, 0, vSize));
  }
}

void addTongueCuts(const Joint& joint, const Component& part, std::vector<LocalCut>& cuts) {
  const bool host = part.id == joint.partA;
  const FaceAxes face = faceAxes(host ? joint.faceA : joint.faceB);
  const std::int64_t uSize = axisNm(part, face.u);
  const std::int64_t vSize = axisNm(part, face.v);
  const std::int64_t normal = axisNm(part, face.normal);
  std::int64_t n0 = 0;
  std::int64_t n1 = 0;
  endSlab(face, joint.depthNm, normal, n0, n1);
  if (host) {
    const std::int64_t u0 = (uSize - joint.thicknessNm) / 2;
    pushIfOpen(cuts, ranged(part, face, n0, n1, u0, u0 + joint.thicknessNm, 0, vSize));
    return;
  }
  const std::int64_t shoulder = (uSize - joint.thicknessNm) / 2;
  pushIfOpen(cuts, ranged(part, face, n0, n1, 0, shoulder, 0, vSize));
  pushIfOpen(cuts, ranged(part, face, n0, n1, shoulder + joint.thicknessNm, uSize, 0, vSize));
}

void addDowelHole(const Component& part, PartFace which, std::int64_t diameter, std::int64_t depth,
                  std::vector<LocalCut>& cuts) {
  const FaceAxes face = faceAxes(which);
  const std::int64_t uSize = axisNm(part, face.u);
  const std::int64_t vSize = axisNm(part, face.v);
  const std::int64_t normal = axisNm(part, face.normal);
  const std::int64_t radius = diameter / 2;
  const std::int64_t u0 = uSize / 2 - radius;
  const std::int64_t v0 = vSize / 2 - radius;
  std::int64_t n0 = 0;
  std::int64_t n1 = 0;
  endSlab(face, depth, normal, n0, n1);
  LocalCut cut = ranged(part, face, n0, n1, u0, u0 + diameter, v0, v0 + diameter);
  cut.cylinderAxis = face.normal;
  cut.radiusNm = radius;
  if (cut.x1 <= cut.x0 || cut.y1 <= cut.y0 || cut.z1 <= cut.z0 || radius <= 0) return;
  cuts.push_back(cut);
}

constexpr double kPocketAngle = 15.0 * 3.14159265358979323846 / 180.0;
constexpr std::int64_t kPocketMarginNm = 1000000;

bool fitHaunched(const Joint& joint, const Component& mortise, const Component& tenon, std::string& error) {
  const FaceAxes mortiseFace = faceAxes(joint.faceA);
  const FaceAxes tenonEnd = faceAxes(joint.faceB);
  const std::int64_t tenonNormal = axisNm(tenon, tenonEnd.normal);
  const std::int64_t tenonU = axisNm(tenon, tenonEnd.u);
  const std::int64_t tenonV = axisNm(tenon, tenonEnd.v);
  const bool sized = inRange(joint.lengthNm) && inRange(joint.depthNm) && inRange(joint.thicknessNm) &&
                     inRange(joint.widthNm) && inRange(joint.length2Nm);
  if (!sized || joint.lengthNm >= tenonNormal || joint.depthNm > axisNm(mortise, mortiseFace.normal)) {
    error = "joint.parameter";
    return false;
  }
  if (joint.thicknessNm > tenonU || joint.widthNm >= tenonV) {
    error = "joint.parameter";
    return false;
  }
  if (joint.length2Nm >= joint.lengthNm || joint.length2Nm > joint.depthNm) {
    error = "joint.parameter";
    return false;
  }
  const std::int64_t mortiseU = axisNm(mortise, mortiseFace.u);
  const std::int64_t mortiseV = axisNm(mortise, mortiseFace.v);
  if (!inside(joint.offsetNm, joint.thicknessNm, mortiseU) || !inside(joint.offset2Nm, tenonV, mortiseV)) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

// Two V ranges of a haunched tenon end: the tenon and the haunch beside it.
void haunchRanges(std::int64_t vSize, std::int64_t tenonWidth, bool haunchLow, std::int64_t origin,
                  std::int64_t range[2][2]) {
  const std::int64_t haunch = vSize - tenonWidth;
  range[0][0] = origin + (haunchLow ? haunch : 0);
  range[0][1] = range[0][0] + tenonWidth;
  range[1][0] = origin + (haunchLow ? 0 : tenonWidth);
  range[1][1] = range[1][0] + haunch;
}

void addHaunchedTenonCuts(const Joint& joint, const Component& tenon, std::vector<LocalCut>& cuts) {
  const FaceAxes face = faceAxes(joint.faceB);
  const std::int64_t normal = axisNm(tenon, face.normal);
  const std::int64_t uSize = axisNm(tenon, face.u);
  const std::int64_t vSize = axisNm(tenon, face.v);
  std::int64_t whole0 = 0;
  std::int64_t whole1 = 0;
  std::int64_t tip0 = 0;
  std::int64_t tip1 = 0;
  endSlab(face, joint.lengthNm, normal, whole0, whole1);
  endSlab(face, joint.lengthNm - joint.length2Nm, normal, tip0, tip1);
  const std::int64_t shoulder0 = face.atMax ? whole0 : tip1;
  const std::int64_t shoulder1 = face.atMax ? tip0 : whole1;
  const std::int64_t u0 = (uSize - joint.thicknessNm) / 2;
  const std::int64_t u1 = u0 + joint.thicknessNm;
  std::int64_t range[2][2];
  haunchRanges(vSize, joint.widthNm, joint.removeMinB, 0, range);
  pushIfOpen(cuts, ranged(tenon, face, whole0, whole1, 0, u0, range[0][0], range[0][1]));
  pushIfOpen(cuts, ranged(tenon, face, whole0, whole1, u1, uSize, range[0][0], range[0][1]));
  pushIfOpen(cuts, ranged(tenon, face, tip0, tip1, 0, uSize, range[1][0], range[1][1]));
  pushIfOpen(cuts, ranged(tenon, face, shoulder0, shoulder1, 0, u0, range[1][0], range[1][1]));
  pushIfOpen(cuts, ranged(tenon, face, shoulder0, shoulder1, u1, uSize, range[1][0], range[1][1]));
}

void addHaunchedMortiseCuts(const Joint& joint, const Component& mortise, const Component& tenon,
                            std::vector<LocalCut>& cuts) {
  const FaceAxes face = faceAxes(joint.faceA);
  const std::int64_t tenonV = axisNm(tenon, faceAxes(joint.faceB).v);
  std::int64_t range[2][2];
  haunchRanges(tenonV, joint.widthNm, joint.removeMinA, joint.offset2Nm, range);
  const std::int64_t u0 = joint.offsetNm;
  const std::int64_t u1 = u0 + joint.thicknessNm;
  addFaceSlot(mortise, face, joint.depthNm, u0, u1, range[0][0], range[0][1], cuts);
  addFaceSlot(mortise, face, joint.length2Nm, u0, u1, range[1][0], range[1][1], cuts);
}

bool fitWedged(const Joint& joint, const Component& mortise, const Component& tenon, std::string& error) {
  if (!fitMortise(joint, mortise, tenon, error)) return false;
  if (joint.count < 1 || joint.count > 2 || !inRange(joint.length2Nm) || !inRange(joint.width2Nm)) {
    error = "joint.parameter";
    return false;
  }
  if (joint.length2Nm > joint.lengthNm || joint.width2Nm * 2 * joint.count > joint.widthNm) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

void addWedgeKerfs(const Joint& joint, const Component& tenon, std::vector<LocalCut>& cuts) {
  const FaceAxes face = faceAxes(joint.faceB);
  const std::int64_t normal = axisNm(tenon, face.normal);
  const std::int64_t uSize = axisNm(tenon, face.u);
  const std::int64_t vSize = axisNm(tenon, face.v);
  std::int64_t n0 = 0;
  std::int64_t n1 = 0;
  endSlab(face, joint.length2Nm, normal, n0, n1);
  const std::int64_t u0 = (uSize - joint.thicknessNm) / 2;
  const std::int64_t v0 = (vSize - joint.widthNm) / 2;
  for (int kerf = 0; kerf < joint.count; ++kerf) {
    const std::int64_t center = v0 + joint.widthNm * (2 * kerf + 1) / (2 * joint.count);
    const std::int64_t k0 = center - joint.width2Nm / 2;
    pushIfOpen(cuts, ranged(tenon, face, n0, n1, u0, u0 + joint.thicknessNm, k0, k0 + joint.width2Nm));
  }
}

std::int64_t rowCenter(std::int64_t extent, std::int64_t pitch, int count, int index) {
  return extent / 2 - ((count - 1) * pitch) / 2 + index * pitch;
}

bool fitPocketRow(const Joint& joint, std::int64_t footprint, const Component& partA, const Component& partB,
                  std::string& error) {
  const std::int64_t pitch = joint.count > 1 ? joint.offsetNm : 0;
  const bool sized = joint.count >= 1 && joint.count <= 8 && inRange(joint.thicknessNm) && inRange(footprint) &&
                     inRange(joint.depthNm) && (joint.count == 1 || (inRange(pitch) && pitch >= footprint));
  if (!sized) {
    error = "joint.parameter";
    return false;
  }
  const std::int64_t span = (joint.count - 1) * pitch + footprint;
  const Component* parts[2] = {&partA, &partB};
  const PartFace faces[2] = {joint.faceA, joint.faceB};
  for (int side = 0; side < 2; ++side) {
    const FaceAxes face = faceAxes(faces[side]);
    const bool fits = joint.thicknessNm < axisNm(*parts[side], face.u) && span <= axisNm(*parts[side], face.v) &&
                      joint.depthNm < axisNm(*parts[side], face.normal);
    if (!fits) {
      error = "joint.parameter";
      return false;
    }
  }
  return true;
}

bool fitLooseTenon(const Joint& joint, const Component& partA, const Component& partB, std::string& error) {
  return fitPocketRow(joint, joint.widthNm, partA, partB, error);
}

void addLooseTenonPockets(const Joint& joint, const Component& part, PartFace which, std::vector<LocalCut>& cuts) {
  const FaceAxes face = faceAxes(which);
  const std::int64_t uSize = axisNm(part, face.u);
  const std::int64_t vSize = axisNm(part, face.v);
  const std::int64_t pitch = joint.count > 1 ? joint.offsetNm : 0;
  const std::int64_t u0 = (uSize - joint.thicknessNm) / 2;
  for (int pocket = 0; pocket < joint.count; ++pocket) {
    const std::int64_t v0 = rowCenter(vSize, pitch, joint.count, pocket) - joint.widthNm / 2;
    addFaceSlot(part, face, joint.depthNm, u0, u0 + joint.thicknessNm, v0, v0 + joint.widthNm, cuts);
  }
}

// The kerf a disc of that diameter leaves in a face when it plunges depth.
std::int64_t biscuitChord(std::int64_t diameter, std::int64_t depth) {
  const double radius = static_cast<double>(diameter) / 2.0;
  const double reach = static_cast<double>(depth);
  return static_cast<std::int64_t>(std::ceil(2.0 * std::sqrt(reach * (2.0 * radius - reach))));
}

bool fitBiscuit(const Joint& joint, const Component& partA, const Component& partB, std::string& error) {
  if (!inRange(joint.widthNm) || (joint.widthNm % 2) != 0 || !inRange(joint.depthNm) ||
      joint.depthNm > joint.widthNm / 2) {
    error = "joint.parameter";
    return false;
  }
  return fitPocketRow(joint, biscuitChord(joint.widthNm, joint.depthNm), partA, partB, error);
}

void addBiscuitKerfs(const Joint& joint, const Component& part, PartFace which, std::vector<LocalCut>& cuts) {
  const FaceAxes face = faceAxes(which);
  const std::int64_t uSize = axisNm(part, face.u);
  const std::int64_t vSize = axisNm(part, face.v);
  const std::int64_t normal = axisNm(part, face.normal);
  const std::int64_t pitch = joint.count > 1 ? joint.offsetNm : 0;
  const std::int64_t radius = joint.widthNm / 2;
  const std::int64_t u0 = (uSize - joint.thicknessNm) / 2;
  const std::int64_t reach = radius - joint.depthNm;
  const std::int64_t centerN = face.atMax ? normal + reach : -reach;
  for (int kerf = 0; kerf < joint.count; ++kerf) {
    const std::int64_t centerV = rowCenter(vSize, pitch, joint.count, kerf);
    LocalCut cut = ranged(part, face, centerN - radius, centerN + radius, u0, u0 + joint.thicknessNm,
                          centerV - radius, centerV + radius);
    cut.cylinderAxis = face.u;
    cut.radiusNm = radius;
    cuts.push_back(cut);
  }
}

bool fitCrossLap(const Joint& joint, const Component& partA, const Component& partB, std::string& error) {
  const FaceAxes faceA = faceAxes(joint.faceA);
  const FaceAxes faceB = faceAxes(joint.faceB);
  const std::int64_t normalA = axisNm(partA, faceA.normal);
  const std::int64_t normalB = axisNm(partB, faceB.normal);
  if (!inRange(joint.depthNm) || joint.depthNm >= normalA || joint.depthNm >= normalB) {
    error = "joint.parameter";
    return false;
  }
  const std::int64_t widthA = axisNm(partB, faceB.v);
  const std::int64_t widthB = axisNm(partA, faceA.v);
  if (!inside(joint.offsetNm, widthA, axisNm(partA, faceA.u)) ||
      !inside(joint.offset2Nm, widthB, axisNm(partB, faceB.u))) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

void addCrossLapCut(const Joint& joint, const Component& part, const Component& mate, std::vector<LocalCut>& cuts) {
  const bool first = part.id == joint.partA;
  const FaceAxes face = faceAxes(first ? joint.faceA : joint.faceB);
  const std::int64_t normal = axisNm(part, face.normal);
  const std::int64_t width = axisNm(mate, faceAxes(first ? joint.faceB : joint.faceA).v);
  const std::int64_t start = first ? joint.offsetNm : joint.offset2Nm;
  addFaceSlot(part, face, first ? joint.depthNm : normal - joint.depthNm, start, start + width, 0,
              axisNm(part, face.v), cuts);
}

bool fitEndLap(const Joint& joint, const Component& host, const Component& end, std::string& error) {
  const FaceAxes hostFace = faceAxes(joint.faceA);
  const FaceAxes endFace = faceAxes(joint.faceB);
  if (!inRange(joint.widthNm) || !inRange(joint.depthNm) || !inRange(joint.lengthNm)) {
    error = "joint.parameter";
    return false;
  }
  if (joint.depthNm >= axisNm(host, hostFace.normal) || joint.depthNm >= axisNm(end, endFace.u) ||
      joint.lengthNm >= axisNm(end, endFace.normal)) {
    error = "joint.parameter";
    return false;
  }
  if (!inside(joint.offsetNm, joint.widthNm, axisNm(host, hostFace.u))) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

void addEndLapCuts(const Joint& joint, const Component& part, std::vector<LocalCut>& cuts) {
  if (part.id == joint.partA) {
    const FaceAxes face = faceAxes(joint.faceA);
    addFaceSlot(part, face, joint.depthNm, joint.offsetNm, joint.offsetNm + joint.widthNm, 0, axisNm(part, face.v),
                cuts);
    return;
  }
  const FaceAxes face = faceAxes(joint.faceB);
  const std::int64_t normal = axisNm(part, face.normal);
  const std::int64_t uSize = axisNm(part, face.u);
  std::int64_t n0 = 0;
  std::int64_t n1 = 0;
  endSlab(face, joint.lengthNm, normal, n0, n1);
  const std::int64_t u0 = joint.removeMinB ? 0 : joint.depthNm;
  const std::int64_t u1 = joint.removeMinB ? uSize - joint.depthNm : uSize;
  pushIfOpen(cuts, ranged(part, face, n0, n1, u0, u1, 0, axisNm(part, face.v)));
}

// The kerf of a splined miter as a quadrilateral in the normal and U plane:
// the whole miter face, pushed margin into the removed corner and depth into the
// material, perpendicular to the face. corners[i] is {normal, u} in nanometres.
void miterKerfCorners(const Component& part, PartFace which, std::int64_t length, bool keepMin, std::int64_t depth,
                      std::int64_t margin, std::int64_t corners[4][2]) {
  const FaceAxes face = faceAxes(which);
  const std::int64_t normal = axisNm(part, face.normal);
  const std::int64_t uSize = axisNm(part, face.u);
  std::int64_t outer = 0;
  std::int64_t inner = 0;
  faceDepth(face, normal, length, outer, inner);
  const double cutU = keepMin ? static_cast<double>(uSize) : 0.0;
  const double keepU = keepMin ? 0.0 : static_cast<double>(uSize);
  const double a1[2] = {static_cast<double>(inner), cutU};
  const double a2[2] = {static_cast<double>(outer), keepU};
  const double run = std::hypot(a2[0] - a1[0], a2[1] - a1[1]);
  double step[2] = {-(a2[1] - a1[1]) / run, (a2[0] - a1[0]) / run};
  const double toRemoved = step[0] * (static_cast<double>(outer) - a1[0]) + step[1] * (cutU - a1[1]);
  if (toRemoved > 0.0) {
    step[0] = -step[0];
    step[1] = -step[1];
  }
  const double reach[4] = {-static_cast<double>(margin), -static_cast<double>(margin), static_cast<double>(depth),
                           static_cast<double>(depth)};
  const double* base[4] = {a1, a2, a2, a1};
  for (int i = 0; i < 4; ++i) {
    corners[i][0] = std::llround(base[i][0] + step[0] * reach[i]);
    corners[i][1] = std::llround(base[i][1] + step[1] * reach[i]);
  }
}

bool fitSplinedMiter(const Joint& joint, const Component& partA, const Component& partB, std::string& error) {
  if (!fitMiter(joint, partA, partB, error)) return false;
  if (!inRange(joint.thicknessNm) || !inRange(joint.depthNm)) {
    error = "joint.parameter";
    return false;
  }
  const Component* parts[2] = {&partA, &partB};
  const PartFace faces[2] = {joint.faceA, joint.faceB};
  const bool keeps[2] = {joint.removeMinA, joint.removeMinB};
  for (int side = 0; side < 2; ++side) {
    const FaceAxes face = faceAxes(faces[side]);
    if (!inside(joint.offsetNm, joint.thicknessNm, axisNm(*parts[side], face.v))) {
      error = "joint.parameter";
      return false;
    }
    std::int64_t corners[4][2];
    miterKerfCorners(*parts[side], faces[side], joint.lengthNm, keeps[side], joint.depthNm, 0, corners);
    const std::int64_t normal = axisNm(*parts[side], face.normal);
    for (const auto& corner : corners) {
      if (corner[0] < 0 || corner[0] > normal) {
        error = "joint.parameter";
        return false;
      }
    }
  }
  return true;
}

void addMiterKerf(const Joint& joint, const Component& part, PartFace which, bool keepMin,
                  std::vector<LocalCut>& cuts) {
  const FaceAxes face = faceAxes(which);
  std::int64_t corners[4][2];
  miterKerfCorners(part, which, joint.lengthNm, keepMin, joint.depthNm, 200000, corners);
  ProfilePoint points[4];
  for (int i = 0; i < 4; ++i) {
    points[i] = profileAt(face.normal, corners[i][0], face.u, corners[i][1], face.v, joint.offsetNm);
  }
  pushPrism(cuts, face.v, joint.offsetNm + joint.thicknessNm, points, 4);
}

// Frame of a pocket hole in the part's local axes. The hole enters through the
// side named by the edge and runs toward the end face at kPocketAngle.
struct PocketFrame {
  int head = 0;
  int depth = 1;
  int spread = 2;
  double headDir = 1.0;
  double depthDir = 1.0;
  double entryHead = 0.0;
  double entryDepth = 0.0;
  double headExtent = 0.0;
  double depthExtent = 0.0;
  double spreadExtent = 0.0;
};

PocketFrame pocketFrame(const Joint& joint, const Component& part) {
  const FaceAxes face = faceAxes(joint.faceA);
  const bool onU = joint.edge == FaceEdge::U0 || joint.edge == FaceEdge::U1;
  const bool entryMax = joint.edge == FaceEdge::U1 || joint.edge == FaceEdge::V1;
  PocketFrame frame;
  frame.head = face.normal;
  frame.depth = onU ? face.u : face.v;
  frame.spread = onU ? face.v : face.u;
  frame.headExtent = static_cast<double>(axisNm(part, frame.head));
  frame.depthExtent = static_cast<double>(axisNm(part, frame.depth));
  frame.spreadExtent = static_cast<double>(axisNm(part, frame.spread));
  frame.headDir = face.atMax ? 1.0 : -1.0;
  frame.depthDir = entryMax ? -1.0 : 1.0;
  const double inset = static_cast<double>(joint.offsetNm);
  frame.entryHead = face.atMax ? frame.headExtent - inset : inset;
  frame.entryDepth = entryMax ? frame.depthExtent : 0.0;
  return frame;
}

bool fitPocketScrew(const Joint& joint, const Component& partA, std::string& error) {
  const bool sized = inRange(joint.widthNm) && inRange(joint.thicknessNm) && inRange(joint.lengthNm) &&
                     inRange(joint.offsetNm) && (joint.widthNm % 2) == 0 && (joint.thicknessNm % 2) == 0 &&
                     joint.thicknessNm < joint.widthNm && joint.count >= 1 && joint.count <= 8 &&
                     (joint.count == 1 || (inRange(joint.offset2Nm) && joint.offset2Nm >= joint.widthNm));
  if (!sized) {
    error = "joint.parameter";
    return false;
  }
  const PocketFrame frame = pocketFrame(joint, partA);
  const double sine = std::sin(kPocketAngle);
  const double cosine = std::cos(kPocketAngle);
  const double bore = static_cast<double>(joint.widthNm) / 2.0;
  const double pilot = static_cast<double>(joint.thicknessNm) / 2.0;
  const double inset = static_cast<double>(joint.offsetNm);
  const double length = static_cast<double>(joint.lengthNm);
  const double exitDepth = inset * std::tan(kPocketAngle);
  const bool mouth = inset >= bore / sine && inset <= frame.headExtent;
  const bool exits = exitDepth - pilot / cosine > 0.0 && exitDepth + pilot / cosine < frame.depthExtent;
  const bool bottom = length * sine + bore / cosine < frame.depthExtent && length * cosine + bore * sine < inset;
  const double span = static_cast<double>((joint.count - 1) * joint.offset2Nm) + static_cast<double>(joint.widthNm);
  if (!mouth || !exits || !bottom || span > frame.spreadExtent) {
    error = "joint.parameter";
    return false;
  }
  return true;
}

LocalCut freeCylinder(const double from[3], const double to[3], std::int64_t radius) {
  LocalCut cut;
  cut.cylinderAxis = 3;
  cut.radiusNm = radius;
  cut.x0 = std::llround(from[0]);
  cut.y0 = std::llround(from[1]);
  cut.z0 = std::llround(from[2]);
  cut.x1 = std::llround(to[0]);
  cut.y1 = std::llround(to[1]);
  cut.z1 = std::llround(to[2]);
  return cut;
}

void addPocketScrewHoles(const Joint& joint, const Component& part, std::vector<LocalCut>& cuts) {
  const PocketFrame frame = pocketFrame(joint, part);
  const double sine = std::sin(kPocketAngle);
  const double cosine = std::cos(kPocketAngle);
  const std::int64_t boreRadius = joint.widthNm / 2;
  const std::int64_t pilotRadius = joint.thicknessNm / 2;
  double direction[3] = {};
  direction[frame.head] = frame.headDir * cosine;
  direction[frame.depth] = frame.depthDir * sine;
  const double back = 4.0 * static_cast<double>(boreRadius) + static_cast<double>(kPocketMarginNm);
  const double floorAt = static_cast<double>(joint.lengthNm);
  const double through = static_cast<double>(joint.offsetNm) / cosine + static_cast<double>(pilotRadius) +
                         static_cast<double>(kPocketMarginNm);
  const std::int64_t pitch = joint.count > 1 ? joint.offset2Nm : 0;
  for (int hole = 0; hole < joint.count; ++hole) {
    double entry[3] = {};
    entry[frame.head] = frame.entryHead;
    entry[frame.depth] = frame.entryDepth;
    entry[frame.spread] = static_cast<double>(rowCenter(static_cast<std::int64_t>(frame.spreadExtent), pitch,
                                                        joint.count, hole));
    double start[3], floorPoint[3], tip[3];
    for (int axis = 0; axis < 3; ++axis) {
      start[axis] = entry[axis] - direction[axis] * back;
      floorPoint[axis] = entry[axis] + direction[axis] * floorAt;
      tip[axis] = entry[axis] + direction[axis] * through;
    }
    cuts.push_back(freeCylinder(start, floorPoint, boreRadius));
    cuts.push_back(freeCylinder(floorPoint, tip, pilotRadius));
  }
}

void setTenonDefaults(Joint& out, const Component& selected, const Component& mate) {
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
}

std::int64_t evenDown(std::int64_t value) { return value - (value % 2); }

// Starting sizes for the types that came after the first set. Returns false for
// a type this function does not know.
bool defaultLaterJoint(JointType type, const Component& selected, const Component& mate, Joint& out) {
  const std::int64_t across = std::min(selected.thicknessNm, mate.thicknessNm);
  const std::int64_t tall = std::min(selected.heightNm, mate.heightNm);
  const std::int64_t reach = std::min(selected.widthNm, mate.widthNm);
  if (type == JointType::HaunchedTenon) {
    setTenonDefaults(out, selected, mate);
    out.widthNm = shrinkTo((selected.heightNm * 2) / 3, selected.heightNm);
    out.length2Nm = shrinkTo(out.lengthNm / 3, out.lengthNm);
    out.offset2Nm = std::max<std::int64_t>(0, (mate.heightNm - selected.heightNm) / 2);
  } else if (type == JointType::WedgedTenon) {
    setTenonDefaults(out, selected, mate);
    out.count = 1;
    out.width2Nm = std::max<std::int64_t>(1, std::min(3 * units::kNmPerMm, out.widthNm / 8));
    out.length2Nm = shrinkTo((out.lengthNm * 2) / 3, out.lengthNm);
  } else if (type == JointType::LooseTenon) {
    out.partA = mate.id;
    out.partB = selected.id;
    out.faceA = PartFace::X1;
    out.faceB = PartFace::X0;
    out.count = 1;
    out.thicknessNm = shrinkTo(across / 3, across);
    out.widthNm = shrinkTo(tall / 2, tall);
    out.depthNm = shrinkTo(reach / 8, reach);
  } else if (type == JointType::Biscuit) {
    out.partA = mate.id;
    out.partB = selected.id;
    out.faceA = PartFace::X1;
    out.faceB = PartFace::X0;
    out.count = 1;
    out.thicknessNm = shrinkTo(std::min(4 * units::kNmPerMm, across / 3), across);
    const std::int64_t chord = std::min(tall / 2, 56 * units::kNmPerMm);
    const std::int64_t plunge = std::min({10 * units::kNmPerMm, reach / 3, chord / 2});
    if (plunge < 1) return true;
    out.depthNm = plunge;
    const double radius = (static_cast<double>(chord) * static_cast<double>(chord) / 4.0 +
                           static_cast<double>(plunge) * static_cast<double>(plunge)) /
                          (2.0 * static_cast<double>(plunge));
    out.widthNm = 2 * std::llround(radius);
  } else if (type == JointType::CrossLap) {
    out.partA = selected.id;
    out.partB = mate.id;
    out.faceA = PartFace::Y1;
    out.faceB = PartFace::Y0;
    out.offsetNm = std::max<std::int64_t>(0, (selected.widthNm - mate.heightNm) / 2);
    out.offset2Nm = std::max<std::int64_t>(0, (mate.widthNm - selected.heightNm) / 2);
    out.depthNm = std::max<std::int64_t>(1, selected.thicknessNm / 2);
  } else if (type == JointType::EndLap) {
    out.partA = mate.id;
    out.partB = selected.id;
    out.faceA = PartFace::Y1;
    out.faceB = PartFace::X0;
    out.widthNm = selected.heightNm;
    out.offsetNm = std::max<std::int64_t>(0, (mate.widthNm - out.widthNm) / 2);
    out.depthNm = shrinkTo(mate.thicknessNm / 2, selected.thicknessNm);
    out.lengthNm = shrinkTo(mate.heightNm, selected.widthNm);
  } else if (type == JointType::SplinedMiter) {
    out.partA = selected.id;
    out.partB = mate.id;
    out.faceA = PartFace::X0;
    out.faceB = PartFace::X0;
    out.removeMinA = true;
    out.removeMinB = true;
    out.lengthNm = shrinkTo(across, reach);
    out.thicknessNm = std::max<std::int64_t>(1, std::min(3 * units::kNmPerMm, tall / 8));
    out.offsetNm = std::max<std::int64_t>(0, (tall - out.thicknessNm) / 2);
    out.depthNm = shrinkTo(across / 3, across);
  } else if (type == JointType::PocketScrew) {
    out.partA = selected.id;
    out.partB = mate.id;
    out.faceA = PartFace::X1;
    out.faceB = PartFace::X0;
    out.edge = FaceEdge::U0;
    out.count = 1;
    out.widthNm = evenDown(selected.thicknessNm / 2);
    out.thicknessNm = evenDown((out.widthNm * 9) / 20);
    const double inset = static_cast<double>(selected.thicknessNm) / 2.0 / std::tan(kPocketAngle);
    out.offsetNm = std::llround(inset);
    out.lengthNm = std::llround(inset / std::cos(kPocketAngle) / 2.0);
  } else {
    return false;
  }
  return true;
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
    case JointType::Finger:
      return "joint.finger";
    case JointType::HalfBlindDovetail:
      return "joint.half_blind_dovetail";
    case JointType::TongueGroove:
      return "joint.tongue_groove";
    case JointType::Dowel:
      return "joint.dowel";
    case JointType::HaunchedTenon:
      return "joint.haunched_tenon";
    case JointType::WedgedTenon:
      return "joint.wedged_tenon";
    case JointType::LooseTenon:
      return "joint.loose_tenon";
    case JointType::CrossLap:
      return "joint.cross_lap";
    case JointType::EndLap:
      return "joint.end_lap";
    case JointType::SplinedMiter:
      return "joint.splined_miter";
    case JointType::Biscuit:
      return "joint.biscuit";
    case JointType::PocketScrew:
      return "joint.pocket_screw";
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
  if (key == "joint.finger") {
    out = JointType::Finger;
    return true;
  }
  if (key == "joint.half_blind_dovetail") {
    out = JointType::HalfBlindDovetail;
    return true;
  }
  if (key == "joint.tongue_groove") {
    out = JointType::TongueGroove;
    return true;
  }
  if (key == "joint.dowel") {
    out = JointType::Dowel;
    return true;
  }
  static constexpr struct {
    const char* key;
    JointType type;
  } later[] = {{"joint.haunched_tenon", JointType::HaunchedTenon}, {"joint.wedged_tenon", JointType::WedgedTenon},
               {"joint.loose_tenon", JointType::LooseTenon},       {"joint.cross_lap", JointType::CrossLap},
               {"joint.end_lap", JointType::EndLap},               {"joint.splined_miter", JointType::SplinedMiter},
               {"joint.biscuit", JointType::Biscuit},              {"joint.pocket_screw", JointType::PocketScrew}};
  for (const auto& entry : later) {
    if (key != entry.key) continue;
    out = entry.type;
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
  if (joint.type == JointType::Dado || joint.type == JointType::Groove || joint.type == JointType::Rabbet ||
      joint.type == JointType::PocketScrew) {
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
    case JointType::Finger:
      return fitFinger(joint, partA, partB, error);
    case JointType::HalfBlindDovetail:
      return fitHalfBlind(joint, partA, partB, error);
    case JointType::TongueGroove:
      return fitTongue(joint, partA, partB, error);
    case JointType::Dowel:
      return fitDowel(joint, partA, partB, error);
    case JointType::HaunchedTenon:
      return fitHaunched(joint, partA, partB, error);
    case JointType::WedgedTenon:
      return fitWedged(joint, partA, partB, error);
    case JointType::LooseTenon:
      return fitLooseTenon(joint, partA, partB, error);
    case JointType::CrossLap:
      return fitCrossLap(joint, partA, partB, error);
    case JointType::EndLap:
      return fitEndLap(joint, partA, partB, error);
    case JointType::SplinedMiter:
      return fitSplinedMiter(joint, partA, partB, error);
    case JointType::Biscuit:
      return fitBiscuit(joint, partA, partB, error);
    case JointType::PocketScrew:
      return fitPocketScrew(joint, partA, error);
  }
  error = "joint.parameter";
  return false;
}

bool cutsForPart(const Joint& joint, const Component& part, const Component& mate, std::vector<LocalCut>& cuts,
                 std::string& error) {
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
    case JointType::Finger:
      addFingerCuts(joint, part, cuts);
      break;
    case JointType::HalfBlindDovetail:
      addDovetailCuts(joint, part, cuts);
      break;
    case JointType::TongueGroove:
      addTongueCuts(joint, part, cuts);
      break;
    case JointType::Dowel:
      if (part.id == joint.partA) addDowelHole(part, joint.faceA, joint.thicknessNm, joint.depthNm, cuts);
      if (part.id == joint.partB) addDowelHole(part, joint.faceB, joint.thicknessNm, joint.depthNm, cuts);
      break;
    case JointType::HaunchedTenon:
      if (part.id == joint.partA) addHaunchedMortiseCuts(joint, part, mate, cuts);
      if (part.id == joint.partB) addHaunchedTenonCuts(joint, part, cuts);
      break;
    case JointType::WedgedTenon:
      if (part.id == joint.partA) addMortiseCut(joint, part, cuts);
      if (part.id == joint.partB) {
        addTenonCuts(joint, part, cuts);
        addWedgeKerfs(joint, part, cuts);
      }
      break;
    case JointType::LooseTenon:
      addLooseTenonPockets(joint, part, part.id == joint.partA ? joint.faceA : joint.faceB, cuts);
      break;
    case JointType::CrossLap:
      addCrossLapCut(joint, part, mate, cuts);
      break;
    case JointType::EndLap:
      addEndLapCuts(joint, part, cuts);
      break;
    case JointType::SplinedMiter: {
      const bool first = part.id == joint.partA;
      const PartFace face = first ? joint.faceA : joint.faceB;
      const bool keepMin = first ? joint.removeMinA : joint.removeMinB;
      addMiterCut(part, face, joint.lengthNm, keepMin, cuts);
      addMiterKerf(joint, part, face, keepMin, cuts);
      break;
    }
    case JointType::Biscuit:
      addBiscuitKerfs(joint, part, part.id == joint.partA ? joint.faceA : joint.faceB, cuts);
      break;
    case JointType::PocketScrew:
      addPocketScrewHoles(joint, part, cuts);
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
  if (defaultLaterJoint(type, selected, mate, out)) {
    // The later types keep their own starting sizes.
  } else if (type == JointType::MortiseTenon) {
    setTenonDefaults(out, selected, mate);
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
  } else if (type == JointType::Finger) {
    out.partA = selected.id;
    out.partB = mate.id;
    out.faceA = PartFace::X0;
    out.faceB = PartFace::X0;
    out.count = 3;
    const std::int64_t room = std::min(selected.widthNm, mate.widthNm);
    out.lengthNm = shrinkTo(room / 4, room);
  } else if (type == JointType::HalfBlindDovetail) {
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
  } else if (type == JointType::TongueGroove) {
    out.partA = selected.id;
    out.partB = mate.id;
    out.faceA = PartFace::Z1;
    out.faceB = PartFace::X0;
    const std::int64_t hostU = selected.widthNm;
    const std::int64_t tongueU = mate.thicknessNm;
    out.thicknessNm = shrinkTo(std::min(hostU, tongueU) / 3, std::min(hostU, tongueU));
    const std::int64_t reach = std::min(selected.heightNm, mate.widthNm);
    out.depthNm = shrinkTo(reach / 4, reach);
  } else if (type == JointType::Dowel) {
    out.partA = selected.id;
    out.partB = mate.id;
    out.faceA = PartFace::X0;
    out.faceB = PartFace::X0;
    const std::int64_t across = std::min(selected.thicknessNm, mate.thicknessNm);
    const std::int64_t tall = std::min(selected.heightNm, mate.heightNm);
    std::int64_t diameter = std::min(across, tall) / 3;
    if ((diameter % 2) != 0) --diameter;
    out.thicknessNm = diameter;
    const std::int64_t room = std::min(selected.widthNm, mate.widthNm);
    out.depthNm = shrinkTo(room / 4, room);
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
