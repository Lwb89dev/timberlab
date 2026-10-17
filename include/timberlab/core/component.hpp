#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace timberlab {

// Local axes, right-handed, Z up (same convention as OCCT and SketchUp):
//   X = width
//   Y = thickness
//   Z = height
// A board stands on Z = 0 with its back-left corner at the local origin.
// A cylinder stands on Z = 0, centered on the local Z axis.
enum class ComponentType { Board, Box, Cylinder };

struct Placement {
  std::int64_t xNm = 0;
  std::int64_t yNm = 0;
  std::int64_t zNm = 0;
  double rxDeg = 0;
  double ryDeg = 0;
  double rzDeg = 0;
};

struct Component {
  std::string id;
  ComponentType type = ComponentType::Board;
  std::string name;
  std::string materialId = "european-oak";
  Placement placement;
  std::int64_t widthNm = 0;
  std::int64_t heightNm = 0;
  std::int64_t thicknessNm = 0;
  std::int64_t radiusNm = 0;
  bool visible = true;
  bool locked = false;
  std::string parentId;
  std::string notes;
};

const char* componentTypeKey(ComponentType type);
bool componentTypeFromKey(std::string_view key, ComponentType& out);

// Local axis-aligned box before placement. Origin is the minimum corner.
struct LocalBox {
  double originXmm = 0;
  double originYmm = 0;
  double originZmm = 0;
  double sizeXmm = 0;
  double sizeYmm = 0;
  double sizeZmm = 0;
};

LocalBox localBox(const Component& component);

}  // namespace timberlab
