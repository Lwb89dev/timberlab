#include "timberlab/core/component.hpp"

#include "timberlab/units/units.hpp"

namespace timberlab {

const char* componentTypeKey(ComponentType type) {
  switch (type) {
    case ComponentType::Board:
      return "board";
    case ComponentType::Box:
      return "box";
    case ComponentType::Cylinder:
      return "cylinder";
  }
  return "board";
}

bool componentTypeFromKey(std::string_view key, ComponentType& out) {
  if (key == "board") {
    out = ComponentType::Board;
    return true;
  }
  if (key == "box") {
    out = ComponentType::Box;
    return true;
  }
  if (key == "cylinder") {
    out = ComponentType::Cylinder;
    return true;
  }
  return false;
}

LocalBox localBox(const Component& component) {
  LocalBox box;
  if (component.type == ComponentType::Cylinder) {
    const double radius = units::toMillimetres(component.radiusNm);
    const double height = units::toMillimetres(component.heightNm);
    box.originXmm = -radius;
    box.originYmm = -radius;
    box.sizeXmm = radius * 2.0;
    box.sizeYmm = radius * 2.0;
    box.sizeZmm = height;
    return box;
  }
  box.sizeXmm = units::toMillimetres(component.widthNm);
  box.sizeYmm = units::toMillimetres(component.thicknessNm);
  box.sizeZmm = units::toMillimetres(component.heightNm);
  return box;
}

}  // namespace timberlab
