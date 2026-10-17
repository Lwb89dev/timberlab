#include "near.hpp"
#include "timberlab/geometry/solids.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("a box solid has the requested volume and bounds") {
  const auto mesh = timberlab::geometry::makeBoxMesh(100, 18, 200, timberlab::geometry::MeshQuality::Standard);
  REQUIRE(mesh.ok);
  REQUIRE(nearRelative(mesh.volumeMm3, 100.0 * 18.0 * 200.0, 1e-6));
  REQUIRE(nearRelative(mesh.bboxMax[0] - mesh.bboxMin[0], 100, 1e-6));
  REQUIRE(nearRelative(mesh.bboxMax[1] - mesh.bboxMin[1], 18, 1e-6));
  REQUIRE(nearRelative(mesh.bboxMax[2] - mesh.bboxMin[2], 200, 1e-6));
  REQUIRE(mesh.indices.size() >= 12);
  REQUIRE(mesh.interleaved.size() == mesh.indices.size() * 6);
}

TEST_CASE("a finer cylinder tessellation has more triangles and the same solid volume") {
  const auto coarse = timberlab::geometry::makeCylinderMesh(40, 120, timberlab::geometry::MeshQuality::Draft);
  const auto fine = timberlab::geometry::makeCylinderMesh(40, 120, timberlab::geometry::MeshQuality::Fine);
  REQUIRE(coarse.ok);
  REQUIRE(fine.ok);
  const double expected = 3.141592653589793 * 40.0 * 40.0 * 120.0;
  REQUIRE(nearRelative(fine.volumeMm3, expected, 1e-4));
  REQUIRE(nearRelative(coarse.volumeMm3, expected, 1e-4));
  REQUIRE(fine.indices.size() > coarse.indices.size());
}

TEST_CASE("degenerate solids are rejected") {
  const auto bad = timberlab::geometry::makeBoxMesh(0, 18, 200, timberlab::geometry::MeshQuality::Draft);
  REQUIRE_FALSE(bad.ok);
  REQUIRE(bad.error == "geometry.degenerate");
}
