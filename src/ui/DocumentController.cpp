#include "DocumentController.hpp"

#include "timberlab/core/transform.hpp"
#include "timberlab/platform/paths.hpp"
#include "timberlab/storage/archive.hpp"

#include <QFile>
#include <QLocale>
#include <QMatrix4x4>

#include <algorithm>
#include <cmath>
#include <exception>
#include <filesystem>
#include <string>

namespace timberlab::ui {
namespace {

QVector3D colorFromHex(const std::string& hex) {
  if (hex.size() != 7 || hex[0] != '#') return {0.72f, 0.58f, 0.40f};
  auto channel = [&](int offset) {
    int value = 0;
    for (int i = 0; i < 2; ++i) {
      const char c = hex[static_cast<std::size_t>(offset + i)];
      value *= 16;
      if (c >= '0' && c <= '9') value += c - '0';
      else if (c >= 'a' && c <= 'f') value += c - 'a' + 10;
      else if (c >= 'A' && c <= 'F') value += c - 'A' + 10;
    }
    return static_cast<float>(value) / 255.f;
  };
  return {channel(1), channel(3), channel(5)};
}

QMatrix4x4 matrixFrom(const Placement& placement) {
  double values[16];
  placementMatrix(placement, values);
  QMatrix4x4 matrix;
  matrix.setColumn(0, QVector4D(values[0], values[1], values[2], values[3]));
  matrix.setColumn(1, QVector4D(values[4], values[5], values[6], values[7]));
  matrix.setColumn(2, QVector4D(values[8], values[9], values[10], values[11]));
  matrix.setColumn(3, QVector4D(values[12], values[13], values[14], values[15]));
  return matrix;
}

QString readResource(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) return {};
  return QString::fromUtf8(file.readAll());
}

geometry::MeshQuality qualityFromKey(const QString& key) {
  if (key == "draft") return geometry::MeshQuality::Draft;
  if (key == "fine") return geometry::MeshQuality::Fine;
  return geometry::MeshQuality::Standard;
}

QString qualityKey(geometry::MeshQuality quality) {
  switch (quality) {
    case geometry::MeshQuality::Draft:
      return "draft";
    case geometry::MeshQuality::Fine:
      return "fine";
    case geometry::MeshQuality::Standard:
      return "standard";
  }
  return "standard";
}

}  // namespace

ComponentListModel::ComponentListModel(DocumentController* owner) : owner_(owner) {}

int ComponentListModel::rowCount(const QModelIndex& parent) const {
  if (parent.isValid()) return 0;
  return static_cast<int>(owner_->document().components().size());
}

QVariant ComponentListModel::data(const QModelIndex& index, int role) const {
  const auto& components = owner_->document().components();
  if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(components.size())) return {};
  const Component& component = components[static_cast<std::size_t>(index.row())];
  switch (role) {
    case IdRole:
      return QString::fromStdString(component.id);
    case NameRole:
      return QString::fromStdString(component.name);
    case TypeRole:
      return QString::fromLatin1(componentTypeKey(component.type));
    case VisibleRole:
      return component.visible;
    case LockedRole:
      return component.locked;
    default:
      return {};
  }
}

QHash<int, QByteArray> ComponentListModel::roleNames() const {
  return {{IdRole, "partId"}, {NameRole, "name"}, {TypeRole, "partType"}, {VisibleRole, "shown"}, {LockedRole, "locked"}};
}

void ComponentListModel::refresh() {
  beginResetModel();
  endResetModel();
}

DocumentController::DocumentController(QObject* parent) : QObject(parent), components_(this) {}

void DocumentController::initialise() {
  std::string error;
  if (!catalog_.load(readResource(":/data/materials.json").toStdString(), error)) {
    report(error.empty() ? "material.json" : error, true);
  }
  std::vector<materials::LumberPreset> lumber;
  if (!materials::loadPresets(readResource(":/data/presets.json").toStdString(), thicknesses_, lumber, error)) {
    report(error.empty() ? "preset.json" : error, false);
  }
  const std::string dir = platform::userDataDir();
  std::filesystem::create_directories(dir);
  preferences_ = std::make_unique<storage::Preferences>(dir + "/timberlab.sqlite");
  if (!preferences_->ok()) report("preferences.open", false);

  QString unit = "mm";
  int precision = 16;
  if (preferences_ && preferences_->ok()) {
    if (auto stored = preferences_->get("display_unit")) unit = QString::fromStdString(*stored);
    if (auto stored = preferences_->get("fraction_precision")) {
      try {
        precision = std::stoi(*stored);
      } catch (const std::exception&) {
        precision = 16;
      }
    }
    if (auto stored = preferences_->get("mesh_quality")) quality_ = qualityFromKey(QString::fromStdString(*stored));
  }
  document_.newProject(unit.toStdString(), precision);
  status_ = tr("Ready");
  touch(false);
}

QString DocumentController::projectName() const { return QString::fromStdString(document_.name()); }

void DocumentController::setProjectName(const QString& name) {
  std::string error;
  if (!document_.setProjectName(name.trimmed().toStdString(), error)) {
    report(error, false);
    return;
  }
  touch(false);
}

QString DocumentController::displayUnit() const { return QString::fromStdString(document_.displayUnit()); }

int DocumentController::fractionPrecision() const { return document_.fractionPrecision(); }

bool DocumentController::canUndo() const { return document_.canUndo(); }

bool DocumentController::canRedo() const { return document_.canRedo(); }

bool DocumentController::dirty() const { return document_.dirty(); }

QString DocumentController::meshQuality() const { return qualityKey(quality_); }

void DocumentController::setMeshQuality(const QString& quality) {
  quality_ = qualityFromKey(quality);
  rememberPreference("mesh_quality", qualityKey(quality_));
  touch(false);
}

units::LengthUnit DocumentController::currentUnit() const {
  units::LengthUnit unit = units::LengthUnit::Millimetre;
  units::lengthUnitFromKey(document_.displayUnit(), unit);
  return unit;
}

QString DocumentController::languageName(const materials::CatalogEntry& entry) const {
  const QString locale = QLocale().name();
  const QString language = locale.section('_', 0, 0);
  QString english;
  for (const auto& name : entry.commonNames) {
    const QString key = QString::fromStdString(name.first);
    if (key == locale || key == language) return QString::fromStdString(name.second);
    if (key == "en") english = QString::fromStdString(name.second);
  }
  return english.isEmpty() ? QString::fromStdString(entry.id) : english;
}

QVariantList DocumentController::materialChoices() const {
  QVariantList list;
  for (const auto& entry : catalog_.entries()) {
    list.push_back(QVariantMap{{"id", QString::fromStdString(entry.id)},
                               {"name", languageName(entry)},
                               {"category", QString::fromStdString(entry.category)}});
  }
  return list;
}

QVariantList DocumentController::thicknessPresets() const {
  QVariantList list;
  for (const auto& preset : thicknesses_) {
    list.push_back(QVariantMap{{"id", QString::fromStdString(preset.id)},
                               {"label", QString::fromStdString(preset.label)},
                               {"group", QString::fromStdString(preset.group)}});
  }
  return list;
}

QVariantMap DocumentController::selection() const {
  const Component* component = document_.find(selectedId_.toStdString());
  if (component == nullptr) return {};
  const units::LengthUnit unit = currentUnit();
  const int denom = document_.fractionPrecision();
  auto length = [&](std::int64_t nm) { return QString::fromStdString(units::formatLength(nm, unit, denom)); };
  QVariantMap map;
  map.insert(QStringLiteral("id"), selectedId_);
  map.insert(QStringLiteral("type"), QString::fromLatin1(componentTypeKey(component->type)));
  map.insert(QStringLiteral("name"), QString::fromStdString(component->name));
  map.insert(QStringLiteral("materialId"), QString::fromStdString(component->materialId));
  map.insert(QStringLiteral("width"), length(component->widthNm));
  map.insert(QStringLiteral("height"), length(component->heightNm));
  map.insert(QStringLiteral("thickness"), length(component->thicknessNm));
  map.insert(QStringLiteral("radius"), length(component->radiusNm));
  map.insert(QStringLiteral("x"), length(component->placement.xNm));
  map.insert(QStringLiteral("y"), length(component->placement.yNm));
  map.insert(QStringLiteral("z"), length(component->placement.zNm));
  map.insert(QStringLiteral("rx"), QString::fromStdString(units::formatAngle(component->placement.rxDeg)));
  map.insert(QStringLiteral("ry"), QString::fromStdString(units::formatAngle(component->placement.ryDeg)));
  map.insert(QStringLiteral("rz"), QString::fromStdString(units::formatAngle(component->placement.rzDeg)));
  map.insert(QStringLiteral("visible"), component->visible);
  map.insert(QStringLiteral("locked"), component->locked);
  map.insert(QStringLiteral("notes"), QString::fromStdString(component->notes));
  return map;
}

void DocumentController::newProject() {
  document_.newProject(document_.displayUnit(), document_.fractionPrecision());
  selectedId_.clear();
  projectPath_.clear();
  status_ = tr("New project");
  touch(true);
}

void DocumentController::makeDemo() {
  document_.makeDemoTable();
  selectedId_.clear();
  projectPath_.clear();
  status_ = tr("Demo table placed as separate boards");
  touch(true);
}

void DocumentController::addBoard() {
  selectedId_ = QString::fromStdString(document_.addBoard());
  status_ = tr("Board added");
  touch(false);
}

void DocumentController::addBox() {
  selectedId_ = QString::fromStdString(document_.addBox());
  status_ = tr("Box added");
  touch(false);
}

void DocumentController::addCylinder() {
  selectedId_ = QString::fromStdString(document_.addCylinder());
  status_ = tr("Cylinder added");
  touch(false);
}

void DocumentController::duplicateSelected() {
  if (selectedId_.isEmpty()) return;
  std::string created;
  std::string error;
  if (!document_.duplicate(selectedId_.toStdString(), created, error)) {
    report(error, false);
    return;
  }
  selectedId_ = QString::fromStdString(created);
  status_ = tr("Duplicated");
  touch(false);
}

void DocumentController::deleteSelected() {
  if (selectedId_.isEmpty()) return;
  std::string error;
  if (!document_.remove(selectedId_.toStdString(), error)) {
    report(error, false);
    return;
  }
  selectedId_.clear();
  status_ = tr("Deleted");
  touch(false);
}

void DocumentController::select(const QString& id) {
  if (id == selectedId_) return;
  if (!id.isEmpty() && document_.find(id.toStdString()) == nullptr) return;
  selectedId_ = id;
  emit documentChanged();
}

void DocumentController::undo() {
  std::string error;
  if (!document_.undo(error)) return;
  status_ = tr("Undo");
  touch(false);
}

void DocumentController::redo() {
  std::string error;
  if (!document_.redo(error)) return;
  status_ = tr("Redo");
  touch(false);
}

bool DocumentController::saveProject(const QUrl& url) {
  QString path = url.toLocalFile();
  if (path.isEmpty()) {
    report("storage.io", true);
    return false;
  }
  if (!path.endsWith(".timberlab")) path += ".timberlab";
  const auto result = storage::saveProject(path.toStdString(), document_);
  if (!result.ok) {
    report(result.error, true);
    return false;
  }
  document_.markSaved();
  projectPath_ = path;
  status_ = tr("Saved");
  emit documentChanged();
  emit statusChanged();
  return true;
}

bool DocumentController::saveCurrent() {
  if (projectPath_.isEmpty()) return false;
  return saveProject(QUrl::fromLocalFile(projectPath_));
}

bool DocumentController::loadProject(const QUrl& url) {
  const QString path = url.toLocalFile();
  Document loaded;
  const auto result = storage::loadProject(path.toStdString(), loaded);
  if (!result.ok) {
    report(result.error, true);
    return false;
  }
  document_ = std::move(loaded);
  document_.markSaved();
  projectPath_ = path;
  selectedId_.clear();
  status_ = tr("Opened");
  touch(true);
  return true;
}

bool DocumentController::setParameterFromText(const QString& id, const QString& key, const QString& text) {
  const auto parsed = units::parseLength(text.toStdString(), currentUnit());
  if (!parsed.ok) {
    report(parsed.error, false);
    return false;
  }
  std::string error;
  if (!document_.setDimension(id.toStdString(), key.toStdString(), parsed.nm, error)) {
    report(error, false);
    return false;
  }
  status_ = tr("Dimension updated");
  touch(false);
  return true;
}

bool DocumentController::setTranslationFromText(const QString& id, const QString& axis, const QString& text) {
  const auto parsed = units::parseLength(text.toStdString(), currentUnit());
  if (!parsed.ok) {
    report(parsed.error, false);
    return false;
  }
  std::string error;
  if (!document_.setTranslation(id.toStdString(), axis.toStdString(), parsed.nm, error)) {
    report(error, false);
    return false;
  }
  status_ = tr("Position updated");
  touch(false);
  return true;
}

bool DocumentController::setRotationFromText(const QString& id, const QString& axis, const QString& text) {
  const auto parsed = units::parseAngle(text.toStdString());
  if (!parsed.ok) {
    report(parsed.error, false);
    return false;
  }
  std::string error;
  if (!document_.setRotation(id.toStdString(), axis.toStdString(), parsed.degrees, error)) {
    report(error, false);
    return false;
  }
  status_ = tr("Rotation updated");
  touch(false);
  return true;
}

void DocumentController::setPartName(const QString& id, const QString& name) {
  std::string error;
  if (!document_.setName(id.toStdString(), name.trimmed().toStdString(), error)) {
    report(error, false);
    return;
  }
  touch(false);
}

void DocumentController::setMaterial(const QString& id, const QString& materialId) {
  std::string error;
  if (!document_.setMaterial(id.toStdString(), materialId.toStdString(), error)) {
    report(error, false);
    return;
  }
  status_ = tr("Material assigned");
  touch(false);
}

void DocumentController::setVisible(const QString& id, bool visible) {
  std::string error;
  if (!document_.setVisible(id.toStdString(), visible, error)) {
    report(error, false);
    return;
  }
  touch(false);
}

void DocumentController::setLocked(const QString& id, bool locked) {
  std::string error;
  if (!document_.setLocked(id.toStdString(), locked, error)) {
    report(error, false);
    return;
  }
  touch(false);
}

void DocumentController::setNotes(const QString& id, const QString& notes) {
  std::string error;
  if (!document_.setNotes(id.toStdString(), notes.toStdString(), error)) {
    report(error, false);
    return;
  }
  touch(false);
}

void DocumentController::setDisplayUnit(const QString& unitKey) {
  std::string error;
  if (!document_.setDisplayUnit(unitKey.toStdString(), error)) {
    report(error, false);
    return;
  }
  rememberPreference("display_unit", unitKey);
  status_ = tr("Display unit changed. Stored geometry is unchanged.");
  touch(false);
}

void DocumentController::setFractionPrecision(int denom) {
  std::string error;
  if (!document_.setFractionPrecision(denom, error)) {
    report(error, false);
    return;
  }
  rememberPreference("fraction_precision", QString::number(denom));
  touch(false);
}

void DocumentController::applyThicknessPreset(const QString& presetId) {
  if (selectedId_.isEmpty()) return;
  const auto found = std::find_if(thicknesses_.begin(), thicknesses_.end(), [&](const auto& preset) {
    return QString::fromStdString(preset.id) == presetId;
  });
  if (found == thicknesses_.end()) return;
  const Component* component = document_.find(selectedId_.toStdString());
  if (component == nullptr) return;
  const char* key = component->type == ComponentType::Cylinder ? "radius" : "thickness";
  std::string error;
  if (!document_.setDimension(selectedId_.toStdString(), key, found->thicknessNm, error)) {
    report(error, false);
    return;
  }
  status_ = tr("Preset applied");
  touch(false);
}

std::vector<ScenePart> DocumentController::copyScene() const {
  std::lock_guard<std::mutex> lock(sceneMutex_);
  return scene_;
}

bool DocumentController::worldBounds(const QString& componentId, QVector3D& minimum, QVector3D& maximum) const {
  bool any = false;
  for (const Component& component : document_.components()) {
    if (!component.visible) continue;
    if (!componentId.isEmpty() && QString::fromStdString(component.id) != componentId) continue;
    double matrix[16];
    placementMatrix(component.placement, matrix);
    const LocalBox box = localBox(component);
    for (int corner = 0; corner < 8; ++corner) {
      const double x = box.originXmm + ((corner & 1) ? box.sizeXmm : 0.0);
      const double y = box.originYmm + ((corner & 2) ? box.sizeYmm : 0.0);
      const double z = box.originZmm + ((corner & 4) ? box.sizeZmm : 0.0);
      double ox = 0, oy = 0, oz = 0;
      transformPoint(matrix, x, y, z, ox, oy, oz);
      const QVector3D point(static_cast<float>(ox), static_cast<float>(oy), static_cast<float>(oz));
      if (!any) {
        minimum = maximum = point;
        any = true;
      } else {
        minimum.setX(std::min(minimum.x(), point.x()));
        minimum.setY(std::min(minimum.y(), point.y()));
        minimum.setZ(std::min(minimum.z(), point.z()));
        maximum.setX(std::max(maximum.x(), point.x()));
        maximum.setY(std::max(maximum.y(), point.y()));
        maximum.setZ(std::max(maximum.z(), point.z()));
      }
    }
  }
  return any;
}

void DocumentController::touch(bool frame) {
  if (!selectedId_.isEmpty() && document_.find(selectedId_.toStdString()) == nullptr) selectedId_.clear();
  components_.refresh();
  rebuildScene();
  ++revision_;
  emit documentChanged();
  emit statusChanged();
  if (frame) emit requestFrameAll();
}

void DocumentController::report(const std::string& code, bool dialog) {
  status_ = messageFor(QString::fromStdString(code));
  emit statusChanged();
  if (dialog) emit errorRaised(status_);
}

void DocumentController::rememberPreference(const QString& key, const QString& value) {
  if (preferences_ && preferences_->ok()) preferences_->set(key.toStdString(), value.toStdString());
}

void DocumentController::rebuildScene() {
  std::vector<ScenePart> next;
  next.reserve(document_.components().size());
  for (const Component& component : document_.components()) {
    geometry::SolidMesh mesh = geometry::meshFor(component, quality_);
    if (!mesh.ok) {
      report(mesh.error, false);
      continue;
    }
    ScenePart part;
    part.id = QString::fromStdString(component.id);
    part.interleaved = std::move(mesh.interleaved);
    part.indices = std::move(mesh.indices);
    const materials::CatalogEntry* entry = catalog_.find(component.materialId);
    part.color = entry != nullptr ? colorFromHex(entry->visualHex) : QVector3D(0.72f, 0.58f, 0.40f);
    part.model = matrixFrom(component.placement);
    part.visible = component.visible;
    next.push_back(std::move(part));
  }
  std::lock_guard<std::mutex> lock(sceneMutex_);
  scene_ = std::move(next);
}

QString DocumentController::messageFor(const QString& code) const {
  if (code == "units.empty") return tr("Enter a dimension.");
  if (code == "units.invalid" || code == "units.trailing") return tr("Could not read that dimension.");
  if (code == "units.overflow") return tr("That dimension is too large to store.");
  if (code == "dimension.non_positive") return tr("Dimensions must be greater than zero.");
  if (code == "dimension.too_large") return tr("That value is outside the supported range.");
  if (code == "dimension.not_applicable") return tr("That dimension does not apply to this part.");
  if (code == "component.locked") return tr("Unlock the part before editing it.");
  if (code == "component.missing") return tr("That part is no longer in the project.");
  if (code == "name.empty") return tr("The name cannot be empty.");
  if (code == "storage.io") return tr("Could not read or write the project file.");
  if (code == "storage.backup") return tr("Could not update the project backup.");
  if (code == "storage.format" || code == "storage.json") return tr("This file is not a TimberLab project.");
  if (code == "storage.checksum") return tr("The project file failed its integrity check.");
  if (code == "storage.version") return tr("This project uses a newer file version.");
  if (code == "angle.invalid" || code == "angle.trailing") return tr("Could not read that angle.");
  if (code == "geometry.invalid" || code == "geometry.empty" || code == "geometry.kernel" ||
      code == "geometry.degenerate") {
    return tr("The solid could not be built.");
  }
  if (code == "preferences.open") return tr("Preferences could not be opened. The project still works.");
  return code;
}

}  // namespace timberlab::ui
