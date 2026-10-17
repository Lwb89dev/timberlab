#include "timberlab/geometry/solids.hpp"

#include "timberlab/units/units.hpp"

#include <BRepBndLib.hxx>
#include <BRepCheck_Analyzer.hxx>
#include <BRepGProp.hxx>
#include <BRepMesh_IncrementalMesh.hxx>
#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeCylinder.hxx>
#include <BRep_Tool.hxx>
#include <Bnd_Box.hxx>
#include <GProp_GProps.hxx>
#include <Poly_Triangulation.hxx>
#include <Standard_Failure.hxx>
#include <TopExp_Explorer.hxx>
#include <TopLoc_Location.hxx>
#include <TopoDS.hxx>
#include <TopoDS_Face.hxx>
#include <gp.hxx>
#include <gp_Ax2.hxx>

#include <utility>

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
    for (int index = 1; index <= triangulation->NbTriangles(); ++index) {
      int n1 = 0, n2 = 0, n3 = 0;
      triangulation->Triangle(index).Get(n1, n2, n3);
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

}  // namespace timberlab::geometry
