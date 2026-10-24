#include "bench.hpp"

#include "DocumentController.hpp"
#include "timberlab/core/document.hpp"
#include "timberlab/core/manufacturing.hpp"
#include "timberlab/geometry/solids.hpp"
#include "timberlab/storage/archive.hpp"
#include "timberlab/units/units.hpp"

#include <QString>
#include <QUrl>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

namespace timberlab {
namespace {

using Clock = std::chrono::steady_clock;

double millisecondsSince(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

double median(std::vector<double> values) {
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

// A grid of boards, 600 mm apart. Every other neighbouring pair on a row gets a
// mortise and tenon, so some boards are plain and some carry a boolean.
bool fill(Document& document, int boards, std::string& error) {
  constexpr std::int64_t kMm = units::kNmPerMm;
  std::vector<std::string> ids;
  for (int index = 0; index < boards; ++index) {
    const std::string id = document.addBoard();
    if (id.empty()) {
      error = "storage.too_many";
      return false;
    }
    const std::int64_t column = index % 20;
    const std::int64_t row = index / 20;
    if (!document.setDimension(id, "width", 300 * kMm, error) || !document.setDimension(id, "height", 400 * kMm, error) ||
        !document.setPosition(id, column * 700 * kMm, 0, row * 500 * kMm, error)) {
      return false;
    }
    ids.push_back(id);
  }
  for (int index = 0; index + 1 < boards; index += 4) {
    if (index % 20 == 19) continue;
    if (document.addJoint("joint.mortise_tenon", ids[static_cast<std::size_t>(index)],
                          ids[static_cast<std::size_t>(index + 1)], error).empty()) {
      return false;
    }
  }
  return true;
}

void benchSize(int boards, const std::filesystem::path& folder) {
  Document document;
  std::string error;
  const auto buildStart = Clock::now();
  if (!fill(document, boards, error)) {
    std::cout << boards << " boards: setup failed (" << error << ")\n";
    return;
  }
  const double build = millisecondsSince(buildStart);
  const std::string path = (folder / ("bench-" + std::to_string(boards) + ".timberlab")).string();

  std::vector<double> saves;
  std::vector<double> loads;
  for (int run = 0; run < 5; ++run) {
    auto start = Clock::now();
    const auto saved = storage::saveProject(path, document);
    saves.push_back(millisecondsSince(start));
    if (!saved.ok) {
      std::cout << boards << " boards: save failed (" << saved.error << ")\n";
      return;
    }
    Document loaded;
    start = Clock::now();
    const auto opened = storage::loadProject(path, loaded);
    loads.push_back(millisecondsSince(start));
    if (!opened.ok) {
      std::cout << boards << " boards: load failed (" << opened.error << ")\n";
      return;
    }
  }
  const std::uintmax_t bytes = std::filesystem::file_size(path);

  // What every edit costs the editor today: it meshes every part again.
  std::vector<double> rebuilds;
  for (int run = 0; run < 3; ++run) {
    const auto start = Clock::now();
    for (const Component& component : document.components()) {
      const auto mesh = geometry::meshFor(component, document.joints(), document.components(),
                                          geometry::MeshQuality::Standard);
      if (!mesh.ok) {
        std::cout << boards << " boards: mesh failed (" << mesh.error << ")\n";
        return;
      }
    }
    rebuilds.push_back(millisecondsSince(start));
  }

  const std::string first = document.components().front().id;
  const auto editStart = Clock::now();
  const bool edited = document.setDimension(first, "thickness", 20 * units::kNmPerMm, error);
  const double edit = millisecondsSince(editStart);

  std::printf("%5d boards, %3zu joints: build %7.1f ms | save %7.1f ms | load %7.1f ms | file %6.1f KiB | "
              "re-mesh all %8.1f ms | one edit (model only) %6.1f ms%s\n",
              boards, document.joints().size(), build, median(saves), median(loads), static_cast<double>(bytes) / 1024.0,
              median(rebuilds), edit, edited ? "" : " (edit refused)");
}

void benchExports(const std::filesystem::path& folder) {
  Document document;
  std::string error;
  if (!fill(document, 100, error)) return;
  auto start = Clock::now();
  const auto stl = geometry::exportStl(document.components(), document.joints(), "", true);
  std::printf("100 boards: STL export %7.1f ms (%.1f KiB)\n", millisecondsSince(start),
              static_cast<double>(stl.data.size()) / 1024.0);
  if (geometry::stepAvailable()) {
    start = Clock::now();
    const auto step = geometry::exportStep(document.components(), document.joints(), "", true);
    std::printf("100 boards: STEP export %6.1f ms (%.1f KiB)\n", millisecondsSince(start),
                static_cast<double>(step.data.size()) / 1024.0);
  }
  const Component& jointed = document.components().front();
  start = Clock::now();
  const std::string sheet = manufacturing::hiddenLineSheetSvg(jointed, document.joints(), document.components(), error);
  std::printf("one jointed board: hidden-line sheet %6.1f ms (%.1f KiB)\n", millisecondsSince(start),
              static_cast<double>(sheet.size()) / 1024.0);
  (void)folder;
}

// What the editor waits for after an edit, through the real controller: it
// loads the project, then changes one part's thickness.
void benchEditor(int boards, const std::filesystem::path& folder) {
  Document document;
  std::string error;
  if (!fill(document, boards, error)) return;
  const std::filesystem::path path = folder / "editor.timberlab";
  if (!storage::saveProject(path.string(), document).ok) return;
  ui::DocumentController controller;
  controller.initialise();
  auto start = Clock::now();
  if (!controller.loadProject(QUrl::fromLocalFile(QString::fromStdString(path.string())))) {
    std::cout << boards << " boards: editor load failed\n";
    return;
  }
  const double load = millisecondsSince(start);
  const QString first = QString::fromStdString(controller.document().components().front().id);
  const QString last = QString::fromStdString(controller.document().components().back().id);
  std::vector<double> edits;
  for (const char* thickness : {"20 mm", "21 mm", "22 mm"}) {
    start = Clock::now();
    if (!controller.setParameterFromText(first, "thickness", thickness)) {
      std::cout << boards << " boards: editor edit failed\n";
      return;
    }
    edits.push_back(millisecondsSince(start));
  }
  start = Clock::now();
  controller.setParameterFromText(last, "thickness", "23 mm");
  const double plainEdit = millisecondsSince(start);
  std::printf("%5d boards in the editor: open %8.1f ms | edit a jointed board %8.1f ms (median of 3) | edit a plain board %8.1f ms\n",
              boards, load, median(edits), plainEdit);
}

}  // namespace

int runBench() {
  const std::filesystem::path folder = std::filesystem::temp_directory_path() / "timberlab-bench";
  std::filesystem::create_directories(folder);
  std::cout << "TimberLab bench. Medians of 5 save/load runs and 3 re-mesh runs, one thread, this machine only.\n";
  for (const int boards : {10, 100, 500, 1000}) benchSize(boards, folder);
  benchExports(folder);
  for (const int boards : {100, 500}) benchEditor(boards, folder);
  std::filesystem::remove_all(folder);
  return 0;
}

int writeBenchProject(const std::string& path, int boards) {
  Document document;
  std::string error;
  if (boards < 1 || !fill(document, boards, error)) {
    std::cerr << "bench project failed: " << error << "\n";
    return 1;
  }
  const auto saved = storage::saveProject(path, document);
  if (!saved.ok) {
    std::cerr << "bench project failed: " << saved.error << "\n";
    return 1;
  }
  std::cout << "wrote " << path << " with " << boards << " boards and " << document.joints().size() << " joints\n";
  return 0;
}

}  // namespace timberlab
