#include "timberlab/core/document.hpp"
#include "timberlab/core/joint.hpp"
#include "timberlab/core/manufacturing.hpp"
#include "timberlab/geometry/solids.hpp"
#include "timberlab/units/units.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>

using timberlab::Component;
using timberlab::ComponentType;
using timberlab::Document;
using timberlab::Joint;
using timberlab::JointType;
using timberlab::PartFace;
using timberlab::manufacturing::billOfMaterials;
using timberlab::manufacturing::cutList;
using timberlab::manufacturing::cutListCsv;
using timberlab::manufacturing::cutListJson;
using timberlab::manufacturing::dimensionedFrontSvg;
using timberlab::manufacturing::frontViewSvg;
using timberlab::manufacturing::rightViewSvg;
using timberlab::manufacturing::sheetSvg;
using timberlab::manufacturing::topViewSvg;
using timberlab::manufacturing::wireDxf;
using timberlab::manufacturing::wirePdf;

namespace {

constexpr std::int64_t kMm = timberlab::units::kNmPerMm;

Component board(const std::string& name, std::int64_t width, std::int64_t thickness, std::int64_t height) {
  Component component;
  component.id = name;
  component.type = ComponentType::Board;
  component.name = name;
  component.materialId = "european-oak";
  component.role = "rail";
  component.widthNm = width;
  component.thicknessNm = thickness;
  component.heightNm = height;
  return component;
}

}  // namespace

TEST_CASE("a cut list keeps finished size and adds the allowance to rough stock") {
  const Component oak = board("Rail", 100 * kMm, 18 * kMm, 200 * kMm);
  std::vector<timberlab::manufacturing::CutPiece> pieces;
  std::string error;
  REQUIRE(cutList({oak}, 0, pieces, error));
  REQUIRE(pieces.size() == 1);
  REQUIRE(pieces[0].roughWidthNm == pieces[0].finishedWidthNm);
  REQUIRE(pieces[0].roughThicknessNm == 18 * kMm);
  REQUIRE(pieces[0].roughHeightNm == 200 * kMm);
  REQUIRE(pieces[0].shape == "board");

  REQUIRE(cutList({oak}, 2 * kMm, pieces, error));
  REQUIRE(pieces[0].finishedWidthNm == 100 * kMm);
  REQUIRE(pieces[0].roughWidthNm == 102 * kMm);
  REQUIRE(pieces[0].roughThicknessNm == 20 * kMm);
  REQUIRE(pieces[0].roughHeightNm == 202 * kMm);

  REQUIRE_FALSE(cutList({oak}, -1, pieces, error));
  REQUIRE(error == "parameter.range");
  REQUIRE(pieces.empty());

  const std::string csv = cutListCsv({});
  REQUIRE(csv.find("finished_width_mm") != std::string::npos);
  REQUIRE(cutList({oak}, 2 * kMm, pieces, error));
  const std::string row = cutListCsv(pieces);
  REQUIRE(row.find("Rail,european-oak,rail,board,100.000000,18.000000,200.000000,102.000000,20.000000,202.000000") !=
          std::string::npos);
}

TEST_CASE("a cylinder is listed as a diameter and the bill of materials groups matches") {
  Component dowel;
  dowel.id = "dowel";
  dowel.type = ComponentType::Cylinder;
  dowel.name = "Dowel";
  dowel.materialId = "european-beech";
  dowel.radiusNm = 4 * kMm;
  dowel.heightNm = 30 * kMm;
  const Component rail = board("Rail", 100 * kMm, 18 * kMm, 200 * kMm);
  Component copy = rail;
  copy.id = "copy";
  copy.name = "Other rail";
  std::vector<timberlab::manufacturing::CutPiece> pieces;
  std::string error;
  REQUIRE(cutList({dowel, rail, copy}, 0, pieces, error));
  REQUIRE(pieces[0].shape == "cylinder");
  REQUIRE(pieces[0].finishedWidthNm == 8 * kMm);
  REQUIRE(pieces[0].finishedThicknessNm == 8 * kMm);
  REQUIRE(pieces[0].finishedHeightNm == 30 * kMm);

  const auto lines = billOfMaterials(pieces);
  REQUIRE(lines.size() == 2);
  REQUIRE(lines[1].quantity == 2);
  REQUIRE(lines[1].materialId == "european-oak");
  REQUIRE(lines[1].role == "rail");

  const auto parsed = nlohmann::json::parse(cutListJson(0, pieces, lines));
  REQUIRE(parsed["allowanceNm"] == 0);
  REQUIRE(parsed["cutList"].size() == 3);
  REQUIRE(parsed["bom"][1]["quantity"] == 2);
}

TEST_CASE("the front view is a wire of the cut solid with Y dropped") {
  Component part = board("Face", 100 * kMm, 18 * kMm, 200 * kMm);
  std::string error;
  const std::string plain = frontViewSvg(part, {}, {part}, error);
  REQUIRE(error.empty());
  REQUIRE(plain.find("<svg") != std::string::npos);
  REQUIRE(plain.find("Tessellation wire") != std::string::npos);
  REQUIRE(plain.find("viewBox=\"0 0 102.000 202.000\"") != std::string::npos);

  Component mate = board("Mate", 80 * kMm, 18 * kMm, 80 * kMm);
  Joint joint;
  joint.type = JointType::Dado;
  joint.partA = part.id;
  joint.partB = mate.id;
  joint.faceA = PartFace::Z1;
  joint.widthNm = 18 * kMm;
  joint.depthNm = 6 * kMm;
  joint.offsetNm = 20 * kMm;
  const std::string cut = frontViewSvg(part, {joint}, {part, mate}, error);
  REQUIRE_FALSE(cut.empty());
  REQUIRE(cut.size() > plain.size());

  const std::string dimensioned = dimensionedFrontSvg(part, {}, {part}, error);
  REQUIRE(error.empty());
  REQUIRE(dimensioned.find("id=\"width-text\"") != std::string::npos);
  REQUIRE(dimensioned.find(">100 mm</text>") != std::string::npos);
  REQUIRE(dimensioned.find(">200 mm</text>") != std::string::npos);
  REQUIRE(dimensioned.find("id=\"width-line\" x1=\"1.000\" y1=\"207.000\" x2=\"101.000\"") != std::string::npos);
  REQUIRE(dimensioned.find("id=\"height-line\"") != std::string::npos);
  REQUIRE(dimensioned.find("y1=\"1.000\"") != std::string::npos);
  REQUIRE(dimensioned.find("y2=\"201.000\"") != std::string::npos);
  REQUIRE(dimensioned.find("Not hidden-line removal") != std::string::npos);
  const std::string still = dimensionedFrontSvg(part, {joint}, {part, mate}, error);
  REQUIRE(still.find(">100 mm</text>") != std::string::npos);
  REQUIRE(still.find(">200 mm</text>") != std::string::npos);
  Component cylinder;
  cylinder.type = ComponentType::Cylinder;
  cylinder.radiusNm = 10 * kMm;
  cylinder.heightNm = 40 * kMm;
  REQUIRE(dimensionedFrontSvg(cylinder, {}, {cylinder}, error).empty());
  REQUIRE(error == "dimension.not_applicable");
}

TEST_CASE("top, right, sheet, DXF, and PDF are wires of the same solid") {
  const Component part = board("Face", 100 * kMm, 18 * kMm, 200 * kMm);
  std::string error;
  const std::string top = topViewSvg(part, {}, {part}, error);
  REQUIRE(top.find("viewBox=\"0 0 102.000 20.000\"") != std::string::npos);
  REQUIRE(top.find("Z is dropped") != std::string::npos);
  const std::string right = rightViewSvg(part, {}, {part}, error);
  REQUIRE(right.find("viewBox=\"0 0 20.000 202.000\"") != std::string::npos);
  REQUIRE(right.find("X is dropped") != std::string::npos);

  const std::string sheet = sheetSvg(part, {}, {part}, error);
  REQUIRE(sheet.find("viewBox=\"0 0 132.000 232.000\"") != std::string::npos);
  REQUIRE(sheet.find("id=\"front\"") != std::string::npos);
  REQUIRE(sheet.find("id=\"top\"") != std::string::npos);
  REQUIRE(sheet.find("id=\"right\"") != std::string::npos);
  REQUIRE(sheet.find("translate(112.000 0)") != std::string::npos);
  REQUIRE(sheet.find("translate(0 212.000)") != std::string::npos);
  REQUIRE(sheet.find("Not a dimensioned drawing") != std::string::npos);

  const std::string dxf = wireDxf(part, {}, {part}, error);
  REQUIRE(dxf.find("AC1009") != std::string::npos);
  REQUIRE(dxf.find("Not a dimensioned drawing") != std::string::npos);
  const std::string frontEdge = "0\nLINE\n8\nFRONT\n10\n0.000\n20\n0.000\n30\n0.0\n11\n100.000\n21\n0.000\n31\n0.0\n";
  const std::string topEdge = "0\nLINE\n8\nTOP\n10\n0.000\n20\n0.000\n30\n0.0\n11\n0.000\n21\n18.000\n31\n0.0\n";
  const std::string rightEdge = "0\nLINE\n8\nRIGHT\n10\n0.000\n20\n0.000\n30\n0.0\n11\n0.000\n21\n200.000\n31\n0.0\n";
  REQUIRE(dxf.find(frontEdge) != std::string::npos);
  REQUIRE(dxf.find(topEdge) != std::string::npos);
  REQUIRE(dxf.find(rightEdge) != std::string::npos);
  REQUIRE(dxf.size() >= 4);
  REQUIRE(dxf.compare(dxf.size() - 4, 4, "EOF\n") == 0);

  const std::string pdf = wirePdf(part, {}, {part}, error);
  REQUIRE(pdf.compare(0, 9, "%PDF-1.4\n") == 0);
  REQUIRE(pdf.find("%%EOF\n") != std::string::npos);
  REQUIRE(pdf.find("Tessellation wire. Not a dimensioned drawing.") != std::string::npos);
  REQUIRE(pdf.find("0.000 0.000 m\n100.000 0.000 l\n") != std::string::npos);
  const auto xrefLabel = pdf.find("\nstartxref\n");
  REQUIRE(xrefLabel != std::string::npos);
  const std::size_t xrefAt = static_cast<std::size_t>(std::stoll(pdf.substr(xrefLabel + std::strlen("\nstartxref\n"))));
  REQUIRE(pdf.compare(xrefAt, 5, "xref\n") == 0);
}

namespace {

Joint dadoOn(const Component& host, PartFace face) {
  Joint joint;
  joint.id = "dado";
  joint.type = JointType::Dado;
  joint.partA = host.id;
  joint.partB = "mate";
  joint.faceA = face;
  joint.widthNm = 20 * kMm;
  joint.offsetNm = 40 * kMm;
  joint.depthNm = 5 * kMm;
  return joint;
}

bool contains(const std::string& text, const std::string& part) { return text.find(part) != std::string::npos; }

std::size_t occurrences(const std::string& text, const std::string& part) {
  std::size_t count = 0;
  for (std::size_t at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size())) ++count;
  return count;
}

}  // namespace

TEST_CASE("hidden-line removal tells a slot facing the viewer from one behind the face") {
  const Component host = board("host", 100 * kMm, 20 * kMm, 40 * kMm);
  const Component mate = board("mate", 40 * kMm, 18 * kMm, 40 * kMm);
  const std::vector<Component> parts{host, mate};

  const auto plain = timberlab::geometry::hiddenLineView(host, {}, parts, timberlab::geometry::ViewDirection::Front);
  REQUIRE(plain.ok);
  REQUIRE(plain.hidden.empty());

  const auto toward = timberlab::geometry::hiddenLineView(host, {dadoOn(host, PartFace::Y0)}, parts,
                                                          timberlab::geometry::ViewDirection::Front);
  const auto away = timberlab::geometry::hiddenLineView(host, {dadoOn(host, PartFace::Y1)}, parts,
                                                        timberlab::geometry::ViewDirection::Front);
  REQUIRE(toward.ok);
  REQUIRE(away.ok);
  INFO("visible " << toward.visible.size() << " hidden " << toward.hidden.size());
  INFO("away visible " << away.visible.size() << " hidden " << away.hidden.size());
  REQUIRE(toward.hidden.empty());
  REQUIRE_FALSE(away.hidden.empty());
  REQUIRE(toward.visible.size() > plain.visible.size());
}

TEST_CASE("the hidden-line sheet draws visible lines solid, hidden lines dashed, and dimensions the cuts") {
  const Component host = board("host", 100 * kMm, 20 * kMm, 40 * kMm);
  const Component mate = board("mate", 40 * kMm, 18 * kMm, 40 * kMm);
  const std::vector<Component> parts{host, mate};
  std::string error;

  const std::string toward = timberlab::manufacturing::hiddenLineSheetSvg(host, {dadoOn(host, PartFace::Y0)}, parts, error);
  REQUIRE_FALSE(toward.empty());
  REQUIRE(contains(toward, "<g id=\"visible\""));
  REQUIRE(contains(toward, "<g id=\"dimensions\""));
  REQUIRE(contains(toward, ">100</text>"));
  REQUIRE(contains(toward, ">20</text>"));
  REQUIRE(contains(toward, ">40</text>"));
  REQUIRE(contains(toward, ">5</text>"));
  REQUIRE(contains(toward, "Hidden-line removal on the exact solid"));
  REQUIRE_FALSE(contains(toward, "Tessellation"));

  const std::string away = timberlab::manufacturing::hiddenLineSheetSvg(host, {dadoOn(host, PartFace::Y1)}, parts, error);
  REQUIRE(occurrences(away, "<line") > occurrences(toward, "<line") - 12);
  const auto hiddenBlock = [](const std::string& svg) {
    const std::size_t from = svg.find("<g id=\"hidden\"");
    const std::size_t to = svg.find("</g>", from);
    return svg.substr(from, to - from);
  };
  REQUIRE(occurrences(hiddenBlock(away), "<line") > occurrences(hiddenBlock(toward), "<line"));

  const std::string dxf = timberlab::manufacturing::hiddenLineDxf(host, {dadoOn(host, PartFace::Y1)}, parts, error);
  REQUIRE(contains(dxf, "HIDDEN"));
  REQUIRE(contains(dxf, "DIMENSIONS"));
  REQUIRE(contains(dxf, "\nTEXT\n"));
  REQUIRE(contains(dxf, "\nEOF\n"));

  const std::string pdf = timberlab::manufacturing::hiddenLinePdf(host, {dadoOn(host, PartFace::Y1)}, parts, error);
  REQUIRE(pdf.rfind("%PDF-1.4", 0) == 0);
  REQUIRE(contains(pdf, "[1.5 1] 0 d"));
  REQUIRE(contains(pdf, "(100) Tj"));
  REQUIRE(contains(pdf, "%%EOF"));

  Component pin;
  pin.id = "pin";
  pin.type = ComponentType::Cylinder;
  pin.radiusNm = 10 * kMm;
  pin.heightNm = 50 * kMm;
  const std::string round = timberlab::manufacturing::hiddenLineSheetSvg(pin, {}, {pin}, error);
  REQUIRE_FALSE(round.empty());
  REQUIRE_FALSE(contains(round, "<text"));
}

TEST_CASE("feature dimensions follow a mortise and tenon through a size edit") {
  Document document;
  std::string error;
  const std::string rail = document.addBoard();
  const std::string leg = document.addBoard();
  REQUIRE(document.setDimension(rail, "width", 300 * kMm, error));
  REQUIRE(document.setDimension(rail, "thickness", 20 * kMm, error));
  REQUIRE(document.setDimension(rail, "height", 60 * kMm, error));
  REQUIRE_FALSE(document.addJoint("joint.mortise_tenon", rail, leg, error).empty());
  const Joint joint = document.joints().front();
  const Component* tenon = document.find(rail);
  const std::string sheet =
      timberlab::manufacturing::hiddenLineSheetSvg(*tenon, document.joints(), document.components(), error);
  REQUIRE_FALSE(sheet.empty());
  const auto expectDimension = [&](std::int64_t nm) {
    std::string text = timberlab::units::formatLength(nm, timberlab::units::LengthUnit::Millimetre, 16);
    const std::size_t space = text.find(' ');
    if (space != std::string::npos) text.resize(space);
    INFO(text);
    REQUIRE(contains(sheet, ">" + text + "</text>"));
  };
  expectDimension(joint.lengthNm);
  expectDimension(300 * kMm);
  expectDimension(60 * kMm);
}

TEST_CASE("binary STL holds one triangle record per face triangle in millimetres") {
  Component box = board("box", 10 * kMm, 20 * kMm, 30 * kMm);
  box.placement.xNm = 100 * kMm;
  const std::vector<Component> parts{box};

  const auto local = timberlab::geometry::exportStl(parts, {}, "box", false);
  REQUIRE(local.ok);
  REQUIRE(local.partCount == 1);
  REQUIRE(local.data.size() == 84 + 12 * 50);
  std::uint32_t count = 0;
  std::memcpy(&count, local.data.data() + 80, 4);
  REQUIRE(count == 12);
  const auto firstX = [](const std::string& data, std::size_t record, int corner) {
    float value = 0;
    std::memcpy(&value, data.data() + 84 + record * 50 + 12 + static_cast<std::size_t>(corner) * 12, 4);
    return value;
  };
  float lowest = 1e9f;
  float highest = -1e9f;
  for (std::size_t record = 0; record < 12; ++record) {
    for (int corner = 0; corner < 3; ++corner) {
      lowest = std::min(lowest, firstX(local.data, record, corner));
      highest = std::max(highest, firstX(local.data, record, corner));
    }
  }
  REQUIRE(lowest == 0.0f);
  REQUIRE(highest == 10.0f);

  const auto placed = timberlab::geometry::exportStl(parts, {}, "", true);
  REQUIRE(placed.ok);
  float placedLowest = 1e9f;
  for (std::size_t record = 0; record < 12; ++record) {
    for (int corner = 0; corner < 3; ++corner) placedLowest = std::min(placedLowest, firstX(placed.data, record, corner));
  }
  REQUIRE(placedLowest == 100.0f);

  REQUIRE_FALSE(timberlab::geometry::exportStl(parts, {}, "missing", false).ok);
  Component hidden = box;
  hidden.visible = false;
  REQUIRE_FALSE(timberlab::geometry::exportStl({hidden}, {}, "", true).ok);
}

TEST_CASE("a STEP file holds one named solid per part and the cut surfaces of its joints") {
  if (!timberlab::geometry::stepAvailable()) SKIP("this Open CASCADE was built without the data-exchange modules");
  Component left = board("Left leg", 60 * kMm, 40 * kMm, 300 * kMm);
  Component rail = board("Rail", 100 * kMm, 20 * kMm, 60 * kMm);
  rail.placement.xNm = 70 * kMm;
  Joint dowel;
  dowel.id = "dowel";
  dowel.type = JointType::Dowel;
  dowel.partA = left.id;
  dowel.partB = rail.id;
  dowel.faceA = PartFace::X1;
  dowel.faceB = PartFace::X0;
  dowel.thicknessNm = 8 * kMm;
  dowel.depthNm = 15 * kMm;
  const std::vector<Component> parts{left, rail};
  const std::vector<Joint> joints{dowel};

  const auto both = timberlab::geometry::exportStep(parts, joints, "", true);
  INFO(both.error);
  REQUIRE(both.ok);
  REQUIRE(both.partCount == 2);
  REQUIRE(both.data.rfind("ISO-10303-21;", 0) == 0);
  REQUIRE(occurrences(both.data, "MANIFOLD_SOLID_BREP") == 2);
  REQUIRE(contains(both.data, "'Left leg'"));
  REQUIRE(contains(both.data, "'Rail'"));
  REQUIRE(contains(both.data, "CYLINDRICAL_SURFACE"));
  REQUIRE(contains(both.data, "MILLI"));

  const auto one = timberlab::geometry::exportStep(parts, joints, "Rail", false);
  REQUIRE(one.ok);
  REQUIRE(one.partCount == 1);
  REQUIRE(occurrences(one.data, "MANIFOLD_SOLID_BREP") == 1);
  REQUIRE_FALSE(contains(one.data, "'Left leg'"));
  REQUIRE_FALSE(timberlab::geometry::exportStep(parts, joints, "missing", false).ok);
}
