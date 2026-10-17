#pragma once

#include "timberlab/storage/sync.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace timberlab::storage {

// Custom materials and thickness presets. This is not the project and it does
// not store a solid. The file is library.sqlite next to the preferences
// database. Schema version 1 had id, name, and color for a material, and id,
// label, and thickness for a preset. Version 2 adds notes and a preset group.
// Version 3 adds a revision on each row, default 1. Opening version 1 migrates
// through version 2 to version 3 and keeps the rows. Opening version 2 adds
// the revision column and keeps the rows.
struct UserMaterial {
  std::string id;
  std::string name;
  std::string visualHex;
  std::string notes;
  std::int64_t revision = 1;
};

struct UserPreset {
  std::string id;
  std::string label;
  std::string group;
  std::int64_t thicknessNm = 0;
  std::int64_t revision = 1;
};

class Library {
 public:
  static constexpr int kSchemaVersion = 3;
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

  // Seals one library row. The key is not stored. importEnvelope opens that
  // blob, compares revisions, and writes a library row only when the remote
  // record wins. A conflict or a local win writes nothing. Neither function
  // reads or writes a project file.
  bool sealMaterial(const std::string& id, const unsigned char key[32], SealedBlob& sealed, std::string& error);
  bool sealPreset(const std::string& id, const unsigned char key[32], SealedBlob& sealed, std::string& error);
  bool importEnvelope(const unsigned char key[32], const SealedBlob& sealed, std::string& winner, std::string& error);

 private:
  struct Sqlite;
  bool migrateFrom1();
  bool migrateFrom2();
  bool findMaterial(const std::string& id, UserMaterial& material) const;
  bool findPreset(const std::string& id, UserPreset& preset) const;
  bool replaceMaterial(const UserMaterial& material, std::string& error);
  bool replacePreset(const UserPreset& preset, std::string& error);
  Sqlite* db_ = nullptr;
  std::string path_;
  bool ok_ = false;
  std::string error_;
  int schemaVersion_ = 0;
};

}  // namespace timberlab::storage
