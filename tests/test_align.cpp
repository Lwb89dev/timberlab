#include "near.hpp"
#include "timberlab/core/align.hpp"
#include "timberlab/core/document.hpp"
#include "timberlab/units/units.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>

using timberlab::Component;
using timberlab::ComponentType;
using timberlab::Document;
using timberlab::Placement;
using timberlab::WorldBox;
using timberlab::localBox;
using timberlab::units::kNmPerMm;

namespace {

Component makeBoard(std::int64_t widthMm, std::int64_t thicknessMm, std::int64_t heightMm,
                    const Placement& placement) {
  Component component;
  component.type = ComponentType::Board;
  component.widthNm = widthMm * kNmPerMm;
  component.thicknessNm = thicknessMm * kNmPerMm;
  component.heightNm = heightMm * kNmPerMm;
  component.placement = placement;
  return component;
}

}  // namespace

TEST_CASE("worldBox matches an axis-aligned board") {
  Placement placement;
  placement.xNm = 10 * kNmPerMm;
  placement.yNm = 20 * kNmPerMm;
  placement.zNm = 30 * kNmPerMm;
  const Component board = makeBoard(100, 18, 200, placement);
  const WorldBox box = timberlab::worldBox(board);
  REQUIRE(nearRelative(box.minX, 10.0, 1e-6));
  REQUIRE(nearRelative(box.maxX, 110.0, 1e-6));
  REQUIRE(nearRelative(box.minY, 20.0, 1e-6));
  REQUIRE(nearRelative(box.maxY, 38.0, 1e-6));
  REQUIRE(nearRelative(box.minZ, 30.0, 1e-6));
  REQUIRE(nearRelative(box.maxZ, 230.0, 1e-6));
}

TEST_CASE("snapTranslation finds the closest edge") {
  WorldBox moving{0, 0, 0, 100, 18, 200};
  const std::vector<WorldBox> others{{105, 30, 210, 205, 48, 410}};
  const timberlab::AxisSnap snap = timberlab::snapTranslation(moving, others, 8.0);
  REQUIRE(snap.used[0]);
  REQUIRE_FALSE(snap.used[1]);
  REQUIRE_FALSE(snap.used[2]);
  REQUIRE(nearRelative(snap.deltaMm[0], 5.0, 1e-6));
}

TEST_CASE("snapTranslation ignores targets beyond the threshold") {
  WorldBox moving{0, 0, 0, 100, 18, 200};
  const std::vector<WorldBox> others{{120, 30, 210, 220, 48, 410}};
  const timberlab::AxisSnap snap = timberlab::snapTranslation(moving, others, 8.0);
  REQUIRE_FALSE(snap.used[0]);
  REQUIRE_FALSE(snap.used[1]);
  REQUIRE_FALSE(snap.used[2]);
}

TEST_CASE("partContacts finds two overlapping boards") {
  Placement a;
  a.xNm = 0;
  a.yNm = 0;
  a.zNm = 0;
  Placement b;
  b.xNm = 0;
  b.yNm = 19 * kNmPerMm;
  b.zNm = 0;
  const std::vector<Component> parts = {makeBoard(100, 18, 200, a), makeBoard(100, 18, 200, b)};
  const auto contacts = timberlab::partContacts(parts, 8.0);
  REQUIRE(contacts.size() == 1);
  REQUIRE(contacts[0].indexA == 0);
  REQUIRE(contacts[0].indexB == 1);
  REQUIRE(contacts[0].facesKnown);
}

TEST_CASE("partContacts ignores boards separated on the thin axis") {
  Placement a;
  a.yNm = 0;
  Placement b;
  b.yNm = 50 * kNmPerMm;
  const std::vector<Component> parts = {makeBoard(100, 18, 200, a), makeBoard(100, 18, 200, b)};
  const auto contacts = timberlab::partContacts(parts, 8.0);
  REQUIRE(contacts.empty());
}

TEST_CASE("prepareContactJoint accepts a default mortise-and-tenon pair") {
  Document document;
  const std::string selectedId = document.addBoard();
  const std::string mateId = document.addBoard();
  std::string error;
  REQUIRE(document.setDimension(selectedId, "width", 100 * kNmPerMm, error));
  REQUIRE(document.setDimension(selectedId, "thickness", 18 * kNmPerMm, error));
  REQUIRE(document.setDimension(selectedId, "height", 200 * kNmPerMm, error));
  REQUIRE(document.setDimension(mateId, "width", 100 * kNmPerMm, error));
  REQUIRE(document.setDimension(mateId, "thickness", 18 * kNmPerMm, error));
  REQUIRE(document.setDimension(mateId, "height", 200 * kNmPerMm, error));
  const Component* selected = document.find(selectedId);
  const Component* mate = document.find(mateId);
  REQUIRE(selected != nullptr);
  REQUIRE(mate != nullptr);
  timberlab::Joint joint;
  const timberlab::ContactFaces faces;
  REQUIRE(timberlab::prepareContactJoint(timberlab::JointType::MortiseTenon, *selected, *mate, faces, joint, error));
  REQUIRE_FALSE(joint.partA.empty());
  REQUIRE_FALSE(joint.partB.empty());
}

TEST_CASE("rayHitsBox hits a box aligned with the ray") {
  const WorldBox box{0, 0, 0, 100, 18, 200};
  const double origin[3] = {-50, 9, 100};
  const double direction[3] = {1, 0, 0};
  double tEnter = 0;
  REQUIRE(timberlab::rayHitsBox(origin, direction, box, tEnter));
  REQUIRE(nearRelative(tEnter, 50.0, 1e-6));
}

TEST_CASE("rayHitsBox misses a box behind the ray") {
  const WorldBox box{0, 0, 0, 100, 18, 200};
  const double origin[3] = {150, 9, 100};
  const double direction[3] = {1, 0, 0};
  double tEnter = 0;
  REQUIRE_FALSE(timberlab::rayHitsBox(origin, direction, box, tEnter));
}
