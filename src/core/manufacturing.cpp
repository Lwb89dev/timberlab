#include "timberlab/core/manufacturing.hpp"

#include "timberlab/geometry/solids.hpp"
#include "timberlab/units/units.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace timberlab::manufacturing {
namespace {

bool addAllowance(std::int64_t value, std::int64_t allowance, std::int64_t& out, std::string& error) {
  if (allowance < 0 || value < 0 || value > std::numeric_limits<std::int64_t>::max() - allowance) {
    error = "parameter.range";
    return false;
  }
  out = value + allowance;
  if (out <= 0 || out > units::kMaxPartNm) {
    error = "parameter.range";
    return false;
  }
  return true;
}

bool finishedSize(const Component& component, CutPiece& piece, std::string& error) {
  if (component.type == ComponentType::Cylinder) {
    if (component.radiusNm <= 0 || component.radiusNm > std::numeric_limits<std::int64_t>::max() / 2) {
      error = "parameter.range";
      return false;
    }
    piece.finishedWidthNm = component.radiusNm * 2;
    piece.finishedThicknessNm = piece.finishedWidthNm;
    piece.finishedHeightNm = component.heightNm;
    piece.shape = "cylinder";
    return true;
  }
  piece.finishedWidthNm = component.widthNm;
  piece.finishedThicknessNm = component.thicknessNm;
  piece.finishedHeightNm = component.heightNm;
  piece.shape = component.type == ComponentType::Box ? "box" : "board";
  return true;
}

std::string millimetresText(std::int64_t nm) {
  const auto magnitude = static_cast<std::uint64_t>(nm < 0 ? 0 : nm);
  std::ostringstream out;
  out << magnitude / 1000000ULL << '.' << std::setw(6) << std::setfill('0') << (magnitude % 1000000ULL);
  return out.str();
}

std::string csvField(const std::string& value) {
  const bool formula = !value.empty() && (value[0] == '=' || value[0] == '+' || value[0] == '-' || value[0] == '@');
  const bool quote = formula || value.find_first_of(",\"\n\r") != std::string::npos;
  if (!quote) return value;
  std::string out = "\"";
  if (formula) out.push_back('\'');
  for (char character : value) {
    if (character == '"') out += "\"\"";
    else out.push_back(character);
  }
  out.push_back('"');
  return out;
}

constexpr std::int64_t kMarginUm = 1000;
constexpr std::int64_t kSheetGapUm = 10000;

enum class WirePlane { Front, Top, Right };

struct Projected {
  std::int64_t u = 0;
  std::int64_t v = 0;
  bool operator<(const Projected& other) const { return u < other.u || (u == other.u && v < other.v); }
};

Projected project(float horizontalMm, float verticalMm) {
  return {std::llround(static_cast<double>(horizontalMm) * 1000.0),
          std::llround(static_cast<double>(verticalMm) * 1000.0)};
}

Projected projectVertex(float xMm, float yMm, float zMm, WirePlane plane) {
  if (plane == WirePlane::Top) return project(xMm, yMm);
  if (plane == WirePlane::Right) return project(yMm, zMm);
  return project(xMm, zMm);
}

bool projectedOf(const geometry::SolidMesh& mesh, std::uint32_t index, WirePlane plane, Projected& out) {
  const std::size_t vertex = index;
  if (vertex >= mesh.interleaved.size() / 6) return false;
  const std::size_t offset = vertex * 6;
  out = projectVertex(mesh.interleaved[offset], mesh.interleaved[offset + 1], mesh.interleaved[offset + 2], plane);
  return true;
}

void addTriangleEdges(const geometry::SolidMesh& mesh, std::size_t index, WirePlane plane,
                      std::set<std::pair<Projected, Projected>>& edges) {
  Projected points[3];
  for (int corner = 0; corner < 3; ++corner) {
    if (!projectedOf(mesh, mesh.indices[index + static_cast<std::size_t>(corner)], plane, points[corner])) return;
  }
  for (int corner = 0; corner < 3; ++corner) {
    Projected a = points[corner];
    Projected b = points[(corner + 1) % 3];
    if (!(a < b) && !(b < a)) continue;
    if (b < a) std::swap(a, b);
    edges.insert({a, b});
  }
}

std::string svgNumber(std::int64_t micrometres) {
  const bool negative = micrometres < 0;
  const std::uint64_t magnitude =
      negative ? static_cast<std::uint64_t>(-(micrometres + 1)) + 1 : static_cast<std::uint64_t>(micrometres);
  std::ostringstream out;
  if (negative) out << '-';
  out << magnitude / 1000ULL << '.' << std::setw(3) << std::setfill('0') << (magnitude % 1000ULL);
  return out.str();
}

struct ViewWire {
  std::string layer;
  std::string comment;
  std::set<std::pair<Projected, Projected>> edges;
  std::int64_t minU = 0;
  std::int64_t maxU = 0;
  std::int64_t minV = 0;
  std::int64_t maxV = 0;
};

bool collectEdges(const geometry::SolidMesh& mesh, WirePlane plane, ViewWire& view, std::string& error) {
  view.edges.clear();
  for (std::size_t index = 0; index + 2 < mesh.indices.size(); index += 3) {
    addTriangleEdges(mesh, index, plane, view.edges);
  }
  if (view.edges.empty()) {
    error = "geometry.empty";
    return false;
  }
  view.minU = view.edges.begin()->first.u;
  view.maxU = view.minU;
  view.minV = view.edges.begin()->first.v;
  view.maxV = view.minV;
  for (const auto& edge : view.edges) {
    for (const Projected& point : {edge.first, edge.second}) {
      view.minU = std::min(view.minU, point.u);
      view.maxU = std::max(view.maxU, point.u);
      view.minV = std::min(view.minV, point.v);
      view.maxV = std::max(view.maxV, point.v);
    }
  }
  return true;
}

std::int64_t viewWidth(const ViewWire& view) {
  return std::max<std::int64_t>(1, view.maxU - view.minU + 2 * kMarginUm);
}

std::int64_t viewHeight(const ViewWire& view) {
  return std::max<std::int64_t>(1, view.maxV - view.minV + 2 * kMarginUm);
}

std::string pathData(const ViewWire& view) {
  std::ostringstream out;
  bool first = true;
  for (const auto& edge : view.edges) {
    const std::int64_t x0 = edge.first.u - view.minU + kMarginUm;
    const std::int64_t y0 = view.maxV - edge.first.v + kMarginUm;
    const std::int64_t x1 = edge.second.u - view.minU + kMarginUm;
    const std::int64_t y1 = view.maxV - edge.second.v + kMarginUm;
    if (!first) out << ' ';
    first = false;
    out << 'M' << svgNumber(x0) << ' ' << svgNumber(y0) << " L" << svgNumber(x1) << ' ' << svgNumber(y1);
  }
  return out.str();
}

std::string pathElement(const ViewWire& view) {
  return "<path fill=\"none\" stroke=\"#1c1b19\" stroke-width=\"0.2\" d=\"" + pathData(view) + "\"/>\n";
}

std::string millimetreText(std::int64_t nm) {
  const bool negative = nm < 0;
  const std::uint64_t magnitude =
      negative ? static_cast<std::uint64_t>(-(nm + 1)) + 1 : static_cast<std::uint64_t>(nm);
  const std::uint64_t whole = magnitude / static_cast<std::uint64_t>(units::kNmPerMm);
  std::uint64_t frac = magnitude % static_cast<std::uint64_t>(units::kNmPerMm);
  std::ostringstream out;
  if (negative) out << '-';
  out << whole;
  if (frac != 0) {
    std::string digits(6, '0');
    for (int place = 5; place >= 0; --place) {
      digits[static_cast<std::size_t>(place)] = static_cast<char>('0' + static_cast<int>(frac % 10));
      frac /= 10;
    }
    while (!digits.empty() && digits.back() == '0') digits.pop_back();
    out << '.' << digits;
  }
  out << " mm";
  return out.str();
}

std::string singleSvg(const ViewWire& view) {
  std::ostringstream out;
  out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << svgNumber(viewWidth(view)) << ' '
      << svgNumber(viewHeight(view)) << "\">\n";
  out << "<!-- " << view.comment << " -->\n";
  out << pathElement(view);
  out << "</svg>\n";
  return out.str();
}

bool threeViews(const Component& component, const std::vector<Joint>& joints, const std::vector<Component>& parts,
                ViewWire& front, ViewWire& top, ViewWire& right, std::string& error) {
  const geometry::SolidMesh mesh = geometry::meshFor(component, joints, parts, geometry::MeshQuality::Standard);
  if (!mesh.ok) {
    error = mesh.error.empty() ? "geometry.invalid" : mesh.error;
    return false;
  }
  front.layer = "FRONT";
  front.comment = "Tessellation wire of the cut solid. Front view: horizontal is X, up is Z, Y is dropped. "
                  "Not a dimensioned drawing.";
  top.layer = "TOP";
  top.comment = "Tessellation wire of the cut solid. Top view: horizontal is X, up is Y, Z is dropped. "
                "Not a dimensioned drawing.";
  right.layer = "RIGHT";
  right.comment = "Tessellation wire of the cut solid. Right view: horizontal is Y, up is Z, X is dropped. "
                  "Not a dimensioned drawing.";
  if (!collectEdges(mesh, WirePlane::Front, front, error)) return false;
  if (!collectEdges(mesh, WirePlane::Top, top, error)) return false;
  if (!collectEdges(mesh, WirePlane::Right, right, error)) return false;
  return true;
}

void appendDxfPair(std::ostringstream& out, int code, const std::string& value) { out << code << '\n' << value << '\n'; }

void appendDxfLine(std::ostringstream& out, const ViewWire& view, const Projected& a, const Projected& b) {
  appendDxfPair(out, 0, "LINE");
  appendDxfPair(out, 8, view.layer);
  appendDxfPair(out, 10, svgNumber(a.u));
  appendDxfPair(out, 20, svgNumber(a.v));
  appendDxfPair(out, 30, "0.0");
  appendDxfPair(out, 11, svgNumber(b.u));
  appendDxfPair(out, 21, svgNumber(b.v));
  appendDxfPair(out, 31, "0.0");
}

void appendDxfLayer(std::ostringstream& out, const std::string& name) {
  appendDxfPair(out, 0, "LAYER");
  appendDxfPair(out, 2, name);
  appendDxfPair(out, 70, "0");
  appendDxfPair(out, 62, "7");
  appendDxfPair(out, 6, "CONTINUOUS");
}

std::string dxfDocument(const ViewWire& front, const ViewWire& top, const ViewWire& right) {
  std::ostringstream out;
  appendDxfPair(out, 0, "SECTION");
  appendDxfPair(out, 2, "HEADER");
  appendDxfPair(out, 9, "$ACADVER");
  appendDxfPair(out, 1, "AC1009");
  appendDxfPair(out, 999, "Tessellation wire. Not a dimensioned drawing.");
  appendDxfPair(out, 0, "ENDSEC");
  appendDxfPair(out, 0, "SECTION");
  appendDxfPair(out, 2, "TABLES");
  appendDxfPair(out, 0, "TABLE");
  appendDxfPair(out, 2, "LAYER");
  appendDxfPair(out, 70, "3");
  appendDxfLayer(out, "FRONT");
  appendDxfLayer(out, "TOP");
  appendDxfLayer(out, "RIGHT");
  appendDxfPair(out, 0, "ENDTAB");
  appendDxfPair(out, 0, "ENDSEC");
  appendDxfPair(out, 0, "SECTION");
  appendDxfPair(out, 2, "ENTITIES");
  for (const ViewWire* view : {&front, &top, &right}) {
    for (const auto& edge : view->edges) appendDxfLine(out, *view, edge.first, edge.second);
  }
  appendDxfPair(out, 0, "ENDSEC");
  appendDxfPair(out, 0, "EOF");
  return out.str();
}

void appendPdfEdges(std::ostringstream& out, const ViewWire& view, std::int64_t originU, std::int64_t originV) {
  for (const auto& edge : view.edges) {
    const std::int64_t x0 = edge.first.u - view.minU + originU;
    const std::int64_t y0 = edge.first.v - view.minV + originV;
    const std::int64_t x1 = edge.second.u - view.minU + originU;
    const std::int64_t y1 = edge.second.v - view.minV + originV;
    out << svgNumber(x0) << ' ' << svgNumber(y0) << " m\n";
    out << svgNumber(x1) << ' ' << svgNumber(y1) << " l\nS\n";
  }
}

std::string pdfDocument(const ViewWire& front, const ViewWire& top, const ViewWire& right) {
  std::ostringstream stream;
  stream << "BT /F1 11 Tf 36 800 Td (Tessellation wire. Not a dimensioned drawing.) Tj ET\n";
  stream << "q\n2.834646 0 0 2.834646 36 36 cm\n0.4 w\n";
  const std::int64_t rightOrigin = (front.maxU - front.minU) + kSheetGapUm;
  const std::int64_t topOrigin = (front.maxV - front.minV) + kSheetGapUm;
  appendPdfEdges(stream, front, 0, 0);
  appendPdfEdges(stream, right, rightOrigin, 0);
  appendPdfEdges(stream, top, 0, topOrigin);
  stream << "Q\n";
  const std::string contents = stream.str();

  const std::string objects[] = {
      "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",
      "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n",
      "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R "
      "/Resources << /Font << /F1 5 0 R >> >> >>\nendobj\n",
      "4 0 obj\n<< /Length " + std::to_string(contents.size()) + " >>\nstream\n" + contents + "endstream\nendobj\n",
      "5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n"};

  std::string out = "%PDF-1.4\n";
  std::size_t offsets[5];
  for (int index = 0; index < 5; ++index) {
    offsets[index] = out.size();
    out += objects[index];
  }
  const std::size_t xrefAt = out.size();
  std::ostringstream xref;
  xref << "xref\n0 6\n0000000000 65535 f \n";
  for (std::size_t offset : offsets) xref << std::setw(10) << std::setfill('0') << offset << " 00000 n \n";
  xref << "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n" << xrefAt << "\n%%EOF\n";
  out += xref.str();
  return out;
}


// ---- Hidden-line sheet -----------------------------------------------------

constexpr std::int64_t kDimReserveUm = 14000;
constexpr std::int64_t kDimTextUm = 2200;
constexpr std::int64_t kMinBreakGapUm = 50;
constexpr std::size_t kMaxBreaks = 40;

enum class StrokeKind { Visible, Hidden, Dimension };

struct SheetLine {
  Projected from;
  Projected to;
  StrokeKind kind = StrokeKind::Visible;
};

// at is the middle of the text along its direction when centered. A vertical
// text reads from bottom to top.
struct SheetText {
  Projected at;
  std::string text;
  bool centered = true;
  bool vertical = false;
};

// Everything on the sheet, in micrometres with v up.
struct Sheet {
  std::int64_t width = 0;
  std::int64_t height = 0;
  std::vector<SheetLine> lines;
  std::vector<SheetText> texts;
};

struct AxisPair {
  int u = 0;
  int v = 2;
};

AxisPair axesOf(WirePlane plane) {
  if (plane == WirePlane::Top) return {0, 1};
  if (plane == WirePlane::Right) return {1, 2};
  return {0, 2};
}

std::int64_t axisSize(const Component& component, int axis) {
  if (axis == 0) return component.widthNm;
  if (axis == 1) return component.thicknessNm;
  return component.heightNm;
}

// The interval a cut occupies on one part axis, in nanometres.
void cutInterval(const LocalCut& cut, int axis, std::int64_t& low, std::int64_t& high) {
  const std::int64_t lows[3] = {cut.x0, cut.y0, cut.z0};
  const std::int64_t highs[3] = {cut.x1, cut.y1, cut.z1};
  if (cut.cylinderAxis == 3) {
    low = std::min(lows[axis], highs[axis]) - cut.radiusNm;
    high = std::max(lows[axis], highs[axis]) + cut.radiusNm;
    return;
  }
  if (cut.cylinderAxis >= 0) {
    if (axis == cut.cylinderAxis) {
      low = lows[axis];
      high = highs[axis];
      return;
    }
    const std::int64_t center = (lows[axis] + highs[axis]) / 2;
    low = center - cut.radiusNm;
    high = center + cut.radiusNm;
    return;
  }
  if (cut.prismAxis >= 0) {
    const std::int64_t* values[3] = {cut.px, cut.py, cut.pz};
    if (axis == cut.prismAxis) {
      low = std::min(values[axis][0], cut.prismEnd);
      high = std::max(values[axis][0], cut.prismEnd);
      return;
    }
    low = high = values[axis][0];
    for (int point = 1; point < cut.pointCount; ++point) {
      low = std::min(low, values[axis][point]);
      high = std::max(high, values[axis][point]);
    }
    return;
  }
  low = std::min(lows[axis], highs[axis]);
  high = std::max(lows[axis], highs[axis]);
}

// Positions along one part axis where a cut starts or stops, with the part's two
// ends. Positions closer than 50 um are one position.
std::vector<std::int64_t> featureBreaks(const Component& component, const std::vector<Joint>& joints,
                                        const std::vector<Component>& parts, int axis) {
  const std::int64_t full = axisSize(component, axis);
  std::set<std::int64_t> found = {0, full};
  for (const Joint& joint : joints) {
    if (!jointCutsPart(joint, component.id)) continue;
    const std::string& mateId = joint.partA == component.id ? joint.partB : joint.partA;
    const Component* mate = nullptr;
    for (const Component& candidate : parts) {
      if (candidate.id == mateId) mate = &candidate;
    }
    if (mate == nullptr) continue;
    std::vector<LocalCut> cuts;
    std::string ignored;
    if (!cutsForPart(joint, component, *mate, cuts, ignored)) continue;
    for (const LocalCut& cut : cuts) {
      std::int64_t low = 0;
      std::int64_t high = 0;
      cutInterval(cut, axis, low, high);
      for (const std::int64_t value : {low, high}) {
        if (value > 0 && value < full) found.insert(value);
      }
    }
  }
  std::vector<std::int64_t> merged;
  for (const std::int64_t value : found) {
    if (!merged.empty() && value - merged.back() < kMinBreakGapUm * 1000 && value != full) continue;
    if (!merged.empty() && value == full && full - merged.back() < kMinBreakGapUm * 1000) merged.pop_back();
    merged.push_back(value);
  }
  if (merged.size() > kMaxBreaks) return {0, full};
  return merged;
}

// Millimetres to 0.01 mm, without trailing zeros.
std::string dimensionText(std::int64_t nm) {
  const std::int64_t hundredths = (nm + 5000) / 10000;
  std::string text = std::to_string(hundredths / 100);
  const std::int64_t fraction = hundredths % 100;
  if (fraction == 0) return text;
  text += '.';
  text += static_cast<char>('0' + fraction / 10);
  if (fraction % 10 != 0) text += static_cast<char>('0' + fraction % 10);
  return text;
}

void addDimensionTick(Sheet& sheet, std::int64_t x, std::int64_t y, bool horizontalChain) {
  const std::int64_t reach = 1000;
  if (horizontalChain) sheet.lines.push_back({{x, y - reach}, {x, y + reach}, StrokeKind::Dimension});
  else sheet.lines.push_back({{x - reach, y}, {x + reach, y}, StrokeKind::Dimension});
}

// A chain of dimensions along the horizontal or vertical axis of a view. origin
// is where break 0 sits on that axis, and across is the other coordinate.
void addChain(Sheet& sheet, const std::vector<std::int64_t>& breaks, bool horizontalChain, std::int64_t origin,
              std::int64_t across) {
  if (breaks.size() < 2) return;
  const std::int64_t first = origin + breaks.front() / 1000;
  const std::int64_t last = origin + breaks.back() / 1000;
  if (horizontalChain) sheet.lines.push_back({{first, across}, {last, across}, StrokeKind::Dimension});
  else sheet.lines.push_back({{across, first}, {across, last}, StrokeKind::Dimension});
  for (const std::int64_t value : breaks) {
    addDimensionTick(sheet, horizontalChain ? origin + value / 1000 : across,
                     horizontalChain ? across : origin + value / 1000, horizontalChain);
  }
  for (std::size_t index = 0; index + 1 < breaks.size(); ++index) {
    const std::int64_t a = origin + breaks[index] / 1000;
    const std::int64_t b = origin + breaks[index + 1] / 1000;
    const std::string text = dimensionText(breaks[index + 1] - breaks[index]);
    if (horizontalChain) sheet.texts.push_back({{(a + b) / 2, across + 700}, text, true, false});
    else sheet.texts.push_back({{across - 600, (a + b) / 2}, text, true, true});
  }
}

struct HlrCell {
  std::set<std::pair<Projected, Projected>> visible;
  std::set<std::pair<Projected, Projected>> hidden;
  std::int64_t minU = 0;
  std::int64_t maxU = 0;
  std::int64_t minV = 0;
  std::int64_t maxV = 0;
  std::vector<std::int64_t> uBreaks;
  std::vector<std::int64_t> vBreaks;
  bool dimensioned = false;
  std::int64_t reserve() const { return dimensioned ? kDimReserveUm : 0; }
  std::int64_t cellWidth() const { return maxU - minU + 2 * kMarginUm + reserve(); }
  std::int64_t cellHeight() const { return maxV - minV + 2 * kMarginUm + reserve(); }
};

void addSegment(std::set<std::pair<Projected, Projected>>& into, const geometry::Segment2& segment) {
  Projected a = project(static_cast<float>(segment.u0), static_cast<float>(segment.v0));
  Projected b = project(static_cast<float>(segment.u1), static_cast<float>(segment.v1));
  if (!(a < b) && !(b < a)) return;
  if (b < a) std::swap(a, b);
  into.insert({a, b});
}

bool buildCell(const Component& component, const std::vector<Joint>& joints, const std::vector<Component>& parts,
               WirePlane plane, HlrCell& cell, std::string& error) {
  geometry::ViewDirection direction = geometry::ViewDirection::Front;
  if (plane == WirePlane::Top) direction = geometry::ViewDirection::Top;
  if (plane == WirePlane::Right) direction = geometry::ViewDirection::Right;
  const geometry::ViewLines lines = geometry::hiddenLineView(component, joints, parts, direction);
  if (!lines.ok) {
    error = lines.error.empty() ? "geometry.invalid" : lines.error;
    return false;
  }
  for (const geometry::Segment2& segment : lines.visible) addSegment(cell.visible, segment);
  for (const geometry::Segment2& segment : lines.hidden) addSegment(cell.hidden, segment);
  for (const auto& edge : cell.visible) cell.hidden.erase(edge);
  if (cell.visible.empty()) {
    error = "geometry.empty";
    return false;
  }
  cell.minU = cell.maxU = cell.visible.begin()->first.u;
  cell.minV = cell.maxV = cell.visible.begin()->first.v;
  for (const auto* set : {&cell.visible, &cell.hidden}) {
    for (const auto& edge : *set) {
      for (const Projected& point : {edge.first, edge.second}) {
        cell.minU = std::min(cell.minU, point.u);
        cell.maxU = std::max(cell.maxU, point.u);
        cell.minV = std::min(cell.minV, point.v);
        cell.maxV = std::max(cell.maxV, point.v);
      }
    }
  }
  if (component.type != ComponentType::Cylinder) {
    const AxisPair axes = axesOf(plane);
    cell.uBreaks = featureBreaks(component, joints, parts, axes.u);
    cell.vBreaks = featureBreaks(component, joints, parts, axes.v);
    cell.dimensioned = true;
  }
  return true;
}

// Places one cell on the sheet with its lower left corner at (originX, originY).
void placeCell(Sheet& sheet, const HlrCell& cell, std::int64_t originX, std::int64_t originY) {
  const std::int64_t left = originX + kMarginUm - cell.minU;
  const std::int64_t bottom = originY + cell.reserve() + kMarginUm - cell.minV;
  for (const auto& edge : cell.visible) {
    sheet.lines.push_back({{edge.first.u + left, edge.first.v + bottom},
                           {edge.second.u + left, edge.second.v + bottom}, StrokeKind::Visible});
  }
  for (const auto& edge : cell.hidden) {
    sheet.lines.push_back({{edge.first.u + left, edge.first.v + bottom},
                           {edge.second.u + left, edge.second.v + bottom}, StrokeKind::Hidden});
  }
  if (!cell.dimensioned) return;
  const std::int64_t below = originY + cell.reserve() - 6000 + 2000;
  const std::int64_t beside = originX + cell.cellWidth() - cell.reserve() + 6000;
  addChain(sheet, cell.uBreaks, true, left, below);
  addChain(sheet, cell.vBreaks, false, bottom, beside);
  if (cell.uBreaks.size() > 2) addChain(sheet, {0, cell.uBreaks.back()}, true, left, below - 5000);
  if (cell.vBreaks.size() > 2) addChain(sheet, {0, cell.vBreaks.back()}, false, bottom, beside + 6000);
}

bool buildSheet(const Component& component, const std::vector<Joint>& joints, const std::vector<Component>& parts,
                Sheet& sheet, std::string& error) {
  HlrCell front;
  HlrCell top;
  HlrCell right;
  if (!buildCell(component, joints, parts, WirePlane::Front, front, error) ||
      !buildCell(component, joints, parts, WirePlane::Top, top, error) ||
      !buildCell(component, joints, parts, WirePlane::Right, right, error)) {
    return false;
  }
  sheet.width = front.cellWidth() + kSheetGapUm + right.cellWidth();
  sheet.height = front.cellHeight() + kSheetGapUm + top.cellHeight();
  placeCell(sheet, front, 0, 0);
  placeCell(sheet, right, front.cellWidth() + kSheetGapUm, 0);
  placeCell(sheet, top, 0, front.cellHeight() + kSheetGapUm);
  return true;
}

const char* kHlrNote =
    "Hidden-line removal on the exact solid. Solid lines are visible and dashed lines are hidden. "
    "Dimensions are millimetres: the size and position of every cut along each view axis, chained from the "
    "part corner, and the overall size. Front: X right, Z up. Right: Y right, Z up. Top: X right, Y up.";

std::string sheetPdfText(const Sheet& sheet) {
  std::ostringstream stream;
  stream << "BT /F1 9 Tf 36 800 Td (Hidden-line sheet. Millimetres. Solid = visible, dashed = hidden.) Tj ET\n";
  stream << "q\n2.834646 0 0 2.834646 36 36 cm\n0.35 w\n";
  for (const SheetLine& line : sheet.lines) {
    if (line.kind == StrokeKind::Hidden) stream << "[1.5 1] 0 d\n";
    stream << svgNumber(line.from.u) << ' ' << svgNumber(line.from.v) << " m\n";
    stream << svgNumber(line.to.u) << ' ' << svgNumber(line.to.v) << " l\nS\n";
    if (line.kind == StrokeKind::Hidden) stream << "[] 0 d\n";
  }
  for (const SheetText& text : sheet.texts) {
    const std::int64_t shift = text.centered ? static_cast<std::int64_t>(text.text.size()) * kDimTextUm * 28 / 100 : 0;
    if (text.vertical) {
      stream << "BT /F1 2.2 Tf 0 1 -1 0 " << svgNumber(text.at.u) << ' ' << svgNumber(text.at.v - shift) << " Tm ("
             << text.text << ") Tj ET\n";
    } else {
      stream << "BT /F1 2.2 Tf 1 0 0 1 " << svgNumber(text.at.u - shift) << ' ' << svgNumber(text.at.v) << " Tm ("
             << text.text << ") Tj ET\n";
    }
  }
  stream << "Q\n";
  return stream.str();
}

std::string pdfFromContents(const std::string& contents) {
  const std::string objects[] = {
      "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n",
      "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n",
      "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 595 842] /Contents 4 0 R "
      "/Resources << /Font << /F1 5 0 R >> >> >>\nendobj\n",
      "4 0 obj\n<< /Length " + std::to_string(contents.size()) + " >>\nstream\n" + contents + "endstream\nendobj\n",
      "5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n"};
  std::string out = "%PDF-1.4\n";
  std::size_t offsets[5];
  for (int index = 0; index < 5; ++index) {
    offsets[index] = out.size();
    out += objects[index];
  }
  const std::size_t xrefAt = out.size();
  std::ostringstream xref;
  xref << "xref\n0 6\n0000000000 65535 f \n";
  for (std::size_t offset : offsets) xref << std::setw(10) << std::setfill('0') << offset << " 00000 n \n";
  xref << "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n" << xrefAt << "\n%%EOF\n";
  out += xref.str();
  return out;
}

const char* layerOf(StrokeKind kind) {
  if (kind == StrokeKind::Hidden) return "HIDDEN";
  if (kind == StrokeKind::Dimension) return "DIMENSIONS";
  return "VISIBLE";
}

std::string sheetDxfText(const Sheet& sheet) {
  std::ostringstream out;
  appendDxfPair(out, 0, "SECTION");
  appendDxfPair(out, 2, "HEADER");
  appendDxfPair(out, 9, "$ACADVER");
  appendDxfPair(out, 1, "AC1009");
  appendDxfPair(out, 999, "Hidden-line removal on the exact solid. Millimetres.");
  appendDxfPair(out, 0, "ENDSEC");
  appendDxfPair(out, 0, "SECTION");
  appendDxfPair(out, 2, "TABLES");
  appendDxfPair(out, 0, "TABLE");
  appendDxfPair(out, 2, "LTYPE");
  appendDxfPair(out, 70, "2");
  appendDxfPair(out, 0, "LTYPE");
  appendDxfPair(out, 2, "CONTINUOUS");
  appendDxfPair(out, 70, "0");
  appendDxfPair(out, 3, "Solid line");
  appendDxfPair(out, 72, "65");
  appendDxfPair(out, 73, "0");
  appendDxfPair(out, 40, "0.0");
  appendDxfPair(out, 0, "LTYPE");
  appendDxfPair(out, 2, "HIDDEN");
  appendDxfPair(out, 70, "0");
  appendDxfPair(out, 3, "Hidden");
  appendDxfPair(out, 72, "65");
  appendDxfPair(out, 73, "2");
  appendDxfPair(out, 40, "2.5");
  appendDxfPair(out, 49, "1.5");
  appendDxfPair(out, 49, "-1.0");
  appendDxfPair(out, 0, "ENDTAB");
  appendDxfPair(out, 0, "TABLE");
  appendDxfPair(out, 2, "LAYER");
  appendDxfPair(out, 70, "3");
  for (const char* name : {"VISIBLE", "HIDDEN", "DIMENSIONS"}) {
    appendDxfPair(out, 0, "LAYER");
    appendDxfPair(out, 2, name);
    appendDxfPair(out, 70, "0");
    appendDxfPair(out, 62, "7");
    appendDxfPair(out, 6, std::string(name) == "HIDDEN" ? "HIDDEN" : "CONTINUOUS");
  }
  appendDxfPair(out, 0, "ENDTAB");
  appendDxfPair(out, 0, "ENDSEC");
  appendDxfPair(out, 0, "SECTION");
  appendDxfPair(out, 2, "ENTITIES");
  for (const SheetLine& line : sheet.lines) {
    appendDxfPair(out, 0, "LINE");
    appendDxfPair(out, 8, layerOf(line.kind));
    appendDxfPair(out, 10, svgNumber(line.from.u));
    appendDxfPair(out, 20, svgNumber(line.from.v));
    appendDxfPair(out, 30, "0.0");
    appendDxfPair(out, 11, svgNumber(line.to.u));
    appendDxfPair(out, 21, svgNumber(line.to.v));
    appendDxfPair(out, 31, "0.0");
  }
  for (const SheetText& text : sheet.texts) {
    const std::int64_t shift = text.centered ? static_cast<std::int64_t>(text.text.size()) * kDimTextUm * 28 / 100 : 0;
    appendDxfPair(out, 0, "TEXT");
    appendDxfPair(out, 8, "DIMENSIONS");
    appendDxfPair(out, 10, svgNumber(text.vertical ? text.at.u : text.at.u - shift));
    appendDxfPair(out, 20, svgNumber(text.vertical ? text.at.v - shift : text.at.v));
    appendDxfPair(out, 30, "0.0");
    appendDxfPair(out, 40, svgNumber(kDimTextUm));
    appendDxfPair(out, 1, text.text);
    if (text.vertical) appendDxfPair(out, 50, "90.0");
  }
  appendDxfPair(out, 0, "ENDSEC");
  appendDxfPair(out, 0, "EOF");
  return out.str();
}

std::string sheetSvgText(const Sheet& sheet) {
  const auto x = [](const Projected& point) { return svgNumber(point.u); };
  const auto y = [&sheet](const Projected& point) { return svgNumber(sheet.height - point.v); };
  std::ostringstream out;
  out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << svgNumber(sheet.width) << ' '
      << svgNumber(sheet.height) << "\">\n";
  out << "<!-- " << kHlrNote << " -->\n";
  const struct {
    StrokeKind kind;
    const char* id;
    const char* style;
  } groups[] = {
      {StrokeKind::Visible, "visible", "stroke=\"#1c1b19\" stroke-width=\"0.35\""},
      {StrokeKind::Hidden, "hidden", "stroke=\"#6b6a66\" stroke-width=\"0.2\" stroke-dasharray=\"1.5 1\""},
      {StrokeKind::Dimension, "dimensions", "stroke=\"#1c1b19\" stroke-width=\"0.15\""},
  };
  for (const auto& group : groups) {
    out << "<g id=\"" << group.id << "\" fill=\"none\" " << group.style << ">\n";
    for (const SheetLine& line : sheet.lines) {
      if (line.kind != group.kind) continue;
      out << "<line x1=\"" << x(line.from) << "\" y1=\"" << y(line.from) << "\" x2=\"" << x(line.to) << "\" y2=\""
          << y(line.to) << "\"/>\n";
    }
    out << "</g>\n";
  }
  out << "<g id=\"dimension-text\" font-family=\"Helvetica, Arial, sans-serif\" font-size=\"2.2\" fill=\"#1c1b19\">\n";
  for (const SheetText& text : sheet.texts) {
    out << "<text x=\"" << x(text.at) << "\" y=\"" << y(text.at) << "\"";
    if (text.vertical) out << " transform=\"rotate(-90 " << x(text.at) << ' ' << y(text.at) << ")\"";
    out << (text.centered ? " text-anchor=\"middle\"" : "") << ">" << text.text << "</text>\n";
  }
  out << "</g>\n</svg>\n";
  return out.str();
}

}  // namespace

bool cutList(const std::vector<Component>& components, std::int64_t allowanceNm, std::vector<CutPiece>& pieces,
             std::string& error) {
  pieces.clear();
  if (allowanceNm < 0) {
    error = "parameter.range";
    return false;
  }
  pieces.reserve(components.size());
  for (const Component& component : components) {
    CutPiece piece;
    piece.id = component.id;
    piece.name = component.name;
    piece.materialId = component.materialId;
    piece.role = component.role;
    if (!finishedSize(component, piece, error)) {
      pieces.clear();
      return false;
    }
    if (!addAllowance(piece.finishedWidthNm, allowanceNm, piece.roughWidthNm, error) ||
        !addAllowance(piece.finishedThicknessNm, allowanceNm, piece.roughThicknessNm, error) ||
        !addAllowance(piece.finishedHeightNm, allowanceNm, piece.roughHeightNm, error)) {
      pieces.clear();
      return false;
    }
    pieces.push_back(std::move(piece));
  }
  return true;
}

std::vector<BomLine> billOfMaterials(const std::vector<CutPiece>& pieces) {
  std::vector<BomLine> lines;
  std::map<std::string, std::size_t> index;
  for (const CutPiece& piece : pieces) {
    const std::string key = piece.materialId + "\n" + piece.role + "\n" + piece.shape + "\n" +
                            std::to_string(piece.finishedWidthNm) + "\n" + std::to_string(piece.finishedThicknessNm) +
                            "\n" + std::to_string(piece.finishedHeightNm);
    const auto found = index.find(key);
    if (found != index.end()) {
      ++lines[found->second].quantity;
      continue;
    }
    BomLine line;
    line.materialId = piece.materialId;
    line.role = piece.role;
    line.shape = piece.shape;
    line.finishedWidthNm = piece.finishedWidthNm;
    line.finishedThicknessNm = piece.finishedThicknessNm;
    line.finishedHeightNm = piece.finishedHeightNm;
    line.quantity = 1;
    index.emplace(key, lines.size());
    lines.push_back(std::move(line));
  }
  return lines;
}

std::string cutListCsv(const std::vector<CutPiece>& pieces) {
  std::ostringstream out;
  out << "name,material,role,shape,finished_width_mm,finished_thickness_mm,finished_height_mm,"
         "rough_width_mm,rough_thickness_mm,rough_height_mm\n";
  for (const CutPiece& piece : pieces) {
    out << csvField(piece.name) << ',' << csvField(piece.materialId) << ',' << csvField(piece.role) << ','
        << piece.shape << ',' << millimetresText(piece.finishedWidthNm) << ','
        << millimetresText(piece.finishedThicknessNm) << ',' << millimetresText(piece.finishedHeightNm) << ','
        << millimetresText(piece.roughWidthNm) << ',' << millimetresText(piece.roughThicknessNm) << ','
        << millimetresText(piece.roughHeightNm) << '\n';
  }
  return out.str();
}

std::string cutListJson(std::int64_t allowanceNm, const std::vector<CutPiece>& pieces,
                        const std::vector<BomLine>& lines) {
  nlohmann::json cuts = nlohmann::json::array();
  for (const CutPiece& piece : pieces) {
    cuts.push_back({{"id", piece.id},
                    {"name", piece.name},
                    {"material", piece.materialId},
                    {"role", piece.role},
                    {"shape", piece.shape},
                    {"finishedWidthNm", piece.finishedWidthNm},
                    {"finishedThicknessNm", piece.finishedThicknessNm},
                    {"finishedHeightNm", piece.finishedHeightNm},
                    {"roughWidthNm", piece.roughWidthNm},
                    {"roughThicknessNm", piece.roughThicknessNm},
                    {"roughHeightNm", piece.roughHeightNm}});
  }
  nlohmann::json bom = nlohmann::json::array();
  for (const BomLine& line : lines) {
    bom.push_back({{"material", line.materialId},
                   {"role", line.role},
                   {"shape", line.shape},
                   {"finishedWidthNm", line.finishedWidthNm},
                   {"finishedThicknessNm", line.finishedThicknessNm},
                   {"finishedHeightNm", line.finishedHeightNm},
                   {"quantity", line.quantity}});
  }
  const nlohmann::json root = {
      {"allowanceNm", allowanceNm},
      {"note", "Lengths are nanometres. Rough size equals finished size when the allowance is zero."},
      {"cutList", std::move(cuts)},
      {"bom", std::move(bom)}};
  return root.dump(2);
}

std::string frontViewSvg(const Component& component, const std::vector<Joint>& joints,
                         const std::vector<Component>& parts, std::string& error) {
  ViewWire front;
  ViewWire top;
  ViewWire right;
  if (!threeViews(component, joints, parts, front, top, right, error)) return {};
  return singleSvg(front);
}

std::string dimensionedFrontSvg(const Component& component, const std::vector<Joint>& joints,
                                const std::vector<Component>& parts, std::string& error) {
  if (component.type == ComponentType::Cylinder || component.widthNm <= 0 || component.heightNm <= 0) {
    error = "dimension.not_applicable";
    return {};
  }
  ViewWire front;
  ViewWire top;
  ViewWire right;
  if (!threeViews(component, joints, parts, front, top, right, error)) return {};
  const std::int64_t widthUm = component.widthNm / 1000;
  const std::int64_t heightUm = component.heightNm / 1000;
  const std::int64_t x1 = -front.minU + kMarginUm;
  const std::int64_t x2 = widthUm - front.minU + kMarginUm;
  const std::int64_t yTop = front.maxV - heightUm + kMarginUm;
  const std::int64_t yBottom = front.maxV + kMarginUm;
  constexpr std::int64_t kGapUm = 5000;
  constexpr std::int64_t kSideUm = 24000;
  constexpr std::int64_t kBelowUm = 8000;
  const std::int64_t widthLineY = viewHeight(front) + kGapUm;
  const std::int64_t heightLineX = viewWidth(front) + kGapUm;
  const std::int64_t sheetW = std::max(viewWidth(front), x2) + kSideUm;
  const std::int64_t sheetH = std::max(viewHeight(front), yBottom) + kBelowUm;
  std::ostringstream out;
  out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << svgNumber(sheetW) << ' ' << svgNumber(sheetH)
      << "\">\n";
  out << "<!-- Overall width and height from the model nanometres. The outline is a tessellation. "
         "Not hidden-line removal. -->\n";
  out << pathElement(front);
  out << "<g id=\"dimensions\">\n";
  out << "<line id=\"width-line\" x1=\"" << svgNumber(x1) << "\" y1=\"" << svgNumber(widthLineY) << "\" x2=\""
      << svgNumber(x2) << "\" y2=\"" << svgNumber(widthLineY) << "\" fill=\"none\" stroke=\"#1c1b19\" stroke-width=\"0.2\"/>\n";
  out << "<text id=\"width-text\" x=\"" << svgNumber((x1 + x2) / 2) << "\" y=\"" << svgNumber(widthLineY - 800)
      << "\" font-size=\"3\" text-anchor=\"middle\">" << millimetreText(component.widthNm) << "</text>\n";
  out << "<line id=\"height-line\" x1=\"" << svgNumber(heightLineX) << "\" y1=\"" << svgNumber(yTop) << "\" x2=\""
      << svgNumber(heightLineX) << "\" y2=\"" << svgNumber(yBottom)
      << "\" fill=\"none\" stroke=\"#1c1b19\" stroke-width=\"0.2\"/>\n";
  out << "<text id=\"height-text\" x=\"" << svgNumber(heightLineX + 1200) << "\" y=\"" << svgNumber((yTop + yBottom) / 2)
      << "\" font-size=\"3\">" << millimetreText(component.heightNm) << "</text>\n";
  out << "</g>\n</svg>\n";
  return out.str();
}

std::string topViewSvg(const Component& component, const std::vector<Joint>& joints,
                       const std::vector<Component>& parts, std::string& error) {
  ViewWire front;
  ViewWire top;
  ViewWire right;
  if (!threeViews(component, joints, parts, front, top, right, error)) return {};
  return singleSvg(top);
}

std::string rightViewSvg(const Component& component, const std::vector<Joint>& joints,
                         const std::vector<Component>& parts, std::string& error) {
  ViewWire front;
  ViewWire top;
  ViewWire right;
  if (!threeViews(component, joints, parts, front, top, right, error)) return {};
  return singleSvg(right);
}

std::string sheetSvg(const Component& component, const std::vector<Joint>& joints, const std::vector<Component>& parts,
                     std::string& error) {
  ViewWire front;
  ViewWire top;
  ViewWire right;
  if (!threeViews(component, joints, parts, front, top, right, error)) return {};
  const std::int64_t width = viewWidth(front) + kSheetGapUm + viewWidth(right);
  const std::int64_t height = viewHeight(front) + kSheetGapUm + viewHeight(top);
  std::ostringstream out;
  out << "<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 " << svgNumber(width) << ' ' << svgNumber(height)
      << "\">\n";
  out << "<!-- Tessellation wire of the cut solid. Front, top, and right views. Not a dimensioned drawing. -->\n";
  out << "<g id=\"front\">\n" << pathElement(front) << "</g>\n";
  out << "<g id=\"right\" transform=\"translate(" << svgNumber(viewWidth(front) + kSheetGapUm) << " 0)\">\n"
      << pathElement(right) << "</g>\n";
  out << "<g id=\"top\" transform=\"translate(0 " << svgNumber(viewHeight(front) + kSheetGapUm) << ")\">\n"
      << pathElement(top) << "</g>\n";
  out << "</svg>\n";
  return out.str();
}

std::string wireDxf(const Component& component, const std::vector<Joint>& joints, const std::vector<Component>& parts,
                    std::string& error) {
  ViewWire front;
  ViewWire top;
  ViewWire right;
  if (!threeViews(component, joints, parts, front, top, right, error)) return {};
  return dxfDocument(front, top, right);
}

std::string wirePdf(const Component& component, const std::vector<Joint>& joints, const std::vector<Component>& parts,
                    std::string& error) {
  ViewWire front;
  ViewWire top;
  ViewWire right;
  if (!threeViews(component, joints, parts, front, top, right, error)) return {};
  return pdfDocument(front, top, right);
}

std::string hiddenLineSheetSvg(const Component& component, const std::vector<Joint>& joints,
                               const std::vector<Component>& parts, std::string& error) {
  Sheet sheet;
  if (!buildSheet(component, joints, parts, sheet, error)) return {};
  return sheetSvgText(sheet);
}

std::string hiddenLineDxf(const Component& component, const std::vector<Joint>& joints,
                          const std::vector<Component>& parts, std::string& error) {
  Sheet sheet;
  if (!buildSheet(component, joints, parts, sheet, error)) return {};
  return sheetDxfText(sheet);
}

std::string hiddenLinePdf(const Component& component, const std::vector<Joint>& joints,
                          const std::vector<Component>& parts, std::string& error) {
  Sheet sheet;
  if (!buildSheet(component, joints, parts, sheet, error)) return {};
  return pdfFromContents(sheetPdfText(sheet));
}

}  // namespace timberlab::manufacturing
