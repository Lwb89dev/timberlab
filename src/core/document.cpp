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

bool acceptableName(const std::string& name, std::string& error) {
  if (name.empty()) {
    error = "name.empty";
    return false;
  }
  if (name.size() > Document::kMaxNameBytes) {
    error = "name.too_long";
    return false;
  }
  return true;
}

bool acceptableAngle(double degrees, std::string& error) {
  if (!std::isfinite(degrees) || std::abs(degrees) > units::kMaxAbsDegrees) {
    error = "angle.invalid";
    return false;
  }
  return true;
}

bool acceptableOffset(std::int64_t nm, std::string& error) {
  if (nm > kMaxPlacementNm || nm < -kMaxPlacementNm) {
    error = "dimension.too_large";
    return false;
  }
  return true;
}

bool acceptableRole(const std::string& role) {
  if (role.empty()) return true;
  if (role.size() > 32) return false;
  for (unsigned char letter : role) {
    if (letter >= 'a' && letter <= 'z') continue;
    if (letter >= '0' && letter <= '9') continue;
    if (letter == '.' || letter == '_' || letter == '-') continue;
    return false;
  }
  return true;
}

bool duplicateId(const std::vector<Component>& components, const std::string& id) {
  for (const Component& existing : components) {
    if (existing.id == id) return true;
  }
  return false;
}

bool readComponent(const nlohmann::json& item, Component& component, std::string& error) {
  if (!item.is_object()) {
    error = "storage.json";
    return false;
  }
  component.id = item.value("id", "");
  component.name = item.value("name", "");
  component.materialId = item.value("materialId", "");
  if (component.id.empty() || component.id.size() > Document::kMaxIdBytes || component.materialId.empty() ||
      component.materialId.size() > Document::kMaxIdBytes) {
    error = "storage.json";
    return false;
  }
  if (component.name.empty()) {
    error = "storage.json";
    return false;
  }
  if (component.name.size() > Document::kMaxNameBytes) {
    error = "name.too_long";
    return false;
  }
  if (!componentTypeFromKey(item.value("type", ""), component.type)) {
    error = "storage.json";
    return false;
  }
  component.visible = item.value("visible", true);
  component.locked = item.value("locked", false);
  component.parentId = item.value("parentId", "");
  component.role = item.value("role", "");
  component.notes = item.value("notes", "");
  if (component.parentId.size() > Document::kMaxIdBytes || !acceptableRole(component.role)) {
    error = "storage.json";
    return false;
  }
  if (component.notes.size() > Document::kMaxNotesBytes) {
    error = "notes.too_long";
    return false;
  }
  if (!item.contains("placement") || !item.contains("parameters")) {
    error = "storage.json";
    return false;
  }
  const auto& placement = item["placement"];
  const auto& parameters = item["parameters"];
  if (!placement.is_object() || !parameters.is_object()) {
    error = "storage.json";
    return false;
  }
  if (!requireInteger(placement, "x", component.placement.xNm, error) ||
      !requireInteger(placement, "y", component.placement.yNm, error) ||
      !requireInteger(placement, "z", component.placement.zNm, error) ||
      !requireInteger(parameters, "width", component.widthNm, error) ||
      !requireInteger(parameters, "height", component.heightNm, error) ||
      !requireInteger(parameters, "thickness", component.thicknessNm, error) ||
      !requireInteger(parameters, "radius", component.radiusNm, error)) {
    return false;
  }
  if (!acceptableOffset(component.placement.xNm, error) || !acceptableOffset(component.placement.yNm, error) ||
      !acceptableOffset(component.placement.zNm, error)) {
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
  if (!acceptableAngle(component.placement.rxDeg, error) || !acceptableAngle(component.placement.ryDeg, error) ||
      !acceptableAngle(component.placement.rzDeg, error)) {
    return false;
  }
  if (component.type == ComponentType::Cylinder) {
    return positiveDimension(component.radiusNm, error) && positiveDimension(component.heightNm, error);
  }
  return positiveDimension(component.widthNm, error) && positiveDimension(component.heightNm, error) &&
         positiveDimension(component.thicknessNm, error);
}

bool jointNeeds(JointType type, const char* key) {
  const std::string_view name(key);
  switch (type) {
    case JointType::MortiseTenon:
      return true;
    case JointType::HalfLap:
      return name == "length";
    case JointType::Dado:
      return name == "width" || name == "depth" || name == "offset";
    case JointType::Groove:
      return name != "thickness";
    case JointType::Rabbet:
      return name == "width" || name == "depth";
    case JointType::Bridle:
      return name == "length" || name == "thickness";
    case JointType::Castle:
      return name == "length" || name == "thickness";
    case JointType::Miter:
      return name == "length";
    case JointType::Dovetail:
      return name == "length" || name == "width" || name == "thickness" || name == "depth" || name == "offset";
    case JointType::SlidingDovetail:
      return name == "width" || name == "depth" || name == "offset" || name == "offset2";
    case JointType::Finger:
      return name == "length";
    case JointType::HalfBlindDovetail:
      return name == "length" || name == "width" || name == "thickness" || name == "depth" || name == "offset";
    case JointType::TongueGroove:
      return name == "thickness" || name == "depth";
    case JointType::Dowel:
      return name == "thickness" || name == "depth";
  }
  return false;
}

bool takeNm(const nlohmann::json& item, const char* key, bool required, std::int64_t& dest, std::string& error) {
  if (!item.contains(key)) {
    if (required) {
      error = "storage.json";
      return false;
    }
    dest = 0;
    return true;
  }
  return requireInteger(item, key, dest, error);
}

bool readFlag(const nlohmann::json& item, const char* key, bool& dest, std::string& error) {
  dest = false;
  if (!item.contains(key)) return true;
  if (!item.at(key).is_boolean()) {
    error = "storage.json";
    return false;
  }
  dest = item.at(key).get<bool>();
  return true;
}

bool readFace(const nlohmann::json& item, const char* key, PartFace& dest, std::string& error) {
  if (!item.contains(key) || !item.at(key).is_string() || !faceFromKey(item.at(key).get<std::string>(), dest)) {
    error = "storage.json";
    return false;
  }
  return true;
}

bool readEdge(const nlohmann::json& item, JointType type, FaceEdge& dest, std::string& error) {
  dest = FaceEdge::U0;
  const bool required = type == JointType::Rabbet;
  if (!item.contains("edge")) {
    if (required) error = "storage.json";
    return !required;
  }
  if (!item.at("edge").is_string() || !edgeFromKey(item.at("edge").get<std::string>(), dest)) {
    error = "storage.json";
    return false;
  }
  return true;
}

bool readJoint(const nlohmann::json& item, Joint& joint, std::string& error) {
  if (!item.is_object()) {
    error = "storage.json";
    return false;
  }
  if (!item.contains("id") || !item.at("id").is_string() || !item.contains("type") || !item.at("type").is_string() ||
      !item.contains("partA") || !item.at("partA").is_string() || !item.contains("partB") ||
      !item.at("partB").is_string()) {
    error = "storage.json";
    return false;
  }
  joint.id = item.at("id").get<std::string>();
  joint.partA = item.at("partA").get<std::string>();
  joint.partB = item.at("partB").get<std::string>();
  if (joint.id.empty() || joint.id.size() > Document::kMaxIdBytes || joint.partA.empty() ||
      joint.partA.size() > Document::kMaxIdBytes || joint.partB.empty() || joint.partB.size() > Document::kMaxIdBytes) {
    error = "storage.json";
    return false;
  }
  if (!jointTypeFromKey(item.at("type").get<std::string>(), joint.type)) {
    error = "storage.json";
    return false;
  }
  if (!readFace(item, "faceA", joint.faceA, error) || !readFace(item, "faceB", joint.faceB, error) ||
      !readEdge(item, joint.type, joint.edge, error)) {
    return false;
  }
  if (!takeNm(item, "length", jointNeeds(joint.type, "length"), joint.lengthNm, error) ||
      !takeNm(item, "width", jointNeeds(joint.type, "width"), joint.widthNm, error) ||
      !takeNm(item, "thickness", jointNeeds(joint.type, "thickness"), joint.thicknessNm, error) ||
      !takeNm(item, "depth", jointNeeds(joint.type, "depth"), joint.depthNm, error) ||
      !takeNm(item, "offset", jointNeeds(joint.type, "offset"), joint.offsetNm, error) ||
      !takeNm(item, "offset2", jointNeeds(joint.type, "offset2"), joint.offset2Nm, error)) {
    return false;
  }
  const bool countRequired = joint.type == JointType::Castle || joint.type == JointType::Dovetail ||
                             joint.type == JointType::Finger || joint.type == JointType::HalfBlindDovetail;
  if (!item.contains("count")) {
    joint.count = 0;
    if (countRequired) {
      error = "storage.json";
      return false;
    }
  } else if (!item.at("count").is_number_integer()) {
    error = "storage.json";
    return false;
  } else {
    joint.count = item.at("count").get<int>();
  }
  return readFlag(item, "removeMinA", joint.removeMinA, error) && readFlag(item, "removeMinB", joint.removeMinB, error);
}

bool duplicateJoint(const std::vector<Joint>& joints, const std::string& id) {
  for (const Joint& joint : joints) {
    if (joint.id == id) return true;
  }
  return false;
}

Component* findLoaded(std::vector<Component>& components, const std::string& id) {
  for (Component& component : components) {
    if (component.id == id) return &component;
  }
  return nullptr;
}

const Component* findLoaded(const std::vector<Component>& components, const std::string& id) {
  for (const Component& component : components) {
    if (component.id == id) return &component;
  }
  return nullptr;
}

bool parentsOk(const std::vector<Component>& components, std::string& error) {
  for (const Component& component : components) {
    std::string cursor = component.parentId;
    for (std::size_t step = 0; step < components.size() && !cursor.empty(); ++step) {
      if (cursor == component.id) {
        error = "component.cycle";
        return false;
      }
      const Component* next = findLoaded(components, cursor);
      if (next == nullptr) {
        error = "component.missing";
        return false;
      }
      cursor = next->parentId;
    }
    if (!cursor.empty()) {
      error = "component.cycle";
      return false;
    }
  }
  return true;
}

bool jointsFit(const std::vector<Component>& components, const std::vector<Joint>& joints, std::string& error) {
  for (const Joint& joint : joints) {
    const Component* partA = findLoaded(components, joint.partA);
    const Component* partB = findLoaded(components, joint.partB);
    if (partA == nullptr || partB == nullptr) {
      error = "component.missing";
      return false;
    }
    if (!jointFits(joint, *partA, *partB, error)) return false;
  }
  return true;
}

bool bindingUsesPart(const constraints::Binding& binding, const std::string& id) {
  if (binding.partId == id) return true;
  for (const constraints::Term& term : binding.terms) {
    if (term.partId == id) return true;
  }
  return false;
}

bool writeDimension(Component& component, std::string_view key, std::int64_t nm, std::string& error) {
  const bool cylinder = component.type == ComponentType::Cylinder;
  if (key == "height") {
    component.heightNm = nm;
    return true;
  }
  if (key == "radius") {
    if (!cylinder) {
      error = "dimension.not_applicable";
      return false;
    }
    component.radiusNm = nm;
    return true;
  }
  if (key == "width" || key == "thickness") {
    if (cylinder) {
      error = "dimension.not_applicable";
      return false;
    }
    if (key == "width") component.widthNm = nm;
    else component.thicknessNm = nm;
    return true;
  }
  error = "dimension.unknown";
  return false;
}

bool writeTranslation(Component& component, std::string_view axis, std::int64_t nm, std::string& error) {
  if (axis == "x") component.placement.xNm = nm;
  else if (axis == "y") component.placement.yNm = nm;
  else if (axis == "z") component.placement.zNm = nm;
  else {
    error = "dimension.unknown";
    return false;
  }
  return true;
}

bool duplicateParameter(const std::vector<constraints::Parameter>& parameters, const std::string& id) {
  for (const constraints::Parameter& parameter : parameters) {
    if (parameter.id == id) return true;
  }
  return false;
}

bool duplicateBinding(const std::vector<constraints::Binding>& bindings, const std::string& partId,
                      const std::string& field) {
  for (const constraints::Binding& binding : bindings) {
    if (binding.partId == partId && binding.field == field) return true;
  }
  return false;
}

bool readParameter(const nlohmann::json& item, constraints::Parameter& parameter, std::string& error) {
  if (!item.is_object()) {
    error = "storage.json";
    return false;
  }
  parameter.id = item.value("id", "");
  if (parameter.id.empty() || parameter.id.size() > Document::kMaxIdBytes) {
    error = "storage.json";
    return false;
  }
  parameter.name = item.value("name", parameter.id);
  if (parameter.name.empty()) parameter.name = parameter.id;
  if (parameter.name.size() > Document::kMaxNameBytes) {
    error = "name.too_long";
    return false;
  }
  if (!requireInteger(item, "value", parameter.valueNm, error)) return false;
  if (parameter.valueNm < 0 || parameter.valueNm > units::kMaxPartNm) {
    error = "parameter.range";
    return false;
  }
  return true;
}

bool readTerm(const nlohmann::json& item, constraints::Term& term, std::string& error) {
  if (!item.is_object() || !item.contains("coefficient") || !item.at("coefficient").is_number_integer()) {
    error = "storage.json";
    return false;
  }
  term.coefficient = item.at("coefficient").get<std::int64_t>();
  if (term.coefficient == 0) {
    error = "storage.json";
    return false;
  }
  const bool fromParameter = item.contains("parameter");
  const bool fromPart = item.contains("part") || item.contains("field");
  if (fromParameter == fromPart) {
    error = "storage.json";
    return false;
  }
  if (fromParameter) {
    if (!item.at("parameter").is_string()) {
      error = "storage.json";
      return false;
    }
    term.parameterId = item.at("parameter").get<std::string>();
    if (term.parameterId.empty() || term.parameterId.size() > Document::kMaxIdBytes) {
      error = "storage.json";
      return false;
    }
    return true;
  }
  if (!item.contains("part") || !item.contains("field") || !item.at("part").is_string() ||
      !item.at("field").is_string()) {
    error = "storage.json";
    return false;
  }
  term.partId = item.at("part").get<std::string>();
  term.field = item.at("field").get<std::string>();
  if (term.partId.empty() || term.partId.size() > Document::kMaxIdBytes) {
    error = "storage.json";
    return false;
  }
  if (!constraints::isBindingField(term.field)) {
    error = "parameter.unknown";
    return false;
  }
  return true;
}

bool readBinding(const nlohmann::json& item, constraints::Binding& binding, std::string& error) {
  if (!item.is_object()) {
    error = "storage.json";
    return false;
  }
  binding.partId = item.value("part", "");
  binding.field = item.value("field", "");
  if (binding.partId.empty() || binding.partId.size() > Document::kMaxIdBytes) {
    error = "storage.json";
    return false;
  }
  if (!constraints::isBindingField(binding.field)) {
    error = "parameter.unknown";
    return false;
  }
  binding.constantNm = 0;
  if (item.contains("constant") && !requireInteger(item, "constant", binding.constantNm, error)) return false;
  if (!item.contains("terms") || !item.at("terms").is_array()) {
    error = "storage.json";
    return false;
  }
  if (item.at("terms").size() > Document::kMaxComponents) {
    error = "storage.too_many";
    return false;
  }
  for (const auto& termItem : item.at("terms")) {
    constraints::Term term;
    if (!readTerm(termItem, term, error)) return false;
    binding.terms.push_back(std::move(term));
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
  joints_.clear();
  parameters_.clear();
  bindings_.clear();
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
  joints_ = std::move(parsed.joints_);
  parameters_ = std::move(parsed.parameters_);
  bindings_ = std::move(parsed.bindings_);
  return true;
}

Component* Document::findMutable(const std::string& id) {
  for (Component& component : components_) {
    if (component.id == id) return &component;
  }
  return nullptr;
}

Joint* Document::findJointMutable(const std::string& id) {
  for (Joint& joint : joints_) {
    if (joint.id == id) return &joint;
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
  if (components_.size() >= kMaxComponents) return {};
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
  if (components_.size() >= kMaxComponents) {
    error = "storage.too_many";
    return false;
  }
  Component copy = *source;
  copy.locked = false;
  copy.name += " copy";
  if (copy.name.size() > kMaxNameBytes) {
    error = "name.too_long";
    return false;
  }
  copy.parentId.clear();
  const LocalBox box = localBox(*source);
  copy.placement.xNm =
      source->placement.xNm + static_cast<std::int64_t>(std::llround(box.sizeXmm * static_cast<double>(kMm))) + 20 * kMm;
  newId = addComponent(std::move(copy), false);
  if (newId.empty()) {
    error = "storage.too_many";
    return false;
  }
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
    if (item.id == id) continue;
    Component keptPart = item;
    if (keptPart.parentId == id) keptPart.parentId.clear();
    kept.push_back(std::move(keptPart));
  }
  components_ = std::move(kept);
  std::vector<Joint> keptJoints;
  keptJoints.reserve(joints_.size());
  for (const Joint& joint : joints_) {
    if (joint.partA != id && joint.partB != id) keptJoints.push_back(joint);
  }
  joints_ = std::move(keptJoints);
  std::vector<constraints::Binding> keptBindings;
  keptBindings.reserve(bindings_.size());
  for (const constraints::Binding& binding : bindings_) {
    if (!bindingUsesPart(binding, id)) keptBindings.push_back(binding);
  }
  bindings_ = std::move(keptBindings);
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
  if (!acceptableName(name, error)) return false;
  component->name = name;
  pushHistory();
  return true;
}

bool Document::setProjectName(const std::string& name, std::string& error) {
  if (!acceptableName(name, error)) return false;
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
  if (materialId.size() > kMaxIdBytes) {
    error = "text.too_long";
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
  if (notes.size() > kMaxNotesBytes) {
    error = "notes.too_long";
    return false;
  }
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

bool Document::setParent(const std::string& id, const std::string& parentId, std::string& error) {
  Component* component = findMutable(id);
  if (component == nullptr) {
    error = "component.missing";
    return false;
  }
  if (!editable(*component, error)) return false;
  if (parentId == component->parentId) return true;
  if (!parentId.empty()) {
    if (find(parentId) == nullptr) {
      error = "component.missing";
      return false;
    }
    std::string cursor = parentId;
    for (std::size_t step = 0; step < components_.size() && !cursor.empty(); ++step) {
      if (cursor == id) {
        error = "component.cycle";
        return false;
      }
      const Component* next = find(cursor);
      if (next == nullptr) {
        error = "component.missing";
        return false;
      }
      cursor = next->parentId;
    }
    if (!cursor.empty()) {
      error = "component.cycle";
      return false;
    }
  }
  component->parentId = parentId;
  pushHistory();
  return true;
}

bool Document::setRole(const std::string& id, const std::string& role, std::string& error) {
  Component* component = findMutable(id);
  if (component == nullptr) {
    error = "component.missing";
    return false;
  }
  if (!editable(*component, error)) return false;
  if (!acceptableRole(role)) {
    error = "component.role";
    return false;
  }
  if (component->role == role) return true;
  component->role = role;
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
  if (findBinding(id, key) != nullptr) {
    error = "parameter.driven";
    return false;
  }
  if (!positiveDimension(nm, error)) return false;

  std::vector<Component> next = components_;
  Component* target = findLoaded(next, id);
  if (target == nullptr) {
    error = "component.missing";
    return false;
  }
  if (!writeDimension(*target, key, nm, error)) return false;
  return commitComponents(std::move(next), error);
}

bool Document::setTranslation(const std::string& id, std::string_view axis, std::int64_t nm, std::string& error) {
  Component* component = findMutable(id);
  if (component == nullptr) {
    error = "component.missing";
    return false;
  }
  if (!editable(*component, error)) return false;
  if (findBinding(id, axis) != nullptr) {
    error = "parameter.driven";
    return false;
  }
  if (!acceptableOffset(nm, error)) return false;

  std::vector<Component> next = components_;
  Component* target = findLoaded(next, id);
  if (target == nullptr) {
    error = "component.missing";
    return false;
  }
  if (!writeTranslation(*target, axis, nm, error)) return false;
  return commitComponents(std::move(next), error);
}

bool Document::setRotation(const std::string& id, std::string_view axis, double degrees, std::string& error) {
  Component* component = findMutable(id);
  if (component == nullptr) {
    error = "component.missing";
    return false;
  }
  if (!editable(*component, error)) return false;
  if (!acceptableAngle(degrees, error)) return false;
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

const constraints::Binding* Document::findBinding(const std::string& partId, std::string_view field) const {
  for (const constraints::Binding& binding : bindings_) {
    if (binding.partId == partId && binding.field == field) return &binding;
  }
  return nullptr;
}

bool Document::commitComponents(std::vector<Component> next, std::string& error) {
  if (!constraints::propagateBindings(parameters_, bindings_, next, error)) return false;
  if (!jointsFit(next, joints_, error)) return false;
  components_ = std::move(next);
  pushHistory();
  return true;
}

bool Document::addParameter(const std::string& id, const std::string& name, std::int64_t valueNm, std::string& error) {
  if (id.empty() || id.size() > kMaxIdBytes) {
    error = "storage.json";
    return false;
  }
  if (duplicateParameter(parameters_, id)) {
    error = "storage.duplicate";
    return false;
  }
  if (parameters_.size() >= kMaxComponents) {
    error = "storage.too_many";
    return false;
  }
  if (valueNm < 0 || valueNm > units::kMaxPartNm) {
    error = "parameter.range";
    return false;
  }
  constraints::Parameter parameter;
  parameter.id = id;
  parameter.name = name.empty() ? id : name;
  if (parameter.name.size() > kMaxNameBytes) {
    error = "name.too_long";
    return false;
  }
  parameter.valueNm = valueNm;
  parameters_.push_back(std::move(parameter));
  pushHistory();
  return true;
}

bool Document::setParameter(const std::string& id, std::int64_t valueNm, std::string& error) {
  const constraints::Parameter* current = nullptr;
  for (const constraints::Parameter& parameter : parameters_) {
    if (parameter.id == id) current = &parameter;
  }
  if (current == nullptr) {
    error = "parameter.missing";
    return false;
  }
  if (valueNm < 0 || valueNm > units::kMaxPartNm) {
    error = "parameter.range";
    return false;
  }
  std::vector<constraints::Parameter> parameters = parameters_;
  for (constraints::Parameter& parameter : parameters) {
    if (parameter.id == id) parameter.valueNm = valueNm;
  }
  std::vector<Component> parts = components_;
  if (!constraints::propagateBindings(parameters, bindings_, parts, error)) return false;
  if (!jointsFit(parts, joints_, error)) return false;
  parameters_ = std::move(parameters);
  components_ = std::move(parts);
  pushHistory();
  return true;
}

bool Document::setBinding(const constraints::Binding& binding, std::string& error) {
  const Component* part = find(binding.partId);
  if (part == nullptr) {
    error = "component.missing";
    return false;
  }
  if (!editable(*part, error)) return false;
  std::vector<constraints::Binding> next = bindings_;
  bool replaced = false;
  for (constraints::Binding& existing : next) {
    if (existing.partId == binding.partId && existing.field == binding.field) {
      existing = binding;
      replaced = true;
      break;
    }
  }
  if (!replaced) {
    if (next.size() >= kMaxComponents) {
      error = "storage.too_many";
      return false;
    }
    next.push_back(binding);
  }
  std::vector<Component> parts = components_;
  if (!constraints::propagateBindings(parameters_, next, parts, error)) return false;
  if (!jointsFit(parts, joints_, error)) return false;
  bindings_ = std::move(next);
  components_ = std::move(parts);
  pushHistory();
  return true;
}

bool Document::bindField(const std::string& partId, const std::string& field, const std::string& parameterId,
                         std::string& error) {
  if (!duplicateParameter(parameters_, parameterId)) {
    error = "parameter.missing";
    return false;
  }
  constraints::Binding binding;
  binding.partId = partId;
  binding.field = field;
  constraints::Term term;
  term.parameterId = parameterId;
  term.coefficient = 1;
  binding.terms.push_back(std::move(term));
  return setBinding(binding, error);
}

bool Document::unbindField(const std::string& partId, const std::string& field, std::string& error) {
  const Component* part = find(partId);
  if (part == nullptr) {
    error = "component.missing";
    return false;
  }
  if (!editable(*part, error)) return false;
  std::vector<constraints::Binding> next;
  next.reserve(bindings_.size());
  bool found = false;
  for (const constraints::Binding& binding : bindings_) {
    if (binding.partId == partId && binding.field == field) {
      found = true;
      continue;
    }
    next.push_back(binding);
  }
  if (!found) {
    error = "parameter.unknown";
    return false;
  }
  std::vector<Component> parts = components_;
  if (!constraints::propagateBindings(parameters_, next, parts, error)) return false;
  bindings_ = std::move(next);
  components_ = std::move(parts);
  pushHistory();
  return true;
}

std::string Document::addJoint(std::string_view typeKey, const std::string& selectedId, const std::string& mateId,
                                  std::string& error) {
  if (selectedId == mateId) {
    error = "joint.same_part";
    return {};
  }
  const Component* selected = find(selectedId);
  const Component* mate = find(mateId);
  if (selected == nullptr || mate == nullptr) {
    error = "component.missing";
    return {};
  }
  if (!editable(*selected, error) || !editable(*mate, error)) return {};
  if (joints_.size() >= kMaxComponents) {
    error = "storage.too_many";
    return {};
  }
  JointType type;
  if (!jointTypeFromKey(typeKey, type)) {
    error = "joint.parameter";
    return {};
  }
  Joint joint;
  if (!defaultJoint(type, *selected, *mate, joint, error)) return {};
  joint.id = makeId();
  const std::string id = joint.id;
  joints_.push_back(std::move(joint));
  pushHistory();
  return id;
}

bool Document::removeJoint(const std::string& id, std::string& error) {
  const Joint* joint = nullptr;
  for (const Joint& item : joints_) {
    if (item.id == id) joint = &item;
  }
  if (joint == nullptr) {
    error = "joint.missing";
    return false;
  }
  const Component* partA = find(joint->partA);
  const Component* partB = find(joint->partB);
  if (partA != nullptr && !editable(*partA, error)) return false;
  if (partB != nullptr && !editable(*partB, error)) return false;
  std::vector<Joint> kept;
  kept.reserve(joints_.size());
  for (const Joint& item : joints_) {
    if (item.id != id) kept.push_back(item);
  }
  joints_ = std::move(kept);
  pushHistory();
  return true;
}

bool Document::setJointParameter(const std::string& id, std::string_view key, std::int64_t nm, std::string& error) {
  Joint* joint = findJointMutable(id);
  if (joint == nullptr) {
    error = "joint.missing";
    return false;
  }
  const Component* partA = find(joint->partA);
  const Component* partB = find(joint->partB);
  if (partA == nullptr || partB == nullptr) {
    error = "component.missing";
    return false;
  }
  if (!editable(*partA, error) || !editable(*partB, error)) return false;
  Joint next = *joint;
  if (key == "length") next.lengthNm = nm;
  else if (key == "width") next.widthNm = nm;
  else if (key == "thickness") next.thicknessNm = nm;
  else if (key == "depth") next.depthNm = nm;
  else if (key == "offset") next.offsetNm = nm;
  else if (key == "offset2") next.offset2Nm = nm;
  else if (key == "count") {
    if (nm < 1 || nm > 8) {
      error = "joint.parameter";
      return false;
    }
    next.count = static_cast<int>(nm);
  } else {
    error = "joint.parameter";
    return false;
  }
  if (!jointFits(next, *partA, *partB, error)) return false;
  *joint = std::move(next);
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
    nlohmann::json item = {
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
    };
    if (!component.role.empty()) item["role"] = component.role;
    components.push_back(std::move(item));
  }
  nlohmann::json root = {{"schema", "timberlab.model"},
                         {"schemaVersion", kSchemaVersion},
                         {"lengthUnit", "nm"},
                         {"id", id_},
                         {"name", name_},
                         {"displayUnit", displayUnit_},
                         {"fractionPrecision", fractionPrecision_},
                         {"components", components}};
  if (!joints_.empty()) {
    nlohmann::json joints = nlohmann::json::array();
    for (const Joint& joint : joints_) {
      nlohmann::json item = {{"id", joint.id},
                             {"type", jointTypeKey(joint.type)},
                             {"partA", joint.partA},
                             {"partB", joint.partB},
                             {"faceA", faceKey(joint.faceA)},
                             {"faceB", faceKey(joint.faceB)},
                             {"edge", edgeKey(joint.edge)},
                             {"length", joint.lengthNm},
                             {"width", joint.widthNm},
                             {"thickness", joint.thicknessNm},
                             {"depth", joint.depthNm},
                             {"offset", joint.offsetNm},
                             {"offset2", joint.offset2Nm},
                             {"removeMinA", joint.removeMinA},
                             {"removeMinB", joint.removeMinB}};
      if (joint.count != 0) item["count"] = joint.count;
      joints.push_back(std::move(item));
    }
    root["joints"] = std::move(joints);
  }
  if (!parameters_.empty()) {
    nlohmann::json parameters = nlohmann::json::array();
    for (const constraints::Parameter& parameter : parameters_) {
      parameters.push_back({{"id", parameter.id}, {"name", parameter.name}, {"value", parameter.valueNm}});
    }
    root["parameters"] = std::move(parameters);
  }
  if (!bindings_.empty()) {
    nlohmann::json bindings = nlohmann::json::array();
    for (const constraints::Binding& binding : bindings_) {
      nlohmann::json terms = nlohmann::json::array();
      for (const constraints::Term& term : binding.terms) {
        nlohmann::json item = {{"coefficient", term.coefficient}};
        if (!term.parameterId.empty()) item["parameter"] = term.parameterId;
        else {
          item["part"] = term.partId;
          item["field"] = term.field;
        }
        terms.push_back(std::move(item));
      }
      nlohmann::json item = {{"part", binding.partId}, {"field", binding.field}, {"terms", std::move(terms)}};
      if (binding.constantNm != 0) item["constant"] = binding.constantNm;
      bindings.push_back(std::move(item));
    }
    root["bindings"] = std::move(bindings);
  }
  return root.dump(2);
}

bool Document::parseInto(const std::string& json, Document& out, std::string& error) {
  try {
    return readModel(json, out, error);
  } catch (const nlohmann::json::exception&) {
    error = "storage.json";
    return false;
  }
}

bool Document::readModel(const std::string& json, Document& out, std::string& error) {
  const nlohmann::json root = nlohmann::json::parse(json);
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
  if (out.id_.empty() || out.id_.size() > kMaxIdBytes || out.name_.empty()) {
    error = "storage.json";
    return false;
  }
  if (out.name_.size() > kMaxNameBytes) {
    error = "name.too_long";
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
  if (root["components"].size() > kMaxComponents) {
    error = "storage.too_many";
    return false;
  }

  std::vector<Component> components;
  components.reserve(root["components"].size());
  for (const auto& item : root["components"]) {
    Component component;
    if (!readComponent(item, component, error)) return false;
    if (duplicateId(components, component.id)) {
      error = "storage.duplicate";
      return false;
    }
    components.push_back(std::move(component));
  }
  if (!parentsOk(components, error)) return false;

  std::vector<constraints::Parameter> parameters;
  if (root.contains("parameters")) {
    if (!root["parameters"].is_array() || root["parameters"].size() > kMaxComponents) {
      error = root["parameters"].is_array() ? "storage.too_many" : "storage.json";
      return false;
    }
    parameters.reserve(root["parameters"].size());
    for (const auto& item : root["parameters"]) {
      constraints::Parameter parameter;
      if (!readParameter(item, parameter, error)) return false;
      if (duplicateParameter(parameters, parameter.id)) {
        error = "storage.duplicate";
        return false;
      }
      parameters.push_back(std::move(parameter));
    }
  }

  std::vector<constraints::Binding> bindings;
  if (root.contains("bindings")) {
    if (!root["bindings"].is_array() || root["bindings"].size() > kMaxComponents) {
      error = root["bindings"].is_array() ? "storage.too_many" : "storage.json";
      return false;
    }
    bindings.reserve(root["bindings"].size());
    for (const auto& item : root["bindings"]) {
      constraints::Binding binding;
      if (!readBinding(item, binding, error)) return false;
      if (duplicateBinding(bindings, binding.partId, binding.field)) {
        error = "storage.duplicate";
        return false;
      }
      bindings.push_back(std::move(binding));
    }
  }
  if (!constraints::propagateBindings(parameters, bindings, components, error)) return false;

  std::vector<Joint> joints;
  if (root.contains("joints")) {
    if (!root["joints"].is_array()) {
      error = "storage.json";
      return false;
    }
    if (root["joints"].size() > kMaxComponents) {
      error = "storage.too_many";
      return false;
    }
    joints.reserve(root["joints"].size());
    for (const auto& item : root["joints"]) {
      Joint joint;
      if (!readJoint(item, joint, error)) return false;
      if (duplicateJoint(joints, joint.id)) {
        error = "storage.duplicate";
        return false;
      }
      const Component* partA = findLoaded(components, joint.partA);
      const Component* partB = findLoaded(components, joint.partB);
      if (partA == nullptr || partB == nullptr) {
        error = "component.missing";
        return false;
      }
      if (!jointFits(joint, *partA, *partB, error)) return false;
      joints.push_back(std::move(joint));
    }
  }
  out.components_ = std::move(components);
  out.joints_ = std::move(joints);
  out.parameters_ = std::move(parameters);
  out.bindings_ = std::move(bindings);
  return true;
}

bool Document::fromJson(const std::string& json, Document& out, std::string& error) {
  Document parsed;
  if (!parseInto(json, parsed, error)) return false;
  parsed.resetHistory();
  out = std::move(parsed);
  return true;
}

}  // namespace timberlab
