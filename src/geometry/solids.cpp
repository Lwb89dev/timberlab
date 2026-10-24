#include "timberlab/geometry/solids.hpp"

#include "timberlab/core/transform.hpp"
#include "timberlab/units/units.hpp"

#include <BRepAdaptor_Curve.hxx>
#include <BRepAlgoAPI_Cut.hxx>
#include <BRepAlgoAPI_Fuse.hxx>
#include <BRepBndLib.hxx>
#include <BRepBuilderAPI_MakeFace.hxx>
#include <BRepBuilderAPI_MakePolygon.hxx>
#include <BRepBuilderAPI_Transform.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepClass3d_SolidClassifier.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRepPrimAPI_MakePrism.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GCPnts_QuasiUniformDeflection.hxx>
#include <GeomAbs_CurveType.hxx>
#include <HLRAlgo_Projector.hxx>
#include <HLRBRep_Algo.hxx>
#include <HLRBRep_HLRToShape.hxx>
#include <GProp_GProps.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <TopAbs_State.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopTools_ListOfShape.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Edge.hxx>
#include <TopoDS_Face.hxx>
#include <gp.hxx>
#include <gp_Ax2.hxx>
#include <gp_Pnt.hxx>
#include <gp_Trsf.hxx>
#include <gp_Vec.hxx>

#if defined(TIMBERLAB_HAS_STEP)
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>
#include <Message.hxx>
#include <Message_Messenger.hxx>
#include <Message_PrinterOStream.hxx>
#include <STEPControl_StepModelType.hxx>
#include <STEPControl_Writer.hxx>
#include <StepBasic_Product.hxx>
#include <StepData_StepModel.hxx>
#include <TCollection_HAsciiString.hxx>
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <set>
#include <sstream>
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

bool makeFreeCylinder(const LocalCut& cut, TopoDS_Shape& shape, std::string& error) {
  const gp_Pnt from(units::toMillimetres(cut.x0), units::toMillimetres(cut.y0), units::toMillimetres(cut.z0));
  const gp_Pnt to(units::toMillimetres(cut.x1), units::toMillimetres(cut.y1), units::toMillimetres(cut.z1));
  const double height = from.Distance(to);
  const double radius = units::toMillimetres(cut.radiusNm);
  if (!(height > 0.0) || !(radius > 0.0)) {
    error = "joint.no_effect";
    return false;
  }
  BRepPrimAPI_MakeCylinder cylinder(gp_Ax2(from, gp_Dir(gp_Vec(from, to))), radius, height);
  cylinder.Build();
  if (!cylinder.IsDone()) {
    error = "geometry.kernel";
    return false;
  }
  shape = cylinder.Shape();
  return true;
}

bool makeCylinder(const LocalCut& original, const Component& part, TopoDS_Shape& shape, std::string& error) {
  if (original.cylinderAxis < 0 || original.cylinderAxis > 3 || original.radiusNm <= 0) {
    error = "joint.no_effect";
    return false;
  }
  if (original.cylinderAxis == 3) return makeFreeCylinder(original, shape, error);
  LocalCut span = original;
  expandBoundary(span, part);
  const std::int64_t axisLow[3] = {span.x0, span.y0, span.z0};
  const std::int64_t axisHigh[3] = {span.x1, span.y1, span.z1};
  const std::int64_t boxLow[3] = {original.x0, original.y0, original.z0};
  const std::int64_t boxHigh[3] = {original.x1, original.y1, original.z1};
  const int axis = original.cylinderAxis;
  if (axisHigh[axis] <= axisLow[axis]) {
    error = "joint.no_effect";
    return false;
  }
  const double radius = units::toMillimetres(original.radiusNm);
  if (!(radius > 0.0)) {
    error = "joint.no_effect";
    return false;
  }
  double origin[3] = {};
  for (int i = 0; i < 3; ++i) {
    if (i == axis) origin[i] = units::toMillimetres(axisLow[i]);
    else origin[i] = units::toMillimetres((boxLow[i] + boxHigh[i]) / 2);
  }
  gp_Dir direction(1, 0, 0);
  if (axis == 1) direction = gp_Dir(0, 1, 0);
  if (axis == 2) direction = gp_Dir(0, 0, 1);
  const double height = units::toMillimetres(axisHigh[axis] - axisLow[axis]);
  BRepPrimAPI_MakeCylinder cylinder(gp_Ax2(gp_Pnt(origin[0], origin[1], origin[2]), direction), radius, height);
  cylinder.Build();
  if (!cylinder.IsDone()) {
    error = "geometry.kernel";
    return false;
  }
  shape = cylinder.Shape();
  return true;
}

bool makeTool(LocalCut cut, const Component& part, TopoDS_Shape& piece, std::string& error) {
  if (cut.cylinderAxis >= 0) return makeCylinder(cut, part, piece, error);
  if (cut.prismAxis < 0) {
    expandBoundary(cut, part);
    return makeBox(cut, piece, error);
  }
  return makePrism(cut, part, piece, error);
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
  for (const LocalCut& cut : cuts) {
    TopoDS_Shape piece;
    if (!makeTool(cut, part, piece, error)) return false;
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
    const Component& mate = component.id == joint.partA ? *partB : *partA;
    if (!cutsForPart(joint, component, mate, cuts, error)) return false;
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

namespace {

void appendSegments(const TopoDS_Shape& compound, std::vector<Segment2>& out) {
  if (compound.IsNull()) return;
  constexpr double kDeflectionMm = 0.02;
  for (TopExp_Explorer explorer(compound, TopAbs_EDGE); explorer.More(); explorer.Next()) {
    const TopoDS_Edge edge = TopoDS::Edge(explorer.Current());
    BRepAdaptor_Curve curve(edge);
    const double first = curve.FirstParameter();
    const double last = curve.LastParameter();
    if (curve.GetType() == GeomAbs_Line) {
      const gp_Pnt a = curve.Value(first);
      const gp_Pnt b = curve.Value(last);
      out.push_back({a.X(), a.Y(), b.X(), b.Y()});
      continue;
    }
    GCPnts_QuasiUniformDeflection points(curve, kDeflectionMm);
    if (!points.IsDone() || points.NbPoints() < 2) continue;
    for (int index = 1; index < points.NbPoints(); ++index) {
      const gp_Pnt a = points.Value(index);
      const gp_Pnt b = points.Value(index + 1);
      out.push_back({a.X(), a.Y(), b.X(), b.Y()});
    }
  }
}

// A hidden edge that lies under visible ones adds nothing to the drawing. The back
// face of a box projects onto its front face, for one, and a slot in the front
// splits the visible edge in pieces while the back edge stays whole. Axis-aligned
// edges lose the parts that visible edges cover. Other edges are dropped only
// when a visible edge has the same two ends.
constexpr double kCoincidentMm = 1.0e-4;

struct Interval {
  double low = 0;
  double high = 0;
};

void subtractCovered(const Interval& span, std::vector<Interval> covered, std::vector<Interval>& out) {
  std::sort(covered.begin(), covered.end(), [](const Interval& a, const Interval& b) { return a.low < b.low; });
  double cursor = span.low;
  for (const Interval& piece : covered) {
    if (piece.high <= cursor) continue;
    if (piece.low >= span.high) break;
    if (piece.low > cursor + kCoincidentMm) out.push_back({cursor, piece.low});
    cursor = std::max(cursor, piece.high);
  }
  if (cursor < span.high - kCoincidentMm) out.push_back({cursor, span.high});
}

void dropCoincidentHidden(ViewLines& lines) {
  const auto key = [](const Segment2& segment) {
    std::array<std::int64_t, 4> ends = {std::llround(segment.u0 * 1.0e4), std::llround(segment.v0 * 1.0e4),
                                        std::llround(segment.u1 * 1.0e4), std::llround(segment.v1 * 1.0e4)};
    if (ends[0] > ends[2] || (ends[0] == ends[2] && ends[1] > ends[3])) {
      std::swap(ends[0], ends[2]);
      std::swap(ends[1], ends[3]);
    }
    return ends;
  };
  std::set<std::array<std::int64_t, 4>> seen;
  for (const Segment2& segment : lines.visible) seen.insert(key(segment));
  std::vector<Segment2> kept;
  for (const Segment2& segment : lines.hidden) {
    const bool horizontal = std::abs(segment.v0 - segment.v1) < kCoincidentMm;
    const bool vertical = std::abs(segment.u0 - segment.u1) < kCoincidentMm;
    if (!horizontal && !vertical) {
      if (seen.insert(key(segment)).second) kept.push_back(segment);
      continue;
    }
    const double fixed = horizontal ? segment.v0 : segment.u0;
    const Interval span = horizontal ? Interval{std::min(segment.u0, segment.u1), std::max(segment.u0, segment.u1)}
                                     : Interval{std::min(segment.v0, segment.v1), std::max(segment.v0, segment.v1)};
    std::vector<Interval> covered;
    for (const Segment2& visible : lines.visible) {
      const bool along = horizontal ? std::abs(visible.v0 - fixed) < kCoincidentMm && std::abs(visible.v1 - fixed) < kCoincidentMm
                                    : std::abs(visible.u0 - fixed) < kCoincidentMm && std::abs(visible.u1 - fixed) < kCoincidentMm;
      if (!along) continue;
      covered.push_back(horizontal ? Interval{std::min(visible.u0, visible.u1), std::max(visible.u0, visible.u1)}
                                   : Interval{std::min(visible.v0, visible.v1), std::max(visible.v0, visible.v1)});
    }
    std::vector<Interval> rest;
    subtractCovered(span, covered, rest);
    for (const Interval& piece : rest) {
      kept.push_back(horizontal ? Segment2{piece.low, fixed, piece.high, fixed}
                                : Segment2{fixed, piece.low, fixed, piece.high});
    }
  }
  lines.hidden = std::move(kept);
}

gp_Ax2 viewFrame(ViewDirection view) {
  if (view == ViewDirection::Top) return gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, 0, 1), gp_Dir(1, 0, 0));
  if (view == ViewDirection::Right) return gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(1, 0, 0), gp_Dir(0, 1, 0));
  return gp_Ax2(gp_Pnt(0, 0, 0), gp_Dir(0, -1, 0), gp_Dir(1, 0, 0));
}

#if defined(TIMBERLAB_HAS_STEP)
bool worldShape(const Component& component, const std::vector<Joint>& joints, const std::vector<Component>& parts,
                bool world, TopoDS_Shape& shape, std::string& error) {
  if (!buildSolid(component, joints, parts, shape, error)) return false;
  if (!world) return true;
  double m[16];
  placementMatrix(component.placement, m);
  gp_Trsf transform;
  transform.SetValues(m[0], m[4], m[8], m[12], m[1], m[5], m[9], m[13], m[2], m[6], m[10], m[14]);
  BRepBuilderAPI_Transform moved(shape, transform, Standard_True);
  if (!moved.IsDone()) {
    error = "geometry.kernel";
    return false;
  }
  shape = moved.Shape();
  return true;
}
#endif

std::vector<const Component*> exportedParts(const std::vector<Component>& parts, const std::string& onlyId) {
  std::vector<const Component*> chosen;
  for (const Component& part : parts) {
    if (!onlyId.empty() && part.id != onlyId) continue;
    if (onlyId.empty() && !part.visible) continue;
    chosen.push_back(&part);
  }
  return chosen;
}

void putFloat(std::string& out, float value) {
  char bytes[sizeof(float)];
  std::memcpy(bytes, &value, sizeof(float));
  out.append(bytes, sizeof(float));
}

void putCount(std::string& out, std::uint32_t value, int width) {
  for (int shift = 0; shift < width * 8; shift += 8) out.push_back(static_cast<char>((value >> shift) & 0xffu));
}

}  // namespace

ViewLines hiddenLineView(const Component& component, const std::vector<Joint>& joints,
                         const std::vector<Component>& parts, ViewDirection view) {
  ViewLines lines;
  TopoDS_Shape shape;
  try {
    if (!buildSolid(component, joints, parts, shape, lines.error)) {
      if (lines.error.empty()) lines.error = "geometry.kernel";
      return lines;
    }
    Handle(HLRBRep_Algo) algo = new HLRBRep_Algo();
    algo->Add(shape);
    algo->Projector(HLRAlgo_Projector(viewFrame(view)));
    algo->Update();
    algo->Hide();
    HLRBRep_HLRToShape result(algo);
    appendSegments(result.VCompound(), lines.visible);
    appendSegments(result.OutLineVCompound(), lines.visible);
    appendSegments(result.HCompound(), lines.hidden);
    appendSegments(result.OutLineHCompound(), lines.hidden);
  } catch (const Standard_Failure&) {
    lines.error = "geometry.kernel";
    return lines;
  }
  if (lines.visible.empty()) {
    lines.error = "geometry.empty";
    return lines;
  }
  dropCoincidentHidden(lines);
  lines.ok = true;
  return lines;
}

ExportResult exportStl(const std::vector<Component>& parts, const std::vector<Joint>& joints,
                       const std::string& onlyId, bool world) {
  ExportResult result;
  const std::vector<const Component*> chosen = exportedParts(parts, onlyId);
  if (chosen.empty()) {
    result.error = "component.missing";
    return result;
  }
  std::string triangles;
  std::uint32_t count = 0;
  try {
    for (const Component* part : chosen) {
      const SolidMesh mesh = meshFor(*part, joints, parts, MeshQuality::Fine);
      if (!mesh.ok) {
        result.error = mesh.error.empty() ? "geometry.invalid" : mesh.error;
        return result;
      }
      double m[16];
      if (world) placementMatrix(part->placement, m);
      else setIdentity(m);
      for (std::size_t index = 0; index + 2 < mesh.indices.size(); index += 3) {
        double corner[3][3];
        for (int c = 0; c < 3; ++c) {
          const std::size_t offset = static_cast<std::size_t>(mesh.indices[index + static_cast<std::size_t>(c)]) * 6;
          transformPoint(m, mesh.interleaved[offset], mesh.interleaved[offset + 1], mesh.interleaved[offset + 2],
                         corner[c][0], corner[c][1], corner[c][2]);
        }
        const double ex = corner[1][0] - corner[0][0];
        const double ey = corner[1][1] - corner[0][1];
        const double ez = corner[1][2] - corner[0][2];
        const double fx = corner[2][0] - corner[0][0];
        const double fy = corner[2][1] - corner[0][1];
        const double fz = corner[2][2] - corner[0][2];
        double nx = ey * fz - ez * fy;
        double ny = ez * fx - ex * fz;
        double nz = ex * fy - ey * fx;
        const double length = std::sqrt(nx * nx + ny * ny + nz * nz);
        if (!(length > 0.0)) continue;
        nx /= length;
        ny /= length;
        nz /= length;
        putFloat(triangles, static_cast<float>(nx));
        putFloat(triangles, static_cast<float>(ny));
        putFloat(triangles, static_cast<float>(nz));
        for (const auto& point : corner) {
          for (const double value : point) putFloat(triangles, static_cast<float>(value));
        }
        putCount(triangles, 0, 2);
        ++count;
      }
    }
  } catch (const Standard_Failure&) {
    result.error = "geometry.kernel";
    return result;
  }
  if (count == 0) {
    result.error = "geometry.empty";
    return result;
  }
  std::string header = "TimberLab binary STL, millimetres";
  header.resize(80, ' ');
  result.data = header;
  putCount(result.data, count, 4);
  result.data += triangles;
  result.partCount = static_cast<int>(chosen.size());
  result.ok = true;
  return result;
}

#if defined(TIMBERLAB_HAS_STEP)
bool stepAvailable() { return true; }

ExportResult exportStep(const std::vector<Component>& parts, const std::vector<Joint>& joints,
                        const std::string& onlyId, bool world) {
  ExportResult result;
  const std::vector<const Component*> chosen = exportedParts(parts, onlyId);
  if (chosen.empty()) {
    result.error = "component.missing";
    return result;
  }
  try {
    // The writer prints a transfer report for each shape to the console.
    static const bool quiet = [] {
      Message::DefaultMessenger()->RemovePrinters(STANDARD_TYPE(Message_PrinterOStream));
      return true;
    }();
    (void)quiet;
    STEPControl_Writer writer;
    Interface_Static::SetCVal("write.step.unit", "MM");
    Interface_Static::SetCVal("write.step.schema", "AP214IS");
    std::vector<std::string> names;
    for (const Component* part : chosen) {
      TopoDS_Shape shape;
      if (!worldShape(*part, joints, parts, world, shape, result.error)) {
        if (result.error.empty()) result.error = "geometry.kernel";
        return result;
      }
      if (writer.Transfer(shape, STEPControl_AsIs) != IFSelect_RetDone) {
        result.error = "geometry.kernel";
        return result;
      }
      names.push_back(part->name.empty() ? part->id : part->name);
    }
    // The writer adds one product per transferred shape, in order.
    Handle(StepData_StepModel) model = writer.Model();
    std::size_t next = 0;
    for (int index = 1; index <= model->NbEntities() && next < names.size(); ++index) {
      Handle(StepBasic_Product) product = Handle(StepBasic_Product)::DownCast(model->Value(index));
      if (product.IsNull()) continue;
      Handle(TCollection_HAsciiString) text = new TCollection_HAsciiString(names[next].c_str());
      product->SetId(text);
      product->SetName(text);
      ++next;
    }
    std::ostringstream stream;
    if (writer.WriteStream(stream) != IFSelect_RetDone) {
      result.error = "geometry.kernel";
      return result;
    }
    result.data = stream.str();
  } catch (const Standard_Failure&) {
    result.error = "geometry.kernel";
    return result;
  }
  if (result.data.empty()) {
    result.error = "geometry.empty";
    return result;
  }
  result.partCount = static_cast<int>(chosen.size());
  result.ok = true;
  return result;
}
#else
bool stepAvailable() { return false; }

ExportResult exportStep(const std::vector<Component>&, const std::vector<Joint>&, const std::string&, bool) {
  ExportResult result;
  result.error = "export.unavailable";
  return result;
}
#endif

}  // namespace timberlab::geometry
