#include "accept.hpp"

#include "DocumentController.hpp"

#include "timberlab/geometry/solids.hpp"
#include "timberlab/units/units.hpp"

#include <QCoreApplication>
#include <QImage>
#include <QUrl>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

namespace timberlab {
namespace {

int fail(const std::string& message) {
  std::cerr << "accept failed: " << message << "\n";
  return 1;
}

int failStatus(const std::string& message, const ui::DocumentController& editor) {
  std::cerr << "accept failed: " << message << " (" << editor.status().toStdString() << ")\n";
  return 1;
}

bool ensureDataDir() {
  if (std::getenv("TIMBERLAB_DATA_DIR") != nullptr) return true;
  std::error_code error;
  std::filesystem::create_directories("build/accept", error);
  if (error) return false;
#if defined(_WIN32)
  return _putenv_s("TIMBERLAB_DATA_DIR", "build/accept") == 0;
#else
  return setenv("TIMBERLAB_DATA_DIR", "build/accept", 0) == 0;
#endif
}

const ui::ScenePart* findPart(const std::vector<ui::ScenePart>& scene, const QString& id) {
  for (const ui::ScenePart& part : scene) {
    if (part.id == id) return &part;
  }
  return nullptr;
}

bool spanOf(const std::vector<float>& interleaved, int axis, float& low, float& high) {
  if (interleaved.size() < 6) return false;
  low = high = interleaved[static_cast<std::size_t>(axis)];
  for (std::size_t index = 0; index + 5 < interleaved.size(); index += 6) {
    const float value = interleaved[index + static_cast<std::size_t>(axis)];
    if (value < low) low = value;
    if (value > high) high = value;
  }
  return true;
}

bool nearSpan(float low, float high, float expectLow, float expectHigh) {
  return std::abs(low - expectLow) < 0.05f && std::abs(high - expectHigh) < 0.05f;
}

bool meshMatches(const ui::DocumentController& editor, const QString& id, float widthMm, float thicknessMm,
                 float heightMm) {
  const std::vector<ui::ScenePart> scene = editor.copyScene();
  const ui::ScenePart* part = findPart(scene, id);
  if (part == nullptr || part->indices.size() < 12) return false;
  float low = 0;
  float high = 0;
  if (!spanOf(part->interleaved, 0, low, high) || !nearSpan(low, high, 0.f, widthMm)) return false;
  if (!spanOf(part->interleaved, 1, low, high) || !nearSpan(low, high, 0.f, thicknessMm)) return false;
  if (!spanOf(part->interleaved, 2, low, high) || !nearSpan(low, high, 0.f, heightMm)) return false;
  return true;
}

const Component* requirePart(const ui::DocumentController& editor, const QString& id) {
  return editor.document().find(id.toStdString());
}

// Volume of a closed triangle mesh, by the divergence theorem. The scene keeps
// a cache of each part's tessellation, and this checks that an edit left no part
// showing an old solid.
double meshVolumeMm3(const ui::ScenePart& part) {
  double volume = 0;
  for (std::size_t index = 0; index + 2 < part.indices.size(); index += 3) {
    double p[3][3];
    for (int corner = 0; corner < 3; ++corner) {
      const std::size_t offset = static_cast<std::size_t>(part.indices[index + static_cast<std::size_t>(corner)]) * 6;
      for (int axis = 0; axis < 3; ++axis) p[corner][axis] = part.interleaved[offset + static_cast<std::size_t>(axis)];
    }
    volume += p[0][0] * (p[1][1] * p[2][2] - p[1][2] * p[2][1]) - p[0][1] * (p[1][0] * p[2][2] - p[1][2] * p[2][0]) +
              p[0][2] * (p[1][0] * p[2][1] - p[1][1] * p[2][0]);
  }
  return std::abs(volume) / 6.0;
}

bool sceneMatchesSolids(const ui::DocumentController& editor) {
  const std::vector<ui::ScenePart> scene = editor.copyScene();
  for (const Component& component : editor.document().components()) {
    const ui::ScenePart* shown = findPart(scene, QString::fromStdString(component.id));
    const auto solid =
        geometry::probeSolid(component, editor.document().joints(), editor.document().components());
    if (shown == nullptr || !solid.ok) return false;
    if (std::abs(meshVolumeMm3(*shown) - solid.volumeMm3) > solid.volumeMm3 * 1e-3) return false;
  }
  return true;
}

}  // namespace

int runAccept() {
  using units::kNmPerInch;
  using units::kNmPerMm;
  if (QCoreApplication::instance() == nullptr) return fail("no application");
  if (!ensureDataDir()) return fail("data dir");

  const auto directory = std::filesystem::path(std::getenv("TIMBERLAB_DATA_DIR"));
  const auto projectPath = directory / "boards.timberlab";
  std::filesystem::create_directories(directory);

  ui::DocumentController editor;
  editor.initialise();
  const QVariantList shippedMaterials = editor.materialChoices();
  if (shippedMaterials.isEmpty()) return failStatus("catalog", editor);
  for (const QVariant& choice : shippedMaterials) {
    const QVariantMap material = choice.toMap();
    if (material.value("category").toString() == QStringLiteral("user")) continue;
    const QString facePath =
        material.value("faceTexture").toString().replace(QStringLiteral("qrc:"), QStringLiteral(":"));
    const QString endPath =
        material.value("endTexture").toString().replace(QStringLiteral("qrc:"), QStringLiteral(":"));
    const QImage face(facePath);
    const QImage end(endPath);
    if (face.size() != QSize(512, 512) || end.size() != QSize(512, 512)) return fail("material texture");
  }
  // A previous run may have stored inches. The metric checks below need millimetres.
  editor.setDisplayUnit("mm");
  if (editor.displayUnit() != "mm") return failStatus("unit", editor);
  editor.setProjectName("Exact boards");

  editor.addBoard();
  const QString shelf = editor.selectedId();
  if (shelf.isEmpty()) return fail("shelf id");
  if (!editor.setParameterFromText(shelf, "width", "800 mm")) return failStatus("width", editor);
  if (!editor.setParameterFromText(shelf, "thickness", "18 mm")) return failStatus("thickness", editor);
  if (!editor.setParameterFromText(shelf, "height", "400 mm")) return failStatus("height", editor);
  editor.setMaterial(shelf, "european-oak");
  const std::vector<ui::ScenePart> texturedScene = editor.copyScene();
  const ui::ScenePart* texturedShelf = findPart(texturedScene, shelf);
  if (texturedShelf == nullptr || texturedShelf->faceTexture != ":/materials/european-oak-face.jpg" ||
      texturedShelf->endTexture != ":/materials/european-oak-end.jpg" || texturedShelf->engineered) {
    return fail("solid texture mapping");
  }
  editor.setLocked(shelf, true);
  if (editor.setParameterFromText(shelf, "width", "10 mm")) return fail("locked edit accepted");
  editor.setLocked(shelf, false);

  const Component* shelfPart = requirePart(editor, shelf);
  if (shelfPart == nullptr) return fail("shelf missing");
  if (shelfPart->widthNm != 800 * kNmPerMm) return fail("shelf width nm");
  if (shelfPart->thicknessNm != 18 * kNmPerMm) return fail("shelf thickness nm");
  if (shelfPart->heightNm != 400 * kNmPerMm) return fail("shelf height nm");
  if (editor.selection().value("width").toString() != "800 mm") return fail("inspector width text");
  if (!meshMatches(editor, shelf, 800.f, 18.f, 400.f)) return fail("shelf mesh");

  editor.setDisplayUnit("in");
  if (requirePart(editor, shelf)->widthNm != 800 * kNmPerMm) return fail("unit switch mutated width");
  editor.addBoard();
  const QString rail = editor.selectedId();
  if (!editor.setParameterFromText(rail, "width", "24 in")) return failStatus("rail width", editor);
  if (!editor.setParameterFromText(rail, "thickness", "3/4\"")) return failStatus("rail thickness", editor);
  if (!editor.setParameterFromText(rail, "height", "12 in")) return failStatus("rail height", editor);
  editor.setMaterial(rail, "black-walnut");
  if (!editor.setParameterFromText(rail, "height", "10 in")) return failStatus("rail shrink", editor);
  editor.undo();

  const Component* railPart = requirePart(editor, rail);
  if (railPart == nullptr) return fail("rail missing");
  if (railPart->widthNm != 24 * kNmPerInch) return fail("rail width nm");
  if (railPart->thicknessNm != (kNmPerInch * 3) / 4) return fail("rail thickness nm");
  if (railPart->heightNm != 12 * kNmPerInch) return fail("rail height nm");
  if (railPart->placement.xNm != 820 * kNmPerMm) return fail("rail placement");
  if (railPart->materialId != "black-walnut") return fail("rail material");
  if (!meshMatches(editor, rail, 609.6f, 19.05f, 304.8f)) return fail("rail mesh");

  const QUrl url = QUrl::fromLocalFile(QString::fromStdString(projectPath.string()));
  if (!editor.saveProject(url) || editor.dirty()) return failStatus("save", editor);

  ui::DocumentController reopened;
  reopened.initialise();
  if (!reopened.loadProject(url)) return failStatus("reopen", reopened);
  if (reopened.projectName() != "Exact boards") return fail("project name");
  if (reopened.document().components().size() != 2) return fail("reopen count");
  const Component* openedShelf = requirePart(reopened, shelf);
  const Component* openedRail = requirePart(reopened, rail);
  if (openedShelf == nullptr || openedRail == nullptr) return fail("reopen ids");
  if (openedShelf->widthNm != 800 * kNmPerMm || openedShelf->thicknessNm != 18 * kNmPerMm ||
      openedShelf->heightNm != 400 * kNmPerMm) {
    return fail("reopen shelf nm");
  }
  if (openedShelf->materialId != "european-oak") return fail("reopen oak");
  if (openedRail->thicknessNm != 19'050'000 || openedRail->materialId != "black-walnut") return fail("reopen rail");
  if (!meshMatches(reopened, shelf, 800.f, 18.f, 400.f)) return fail("reopen shelf mesh");
  if (!meshMatches(reopened, rail, 609.6f, 19.05f, 304.8f)) return fail("reopen rail mesh");

  const geometry::SolidMesh solid =
      geometry::meshFor(*openedShelf, geometry::MeshQuality::Standard);
  if (!solid.ok) return fail(solid.error);
  const double expectedVolume = 800.0 * 18.0 * 400.0;
  if (std::abs(solid.volumeMm3 - expectedVolume) > expectedVolume * 1e-6) return fail("reopen volume");

  reopened.select(shelf);
  if (!reopened.setParameterFromText(shelf, "thickness", "19.05 mm")) return failStatus("edit after reopen", reopened);
  if (!reopened.saveCurrent()) return failStatus("save after edit", reopened);

  ui::DocumentController checked;
  checked.initialise();
  if (!checked.loadProject(url)) return failStatus("second reopen", checked);
  const Component* edited = requirePart(checked, shelf);
  if (edited == nullptr || edited->thicknessNm != 19'050'000) return fail("edited thickness");
  if (edited->widthNm != 800 * kNmPerMm) return fail("edited width drifted");
  if (!meshMatches(checked, shelf, 800.f, 19.05f, 400.f)) return fail("edited mesh");

  checked.select(rail);
  if (!checked.addJoint("joint.mortise_tenon", rail, shelf)) return failStatus("joint", checked);
  if (checked.document().joints().size() != 1) return fail("joint count");
  const std::string jointId = checked.document().joints().front().id;
  const auto lengthBefore = checked.document().joints().front().lengthNm;
  const Component* joinedShelf = requirePart(checked, shelf);
  const Component* joinedRail = requirePart(checked, rail);
  if (joinedShelf == nullptr || joinedRail == nullptr) return fail("joined parts");
  const auto shelfCut = geometry::probeSolid(*joinedShelf, checked.document().joints(), checked.document().components());
  const auto railCut = geometry::probeSolid(*joinedRail, checked.document().joints(), checked.document().components());
  const double shelfPlain = 800.0 * 19.05 * 400.0;
  const double railPlain = 609.6 * 19.05 * 304.8;
  if (!shelfCut.ok || !(shelfCut.volumeMm3 < shelfPlain - 1.0)) return fail("mortise volume");
  if (!railCut.ok || !(railCut.volumeMm3 < railPlain - 1.0)) return fail("tenon volume");

  if (!checked.setJointFromText(QString::fromStdString(jointId), "length", "20 mm")) {
    return failStatus("joint length", checked);
  }
  const Component* shorterRail = requirePart(checked, rail);
  const Component* sameShelf = requirePart(checked, shelf);
  const auto railShorter =
      geometry::probeSolid(*shorterRail, checked.document().joints(), checked.document().components());
  const auto shelfSame = geometry::probeSolid(*sameShelf, checked.document().joints(), checked.document().components());
  if (!railShorter.ok || !(railShorter.volumeMm3 > railCut.volumeMm3 + 1.0)) return fail("tenon length");
  if (!shelfSame.ok || std::abs(shelfSame.volumeMm3 - shelfCut.volumeMm3) > 1.0) return fail("mortise depth drifted");

  checked.undo();
  if (checked.document().joints().size() != 1) return fail("undo dropped joint");
  if (checked.document().joints().front().lengthNm != lengthBefore) return fail("undo length");
  const auto railRestored =
      geometry::probeSolid(*requirePart(checked, rail), checked.document().joints(), checked.document().components());
  if (!railRestored.ok || std::abs(railRestored.volumeMm3 - railCut.volumeMm3) > 1.0) return fail("undo tenon");

  if (!checked.saveCurrent()) return failStatus("save joint", checked);
  ui::DocumentController joined;
  joined.initialise();
  if (!joined.loadProject(url)) return failStatus("reopen joint", joined);
  if (joined.document().joints().size() != 1) return fail("reopen joint count");
  const auto reloadedShelf =
      geometry::probeSolid(*requirePart(joined, shelf), joined.document().joints(), joined.document().components());
  const auto reloadedRail =
      geometry::probeSolid(*requirePart(joined, rail), joined.document().joints(), joined.document().components());
  if (!reloadedShelf.ok || !(reloadedShelf.volumeMm3 < shelfPlain - 1.0)) return fail("reopen mortise");
  if (!reloadedRail.ok || !(reloadedRail.volumeMm3 < railPlain - 1.0)) return fail("reopen tenon");

  while (!joined.userMaterials().isEmpty()) {
    const QString id = joined.userMaterials().front().toMap().value("id").toString();
    if (!joined.removeUserMaterial(id)) return failStatus("library clear", joined);
  }
  while (!joined.userPresets().isEmpty()) {
    const QString id = joined.userPresets().front().toMap().value("id").toString();
    if (!joined.removeUserPreset(id)) return failStatus("library clear", joined);
  }
  if (!joined.addUserMaterial("Shop oak", "#A67C52")) return failStatus("library material", joined);
  if (joined.userMaterials().size() != 1) return fail("library count");
  const QString materialId = joined.userMaterials().front().toMap().value("id").toString();
  if (!materialId.startsWith("user_")) return fail("library id");
  bool listed = false;
  for (const QVariant& choice : joined.materialChoices()) {
    if (choice.toMap().value("id").toString() == materialId) listed = true;
  }
  if (!listed) return fail("library choice");
  joined.setMaterial(shelf, materialId);
  if (requirePart(joined, shelf) == nullptr || requirePart(joined, shelf)->materialId != materialId.toStdString()) {
    return fail("library assign");
  }
  const std::int64_t thicknessBefore = requirePart(joined, shelf)->thicknessNm;
  if (!joined.addUserPreset("18 mm")) return failStatus("library preset", joined);
  if (joined.userPresets().size() != 1 || joined.userPresets().front().toMap().value("label").toString() != "18 mm") {
    return fail("library preset row");
  }
  if (requirePart(joined, shelf)->thicknessNm != thicknessBefore) return fail("library preset resized the part");
  if (!joined.saveCurrent()) return failStatus("save library material", joined);
  ui::DocumentController reloaded;
  reloaded.initialise();
  if (!reloaded.loadProject(url)) return failStatus("reopen library material", reloaded);
  if (requirePart(reloaded, shelf) == nullptr || requirePart(reloaded, shelf)->materialId != materialId.toStdString()) {
    return fail("project kept material");
  }
  if (reloaded.userMaterials().size() != 1) return fail("library reopen");
  if (reloaded.document().joints().size() != 1) return fail("library edit dropped joint");

  // The Joinery button: two selected boards that touch get a joint on their
  // contacting faces, and a pair that does not touch is refused.
  ui::DocumentController pair;
  pair.initialise();
  pair.setDisplayUnit("mm");
  QString boards[2];
  for (QString& board : boards) {
    pair.addBoard();
    board = pair.selectedId();
    if (!pair.setParameterFromText(board, "width", "100 mm")) return failStatus("pair width", pair);
    if (!pair.setParameterFromText(board, "thickness", "18 mm")) return failStatus("pair thickness", pair);
    if (!pair.setParameterFromText(board, "height", "200 mm")) return failStatus("pair height", pair);
    for (const char* axis : {"x", "y", "z"}) {
      if (!pair.setTranslationFromText(board, axis, "0 mm")) return failStatus("pair origin", pair);
    }
  }
  if (!pair.setTranslationFromText(boards[1], "x", "150 mm")) return failStatus("pair apart", pair);
  pair.select(boards[0]);
  if (!pair.armSelectedPair().isEmpty()) return fail("one board armed a joint");
  pair.select(boards[1], Qt::ControlModifier);
  if (!pair.armSelectedPair().isEmpty()) return fail("distant boards armed a joint");
  if (pair.addArmedJoint("joint.half_lap")) return fail("distant boards joined");
  if (!pair.setTranslationFromText(boards[1], "x", "100 mm")) return failStatus("pair touching", pair);
  pair.select(boards[0]);
  pair.select(boards[1], Qt::ControlModifier);
  const QStringList fitting = pair.armSelectedPair();
  if (fitting.isEmpty() || fitting != pair.armedJointTypes()) return failStatus("touching boards offered no joint", pair);
  for (const char* later : {"joint.haunched_tenon", "joint.wedged_tenon", "joint.loose_tenon"}) {
    if (!fitting.contains(later)) return fail("later joint not offered");
  }
  if (!pair.addArmedJoint(fitting.first())) return failStatus("pair joint", pair);
  if (pair.document().joints().size() != 1) return fail("pair joint count");
  const Joint& pairJoint = pair.document().joints().front();
  const bool samePair = (pairJoint.partA == boards[0].toStdString() && pairJoint.partB == boards[1].toStdString()) ||
                        (pairJoint.partA == boards[1].toStdString() && pairJoint.partB == boards[0].toStdString());
  if (!samePair) return fail("pair joint parts");

  // Edits after the joint: the scene must follow the solids, whether the part
  // itself changed or only the part it is joined to.
  if (!sceneMatchesSolids(pair)) return fail("scene after the joint");
  if (!pair.setParameterFromText(boards[1], "width", "90 mm")) return failStatus("pair mate width", pair);
  if (!sceneMatchesSolids(pair)) return fail("scene after the mate changed");
  const QString pairJointId = QString::fromStdString(pair.document().joints().front().id);
  for (const char* key : {"length", "width", "thickness", "depth"}) {
    if (!pair.setJointFromText(pairJointId, key, "5 mm")) continue;
    if (!sceneMatchesSolids(pair)) return fail(std::string("scene after the joint ") + key);
  }
  pair.undo();
  if (!sceneMatchesSolids(pair)) return fail("scene after undo");

  // The exports, on the joined pair: STL, STEP when this build has the writer,
  // and the hidden-line sheet of the selected part.
  const auto exported = [&](const char* name) { return QUrl::fromLocalFile(QString::fromStdString((directory / name).string())); };
  const auto written = [&](const char* name) {
    std::error_code size_error;
    return std::filesystem::file_size(directory / name, size_error) > 0 && !size_error;
  };
  if (!pair.exportModel(exported("pair.stl"), "stl", "project") || !written("pair.stl")) {
    return failStatus("pair stl", pair);
  }
  if (pair.stepAvailable() &&
      (!pair.exportModel(exported("pair.step"), "step", "project") || !written("pair.step"))) {
    return failStatus("pair step", pair);
  }
  pair.select(boards[0]);
  if (!pair.exportDrawing(exported("pair-hlr.svg"), "hlr-sheet") || !written("pair-hlr.svg")) {
    return failStatus("pair hidden-line sheet", pair);
  }

  std::cout << "accept ok: shelf 800 x 19.05 x 400 mm with a mortise, rail 609.6 x 19.05 x 304.8 mm with a tenon, reloaded "
            << projectPath.string() << "\n";
  return 0;
}

}  // namespace timberlab
