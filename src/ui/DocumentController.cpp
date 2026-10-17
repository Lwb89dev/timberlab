#include "DocumentController.hpp"

#include "AmberBridge.hpp"
#include "timberlab/core/manufacturing.hpp"
#include "timberlab/core/transform.hpp"
#include "timberlab/platform/paths.hpp"
#include "timberlab/storage/archive.hpp"

#include <openssl/crypto.h>

#include <QFile>
#include <QLocale>
#include <QMatrix4x4>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <exception>
#include <limits>
#include <filesystem>
#include <random>
#include <string>

namespace timberlab::ui {
namespace {

std::string makeLibraryId() {
  thread_local std::mt19937_64 rng{std::random_device{}()};
  std::uniform_int_distribution<std::uint64_t> dist;
  char buffer[24];
  std::snprintf(buffer, sizeof(buffer), "user_%016llx", static_cast<unsigned long long>(dist(rng)));
  return buffer;
}

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

QString signedTerm(std::int64_t coefficient, const QString& source, bool first) {
  if (coefficient == 1) {
    if (first) return source;
    return QStringLiteral("+ ") + source;
  }
  if (coefficient == -1) return QStringLiteral("− ") + source;
  if (coefficient > 0) {
    const QString body = QString::number(static_cast<qlonglong>(coefficient)) + QStringLiteral(" × ") + source;
    if (first) return body;
    return QStringLiteral("+ ") + body;
  }
  if (coefficient == std::numeric_limits<std::int64_t>::min()) {
    return QString::number(static_cast<qlonglong>(coefficient)) + QStringLiteral(" × ") + source;
  }
  const QString body = QString::number(static_cast<qlonglong>(-coefficient)) + QStringLiteral(" × ") + source;
  return QStringLiteral("− ") + body;
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

DocumentController::DocumentController(QObject* parent)
    : QObject(parent), components_(this), relay_(this), bunkerTimer_(this) {
  bunkerTimer_.setSingleShot(true);
  connect(&bunkerTimer_, &QTimer::timeout, this, [this] { openNextBunker(); });
  connect(&relay_, &RelaySocket::connected, this, &DocumentController::onBunkerConnected);
  connect(&relay_, &RelaySocket::textMessage, this, &DocumentController::onBunkerText);
  connect(&relay_, &RelaySocket::failed, this, [this](const QString&) { onBunkerSocketFailed(); });
  connect(&relay_, &RelaySocket::disconnected, this, [this] { onBunkerSocketFailed(); });
}

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
  library_ = std::make_unique<storage::Library>(dir + "/library.sqlite");
  reloadLibrary();

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
  if (library_ && !library_->ok()) report(library_->error(), false);
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
  for (const storage::UserMaterial& material : userMaterials_) {
    list.push_back(QVariantMap{{"id", QString::fromStdString(material.id)},
                               {"name", QString::fromStdString(material.name)},
                               {"category", QStringLiteral("user")}});
  }
  return list;
}

QVariantList DocumentController::userMaterials() const {
  QVariantList list;
  for (const storage::UserMaterial& material : userMaterials_) {
    list.push_back(QVariantMap{{"id", QString::fromStdString(material.id)},
                               {"name", QString::fromStdString(material.name)},
                               {"visualHex", QString::fromStdString(material.visualHex)}});
  }
  return list;
}

QVariantList DocumentController::userPresets() const {
  QVariantList list;
  for (const storage::UserPreset& preset : userPresets_) {
    list.push_back(QVariantMap{{"id", QString::fromStdString(preset.id)},
                               {"label", QString::fromStdString(preset.label)},
                               {"group", QString::fromStdString(preset.group)}});
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
  for (const storage::UserPreset& preset : userPresets_) {
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
  map.insert(QStringLiteral("parentId"), QString::fromStdString(component->parentId));
  map.insert(QStringLiteral("role"), QString::fromStdString(component->role));
  map.insert(QStringLiteral("notes"), QString::fromStdString(component->notes));
  QVariantList drives;
  for (const constraints::Binding& binding : document_.bindings()) {
    if (binding.partId != component->id) continue;
    QVariantMap drive;
    drive.insert(QStringLiteral("field"), QString::fromStdString(binding.field));
    drive.insert(QStringLiteral("label"), fieldLabel(binding.field));
    drive.insert(QStringLiteral("formula"), formulaFor(binding));
    drives.push_back(std::move(drive));
  }
  map.insert(QStringLiteral("drives"), drives);
  return map;
}

QVariantList DocumentController::partChoices() const {
  QVariantList list;
  for (const Component& component : document_.components()) {
    if (QString::fromStdString(component.id) == selectedId_) continue;
    list.push_back(QVariantMap{{"id", QString::fromStdString(component.id)},
                               {"name", QString::fromStdString(component.name)}});
  }
  return list;
}

QVariantList DocumentController::parentChoices() const {
  QVariantList list;
  list.push_back(QVariantMap{{"id", QString()}, {"name", tr("None")}});
  for (const Component& component : document_.components()) {
    if (QString::fromStdString(component.id) == selectedId_) continue;
    list.push_back(QVariantMap{{"id", QString::fromStdString(component.id)},
                               {"name", QString::fromStdString(component.name)}});
  }
  return list;
}

QVariantList DocumentController::joints() const {
  QVariantList list;
  const std::string selected = selectedId_.toStdString();
  if (selected.empty()) return list;
  const units::LengthUnit unit = currentUnit();
  const int denom = document_.fractionPrecision();
  auto text = [&](std::int64_t nm) { return QString::fromStdString(units::formatLength(nm, unit, denom)); };
  for (const Joint& joint : document_.joints()) {
    if (joint.partA != selected && joint.partB != selected) continue;
    const std::string mateId = joint.partA == selected ? joint.partB : joint.partA;
    const Component* mate = document_.find(mateId);
    QVariantMap map;
    map.insert(QStringLiteral("id"), QString::fromStdString(joint.id));
    map.insert(QStringLiteral("type"), QString::fromLatin1(jointTypeKey(joint.type)));
    map.insert(QStringLiteral("mateId"), QString::fromStdString(mateId));
    map.insert(QStringLiteral("mateName"), mate == nullptr ? QString() : QString::fromStdString(mate->name));
    map.insert(QStringLiteral("length"), text(joint.lengthNm));
    map.insert(QStringLiteral("width"), text(joint.widthNm));
    map.insert(QStringLiteral("thickness"), text(joint.thicknessNm));
    map.insert(QStringLiteral("depth"), text(joint.depthNm));
    map.insert(QStringLiteral("offset"), text(joint.offsetNm));
    map.insert(QStringLiteral("offset2"), text(joint.offset2Nm));
    map.insert(QStringLiteral("count"), QString::number(joint.count));
    list.push_back(map);
  }
  return list;
}

QVariantList DocumentController::parameters() const {
  QVariantList list;
  const units::LengthUnit unit = currentUnit();
  const int denom = document_.fractionPrecision();
  for (const constraints::Parameter& parameter : document_.parameters()) {
    list.push_back(QVariantMap{{"id", QString::fromStdString(parameter.id)},
                               {"label", parameterLabel(parameter)},
                               {"value", QString::fromStdString(units::formatLength(parameter.valueNm, unit, denom))}});
  }
  return list;
}

QVariantList DocumentController::driveFields() const {
  const auto row = [](const char* id, const QString& label) {
    return QVariantMap{{"id", QString::fromLatin1(id)}, {"label", label}};
  };
  QVariantList fields;
  fields.push_back(row("width", tr("Width")));
  fields.push_back(row("thickness", tr("Thickness")));
  fields.push_back(row("height", tr("Height")));
  fields.push_back(row("radius", tr("Radius")));
  fields.push_back(row("x", QStringLiteral("X")));
  fields.push_back(row("y", QStringLiteral("Y")));
  fields.push_back(row("z", QStringLiteral("Z")));
  return fields;
}

QString DocumentController::fieldLabel(const std::string& field) const {
  if (field == "width") return tr("Width");
  if (field == "thickness") return tr("Thickness");
  if (field == "height") return tr("Height");
  if (field == "radius") return tr("Radius");
  if (field == "x") return QStringLiteral("X");
  if (field == "y") return QStringLiteral("Y");
  if (field == "z") return QStringLiteral("Z");
  return QString::fromStdString(field);
}

QString DocumentController::instanceParameterLabel(const std::string& id, const std::string& role) const {
  if (id.starts_with("table_")) {
    if (role == "width") return tr("Table width");
    if (role == "depth") return tr("Table depth");
    if (role == "leg") return tr("Table leg");
    if (role == "inset") return tr("Table inset");
    if (role == "legHeight") return tr("Table leg height");
    if (role == "topThickness") return tr("Table top thickness");
    if (role == "apronHeight") return tr("Table apron height");
    if (role == "apronThickness") return tr("Table apron thickness");
    if (role == "apronDrop") return tr("Table apron drop");
    return {};
  }
  if (id.starts_with("carcass_")) {
    if (role == "width") return tr("Cabinet width");
    if (role == "depth") return tr("Cabinet depth");
    if (role == "height") return tr("Cabinet height");
    if (role == "side") return tr("Cabinet side");
    if (role == "panel") return tr("Cabinet panel");
    if (role == "back") return tr("Cabinet back");
    return {};
  }
  if (id.starts_with("drawer_")) {
    if (role == "width") return tr("Drawer width");
    if (role == "depth") return tr("Drawer depth");
    if (role == "height") return tr("Drawer height");
    if (role == "side") return tr("Drawer side");
    if (role == "bottom") return tr("Drawer bottom");
    return {};
  }
  if (id.starts_with("frame_")) {
    if (role == "width") return tr("Frame width");
    if (role == "height") return tr("Frame height");
    if (role == "stock") return tr("Frame stock");
    if (role == "depth") return tr("Frame depth");
    return {};
  }
  if (id.starts_with("shelving_")) {
    if (role == "width") return tr("Shelf width");
    if (role == "depth") return tr("Shelf depth");
    if (role == "side") return tr("Shelf side");
    if (role == "shelf") return tr("Shelf thickness");
    if (role == "bay") return tr("Shelf bay");
    return {};
  }
  if (id.starts_with("bedside_")) {
    if (role == "width") return tr("Bedside width");
    if (role == "depth") return tr("Bedside depth");
    if (role == "height") return tr("Bedside height");
    if (role == "leg") return tr("Bedside leg");
    if (role == "top") return tr("Bedside top");
    if (role == "drawerHeight") return tr("Bedside drawer");
    if (role == "side") return tr("Bedside side");
    if (role == "bottom") return tr("Bedside bottom");
    return {};
  }
  return {};
}

QString DocumentController::parameterLabel(const constraints::Parameter& parameter) const {
  if (parameter.id == "table.width") return tr("Width");
  if (parameter.id == "table.depth") return tr("Depth");
  if (parameter.id == "table.leg") return tr("Leg");
  if (parameter.id == "table.inset") return tr("Inset");
  if (parameter.id == "table.legHeight") return tr("Leg height");
  if (parameter.id == "table.topThickness") return tr("Top thickness");
  if (parameter.id == "table.apronHeight") return tr("Apron height");
  if (parameter.id == "table.apronThickness") return tr("Apron thickness");
  if (parameter.id == "table.apronDrop") return tr("Apron drop");
  const auto dot = parameter.id.rfind('.');
  if (dot != std::string::npos) {
    const QString composed = instanceParameterLabel(parameter.id, parameter.id.substr(dot + 1));
    if (!composed.isNull()) return composed;
  }
  return QString::fromStdString(parameter.name);
}

QString DocumentController::termSource(const constraints::Term& term) const {
  if (!term.parameterId.empty()) {
    for (const constraints::Parameter& parameter : document_.parameters()) {
      if (parameter.id == term.parameterId) return parameterLabel(parameter);
    }
    return QString::fromStdString(term.parameterId);
  }
  const Component* part = document_.find(term.partId);
  const QString name = part == nullptr ? QString::fromStdString(term.partId) : QString::fromStdString(part->name);
  return name + QStringLiteral(" ") + fieldLabel(term.field);
}

QString DocumentController::formulaFor(const constraints::Binding& binding) const {
  QStringList pieces;
  if (binding.constantNm != 0) {
    const QString constant =
        QString::fromStdString(units::formatLength(binding.constantNm, currentUnit(), document_.fractionPrecision()));
    pieces.push_back(constant);
  }
  for (const constraints::Term& term : binding.terms) {
    const bool first = pieces.isEmpty();
    pieces.push_back(signedTerm(term.coefficient, termSource(term), first));
  }
  if (pieces.isEmpty()) return QStringLiteral("0");
  return pieces.join(QStringLiteral(" "));
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
  status_ = tr("Demo table placed. Its stock sizes are parameters.");
  touch(true);
}

void DocumentController::addBoard() {
  const std::string id = document_.addBoard();
  if (id.empty()) {
    report("storage.too_many", false);
    return;
  }
  selectedId_ = QString::fromStdString(id);
  status_ = tr("Board added");
  touch(false);
}

void DocumentController::addBox() {
  const std::string id = document_.addBox();
  if (id.empty()) {
    report("storage.too_many", false);
    return;
  }
  selectedId_ = QString::fromStdString(id);
  status_ = tr("Box added");
  touch(false);
}

void DocumentController::addCylinder() {
  const std::string id = document_.addCylinder();
  if (id.empty()) {
    report("storage.too_many", false);
    return;
  }
  selectedId_ = QString::fromStdString(id);
  status_ = tr("Cylinder added");
  touch(false);
}

void DocumentController::addAssembly(const QString& kind) {
  const std::size_t before = document_.components().size();
  std::string error;
  if (!document_.addAssembly(kind.toStdString(), error)) {
    report(error, false);
    return;
  }
  if (before < document_.components().size()) {
    selectedId_ = QString::fromStdString(document_.components()[before].id);
  }
  if (kind == QStringLiteral("table")) status_ = tr("Table added. Its sizes are parameters.");
  else if (kind == QStringLiteral("carcass")) status_ = tr("Cabinet added. Its sizes are parameters.");
  else if (kind == QStringLiteral("drawer")) status_ = tr("Drawer box added. Its sizes are parameters.");
  else if (kind == QStringLiteral("frame")) status_ = tr("Frame added. Its sizes are parameters.");
  else if (kind == QStringLiteral("shelving")) status_ = tr("Shelving added. Its sizes are parameters.");
  else if (kind == QStringLiteral("bedside")) status_ = tr("Bedside table added. Its sizes are parameters.");
  touch(true);
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

bool DocumentController::setDriverFromText(const QString& parameterId, const QString& text) {
  const auto parsed = units::parseLength(text.toStdString(), currentUnit());
  if (!parsed.ok) {
    report(parsed.error, false);
    return false;
  }
  std::string error;
  if (!document_.setParameter(parameterId.toStdString(), parsed.nm, error)) {
    report(error, false);
    return false;
  }
  status_ = tr("Parameter updated");
  touch(false);
  return true;
}

bool DocumentController::drivePartField(const QString& partId, const QString& field, const QString& parameterId) {
  std::string error;
  if (!document_.bindField(partId.toStdString(), field.toStdString(), parameterId.toStdString(), error)) {
    report(error, false);
    return false;
  }
  status_ = tr("Field driven");
  touch(false);
  return true;
}

bool DocumentController::releasePartField(const QString& partId, const QString& field) {
  std::string error;
  if (!document_.unbindField(partId.toStdString(), field.toStdString(), error)) {
    report(error, false);
    return false;
  }
  status_ = tr("Field released");
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
  std::int64_t nm = 0;
  bool known = false;
  for (const auto& preset : thicknesses_) {
    if (QString::fromStdString(preset.id) != presetId) continue;
    nm = preset.thicknessNm;
    known = true;
  }
  for (const storage::UserPreset& preset : userPresets_) {
    if (QString::fromStdString(preset.id) != presetId) continue;
    nm = preset.thicknessNm;
    known = true;
  }
  if (!known) return;
  const Component* component = document_.find(selectedId_.toStdString());
  if (component == nullptr) return;
  const char* key = component->type == ComponentType::Cylinder ? "radius" : "thickness";
  std::string error;
  if (!document_.setDimension(selectedId_.toStdString(), key, nm, error)) {
    report(error, false);
    return;
  }
  status_ = tr("Preset applied");
  touch(false);
}

bool DocumentController::addUserMaterial(const QString& name, const QString& visualHex) {
  if (!library_ || !library_->ok()) {
    report(library_ && !library_->error().empty() ? library_->error() : "library.open", false);
    return false;
  }
  storage::UserMaterial material;
  material.name = name.trimmed().toStdString();
  const QString hex = visualHex.trimmed().isEmpty() ? QStringLiteral("#A67C52") : visualHex.trimmed();
  material.visualHex = hex.toStdString();
  std::string error;
  for (int attempt = 0; attempt < 4; ++attempt) {
    material.id = makeLibraryId();
    if (library_->addMaterial(material, error)) {
      reloadLibrary();
      status_ = tr("Material added to the library.");
      touch(false);
      return true;
    }
    if (error != "library.duplicate") break;
  }
  report(error, false);
  return false;
}

bool DocumentController::addUserPreset(const QString& thicknessText) {
  if (!library_ || !library_->ok()) {
    report(library_ && !library_->error().empty() ? library_->error() : "library.open", false);
    return false;
  }
  const QString label = thicknessText.trimmed();
  const auto parsed = units::parseLength(label.toStdString(), currentUnit());
  if (!parsed.ok) {
    report(parsed.error, false);
    return false;
  }
  storage::UserPreset preset;
  preset.label = label.toStdString();
  preset.group = "user";
  preset.thicknessNm = parsed.nm;
  std::string error;
  for (int attempt = 0; attempt < 4; ++attempt) {
    preset.id = makeLibraryId();
    if (library_->addPreset(preset, error)) {
      reloadLibrary();
      status_ = tr("Preset added to the library.");
      touch(false);
      return true;
    }
    if (error != "library.duplicate") break;
  }
  report(error, false);
  return false;
}

bool DocumentController::removeUserMaterial(const QString& id) {
  if (!library_ || !library_->ok()) {
    report(library_ && !library_->error().empty() ? library_->error() : "library.open", false);
    return false;
  }
  std::string error;
  if (!library_->removeMaterial(id.toStdString(), error)) {
    report(error, false);
    return false;
  }
  reloadLibrary();
  status_ = tr("Material removed from the library.");
  touch(false);
  return true;
}

bool DocumentController::removeUserPreset(const QString& id) {
  if (!library_ || !library_->ok()) {
    report(library_ && !library_->error().empty() ? library_->error() : "library.open", false);
    return false;
  }
  std::string error;
  if (!library_->removePreset(id.toStdString(), error)) {
    report(error, false);
    return false;
  }
  reloadLibrary();
  status_ = tr("Preset removed from the library.");
  touch(false);
  return true;
}

bool DocumentController::addJoint(const QString& typeKey, const QString& selectedId, const QString& mateId) {
  std::string error;
  const std::string id =
      document_.addJoint(typeKey.toStdString(), selectedId.toStdString(), mateId.toStdString(), error);
  if (id.empty()) {
    report(error, false);
    return false;
  }
  status_ = tr("Joint added");
  touch(false);
  return true;
}

bool DocumentController::setJointFromText(const QString& id, const QString& key, const QString& text) {
  std::int64_t nm = 0;
  if (key == QStringLiteral("count")) {
    bool ok = false;
    const int count = text.trimmed().toInt(&ok);
    if (!ok) {
      report("joint.parameter", false);
      return false;
    }
    nm = count;
  } else {
    const auto parsed = units::parseLength(text.toStdString(), currentUnit());
    if (!parsed.ok) {
      report(parsed.error, false);
      return false;
    }
    nm = parsed.nm;
  }
  std::string error;
  if (!document_.setJointParameter(id.toStdString(), key.toStdString(), nm, error)) {
    report(error, false);
    return false;
  }
  status_ = tr("Joint updated");
  touch(false);
  return true;
}

void DocumentController::setParent(const QString& id, const QString& parentId) {
  std::string error;
  if (!document_.setParent(id.toStdString(), parentId.toStdString(), error)) {
    report(error, false);
    return;
  }
  status_ = tr("Parent updated");
  touch(false);
}

void DocumentController::setRole(const QString& id, const QString& role) {
  std::string error;
  if (!document_.setRole(id.toStdString(), role.toStdString(), error)) {
    report(error, false);
    return;
  }
  status_ = tr("Role updated");
  touch(false);
}

bool DocumentController::exportCutList(const QUrl& url, const QString& allowanceText) {
  const QString path = url.toLocalFile();
  if (path.isEmpty()) {
    report("storage.io", false);
    return false;
  }
  const auto parsed = units::parseLength(allowanceText.toStdString(), currentUnit());
  if (!parsed.ok) {
    report(parsed.error, false);
    return false;
  }
  std::vector<manufacturing::CutPiece> pieces;
  std::string error;
  if (!manufacturing::cutList(document_.components(), parsed.nm, pieces, error)) {
    report(error, false);
    return false;
  }
  const auto bom = manufacturing::billOfMaterials(pieces);
  const bool asJson = path.endsWith(QStringLiteral(".json"), Qt::CaseInsensitive);
  const std::string text =
      asJson ? manufacturing::cutListJson(parsed.nm, pieces, bom) : manufacturing::cutListCsv(pieces);
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    report("storage.io", false);
    return false;
  }
  const QByteArray bytes = QByteArray::fromStdString(text);
  if (file.write(bytes) != bytes.size()) {
    report("storage.io", false);
    return false;
  }
  status_ = parsed.nm == 0 ? tr("Cut list written. Rough size equals finished size.") : tr("Cut list written.");
  emit statusChanged();
  return true;
}

bool DocumentController::exportFrontView(const QUrl& url) { return exportDrawing(url, QStringLiteral("front")); }

bool DocumentController::exportDrawing(const QUrl& url, const QString& kind) {
  const QString path = url.toLocalFile();
  if (path.isEmpty()) {
    report("storage.io", false);
    return false;
  }
  const Component* component = document_.find(selectedId_.toStdString());
  if (component == nullptr) {
    report("component.missing", false);
    return false;
  }
  std::string error;
  const std::vector<Joint>& joints = document_.joints();
  const std::vector<Component>& parts = document_.components();
  std::string text;
  QString written;
  if (kind == QLatin1String("front")) {
    text = manufacturing::frontViewSvg(*component, joints, parts, error);
    written = tr("Front view written. It is a tessellation wire, not a dimensioned drawing.");
  } else if (kind == QLatin1String("top")) {
    text = manufacturing::topViewSvg(*component, joints, parts, error);
    written = tr("Top view written. It is a tessellation wire, not a dimensioned drawing.");
  } else if (kind == QLatin1String("right")) {
    text = manufacturing::rightViewSvg(*component, joints, parts, error);
    written = tr("Right view written. It is a tessellation wire, not a dimensioned drawing.");
  } else if (kind == QLatin1String("sheet")) {
    text = manufacturing::sheetSvg(*component, joints, parts, error);
    written = tr("Sheet written. It is a tessellation wire, not a dimensioned drawing.");
  } else if (kind == QLatin1String("dxf")) {
    text = manufacturing::wireDxf(*component, joints, parts, error);
    written = tr("DXF wire written. It is a tessellation wire, not a dimensioned drawing.");
  } else if (kind == QLatin1String("pdf")) {
    text = manufacturing::wirePdf(*component, joints, parts, error);
    written = tr("PDF wire written. It is a tessellation wire, not a dimensioned drawing.");
  } else {
    report("drawing.kind", false);
    return false;
  }
  if (text.empty()) {
    report(error.empty() ? "geometry.invalid" : error, false);
    return false;
  }
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    report("storage.io", false);
    return false;
  }
  const QByteArray bytes = QByteArray::fromStdString(text);
  if (file.write(bytes) != bytes.size()) {
    report("storage.io", false);
    return false;
  }
  status_ = written;
  emit statusChanged();
  return true;
}

bool DocumentController::restoreBackup() {
  if (projectPath_.isEmpty()) {
    report("storage.io", true);
    return false;
  }
  Document loaded;
  const auto result = storage::restoreBackup(projectPath_.toStdString(), loaded);
  if (!result.ok) {
    report(result.error, true);
    return false;
  }
  document_ = std::move(loaded);
  document_.markSaved();
  selectedId_.clear();
  status_ = tr("Restored the previous save. The backup was checked before it replaced the project.");
  touch(true);
  return true;
}

void DocumentController::removeJoint(const QString& id) {
  std::string error;
  if (!document_.removeJoint(id.toStdString(), error)) {
    report(error, false);
    return;
  }
  status_ = tr("Joint removed");
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

void DocumentController::reloadLibrary() {
  userMaterials_.clear();
  userPresets_.clear();
  if (!library_ || !library_->ok()) return;
  userMaterials_ = library_->materials();
  userPresets_ = library_->presets();
}

QVector3D DocumentController::colorForMaterial(const std::string& id) const {
  if (const materials::CatalogEntry* entry = catalog_.find(id)) return colorFromHex(entry->visualHex);
  for (const storage::UserMaterial& material : userMaterials_) {
    if (material.id == id) return colorFromHex(material.visualHex);
  }
  return {0.72f, 0.58f, 0.40f};
}

void DocumentController::rebuildScene() {
  std::vector<ScenePart> next;
  next.reserve(document_.components().size());
  for (const Component& component : document_.components()) {
    geometry::SolidMesh mesh = geometry::meshFor(component, document_.joints(), document_.components(), quality_);
    if (!mesh.ok) {
      report(mesh.error, false);
      continue;
    }
    ScenePart part;
    part.id = QString::fromStdString(component.id);
    part.interleaved = std::move(mesh.interleaved);
    part.indices = std::move(mesh.indices);
    part.color = colorForMaterial(component.materialId);
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
  if (code == "component.cycle") return tr("That parent would make a loop.");
  if (code == "component.role") return tr("A role is a short token: letters, digits, dot, underscore, or hyphen.");
  if (code == "name.empty") return tr("The name cannot be empty.");
  if (code == "name.too_long") return tr("That name is too long.");
  if (code == "notes.too_long") return tr("Those notes are too long.");
  if (code == "text.too_long") return tr("That text is too long.");
  if (code == "storage.io") return tr("Could not read or write the project file.");
  if (code == "storage.backup") return tr("Could not update the project backup.");
  if (code == "storage.restore") return tr("There is no backup to restore.");
  if (code == "drawing.kind") return tr("That drawing is not available.");
  if (code == "storage.format" || code == "storage.json") return tr("This file is not a TimberLab project.");
  if (code == "storage.checksum") return tr("The project file failed its integrity check.");
  if (code == "storage.version") return tr("This project uses a newer file version.");
  if (code == "storage.too_large") return tr("That project file is too large.");
  if (code == "storage.too_many") return tr("That project has too many entries.");
  if (code == "storage.duplicate") return tr("That project contains a duplicate id.");
  if (code == "angle.invalid" || code == "angle.trailing") return tr("Could not read that angle.");
  if (code == "geometry.invalid" || code == "geometry.empty" || code == "geometry.kernel" ||
      code == "geometry.degenerate") {
    return tr("The solid could not be built.");
  }
  if (code == "preferences.open") return tr("Preferences could not be opened. The project still works.");
  if (code == "library.open") return tr("The library could not be opened. The project still works.");
  if (code == "library.schema") return tr("The library database could not be read.");
  if (code == "library.version") return tr("This library uses a newer database version.");
  if (code == "library.duplicate") return tr("That library entry already exists.");
  if (code == "library.missing") return tr("That library entry is no longer there.");
  if (code == "library.too_many") return tr("That library is full.");
  if (code == "library.range") return tr("That library value is not valid.");
  if (code == "joint.same_part") return tr("A joint needs two different parts.");
  if (code == "joint.not_box") return tr("Joints cut boards and boxes, not cylinders.");
  if (code == "joint.parameter") return tr("That joint does not fit these parts.");
  if (code == "joint.no_effect") return tr("That joint does not change the solid.");
  if (code == "joint.missing") return tr("That joint is no longer in the project.");
  if (code == "parameter.missing") return tr("That parameter is not in the project.");
  if (code == "parameter.driven") return tr("That dimension follows a parameter. Release it to type a number.");
  if (code == "parameter.cycle") return tr("Those dimensions depend on each other.");
  if (code == "parameter.range") return tr("That value makes a part size invalid.");
  if (code == "parameter.unknown") return tr("That field is not driven.");
  if (code == "assembly.unknown") return tr("That assembly is not available.");
  if (code == "signer.nsec") return tr("That secret key is not a valid nsec.");
  if (code == "signer.bunker") return tr("That bunker link is not valid.");
  if (code == "signer.amber") return tr("Amber signs on Android. On this computer, sign in with a secret key or a bunker.");
  if (code == "signer.relay") return tr("The relay did not accept the bunker connection.");
  if (code == "signer.timeout") return tr("The bunker did not answer.");
  if (code == "signer.rejected") return tr("The bunker rejected the connection.");
  if (code == "signer.crypto") return tr("The bunker response could not be decrypted.");
  if (code == "signer.state") return tr("The sign-in session is no longer valid.");
  return tr("Something went wrong.");
}

namespace {

void wipeBytes(QByteArray& bytes, std::string& text) {
  if (!text.empty()) OPENSSL_cleanse(text.data(), text.size());
  text.clear();
  if (!bytes.isEmpty()) OPENSSL_cleanse(bytes.data(), static_cast<std::size_t>(bytes.size()));
  bytes.fill('\0');
  bytes.clear();
}

}  // namespace

void DocumentController::abortBunker() {
  bunkerActive_ = false;
  bunkerTimer_.stop();
  relay_.close();
  bunkerRelays_.clear();
  bunkerRelayIndex_ = -1;
  bunkerHelloSent_ = false;
}

void DocumentController::openNextBunker() {
  if (!bunkerActive_) return;
  bunkerTimer_.stop();
  ++bunkerRelayIndex_;
  if (bunkerRelayIndex_ < 0 || bunkerRelayIndex_ >= bunkerRelays_.size()) {
    finishBunker(bunkerConnectedOnce_ ? "signer.timeout" : "signer.relay");
    return;
  }
  bunkerHelloSent_ = false;
  bunkerRelayUrl_ = bunkerRelays_.at(bunkerRelayIndex_);
  relay_.open(bunkerRelayUrl_);
}

void DocumentController::sendBunkerHello() {
  std::string error;
  std::string sub;
  if (!storage::randomHex(8, sub, error)) {
    finishBunker("signer.crypto");
    return;
  }
  bunkerSub_ = QString::fromStdString(sub);
  storage::Event event;
  if (!signer_.pendingEvent(event)) {
    finishBunker("signer.state");
    return;
  }
  const auto now = std::time(nullptr);
  const std::int64_t since = now > 600 ? static_cast<std::int64_t>(now) - 600 : 0;
  relay_.sendText(QString::fromStdString(storage::relayReqMessage(sub, event.pubkey, since)));
  relay_.sendText(QString::fromStdString(storage::relayEventMessage(event)));
  bunkerHelloSent_ = true;
  bunkerTimer_.start(20000);
}

void DocumentController::onBunkerConnected() {
  if (!bunkerActive_) return;
  bunkerConnectedOnce_ = true;
  sendBunkerHello();
}

void DocumentController::onBunkerSocketFailed() {
  if (!bunkerActive_) return;
  openNextBunker();
}

void DocumentController::onBunkerText(const QString& text) {
  if (!bunkerActive_) return;
  storage::RelayMessage message;
  std::string error;
  if (!storage::parseRelayMessage(text.toStdString(), message, error)) return;
  if (message.type == "AUTH") {
    storage::Event auth;
    const auto now = static_cast<std::int64_t>(std::time(nullptr));
    if (!signer_.signAuth(bunkerRelayUrl_.toStdString(), message.text, now, auth, error)) return;
    relay_.sendText(QString::fromStdString(storage::relayAuthMessage(auth)));
    storage::Event pending;
    if (bunkerHelloSent_ && signer_.pendingEvent(pending)) {
      relay_.sendText(QString::fromStdString(storage::relayEventMessage(pending)));
    }
    bunkerTimer_.start(20000);
    return;
  }
  if (message.type == "OK" && !message.accepted) {
    const QString detail = QString::fromStdString(message.text).left(180);
    finishBunker("signer.rejected");
    if (!detail.isEmpty()) {
      status_ = tr("The bunker rejected the connection.") + QStringLiteral(" ") + detail;
      emit statusChanged();
    }
    return;
  }
  if (message.type == "CLOSED") {
    finishBunker("signer.relay");
    return;
  }
  if (message.type != "EVENT") return;
  if (message.sub != bunkerSub_.toStdString()) return;
  const auto now = static_cast<std::int64_t>(std::time(nullptr));
  const auto result = signer_.acceptRemoteEvent(message.event, now, error);
  if (result == storage::RemoteResult::Ignored) return;
  if (result == storage::RemoteResult::AuthRequired) {
    status_ = tr("The bunker asked to open this page: %1").arg(QString::fromStdString(signer_.detail()));
    emit statusChanged();
    bunkerTimer_.start(20000);
    return;
  }
  if (result == storage::RemoteResult::Continue) {
    storage::Event pending;
    if (!signer_.pendingEvent(pending)) {
      finishBunker("signer.state");
      return;
    }
    relay_.sendText(QString::fromStdString(storage::relayEventMessage(pending)));
    bunkerTimer_.start(20000);
    return;
  }
  if (result == storage::RemoteResult::SignedIn) {
    bunkerActive_ = false;
    bunkerTimer_.stop();
    relay_.close();
    status_ = tr("Signed in as %1").arg(QString::fromStdString(signer_.npub()));
    emit statusChanged();
    emit accountChanged();
    return;
  }
  const QString detail = QString::fromStdString(signer_.detail()).left(180);
  finishBunker(error.empty() ? std::string("signer.rejected") : error);
  if (!detail.isEmpty()) {
    status_ = messageFor(QStringLiteral("signer.rejected")) + QStringLiteral(" ") + detail;
    emit statusChanged();
  }
}

void DocumentController::finishBunker(const std::string& code) {
  bunkerActive_ = false;
  bunkerTimer_.stop();
  relay_.close();
  if (!signer_.signedIn()) signer_.logout();
  report(code, false);
  emit accountChanged();
}

bool DocumentController::loginWithNsec(const QString& nsec) {
  abortBunker();
  QByteArray bytes = nsec.toUtf8();
  std::string text(bytes.constData(), static_cast<std::size_t>(bytes.size()));
  std::string error;
  const bool ok = signer_.loginWithNsec(text, error);
  wipeBytes(bytes, text);
  emit accountChanged();
  if (!ok) {
    report(error.empty() ? "signer.nsec" : error, false);
    return false;
  }
  status_ = tr("Signed in as %1").arg(QString::fromStdString(signer_.npub()));
  emit statusChanged();
  return true;
}

bool DocumentController::loginWithBunker(const QString& uri) {
  abortBunker();
  signer_.logout();
  QByteArray bytes = uri.toUtf8();
  std::string text(bytes.constData(), static_cast<std::size_t>(bytes.size()));
  const auto now = std::time(nullptr);
  std::string error;
  const bool ok = now >= 0 && signer_.beginBunker(text, static_cast<std::int64_t>(now), error);
  wipeBytes(bytes, text);
  if (!ok) {
    report(error.empty() ? "signer.bunker" : error, false);
    emit accountChanged();
    return false;
  }
  bunkerRelays_.clear();
  for (const std::string& relay : signer_.relays()) {
    if (bunkerRelays_.size() >= 4) break;
    bunkerRelays_.push_back(QString::fromStdString(relay));
  }
  bunkerRelayIndex_ = -1;
  bunkerConnectedOnce_ = false;
  bunkerActive_ = true;
  status_ = tr("Waiting for the bunker to approve.");
  emit statusChanged();
  emit accountChanged();
  openNextBunker();
  return true;
}

bool DocumentController::loginWithAmber() {
  if (!amberSignerAvailable()) {
    report("signer.amber", false);
    return false;
  }
  abortBunker();
  signer_.logout();
  emit accountChanged();
  status_ = tr("Waiting for Amber to approve.");
  emit statusChanged();
  if (!requestAmberPublicKey(this)) {
    report("signer.amber", false);
    return false;
  }
  return true;
}

void DocumentController::completeAmberLogin(const QString& pubkeyHex, const QString& errorCode) {
  if (!errorCode.isEmpty()) {
    report(errorCode.toStdString(), false);
    emit accountChanged();
    return;
  }
  std::string error;
  if (!signer_.loginWithPublicKey(pubkeyHex.toStdString(), storage::SignerMethod::Amber, error)) {
    report(error.empty() ? "signer.rejected" : error, false);
    emit accountChanged();
    return;
  }
  status_ = tr("Signed in as %1").arg(QString::fromStdString(signer_.npub()));
  emit statusChanged();
  emit accountChanged();
}

void DocumentController::logout() {
  abortBunker();
  signer_.logout();
  status_ = tr("Signed out. The key was cleared from memory.");
  emit statusChanged();
  emit accountChanged();
}

}  // namespace timberlab::ui
