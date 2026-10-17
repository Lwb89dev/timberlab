#include "near.hpp"
#include "timberlab/core/document.hpp"
#include "timberlab/core/joint.hpp"
#include "timberlab/geometry/solids.hpp"
#include "timberlab/units/units.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <string>
#include <vector>

using timberlab::Component;
using timberlab::ComponentType;
using timberlab::Document;
using timberlab::FaceEdge;
using timberlab::Joint;
using timberlab::JointType;
using timberlab::PartFace;
using timberlab::geometry::MeshQuality;
using timberlab::geometry::PointSide;
using timberlab::geometry::classifyLocal;
using timberlab::geometry::meshFor;
using timberlab::geometry::probeSolid;

namespace {

constexpr std::int64_t kMm = timberlab::units::kNmPerMm;

Component board(const std::string& id, std::int64_t width, std::int64_t thickness, std::int64_t height) {
  Component component;
  component.id = id;
  component.type = ComponentType::Board;
  component.name = id;
  component.materialId = "european-oak";
  component.widthNm = width;
  component.thicknessNm = thickness;
  component.heightNm = height;
  return component;
}

void expectVolume(const Component& part, const std::vector<Joint>& joints, const std::vector<Component>& parts,
                  double expected) {
  const auto probe = probeSolid(part, joints, parts);
  INFO(probe.error);
  INFO(probe.volumeMm3);
  REQUIRE(probe.ok);
  REQUIRE(std::abs(probe.volumeMm3 - expected) < 1.0);
}

}  // namespace

TEST_CASE("mortise and tenon cut both solids and follow a thickness change") {
  Component mortise = board("mortise", 100 * kMm, 30 * kMm, 80 * kMm);
  Component tenon = board("tenon", 40 * kMm, 18 * kMm, 40 * kMm);
  Joint joint;
  joint.id = "mt";
  joint.type = JointType::MortiseTenon;
  joint.partA = mortise.id;
  joint.partB = tenon.id;
  joint.faceA = PartFace::X1;
  joint.faceB = PartFace::X0;
  joint.thicknessNm = 6 * kMm;
  joint.widthNm = 20 * kMm;
  joint.lengthNm = 15 * kMm;
  joint.depthNm = 12 * kMm;
  joint.offsetNm = 10 * kMm;
  joint.offset2Nm = 20 * kMm;
  std::vector<Component> parts{mortise, tenon};
  const std::vector<Joint> joints{joint};

  REQUIRE(classifyLocal(tenon, joints, parts, 5, 3, 20) == PointSide::Out);
  REQUIRE(classifyLocal(tenon, joints, parts, 5, 9, 20) == PointSide::In);
  REQUIRE(classifyLocal(mortise, joints, parts, 94, 13, 30) == PointSide::Out);
  REQUIRE(classifyLocal(mortise, joints, parts, 50, 13, 30) == PointSide::In);
  expectVolume(tenon, joints, parts, 40.0 * 18.0 * 40.0 - 15.0 * (18.0 * 40.0 - 6.0 * 20.0));
  expectVolume(mortise, joints, parts, 100.0 * 30.0 * 80.0 - 12.0 * 6.0 * 20.0);

  const auto meshed = meshFor(tenon, joints, parts, MeshQuality::Standard);
  const auto probed = probeSolid(tenon, joints, parts);
  REQUIRE(meshed.ok);
  REQUIRE(probed.ok);
  REQUIRE(nearRelative(meshed.volumeMm3, probed.volumeMm3, 1e-6));

  tenon.thicknessNm = 30 * kMm;
  parts[1] = tenon;
  REQUIRE(classifyLocal(tenon, joints, parts, 5, 9, 20) == PointSide::Out);
  REQUIRE(classifyLocal(tenon, joints, parts, 5, 15, 20) == PointSide::In);
  expectVolume(tenon, joints, parts, 40.0 * 30.0 * 40.0 - 15.0 * (30.0 * 40.0 - 6.0 * 20.0));

  Joint unfit = joint;
  unfit.thicknessNm = 100 * kMm;
  const auto rejected = probeSolid(tenon, {unfit}, parts);
  REQUIRE_FALSE(rejected.ok);
  REQUIRE(rejected.error == "joint.parameter");
}

TEST_CASE("half lap removes a complementary half from each part") {
  const Component partA = board("a", 80 * kMm, 20 * kMm, 40 * kMm);
  const Component partB = board("b", 80 * kMm, 20 * kMm, 40 * kMm);
  Joint joint;
  joint.id = "lap";
  joint.type = JointType::HalfLap;
  joint.partA = partA.id;
  joint.partB = partB.id;
  joint.faceA = PartFace::X0;
  joint.faceB = PartFace::X0;
  joint.lengthNm = 20 * kMm;
  joint.removeMinA = false;
  joint.removeMinB = true;
  const std::vector<Component> parts{partA, partB};
  const std::vector<Joint> joints{joint};
  const double expected = 80.0 * 20.0 * 40.0 - 20.0 * 10.0 * 40.0;

  expectVolume(partA, joints, parts, expected);
  expectVolume(partB, joints, parts, expected);
  REQUIRE(classifyLocal(partA, joints, parts, 5, 15, 20) == PointSide::Out);
  REQUIRE(classifyLocal(partA, joints, parts, 5, 5, 20) == PointSide::In);
  REQUIRE(classifyLocal(partA, joints, parts, 30, 15, 20) == PointSide::In);
  REQUIRE(classifyLocal(partB, joints, parts, 5, 5, 20) == PointSide::Out);
  REQUIRE(classifyLocal(partB, joints, parts, 5, 15, 20) == PointSide::In);
}

TEST_CASE("dado and groove cut the host and leave the mate whole") {
  Component host = board("host", 400 * kMm, 200 * kMm, 18 * kMm);
  Component mate = board("mate", 200 * kMm, 18 * kMm, 100 * kMm);
  Component pin;
  pin.id = "pin";
  pin.type = ComponentType::Cylinder;
  pin.name = "pin";
  pin.materialId = "european-oak";
  pin.radiusNm = 10 * kMm;
  pin.heightNm = 50 * kMm;

  Joint dado;
  dado.id = "dado";
  dado.type = JointType::Dado;
  dado.partA = host.id;
  dado.partB = mate.id;
  dado.faceA = PartFace::Z1;
  dado.widthNm = 18 * kMm;
  dado.offsetNm = 100 * kMm;
  dado.depthNm = 6 * kMm;
  std::vector<Component> parts{host, mate, pin};
  expectVolume(host, {dado}, parts, 400.0 * 200.0 * 18.0 - 18.0 * 200.0 * 6.0);
  expectVolume(mate, {dado}, parts, 200.0 * 18.0 * 100.0);
  const auto plainPin = meshFor(pin, MeshQuality::Standard);
  const auto joinedPin = probeSolid(pin, {dado}, parts);
  REQUIRE(plainPin.ok);
  REQUIRE(joinedPin.ok);
  REQUIRE(nearRelative(joinedPin.volumeMm3, plainPin.volumeMm3, 1e-6));
  REQUIRE(classifyLocal(host, {dado}, parts, 109, 100, 15) == PointSide::Out);
  REQUIRE(classifyLocal(host, {dado}, parts, 50, 100, 15) == PointSide::In);

  Joint groove;
  groove.id = "groove";
  groove.type = JointType::Groove;
  groove.partA = host.id;
  groove.partB = mate.id;
  groove.faceA = PartFace::Z1;
  groove.offsetNm = 20 * kMm;
  groove.widthNm = 10 * kMm;
  groove.offset2Nm = 15 * kMm;
  groove.lengthNm = 30 * kMm;
  groove.depthNm = 5 * kMm;
  host.widthNm = 200 * kMm;
  host.thicknessNm = 80 * kMm;
  parts[0] = host;
  expectVolume(host, {groove}, parts, 200.0 * 80.0 * 18.0 - 10.0 * 30.0 * 5.0);
  expectVolume(mate, {groove}, parts, 200.0 * 18.0 * 100.0);
  REQUIRE(classifyLocal(host, {groove}, parts, 25, 30, 16) == PointSide::Out);
  REQUIRE(classifyLocal(host, {groove}, parts, 10, 30, 16) == PointSide::In);
  REQUIRE(classifyLocal(host, {groove}, parts, 2, 2, 16) == PointSide::In);
}

TEST_CASE("a rabbet removes one edge and leaves the face interior") {
  const Component host = board("host", 200 * kMm, 80 * kMm, 18 * kMm);
  const Component mate = board("mate", 80 * kMm, 18 * kMm, 40 * kMm);
  Joint joint;
  joint.id = "rabbet";
  joint.type = JointType::Rabbet;
  joint.partA = host.id;
  joint.partB = mate.id;
  joint.faceA = PartFace::Z1;
  joint.edge = FaceEdge::U0;
  joint.widthNm = 8 * kMm;
  joint.depthNm = 5 * kMm;
  const std::vector<Component> parts{host, mate};
  expectVolume(host, {joint}, parts, 200.0 * 80.0 * 18.0 - 8.0 * 80.0 * 5.0);
  expectVolume(mate, {joint}, parts, 80.0 * 18.0 * 40.0);
  REQUIRE(classifyLocal(host, {joint}, parts, 3, 40, 16) == PointSide::Out);
  REQUIRE(classifyLocal(host, {joint}, parts, 40, 40, 16) == PointSide::In);
}

TEST_CASE("joints survive save text, undo, lock, and part deletion") {
  Document document;
  const std::string shelf = document.addBoard();
  const std::string rail = document.addBoard();
  std::string error;
  nlohmann::json plain = nlohmann::json::parse(document.toJson());
  REQUIRE_FALSE(plain.contains("joints"));

  REQUIRE(document.setLocked(shelf, true, error));
  REQUIRE(document.addJoint("joint.mortise_tenon", rail, shelf, error).empty());
  REQUIRE(error == "component.locked");
  REQUIRE(document.joints().empty());
  REQUIRE(document.setLocked(shelf, false, error));

  const std::string jointId = document.addJoint("joint.mortise_tenon", rail, shelf, error);
  REQUIRE_FALSE(jointId.empty());
  REQUIRE(document.joints().size() == 1);
  REQUIRE(document.joints().front().partA == shelf);
  REQUIRE(document.joints().front().partB == rail);
  const Component* cutRail = document.find(rail);
  const auto cut = probeSolid(*cutRail, document.joints(), document.components());
  REQUIRE(cut.ok);
  const double plainRail = 600.0 * 18.0 * 720.0;
  REQUIRE(cut.volumeMm3 < plainRail - 1.0);

  const std::string snapshot = document.toJson();
  REQUIRE_FALSE(document.setJointParameter(jointId, "thickness", 1000 * kMm, error));
  REQUIRE(error == "joint.parameter");
  REQUIRE(document.toJson() == snapshot);
  REQUIRE_FALSE(document.setJointParameter(jointId, "angle", kMm, error));
  REQUIRE(error == "joint.parameter");
  REQUIRE_FALSE(document.setJointParameter("missing", "length", kMm, error));
  REQUIRE(error == "joint.missing");

  Document loaded;
  REQUIRE(Document::fromJson(snapshot, loaded, error));
  REQUIRE(loaded.joints().size() == 1);
  REQUIRE(loaded.joints().front().id == jointId);
  REQUIRE(loaded.joints().front().lengthNm == document.joints().front().lengthNm);
  REQUIRE(loaded.joints().front().thicknessNm == document.joints().front().thicknessNm);
  REQUIRE(loaded.joints().front().widthNm == document.joints().front().widthNm);
  REQUIRE(loaded.joints().front().depthNm == document.joints().front().depthNm);
  REQUIRE(loaded.joints().front().offsetNm == document.joints().front().offsetNm);
  REQUIRE(loaded.joints().front().offset2Nm == document.joints().front().offset2Nm);

  const std::string previousName = document.name();
  REQUIRE(document.setProjectName("Joined", error));
  REQUIRE(document.undo(error));
  REQUIRE(document.name() == previousName);
  REQUIRE(document.joints().size() == 1);
  REQUIRE(document.undo(error));
  REQUIRE(document.joints().empty());
  const auto restored = probeSolid(*document.find(rail), document.joints(), document.components());
  REQUIRE(restored.ok);
  REQUIRE(std::abs(restored.volumeMm3 - plainRail) < 1.0);

  const std::string again = document.addJoint("joint.half_lap", shelf, rail, error);
  REQUIRE_FALSE(again.empty());
  REQUIRE(document.joints().front().removeMinB);
  REQUIRE_FALSE(document.joints().front().removeMinA);
  Document lapped;
  REQUIRE(Document::fromJson(document.toJson(), lapped, error));
  REQUIRE(lapped.joints().size() == 1);
  REQUIRE(lapped.joints().front().type == JointType::HalfLap);
  REQUIRE(lapped.joints().front().removeMinB);
  REQUIRE_FALSE(lapped.joints().front().removeMinA);

  const auto lappedProbe = probeSolid(*document.find(shelf), document.joints(), document.components());
  REQUIRE(lappedProbe.ok);
  REQUIRE(lappedProbe.volumeMm3 < plainRail - 1.0);
  REQUIRE(document.removeJoint(again, error));
  const auto cleared = probeSolid(*document.find(shelf), document.joints(), document.components());
  REQUIRE(cleared.ok);
  REQUIRE(std::abs(cleared.volumeMm3 - plainRail) < 1.0);

  const std::string kept = document.addJoint("joint.dado", shelf, rail, error);
  REQUIRE_FALSE(kept.empty());
  REQUIRE(document.remove(rail, error));
  REQUIRE(document.joints().empty());
  REQUIRE(document.find(shelf) != nullptr);
  REQUIRE(document.find(rail) == nullptr);

  nlohmann::json broken = nlohmann::json::parse(snapshot);
  broken["joints"][0]["thickness"] = 1000 * kMm;
  Document rejected;
  rejected.setProjectName("untouched", error);
  REQUIRE_FALSE(Document::fromJson(broken.dump(), rejected, error));
  REQUIRE(error == "joint.parameter");
  REQUIRE(rejected.name() == "untouched");

  nlohmann::json duplicated = nlohmann::json::parse(snapshot);
  duplicated["joints"].push_back(duplicated["joints"][0]);
  REQUIRE_FALSE(Document::fromJson(duplicated.dump(), rejected, error));
  REQUIRE(error == "storage.duplicate");

  nlohmann::json flag = nlohmann::json::parse(snapshot);
  flag["joints"][0]["removeMinA"] = "yes";
  REQUIRE_FALSE(Document::fromJson(flag.dump(), rejected, error));
  REQUIRE(error == "storage.json");

  nlohmann::json orphan = nlohmann::json::parse(snapshot);
  orphan["joints"][0]["partA"] = "missing-part";
  REQUIRE_FALSE(Document::fromJson(orphan.dump(), rejected, error));
  REQUIRE(error == "component.missing");
}
