#include "timberlab/core/document.hpp"
#include "timberlab/units/units.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <string>

using timberlab::Component;
using timberlab::Document;
using timberlab::LocalBox;
using timberlab::constraints::Binding;
using timberlab::constraints::Parameter;
using timberlab::localBox;

namespace {

constexpr std::int64_t kMm = timberlab::units::kNmPerMm;

const Component* named(const Document& document, const char* name) {
  for (const Component& component : document.components()) {
    if (component.name == name) return &component;
  }
  return nullptr;
}

const Parameter* findDriver(const Document& document, const char* prefix, const char* role) {
  const std::string head(prefix);
  const std::string tail = std::string(".") + role;
  for (const Parameter& parameter : document.parameters()) {
    if (parameter.id.compare(0, head.size(), head) != 0) continue;
    if (parameter.id.size() < tail.size()) continue;
    if (parameter.id.compare(parameter.id.size() - tail.size(), tail.size(), tail) != 0) continue;
    return &parameter;
  }
  return nullptr;
}

const Binding* bindingOf(const Document& document, const std::string& partId, const char* field) {
  for (const Binding& binding : document.bindings()) {
    if (binding.partId == partId && binding.field == field) return &binding;
  }
  return nullptr;
}

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

bool separated(const Document& document) {
  const std::vector<Component>& parts = document.components();
  for (std::size_t i = 0; i < parts.size(); ++i) {
    for (std::size_t j = i + 1; j < parts.size(); ++j) {
      if (overlaps(parts[i], parts[j])) return false;
    }
  }
  return true;
}

bool loadBoards(Document& document, int count) {
  nlohmann::json components = nlohmann::json::array();
  for (int i = 0; i < count; ++i) {
    components.push_back({{"id", "board-" + std::to_string(i)},
                          {"type", "board"},
                          {"name", "Board"},
                          {"materialId", "european-oak"},
                          {"placement", {{"x", 0}, {"y", 0}, {"z", 0}, {"rx", 0}, {"ry", 0}, {"rz", 0}}},
                          {"parameters", {{"width", 600 * kMm}, {"height", 720 * kMm}, {"thickness", 18 * kMm}, {"radius", 0}}}});
  }
  const nlohmann::json root = {{"schema", "timberlab.model"},
                               {"schemaVersion", 1},
                               {"lengthUnit", "nm"},
                               {"id", "cap"},
                               {"name", "Cap"},
                               {"displayUnit", "mm"},
                               {"fractionPrecision", 16},
                               {"components", std::move(components)}};
  std::string error;
  return Document::fromJson(root.dump(), document, error);
}

}  // namespace

TEST_CASE("the demo table keeps its short driver ids") {
  Document document;
  document.makeDemoTable();
  REQUIRE(document.parameters().size() == 9);
  REQUIRE(document.bindings().size() == 48);
  REQUIRE(document.parameters().front().id == "table.width");
  REQUIRE(document.parameters().front().name == "Width");
  REQUIRE(document.components().front().name == "Leg 1");
  REQUIRE(document.components().front().role == "leg");
  REQUIRE(document.components().front().parentId.empty());
  for (const Binding& binding : document.bindings()) REQUIRE(binding.constantNm == 0);
}

TEST_CASE("an inserted table is ordinary parts driven by its own parameters") {
  Document document;
  std::string error;
  REQUIRE(document.addAssembly("table", error));
  REQUIRE(document.components().size() == 9);
  REQUIRE(document.parameters().size() == 9);
  REQUIRE(document.bindings().size() == 48);
  REQUIRE(document.joints().empty());
  REQUIRE(document.components().front().name == "Leg 1");
  REQUIRE(document.components().front().materialId == "black-walnut");
  REQUIRE(named(document, "Front apron")->materialId == "european-beech");
  REQUIRE(named(document, "Top")->materialId == "european-oak");
  for (const Component& component : document.components()) REQUIRE(component.parentId.empty());
  for (const Parameter& parameter : document.parameters()) {
    REQUIRE(parameter.id != "table.width");
    REQUIRE(parameter.id.starts_with("table_"));
  }
  REQUIRE(findDriver(document, "table_", "width")->name == "Table width");
  REQUIRE(findDriver(document, "table_", "width")->valueNm == 1200 * kMm);

  REQUIRE(named(document, "Leg 1")->widthNm == 60 * kMm);
  REQUIRE(named(document, "Leg 1")->placement.xNm == 40 * kMm);
  REQUIRE(named(document, "Leg 2")->placement.xNm == 1100 * kMm);
  REQUIRE(named(document, "Leg 4")->placement.yNm == 600 * kMm);
  REQUIRE(named(document, "Front apron")->widthNm == 1000 * kMm);
  REQUIRE(named(document, "Front apron")->placement.zNm == 560 * kMm);
  REQUIRE(named(document, "Back apron")->placement.yNm == 640 * kMm);
  REQUIRE(named(document, "Left apron")->thicknessNm == 500 * kMm);
  REQUIRE(named(document, "Right apron")->placement.xNm == 1140 * kMm);
  REQUIRE(named(document, "Top")->placement.xNm == 0);
  REQUIRE(named(document, "Top")->widthNm == 1200 * kMm);
  REQUIRE(named(document, "Top")->thicknessNm == 700 * kMm);
  REQUIRE(named(document, "Top")->heightNm == 20 * kMm);
  REQUIRE(named(document, "Top")->placement.zNm == 720 * kMm);
  REQUIRE(separated(document));
  for (const Binding& binding : document.bindings()) REQUIRE(binding.constantNm == 0);

  const std::string widthId = findDriver(document, "table_", "width")->id;
  REQUIRE(document.setParameter(widthId, 1500 * kMm, error));
  REQUIRE(named(document, "Top")->widthNm == 1500 * kMm);
  REQUIRE(named(document, "Front apron")->widthNm == 1300 * kMm);
  REQUIRE(named(document, "Leg 2")->placement.xNm == 1400 * kMm);
  REQUIRE(named(document, "Leg 4")->placement.xNm == 1400 * kMm);
  REQUIRE(named(document, "Right apron")->placement.xNm == 1440 * kMm);
  REQUIRE(separated(document));

  REQUIRE(document.undo(error));
  REQUIRE(named(document, "Top")->widthNm == 1200 * kMm);
  REQUIRE(named(document, "Front apron")->widthNm == 1000 * kMm);
  REQUIRE(named(document, "Leg 2")->placement.xNm == 1100 * kMm);

  const std::string topId = named(document, "Top")->id;
  const std::string before = document.toJson();
  REQUIRE_FALSE(document.setDimension(topId, "width", 1500 * kMm, error));
  REQUIRE(error == "parameter.driven");
  REQUIRE(document.toJson() == before);
  REQUIRE(document.unbindField(topId, "width", error));
  REQUIRE(document.setDimension(topId, "width", 1500 * kMm, error));
  REQUIRE(named(document, "Top")->widthNm == 1500 * kMm);
}

TEST_CASE("a carcass is five touching boards and rejects a collapsed width") {
  Document document;
  std::string error;
  REQUIRE(document.addAssembly("carcass", error));
  REQUIRE(document.components().size() == 5);
  REQUIRE(document.parameters().size() == 6);
  REQUIRE(document.bindings().size() == 22);
  REQUIRE(document.joints().empty());
  REQUIRE(separated(document));
  REQUIRE(named(document, "Carcass left")->materialId == "birch-plywood");
  REQUIRE(named(document, "Carcass left")->placement.xNm == 0);
  REQUIRE(named(document, "Carcass left")->widthNm == 18 * kMm);
  REQUIRE(named(document, "Carcass left")->thicknessNm == 500 * kMm);
  REQUIRE(named(document, "Carcass left")->heightNm == 720 * kMm);
  REQUIRE(named(document, "Carcass right")->placement.xNm == 582 * kMm);
  REQUIRE(named(document, "Carcass bottom")->placement.xNm == 18 * kMm);
  REQUIRE(named(document, "Carcass bottom")->widthNm == 564 * kMm);
  REQUIRE(named(document, "Carcass bottom")->thicknessNm == 494 * kMm);
  REQUIRE(named(document, "Carcass bottom")->heightNm == 18 * kMm);
  REQUIRE(named(document, "Carcass top")->placement.zNm == 702 * kMm);
  REQUIRE(named(document, "Carcass top")->widthNm == 564 * kMm);
  REQUIRE(named(document, "Carcass back")->placement.yNm == 494 * kMm);
  REQUIRE(named(document, "Carcass back")->placement.zNm == 18 * kMm);
  REQUIRE(named(document, "Carcass back")->thicknessNm == 6 * kMm);
  REQUIRE(named(document, "Carcass back")->heightNm == 684 * kMm);
  REQUIRE(findDriver(document, "carcass_", "width")->name == "Cabinet width");

  const std::string widthId = findDriver(document, "carcass_", "width")->id;
  REQUIRE(document.setParameter(widthId, 900 * kMm, error));
  REQUIRE(named(document, "Carcass right")->placement.xNm == 882 * kMm);
  REQUIRE(named(document, "Carcass bottom")->widthNm == 864 * kMm);
  REQUIRE(named(document, "Carcass top")->widthNm == 864 * kMm);
  REQUIRE(named(document, "Carcass back")->widthNm == 864 * kMm);
  REQUIRE(separated(document));
  REQUIRE(document.undo(error));
  REQUIRE(named(document, "Carcass right")->placement.xNm == 582 * kMm);
  REQUIRE(named(document, "Carcass bottom")->widthNm == 564 * kMm);

  const std::string before = document.toJson();
  REQUIRE_FALSE(document.setParameter(widthId, 30 * kMm, error));
  REQUIRE(error == "parameter.range");
  REQUIRE(document.toJson() == before);

  const nlohmann::json saved = nlohmann::json::parse(before);
  REQUIRE_FALSE(saved.contains("assemblies"));
  Document loaded;
  REQUIRE(Document::fromJson(before, loaded, error));
  REQUIRE(loaded.components().size() == 5);
  REQUIRE(loaded.bindings().size() == 22);
  REQUIRE(loaded.setParameter(widthId, 900 * kMm, error));
  REQUIRE(named(loaded, "Carcass right")->placement.xNm == 882 * kMm);
}

TEST_CASE("a drawer box grows in height without changing the bottom") {
  Document document;
  std::string error;
  REQUIRE(document.addAssembly("drawer", error));
  REQUIRE(document.components().size() == 5);
  REQUIRE(document.parameters().size() == 5);
  REQUIRE(document.bindings().size() == 23);
  REQUIRE(document.joints().empty());
  REQUIRE(separated(document));
  REQUIRE(named(document, "Drawer left")->materialId == "european-beech");
  REQUIRE(named(document, "Drawer front")->materialId == "european-beech");
  REQUIRE(named(document, "Drawer bottom")->materialId == "poplar-plywood");
  REQUIRE(named(document, "Drawer left")->widthNm == 12 * kMm);
  REQUIRE(named(document, "Drawer left")->thicknessNm == 400 * kMm);
  REQUIRE(named(document, "Drawer left")->heightNm == 140 * kMm);
  REQUIRE(named(document, "Drawer right")->placement.xNm == 488 * kMm);
  REQUIRE(named(document, "Drawer front")->placement.xNm == 12 * kMm);
  REQUIRE(named(document, "Drawer front")->placement.yNm == 0);
  REQUIRE(named(document, "Drawer front")->placement.zNm == 6 * kMm);
  REQUIRE(named(document, "Drawer front")->widthNm == 476 * kMm);
  REQUIRE(named(document, "Drawer front")->thicknessNm == 12 * kMm);
  REQUIRE(named(document, "Drawer front")->heightNm == 134 * kMm);
  REQUIRE(named(document, "Drawer back")->placement.yNm == 388 * kMm);
  REQUIRE(named(document, "Drawer bottom")->placement.yNm == 12 * kMm);
  REQUIRE(named(document, "Drawer bottom")->placement.zNm == 0);
  REQUIRE(named(document, "Drawer bottom")->widthNm == 476 * kMm);
  REQUIRE(named(document, "Drawer bottom")->thicknessNm == 376 * kMm);
  REQUIRE(named(document, "Drawer bottom")->heightNm == 6 * kMm);
  REQUIRE(findDriver(document, "drawer_", "bottom")->name == "Drawer bottom");

  const std::string heightId = findDriver(document, "drawer_", "height")->id;
  REQUIRE(document.setParameter(heightId, 200 * kMm, error));
  REQUIRE(named(document, "Drawer left")->heightNm == 200 * kMm);
  REQUIRE(named(document, "Drawer right")->heightNm == 200 * kMm);
  REQUIRE(named(document, "Drawer front")->heightNm == 194 * kMm);
  REQUIRE(named(document, "Drawer back")->heightNm == 194 * kMm);
  REQUIRE(named(document, "Drawer bottom")->heightNm == 6 * kMm);
  REQUIRE(separated(document));

  // The bottom's stock thickness is its local Z, stored as height.
  const std::string bottomId = named(document, "Drawer bottom")->id;
  const std::string before = document.toJson();
  REQUIRE_FALSE(document.setDimension(bottomId, "height", 8 * kMm, error));
  REQUIRE(error == "parameter.driven");
  REQUIRE(document.toJson() == before);
  REQUIRE(document.unbindField(bottomId, "height", error));
  REQUIRE(document.setDimension(bottomId, "height", 8 * kMm, error));
  REQUIRE(named(document, "Drawer bottom")->heightNm == 8 * kMm);
}

TEST_CASE("a second assembly keeps the origin it was given") {
  Document document;
  std::string error;
  REQUIRE(document.addAssembly("table", error));
  REQUIRE(document.addAssembly("carcass", error));
  REQUIRE(named(document, "Leg 1")->placement.xNm == 40 * kMm);
  REQUIRE(named(document, "Carcass left")->placement.xNm == 1220 * kMm);
  REQUIRE(bindingOf(document, named(document, "Carcass left")->id, "x") == nullptr);
  const Binding* rightX = bindingOf(document, named(document, "Carcass right")->id, "x");
  REQUIRE(rightX != nullptr);
  REQUIRE(rightX->constantNm == 1220 * kMm);
  REQUIRE(separated(document));

  const std::string tableWidth = findDriver(document, "table_", "width")->id;
  REQUIRE(document.setParameter(tableWidth, 1500 * kMm, error));
  REQUIRE(named(document, "Top")->widthNm == 1500 * kMm);
  REQUIRE(named(document, "Leg 1")->placement.xNm == 40 * kMm);
  REQUIRE(named(document, "Carcass left")->placement.xNm == 1220 * kMm);
  REQUIRE(named(document, "Carcass right")->placement.xNm == 582 * kMm + 1220 * kMm);

  Document beside;
  const std::string board = beside.addBoard();
  REQUIRE(beside.addAssembly("table", error));
  REQUIRE(beside.find(board)->name == "Board 1");
  REQUIRE(beside.find(board)->widthNm == 600 * kMm);
  REQUIRE(named(beside, "Leg 1")->placement.xNm == 660 * kMm);
  REQUIRE(named(beside, "Top")->placement.xNm == 620 * kMm);
}

TEST_CASE("two tables do not share a driver") {
  Document document;
  std::string error;
  REQUIRE(document.addAssembly("table", error));
  REQUIRE(document.addAssembly("table", error));
  REQUIRE(document.components().size() == 18);
  REQUIRE(document.parameters().size() == 18);
  std::string firstTop;
  std::string secondTop;
  for (const Component& component : document.components()) {
    if (component.name != "Top") continue;
    if (component.placement.xNm == 0) firstTop = component.id;
    else secondTop = component.id;
  }
  REQUIRE_FALSE(firstTop.empty());
  REQUIRE(named(document, "Top")->placement.xNm == 0);
  std::string firstWidth;
  for (const Binding& binding : document.bindings()) {
    if (binding.partId != firstTop || binding.field != "width") continue;
    REQUIRE(binding.terms.size() == 1);
    firstWidth = binding.terms[0].parameterId;
  }
  REQUIRE(firstWidth.starts_with("table_"));
  REQUIRE(document.setParameter(firstWidth, 1500 * kMm, error));
  const Component* wide = nullptr;
  const Component* other = nullptr;
  for (const Component& component : document.components()) {
    if (component.name != "Top") continue;
    if (component.id == firstTop) wide = &component;
    if (component.id == secondTop) other = &component;
  }
  REQUIRE(wide != nullptr);
  REQUIRE(other != nullptr);
  REQUIRE(wide->widthNm == 1500 * kMm);
  REQUIRE(wide->placement.xNm == 0);
  REQUIRE(other->widthNm == 1200 * kMm);
  REQUIRE(other->placement.xNm == 1220 * kMm);
}

TEST_CASE("an unknown assembly leaves the document unchanged") {
  Document document;
  document.addBoard();
  const std::string before = document.toJson();
  std::string error;
  REQUIRE_FALSE(document.addAssembly("nope", error));
  REQUIRE(error == "assembly.unknown");
  REQUIRE(document.toJson() == before);
}

TEST_CASE("a turned board reserves its world width, not its local width") {
  Document document;
  std::string error;
  const std::string id = document.addBoard();
  REQUIRE(document.setRotation(id, "z", 90, error));
  REQUIRE(document.addAssembly("drawer", error));
  const std::int64_t x = named(document, "Drawer left")->placement.xNm;
  REQUIRE(x > 0);
  REQUIRE(x < 100 * kMm);
}

TEST_CASE("an assembly does not cut a joint and does not drop one") {
  Document document;
  const std::string shelf = document.addBoard();
  const std::string rail = document.addBoard();
  std::string error;
  REQUIRE_FALSE(document.addJoint("joint.mortise_tenon", rail, shelf, error).empty());
  REQUIRE(document.addAssembly("drawer", error));
  REQUIRE(document.joints().size() == 1);
  REQUIRE(document.components().size() == 7);
  REQUIRE(named(document, "Drawer left") != nullptr);
  REQUIRE(separated(document));
}

TEST_CASE("the component cap rejects a carcass and keeps the parts") {
  Document crowded;
  std::string error;
  REQUIRE(loadBoards(crowded, 1996));
  REQUIRE(crowded.components().size() == 1996);
  const std::string before = crowded.toJson();
  REQUIRE_FALSE(crowded.addAssembly("carcass", error));
  REQUIRE(error == "storage.too_many");
  REQUIRE(crowded.components().size() == 1996);
  REQUIRE(crowded.toJson() == before);

  Document exact;
  REQUIRE(loadBoards(exact, 1995));
  REQUIRE(exact.addAssembly("carcass", error));
  REQUIRE(exact.components().size() == 2000);
}

TEST_CASE("a frame is four touching rails driven by its stock") {
  Document document;
  std::string error;
  REQUIRE(document.addAssembly("frame", error));
  REQUIRE(document.components().size() == 4);
  REQUIRE(document.parameters().size() == 4);
  REQUIRE(document.joints().empty());
  REQUIRE(named(document, "Frame left")->role == "stile");
  REQUIRE(named(document, "Frame left")->parentId.empty());
  REQUIRE(named(document, "Frame left")->placement.xNm == 0);
  REQUIRE(named(document, "Frame left")->widthNm == 60 * kMm);
  REQUIRE(named(document, "Frame right")->placement.xNm == 540 * kMm);
  REQUIRE(named(document, "Frame right")->role == "stile");
  REQUIRE(named(document, "Frame bottom")->widthNm == 480 * kMm);
  REQUIRE(named(document, "Frame bottom")->role == "rail");
  REQUIRE(named(document, "Frame top")->placement.zNm == 740 * kMm);
  REQUIRE(named(document, "Frame top")->role == "rail");
  REQUIRE(separated(document));

  const std::string widthId = findDriver(document, "frame_", "width")->id;
  REQUIRE(document.setParameter(widthId, 900 * kMm, error));
  REQUIRE(named(document, "Frame right")->placement.xNm == 840 * kMm);
  REQUIRE(named(document, "Frame bottom")->widthNm == 780 * kMm);
  REQUIRE(document.undo(error));
  REQUIRE(named(document, "Frame right")->placement.xNm == 540 * kMm);
}

TEST_CASE("shelving spaces the shelves with a bay") {
  Document document;
  std::string error;
  REQUIRE(document.addAssembly("shelving", error));
  REQUIRE(document.components().size() == 6);
  REQUIRE(document.parameters().size() == 5);
  REQUIRE(named(document, "Shelf left")->role == "side");
  REQUIRE(named(document, "Shelf left")->parentId.empty());
  REQUIRE(named(document, "Shelf left")->heightNm == 972 * kMm);
  REQUIRE(named(document, "Shelf bottom")->role == "shelf");
  REQUIRE(named(document, "Shelf bottom")->widthNm == 764 * kMm);
  REQUIRE(named(document, "Shelf mid 1")->placement.zNm == 318 * kMm);
  REQUIRE(named(document, "Shelf mid 2")->placement.zNm == 636 * kMm);
  REQUIRE(named(document, "Shelf top")->placement.zNm == 954 * kMm);
  REQUIRE(separated(document));

  const std::string bayId = findDriver(document, "shelving_", "bay")->id;
  REQUIRE(document.setParameter(bayId, 400 * kMm, error));
  REQUIRE(named(document, "Shelf left")->heightNm == 1272 * kMm);
  REQUIRE(named(document, "Shelf mid 1")->placement.zNm == 418 * kMm);
  REQUIRE(document.undo(error));
  REQUIRE(named(document, "Shelf left")->heightNm == 972 * kMm);
}

TEST_CASE("a bedside table keeps the lower shelf still when the height changes") {
  Document document;
  std::string error;
  REQUIRE(document.addAssembly("bedside", error));
  REQUIRE(document.components().size() == 11);
  REQUIRE(document.parameters().size() == 8);
  REQUIRE(document.joints().empty());
  REQUIRE(named(document, "Bedside leg 1")->role == "leg");
  REQUIRE(named(document, "Bedside leg 1")->parentId.empty());
  REQUIRE(named(document, "Bedside leg 1")->heightNm == 530 * kMm);
  REQUIRE(named(document, "Bedside top")->role == "top");
  REQUIRE(named(document, "Bedside top")->placement.zNm == 530 * kMm);
  REQUIRE(named(document, "Bedside shelf")->role == "shelf");
  REQUIRE(named(document, "Bedside shelf")->placement.zNm == 120 * kMm);
  REQUIRE(named(document, "Bedside shelf")->heightNm == 18 * kMm);
  REQUIRE(named(document, "Bedside drawer left")->role == "side");
  REQUIRE(named(document, "Bedside drawer left")->placement.zNm == 350 * kMm);
  REQUIRE(named(document, "Bedside drawer front")->role == "front");
  REQUIRE(named(document, "Bedside drawer back")->role == "back");
  REQUIRE(named(document, "Bedside drawer bottom")->role == "bottom");
  REQUIRE(named(document, "Bedside drawer bottom")->heightNm == 6 * kMm);
  REQUIRE(separated(document));

  const std::string heightId = findDriver(document, "bedside_", "height")->id;
  REQUIRE(document.setParameter(heightId, 600 * kMm, error));
  REQUIRE(named(document, "Bedside leg 1")->heightNm == 580 * kMm);
  REQUIRE(named(document, "Bedside drawer left")->placement.zNm == 400 * kMm);
  REQUIRE(named(document, "Bedside shelf")->placement.zNm == 120 * kMm);
  REQUIRE(named(document, "Bedside shelf")->heightNm == 18 * kMm);
  REQUIRE(document.undo(error));
  REQUIRE(named(document, "Bedside drawer left")->placement.zNm == 350 * kMm);
}
