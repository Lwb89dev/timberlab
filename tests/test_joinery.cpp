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
using timberlab::jointTypeKey;
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

TEST_CASE("a bridle cuts a centered slot and two cheeks") {
  Component female = board("female", 80 * kMm, 40 * kMm, 30 * kMm);
  Component male = board("male", 50 * kMm, 40 * kMm, 30 * kMm);
  Joint joint;
  joint.type = JointType::Bridle;
  joint.partA = female.id;
  joint.partB = male.id;
  joint.faceA = PartFace::X1;
  joint.faceB = PartFace::X0;
  joint.lengthNm = 15 * kMm;
  joint.thicknessNm = 10 * kMm;
  std::vector<Component> parts{female, male};
  const std::vector<Joint> joints{joint};
  expectVolume(female, joints, parts, 80.0 * 40.0 * 30.0 - 15.0 * 10.0 * 30.0);
  expectVolume(male, joints, parts, 50.0 * 40.0 * 30.0 - 15.0 * 30.0 * 30.0);
}

TEST_CASE("a castle joint alternates equal fingers") {
  Component female = board("female", 80 * kMm, 40 * kMm, 30 * kMm);
  Component male = board("male", 50 * kMm, 40 * kMm, 30 * kMm);
  Joint joint;
  joint.type = JointType::Castle;
  joint.partA = female.id;
  joint.partB = male.id;
  joint.faceA = PartFace::X1;
  joint.faceB = PartFace::X0;
  joint.count = 3;
  joint.lengthNm = 15 * kMm;
  joint.thicknessNm = 8 * kMm;
  std::vector<Component> parts{female, male};
  const std::vector<Joint> joints{joint};
  expectVolume(female, joints, parts, 80.0 * 40.0 * 30.0 - 2.0 * 15.0 * 8.0 * 30.0);
  expectVolume(male, joints, parts, 50.0 * 40.0 * 30.0 - 3.0 * 15.0 * 8.0 * 30.0);
}

TEST_CASE("a miter removes a right triangle and keeps the low corner") {
  Component part = board("rail", 80 * kMm, 20 * kMm, 40 * kMm);
  Joint joint;
  joint.type = JointType::Miter;
  joint.partA = part.id;
  joint.partB = "other";
  joint.faceA = PartFace::X0;
  joint.lengthNm = 20 * kMm;
  joint.removeMinA = true;
  Component other = board("other", 80 * kMm, 20 * kMm, 40 * kMm);
  joint.faceB = PartFace::X0;
  joint.removeMinB = true;
  std::vector<Component> parts{part, other};
  const std::vector<Joint> joints{joint};
  expectVolume(part, joints, parts, 80.0 * 20.0 * 40.0 - 0.5 * 20.0 * 20.0 * 40.0);
  REQUIRE(classifyLocal(part, joints, parts, 2, 10, 20) == PointSide::Out);
  REQUIRE(classifyLocal(part, joints, parts, 10, 2, 20) == PointSide::In);
}

TEST_CASE("a through dovetail cuts pins and tails as trapezoids") {
  Component pins = board("pins", 40 * kMm, 48 * kMm, 20 * kMm);
  Component tails = board("tails", 40 * kMm, 48 * kMm, 20 * kMm);
  Joint joint;
  joint.type = JointType::Dovetail;
  joint.partA = pins.id;
  joint.partB = tails.id;
  joint.faceA = PartFace::X1;
  joint.faceB = PartFace::X0;
  joint.count = 2;
  joint.thicknessNm = 12 * kMm;
  joint.widthNm = 12 * kMm;
  joint.offsetNm = 2 * kMm;
  joint.lengthNm = 10 * kMm;
  joint.depthNm = 10 * kMm;
  std::vector<Component> parts{pins, tails};
  const std::vector<Joint> joints{joint};
  expectVolume(tails, joints, parts, 40.0 * 48.0 * 20.0 - 5600.0);
  expectVolume(pins, joints, parts, 40.0 * 48.0 * 20.0 - 4000.0);
}

TEST_CASE("a sliding dovetail cuts a flared dado and two shoulders") {
  Component host = board("host", 100 * kMm, 30 * kMm, 40 * kMm);
  Component tongue = board("tongue", 40 * kMm, 20 * kMm, 30 * kMm);
  Joint joint;
  joint.type = JointType::SlidingDovetail;
  joint.partA = host.id;
  joint.partB = tongue.id;
  joint.faceA = PartFace::Z1;
  joint.faceB = PartFace::X0;
  joint.widthNm = 10 * kMm;
  joint.offsetNm = 20 * kMm;
  joint.offset2Nm = 2 * kMm;
  joint.depthNm = 8 * kMm;
  std::vector<Component> parts{host, tongue};
  const std::vector<Joint> joints{joint};
  expectVolume(host, joints, parts, 100.0 * 30.0 * 40.0 - 2880.0);
  expectVolume(tongue, joints, parts, 40.0 * 20.0 * 30.0 - 1920.0);
  REQUIRE(classifyLocal(host, joints, parts, 25, 15, 39) == PointSide::Out);
  REQUIRE(classifyLocal(host, joints, parts, 10, 15, 39) == PointSide::In);
  REQUIRE(classifyLocal(tongue, joints, parts, 2, 1, 15) == PointSide::Out);
  REQUIRE(classifyLocal(tongue, joints, parts, 2, 10, 15) == PointSide::In);
}

TEST_CASE("starting sizes of the new joints fit two default boards") {
  const char* keys[] = {"joint.bridle",
                        "joint.castle",
                        "joint.miter",
                        "joint.dovetail",
                        "joint.sliding_dovetail",
                        "joint.finger",
                        "joint.half_blind_dovetail",
                        "joint.tongue_groove",
                        "joint.dowel"};
  for (const char* key : keys) {
    Document document;
    std::string error;
    const std::string selected = document.addBoard();
    const std::string mate = document.addBoard();
    const std::string id = document.addJoint(key, selected, mate, error);
    INFO(key);
    INFO(error);
    REQUIRE_FALSE(id.empty());
    const auto cutA = probeSolid(*document.find(selected), document.joints(), document.components());
    const auto cutB = probeSolid(*document.find(mate), document.joints(), document.components());
    REQUIRE(cutA.ok);
    REQUIRE(cutB.ok);
    REQUIRE(cutA.volumeMm3 < 600.0 * 18.0 * 720.0 - 0.5);
    REQUIRE(cutB.volumeMm3 < 600.0 * 18.0 * 720.0 - 0.5);
    Document loaded;
    REQUIRE(Document::fromJson(document.toJson(), loaded, error));
    REQUIRE(loaded.joints().size() == 1);
    REQUIRE(jointTypeKey(loaded.joints().front().type) == std::string(key));
  }
}

TEST_CASE("castle and dovetail require a count and reload it") {
  Document document;
  std::string error;
  const std::string selected = document.addBoard();
  const std::string mate = document.addBoard();
  REQUIRE_FALSE(document.addJoint("joint.castle", selected, mate, error).empty());
  REQUIRE(document.joints().front().count == 3);
  Document loaded;
  REQUIRE(Document::fromJson(document.toJson(), loaded, error));
  REQUIRE(loaded.joints().front().count == 3);

  nlohmann::json missing = nlohmann::json::parse(document.toJson());
  missing["joints"][0].erase("count");
  REQUIRE_FALSE(Document::fromJson(missing.dump(), loaded, error));
  REQUIRE(error == "storage.json");

  for (const char* key : {"joint.finger", "joint.half_blind_dovetail"}) {
    Document fresh;
    const std::string partA = fresh.addBoard();
    const std::string partB = fresh.addBoard();
    REQUIRE_FALSE(fresh.addJoint(key, partA, partB, error).empty());
    nlohmann::json body = nlohmann::json::parse(fresh.toJson());
    body["joints"][0].erase("count");
    Document rejected;
    REQUIRE_FALSE(Document::fromJson(body.dump(), rejected, error));
    REQUIRE(error == "storage.json");
  }
}

TEST_CASE("a finger joint removes alternating full-width bands") {
  Component partA = board("female", 80 * kMm, 40 * kMm, 30 * kMm);
  Component partB = board("male", 80 * kMm, 40 * kMm, 30 * kMm);
  Joint joint;
  joint.type = JointType::Finger;
  joint.partA = partA.id;
  joint.partB = partB.id;
  joint.faceA = PartFace::X1;
  joint.faceB = PartFace::X0;
  joint.count = 4;
  joint.lengthNm = 15 * kMm;
  std::vector<Component> parts{partA, partB};
  const std::vector<Joint> joints{joint};
  expectVolume(partA, joints, parts, 80.0 * 40.0 * 30.0 - 9000.0);
  expectVolume(partB, joints, parts, 80.0 * 40.0 * 30.0 - 9000.0);
  REQUIRE(classifyLocal(partA, joints, parts, 70, 15, 15) == PointSide::Out);
  REQUIRE(classifyLocal(partA, joints, parts, 70, 5, 15) == PointSide::In);
  REQUIRE(classifyLocal(partB, joints, parts, 5, 5, 15) == PointSide::Out);
  REQUIRE(classifyLocal(partB, joints, parts, 5, 15, 15) == PointSide::In);
}

TEST_CASE("a half-blind dovetail stops before the show face") {
  Component pins = board("pins", 40 * kMm, 48 * kMm, 20 * kMm);
  Component tails = board("tails", 40 * kMm, 48 * kMm, 20 * kMm);
  Joint joint;
  joint.type = JointType::HalfBlindDovetail;
  joint.partA = pins.id;
  joint.partB = tails.id;
  joint.faceA = PartFace::X1;
  joint.faceB = PartFace::X0;
  joint.count = 2;
  joint.thicknessNm = 12 * kMm;
  joint.widthNm = 12 * kMm;
  joint.offsetNm = 2 * kMm;
  joint.lengthNm = 10 * kMm;
  joint.depthNm = 10 * kMm;
  std::vector<Component> parts{pins, tails};
  const std::vector<Joint> joints{joint};
  expectVolume(tails, joints, parts, 40.0 * 48.0 * 20.0 - 5600.0);
  expectVolume(pins, joints, parts, 40.0 * 48.0 * 20.0 - 4000.0);
  REQUIRE(classifyLocal(pins, joints, parts, 5, 24, 10) == PointSide::In);
  REQUIRE(classifyLocal(pins, joints, parts, 38, 12, 10) == PointSide::Out);
  std::string error;
  Joint through = joint;
  through.depthNm = 40 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(through, pins, tails, error));
  through.type = JointType::Dovetail;
  REQUIRE(timberlab::jointFits(through, pins, tails, error));
}

TEST_CASE("a tongue and groove cuts one centered slot and two shoulders") {
  Component host = board("host", 100 * kMm, 30 * kMm, 40 * kMm);
  Component tongue = board("tongue", 40 * kMm, 20 * kMm, 30 * kMm);
  Joint joint;
  joint.type = JointType::TongueGroove;
  joint.partA = host.id;
  joint.partB = tongue.id;
  joint.faceA = PartFace::Z1;
  joint.faceB = PartFace::X0;
  joint.thicknessNm = 10 * kMm;
  joint.depthNm = 8 * kMm;
  std::vector<Component> parts{host, tongue};
  const std::vector<Joint> joints{joint};
  expectVolume(host, joints, parts, 100.0 * 30.0 * 40.0 - 2400.0);
  expectVolume(tongue, joints, parts, 40.0 * 20.0 * 30.0 - 2400.0);
  REQUIRE(classifyLocal(host, joints, parts, 50, 15, 36) == PointSide::Out);
  REQUIRE(classifyLocal(host, joints, parts, 20, 15, 36) == PointSide::In);
  REQUIRE(classifyLocal(tongue, joints, parts, 2, 2, 15) == PointSide::Out);
  REQUIRE(classifyLocal(tongue, joints, parts, 2, 10, 15) == PointSide::In);
}

TEST_CASE("a dowel cuts a centered cylindrical hole in each face") {
  Component partA = board("partA", 80 * kMm, 40 * kMm, 30 * kMm);
  Component partB = board("partB", 80 * kMm, 40 * kMm, 30 * kMm);
  Joint joint;
  joint.type = JointType::Dowel;
  joint.partA = partA.id;
  joint.partB = partB.id;
  joint.faceA = PartFace::X1;
  joint.faceB = PartFace::X1;
  joint.thicknessNm = 8 * kMm;
  joint.depthNm = 15 * kMm;
  std::vector<Component> parts{partA, partB};
  const std::vector<Joint> joints{joint};
  const double removed = 3.141592653589793 * 16.0 * 15.0;
  expectVolume(partA, joints, parts, 80.0 * 40.0 * 30.0 - removed);
  expectVolume(partB, joints, parts, 80.0 * 40.0 * 30.0 - removed);
  REQUIRE(classifyLocal(partA, joints, parts, 79, 20, 15) == PointSide::Out);
  REQUIRE(classifyLocal(partA, joints, parts, 79, 26, 15) == PointSide::In);
  std::string error;
  Joint odd = joint;
  odd.thicknessNm = 8 * kMm + 1;
  REQUIRE_FALSE(timberlab::jointFits(odd, partA, partB, error));
}

namespace {

Joint tenonJoint(JointType type, const Component& mortise, const Component& tenon) {
  Joint joint;
  joint.id = "tenon";
  joint.type = type;
  joint.partA = mortise.id;
  joint.partB = tenon.id;
  joint.faceA = PartFace::X1;
  joint.faceB = PartFace::X0;
  joint.thicknessNm = 6 * kMm;
  joint.lengthNm = 15 * kMm;
  joint.depthNm = 12 * kMm;
  joint.offsetNm = 12 * kMm;
  joint.offset2Nm = 20 * kMm;
  return joint;
}

}  // namespace

TEST_CASE("a haunched tenon keeps a short haunch beside the tenon") {
  Component mortise = board("mortise", 100 * kMm, 30 * kMm, 80 * kMm);
  Component tenon = board("tenon", 40 * kMm, 18 * kMm, 40 * kMm);
  Joint joint = tenonJoint(JointType::HaunchedTenon, mortise, tenon);
  joint.widthNm = 24 * kMm;
  joint.length2Nm = 5 * kMm;
  std::vector<Component> parts{mortise, tenon};
  const std::vector<Joint> joints{joint};
  expectVolume(tenon, joints, parts, 40.0 * 18.0 * 40.0 - (4320.0 + 2880.0 + 960.0));
  expectVolume(mortise, joints, parts, 100.0 * 30.0 * 80.0 - (1728.0 + 480.0));
  REQUIRE(classifyLocal(tenon, joints, parts, 3, 9, 30) == PointSide::Out);
  REQUIRE(classifyLocal(tenon, joints, parts, 3, 9, 10) == PointSide::In);
  REQUIRE(classifyLocal(tenon, joints, parts, 12, 9, 30) == PointSide::In);
  REQUIRE(classifyLocal(tenon, joints, parts, 12, 2, 30) == PointSide::Out);
  REQUIRE(classifyLocal(mortise, joints, parts, 94, 15, 30) == PointSide::Out);
  REQUIRE(classifyLocal(mortise, joints, parts, 94, 15, 50) == PointSide::In);
  REQUIRE(classifyLocal(mortise, joints, parts, 98, 15, 50) == PointSide::Out);

  std::string error;
  Joint flipped = joint;
  flipped.removeMinB = true;
  expectVolume(tenon, {flipped}, parts, 40.0 * 18.0 * 40.0 - (4320.0 + 2880.0 + 960.0));
  REQUIRE(classifyLocal(tenon, {flipped}, parts, 3, 9, 30) == PointSide::In);
  REQUIRE(classifyLocal(tenon, {flipped}, parts, 3, 9, 10) == PointSide::Out);

  Joint long_haunch = joint;
  long_haunch.length2Nm = joint.lengthNm;
  REQUIRE_FALSE(timberlab::jointFits(long_haunch, mortise, tenon, error));
  Joint full = joint;
  full.widthNm = 40 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(full, mortise, tenon, error));
  Joint shallow = joint;
  shallow.depthNm = 4 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(shallow, mortise, tenon, error));
}

TEST_CASE("a wedged tenon cuts a kerf for each wedge into the tenon tip") {
  Component mortise = board("mortise", 100 * kMm, 30 * kMm, 80 * kMm);
  Component tenon = board("tenon", 40 * kMm, 18 * kMm, 40 * kMm);
  Joint joint = tenonJoint(JointType::WedgedTenon, mortise, tenon);
  joint.widthNm = 20 * kMm;
  joint.offsetNm = 10 * kMm;
  joint.count = 1;
  joint.width2Nm = 2 * kMm;
  joint.length2Nm = 8 * kMm;
  std::vector<Component> parts{mortise, tenon};
  const double plain = 40.0 * 18.0 * 40.0 - 15.0 * (18.0 * 40.0 - 6.0 * 20.0);
  expectVolume(tenon, {joint}, parts, plain - 96.0);
  expectVolume(mortise, {joint}, parts, 100.0 * 30.0 * 80.0 - 12.0 * 6.0 * 20.0);
  REQUIRE(classifyLocal(tenon, {joint}, parts, 4, 9, 20) == PointSide::Out);
  REQUIRE(classifyLocal(tenon, {joint}, parts, 4, 9, 24) == PointSide::In);
  REQUIRE(classifyLocal(tenon, {joint}, parts, 12, 9, 20) == PointSide::In);

  Joint twin = joint;
  twin.count = 2;
  expectVolume(tenon, {twin}, parts, plain - 192.0);
  REQUIRE(classifyLocal(tenon, {twin}, parts, 4, 9, 15) == PointSide::Out);
  REQUIRE(classifyLocal(tenon, {twin}, parts, 4, 9, 25) == PointSide::Out);
  REQUIRE(classifyLocal(tenon, {twin}, parts, 4, 9, 20) == PointSide::In);

  std::string error;
  Joint deep = joint;
  deep.length2Nm = 16 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(deep, mortise, tenon, error));
  Joint wide = joint;
  wide.width2Nm = 12 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(wide, mortise, tenon, error));
  Joint triple = joint;
  triple.count = 3;
  REQUIRE_FALSE(timberlab::jointFits(triple, mortise, tenon, error));
}

TEST_CASE("a loose tenon cuts matching pockets in both faces") {
  Component partA = board("a", 100 * kMm, 30 * kMm, 80 * kMm);
  Component partB = board("b", 40 * kMm, 18 * kMm, 50 * kMm);
  Joint joint;
  joint.type = JointType::LooseTenon;
  joint.partA = partA.id;
  joint.partB = partB.id;
  joint.faceA = PartFace::X1;
  joint.faceB = PartFace::X0;
  joint.thicknessNm = 6 * kMm;
  joint.widthNm = 20 * kMm;
  joint.depthNm = 8 * kMm;
  joint.count = 2;
  joint.offsetNm = 25 * kMm;
  std::vector<Component> parts{partA, partB};
  const std::vector<Joint> joints{joint};
  expectVolume(partA, joints, parts, 100.0 * 30.0 * 80.0 - 1920.0);
  expectVolume(partB, joints, parts, 40.0 * 18.0 * 50.0 - 1920.0);
  REQUIRE(classifyLocal(partA, joints, parts, 96, 15, 28) == PointSide::Out);
  REQUIRE(classifyLocal(partA, joints, parts, 96, 15, 40) == PointSide::In);
  REQUIRE(classifyLocal(partA, joints, parts, 96, 15, 53) == PointSide::Out);
  REQUIRE(classifyLocal(partB, joints, parts, 4, 9, 13) == PointSide::Out);
  REQUIRE(classifyLocal(partB, joints, parts, 4, 9, 25) == PointSide::In);

  std::string error;
  Joint crowded = joint;
  crowded.offsetNm = 15 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(crowded, partA, partB, error));
  Joint spill = joint;
  spill.offsetNm = 40 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(spill, partA, partB, error));
  Joint through = joint;
  through.depthNm = 40 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(through, partA, partB, error));
}

TEST_CASE("a biscuit cuts a disc-shaped kerf in each face") {
  Component partA = board("a", 100 * kMm, 30 * kMm, 80 * kMm);
  Component partB = board("b", 40 * kMm, 18 * kMm, 80 * kMm);
  Joint joint;
  joint.type = JointType::Biscuit;
  joint.partA = partA.id;
  joint.partB = partB.id;
  joint.faceA = PartFace::X1;
  joint.faceB = PartFace::X0;
  joint.thicknessNm = 4 * kMm;
  joint.widthNm = 100 * kMm;
  joint.depthNm = 10 * kMm;
  joint.count = 1;
  std::vector<Component> parts{partA, partB};
  const std::vector<Joint> joints{joint};
  const double segment = 2500.0 * std::acos(0.8) - 40.0 * 30.0;
  expectVolume(partA, joints, parts, 100.0 * 30.0 * 80.0 - 4.0 * segment);
  expectVolume(partB, joints, parts, 40.0 * 18.0 * 80.0 - 4.0 * segment);
  REQUIRE(classifyLocal(partA, joints, parts, 99, 15, 40) == PointSide::Out);
  REQUIRE(classifyLocal(partA, joints, parts, 99, 15, 60) == PointSide::Out);
  REQUIRE(classifyLocal(partA, joints, parts, 99, 15, 70) == PointSide::In);
  REQUIRE(classifyLocal(partA, joints, parts, 91, 15, 45) == PointSide::Out);
  REQUIRE(classifyLocal(partA, joints, parts, 91, 15, 55) == PointSide::In);
  REQUIRE(classifyLocal(partA, joints, parts, 99, 5, 40) == PointSide::In);
  REQUIRE(classifyLocal(partB, joints, parts, 1, 9, 40) == PointSide::Out);

  std::string error;
  Joint odd = joint;
  odd.widthNm = 100 * kMm + 1;
  REQUIRE_FALSE(timberlab::jointFits(odd, partA, partB, error));
  Joint buried = joint;
  buried.depthNm = 60 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(buried, partA, partB, error));
  Joint wide = joint;
  wide.widthNm = 200 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(wide, partA, partB, error));
}

TEST_CASE("a cross lap leaves one board's thickness where two boards cross") {
  Component partA = board("a", 200 * kMm, 20 * kMm, 40 * kMm);
  Component partB = board("b", 300 * kMm, 20 * kMm, 60 * kMm);
  Joint joint;
  joint.type = JointType::CrossLap;
  joint.partA = partA.id;
  joint.partB = partB.id;
  joint.faceA = PartFace::Y1;
  joint.faceB = PartFace::Y0;
  joint.offsetNm = 70 * kMm;
  joint.offset2Nm = 130 * kMm;
  joint.depthNm = 10 * kMm;
  std::vector<Component> parts{partA, partB};
  const std::vector<Joint> joints{joint};
  expectVolume(partA, joints, parts, 200.0 * 20.0 * 40.0 - 60.0 * 40.0 * 10.0);
  expectVolume(partB, joints, parts, 300.0 * 20.0 * 60.0 - 40.0 * 60.0 * 10.0);
  REQUIRE(classifyLocal(partA, joints, parts, 100, 15, 20) == PointSide::Out);
  REQUIRE(classifyLocal(partA, joints, parts, 100, 5, 20) == PointSide::In);
  REQUIRE(classifyLocal(partA, joints, parts, 20, 15, 20) == PointSide::In);
  REQUIRE(classifyLocal(partB, joints, parts, 150, 5, 30) == PointSide::Out);
  REQUIRE(classifyLocal(partB, joints, parts, 150, 15, 30) == PointSide::In);

  partB.heightNm = 80 * kMm;
  parts[1] = partB;
  expectVolume(partA, joints, parts, 200.0 * 20.0 * 40.0 - 80.0 * 40.0 * 10.0);

  std::string error;
  Joint outside = joint;
  outside.offsetNm = 150 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(outside, partA, partB, error));
  Joint through = joint;
  through.depthNm = 20 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(through, partA, partB, error));
}

TEST_CASE("an end lap notches the host and thins the lapping end to fit") {
  Component host = board("host", 200 * kMm, 20 * kMm, 40 * kMm);
  Component end = board("end", 120 * kMm, 20 * kMm, 60 * kMm);
  Joint joint;
  joint.type = JointType::EndLap;
  joint.partA = host.id;
  joint.partB = end.id;
  joint.faceA = PartFace::Y1;
  joint.faceB = PartFace::X0;
  joint.widthNm = 60 * kMm;
  joint.offsetNm = 70 * kMm;
  joint.depthNm = 10 * kMm;
  joint.lengthNm = 40 * kMm;
  std::vector<Component> parts{host, end};
  const std::vector<Joint> joints{joint};
  expectVolume(host, joints, parts, 200.0 * 20.0 * 40.0 - 60.0 * 40.0 * 10.0);
  expectVolume(end, joints, parts, 120.0 * 20.0 * 60.0 - 10.0 * 40.0 * 60.0);
  REQUIRE(classifyLocal(host, joints, parts, 100, 15, 20) == PointSide::Out);
  REQUIRE(classifyLocal(host, joints, parts, 100, 5, 20) == PointSide::In);
  REQUIRE(classifyLocal(end, joints, parts, 10, 15, 30) == PointSide::Out);
  REQUIRE(classifyLocal(end, joints, parts, 10, 5, 30) == PointSide::In);
  REQUIRE(classifyLocal(end, joints, parts, 60, 15, 30) == PointSide::In);

  Joint high = joint;
  high.removeMinB = true;
  expectVolume(end, {high}, parts, 120.0 * 20.0 * 60.0 - 10.0 * 40.0 * 60.0);
  REQUIRE(classifyLocal(end, {high}, parts, 10, 15, 30) == PointSide::In);
  REQUIRE(classifyLocal(end, {high}, parts, 10, 5, 30) == PointSide::Out);

  std::string error;
  Joint thick = joint;
  thick.depthNm = 20 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(thick, host, end, error));
  Joint over = joint;
  over.offsetNm = 150 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(over, host, end, error));
}

TEST_CASE("a splined miter adds a kerf across each mitered face") {
  Component partA = board("a", 80 * kMm, 20 * kMm, 40 * kMm);
  Component partB = board("b", 80 * kMm, 20 * kMm, 40 * kMm);
  Joint joint;
  joint.type = JointType::SplinedMiter;
  joint.partA = partA.id;
  joint.partB = partB.id;
  joint.faceA = PartFace::X0;
  joint.faceB = PartFace::X0;
  joint.removeMinA = true;
  joint.removeMinB = true;
  joint.lengthNm = 20 * kMm;
  joint.thicknessNm = 4 * kMm;
  joint.offsetNm = 18 * kMm;
  joint.depthNm = 6 * kMm;
  std::vector<Component> parts{partA, partB};
  const std::vector<Joint> joints{joint};
  const double kerf = 4.0 * (120.0 * std::sqrt(2.0) - 18.0);
  expectVolume(partA, joints, parts, 80.0 * 20.0 * 40.0 - 8000.0 - kerf);
  expectVolume(partB, joints, parts, 80.0 * 20.0 * 40.0 - 8000.0 - kerf);
  REQUIRE(classifyLocal(partA, joints, parts, 12.12, 7.88, 20) == PointSide::Out);
  REQUIRE(classifyLocal(partA, joints, parts, 12.12, 7.88, 5) == PointSide::In);
  REQUIRE(classifyLocal(partA, joints, parts, 2, 15, 20) == PointSide::Out);

  std::string error;
  Joint outside = joint;
  outside.offsetNm = 38 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(outside, partA, partB, error));
  Joint deep = joint;
  deep.depthNm = 400 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(deep, partA, partB, error));
}

TEST_CASE("a pocket screw drills a stepped angled hole in the first part only") {
  Component partA = board("a", 100 * kMm, 18 * kMm, 60 * kMm);
  Component partB = board("b", 100 * kMm, 18 * kMm, 60 * kMm);
  Joint joint;
  joint.type = JointType::PocketScrew;
  joint.partA = partA.id;
  joint.partB = partB.id;
  joint.faceA = PartFace::X1;
  joint.faceB = PartFace::X0;
  joint.edge = FaceEdge::U0;
  joint.widthNm = 8 * kMm;
  joint.thicknessNm = 4 * kMm;
  joint.lengthNm = 16 * kMm;
  joint.offsetNm = 34 * kMm;
  joint.count = 1;
  std::vector<Component> parts{partA, partB};
  const std::vector<Joint> joints{joint};
  const double angle = 15.0 * 3.141592653589793 / 180.0;
  const double toEnd = 34.0 / std::cos(angle);
  const double removed = 3.141592653589793 * (16.0 * 16.0 + 4.0 * (toEnd - 16.0));
  expectVolume(partA, joints, parts, 100.0 * 18.0 * 60.0 - removed);
  expectVolume(partB, joints, parts, 100.0 * 18.0 * 60.0);
  REQUIRE(classifyLocal(partA, joints, parts, 73.73, 2.07, 30) == PointSide::Out);
  REQUIRE(classifyLocal(partA, joints, parts, 73.73, 2.07, 33) == PointSide::Out);
  REQUIRE(classifyLocal(partA, joints, parts, 73.73, 2.07, 36) == PointSide::In);
  REQUIRE(classifyLocal(partA, joints, parts, 93.05, 7.25, 30) == PointSide::Out);
  REQUIRE(classifyLocal(partA, joints, parts, 93.05, 7.25, 33) == PointSide::In);
  REQUIRE(classifyLocal(partA, joints, parts, 20, 9, 30) == PointSide::In);

  Joint pair = joint;
  pair.count = 2;
  pair.offset2Nm = 20 * kMm;
  expectVolume(partA, {pair}, parts, 100.0 * 18.0 * 60.0 - 2.0 * removed);
  REQUIRE(classifyLocal(partA, {pair}, parts, 73.73, 2.07, 20) == PointSide::Out);
  REQUIRE(classifyLocal(partA, {pair}, parts, 73.73, 2.07, 40) == PointSide::Out);
  REQUIRE(classifyLocal(partA, {pair}, parts, 73.73, 2.07, 30) == PointSide::In);

  std::string error;
  Joint near = joint;
  near.offsetNm = 10 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(near, partA, partB, error));
  Joint steep = joint;
  steep.offsetNm = 80 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(steep, partA, partB, error));
  Joint thin = joint;
  thin.thicknessNm = 8 * kMm;
  REQUIRE_FALSE(timberlab::jointFits(thin, partA, partB, error));
  Joint odd = joint;
  odd.widthNm = 8 * kMm + 1;
  REQUIRE_FALSE(timberlab::jointFits(odd, partA, partB, error));
  REQUIRE(timberlab::jointCutsPart(joint, partA.id));
  REQUIRE_FALSE(timberlab::jointCutsPart(joint, partB.id));
}

TEST_CASE("starting sizes of the later joints fit two default boards and reload") {
  const char* keys[] = {"joint.haunched_tenon", "joint.wedged_tenon", "joint.loose_tenon",
                        "joint.biscuit",        "joint.splined_miter", "joint.pocket_screw"};
  for (const char* key : keys) {
    Document document;
    std::string error;
    const std::string selected = document.addBoard();
    const std::string mate = document.addBoard();
    const std::string id = document.addJoint(key, selected, mate, error);
    INFO(key);
    INFO(error);
    REQUIRE_FALSE(id.empty());
    const auto cutA = probeSolid(*document.find(selected), document.joints(), document.components());
    const auto cutB = probeSolid(*document.find(mate), document.joints(), document.components());
    REQUIRE(cutA.ok);
    REQUIRE(cutB.ok);
    REQUIRE(cutA.volumeMm3 < 600.0 * 18.0 * 720.0 - 0.5);
    if (std::string(key) != "joint.pocket_screw") REQUIRE(cutB.volumeMm3 < 600.0 * 18.0 * 720.0 - 0.5);
    Document loaded;
    REQUIRE(Document::fromJson(document.toJson(), loaded, error));
    REQUIRE(loaded.joints().size() == 1);
    const Joint& back = loaded.joints().front();
    const Joint& original = document.joints().front();
    REQUIRE(jointTypeKey(back.type) == std::string(key));
    REQUIRE(back.length2Nm == original.length2Nm);
    REQUIRE(back.width2Nm == original.width2Nm);
    REQUIRE(back.count == original.count);
    REQUIRE(back.edge == original.edge);
  }

  for (const char* key : {"joint.cross_lap", "joint.end_lap"}) {
    Document document;
    std::string error;
    const std::string selected = document.addBoard();
    const std::string mate = document.addBoard();
    REQUIRE(document.setDimension(selected, "width", 200 * kMm, error));
    REQUIRE(document.setDimension(selected, "height", 40 * kMm, error));
    REQUIRE(document.setDimension(mate, "width", 300 * kMm, error));
    REQUIRE(document.setDimension(mate, "height", 60 * kMm, error));
    const std::string id = document.addJoint(key, selected, mate, error);
    INFO(key);
    INFO(error);
    REQUIRE_FALSE(id.empty());
    REQUIRE(probeSolid(*document.find(selected), document.joints(), document.components()).ok);
    REQUIRE(probeSolid(*document.find(mate), document.joints(), document.components()).ok);
  }
}

TEST_CASE("the later joints store their own keys and an older file still loads") {
  Document document;
  std::string error;
  const std::string selected = document.addBoard();
  const std::string mate = document.addBoard();
  REQUIRE_FALSE(document.addJoint("joint.mortise_tenon", selected, mate, error).empty());
  nlohmann::json plain = nlohmann::json::parse(document.toJson());
  REQUIRE_FALSE(plain["joints"][0].contains("length2"));
  REQUIRE_FALSE(plain["joints"][0].contains("width2"));
  Document older;
  REQUIRE(Document::fromJson(plain.dump(), older, error));

  Document wedged;
  const std::string rail = wedged.addBoard();
  const std::string leg = wedged.addBoard();
  REQUIRE_FALSE(wedged.addJoint("joint.wedged_tenon", rail, leg, error).empty());
  nlohmann::json body = nlohmann::json::parse(wedged.toJson());
  REQUIRE(body["joints"][0].contains("length2"));
  REQUIRE(body["joints"][0].contains("width2"));
  for (const char* key : {"length2", "width2", "count"}) {
    nlohmann::json missing = body;
    missing["joints"][0].erase(key);
    Document rejected;
    REQUIRE_FALSE(Document::fromJson(missing.dump(), rejected, error));
    REQUIRE(error == "storage.json");
  }
  const std::string jointId = wedged.joints().front().id;
  REQUIRE(wedged.setJointParameter(jointId, "width2", 2 * kMm, error));
  REQUIRE(wedged.joints().front().width2Nm == 2 * kMm);
  REQUIRE_FALSE(wedged.setJointParameter(jointId, "width2", 5000 * kMm, error));
  REQUIRE(error == "joint.parameter");

  Document pocket;
  const std::string first = pocket.addBoard();
  const std::string second = pocket.addBoard();
  REQUIRE_FALSE(pocket.addJoint("joint.pocket_screw", first, second, error).empty());
  nlohmann::json screw = nlohmann::json::parse(pocket.toJson());
  screw["joints"][0].erase("edge");
  Document rejected;
  REQUIRE_FALSE(Document::fromJson(screw.dump(), rejected, error));
  REQUIRE(error == "storage.json");
}
