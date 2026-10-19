#include "timberlab/core/align.hpp"

#include "timberlab/core/transform.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace timberlab {
namespace {

constexpr double kPlaneOverlapMm = 1.0;

double axisLo(const WorldBox& box, int axis) {
  if (axis == 0) return box.minX;
  if (axis == 1) return box.minY;
  return box.minZ;
}

double axisHi(const WorldBox& box, int axis) {
  if (axis == 0) return box.maxX;
  if (axis == 1) return box.maxY;
  return box.maxZ;
}

void setAxis(WorldBox& box, int axis, double lo, double hi) {
  if (axis == 0) {
    box.minX = lo;
    box.maxX = hi;
    return;
  }
  if (axis == 1) {
    box.minY = lo;
    box.maxY = hi;
    return;
  }
  box.minZ = lo;
  box.maxZ = hi;
}

double separation(const WorldBox& a, const WorldBox& b, int axis) {
  const double lo = std::max(axisLo(a, axis), axisLo(b, axis));
  const double hi = std::min(axisHi(a, axis), axisHi(b, axis));
  return lo - hi;
}

double edge(const WorldBox& box, int axis, int which) {
  const double lo = axisLo(box, axis);
  const double hi = axisHi(box, axis);
  if (which == 0) return lo;
  if (which == 1) return hi;
  return (lo + hi) * 0.5;
}

void considerSnap(const WorldBox& moving, const WorldBox& other, int axis, double& best, double& delta, bool& used,
                  double& targetMm, int& targetEdge) {
  for (int source = 0; source < 3; ++source) {
    const double from = edge(moving, axis, source);
    for (int target = 0; target < 3; ++target) {
      const double to = edge(other, axis, target);
      const double step = to - from;
      const double distance = std::abs(step);
      if (distance >= best) continue;
      best = distance;
      delta = step;
      used = true;
      targetMm = to;
      targetEdge = target;
    }
  }
}

bool boardOrBox(const Component& component) {
  return component.type == ComponentType::Board || component.type == ComponentType::Box;
}

bool flat(const Placement& placement) {
  return std::abs(placement.rxDeg) < 1e-4 && std::abs(placement.ryDeg) < 1e-4 &&
         std::abs(placement.rzDeg) < 1e-4;
}

bool planePair(const WorldBox& a, const WorldBox& b, double nearMm) {
  int planes = 0;
  int close = 0;
  for (int axis = 0; axis < 3; ++axis) {
    const double sep = separation(a, b, axis);
    if (sep < -kPlaneOverlapMm) ++planes;
    if (sep <= nearMm) ++close;
  }
  return planes >= 2 && close == 3;
}

void fillZone(WorldBox& zone, const WorldBox& a, const WorldBox& b) {
  for (int axis = 0; axis < 3; ++axis) {
    const double sep = separation(a, b, axis);
    double lo = 0;
    double hi = 0;
    if (sep < 0) {
      lo = std::max(axisLo(a, axis), axisLo(b, axis));
      hi = std::min(axisHi(a, axis), axisHi(b, axis));
    } else {
      lo = std::min(axisHi(a, axis), axisHi(b, axis));
      hi = std::max(axisLo(a, axis), axisLo(b, axis));
    }
    setAxis(zone, axis, lo, hi);
  }
}

PartFace faceOn(int axis, bool atMax) {
  if (axis == 0) return atMax ? PartFace::X1 : PartFace::X0;
  if (axis == 1) return atMax ? PartFace::Y1 : PartFace::Y0;
  return atMax ? PartFace::Z1 : PartFace::Z0;
}

bool oneThinAxis(const WorldBox& a, const WorldBox& b, double nearMm, int& axis) {
  int count = 0;
  axis = -1;
  for (int i = 0; i < 3; ++i) {
    const double sep = separation(a, b, i);
    if (sep > -nearMm && sep <= nearMm) {
      axis = i;
      ++count;
    }
  }
  return count == 1;
}

bool containsSpan(double outerLo, double outerHi, double innerLo, double innerHi) {
  return outerLo <= innerLo && outerHi >= innerHi;
}

bool facesOf(const Component& a, const Component& b, const WorldBox& boxA, const WorldBox& boxB, double nearMm,
             PartFace& faceA, PartFace& faceB) {
  if (!flat(a.placement) || !flat(b.placement)) return false;
  int axis = -1;
  if (!oneThinAxis(boxA, boxB, nearMm, axis)) return false;
  const double aLo = axisLo(boxA, axis);
  const double aHi = axisHi(boxA, axis);
  const double bLo = axisLo(boxB, axis);
  const double bHi = axisHi(boxB, axis);
  if (containsSpan(aLo, aHi, bLo, bHi) || containsSpan(bLo, bHi, aLo, aHi)) return false;
  const double aMid = (aLo + aHi) * 0.5;
  const double bMid = (bLo + bHi) * 0.5;
  if (std::abs(aMid - bMid) <= 1e-6) return false;
  const bool aLow = aMid < bMid;
  faceA = faceOn(axis, aLow);
  faceB = faceOn(axis, !aLow);
  return true;
}

bool clipSlab(double origin, double direction, double lo, double hi, double& tMin, double& tMax) {
  if (std::abs(direction) < 1e-12) return origin >= lo && origin <= hi;
  double first = (lo - origin) / direction;
  double second = (hi - origin) / direction;
  if (first > second) std::swap(first, second);
  tMin = std::max(tMin, first);
  tMax = std::min(tMax, second);
  return tMin <= tMax;
}

}  // namespace

WorldBox worldBox(const Component& component) {
  double matrix[16];
  placementMatrix(component.placement, matrix);
  const LocalBox local = localBox(component);
  WorldBox box;
  bool any = false;
  for (int corner = 0; corner < 8; ++corner) {
    const double x = local.originXmm + ((corner & 1) ? local.sizeXmm : 0.0);
    const double y = local.originYmm + ((corner & 2) ? local.sizeYmm : 0.0);
    const double z = local.originZmm + ((corner & 4) ? local.sizeZmm : 0.0);
    double ox = 0;
    double oy = 0;
    double oz = 0;
    transformPoint(matrix, x, y, z, ox, oy, oz);
    if (!any) {
      box.minX = box.maxX = ox;
      box.minY = box.maxY = oy;
      box.minZ = box.maxZ = oz;
      any = true;
      continue;
    }
    box.minX = std::min(box.minX, ox);
    box.minY = std::min(box.minY, oy);
    box.minZ = std::min(box.minZ, oz);
    box.maxX = std::max(box.maxX, ox);
    box.maxY = std::max(box.maxY, oy);
    box.maxZ = std::max(box.maxZ, oz);
  }
  return box;
}

AxisSnap snapTranslation(const WorldBox& moving, const std::vector<WorldBox>& others, double thresholdMm) {
  AxisSnap snap;
  for (int axis = 0; axis < 3; ++axis) {
    double best = thresholdMm + 1e-9;
    for (const WorldBox& other : others) {
      considerSnap(moving, other, axis, best, snap.deltaMm[axis], snap.used[axis], snap.targetMm[axis],
                   snap.targetEdge[axis]);
    }
  }
  return snap;
}

std::vector<PartContact> partContacts(const std::vector<Component>& parts, double nearMm) {
  std::vector<std::size_t> usable;
  std::vector<WorldBox> boxes;
  usable.reserve(parts.size());
  for (std::size_t index = 0; index < parts.size(); ++index) {
    if (!parts[index].visible || !boardOrBox(parts[index])) continue;
    usable.push_back(index);
    boxes.push_back(worldBox(parts[index]));
  }
  std::vector<PartContact> contacts;
  for (std::size_t a = 0; a < usable.size(); ++a) {
    for (std::size_t b = a + 1; b < usable.size(); ++b) {
      if (!planePair(boxes[a], boxes[b], nearMm)) continue;
      PartContact contact;
      contact.indexA = usable[a];
      contact.indexB = usable[b];
      fillZone(contact.zone, boxes[a], boxes[b]);
      contact.facesKnown =
          facesOf(parts[usable[a]], parts[usable[b]], boxes[a], boxes[b], nearMm, contact.faceA, contact.faceB);
      contacts.push_back(contact);
    }
  }
  return contacts;
}

bool prepareContactJoint(JointType type, const Component& selected, const Component& mate, const ContactFaces& faces,
                         Joint& out, std::string& error) {
  if (!defaultJoint(type, selected, mate, out, error)) return false;
  if (!faces.known) return true;
  out.faceA = out.partA == selected.id ? faces.faceOfSelected : faces.faceOfMate;
  out.faceB = out.partB == selected.id ? faces.faceOfSelected : faces.faceOfMate;
  const Component& partA = out.partA == selected.id ? selected : mate;
  const Component& partB = out.partB == selected.id ? selected : mate;
  return jointFits(out, partA, partB, error);
}

bool rayHitsBox(const double origin[3], const double direction[3], const WorldBox& box, double& tEnter) {
  double tMin = 0;
  double tMax = 1e300;
  const double lo[3] = {box.minX, box.minY, box.minZ};
  const double hi[3] = {box.maxX, box.maxY, box.maxZ};
  for (int axis = 0; axis < 3; ++axis) {
    if (!clipSlab(origin[axis], direction[axis], lo[axis], hi[axis], tMin, tMax)) return false;
  }
  if (tMax < 0) return false;
  tEnter = tMin;
  return true;
}

}  // namespace timberlab
