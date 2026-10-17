#pragma once

#include "timberlab/core/document.hpp"
#include "timberlab/geometry/solids.hpp"
#include "timberlab/materials/catalog.hpp"
#include "timberlab/storage/library.hpp"
#include "timberlab/storage/preferences.hpp"
#include "timberlab/units/units.hpp"

#include <QAbstractListModel>
#include <QMatrix4x4>
#include <QObject>
#include <QUrl>
#include <QVariant>
#include <QVector3D>

#include <memory>
#include <mutex>
#include <vector>

namespace timberlab::ui {

struct ScenePart {
  QString id;
  std::vector<float> interleaved;
  std::vector<std::uint32_t> indices;
  QVector3D color;
  QMatrix4x4 model;
  bool visible = true;
};

class DocumentController;

class ComponentListModel : public QAbstractListModel {
  Q_OBJECT
 public:
  enum Roles { IdRole = Qt::UserRole + 1, NameRole, TypeRole, VisibleRole, LockedRole };

  explicit ComponentListModel(DocumentController* owner);

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  void refresh();

 private:
  DocumentController* owner_ = nullptr;
};

class DocumentController : public QObject {
  Q_OBJECT
  Q_PROPERTY(QString projectName READ projectName WRITE setProjectName NOTIFY documentChanged)
  Q_PROPERTY(QString displayUnit READ displayUnit NOTIFY documentChanged)
  Q_PROPERTY(int fractionPrecision READ fractionPrecision NOTIFY documentChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY documentChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY documentChanged)
  Q_PROPERTY(bool dirty READ dirty NOTIFY documentChanged)
  Q_PROPERTY(QString status READ status NOTIFY statusChanged)
  Q_PROPERTY(QString selectedId READ selectedId NOTIFY documentChanged)
  Q_PROPERTY(QString projectPath READ projectPath NOTIFY documentChanged)
  Q_PROPERTY(ComponentListModel* components READ components CONSTANT)
  Q_PROPERTY(QVariantList materialChoices READ materialChoices NOTIFY documentChanged)
  Q_PROPERTY(QVariantList thicknessPresets READ thicknessPresets NOTIFY documentChanged)
  Q_PROPERTY(QVariantList userMaterials READ userMaterials NOTIFY documentChanged)
  Q_PROPERTY(QVariantList userPresets READ userPresets NOTIFY documentChanged)
  Q_PROPERTY(QVariantMap selection READ selection NOTIFY documentChanged)
  Q_PROPERTY(QVariantList partChoices READ partChoices NOTIFY documentChanged)
  Q_PROPERTY(QVariantList parentChoices READ parentChoices NOTIFY documentChanged)
  Q_PROPERTY(QVariantList joints READ joints NOTIFY documentChanged)
  Q_PROPERTY(QVariantList parameters READ parameters NOTIFY documentChanged)
  Q_PROPERTY(QVariantList driveFields READ driveFields CONSTANT)
  Q_PROPERTY(int revision READ revision NOTIFY documentChanged)
  Q_PROPERTY(QString meshQuality READ meshQuality WRITE setMeshQuality NOTIFY documentChanged)

 public:
  explicit DocumentController(QObject* parent = nullptr);

  void initialise();
  QString projectName() const;
  void setProjectName(const QString& name);
  QString displayUnit() const;
  int fractionPrecision() const;
  bool canUndo() const;
  bool canRedo() const;
  bool dirty() const;
  QString status() const { return status_; }
  QString selectedId() const { return selectedId_; }
  QString projectPath() const { return projectPath_; }
  ComponentListModel* components() { return &components_; }
  QVariantList materialChoices() const;
  QVariantList thicknessPresets() const;
  QVariantList userMaterials() const;
  QVariantList userPresets() const;
  QVariantMap selection() const;
  QVariantList partChoices() const;
  QVariantList parentChoices() const;
  QVariantList joints() const;
  QVariantList parameters() const;
  QVariantList driveFields() const;
  int revision() const { return revision_; }
  QString meshQuality() const;
  void setMeshQuality(const QString& quality);

  const Document& document() const { return document_; }
  std::vector<ScenePart> copyScene() const;
  bool worldBounds(const QString& componentId, QVector3D& minimum, QVector3D& maximum) const;

  Q_INVOKABLE void newProject();
  Q_INVOKABLE void makeDemo();
  Q_INVOKABLE void addBoard();
  Q_INVOKABLE void addBox();
  Q_INVOKABLE void addCylinder();
  Q_INVOKABLE void addAssembly(const QString& kind);
  Q_INVOKABLE void duplicateSelected();
  Q_INVOKABLE void deleteSelected();
  Q_INVOKABLE void select(const QString& id);
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE bool saveProject(const QUrl& url);
  Q_INVOKABLE bool saveCurrent();
  Q_INVOKABLE bool loadProject(const QUrl& url);
  Q_INVOKABLE bool setParameterFromText(const QString& id, const QString& key, const QString& text);
  Q_INVOKABLE bool setDriverFromText(const QString& parameterId, const QString& text);
  Q_INVOKABLE bool drivePartField(const QString& partId, const QString& field, const QString& parameterId);
  Q_INVOKABLE bool releasePartField(const QString& partId, const QString& field);
  Q_INVOKABLE bool setTranslationFromText(const QString& id, const QString& axis, const QString& text);
  Q_INVOKABLE bool setRotationFromText(const QString& id, const QString& axis, const QString& text);
  Q_INVOKABLE void setPartName(const QString& id, const QString& name);
  Q_INVOKABLE void setMaterial(const QString& id, const QString& materialId);
  Q_INVOKABLE void setVisible(const QString& id, bool visible);
  Q_INVOKABLE void setLocked(const QString& id, bool locked);
  Q_INVOKABLE void setParent(const QString& id, const QString& parentId);
  Q_INVOKABLE void setRole(const QString& id, const QString& role);
  Q_INVOKABLE void setNotes(const QString& id, const QString& notes);
  Q_INVOKABLE bool exportCutList(const QUrl& url, const QString& allowanceText);
  Q_INVOKABLE bool exportFrontView(const QUrl& url);
  Q_INVOKABLE bool exportDrawing(const QUrl& url, const QString& kind);
  Q_INVOKABLE bool restoreBackup();
  Q_INVOKABLE void setDisplayUnit(const QString& unitKey);
  Q_INVOKABLE void setFractionPrecision(int denom);
  Q_INVOKABLE void applyThicknessPreset(const QString& presetId);
  Q_INVOKABLE bool addUserMaterial(const QString& name, const QString& visualHex);
  Q_INVOKABLE bool addUserPreset(const QString& thicknessText);
  Q_INVOKABLE bool removeUserMaterial(const QString& id);
  Q_INVOKABLE bool removeUserPreset(const QString& id);
  Q_INVOKABLE bool addJoint(const QString& typeKey, const QString& selectedId, const QString& mateId);
  Q_INVOKABLE bool setJointFromText(const QString& id, const QString& key, const QString& text);
  Q_INVOKABLE void removeJoint(const QString& id);

 signals:
  void documentChanged();
  void statusChanged();
  void errorRaised(const QString& message);
  void requestFrameAll();

 private:
  void touch(bool frame = false);
  void report(const std::string& code, bool dialog);
  QString messageFor(const QString& code) const;
  QString fieldLabel(const std::string& field) const;
  QString parameterLabel(const constraints::Parameter& parameter) const;
  QString instanceParameterLabel(const std::string& id, const std::string& role) const;
  QString termSource(const constraints::Term& term) const;
  QString formulaFor(const constraints::Binding& binding) const;
  units::LengthUnit currentUnit() const;
  void rebuildScene();
  void rememberPreference(const QString& key, const QString& value);
  void reloadLibrary();
  QString languageName(const materials::CatalogEntry& entry) const;
  QVector3D colorForMaterial(const std::string& id) const;

  Document document_;
  materials::Catalog catalog_;
  std::vector<materials::ThicknessPreset> thicknesses_;
  std::vector<storage::UserMaterial> userMaterials_;
  std::vector<storage::UserPreset> userPresets_;
  std::unique_ptr<storage::Preferences> preferences_;
  std::unique_ptr<storage::Library> library_;
  ComponentListModel components_;
  QString selectedId_;
  QString projectPath_;
  QString status_;
  int revision_ = 0;
  geometry::MeshQuality quality_ = geometry::MeshQuality::Standard;
  mutable std::mutex sceneMutex_;
  std::vector<ScenePart> scene_;
};

}  // namespace timberlab::ui
