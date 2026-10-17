#include "accept.hpp"

#include "DocumentController.hpp"

#include "timberlab/geometry/solids.hpp"
#include "timberlab/units/units.hpp"

#include <QCoreApplication>
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
  return setenv("TIMBERLAB_DATA_DIR", "build/accept", 0) == 0;
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
  if (editor.materialChoices().isEmpty()) return failStatus("catalog", editor);
  editor.setProjectName("Exact boards");

  editor.addBoard();
  const QString shelf = editor.selectedId();
  if (shelf.isEmpty()) return fail("shelf id");
  if (!editor.setParameterFromText(shelf, "width", "800 mm")) return failStatus("width", editor);
  if (!editor.setParameterFromText(shelf, "thickness", "18 mm")) return failStatus("thickness", editor);
  if (!editor.setParameterFromText(shelf, "height", "400 mm")) return failStatus("height", editor);
  editor.setMaterial(shelf, "european-oak");
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

  std::cout << "accept ok: shelf 800 x 18 x 400 mm, rail 609.6 x 19.05 x 304.8 mm, reloaded "
            << projectPath.string() << "\n";
  return 0;
}

}  // namespace timberlab
