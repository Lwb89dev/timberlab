#pragma once

#include "timberlab/core/component.hpp"
#include "timberlab/core/joint.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace timberlab::manufacturing {

// One row of the cut list. Finished sizes are the model. Rough sizes add the
// same allowance on each reported edge. A cylinder is reported as a diameter
// (width and thickness are both 2 × radius) and a height. Lengths are nanometres.
struct CutPiece {
  std::string id;
  std::string name;
  std::string materialId;
  std::string role;
  std::string shape;
  std::int64_t finishedWidthNm = 0;
  std::int64_t finishedThicknessNm = 0;
  std::int64_t finishedHeightNm = 0;
  std::int64_t roughWidthNm = 0;
  std::int64_t roughThicknessNm = 0;
  std::int64_t roughHeightNm = 0;
};

// Pieces that share a material, a role, and a finished size.
struct BomLine {
  std::string materialId;
  std::string role;
  std::string shape;
  std::int64_t finishedWidthNm = 0;
  std::int64_t finishedThicknessNm = 0;
  std::int64_t finishedHeightNm = 0;
  int quantity = 0;
};

bool cutList(const std::vector<Component>& components, std::int64_t allowanceNm, std::vector<CutPiece>& pieces,
             std::string& error);
std::vector<BomLine> billOfMaterials(const std::vector<CutPiece>& pieces);

std::string cutListCsv(const std::vector<CutPiece>& pieces);
std::string cutListJson(std::int64_t allowanceNm, const std::vector<CutPiece>& pieces,
                        const std::vector<BomLine>& lines);

// Orthographic wires of the cut solid. Each one is the tessellation, with a 1 mm
// margin. Front drops Y (X horizontal, Z up). Top drops Z (X horizontal, Y up).
// Right drops X (Y horizontal, Z up). The sheet places front, then right, then
// top, with a 10 mm gap. DXF is R12 LINE entities in millimetres, one layer per
// view. PDF is the same edges. Those wires are not dimensioned drawings.
// dimensionedFrontSvg adds the model's overall width and height, in millimetres,
// on the front wire. The numbers come from the part, not from the tessellation.
// It is not hidden-line removal. A cylinder has no width and height in that sense.
std::string frontViewSvg(const Component& component, const std::vector<Joint>& joints,
                         const std::vector<Component>& parts, std::string& error);
std::string dimensionedFrontSvg(const Component& component, const std::vector<Joint>& joints,
                                const std::vector<Component>& parts, std::string& error);
std::string topViewSvg(const Component& component, const std::vector<Joint>& joints,
                       const std::vector<Component>& parts, std::string& error);
std::string rightViewSvg(const Component& component, const std::vector<Joint>& joints,
                         const std::vector<Component>& parts, std::string& error);
std::string sheetSvg(const Component& component, const std::vector<Joint>& joints,
                     const std::vector<Component>& parts, std::string& error);
std::string wireDxf(const Component& component, const std::vector<Joint>& joints,
                    const std::vector<Component>& parts, std::string& error);
std::string wirePdf(const Component& component, const std::vector<Joint>& joints,
                    const std::vector<Component>& parts, std::string& error);

}  // namespace timberlab::manufacturing
