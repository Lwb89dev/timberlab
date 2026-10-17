#include "timberlab/constraints/binding.hpp"

#include "timberlab/constraints/dependency_graph.hpp"
#include "timberlab/units/units.hpp"

#include <limits>
#include <string>
#include <unordered_map>
#include <utility>

namespace timberlab::constraints {
namespace {

constexpr std::int64_t kMaxPlacementNm = units::kMaxPartNm * 10;

std::string fieldNode(const std::string& partId, const std::string& field) {
  return "part:" + partId + ":" + field;
}

std::string parameterNode(const std::string& id) { return "param:" + id; }

const Component* findPart(const std::vector<Component>& components, const std::string& id) {
  for (const Component& component : components) {
    if (component.id == id) return &component;
  }
  return nullptr;
}

Component* findPart(std::vector<Component>& components, const std::string& id) {
  for (Component& component : components) {
    if (component.id == id) return &component;
  }
  return nullptr;
}

bool termShape(const Term& term) {
  const bool fromParameter = !term.parameterId.empty();
  const bool fromPart = !term.partId.empty() || !term.field.empty();
  if (fromParameter == fromPart || term.coefficient == 0) return false;
  if (fromParameter) return true;
  return !term.partId.empty() && isBindingField(term.field);
}

bool multiply(std::int64_t coefficient, std::int64_t value, std::int64_t& out) {
  if (coefficient == 0 || value == 0) {
    out = 0;
    return true;
  }
  const std::int64_t max = std::numeric_limits<std::int64_t>::max();
  const std::int64_t min = std::numeric_limits<std::int64_t>::min();
  if (coefficient < 0 && value < 0) {
    if (coefficient == min || value == min) return false;
    const std::int64_t left = -coefficient;
    const std::int64_t right = -value;
    if (left > max / right) return false;
    out = left * right;
    return true;
  }
  if ((coefficient < 0) == (value < 0)) {
    if (coefficient > max / value) return false;
    out = coefficient * value;
    return true;
  }

  const std::int64_t positive = coefficient > 0 ? coefficient : value;
  const std::int64_t negative = coefficient < 0 ? coefficient : value;
  if (negative == min) {
    if (positive != 1) return false;
    out = min;
    return true;
  }
  const std::int64_t magnitude = -negative;
  if (positive > max / magnitude) return false;
  out = -(positive * magnitude);
  return true;
}

bool add(std::int64_t left, std::int64_t right, std::int64_t& out) {
  const std::int64_t max = std::numeric_limits<std::int64_t>::max();
  const std::int64_t min = std::numeric_limits<std::int64_t>::min();
  if (right > 0 && left > max - right) return false;
  if (right < 0 && left < min - right) return false;
  out = left + right;
  return true;
}

bool readField(const Component& component, const std::string& field, std::int64_t& out, std::string& error) {
  const bool cylinder = component.type == ComponentType::Cylinder;
  if (field == "width" || field == "thickness") {
    if (cylinder) {
      error = "dimension.not_applicable";
      return false;
    }
    out = field == "width" ? component.widthNm : component.thicknessNm;
    return true;
  }
  if (field == "height") {
    out = component.heightNm;
    return true;
  }
  if (field == "radius") {
    if (!cylinder) {
      error = "dimension.not_applicable";
      return false;
    }
    out = component.radiusNm;
    return true;
  }
  if (field == "x") out = component.placement.xNm;
  else if (field == "y") out = component.placement.yNm;
  else if (field == "z") out = component.placement.zNm;
  else {
    error = "parameter.unknown";
    return false;
  }
  return true;
}

bool positive(std::int64_t nm) { return nm > 0 && nm <= units::kMaxPartNm; }

bool writeField(Component& component, const std::string& field, std::int64_t nm, std::string& error) {
  const bool cylinder = component.type == ComponentType::Cylinder;
  if (field == "width" || field == "thickness") {
    if (cylinder) {
      error = "dimension.not_applicable";
      return false;
    }
    if (!positive(nm)) {
      error = "parameter.range";
      return false;
    }
    if (field == "width") component.widthNm = nm;
    else component.thicknessNm = nm;
    return true;
  }
  if (field == "height") {
    if (!positive(nm)) {
      error = "parameter.range";
      return false;
    }
    component.heightNm = nm;
    return true;
  }
  if (field == "radius") {
    if (!cylinder) {
      error = "dimension.not_applicable";
      return false;
    }
    if (!positive(nm)) {
      error = "parameter.range";
      return false;
    }
    component.radiusNm = nm;
    return true;
  }
  if (nm > kMaxPlacementNm || nm < -kMaxPlacementNm) {
    error = "parameter.range";
    return false;
  }
  if (field == "x") component.placement.xNm = nm;
  else if (field == "y") component.placement.yNm = nm;
  else if (field == "z") component.placement.zNm = nm;
  else {
    error = "parameter.unknown";
    return false;
  }
  return true;
}

bool validate(const std::vector<Parameter>& parameters, const std::vector<Binding>& bindings,
              const std::vector<Component>& components, std::string& error) {
  std::unordered_map<std::string, std::int64_t> drivers;
  drivers.reserve(parameters.size());
  for (const Parameter& parameter : parameters) {
    if (!drivers.emplace(parameter.id, parameter.valueNm).second) {
      error = "storage.duplicate";
      return false;
    }
  }

  std::unordered_map<std::string, int> targets;
  for (const Binding& binding : bindings) {
    if (!isBindingField(binding.field)) {
      error = "parameter.unknown";
      return false;
    }
    if (findPart(components, binding.partId) == nullptr) {
      error = "component.missing";
      return false;
    }
    const std::string target = binding.partId + "\n" + binding.field;
    if (!targets.emplace(target, 0).second) {
      error = "storage.duplicate";
      return false;
    }
    for (const Term& term : binding.terms) {
      if (!termShape(term)) {
        error = "storage.json";
        return false;
      }
      if (!term.parameterId.empty()) {
        if (drivers.find(term.parameterId) == drivers.end()) {
          error = "parameter.missing";
          return false;
        }
        continue;
      }
      if (findPart(components, term.partId) == nullptr) {
        error = "component.missing";
        return false;
      }
    }
  }
  return true;
}

bool sourceValue(const Term& term, const std::unordered_map<std::string, std::int64_t>& drivers,
                 const std::vector<Component>& components, std::int64_t& out, std::string& error) {
  if (!term.parameterId.empty()) {
    const auto found = drivers.find(term.parameterId);
    if (found == drivers.end()) {
      error = "parameter.missing";
      return false;
    }
    out = found->second;
    return true;
  }
  const Component* part = findPart(components, term.partId);
  if (part == nullptr) {
    error = "component.missing";
    return false;
  }
  return readField(*part, term.field, out, error);
}

bool evaluateBinding(const Binding& binding, const std::unordered_map<std::string, std::int64_t>& drivers,
                     std::vector<Component>& components, std::string& error) {
  Component* part = findPart(components, binding.partId);
  if (part == nullptr) {
    error = "component.missing";
    return false;
  }
  std::int64_t value = binding.constantNm;
  for (const Term& term : binding.terms) {
    std::int64_t source = 0;
    if (!sourceValue(term, drivers, components, source, error)) return false;
    std::int64_t product = 0;
    if (!multiply(term.coefficient, source, product) || !add(value, product, value)) {
      error = "parameter.range";
      return false;
    }
  }
  return writeField(*part, binding.field, value, error);
}

}  // namespace

bool isBindingField(std::string_view field) {
  return field == "width" || field == "height" || field == "thickness" || field == "radius" || field == "x" ||
         field == "y" || field == "z";
}

bool propagateBindings(const std::vector<Parameter>& parameters, const std::vector<Binding>& bindings,
                       std::vector<Component>& components, std::string& error) {
  if (bindings.empty()) return true;
  if (!validate(parameters, bindings, components, error)) return false;

  DependencyGraph graph;
  std::unordered_map<std::string, std::size_t> byNode;
  for (std::size_t index = 0; index < bindings.size(); ++index) {
    const Binding& binding = bindings[index];
    const std::string target = fieldNode(binding.partId, binding.field);
    graph.addNode(target);
    byNode.emplace(target, index);
    for (const Term& term : binding.terms) {
      if (!term.parameterId.empty()) graph.addEdge(parameterNode(term.parameterId), target);
      else graph.addEdge(fieldNode(term.partId, term.field), target);
    }
  }
  if (graph.hasCycle() || graph.topologicalOrder().empty()) {
    error = "parameter.cycle";
    return false;
  }

  std::unordered_map<std::string, std::int64_t> drivers;
  drivers.reserve(parameters.size());
  for (const Parameter& parameter : parameters) drivers.emplace(parameter.id, parameter.valueNm);

  std::vector<Component> updated = components;
  for (const std::string& node : graph.topologicalOrder()) {
    const auto found = byNode.find(node);
    if (found == byNode.end()) continue;
    if (!evaluateBinding(bindings[found->second], drivers, updated, error)) return false;
  }
  components = std::move(updated);
  return true;
}

}  // namespace timberlab::constraints
