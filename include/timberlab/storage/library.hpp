#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace timberlab::storage {

// Custom materials and thickness presets. This is not the project and it does
// not store a solid. The file is library.sqlite next to the preferences
// database. Schema version 1 had id, name, and color for a material, and id,
// label, and thickness for a preset. Version 2 adds notes and a preset group.
// Opening a version 1 file migrates it and keeps the rows.
struct UserMaterial {
  std::string id;
  std::string name;
  std::string visualHex;
  std::string notes;
};

struct UserPreset {
  std::string id;
  std::string label;
  std::string group;
  std::int64_t thicknessNm = 0;
};

class Library {
 public:
  static constexpr int kSchemaVersion = 2;
  static constexpr std::size_t kMaxRows = 2000;
  static constexpr std::size_t kMaxNameBytes = 200;
  static constexpr std::size_t kMaxNotesBytes = 8000;
  static constexpr std::size_t kMaxIdBytes = 64;

  explicit Library(const std::string& path);
  ~Library();

  Library(const Library&) = delete;
  Library& operator=(const Library&) = delete;

  bool ok() const { return ok_; }
  const std::string& error() const { return error_; }
  int schemaVersion() const { return schemaVersion_; }

  bool addMaterial(const UserMaterial& material, std::string& error);
  bool removeMaterial(const std::string& id, std::string& error);
  std::vector<UserMaterial> materials() const;

  bool addPreset(const UserPreset& preset, std::string& error);
  bool removePreset(const std::string& id, std::string& error);
  std::vector<UserPreset> presets() const;

 private:
  struct Sqlite;
  bool migrateFrom1();
  Sqlite* db_ = nullptr;
  std::string path_;
  bool ok_ = false;
  std::string error_;
  int schemaVersion_ = 0;
};

}  // namespace timberlab::storage
