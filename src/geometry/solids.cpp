#include "timberlab/geometry/solids.hpp"

#include "timberlab/units/units.hpp"

#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_State.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>
#include <gp_Vec.hxx>

#include <cmath>
#include <utility>
#include <vector>

namespace timberlab::geometry {
namespace {

void addTriangle(SolidMesh& mesh, const gp_Pnt& a, const gp_Pnt& b, const gp_Pnt& c) {
  gp_Vec ab(a, b);
  gp_Vec ac(a, c);
  gp_Vec normal = ab.Crossed(ac);
  if (normal.SquareMagnitude() < 1e-18) return;
  normal.Normalize();
  const gp_Pnt points[3] = {a, b, c};
  const auto base = static_cast<std::uint32_t>(mesh.interleaved.size() / 6);
  for (const gp_Pnt& point : points) {
    mesh.interleaved.push_back(static_cast<float>(point.X()));
    mesh.interleaved.push_back(static_cast<float>(point.Y()));
    mesh.interleaved.push_back(static_cast<float>(point.Z()));
    mesh.interleaved.push_back(static_cast<float>(normal.X()));
    mesh.interleaved.push_back(static_cast<float>(normal.Y()));
    mesh.interleaved.push_back(static_cast<float>(normal.Z()));
  }
  mesh.indices.push_back(base);
  mesh.indices.push_back(base + 1);
  mesh.indices.push_back(base + 2);
}

SolidMesh meshShape(const TopoDS_Shape& shape, double deflection) {
  SolidMesh mesh;
  BRepCheck_Analyzer checker(shape);
  if (!checker.IsValid()) {
    mesh.error = "geometry.invalid";
    return mesh;
  }

  GProp_GProps props;
  BRepGProp::VolumeProperties(shape, props);
  mesh.volumeMm3 = props.Mass();
  if (!(mesh.volumeMm3 > 0.0)) {
    mesh.error = "geometry.empty";
    return mesh;
  }

  Bnd_Box bounds;
  BRepBndLib::Add(shape, bounds);
  if (bounds.IsVoid()) {
    mesh.error = "geometry.empty";
    return mesh;
  }
  Standard_Real x0 = 0, y0 = 0, z0 = 0, x1 = 0, y1 = 0, z1 = 0;
  bounds.Get(x0, y0, z0, x1, y1, z1);
  mesh.bboxMin[0] = x0;
  mesh.bboxMin[1] = y0;
  mesh.bboxMin[2] = z0;
  mesh.bboxMax[0] = x1;
  mesh.bboxMax[1] = y1;
  mesh.bboxMax[2] = z1;

  BRepMesh_IncrementalMesh mesher(shape, deflection, Standard_False, 0.4, Standard_False);
  for (TopExp_Explorer faces(shape, TopAbs_FACE); faces.More(); faces.Next()) {
    const TopoDS_Face face = TopoDS::Face(faces.Current());
    TopLoc_Location location;
    const Handle(Poly_Triangulation) triangulation = BRep_Tool::Triangulation(face, location);
    if (triangulation.IsNull()) continue;
    const gp_Trsf transform = location.Transformation();
    const bool reversed = face.Orientation() == TopAbs_REVERSED;
    const int nodeCount = triangulation->NbNodes();
    for (int index = 1; index <= triangulation->NbTriangles(); ++index) {
      int n1 = 0, n2 = 0, n3 = 0;
      triangulation->Triangle(index).Get(n1, n2, n3);
      if (n1 < 1 || n2 < 1 || n3 < 1 || n1 > nodeCount || n2 > nodeCount || n3 > nodeCount) continue;
      if (reversed) std::swap(n2, n3);
      const gp_Pnt a = triangulation->Node(n1).Transformed(transform);
      const gp_Pnt b = triangulation->Node(n2).Transformed(transform);
      const gp_Pnt c = triangulation->Node(n3).Transformed(transform);
      addTriangle(mesh, a, b, c);
    }
  }
  if (mesh.indices.empty()) {
    mesh.error = "geometry.empty";
    return mesh;
  }
  mesh.ok = true;
  return mesh;
}

constexpr std::int64_t kBooleanOverlapNm = 200000;

const Component* findPart(const std::vector<Component>& parts, const std::string& id) {
  for (const Component& part : parts) {
    if (part.id == id) return &part;
  }
  return nullptr;
}

void expandBoundary(LocalCut& cut, const Component& part) {
  auto open = [](std::int64_t& low, std::int64_t& high, std::int64_t extent) {
    if (low <= 0) low -= kBooleanOverlapNm;
    if (high >= extent) high += kBooleanOverlapNm;
  };
  open(cut.x0, cut.x1, part.widthNm);
  open(cut.y0, cut.y1, part.thicknessNm);
  open(cut.z0, cut.z1, part.heightNm);
}

bool makeBox(const LocalCut& cut, TopoDS_Shape& shape, std::string& error) {
  double x0 = units::toMillimetres(cut.x0);
  double y0 = units::toMillimetres(cut.y0);
  double z0 = units::toMillimetres(cut.z0);
  double x1 = units::toMillimetres(cut.x1);
  double y1 = units::toMillimetres(cut.y1);
  double z1 = units::toMillimetres(cut.z1);
  if (x1 < x0) std::swap(x0, x1);
  if (y1 < y0) std::swap(y0, y1);
  if (z1 < z0) std::swap(z0, z1);
  if (!(x1 > x0) || !(y1 > y0) || !(z1 > z0)) {
    error = "joint.no_effect";
    return false;
  }
  BRepPrimAPI_MakeBox box(gp_Pnt(x0, y0, z0), gp_Pnt(x1, y1, z1));
  box.Build();
  if (!box.IsDone()) {
    error = "geometry.kernel";
    return false;
  }
  shape = box.Shape();
  return true;
}

std::int64_t profileCoord(const LocalCut& cut, int point, int axis) {
  if (axis == 0) return cut.px[point];
  if (axis == 1) return cut.py[point];
  return cut.pz[point];
}

bool fuseTools(const TopoDS_Shape& left, const TopoDS_Shape& right, TopoDS_Shape& out, std::string& error);

bool profileOpen(const LocalCut& cut) {
  if (cut.pointCount < 3 || cut.prismAxis < 0 || cut.prismAxis > 2) return false;
  const int u = cut.prismAxis == 0 ? 1 : 0;
  const int v = cut.prismAxis == 2 ? 1 : 2;
  const double ux = static_cast<double>(profileCoord(cut, 1, u) - profileCoord(cut, 0, u));
  const double uy = static_cast<double>(profileCoord(cut, 1, v) - profileCoord(cut, 0, v));
  const double vx = static_cast<double>(profileCoord(cut, 2, u) - profileCoord(cut, 0, u));
  const double vy = static_cast<double>(profileCoord(cut, 2, v) - profileCoord(cut, 0, v));
  return std::abs(ux * vy - uy * vx) > 1.0;
}

bool makePrism(LocalCut cut, const Component& part, TopoDS_Shape& shape, std::string& error) {
  if (!profileOpen(cut)) {
    error = "joint.no_effect";
    return false;
  }
  const int axis = cut.prismAxis;
  const std::int64_t extents[3] = {part.widthNm, part.thicknessNm, part.heightNm};
  std::int64_t start = profileCoord(cut, 0, axis);
  std::int64_t end = cut.prismEnd;
  if (start <= 0) start -= kBooleanOverlapNm;
  if (end >= extents[axis]) end += kBooleanOverlapNm;
  const std::int64_t shift = start - profileCoord(cut, 0, axis);
  BRepBuilderAPI_MakePolygon polygon;
  for (int i = 0; i < cut.pointCount; ++i) {
    double coords[3] = {units::toMillimetres(cut.px[i]), units::toMillimetres(cut.py[i]),
                        units::toMillimetres(cut.pz[i])};
    coords[axis] = units::toMillimetres(profileCoord(cut, i, axis) + shift);
    polygon.Add(gp_Pnt(coords[0], coords[1], coords[2]));
  }
  polygon.Close();
  if (!polygon.IsDone()) {
    error = "geometry.kernel";
    return false;
  }
  BRepBuilderAPI_MakeFace face(polygon.Wire());
  if (!face.IsDone()) {
    error = "geometry.kernel";
    return false;
  }
  const double delta = units::toMillimetres(end - start);
  gp_Vec direction;
  if (axis == 0) direction = gp_Vec(delta, 0, 0);
  else if (axis == 1) direction = gp_Vec(0, delta, 0);
  else direction = gp_Vec(0, 0, delta);
  BRepPrimAPI_MakePrism prism(face.Shape(), direction);
  prism.Build();
  if (!prism.IsDone()) {
    error = "geometry.kernel";
    return false;
  }
  shape = prism.Shape();
  for (int i = 0; i < cut.pointCount; ++i) {
    const int next = (i + 1) % cut.pointCount;
    for (int side = 0; side < 3; ++side) {
      if (side == axis) continue;
      const std::int64_t a = profileCoord(cut, i, side);
      const std::int64_t b = profileCoord(cut, next, side);
      if (a != b || (a > 0 && a < extents[side])) continue;
      LocalCut slab;
      slab.x0 = cut.px[i];
      slab.y0 = cut.py[i];
      slab.z0 = cut.pz[i];
      slab.x1 = cut.px[next];
      slab.y1 = cut.py[next];
      slab.z1 = cut.pz[next];
      if (axis == 0) {
        slab.x0 = start;
        slab.x1 = end;
      } else if (axis == 1) {
        slab.y0 = start;
        slab.y1 = end;
      } else {
        slab.z0 = start;
        slab.z1 = end;
      }
      if (side == 0) {
        slab.x0 = a <= 0 ? a - kBooleanOverlapNm : a;
        slab.x1 = a <= 0 ? a : a + kBooleanOverlapNm;
      } else if (side == 1) {
        slab.y0 = a <= 0 ? a - kBooleanOverlapNm : a;
        slab.y1 = a <= 0 ? a : a + kBooleanOverlapNm;
      } else {
        slab.z0 = a <= 0 ? a - kBooleanOverlapNm : a;
        slab.z1 = a <= 0 ? a : a + kBooleanOverlapNm;
      }
      TopoDS_Shape extra;
      if (!makeBox(slab, extra, error)) return false;
      TopoDS_Shape fused;
      if (!fuseTools(shape, extra, fused, error)) return false;
      shape = fused;
    }
  }
  return true;
}

double volumeOf(const TopoDS_Shape& shape) {
  GProp_GProps props;
  BRepGProp::VolumeProperties(shape, props);
  return props.Mass();
}

TopoDS_Shape singleSolid(const TopoDS_Shape& shape) {
  if (shape.ShapeType() == TopAbs_SOLID) return shape;
  TopoDS_Shape found;
  int count = 0;
  for (TopExp_Explorer explorer(shape, TopAbs_SOLID); explorer.More(); explorer.Next()) {
    found = explorer.Current();
    ++count;
  }
  if (count == 1) return found;
  return shape;
}

bool fuseTools(const TopoDS_Shape& left, const TopoDS_Shape& right, TopoDS_Shape& out, std::string& error) {
  TopTools_ListOfShape arguments;
  arguments.Append(left);
  TopTools_ListOfShape tools;
  tools.Append(right);
  BRepAlgoAPI_Fuse operation;
  operation.SetArguments(arguments);
  operation.SetTools(tools);
  operation.SetRunParallel(Standard_False);
  operation.Build();
  if (!operation.IsDone()) {
    error = "geometry.kernel";
    return false;
  }
  out = operation.Shape();
  return true;
}

bool subtractTool(TopoDS_Shape& current, const TopoDS_Shape& tool, std::string& error) {
  TopTools_ListOfShape arguments;
  arguments.Append(current);
  TopTools_ListOfShape tools;
  tools.Append(tool);
  BRepAlgoAPI_Cut operation;
  operation.SetArguments(arguments);
  operation.SetTools(tools);
  operation.SetRunParallel(Standard_False);
  operation.Build();
  if (!operation.IsDone()) {
    error = "joint.no_effect";
    return false;
  }
  current = singleSolid(operation.Shape());
  return true;
}

bool applyCuts(TopoDS_Shape& shape, const Component& part, const std::vector<LocalCut>& cuts, std::string& error) {
  const double before = volumeOf(shape);
  TopoDS_Shape tool;
  bool haveTool = false;
  for (LocalCut cut : cuts) {
    TopoDS_Shape piece;
    if (cut.prismAxis < 0) {
      expandBoundary(cut, part);
      if (!makeBox(cut, piece, error)) return false;
    } else if (!makePrism(cut, part, piece, error)) {
      return false;
    }
    if (!haveTool) {
      tool = piece;
      haveTool = true;
      continue;
    }
    TopoDS_Shape fused;
    if (!fuseTools(tool, piece, fused, error)) return false;
    tool = fused;
  }
  if (!haveTool) {
    error = "joint.no_effect";
    return false;
  }
  if (!subtractTool(shape, tool, error)) return false;
  const double after = volumeOf(shape);
  if (!(after > 0.0)) {
    error = "geometry.empty";
    return false;
  }
  if (!(before - after > 0.5)) {
    error = "joint.no_effect";
    return false;
  }
  return true;
}

bool primitiveShape(const Component& component, TopoDS_Shape& shape, std::string& error) {
  if (component.type == ComponentType::Cylinder) {
    const double radius = units::toMillimetres(component.radiusNm);
    const double height = units::toMillimetres(component.heightNm);
    if (!(radius > 0.0) || !(height > 0.0)) {
      error = "geometry.degenerate";
      return false;
    }
    BRepPrimAPI_MakeCylinder cylinder(gp_Ax2(gp_Pnt(0, 0, 0), gp::DZ()), radius, height);
    cylinder.Build();
    if (!cylinder.IsDone()) {
      error = "geometry.kernel";
      return false;
    }
    shape = cylinder.Shape();
    return true;
  }
  const double width = units::toMillimetres(component.widthNm);
  const double thickness = units::toMillimetres(component.thicknessNm);
  const double height = units::toMillimetres(component.heightNm);
  if (!(width > 0.0) || !(thickness > 0.0) || !(height > 0.0)) {
    error = "geometry.degenerate";
    return false;
  }
  BRepPrimAPI_MakeBox box(width, thickness, height);
  box.Build();
  if (!box.IsDone()) {
    error = "geometry.kernel";
    return false;
  }
  shape = box.Shape();
  return true;
}

bool collectCuts(const Component& component, const std::vector<Joint>& joints, const std::vector<Component>& parts,
                 std::vector<LocalCut>& cuts, bool& touched, std::string& error) {
  touched = false;
  for (const Joint& joint : joints) {
    if (!jointCutsPart(joint, component.id)) continue;
    touched = true;
    const Component* partA = findPart(parts, joint.partA);
    const Component* partB = findPart(parts, joint.partB);
    if (partA == nullptr || partB == nullptr) {
      error = "component.missing";
      return false;
    }
    if (!jointFits(joint, *partA, *partB, error)) return false;
    if (!cutsForPart(joint, component, cuts, error)) return false;
  }
  return true;
}

bool buildSolid(const Component& component, const std::vector<Joint>& joints, const std::vector<Component>& parts,
                TopoDS_Shape& shape, std::string& error) {
  if (!primitiveShape(component, shape, error)) return false;
  std::vector<LocalCut> cuts;
  bool touched = false;
  if (!collectCuts(component, joints, parts, cuts, touched, error)) return false;
  if (!touched) return true;
  if (cuts.empty()) {
    error = "joint.no_effect";
    return false;
  }
  return applyCuts(shape, component, cuts, error);
}

}  // namespace

double deflectionMm(MeshQuality quality) {
  switch (quality) {
    case MeshQuality::Draft:
      return 1.5;
    case MeshQuality::Standard:
      return 0.4;
    case MeshQuality::Fine:
      return 0.1;
  }
  return 0.4;
}

SolidMesh makeBoxMesh(double widthMm, double thicknessMm, double heightMm, MeshQuality quality) {
  if (!(widthMm > 0.0) || !(thicknessMm > 0.0) || !(heightMm > 0.0)) {
    SolidMesh mesh;
    mesh.error = "geometry.degenerate";
    return mesh;
  }
  try {
    BRepPrimAPI_MakeBox box(widthMm, thicknessMm, heightMm);
    box.Build();
    if (!box.IsDone()) {
      SolidMesh mesh;
      mesh.error = "geometry.kernel";
      return mesh;
    }
    return meshShape(box.Shape(), deflectionMm(quality));
  } catch (const Standard_Failure&) {
    SolidMesh mesh;
    mesh.error = "geometry.kernel";
    return mesh;
  }
}

SolidMesh makeCylinderMesh(double radiusMm, double heightMm, MeshQuality quality) {
  if (!(radiusMm > 0.0) || !(heightMm > 0.0)) {
    SolidMesh mesh;
    mesh.error = "geometry.degenerate";
    return mesh;
  }
  try {
    const gp_Ax2 axis(gp_Pnt(0, 0, 0), gp::DZ());
    BRepPrimAPI_MakeCylinder cylinder(axis, radiusMm, heightMm);
    cylinder.Build();
    if (!cylinder.IsDone()) {
      SolidMesh mesh;
      mesh.error = "geometry.kernel";
      return mesh;
    }
    return meshShape(cylinder.Shape(), deflectionMm(quality));
  } catch (const Standard_Failure&) {
    SolidMesh mesh;
    mesh.error = "geometry.kernel";
    return mesh;
  }
}

SolidMesh meshFor(const Component& component, MeshQuality quality) {
  if (component.type == ComponentType::Cylinder) {
    return makeCylinderMesh(units::toMillimetres(component.radiusNm), units::toMillimetres(component.heightNm),
                            quality);
  }
  return makeBoxMesh(units::toMillimetres(component.widthNm), units::toMillimetres(component.thicknessNm),
                     units::toMillimetres(component.heightNm), quality);
}

SolidMesh meshFor(const Component& component, const std::vector<Joint>& joints, const std::vector<Component>& parts,
                  MeshQuality quality) {
  TopoDS_Shape shape;
  std::string error;
  try {
    if (!buildSolid(component, joints, parts, shape, error)) {
      SolidMesh mesh;
      mesh.error = error.empty() ? "geometry.kernel" : error;
      return mesh;
    }
    return meshShape(shape, deflectionMm(quality));
  } catch (const Standard_Failure&) {
    SolidMesh mesh;
    mesh.error = "geometry.kernel";
    return mesh;
  }
}

SolidProbe probeSolid(const Component& component, const std::vector<Joint>& joints,
                      const std::vector<Component>& parts) {
  SolidProbe probe;
  TopoDS_Shape shape;
  try {
    if (!buildSolid(component, joints, parts, shape, probe.error)) {
      if (probe.error.empty()) probe.error = "geometry.kernel";
      return probe;
    }
    probe.volumeMm3 = volumeOf(shape);
  } catch (const Standard_Failure&) {
    probe.error = "geometry.kernel";
    return probe;
  }
  if (!(probe.volumeMm3 > 0.0)) {
    probe.error = "geometry.empty";
    return probe;
  }
  probe.ok = true;
  return probe;
}

PointSide classifyLocal(const Component& component, const std::vector<Joint>& joints,
                        const std::vector<Component>& parts, double xMm, double yMm, double zMm) {
  TopoDS_Shape shape;
  std::string error;
  try {
    if (!buildSolid(component, joints, parts, shape, error)) return PointSide::Unknown;
    BRepClass3d_SolidClassifier classifier(shape);
    classifier.Perform(gp_Pnt(xMm, yMm, zMm), 1.0e-4);
    const TopAbs_State state = classifier.State();
    if (state == TopAbs_IN) return PointSide::In;
    if (state == TopAbs_OUT) return PointSide::Out;
    if (state == TopAbs_ON) return PointSide::On;
  } catch (const Standard_Failure&) {
    return PointSide::Unknown;
  }
  return PointSide::Unknown;
}

}  // namespace timberlab::geometry
