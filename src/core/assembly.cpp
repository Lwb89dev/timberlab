#include "timberlab/core/document.hpp"

#include "timberlab/core/transform.hpp"
#include "timberlab/units/units.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace timberlab {
namespace {

// An assembly runs once. It appends ordinary boards, parameters, and linear
// bindings. The file does not remember the recipe, and a later edit does not
// move a neighbour placed beside it.

constexpr std::int64_t kMm = units::kNmPerMm;
constexpr std::int64_t kMaxPlacementNm = units::kMaxPartNm * 10;
constexpr std::int64_t kAssemblyGapNm = 20 * kMm;

struct Recipe {
  std::vector<Component> parts;
  std::vector<constraints::Parameter> parameters;
  std::vector<constraints::Binding> bindings;
};

std::string newId() {
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

std::string driverId(const std::string& prefix, const char* role) { return prefix + "." + role; }

constraints::Term driver(const std::string& parameterId, std::int64_t coefficient) {
  constraints::Term term;
  term.parameterId = parameterId;
  term.coefficient = coefficient;
  return term;
}

const char* pickName(bool shortNames, const char* shortName, const char* fullName) {
  return shortNames ? shortName : fullName;
}

bool addDriver(Recipe& recipe, const std::string& id, const char* name, std::int64_t valueNm, std::string& error) {
  if (id.empty() || id.size() > Document::kMaxIdBytes) {
    error = "storage.json";
    return false;
  }
  constraints::Parameter parameter;
  parameter.id = id;
  parameter.name = name;
  parameter.valueNm = valueNm;
  recipe.parameters.push_back(std::move(parameter));
  return true;
}

std::string addPart(Recipe& recipe, const std::string& name, const std::string& material, std::int64_t x, std::int64_t y,
                    std::int64_t z, std::int64_t width, std::int64_t thickness, std::int64_t height) {
  Component component;
  component.id = newId();
  component.type = ComponentType::Board;
  component.name = name;
  component.materialId = material;
  component.placement.xNm = x;
  component.placement.yNm = y;
  component.placement.zNm = z;
  component.widthNm = width;
  component.thicknessNm = thickness;
  component.heightNm = height;
  recipe.parts.push_back(std::move(component));
  return recipe.parts.back().id;
}

void bindTo(Recipe& recipe, const std::string& partId, const char* field, std::int64_t constantNm,
            std::vector<constraints::Term> terms) {
  constraints::Binding binding;
  binding.partId = partId;
  binding.field = field;
  binding.constantNm = constantNm;
  binding.terms = std::move(terms);
  recipe.bindings.push_back(std::move(binding));
}

double cornerX(const double matrix[16], double lx, double ly, double lz) {
  double x = 0;
  double y = 0;
  double z = 0;
  transformPoint(matrix, lx, ly, lz, x, y, z);
  return x;
}

double maxWorldXmm(const Component& part) {
  const LocalBox box = localBox(part);
  double matrix[16];
  placementMatrix(part.placement, matrix);
  const double x0 = box.originXmm;
  const double x1 = box.originXmm + box.sizeXmm;
  const double y0 = box.originYmm;
  const double y1 = box.originYmm + box.sizeYmm;
  const double z0 = box.originZmm;
  const double z1 = box.originZmm + box.sizeZmm;
  double maxX = cornerX(matrix, x0, y0, z0);
  maxX = std::max(maxX, cornerX(matrix, x1, y0, z0));
  maxX = std::max(maxX, cornerX(matrix, x0, y1, z0));
  maxX = std::max(maxX, cornerX(matrix, x1, y1, z0));
  maxX = std::max(maxX, cornerX(matrix, x0, y0, z1));
  maxX = std::max(maxX, cornerX(matrix, x1, y0, z1));
  maxX = std::max(maxX, cornerX(matrix, x0, y1, z1));
  maxX = std::max(maxX, cornerX(matrix, x1, y1, z1));
  return maxX;
}

bool assemblyOrigin(const std::vector<Component>& components, std::int64_t& originNm, std::string& error) {
  if (components.empty()) {
    originNm = 0;
    return true;
  }
  double maxMm = -std::numeric_limits<double>::infinity();
  for (const Component& component : components) maxMm = std::max(maxMm, maxWorldXmm(component));
  if (!std::isfinite(maxMm)) {
    error = "parameter.range";
    return false;
  }
  const std::int64_t edge = std::llround(maxMm * static_cast<double>(kMm));
  if (edge > std::numeric_limits<std::int64_t>::max() - kAssemblyGapNm) {
    error = "parameter.range";
    return false;
  }
  originNm = edge + kAssemblyGapNm;
  return true;
}

bool buildTable(Recipe& recipe, const std::string& prefix, std::int64_t originNm, bool shortNames, std::string& error) {
  const std::int64_t topW = 1200 * kMm;
  const std::int64_t topD = 700 * kMm;
  const std::int64_t topT = 20 * kMm;
  const std::int64_t leg = 60 * kMm;
  const std::int64_t inset = 40 * kMm;
  const std::int64_t legH = 720 * kMm;
  const std::int64_t apronH = 90 * kMm;
  const std::int64_t apronT = 20 * kMm;
  const std::int64_t apronDrop = 70 * kMm;
  const std::int64_t apronZ = legH - apronDrop - apronH;
  const std::int64_t apronSpanX = topW - 2 * inset - 2 * leg;
  const std::int64_t apronSpanY = topD - 2 * inset - 2 * leg;

  const std::string widthId = driverId(prefix, "width");
  const std::string depthId = driverId(prefix, "depth");
  const std::string legId = driverId(prefix, "leg");
  const std::string insetId = driverId(prefix, "inset");
  const std::string legHeightId = driverId(prefix, "legHeight");
  const std::string topThicknessId = driverId(prefix, "topThickness");
  const std::string apronHeightId = driverId(prefix, "apronHeight");
  const std::string apronThicknessId = driverId(prefix, "apronThickness");
  const std::string apronDropId = driverId(prefix, "apronDrop");
  if (!addDriver(recipe, widthId, pickName(shortNames, "Width", "Table width"), topW, error) ||
      !addDriver(recipe, depthId, pickName(shortNames, "Depth", "Table depth"), topD, error) ||
      !addDriver(recipe, legId, pickName(shortNames, "Leg", "Table leg"), leg, error) ||
      !addDriver(recipe, insetId, pickName(shortNames, "Inset", "Table inset"), inset, error) ||
      !addDriver(recipe, legHeightId, pickName(shortNames, "Leg height", "Table leg height"), legH, error) ||
      !addDriver(recipe, topThicknessId, pickName(shortNames, "Top thickness", "Table top thickness"), topT, error) ||
      !addDriver(recipe, apronHeightId, pickName(shortNames, "Apron height", "Table apron height"), apronH, error) ||
      !addDriver(recipe, apronThicknessId, pickName(shortNames, "Apron thickness", "Table apron thickness"), apronT,
                 error) ||
      !addDriver(recipe, apronDropId, pickName(shortNames, "Apron drop", "Table apron drop"), apronDrop, error)) {
    return false;
  }

  const std::string leg1 = addPart(recipe, "Leg 1", "black-walnut", originNm + inset, inset, 0, leg, leg, legH);
  const std::string leg2 =
      addPart(recipe, "Leg 2", "black-walnut", originNm + topW - inset - leg, inset, 0, leg, leg, legH);
  const std::string leg3 =
      addPart(recipe, "Leg 3", "black-walnut", originNm + inset, topD - inset - leg, 0, leg, leg, legH);
  const std::string leg4 = addPart(recipe, "Leg 4", "black-walnut", originNm + topW - inset - leg, topD - inset - leg,
                                   0, leg, leg, legH);
  const std::string front = addPart(recipe, "Front apron", "european-beech", originNm + inset + leg, inset, apronZ,
                                    apronSpanX, apronT, apronH);
  const std::string back = addPart(recipe, "Back apron", "european-beech", originNm + inset + leg,
                                   topD - inset - apronT, apronZ, apronSpanX, apronT, apronH);
  const std::string left = addPart(recipe, "Left apron", "european-beech", originNm + inset, inset + leg, apronZ,
                                   apronT, apronSpanY, apronH);
  const std::string right = addPart(recipe, "Right apron", "european-beech", originNm + topW - inset - apronT,
                                    inset + leg, apronZ, apronT, apronSpanY, apronH);
  const std::string top = addPart(recipe, "Top", "european-oak", originNm, 0, legH, topW, topD, topT);

  const constraints::Term widthTerm = driver(widthId, 1);
  const constraints::Term depthTerm = driver(depthId, 1);
  const constraints::Term legSize = driver(legId, 1);
  const constraints::Term minusLeg = driver(legId, -1);
  const constraints::Term minusTwoLeg = driver(legId, -2);
  const constraints::Term insetTerm = driver(insetId, 1);
  const constraints::Term minusInset = driver(insetId, -1);
  const constraints::Term minusTwoInset = driver(insetId, -2);
  const constraints::Term legHeight = driver(legHeightId, 1);
  const constraints::Term topThickness = driver(topThicknessId, 1);
  const constraints::Term apronHeight = driver(apronHeightId, 1);
  const constraints::Term minusApronHeight = driver(apronHeightId, -1);
  const constraints::Term apronThickness = driver(apronThicknessId, 1);
  const constraints::Term minusApronThickness = driver(apronThicknessId, -1);
  const constraints::Term minusApronDrop = driver(apronDropId, -1);
  const std::vector<constraints::Term> apronOriginZ = {legHeight, minusApronDrop, minusApronHeight};
  const std::vector<constraints::Term> spanX = {widthTerm, minusTwoInset, minusTwoLeg};
  const std::vector<constraints::Term> spanY = {depthTerm, minusTwoInset, minusTwoLeg};

  for (const std::string& id : {leg1, leg2, leg3, leg4}) {
    bindTo(recipe, id, "width", 0, {legSize});
    bindTo(recipe, id, "thickness", 0, {legSize});
    bindTo(recipe, id, "height", 0, {legHeight});
  }
  bindTo(recipe, leg1, "x", originNm, {insetTerm});
  bindTo(recipe, leg1, "y", 0, {insetTerm});
  bindTo(recipe, leg2, "x", originNm, {widthTerm, minusInset, minusLeg});
  bindTo(recipe, leg2, "y", 0, {insetTerm});
  bindTo(recipe, leg3, "x", originNm, {insetTerm});
  bindTo(recipe, leg3, "y", 0, {depthTerm, minusInset, minusLeg});
  bindTo(recipe, leg4, "x", originNm, {widthTerm, minusInset, minusLeg});
  bindTo(recipe, leg4, "y", 0, {depthTerm, minusInset, minusLeg});

  bindTo(recipe, front, "x", originNm, {insetTerm, legSize});
  bindTo(recipe, front, "y", 0, {insetTerm});
  bindTo(recipe, front, "z", 0, apronOriginZ);
  bindTo(recipe, front, "width", 0, spanX);
  bindTo(recipe, front, "thickness", 0, {apronThickness});
  bindTo(recipe, front, "height", 0, {apronHeight});
  bindTo(recipe, back, "x", originNm, {insetTerm, legSize});
  bindTo(recipe, back, "y", 0, {depthTerm, minusInset, minusApronThickness});
  bindTo(recipe, back, "z", 0, apronOriginZ);
  bindTo(recipe, back, "width", 0, spanX);
  bindTo(recipe, back, "thickness", 0, {apronThickness});
  bindTo(recipe, back, "height", 0, {apronHeight});
  bindTo(recipe, left, "x", originNm, {insetTerm});
  bindTo(recipe, left, "y", 0, {insetTerm, legSize});
  bindTo(recipe, left, "z", 0, apronOriginZ);
  bindTo(recipe, left, "width", 0, {apronThickness});
  bindTo(recipe, left, "thickness", 0, spanY);
  bindTo(recipe, left, "height", 0, {apronHeight});
  bindTo(recipe, right, "x", originNm, {widthTerm, minusInset, minusApronThickness});
  bindTo(recipe, right, "y", 0, {insetTerm, legSize});
  bindTo(recipe, right, "z", 0, apronOriginZ);
  bindTo(recipe, right, "width", 0, {apronThickness});
  bindTo(recipe, right, "thickness", 0, spanY);
  bindTo(recipe, right, "height", 0, {apronHeight});

  bindTo(recipe, top, "z", 0, {legHeight});
  bindTo(recipe, top, "width", 0, {widthTerm});
  bindTo(recipe, top, "thickness", 0, {depthTerm});
  bindTo(recipe, top, "height", 0, {topThickness});
  return true;
}

bool buildCarcass(Recipe& recipe, const std::string& prefix, std::int64_t originNm, std::string& error) {
  const std::int64_t width = 600 * kMm;
  const std::int64_t depth = 500 * kMm;
  const std::int64_t height = 720 * kMm;
  const std::int64_t side = 18 * kMm;
  const std::int64_t panel = 18 * kMm;
  const std::int64_t back = 6 * kMm;
  const std::int64_t innerW = width - 2 * side;
  const std::int64_t innerD = depth - back;
  const std::int64_t backH = height - 2 * panel;

  const std::string widthId = driverId(prefix, "width");
  const std::string depthId = driverId(prefix, "depth");
  const std::string heightId = driverId(prefix, "height");
  const std::string sideId = driverId(prefix, "side");
  const std::string panelId = driverId(prefix, "panel");
  const std::string backId = driverId(prefix, "back");
  if (!addDriver(recipe, widthId, "Cabinet width", width, error) ||
      !addDriver(recipe, depthId, "Cabinet depth", depth, error) ||
      !addDriver(recipe, heightId, "Cabinet height", height, error) ||
      !addDriver(recipe, sideId, "Cabinet side", side, error) ||
      !addDriver(recipe, panelId, "Cabinet panel", panel, error) ||
      !addDriver(recipe, backId, "Cabinet back", back, error)) {
    return false;
  }

  const std::string left = addPart(recipe, "Carcass left", "birch-plywood", originNm, 0, 0, side, depth, height);
  const std::string right =
      addPart(recipe, "Carcass right", "birch-plywood", originNm + width - side, 0, 0, side, depth, height);
  const std::string bottom =
      addPart(recipe, "Carcass bottom", "birch-plywood", originNm + side, 0, 0, innerW, innerD, panel);
  const std::string top =
      addPart(recipe, "Carcass top", "birch-plywood", originNm + side, 0, height - panel, innerW, innerD, panel);
  const std::string backPart =
      addPart(recipe, "Carcass back", "birch-plywood", originNm + side, depth - back, panel, innerW, back, backH);

  const constraints::Term widthTerm = driver(widthId, 1);
  const constraints::Term depthTerm = driver(depthId, 1);
  const constraints::Term heightTerm = driver(heightId, 1);
  const constraints::Term sideTerm = driver(sideId, 1);
  const constraints::Term minusSide = driver(sideId, -1);
  const constraints::Term minusTwoSide = driver(sideId, -2);
  const constraints::Term panelTerm = driver(panelId, 1);
  const constraints::Term minusPanel = driver(panelId, -1);
  const constraints::Term minusTwoPanel = driver(panelId, -2);
  const constraints::Term backTerm = driver(backId, 1);
  const constraints::Term minusBack = driver(backId, -1);

  bindTo(recipe, left, "width", 0, {sideTerm});
  bindTo(recipe, left, "thickness", 0, {depthTerm});
  bindTo(recipe, left, "height", 0, {heightTerm});
  bindTo(recipe, right, "x", originNm, {widthTerm, minusSide});
  bindTo(recipe, right, "width", 0, {sideTerm});
  bindTo(recipe, right, "thickness", 0, {depthTerm});
  bindTo(recipe, right, "height", 0, {heightTerm});
  bindTo(recipe, bottom, "x", originNm, {sideTerm});
  bindTo(recipe, bottom, "width", 0, {widthTerm, minusTwoSide});
  bindTo(recipe, bottom, "thickness", 0, {depthTerm, minusBack});
  bindTo(recipe, bottom, "height", 0, {panelTerm});
  bindTo(recipe, top, "x", originNm, {sideTerm});
  bindTo(recipe, top, "z", 0, {heightTerm, minusPanel});
  bindTo(recipe, top, "width", 0, {widthTerm, minusTwoSide});
  bindTo(recipe, top, "thickness", 0, {depthTerm, minusBack});
  bindTo(recipe, top, "height", 0, {panelTerm});
  bindTo(recipe, backPart, "x", originNm, {sideTerm});
  bindTo(recipe, backPart, "y", 0, {depthTerm, minusBack});
  bindTo(recipe, backPart, "z", 0, {panelTerm});
  bindTo(recipe, backPart, "width", 0, {widthTerm, minusTwoSide});
  bindTo(recipe, backPart, "thickness", 0, {backTerm});
  bindTo(recipe, backPart, "height", 0, {heightTerm, minusTwoPanel});
  return true;
}

bool buildDrawer(Recipe& recipe, const std::string& prefix, std::int64_t originNm, std::string& error) {
  const std::int64_t width = 500 * kMm;
  const std::int64_t depth = 400 * kMm;
  const std::int64_t height = 140 * kMm;
  const std::int64_t side = 12 * kMm;
  const std::int64_t bottomT = 6 * kMm;
  const std::int64_t innerW = width - 2 * side;
  const std::int64_t innerD = depth - 2 * side;
  const std::int64_t wallH = height - bottomT;

  const std::string widthId = driverId(prefix, "width");
  const std::string depthId = driverId(prefix, "depth");
  const std::string heightId = driverId(prefix, "height");
  const std::string sideId = driverId(prefix, "side");
  const std::string bottomId = driverId(prefix, "bottom");
  if (!addDriver(recipe, widthId, "Drawer width", width, error) ||
      !addDriver(recipe, depthId, "Drawer depth", depth, error) ||
      !addDriver(recipe, heightId, "Drawer height", height, error) ||
      !addDriver(recipe, sideId, "Drawer side", side, error) ||
      !addDriver(recipe, bottomId, "Drawer bottom", bottomT, error)) {
    return false;
  }

  const std::string left = addPart(recipe, "Drawer left", "european-beech", originNm, 0, 0, side, depth, height);
  const std::string right =
      addPart(recipe, "Drawer right", "european-beech", originNm + width - side, 0, 0, side, depth, height);
  const std::string front =
      addPart(recipe, "Drawer front", "european-beech", originNm + side, 0, bottomT, innerW, side, wallH);
  const std::string back = addPart(recipe, "Drawer back", "european-beech", originNm + side, depth - side, bottomT,
                                   innerW, side, wallH);
  const std::string bottom =
      addPart(recipe, "Drawer bottom", "poplar-plywood", originNm + side, side, 0, innerW, innerD, bottomT);

  const constraints::Term widthTerm = driver(widthId, 1);
  const constraints::Term depthTerm = driver(depthId, 1);
  const constraints::Term heightTerm = driver(heightId, 1);
  const constraints::Term sideTerm = driver(sideId, 1);
  const constraints::Term minusSide = driver(sideId, -1);
  const constraints::Term minusTwoSide = driver(sideId, -2);
  const constraints::Term bottomTerm = driver(bottomId, 1);
  const constraints::Term minusBottom = driver(bottomId, -1);

  bindTo(recipe, left, "width", 0, {sideTerm});
  bindTo(recipe, left, "thickness", 0, {depthTerm});
  bindTo(recipe, left, "height", 0, {heightTerm});
  bindTo(recipe, right, "x", originNm, {widthTerm, minusSide});
  bindTo(recipe, right, "width", 0, {sideTerm});
  bindTo(recipe, right, "thickness", 0, {depthTerm});
  bindTo(recipe, right, "height", 0, {heightTerm});
  bindTo(recipe, front, "x", originNm, {sideTerm});
  bindTo(recipe, front, "z", 0, {bottomTerm});
  bindTo(recipe, front, "width", 0, {widthTerm, minusTwoSide});
  bindTo(recipe, front, "thickness", 0, {sideTerm});
  bindTo(recipe, front, "height", 0, {heightTerm, minusBottom});
  bindTo(recipe, back, "x", originNm, {sideTerm});
  bindTo(recipe, back, "y", 0, {depthTerm, minusSide});
  bindTo(recipe, back, "z", 0, {bottomTerm});
  bindTo(recipe, back, "width", 0, {widthTerm, minusTwoSide});
  bindTo(recipe, back, "thickness", 0, {sideTerm});
  bindTo(recipe, back, "height", 0, {heightTerm, minusBottom});
  bindTo(recipe, bottom, "x", originNm, {sideTerm});
  bindTo(recipe, bottom, "y", 0, {sideTerm});
  bindTo(recipe, bottom, "width", 0, {widthTerm, minusTwoSide});
  bindTo(recipe, bottom, "thickness", 0, {depthTerm, minusTwoSide});
  bindTo(recipe, bottom, "height", 0, {bottomTerm});
  return true;
}

bool buildRecipe(std::string_view kind, const std::string& prefix, std::int64_t originNm, bool shortNames,
                 Recipe& recipe, std::string& error) {
  if (kind == "table") return buildTable(recipe, prefix, originNm, shortNames, error);
  if (kind == "carcass") return buildCarcass(recipe, prefix, originNm, error);
  if (kind == "drawer") return buildDrawer(recipe, prefix, originNm, error);
  error = "assembly.unknown";
  return false;
}

bool idTaken(const std::vector<Component>& components, const std::string& id) {
  for (const Component& component : components) {
    if (component.id == id) return true;
  }
  return false;
}

bool driverTaken(const std::vector<constraints::Parameter>& parameters, const std::string& id) {
  for (const constraints::Parameter& parameter : parameters) {
    if (parameter.id == id) return true;
  }
  return false;
}

bool recipeClashes(const Recipe& recipe, const std::vector<Component>& components,
                   const std::vector<constraints::Parameter>& parameters) {
  for (std::size_t i = 0; i < recipe.parts.size(); ++i) {
    if (idTaken(components, recipe.parts[i].id)) return true;
    for (std::size_t j = 0; j < i; ++j) {
      if (recipe.parts[j].id == recipe.parts[i].id) return true;
    }
  }
  for (std::size_t i = 0; i < recipe.parameters.size(); ++i) {
    if (driverTaken(parameters, recipe.parameters[i].id)) return true;
    for (std::size_t j = 0; j < i; ++j) {
      if (recipe.parameters[j].id == recipe.parameters[i].id) return true;
    }
  }
  return false;
}

const Component* findPart(const std::vector<Component>& components, const std::string& id) {
  for (const Component& component : components) {
    if (component.id == id) return &component;
  }
  return nullptr;
}

bool storedJointsFit(const std::vector<Component>& components, const std::vector<Joint>& joints, std::string& error) {
  for (const Joint& joint : joints) {
    const Component* partA = findPart(components, joint.partA);
    const Component* partB = findPart(components, joint.partB);
    if (partA == nullptr || partB == nullptr) {
      error = "component.missing";
      return false;
    }
    if (!jointFits(joint, *partA, *partB, error)) return false;
  }
  return true;
}

bool placementsFit(const std::vector<Component>& components, std::string& error) {
  for (const Component& component : components) {
    const std::int64_t x = component.placement.xNm;
    const std::int64_t y = component.placement.yNm;
    const std::int64_t z = component.placement.zNm;
    if (x > kMaxPlacementNm || x < -kMaxPlacementNm || y > kMaxPlacementNm || y < -kMaxPlacementNm ||
        z > kMaxPlacementNm || z < -kMaxPlacementNm) {
      error = "parameter.range";
      return false;
    }
  }
  return true;
}

}  // namespace

void Document::makeDemoTable() {
  newProject("mm", 16);
  name_ = "Demo table";
  Recipe recipe;
  std::string error;
  if (buildRecipe("table", "table", 0, true, recipe, error)) {
    components_ = std::move(recipe.parts);
    parameters_ = std::move(recipe.parameters);
    bindings_ = std::move(recipe.bindings);
  }
  // The defaults are valid. A failure still keeps the drivers, as this demo always did.
  constraints::propagateBindings(parameters_, bindings_, components_, error);
  pushHistory();
}

bool Document::addAssembly(std::string_view kind, std::string& error) {
  if (kind != "table" && kind != "carcass" && kind != "drawer") {
    error = "assembly.unknown";
    return false;
  }
  std::int64_t originNm = 0;
  if (!assemblyOrigin(components_, originNm, error)) return false;

  Recipe recipe;
  const std::string prefix = std::string(kind) + "_" + newId();
  if (!buildRecipe(kind, prefix, originNm, false, recipe, error)) return false;
  if (components_.size() + recipe.parts.size() > kMaxComponents ||
      parameters_.size() + recipe.parameters.size() > kMaxComponents ||
      bindings_.size() + recipe.bindings.size() > kMaxComponents) {
    error = "storage.too_many";
    return false;
  }
  if (recipeClashes(recipe, components_, parameters_)) {
    error = "storage.duplicate";
    return false;
  }

  std::vector<Component> parts = components_;
  parts.insert(parts.end(), recipe.parts.begin(), recipe.parts.end());
  std::vector<constraints::Parameter> parameters = parameters_;
  parameters.insert(parameters.end(), recipe.parameters.begin(), recipe.parameters.end());
  std::vector<constraints::Binding> bindings = bindings_;
  bindings.insert(bindings.end(), recipe.bindings.begin(), recipe.bindings.end());
  if (!constraints::propagateBindings(parameters, bindings, parts, error)) return false;
  if (!placementsFit(parts, error)) return false;
  if (!storedJointsFit(parts, joints_, error)) return false;

  components_ = std::move(parts);
  parameters_ = std::move(parameters);
  bindings_ = std::move(bindings);
  pushHistory();
  return true;
}

}  // namespace timberlab
