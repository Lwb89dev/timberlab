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
  for (Component& part : recipe.parts) {
    if (part.name.rfind("Leg", 0) == 0) part.role = "leg";
    else if (part.name.find("apron") != std::string::npos) part.role = "apron";
    else if (part.name == "Top") part.role = "top";
  }
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
  for (Component& part : recipe.parts) {
    if (part.name.find("left") != std::string::npos || part.name.find("right") != std::string::npos) part.role = "side";
    else if (part.name.find("back") != std::string::npos) part.role = "back";
    else part.role = "panel";
  }
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
  for (Component& part : recipe.parts) {
    if (part.name.find("left") != std::string::npos || part.name.find("right") != std::string::npos) part.role = "side";
    else if (part.name.find("front") != std::string::npos) part.role = "front";
    else if (part.name.find("back") != std::string::npos) part.role = "back";
    else part.role = "bottom";
  }
  return true;
}

bool buildFrame(Recipe& recipe, const std::string& prefix, std::int64_t originNm, std::string& error) {
  const std::int64_t width = 600 * kMm;
  const std::int64_t height = 800 * kMm;
  const std::int64_t stock = 60 * kMm;
  const std::int64_t depth = 20 * kMm;
  const std::int64_t span = width - 2 * stock;
  const std::string widthId = driverId(prefix, "width");
  const std::string heightId = driverId(prefix, "height");
  const std::string stockId = driverId(prefix, "stock");
  const std::string depthId = driverId(prefix, "depth");
  if (!addDriver(recipe, widthId, "Frame width", width, error) ||
      !addDriver(recipe, heightId, "Frame height", height, error) ||
      !addDriver(recipe, stockId, "Frame stock", stock, error) ||
      !addDriver(recipe, depthId, "Frame depth", depth, error)) {
    return false;
  }
  const std::string left = addPart(recipe, "Frame left", "european-oak", originNm, 0, 0, stock, depth, height);
  const std::string right =
      addPart(recipe, "Frame right", "european-oak", originNm + width - stock, 0, 0, stock, depth, height);
  const std::string bottom =
      addPart(recipe, "Frame bottom", "european-oak", originNm + stock, 0, 0, span, depth, stock);
  const std::string top =
      addPart(recipe, "Frame top", "european-oak", originNm + stock, 0, height - stock, span, depth, stock);
  const constraints::Term widthTerm = driver(widthId, 1);
  const constraints::Term heightTerm = driver(heightId, 1);
  const constraints::Term stockTerm = driver(stockId, 1);
  const constraints::Term minusStock = driver(stockId, -1);
  const constraints::Term minusTwoStock = driver(stockId, -2);
  const constraints::Term depthTerm = driver(depthId, 1);
  bindTo(recipe, left, "width", 0, {stockTerm});
  bindTo(recipe, left, "thickness", 0, {depthTerm});
  bindTo(recipe, left, "height", 0, {heightTerm});
  bindTo(recipe, right, "x", originNm, {widthTerm, minusStock});
  bindTo(recipe, right, "width", 0, {stockTerm});
  bindTo(recipe, right, "thickness", 0, {depthTerm});
  bindTo(recipe, right, "height", 0, {heightTerm});
  bindTo(recipe, bottom, "x", originNm, {stockTerm});
  bindTo(recipe, bottom, "width", 0, {widthTerm, minusTwoStock});
  bindTo(recipe, bottom, "thickness", 0, {depthTerm});
  bindTo(recipe, bottom, "height", 0, {stockTerm});
  bindTo(recipe, top, "x", originNm, {stockTerm});
  bindTo(recipe, top, "z", 0, {heightTerm, minusStock});
  bindTo(recipe, top, "width", 0, {widthTerm, minusTwoStock});
  bindTo(recipe, top, "thickness", 0, {depthTerm});
  bindTo(recipe, top, "height", 0, {stockTerm});
  for (Component& part : recipe.parts) part.role = part.name.find("left") != std::string::npos ||
                                                           part.name.find("right") != std::string::npos
                                                       ? "stile"
                                                       : "rail";
  return true;
}

bool buildShelving(Recipe& recipe, const std::string& prefix, std::int64_t originNm, std::string& error) {
  const std::int64_t width = 800 * kMm;
  const std::int64_t depth = 320 * kMm;
  const std::int64_t side = 18 * kMm;
  const std::int64_t shelf = 18 * kMm;
  const std::int64_t bay = 300 * kMm;
  const std::int64_t height = 4 * shelf + 3 * bay;
  const std::int64_t inner = width - 2 * side;
  const std::string widthId = driverId(prefix, "width");
  const std::string depthId = driverId(prefix, "depth");
  const std::string sideId = driverId(prefix, "side");
  const std::string shelfId = driverId(prefix, "shelf");
  const std::string bayId = driverId(prefix, "bay");
  if (!addDriver(recipe, widthId, "Shelf width", width, error) ||
      !addDriver(recipe, depthId, "Shelf depth", depth, error) ||
      !addDriver(recipe, sideId, "Shelf side", side, error) ||
      !addDriver(recipe, shelfId, "Shelf thickness", shelf, error) ||
      !addDriver(recipe, bayId, "Shelf bay", bay, error)) {
    return false;
  }
  const std::string left = addPart(recipe, "Shelf left", "birch-plywood", originNm, 0, 0, side, depth, height);
  const std::string right =
      addPart(recipe, "Shelf right", "birch-plywood", originNm + width - side, 0, 0, side, depth, height);
  const std::string bottom = addPart(recipe, "Shelf bottom", "birch-plywood", originNm + side, 0, 0, inner, depth, shelf);
  const std::string mid1 =
      addPart(recipe, "Shelf mid 1", "birch-plywood", originNm + side, 0, shelf + bay, inner, depth, shelf);
  const std::string mid2 =
      addPart(recipe, "Shelf mid 2", "birch-plywood", originNm + side, 0, 2 * shelf + 2 * bay, inner, depth, shelf);
  const std::string top = addPart(recipe, "Shelf top", "birch-plywood", originNm + side, 0, 3 * shelf + 3 * bay, inner,
                                  depth, shelf);
  const constraints::Term widthTerm = driver(widthId, 1);
  const constraints::Term depthTerm = driver(depthId, 1);
  const constraints::Term sideTerm = driver(sideId, 1);
  const constraints::Term minusSide = driver(sideId, -1);
  const constraints::Term minusTwoSide = driver(sideId, -2);
  const constraints::Term shelfTerm = driver(shelfId, 1);
  const constraints::Term twoShelf = driver(shelfId, 2);
  const constraints::Term threeShelf = driver(shelfId, 3);
  const constraints::Term bayTerm = driver(bayId, 1);
  const constraints::Term twoBay = driver(bayId, 2);
  const constraints::Term threeBay = driver(bayId, 3);
  const constraints::Term fourShelf = driver(shelfId, 4);
  bindTo(recipe, left, "width", 0, {sideTerm});
  bindTo(recipe, left, "thickness", 0, {depthTerm});
  bindTo(recipe, left, "height", 0, {fourShelf, threeBay});
  bindTo(recipe, right, "x", originNm, {widthTerm, minusSide});
  bindTo(recipe, right, "width", 0, {sideTerm});
  bindTo(recipe, right, "thickness", 0, {depthTerm});
  bindTo(recipe, right, "height", 0, {fourShelf, threeBay});
  for (const std::string& id : {bottom, mid1, mid2, top}) {
    bindTo(recipe, id, "x", originNm, {sideTerm});
    bindTo(recipe, id, "width", 0, {widthTerm, minusTwoSide});
    bindTo(recipe, id, "thickness", 0, {depthTerm});
    bindTo(recipe, id, "height", 0, {shelfTerm});
  }
  bindTo(recipe, mid1, "z", 0, {shelfTerm, bayTerm});
  bindTo(recipe, mid2, "z", 0, {twoShelf, twoBay});
  bindTo(recipe, top, "z", 0, {threeShelf, threeBay});
  for (Component& part : recipe.parts) {
    part.role = part.name.find("left") != std::string::npos || part.name.find("right") != std::string::npos ? "side"
                                                                                                             : "shelf";
  }
  return true;
}

bool buildBedside(Recipe& recipe, const std::string& prefix, std::int64_t originNm, std::string& error) {
  const std::int64_t width = 450 * kMm;
  const std::int64_t depth = 380 * kMm;
  const std::int64_t height = 550 * kMm;
  const std::int64_t leg = 35 * kMm;
  const std::int64_t topT = 20 * kMm;
  const std::int64_t drawerH = 140 * kMm;
  const std::int64_t side = 12 * kMm;
  const std::int64_t bottomT = 6 * kMm;
  const std::int64_t gap = 40 * kMm;
  const std::int64_t shelfT = 18 * kMm;
  const std::int64_t shelfZ = 120 * kMm;
  const std::int64_t legH = height - topT;
  const std::int64_t drawerZ = height - topT - drawerH - gap;
  const std::int64_t innerW = width - 2 * leg - 2 * side;
  const std::int64_t innerD = depth - 2 * leg - 2 * side;
  const std::int64_t boxW = width - 2 * leg;
  const std::int64_t boxD = depth - 2 * leg;
  const std::string widthId = driverId(prefix, "width");
  const std::string depthId = driverId(prefix, "depth");
  const std::string heightId = driverId(prefix, "height");
  const std::string legId = driverId(prefix, "leg");
  const std::string topId = driverId(prefix, "top");
  const std::string drawerId = driverId(prefix, "drawerHeight");
  const std::string sideId = driverId(prefix, "side");
  const std::string bottomId = driverId(prefix, "bottom");
  if (!addDriver(recipe, widthId, "Bedside width", width, error) ||
      !addDriver(recipe, depthId, "Bedside depth", depth, error) ||
      !addDriver(recipe, heightId, "Bedside height", height, error) ||
      !addDriver(recipe, legId, "Bedside leg", leg, error) ||
      !addDriver(recipe, topId, "Bedside top", topT, error) ||
      !addDriver(recipe, drawerId, "Bedside drawer", drawerH, error) ||
      !addDriver(recipe, sideId, "Bedside side", side, error) ||
      !addDriver(recipe, bottomId, "Bedside bottom", bottomT, error)) {
    return false;
  }
  const std::string leg1 = addPart(recipe, "Bedside leg 1", "european-oak", originNm, 0, 0, leg, leg, legH);
  const std::string leg2 =
      addPart(recipe, "Bedside leg 2", "european-oak", originNm + width - leg, 0, 0, leg, leg, legH);
  const std::string leg3 =
      addPart(recipe, "Bedside leg 3", "european-oak", originNm, depth - leg, 0, leg, leg, legH);
  const std::string leg4 =
      addPart(recipe, "Bedside leg 4", "european-oak", originNm + width - leg, depth - leg, 0, leg, leg, legH);
  const std::string top = addPart(recipe, "Bedside top", "european-oak", originNm, 0, height - topT, width, depth, topT);
  const std::string shelf = addPart(recipe, "Bedside shelf", "european-oak", originNm + leg, leg, shelfZ, boxW, boxD, shelfT);
  const std::string left =
      addPart(recipe, "Bedside drawer left", "european-beech", originNm + leg, leg, drawerZ, side, boxD, drawerH);
  const std::string right = addPart(recipe, "Bedside drawer right", "european-beech", originNm + width - leg - side, leg,
                                    drawerZ, side, boxD, drawerH);
  const std::string front = addPart(recipe, "Bedside drawer front", "european-beech", originNm + leg + side, leg,
                                    drawerZ + bottomT, innerW, side, drawerH - bottomT);
  const std::string back = addPart(recipe, "Bedside drawer back", "european-beech", originNm + leg + side,
                                   depth - leg - side, drawerZ + bottomT, innerW, side, drawerH - bottomT);
  const std::string bottom = addPart(recipe, "Bedside drawer bottom", "poplar-plywood", originNm + leg + side, leg + side,
                                     drawerZ, innerW, innerD, bottomT);
  const constraints::Term widthTerm = driver(widthId, 1);
  const constraints::Term depthTerm = driver(depthId, 1);
  const constraints::Term heightTerm = driver(heightId, 1);
  const constraints::Term legTerm = driver(legId, 1);
  const constraints::Term minusLeg = driver(legId, -1);
  const constraints::Term minusTwoLeg = driver(legId, -2);
  const constraints::Term topTerm = driver(topId, 1);
  const constraints::Term minusTop = driver(topId, -1);
  const constraints::Term drawerTerm = driver(drawerId, 1);
  const constraints::Term minusDrawer = driver(drawerId, -1);
  const constraints::Term sideTerm = driver(sideId, 1);
  const constraints::Term minusSide = driver(sideId, -1);
  const constraints::Term minusTwoSide = driver(sideId, -2);
  const constraints::Term bottomTerm = driver(bottomId, 1);
  const constraints::Term minusBottom = driver(bottomId, -1);
  for (const std::string& id : {leg1, leg2, leg3, leg4}) {
    bindTo(recipe, id, "width", 0, {legTerm});
    bindTo(recipe, id, "thickness", 0, {legTerm});
    bindTo(recipe, id, "height", 0, {heightTerm, minusTop});
  }
  bindTo(recipe, leg2, "x", originNm, {widthTerm, minusLeg});
  bindTo(recipe, leg3, "y", 0, {depthTerm, minusLeg});
  bindTo(recipe, leg4, "x", originNm, {widthTerm, minusLeg});
  bindTo(recipe, leg4, "y", 0, {depthTerm, minusLeg});
  bindTo(recipe, top, "z", 0, {heightTerm, minusTop});
  bindTo(recipe, top, "width", 0, {widthTerm});
  bindTo(recipe, top, "thickness", 0, {depthTerm});
  bindTo(recipe, top, "height", 0, {topTerm});
  bindTo(recipe, shelf, "x", originNm, {legTerm});
  bindTo(recipe, shelf, "y", 0, {legTerm});
  bindTo(recipe, shelf, "width", 0, {widthTerm, minusTwoLeg});
  bindTo(recipe, shelf, "thickness", 0, {depthTerm, minusTwoLeg});
  bindTo(recipe, left, "x", originNm, {legTerm});
  bindTo(recipe, left, "y", 0, {legTerm});
  bindTo(recipe, left, "z", -gap, {heightTerm, minusTop, minusDrawer});
  bindTo(recipe, left, "width", 0, {sideTerm});
  bindTo(recipe, left, "thickness", 0, {depthTerm, minusTwoLeg});
  bindTo(recipe, left, "height", 0, {drawerTerm});
  bindTo(recipe, right, "x", originNm, {widthTerm, minusLeg, minusSide});
  bindTo(recipe, right, "y", 0, {legTerm});
  bindTo(recipe, right, "z", -gap, {heightTerm, minusTop, minusDrawer});
  bindTo(recipe, right, "width", 0, {sideTerm});
  bindTo(recipe, right, "thickness", 0, {depthTerm, minusTwoLeg});
  bindTo(recipe, right, "height", 0, {drawerTerm});
  bindTo(recipe, front, "x", originNm, {legTerm, sideTerm});
  bindTo(recipe, front, "y", 0, {legTerm});
  bindTo(recipe, front, "z", -gap, {heightTerm, minusTop, minusDrawer, bottomTerm});
  bindTo(recipe, front, "width", 0, {widthTerm, minusTwoLeg, minusTwoSide});
  bindTo(recipe, front, "thickness", 0, {sideTerm});
  bindTo(recipe, front, "height", 0, {drawerTerm, minusBottom});
  bindTo(recipe, back, "x", originNm, {legTerm, sideTerm});
  bindTo(recipe, back, "y", 0, {depthTerm, minusLeg, minusSide});
  bindTo(recipe, back, "z", -gap, {heightTerm, minusTop, minusDrawer, bottomTerm});
  bindTo(recipe, back, "width", 0, {widthTerm, minusTwoLeg, minusTwoSide});
  bindTo(recipe, back, "thickness", 0, {sideTerm});
  bindTo(recipe, back, "height", 0, {drawerTerm, minusBottom});
  bindTo(recipe, bottom, "x", originNm, {legTerm, sideTerm});
  bindTo(recipe, bottom, "y", 0, {legTerm, sideTerm});
  bindTo(recipe, bottom, "z", -gap, {heightTerm, minusTop, minusDrawer});
  bindTo(recipe, bottom, "width", 0, {widthTerm, minusTwoLeg, minusTwoSide});
  bindTo(recipe, bottom, "thickness", 0, {depthTerm, minusTwoLeg, minusTwoSide});
  bindTo(recipe, bottom, "height", 0, {bottomTerm});
  for (Component& part : recipe.parts) {
    if (part.name.find("leg") != std::string::npos) part.role = "leg";
    else if (part.name.find("top") != std::string::npos) part.role = "top";
    else if (part.name.find("shelf") != std::string::npos) part.role = "shelf";
    else if (part.name.find("left") != std::string::npos || part.name.find("right") != std::string::npos)
      part.role = "side";
    else if (part.name.find("front") != std::string::npos) part.role = "front";
    else if (part.name.find("back") != std::string::npos) part.role = "back";
    else part.role = "bottom";
  }
  return true;
}

bool buildRecipe(std::string_view kind, const std::string& prefix, std::int64_t originNm, bool shortNames,
                 Recipe& recipe, std::string& error) {
  if (kind == "table") return buildTable(recipe, prefix, originNm, shortNames, error);
  if (kind == "carcass") return buildCarcass(recipe, prefix, originNm, error);
  if (kind == "drawer") return buildDrawer(recipe, prefix, originNm, error);
  if (kind == "frame") return buildFrame(recipe, prefix, originNm, error);
  if (kind == "shelving") return buildShelving(recipe, prefix, originNm, error);
  if (kind == "bedside") return buildBedside(recipe, prefix, originNm, error);
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
  if (kind != "table" && kind != "carcass" && kind != "drawer" && kind != "frame" && kind != "shelving" &&
      kind != "bedside") {
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
