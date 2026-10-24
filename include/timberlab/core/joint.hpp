#pragma once

#include "timberlab/core/component.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace timberlab {

// Faces use the part's local axes. X is width, Y is thickness, Z is height.
// x0 is the face at the minimum of that axis, x1 the face at the maximum.
enum class JointType {
  MortiseTenon,
  HalfLap,
  Dado,
  Groove,
  Rabbet,
  Bridle,
  Castle,
  Miter,
  Dovetail,
  SlidingDovetail,
  Finger,
  HalfBlindDovetail,
  TongueGroove,
  Dowel,
  HaunchedTenon,
  WedgedTenon,
  LooseTenon,
  CrossLap,
  EndLap,
  SplinedMiter,
  Biscuit,
  PocketScrew
};
enum class PartFace { X0, X1, Y0, Y1, Z0, Z1 };
enum class FaceEdge { U0, U1, V0, V1 };

// U and V are the in-plane axes of a face. normal is 0=X, 1=Y, 2=Z.
struct FaceAxes {
  int normal = 0;
  bool atMax = false;
  int u = 1;
  int v = 2;
};

// One stored joint. Unused fields stay zero for types that do not read them.
// Mortise and tenon: partA is the mortise, partB is the tenon. faceA is the
// mortise face, faceB is the tenon end. thickness is the opening along each
// face's U axis, width along V. The tenon is centered on its end. offset and
// offset2 place the mortise on face A's U and V. length is the tenon length,
// depth is the mortise depth.
// Half lap: length along each end, removeMin* chooses the low-U half.
// Dado: full V slot on faceA of partA. width along U, depth inward, offset on U.
// Groove: stopped slot. length along V, offset2 on V.
// Rabbet: step on edge of faceA. width is the inset from that edge, depth inward.
// Bridle: partA is the open slot, partB is the tongue. thickness is the tongue
// width along U. length is the engagement. The cut runs the full V.
// Castle: the same end, divided into count fingers on partA. thickness is one
// band. count is at least 2.
// Miter: both ends lose a right triangle. length is the inset along the normal.
// removeMin* keeps the point on the low-U side.
// Dovetail: partB carries count tails, partA the pins. thickness is the tail
// width at the tail's end, width is the pin width there, offset is the slope
// on each side. length enters partB, depth enters partA. depth may equal the
// pin board's thickness (through).
// Sliding dovetail: a trapezoid dado on partA and the matching tongue on partB.
// width is the opening at the face, offset2 is the slope on each side, depth is
// both the groove depth and the tongue length.
// Finger: count equal bands fill both U axes. Those axes must be equal, and
// U must divide by count. Part A loses the odd bands, part B the even bands.
// length is the engagement. The cut runs the full V.
// Half-blind dovetail: the same prisms as a through dovetail. depth must stay
// short of the pin board, so the show face remains solid.
// Tongue and groove: part A is the groove, part B the tongue. thickness is the
// tongue width, centered. depth is both the groove depth and the tongue length.
// Dowel: both faces get one centered cylindrical hole. thickness is the
// diameter and must be even, so the radius is an integer. depth may reach the
// far face. The hole stays inside both faces.
// Haunched tenon: a mortise and tenon whose tenon keeps a short haunch beside
// it. width is the tenon along V, and the rest of the tenon end's V is the
// haunch. length2 is the haunch length at the shoulder. The mortise is the
// stepped pocket of both. removeMinB puts the haunch on the low-V side of the
// tenon, removeMinA on the low-V side of the mortise.
// Wedged tenon: a mortise and tenon with count (1 or 2) saw kerfs cut into the
// tenon tip. width2 is the kerf width along V, length2 the kerf depth from the
// tip. The kerfs run across the tenon thickness.
// Loose tenon: both faces get count pockets for a separate tenon, centered on
// U. thickness is the pocket width on U, width its length on V, depth the depth
// in each part. offset is the pitch between pockets when count is above 1.
// Cross lap: both parts lose a full-V slot where they cross. Part A loses
// depth from faceA, part B loses what is left of its own thickness, so the two
// remainders fill one board. Each slot is as wide as the other part's V.
// offset places A's slot on U, offset2 places B's.
// End lap: part A loses a full-V notch on faceA, width along U at offset and
// depth deep. Part B's end on faceB keeps a tongue depth thick over length,
// and loses the rest. removeMinB keeps the high-U side instead of the low.
// Splined miter: a miter, plus a kerf in each mitered face. thickness is the
// kerf along V at offset, depth is its reach perpendicular to the miter face.
// Biscuit: both faces get count disc-shaped kerfs, centered on U. thickness is
// the kerf, width the disc diameter (even), depth the plunge into each part.
// offset is the pitch between kerfs when count is above 1.
// Pocket screw: cuts part A only. faceA is the end face that meets part B and
// edge names the side of that face where the hole enters, so the hole runs
// from that side toward the end at 15 degrees. width is the counterbore
// diameter and thickness the pilot diameter, both even. length is the
// counterbore length along the hole. offset is the distance from the end face
// to the entry point. count holes sit offset2 apart along the other axis.
struct Joint {
  std::string id;
  JointType type = JointType::MortiseTenon;
  std::string partA;
  std::string partB;
  PartFace faceA = PartFace::X1;
  PartFace faceB = PartFace::X0;
  FaceEdge edge = FaceEdge::U0;
  std::int64_t lengthNm = 0;
  std::int64_t widthNm = 0;
  std::int64_t thicknessNm = 0;
  std::int64_t depthNm = 0;
  std::int64_t offsetNm = 0;
  std::int64_t offset2Nm = 0;
  std::int64_t length2Nm = 0;
  std::int64_t width2Nm = 0;
  int count = 0;
  bool removeMinA = false;
  bool removeMinB = false;
};

// A cutter in the part's local frame, nanometres. A box uses x0..z1.
// A prism (prismAxis 0, 1, or 2) extrudes pointCount profile points, which all
// share that axis coordinate, out to prismEnd. The boolean opens a box past an
// outer face. A prism keeps its sloped profile and is extended only outside.
// A cylinder (cylinderAxis 0, 1, or 2) uses radiusNm. The radius is not taken
// from the box, because opening the box for the boolean would widen it. The
// axis may extend past an outer face. That extension stays outside the solid.
// A cylinderAxis of 3 is a free axis: it runs from (x0, y0, z0) to (x1, y1, z1)
// and the caller keeps both ends outside the solid. Nothing is opened.
struct LocalCut {
  std::int64_t x0 = 0;
  std::int64_t y0 = 0;
  std::int64_t z0 = 0;
  std::int64_t x1 = 0;
  std::int64_t y1 = 0;
  std::int64_t z1 = 0;
  int prismAxis = -1;
  int pointCount = 0;
  std::int64_t px[4] = {};
  std::int64_t py[4] = {};
  std::int64_t pz[4] = {};
  std::int64_t prismEnd = 0;
  int cylinderAxis = -1;
  std::int64_t radiusNm = 0;
};

FaceAxes faceAxes(PartFace face);
std::int64_t axisNm(const Component& component, int axis);

const char* jointTypeKey(JointType type);
bool jointTypeFromKey(std::string_view key, JointType& out);
const char* faceKey(PartFace face);
bool faceFromKey(std::string_view key, PartFace& out);
const char* edgeKey(FaceEdge edge);
bool edgeFromKey(std::string_view key, FaceEdge& out);

// True when this joint removes material from that part. A dado, groove, or
// rabbet cuts only partA. The mate is still stored.
bool jointCutsPart(const Joint& joint, const std::string& partId);

bool jointFits(const Joint& joint, const Component& partA, const Component& partB, std::string& error);
// mate is the other part of the joint. Types whose cut depends on the other
// part's extent (cross lap, haunched tenon) read it.
bool cutsForPart(const Joint& joint, const Component& part, const Component& mate, std::vector<LocalCut>& cuts,
                 std::string& error);

// selected is the tenon, the first lap, or the host. mate is the other part.
// Sizes are starting points, not a structural rating.
bool defaultJoint(JointType type, const Component& selected, const Component& mate, Joint& out, std::string& error);

}  // namespace timberlab
