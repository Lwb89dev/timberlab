#include "near.hpp"
#include "timberlab/core/document.hpp"
#include "timberlab/core/transform.hpp"
#include "timberlab/units/units.hpp"

#include <catch2/catch_test_macros.hpp>

using timberlab::Component;
using timberlab::Document;
using timberlab::LocalBox;
using timberlab::Placement;
using timberlab::localBox;

namespace {

bool overlaps(const Component& a, const Component& b) {
  const LocalBox boxA = localBox(a);
  const LocalBox boxB = localBox(b);
  const double ax0 = timberlab::units::toMillimetres(a.placement.xNm) + boxA.originXmm;
  const double ay0 = timberlab::units::toMillimetres(a.placement.yNm) + boxA.originYmm;
  const double az0 = timberlab::units::toMillimetres(a.placement.zNm) + boxA.originZmm;
  const double bx0 = timberlab::units::toMillimetres(b.placement.xNm) + boxB.originXmm;
  const double by0 = timberlab::units::toMillimetres(b.placement.yNm) + boxB.originYmm;
  const double bz0 = timberlab::units::toMillimetres(b.placement.zNm) + boxB.originZmm;
  if (ax0 + boxA.sizeXmm <= bx0 || bx0 + boxB.sizeXmm <= ax0) return false;
  if (ay0 + boxA.sizeYmm <= by0 || by0 + boxB.sizeYmm <= ay0) return false;
  if (az0 + boxA.sizeZmm <= bz0 || bz0 + boxB.sizeZmm <= az0) return false;
  return true;
}

}  // namespace

TEST_CASE("unit display does not change stored nanometres") {
  Document document;
  const std::string id = document.addBoard();
  std::string error;
  REQUIRE(document.setDisplayUnit("in", error));
  REQUIRE_FALSE(document.setDimension(id, "width", 0, error));
  REQUIRE(error == "dimension.non_positive");
  const auto threeQuarter = timberlab::units::parseLength("3/4\"", timberlab::units::LengthUnit::Inch);
  REQUIRE(document.setDimension(id, "thickness", threeQuarter.nm, error));
  REQUIRE(document.find(id)->thicknessNm == 19'050'000);
  REQUIRE(document.setDisplayUnit("mm", error));
  REQUIRE(document.find(id)->thicknessNm == 19'050'000);
  REQUIRE(timberlab::units::formatLength(document.find(id)->thicknessNm, timberlab::units::LengthUnit::Millimetre, 16) ==
          "19.05 mm");

  REQUIRE(document.undo(error));
  REQUIRE(document.find(id)->thicknessNm == 19'050'000);
  REQUIRE(document.undo(error));
  REQUIRE(document.find(id)->thicknessNm != 19'050'000);
  REQUIRE(document.redo(error));
  REQUIRE(document.find(id)->thicknessNm == 19'050'000);
}

TEST_CASE("lock blocks edits and still allows visibility") {
  Document document;
  const std::string id = document.addBoard();
  std::string error;
  REQUIRE(document.setLocked(id, true, error));
  REQUIRE_FALSE(document.setDimension(id, "width", 100 * timberlab::units::kNmPerMm, error));
  REQUIRE(error == "component.locked");
  REQUIRE(document.setVisible(id, false, error));
  REQUIRE_FALSE(document.find(id)->visible);
  std::string copyId;
  REQUIRE(document.duplicate(id, copyId, error));
  REQUIRE_FALSE(document.find(copyId)->locked);
  REQUIRE(document.find(copyId)->placement.xNm > document.find(id)->placement.xNm);
}

TEST_CASE("json round trip keeps several parts") {
  Document document;
  document.addBoard();
  document.addBox();
  document.addCylinder();
  std::string error;
  REQUIRE(document.setProjectName("Round trip", error));
  Document loaded;
  REQUIRE(Document::fromJson(document.toJson(), loaded, error));
  REQUIRE(loaded.name() == "Round trip");
  REQUIRE(loaded.components().size() == 3);
  REQUIRE(loaded.components()[0].widthNm == document.components()[0].widthNm);
  REQUIRE(loaded.components()[2].radiusNm == document.components()[2].radiusNm);
}

TEST_CASE("malformed model json is rejected") {
  Document loaded;
  std::string error;
  REQUIRE_FALSE(Document::fromJson("{", loaded, error));
  REQUIRE_FALSE(Document::fromJson("{\"schema\":\"other\"}", loaded, error));
}

TEST_CASE("demo table parts touch without overlapping") {
  Document document;
  document.makeDemoTable();
  REQUIRE(document.components().size() == 9);
  for (std::size_t i = 0; i < document.components().size(); ++i) {
    for (std::size_t j = i + 1; j < document.components().size(); ++j) {
      REQUIRE_FALSE(overlaps(document.components()[i], document.components()[j]));
    }
  }
}

TEST_CASE("a right angle about Z sends X toward Y") {
  double matrix[16];
  Placement placement;
  placement.rzDeg = 90;
  timberlab::placementMatrix(placement, matrix);
  double x = 0;
  double y = 0;
  double z = 0;
  timberlab::transformPoint(matrix, 1, 0, 0, x, y, z);
  REQUIRE(nearRelative(x, 0, 1e-9));
  REQUIRE(nearRelative(y, 1, 1e-9));
  REQUIRE(nearRelative(z, 0, 1e-9));
}
