#pragma once

#include "timberlab/core/component.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace timberlab {

// Parametric project. Lengths are nanometres. History stores full document
// JSON snapshots, which is sound for the current model size. A later milestone
// can switch to incremental commands without changing the file format.
class Document {
 public:
  static constexpr int kSchemaVersion = 1;

  Document();

  const std::string& id() const { return id_; }
  const std::string& name() const { return name_; }
  const std::string& displayUnit() const { return displayUnit_; }
  int fractionPrecision() const { return fractionPrecision_; }
  const std::vector<Component>& components() const { return components_; }

  bool dirty() const { return !savedValid_ || savedIndex_ != historyIndex_; }
  bool canUndo() const { return historyIndex_ > 0; }
  bool canRedo() const { return historyIndex_ + 1 < history_.size(); }

  void newProject(const std::string& displayUnit, int fractionPrecision);
  void markSaved();

  std::string addBoard();
  std::string addBox();
  std::string addCylinder();
  bool duplicate(const std::string& id, std::string& newId, std::string& error);
  bool remove(const std::string& id, std::string& error);

  bool setName(const std::string& id, const std::string& name, std::string& error);
  bool setProjectName(const std::string& name, std::string& error);
  bool setMaterial(const std::string& id, const std::string& materialId, std::string& error);
  bool setNotes(const std::string& id, const std::string& notes, std::string& error);
  bool setVisible(const std::string& id, bool visible, std::string& error);
  bool setLocked(const std::string& id, bool locked, std::string& error);
  bool setDimension(const std::string& id, std::string_view key, std::int64_t nm, std::string& error);
  bool setTranslation(const std::string& id, std::string_view axis, std::int64_t nm, std::string& error);
  bool setRotation(const std::string& id, std::string_view axis, double degrees, std::string& error);
  bool setDisplayUnit(const std::string& unitKey, std::string& error);
  bool setFractionPrecision(int denom, std::string& error);

  const Component* find(const std::string& id) const;

  bool undo(std::string& error);
  bool redo(std::string& error);

  std::string toJson() const;
  static bool fromJson(const std::string& json, Document& out, std::string& error);

  // Freeform sample: four legs, four aprons, one top. Not a parametric assembly.
  void makeDemoTable();

 private:
  std::string id_;
  std::string name_ = "Untitled";
  std::string displayUnit_ = "mm";
  int fractionPrecision_ = 16;
  std::vector<Component> components_;
  std::vector<std::string> history_;
  std::size_t historyIndex_ = 0;
  std::size_t savedIndex_ = 0;
  bool savedValid_ = true;

  void resetHistory();
  void pushHistory();
  bool restoreCurrent(std::string& error);
  Component* findMutable(const std::string& id);
  bool editable(const Component& component, std::string& error) const;
  std::string addComponent(Component component, bool autoPlace);
  static bool parseInto(const std::string& json, Document& out, std::string& error);
};

}  // namespace timberlab
