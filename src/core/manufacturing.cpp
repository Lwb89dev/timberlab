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

}  // namespace timberlab::manufacturing
