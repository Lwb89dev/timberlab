#include "timberlab/core/document.hpp"

#include "timberlab/units/units.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <random>

namespace timberlab {
namespace {

constexpr std::size_t kHistoryLimit = 100;
constexpr std::int64_t kMm = units::kNmPerMm;
constexpr std::int64_t kMaxPlacementNm = units::kMaxPartNm * 10;

std::string makeId() {
  thread_local std::mt19937_64 rng = [] {
    std::random_device device;
    std::seed_seq seed{device(), device(), device(), device()};
    return std::mt19937_64(seed);
  }();
  std::uniform_int_distribution<std::uint64_t> dist;
  const std::uint64_t high = dist(rng);
  const std::uint64_t low = dist(rng);
  char buffer[40];
  std::snprintf(buffer, sizeof(buffer), "tl_%016llx%016llx", static_cast<unsigned long long>(high),
                static_cast<unsigned long long>(low));
  return buffer;
}

bool requireInteger(const nlohmann::json& object, const char* key, std::int64_t& out, std::string& error) {
  if (!object.contains(key) || !object.at(key).is_number_integer()) {
    error = "storage.json";
    return false;
  }
  out = object.at(key).get<std::int64_t>();
  return true;
}

bool positiveDimension(std::int64_t nm, std::string& error) {
  if (nm <= 0) {
    error = "dimension.non_positive";
    return false;
  }
  if (nm > units::kMaxPartNm) {
    error = "dimension.too_large";
    return false;
  }
  return true;
}

}  // namespace

Document::Document() { newProject("mm", 16); }

void Document::newProject(const std::string& displayUnit, int fractionPrecision) {
  id_ = makeId();
  name_ = "Untitled";
  displayUnit_ = displayUnit.empty() ? "mm" : displayUnit;
  fractionPrecision_ = units::validFractionDenom(fractionPrecision) ? fractionPrecision : 16;
  components_.clear();
  resetHistory();
}

void Document::markSaved() {
  savedIndex_ = historyIndex_;
  savedValid_ = true;
}

void Document::resetHistory() {
  history_.clear();
  history_.push_back(toJson());
  historyIndex_ = 0;
  savedIndex_ = 0;
  savedValid_ = true;
}

void Document::pushHistory() {
  const std::string now = toJson();
  if (history_[historyIndex_] == now) return;
  if (historyIndex_ + 1 < history_.size()) {
    history_.resize(historyIndex_ + 1);
    if (savedValid_ && savedIndex_ > historyIndex_) savedValid_ = false;
  }
  history_.push_back(now);
  historyIndex_ = history_.size() - 1;
  if (history_.size() > kHistoryLimit) {
    const std::size_t drop = history_.size() - kHistoryLimit;
    history_.erase(history_.begin(), history_.begin() + static_cast<std::ptrdiff_t>(drop));
    historyIndex_ = history_.size() - 1;
    if (savedValid_) {
      if (savedIndex_ < drop) savedValid_ = false;
      else savedIndex_ -= drop;
    }
  }
}

bool Document::restoreCurrent(std::string& error) {
  Document parsed;
  if (!parseInto(history_[historyIndex_], parsed, error)) return false;
  id_ = std::move(parsed.id_);
  name_ = std::move(parsed.name_);
  displayUnit_ = std::move(parsed.displayUnit_);
  fractionPrecision_ = parsed.fractionPrecision_;
  components_ = std::move(parsed.components_);
  return true;
}

Component* Document::findMutable(const std::string& id) {
  for (Component& component : components_) {
    if (component.id == id) return &component;
  }
  return nullptr;
}

const Component* Document::find(const std::string& id) const {
  for (const Component& component : components_) {
    if (component.id == id) return &component;
  }
  return nullptr;
}

bool Document::editable(const Component& component, std::string& error) const {
  if (!component.locked) return true;
  error = "component.locked";
  return false;
}

std::string Document::addComponent(Component component, bool autoPlace) {
  if (autoPlace) {
    std::int64_t nextX = 0;
    constexpr std::int64_t kGap = 20 * kMm;
    for (const Component& existing : components_) {
      const LocalBox box = localBox(existing);
      const std::int64_t end =
          existing.placement.xNm + static_cast<std::int64_t>(std::llround(box.sizeXmm * static_cast<double>(kMm)));
      if (end + kGap > nextX) nextX = end + kGap;
    }
    component.placement.xNm = nextX;
  }
  component.id = makeId();
  components_.push_back(std::move(component));
  pushHistory();
  return components_.back().id;
}

std::string Document::addBoard() {
  Component component;
  component.type = ComponentType::Board;
  component.widthNm = 600 * kMm;
  component.heightNm = 720 * kMm;
  component.thicknessNm = 18 * kMm;
  int boards = 1;
  for (const Component& existing : components_) {
    if (existing.type == ComponentType::Board) ++boards;
  }
  component.name = "Board " + std::to_string(boards);
  return addComponent(std::move(component), true);
}

std::string Document::addBox() {
  Component component;
  component.type = ComponentType::Box;
  int count = 1;
  for (const Component& existing : components_) {
    if (existing.type == ComponentType::Box) ++count;
  }
  component.name = "Box " + std::to_string(count);
  component.widthNm = 100 * kMm;
  component.heightNm = 100 * kMm;
  component.thicknessNm = 100 * kMm;
  return addComponent(std::move(component), true);
}

std::string Document::addCylinder() {
  Component component;
  component.type = ComponentType::Cylinder;
  int count = 1;
  for (const Component& existing : components_) {
    if (existing.type == ComponentType::Cylinder) ++count;
  }
  component.name = "Cylinder " + std::to_string(count);
  component.radiusNm = 20 * kMm;
  component.heightNm = 100 * kMm;
  return addComponent(std::move(component), true);
}

bool Document::duplicate(const std::string& id, std::string& newId, std::string& error) {
  const Component* source = find(id);
  if (source == nullptr) {
    error = "component.missing";
    return false;
  }
  Component copy = *source;
  copy.locked = false;
  copy.name += " copy";
  copy.parentId.clear();
  const LocalBox box = localBox(*source);
  copy.placement.xNm =
      source->placement.xNm + static_cast<std::int64_t>(std::llround(box.sizeXmm * static_cast<double>(kMm))) + 20 * kMm;
  newId = addComponent(std::move(copy), false);
  return true;
}

bool Document::remove(const std::string& id, std::string& error) {
  const Component* component = find(id);
  if (component == nullptr) {
    error = "component.missing";
    return false;
  }
  if (!editable(*component, error)) return false;
  std::vector<Component> kept;
  kept.reserve(components_.size());
  for (const Component& item : components_) {
    if (item.id != id) kept.push_back(item);
  }
  components_ = std::move(kept);
  pushHistory();
  return true;
}

bool Document::setName(const std::string& id, const std::string& name, std::string& error) {
  Component* component = findMutable(id);
  if (component == nullptr) {
    error = "component.missing";
    return false;
  }
  if (!editable(*component, error)) return false;
  if (name.empty()) {
    error = "name.empty";
    return false;
  }
  component->name = name;
  pushHistory();
  return true;
}

bool Document::setProjectName(const std::string& name, std::string& error) {
  if (name.empty()) {
    error = "name.empty";
    return false;
  }
  name_ = name;
  pushHistory();
  return true;
}

bool Document::setMaterial(const std::string& id, const std::string& materialId, std::string& error) {
  Component* component = findMutable(id);
  if (component == nullptr) {
    error = "component.missing";
    return false;
  }
  if (!editable(*component, error)) return false;
  if (materialId.empty()) {
    error = "material.missing";
    return false;
  }
  component->materialId = materialId;
  pushHistory();
  return true;
}

bool Document::setNotes(const std::string& id, const std::string& notes, std::string& error) {
  Component* component = findMutable(id);
  if (component == nullptr) {
    error = "component.missing";
    return false;
  }
  if (!editable(*component, error)) return false;
  component->notes = notes;
  pushHistory();
  return true;
}

bool Document::setVisible(const std::string& id, bool visible, std::string& error) {
  Component* component = findMutable(id);
  if (component == nullptr) {
    error = "component.missing";
    return false;
  }
  component->visible = visible;
  pushHistory();
  return true;
}

bool Document::setLocked(const std::string& id, bool locked, std::string& error) {
  Component* component = findMutable(id);
  if (component == nullptr) {
    error = "component.missing";
    return false;
  }
  component->locked = locked;
  pushHistory();
  return true;
}

bool Document::setDimension(const std::string& id, std::string_view key, std::int64_t nm, std::string& error) {
  Component* component = findMutable(id);
  if (component == nullptr) {
    error = "component.missing";
    return false;
  }
  if (!editable(*component, error)) return false;
  if (!positiveDimension(nm, error)) return false;

  const bool cylinder = component->type == ComponentType::Cylinder;
  if (key == "width") {
    if (cylinder) {
      error = "dimension.not_applicable";
      return false;
    }
    component->widthNm = nm;
  } else if (key == "height") {
    component->heightNm = nm;
  } else if (key == "thickness") {
    if (cylinder) {
      error = "dimension.not_applicable";
      return false;
    }
    component->thicknessNm = nm;
  } else if (key == "radius") {
    if (!cylinder) {
      error = "dimension.not_applicable";
      return false;
    }
    component->radiusNm = nm;
  } else {
    error = "dimension.unknown";
    return false;
  }
  pushHistory();
  return true;
}

bool Document::setTranslation(const std::string& id, std::string_view axis, std::int64_t nm, std::string& error) {
  Component* component = findMutable(id);
  if (component == nullptr) {
    error = "component.missing";
    return false;
  }
  if (!editable(*component, error)) return false;
  if (nm > kMaxPlacementNm || nm < -kMaxPlacementNm) {
    error = "dimension.too_large";
    return false;
  }
  if (axis == "x") component->placement.xNm = nm;
  else if (axis == "y") component->placement.yNm = nm;
  else if (axis == "z") component->placement.zNm = nm;
  else {
    error = "dimension.unknown";
    return false;
  }
  pushHistory();
  return true;
}

bool Document::setRotation(const std::string& id, std::string_view axis, double degrees, std::string& error) {
  Component* component = findMutable(id);
  if (component == nullptr) {
    error = "component.missing";
    return false;
  }
  if (!editable(*component, error)) return false;
  if (!std::isfinite(degrees)) {
    error = "angle.invalid";
    return false;
  }
  if (axis == "x") component->placement.rxDeg = degrees;
  else if (axis == "y") component->placement.ryDeg = degrees;
  else if (axis == "z") component->placement.rzDeg = degrees;
  else {
    error = "dimension.unknown";
    return false;
  }
  pushHistory();
  return true;
}

bool Document::setDisplayUnit(const std::string& unitKey, std::string& error) {
  units::LengthUnit unit;
  if (!units::lengthUnitFromKey(unitKey, unit)) {
    error = "units.invalid";
    return false;
  }
  (void)unit;
  displayUnit_ = unitKey;
  pushHistory();
  return true;
}

bool Document::setFractionPrecision(int denom, std::string& error) {
  if (!units::validFractionDenom(denom)) {
    error = "units.invalid";
    return false;
  }
  fractionPrecision_ = denom;
  pushHistory();
  return true;
}

bool Document::undo(std::string& error) {
  if (!canUndo()) return false;
  --historyIndex_;
  return restoreCurrent(error);
}

bool Document::redo(std::string& error) {
  if (!canRedo()) return false;
  ++historyIndex_;
  return restoreCurrent(error);
}

std::string Document::toJson() const {
  nlohmann::json components = nlohmann::json::array();
  for (const Component& component : components_) {
    components.push_back({
        {"id", component.id},
        {"type", componentTypeKey(component.type)},
        {"name", component.name},
        {"materialId", component.materialId},
        {"visible", component.visible},
        {"locked", component.locked},
        {"parentId", component.parentId},
        {"notes", component.notes},
        {"placement",
         {{"x", component.placement.xNm},
          {"y", component.placement.yNm},
          {"z", component.placement.zNm},
          {"rx", component.placement.rxDeg},
          {"ry", component.placement.ryDeg},
          {"rz", component.placement.rzDeg}}},
        {"parameters",
         {{"width", component.widthNm},
          {"height", component.heightNm},
          {"thickness", component.thicknessNm},
          {"radius", component.radiusNm}}},
    });
  }
  nlohmann::json root = {{"schema", "timberlab.model"},
                         {"schemaVersion", kSchemaVersion},
                         {"lengthUnit", "nm"},
                         {"id", id_},
                         {"name", name_},
                         {"displayUnit", displayUnit_},
                         {"fractionPrecision", fractionPrecision_},
                         {"components", components}};
  return root.dump(2);
}

bool Document::parseInto(const std::string& json, Document& out, std::string& error) {
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(json);
  } catch (const nlohmann::json::exception&) {
    error = "storage.json";
    return false;
  }
  if (!root.is_object() || root.value("schema", "") != "timberlab.model") {
    error = "storage.format";
    return false;
  }
  if (!root.contains("schemaVersion") || !root["schemaVersion"].is_number_integer()) {
    error = "storage.format";
    return false;
  }
  const int version = root["schemaVersion"].get<int>();
  if (version != kSchemaVersion) {
    error = "storage.version";
    return false;
  }
  if (root.value("lengthUnit", "") != "nm") {
    error = "storage.format";
    return false;
  }
  out.id_ = root.value("id", "");
  out.name_ = root.value("name", "Untitled");
  if (out.id_.empty() || out.name_.empty()) {
    error = "storage.json";
    return false;
  }
  out.displayUnit_ = root.value("displayUnit", "mm");
  units::LengthUnit unit;
  if (!units::lengthUnitFromKey(out.displayUnit_, unit)) {
    error = "storage.json";
    return false;
  }
  out.fractionPrecision_ = root.value("fractionPrecision", 16);
  if (!units::validFractionDenom(out.fractionPrecision_)) {
    error = "storage.json";
    return false;
  }
  if (!root.contains("components") || !root["components"].is_array()) {
    error = "storage.json";
    return false;
  }

  std::vector<Component> components;
  for (const auto& item : root["components"]) {
    if (!item.is_object()) {
      error = "storage.json";
      return false;
    }
    Component component;
    component.id = item.value("id", "");
    component.name = item.value("name", "");
    component.materialId = item.value("materialId", "");
    if (component.id.empty() || component.name.empty() || component.materialId.empty()) {
      error = "storage.json";
      return false;
    }
    if (!componentTypeFromKey(item.value("type", ""), component.type)) {
      error = "storage.json";
      return false;
    }
    component.visible = item.value("visible", true);
    component.locked = item.value("locked", false);
    component.parentId = item.value("parentId", "");
    component.notes = item.value("notes", "");
    if (!item.contains("placement") || !item.contains("parameters")) {
      error = "storage.json";
      return false;
    }
    const auto& placement = item["placement"];
    const auto& parameters = item["parameters"];
    if (!requireInteger(placement, "x", component.placement.xNm, error) ||
        !requireInteger(placement, "y", component.placement.yNm, error) ||
        !requireInteger(placement, "z", component.placement.zNm, error) ||
        !requireInteger(parameters, "width", component.widthNm, error) ||
        !requireInteger(parameters, "height", component.heightNm, error) ||
        !requireInteger(parameters, "thickness", component.thicknessNm, error) ||
        !requireInteger(parameters, "radius", component.radiusNm, error)) {
      return false;
    }
    if (!placement.contains("rx") || !placement["rx"].is_number() || !placement.contains("ry") ||
        !placement["ry"].is_number() || !placement.contains("rz") || !placement["rz"].is_number()) {
      error = "storage.json";
      return false;
    }
    component.placement.rxDeg = placement["rx"].get<double>();
    component.placement.ryDeg = placement["ry"].get<double>();
    component.placement.rzDeg = placement["rz"].get<double>();
    if (component.type == ComponentType::Cylinder) {
      if (!positiveDimension(component.radiusNm, error) || !positiveDimension(component.heightNm, error)) return false;
    } else if (!positiveDimension(component.widthNm, error) || !positiveDimension(component.heightNm, error) ||
               !positiveDimension(component.thicknessNm, error)) {
      return false;
    }
    components.push_back(std::move(component));
  }
  out.components_ = std::move(components);
  return true;
}

bool Document::fromJson(const std::string& json, Document& out, std::string& error) {
  Document parsed;
  if (!parseInto(json, parsed, error)) return false;
  parsed.resetHistory();
  out = std::move(parsed);
  return true;
}

void Document::makeDemoTable() {
  newProject("mm", 16);
  name_ = "Demo table";
  components_.clear();

  const std::int64_t topW = 1200 * kMm;
  const std::int64_t topD = 700 * kMm;
  const std::int64_t topT = 20 * kMm;
  const std::int64_t leg = 60 * kMm;
  const std::int64_t inset = 40 * kMm;
  const std::int64_t legH = 720 * kMm;
  const std::int64_t apronH = 90 * kMm;
  const std::int64_t apronT = 20 * kMm;
  const std::int64_t apronZ = 560 * kMm;

  auto add = [this](ComponentType type, const std::string& name, const std::string& material, std::int64_t x,
                    std::int64_t y, std::int64_t z, std::int64_t width, std::int64_t thickness, std::int64_t height) {
    Component component;
    component.id = makeId();
    component.type = type;
    component.name = name;
    component.materialId = material;
    component.placement.xNm = x;
    component.placement.yNm = y;
    component.placement.zNm = z;
    component.widthNm = width;
    component.thicknessNm = thickness;
    component.heightNm = height;
    components_.push_back(std::move(component));
  };

  add(ComponentType::Board, "Leg 1", "black-walnut", inset, inset, 0, leg, leg, legH);
  add(ComponentType::Board, "Leg 2", "black-walnut", topW - inset - leg, inset, 0, leg, leg, legH);
  add(ComponentType::Board, "Leg 3", "black-walnut", inset, topD - inset - leg, 0, leg, leg, legH);
  add(ComponentType::Board, "Leg 4", "black-walnut", topW - inset - leg, topD - inset - leg, 0, leg, leg, legH);

  const std::int64_t apronSpanX = topW - 2 * inset - 2 * leg;
  const std::int64_t apronSpanY = topD - 2 * inset - 2 * leg;
  add(ComponentType::Board, "Front apron", "european-beech", inset + leg, inset, apronZ, apronSpanX, apronT, apronH);
  add(ComponentType::Board, "Back apron", "european-beech", inset + leg, topD - inset - apronT, apronZ, apronSpanX,
      apronT, apronH);
  add(ComponentType::Board, "Left apron", "european-beech", inset, inset + leg, apronZ, apronT, apronSpanY, apronH);
  add(ComponentType::Board, "Right apron", "european-beech", topW - inset - apronT, inset + leg, apronZ, apronT,
      apronSpanY, apronH);
  add(ComponentType::Board, "Top", "european-oak", 0, 0, legH, topW, topD, topT);
  pushHistory();
}

}  // namespace timberlab
