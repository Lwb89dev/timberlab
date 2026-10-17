#include "self_check.hpp"

#include "timberlab/constraints/dependency_graph.hpp"
#include "timberlab/core/document.hpp"
#include "timberlab/core/transform.hpp"
#include "timberlab/geometry/solids.hpp"
#include "timberlab/materials/catalog.hpp"
#include "timberlab/storage/archive.hpp"
#include "timberlab/storage/library.hpp"
#include "timberlab/storage/preferences.hpp"
#include "timberlab/units/units.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace timberlab {
namespace {

int fail(const std::string& message) {
  std::cerr << "self-check failed: " << message << "\n";
  return 1;
}

bool near(double value, double expected, double relative) {
  const double scale = std::max(1.0, std::abs(expected));
  return std::abs(value - expected) <= relative * scale;
}

bool overlaps(const Component& a, const Component& b) {
  const LocalBox boxA = localBox(a);
  const LocalBox boxB = localBox(b);
  const double ax0 = units::toMillimetres(a.placement.xNm) + boxA.originXmm;
  const double ay0 = units::toMillimetres(a.placement.yNm) + boxA.originYmm;
  const double az0 = units::toMillimetres(a.placement.zNm) + boxA.originZmm;
  const double bx0 = units::toMillimetres(b.placement.xNm) + boxB.originXmm;
  const double by0 = units::toMillimetres(b.placement.yNm) + boxB.originYmm;
  const double bz0 = units::toMillimetres(b.placement.zNm) + boxB.originZmm;
  const double ax1 = ax0 + boxA.sizeXmm;
  const double ay1 = ay0 + boxA.sizeYmm;
  const double az1 = az0 + boxA.sizeZmm;
  const double bx1 = bx0 + boxB.sizeXmm;
  const double by1 = by0 + boxB.sizeYmm;
  const double bz1 = bz0 + boxB.sizeZmm;
  if (ax1 <= bx0 || bx1 <= ax0) return false;
  if (ay1 <= by0 || by1 <= ay0) return false;
  if (az1 <= bz0 || bz1 <= az0) return false;
  return true;
}

}  // namespace

int runSelfCheck() {
  using units::LengthUnit;
  const std::pair<const char*, std::int64_t> presets[] = {
      {"1/8\"", 3'175'000},   {"1/4\"", 6'350'000},   {"3/8\"", 9'525'000},   {"1/2\"", 12'700'000},
      {"5/8\"", 15'875'000},  {"3/4\"", 19'050'000},  {"1\"", 25'400'000},    {"1 1/4\"", 31'750'000},
      {"1 1/2\"", 38'100'000}, {"2\"", 50'800'000},   {"3\"", 76'200'000},    {"4\"", 101'600'000},
  };
  for (const auto& preset : presets) {
    const auto parsed = units::parseLength(preset.first, LengthUnit::Millimetre);
    if (!parsed.ok || parsed.nm != preset.second) return fail(std::string("preset ") + preset.first);
    const auto formatted = units::formatLength(parsed.nm, LengthUnit::Inch, 64);
    const auto roundTrip = units::parseLength(formatted, LengthUnit::Millimetre);
    if (!roundTrip.ok || roundTrip.nm != preset.second) return fail(std::string("round trip ") + preset.first);
  }

  std::int64_t sum = 0;
  for (int i = 0; i < 64; ++i) {
    const auto step = units::parseLength("1/64\"", LengthUnit::Millimetre);
    if (!step.ok) return fail("1/64");
    sum += step.nm;
  }
  if (sum != units::kNmPerInch) return fail("64/64 inch");

  const auto mixed = units::parseLength("3' 6 1/4\"", LengthUnit::Millimetre);
  const std::int64_t expectedMixed = 3 * units::kNmPerFoot + 6 * units::kNmPerInch + units::kNmPerInch / 4;
  if (!mixed.ok || mixed.nm != expectedMixed) return fail("feet and inches");

  const auto comma = units::parseLength("1,5 cm", LengthUnit::Millimetre);
  if (!comma.ok || comma.nm != 15 * units::kNmPerMm) return fail("comma centimetre");

  Document document;
  const std::string boardId = document.addBoard();
  std::string error;
  if (!document.setDisplayUnit("in", error)) return fail(error);
  if (document.setDimension(boardId, "width", 0, error) || error != "dimension.non_positive") return fail("zero width");
  const auto threeQuarter = units::parseLength("3/4\"", LengthUnit::Inch);
  if (!document.setDimension(boardId, "thickness", threeQuarter.nm, error)) return fail(error);
  const Component* board = document.find(boardId);
  if (board == nullptr || board->thicknessNm != 19'050'000) return fail("stored thickness");
  if (!document.setDisplayUnit("mm", error)) return fail(error);
  if (document.find(boardId)->thicknessNm != 19'050'000) return fail("unit switch mutated geometry");
  if (units::formatLength(document.find(boardId)->thicknessNm, LengthUnit::Millimetre, 16) != "19.05 mm") {
    return fail("metric display");
  }
  if (!document.undo(error) || document.find(boardId)->thicknessNm != 19'050'000) return fail("undo unit");
  if (!document.undo(error) || document.find(boardId)->thicknessNm == 19'050'000) return fail("undo thickness");
  if (!document.redo(error) || document.find(boardId)->thicknessNm != 19'050'000) return fail("redo thickness");

  const auto mesh = geometry::makeBoxMesh(100, 18, 200, geometry::MeshQuality::Standard);
  if (!mesh.ok) return fail(mesh.error);
  if (!near(mesh.volumeMm3, 100.0 * 18.0 * 200.0, 1e-6)) return fail("box volume");
  if (!near(mesh.bboxMax[0] - mesh.bboxMin[0], 100, 1e-6)) return fail("box width");
  if (!near(mesh.bboxMax[1] - mesh.bboxMin[1], 18, 1e-6)) return fail("box thickness");
  if (!near(mesh.bboxMax[2] - mesh.bboxMin[2], 200, 1e-6)) return fail("box height");
  if (mesh.indices.size() < 12) return fail("box mesh");

  const auto coarse = geometry::makeCylinderMesh(40, 120, geometry::MeshQuality::Draft);
  const auto fine = geometry::makeCylinderMesh(40, 120, geometry::MeshQuality::Fine);
  if (!coarse.ok || !fine.ok) return fail("cylinder");
  const double cylinderVolume = 3.141592653589793 * 40.0 * 40.0 * 120.0;
  if (!near(fine.volumeMm3, cylinderVolume, 1e-4)) return fail("cylinder volume");
  if (fine.indices.size() <= coarse.indices.size()) return fail("mesh quality");

  double matrix[16];
  Placement placement;
  placement.rzDeg = 90;
  placementMatrix(placement, matrix);
  double x = 0, y = 0, z = 0;
  transformPoint(matrix, 1, 0, 0, x, y, z);
  if (!near(x, 0, 1e-6) || !near(y, 1, 1e-6) || !near(z, 0, 1e-6)) return fail("rotation");

  document.makeDemoTable();
  if (document.components().size() != 9) return fail("demo count");
  if (document.parameters().size() != 9) return fail("demo drivers");
  for (std::size_t i = 0; i < document.components().size(); ++i) {
    for (std::size_t j = i + 1; j < document.components().size(); ++j) {
      if (overlaps(document.components()[i], document.components()[j])) return fail("demo overlap");
    }
  }
  if (!document.setParameter("table.width", 1500 * units::kNmPerMm, error)) return fail("propagate " + error);
  const Component* top = nullptr;
  const Component* apron = nullptr;
  const Component* leg = nullptr;
  for (const Component& component : document.components()) {
    if (component.name == "Top") top = &component;
    if (component.name == "Front apron") apron = &component;
    if (component.name == "Leg 2") leg = &component;
  }
  if (top == nullptr || top->widthNm != 1500 * units::kNmPerMm) return fail("propagated top");
  if (apron == nullptr || apron->widthNm != 1300 * units::kNmPerMm) return fail("propagated apron");
  if (leg == nullptr || leg->placement.xNm != 1400 * units::kNmPerMm) return fail("propagated leg");
  for (std::size_t i = 0; i < document.components().size(); ++i) {
    for (std::size_t j = i + 1; j < document.components().size(); ++j) {
      if (overlaps(document.components()[i], document.components()[j])) return fail("propagated overlap");
    }
  }
  if (!document.undo(error)) return fail("undo width");
  top = nullptr;
  apron = nullptr;
  leg = nullptr;
  for (const Component& component : document.components()) {
    if (component.name == "Top") top = &component;
    if (component.name == "Front apron") apron = &component;
    if (component.name == "Leg 2") leg = &component;
  }
  if (top == nullptr || top->widthNm != 1200 * units::kNmPerMm) return fail("undo top");
  if (apron == nullptr || apron->widthNm != 1000 * units::kNmPerMm) return fail("undo apron");
  if (leg == nullptr || leg->placement.xNm != 1100 * units::kNmPerMm) return fail("undo leg");

  Document cabinet;
  if (!cabinet.addAssembly("carcass", error)) return fail("carcass " + error);
  if (cabinet.components().size() != 5) return fail("carcass count");
  std::int64_t carcassRightX = 0;
  bool sawCarcassRight = false;
  for (const Component& component : cabinet.components()) {
    if (component.name != "Carcass right") continue;
    carcassRightX = component.placement.xNm;
    sawCarcassRight = true;
  }
  if (!sawCarcassRight) return fail("carcass right");
  std::string carcassWidth;
  for (const constraints::Parameter& parameter : cabinet.parameters()) {
    const std::string suffix = ".width";
    if (parameter.id.size() >= suffix.size() &&
        parameter.id.compare(parameter.id.size() - suffix.size(), suffix.size(), suffix) == 0) {
      carcassWidth = parameter.id;
    }
  }
  if (carcassWidth.empty()) return fail("carcass width");
  if (!cabinet.setParameter(carcassWidth, 900 * units::kNmPerMm, error)) return fail("carcass edit " + error);
  const Component* carcassRight = nullptr;
  for (const Component& component : cabinet.components()) {
    if (component.name == "Carcass right") carcassRight = &component;
  }
  if (carcassRight == nullptr || carcassRight->placement.xNm != carcassRightX + 300 * units::kNmPerMm) {
    return fail("carcass move");
  }
  if (!cabinet.undo(error)) return fail("carcass undo");
  carcassRight = nullptr;
  for (const Component& component : cabinet.components()) {
    if (component.name == "Carcass right") carcassRight = &component;
  }
  if (carcassRight == nullptr || carcassRight->placement.xNm != carcassRightX) return fail("carcass restored");

  Document frame;
  if (!frame.addAssembly("frame", error)) return fail("frame " + error);
  if (frame.components().size() != 4) return fail("frame count");
  bool sawFrame = false;
  for (const Component& component : frame.components()) {
    if (component.name != "Frame right") continue;
    sawFrame = component.placement.xNm == 540 * units::kNmPerMm && component.role == "stile" && component.parentId.empty();
  }
  if (!sawFrame) return fail("frame right");

  const char* dataDir = std::getenv("TIMBERLAB_DATA_DIR");
  const std::filesystem::path root = dataDir != nullptr ? dataDir : std::filesystem::path("build/self-check");
  std::filesystem::create_directories(root);
  const auto projectPath = root / "self-check.timberlab";
  const auto saved = storage::saveProject(projectPath.string(), document);
  if (!saved.ok) return fail(saved.error);
  Document loaded;
  const auto opened = storage::loadProject(projectPath.string(), loaded);
  if (!opened.ok) return fail(opened.error);
  if (loaded.components().size() != document.components().size()) return fail("reload count");
  if (loaded.components().front().widthNm != document.components().front().widthNm) return fail("reload size");
  if (loaded.parameters().size() != document.parameters().size()) return fail("reload drivers");

  std::ofstream garbage(root / "garbage.timberlab", std::ios::binary);
  garbage << "this is not a project";
  garbage.close();
  Document rejected;
  const auto bad = storage::loadProject((root / "garbage.timberlab").string(), rejected);
  if (bad.ok) return fail("garbage accepted");

  storage::Preferences preferences((root / "prefs.sqlite").string());
  if (!preferences.ok() || preferences.schemaVersion() != 1) return fail("preferences");
  if (!preferences.set("display_unit", "in")) return fail("preference write");
  storage::Preferences again((root / "prefs.sqlite").string());
  const auto stored = again.get("display_unit");
  if (!stored || *stored != "in") return fail("preference read");

  const auto libraryPath = root / "library.sqlite";
  std::filesystem::remove(libraryPath);
  std::filesystem::remove(std::filesystem::path(libraryPath.string() + "-wal"));
  std::filesystem::remove(std::filesystem::path(libraryPath.string() + "-shm"));
  {
    storage::Library library(libraryPath.string());
    if (!library.ok() || library.schemaVersion() != storage::Library::kSchemaVersion) return fail("library");
    storage::UserMaterial material;
    material.id = "user_selfcheck";
    material.name = "Shop oak";
    material.visualHex = "#A67C52";
    std::string libraryError;
    if (!library.addMaterial(material, libraryError)) return fail(libraryError);
    storage::UserPreset preset;
    preset.id = "user_self18";
    preset.label = "18 mm";
    preset.group = "user";
    preset.thicknessNm = 18 * units::kNmPerMm;
    if (!library.addPreset(preset, libraryError)) return fail(libraryError);
  }
  storage::Library reopened(libraryPath.string());
  const auto libraryMaterials = reopened.materials();
  const auto libraryPresets = reopened.presets();
  if (!reopened.ok() || libraryMaterials.size() != 1 || libraryMaterials[0].name != "Shop oak") return fail("library read");
  if (libraryPresets.size() != 1 || libraryPresets[0].thicknessNm != 18 * units::kNmPerMm) return fail("library preset");

  const char* source = std::getenv("TIMBERLAB_SOURCE_DIR");
  if (source != nullptr) {
    std::ifstream catalogFile(std::filesystem::path(source) / "resources/data/materials.json");
    std::stringstream buffer;
    buffer << catalogFile.rdbuf();
    materials::Catalog catalog;
    std::string catalogError;
    if (!catalog.load(buffer.str(), catalogError)) return fail(catalogError);
    int species = 0;
    for (const auto& entry : catalog.entries()) {
      if (entry.category == "hardwood" || entry.category == "softwood") ++species;
      if (!entry.identityAmbiguous && entry.category != "engineered" && entry.scientificName.find(' ') == std::string::npos) {
        return fail("scientific name " + entry.id);
      }
      if (entry.densityKgM3.has_value()) return fail("unexpected density " + entry.id);
    }
    if (species < 40) return fail("species count");
  }

  constraints::DependencyGraph graph;
  graph.addEdge("width", "apron");
  graph.addEdge("apron", "tenon");
  graph.addEdge("width", "leg");
  if (graph.hasCycle()) return fail("false cycle");
  if (graph.affectedBy("width").size() != 3) return fail("affected");
  const std::vector<std::string> order = graph.topologicalOrder();
  if (order.size() != 4) return fail("topo size");
  const auto at = [&](const char* id) {
    return static_cast<int>(std::find(order.begin(), order.end(), id) - order.begin());
  };
  if (!(at("width") < at("apron") && at("apron") < at("tenon") && at("width") < at("leg"))) return fail("topo order");
  graph.addEdge("tenon", "width");
  if (!graph.hasCycle() || !graph.topologicalOrder().empty()) return fail("missed cycle");

  std::cout << "self-check ok\n";
  return 0;
}

}  // namespace timberlab
