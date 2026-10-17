#include "timberlab/constraints/binding.hpp"
#include "timberlab/core/document.hpp"
#include "timberlab/units/units.hpp"

#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <limits>
#include <string>

using timberlab::Component;
using timberlab::Document;
using timberlab::LocalBox;
using timberlab::constraints::Binding;
using timberlab::constraints::Term;
using timberlab::localBox;

namespace {

constexpr std::int64_t kMm = timberlab::units::kNmPerMm;

const Component* named(const Document& document, const char* name) {
  for (const Component& component : document.components()) {
    if (component.name == name) return &component;
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

Binding widthFrom(const std::string& target, const std::string& source) {
  Binding binding;
  binding.partId = target;
  binding.field = "width";
  Term term;
  term.partId = source;
  term.field = "width";
  term.coefficient = 1;
  binding.terms.push_back(std::move(term));
  return binding;
}

}  // namespace

TEST_CASE("a part field chains through another part") {
  Document document;
  const std::string first = document.addBoard();
  const std::string second = document.addBoard();
  const std::string third = document.addBoard();
  std::string error;
  REQUIRE(document.setBinding(widthFrom(second, first), error));
  Binding place;
  place.partId = third;
  place.field = "x";
  Term term;
  term.partId = second;
  term.field = "width";
  term.coefficient = 1;
  place.terms.push_back(term);
  REQUIRE(document.setBinding(place, error));
  REQUIRE(document.setDimension(first, "width", 800 * kMm, error));
  REQUIRE(document.find(second)->widthNm == 800 * kMm);
  REQUIRE(document.find(third)->placement.xNm == 800 * kMm);
}

TEST_CASE("a cycle is rejected and leaves the document unchanged") {
  Document document;
  const std::string first = document.addBoard();
  const std::string second = document.addBoard();
  std::string error;
  REQUIRE(document.setBinding(widthFrom(second, first), error));
  const std::string before = document.toJson();
  REQUIRE_FALSE(document.setBinding(widthFrom(first, second), error));
  REQUIRE(error == "parameter.cycle");
  REQUIRE(document.toJson() == before);
}

TEST_CASE("the demo table follows its drivers") {
  Document document;
  document.makeDemoTable();
  std::string error;
  REQUIRE(document.parameters().size() == 9);
  REQUIRE(document.bindings().size() == 48);
  REQUIRE(named(document, "Leg 1")->widthNm == 60 * kMm);
  REQUIRE(named(document, "Leg 1")->placement.xNm == 40 * kMm);
  REQUIRE(named(document, "Leg 2")->placement.xNm == 1100 * kMm);
  REQUIRE(named(document, "Leg 4")->placement.yNm == 600 * kMm);
  REQUIRE(named(document, "Front apron")->widthNm == 1000 * kMm);
  REQUIRE(named(document, "Front apron")->placement.zNm == 560 * kMm);
  REQUIRE(named(document, "Back apron")->placement.yNm == 640 * kMm);
  REQUIRE(named(document, "Left apron")->thicknessNm == 500 * kMm);
  REQUIRE(named(document, "Right apron")->placement.xNm == 1140 * kMm);
  REQUIRE(named(document, "Top")->widthNm == 1200 * kMm);
  REQUIRE(named(document, "Top")->thicknessNm == 700 * kMm);
  REQUIRE(named(document, "Top")->heightNm == 20 * kMm);
  REQUIRE(named(document, "Top")->placement.zNm == 720 * kMm);
  REQUIRE(separated(document));

  REQUIRE(document.setParameter("table.width", 1500 * kMm, error));
  REQUIRE(named(document, "Top")->widthNm == 1500 * kMm);
  REQUIRE(named(document, "Front apron")->widthNm == 1300 * kMm);
  REQUIRE(named(document, "Back apron")->widthNm == 1300 * kMm);
  REQUIRE(named(document, "Leg 2")->placement.xNm == 1400 * kMm);
  REQUIRE(named(document, "Leg 4")->placement.xNm == 1400 * kMm);
  REQUIRE(named(document, "Right apron")->placement.xNm == 1440 * kMm);
  REQUIRE(separated(document));

  REQUIRE(document.undo(error));
  REQUIRE(named(document, "Top")->widthNm == 1200 * kMm);
  REQUIRE(named(document, "Front apron")->widthNm == 1000 * kMm);
  REQUIRE(named(document, "Leg 2")->placement.xNm == 1100 * kMm);
  REQUIRE(separated(document));

  const std::string topId = named(document, "Top")->id;
  REQUIRE_FALSE(document.setDimension(topId, "width", 1500 * kMm, error));
  REQUIRE(error == "parameter.driven");
  REQUIRE(named(document, "Top")->widthNm == 1200 * kMm);

  const std::string before = document.toJson();
  REQUIRE_FALSE(document.setParameter("table.width", 100 * kMm, error));
  REQUIRE(error == "parameter.range");
  REQUIRE(document.toJson() == before);
  REQUIRE_FALSE(document.setParameter("table.width", -1, error));
  REQUIRE(error == "parameter.range");
  REQUIRE(document.toJson() == before);
  REQUIRE_FALSE(document.setParameter("missing", 10 * kMm, error));
  REQUIRE(error == "parameter.missing");

  REQUIRE(document.setLocked(topId, true, error));
  REQUIRE(document.setParameter("table.width", 1400 * kMm, error));
  REQUIRE(named(document, "Top")->widthNm == 1400 * kMm);
  REQUIRE_FALSE(document.setDimension(topId, "width", 1500 * kMm, error));
  REQUIRE(error == "component.locked");
}

TEST_CASE("bindings round trip and outrank a stored size") {
  Document fresh;
  fresh.addBoard();
  const nlohmann::json plain = nlohmann::json::parse(fresh.toJson());
  REQUIRE_FALSE(plain.contains("parameters"));
  REQUIRE_FALSE(plain.contains("bindings"));

  Document document;
  document.makeDemoTable();
  std::string error;
  nlohmann::json edited = nlohmann::json::parse(document.toJson());
  bool found = false;
  for (auto& component : edited["components"]) {
    if (component["name"] != "Top") continue;
    component["parameters"]["width"] = 50 * kMm;
    found = true;
  }
  REQUIRE(found);
  Document loaded;
  loaded.addBoard();
  const std::string untouched = loaded.toJson();
  REQUIRE(Document::fromJson(edited.dump(), loaded, error));
  REQUIRE(named(loaded, "Top")->widthNm == 1200 * kMm);
  REQUIRE(loaded.parameters().size() == 9);

  Document rejected;
  rejected.addBoard();
  const std::string kept = rejected.toJson();
  edited["parameters"][0]["id"] = "table.depth";
  REQUIRE_FALSE(Document::fromJson(edited.dump(), rejected, error));
  REQUIRE(error == "storage.duplicate");
  REQUIRE(rejected.toJson() == kept);
  REQUIRE(untouched.size() > 0);
}

TEST_CASE("a joint is checked after bindings recompute the parts") {
  Document document;
  const std::string shelf = document.addBoard();
  const std::string rail = document.addBoard();
  std::string error;
  REQUIRE_FALSE(document.addJoint("joint.mortise_tenon", rail, shelf, error).empty());
  REQUIRE(document.addParameter("span", "Span", document.find(shelf)->widthNm, error));
  REQUIRE(document.bindField(shelf, "width", "span", error));
  nlohmann::json root = nlohmann::json::parse(document.toJson());
  root["parameters"][0]["value"] = kMm;
  Document destination;
  destination.addBoard();
  const std::string before = destination.toJson();
  REQUIRE_FALSE(Document::fromJson(root.dump(), destination, error));
  REQUIRE(error == "joint.parameter");
  REQUIRE(destination.toJson() == before);

  Document consistent;
  REQUIRE(Document::fromJson(document.toJson(), consistent, error));
  REQUIRE(consistent.joints().size() == 1);
  REQUIRE(consistent.find(shelf)->widthNm == document.find(shelf)->widthNm);
}

TEST_CASE("deleting a part drops bindings that use it") {
  Document document;
  const std::string first = document.addBoard();
  const std::string second = document.addBoard();
  std::string error;
  REQUIRE(document.setBinding(widthFrom(second, first), error));
  const std::int64_t width = document.find(second)->widthNm;
  REQUIRE(document.remove(first, error));
  REQUIRE(document.bindings().empty());
  REQUIRE(document.find(second)->widthNm == width);
  REQUIRE(document.setDimension(second, "width", 500 * kMm, error));
  REQUIRE(document.find(second)->widthNm == 500 * kMm);
}

TEST_CASE("a duplicate keeps the size and not the binding") {
  Document document;
  document.makeDemoTable();
  std::string error;
  const std::string topId = named(document, "Top")->id;
  std::string copyId;
  REQUIRE(document.duplicate(topId, copyId, error));
  REQUIRE(document.setDimension(copyId, "width", 400 * kMm, error));
  REQUIRE(document.find(copyId)->widthNm == 400 * kMm);
  REQUIRE_FALSE(document.setDimension(topId, "width", 400 * kMm, error));
  REQUIRE(error == "parameter.driven");
}

TEST_CASE("releasing a field keeps the last size") {
  Document document;
  const std::string id = document.addBoard();
  std::string error;
  REQUIRE(document.addParameter("span", "Span", 400 * kMm, error));
  REQUIRE(document.bindField(id, "width", "span", error));
  REQUIRE(document.find(id)->widthNm == 400 * kMm);
  REQUIRE(document.unbindField(id, "width", error));
  REQUIRE(document.find(id)->widthNm == 400 * kMm);
  REQUIRE(document.bindings().empty());
  REQUIRE(document.setDimension(id, "width", 250 * kMm, error));
  REQUIRE(document.find(id)->widthNm == 250 * kMm);
  REQUIRE_FALSE(document.unbindField(id, "width", error));
  REQUIRE(error == "parameter.unknown");
}

TEST_CASE("an overflowing coefficient does not change the part") {
  Document document;
  const std::string id = document.addBoard();
  std::string error;
  REQUIRE(document.addParameter("span", "Span", 2, error));
  const std::string before = document.toJson();
  Binding binding;
  binding.partId = id;
  binding.field = "x";
  Term term;
  term.parameterId = "span";
  term.coefficient = std::numeric_limits<std::int64_t>::max();
  binding.terms.push_back(term);
  REQUIRE_FALSE(document.setBinding(binding, error));
  REQUIRE(error == "parameter.range");
  REQUIRE(document.toJson() == before);

  binding.terms[0].coefficient = 1;
  binding.constantNm = std::numeric_limits<std::int64_t>::max();
  REQUIRE_FALSE(document.setBinding(binding, error));
  REQUIRE(error == "parameter.range");
  REQUIRE(document.toJson() == before);
}

TEST_CASE("a constant binding writes that length") {
  Document document;
  const std::string id = document.addBoard();
  std::string error;
  Binding binding;
  binding.partId = id;
  binding.field = "x";
  binding.constantNm = 25 * kMm;
  REQUIRE(document.setBinding(binding, error));
  REQUIRE(document.find(id)->placement.xNm == 25 * kMm);
}

TEST_CASE("a size that makes a stored joint not fit is rejected") {
  Document document;
  const std::string shelf = document.addBoard();
  const std::string rail = document.addBoard();
  std::string error;
  REQUIRE_FALSE(document.addJoint("joint.mortise_tenon", rail, shelf, error).empty());
  const std::string before = document.toJson();

  REQUIRE_FALSE(document.setDimension(shelf, "width", 10 * kMm, error));
  REQUIRE(error == "joint.parameter");
  REQUIRE(document.toJson() == before);

  REQUIRE(document.setDimension(shelf, "width", 400 * kMm, error));
  REQUIRE(document.find(shelf)->widthNm == 400 * kMm);
  Document loaded;
  REQUIRE(Document::fromJson(document.toJson(), loaded, error));
  REQUIRE(loaded.joints().size() == 1);
  REQUIRE(loaded.find(shelf)->widthNm == 400 * kMm);

  REQUIRE(document.addParameter("span", "Span", 400 * kMm, error));
  REQUIRE(document.bindField(shelf, "width", "span", error));
  const std::string driven = document.toJson();
  REQUIRE_FALSE(document.setParameter("span", 10 * kMm, error));
  REQUIRE(error == "joint.parameter");
  REQUIRE(document.toJson() == driven);
}

TEST_CASE("a lock blocks driving and releasing a field") {
  Document document;
  const std::string id = document.addBoard();
  std::string error;
  REQUIRE(document.addParameter("span", "Span", 400 * kMm, error));
  REQUIRE(document.setLocked(id, true, error));
  const std::string locked = document.toJson();
  REQUIRE_FALSE(document.bindField(id, "width", "span", error));
  REQUIRE(error == "component.locked");
  REQUIRE(document.toJson() == locked);
  REQUIRE_FALSE(document.unbindField(id, "width", error));
  REQUIRE(error == "component.locked");
  REQUIRE(document.toJson() == locked);

  REQUIRE(document.setLocked(id, false, error));
  REQUIRE(document.bindField(id, "width", "span", error));
  REQUIRE(document.find(id)->widthNm == 400 * kMm);
  REQUIRE(document.setLocked(id, true, error));
  const std::string bound = document.toJson();
  REQUIRE_FALSE(document.unbindField(id, "width", error));
  REQUIRE(error == "component.locked");
  REQUIRE(document.toJson() == bound);
  REQUIRE(document.setParameter("span", 500 * kMm, error));
  REQUIRE(document.find(id)->widthNm == 500 * kMm);

  REQUIRE_FALSE(document.unbindField("missing", "width", error));
  REQUIRE(error == "component.missing");
}
