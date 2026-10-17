#include "timberlab/core/document.hpp"
#include "timberlab/core/joint.hpp"
#include "timberlab/core/manufacturing.hpp"
#include "timberlab/units/units.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

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
