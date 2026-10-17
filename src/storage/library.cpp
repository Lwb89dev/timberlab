#include "timberlab/storage/library.hpp"

#include "timberlab/units/units.hpp"

#include <sqlite3.h>

#include <filesystem>

namespace timberlab::storage {
namespace {

constexpr const char* kMaterialTable = "user_material";
constexpr const char* kPresetTable = "user_preset";

void ownerOnly(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, ec);
}

void protectLibraryFiles(const std::string& path) {
  ownerOnly(path);
  ownerOnly(path + "-wal");
  ownerOnly(path + "-shm");
}

bool execSql(sqlite3* db, const char* sql) {
  char* message = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &message);
  sqlite3_free(message);
  return rc == SQLITE_OK;
}

bool tableExists(sqlite3* db, const char* name) {
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?", -1, &statement, nullptr) !=
      SQLITE_OK) {
    return false;
  }
  sqlite3_bind_text(statement, 1, name, -1, SQLITE_TRANSIENT);
  const bool found = sqlite3_step(statement) == SQLITE_ROW;
  sqlite3_finalize(statement);
  return found;
}

bool columnExists(sqlite3* db, const char* table, const char* column) {
  const std::string sql = std::string("PRAGMA table_info(") + table + ")";
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) return false;
  bool found = false;
  while (sqlite3_step(statement) == SQLITE_ROW) {
    const unsigned char* text = sqlite3_column_text(statement, 1);
    if (text != nullptr && std::string(reinterpret_cast<const char*>(text)) == column) found = true;
  }
  sqlite3_finalize(statement);
  return found;
}

int readVersion(sqlite3* db, bool& present) {
  present = false;
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db, "SELECT version FROM schema_meta LIMIT 1", -1, &statement, nullptr) != SQLITE_OK) return -1;
  int version = 0;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    present = true;
    version = sqlite3_column_int(statement, 0);
  }
  sqlite3_finalize(statement);
  return version;
}

int countRows(sqlite3* db, const char* table) {
  const std::string sql = std::string("SELECT COUNT(*) FROM ") + table;
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db, sql.c_str(), -1, &statement, nullptr) != SQLITE_OK) return -1;
  int count = -1;
  if (sqlite3_step(statement) == SQLITE_ROW) count = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  return count;
}

bool validToken(const std::string& id) {
  if (id.empty() || id.size() > Library::kMaxIdBytes) return false;
  for (const unsigned char c : id) {
    if (c >= 'a' && c <= 'z') continue;
    if (c >= '0' && c <= '9') continue;
    if (c == '_' || c == '-' || c == '.') continue;
    return false;
  }
  return true;
}

bool validHex(const std::string& hex) {
  if (hex.size() != 7 || hex[0] != '#') return false;
  for (std::size_t i = 1; i < hex.size(); ++i) {
    const unsigned char c = static_cast<unsigned char>(hex[i]);
    if (c >= '0' && c <= '9') continue;
    if (c >= 'a' && c <= 'f') continue;
    if (c >= 'A' && c <= 'F') continue;
    return false;
  }
  return true;
}

bool validLabel(const std::string& text, std::string& error) {
  if (text.empty()) {
    error = "library.range";
    return false;
  }
  if (text.size() > Library::kMaxNameBytes) {
    error = "text.too_long";
    return false;
  }
  return true;
}

const char* createVersion2Sql() {
  return "CREATE TABLE IF NOT EXISTS user_material("
         "id TEXT PRIMARY KEY NOT NULL, name TEXT NOT NULL, visual_hex TEXT NOT NULL, notes TEXT NOT NULL DEFAULT '');"
         "CREATE TABLE IF NOT EXISTS user_preset("
         "id TEXT PRIMARY KEY NOT NULL, label TEXT NOT NULL, thickness_nm INTEGER NOT NULL,"
         "group_name TEXT NOT NULL DEFAULT 'user');"
         "INSERT INTO schema_meta(version) VALUES (2);";
}

}  // namespace

struct Library::Sqlite {
  sqlite3* handle = nullptr;
};

Library::Library(const std::string& path) : db_(new Sqlite), path_(path) {
  const std::filesystem::path file(path);
  std::error_code ec;
  if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
  const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  if (sqlite3_open_v2(path.c_str(), &db_->handle, flags, nullptr) != SQLITE_OK) {
    error_ = "library.open";
    protectLibraryFiles(path_);
    return;
  }
  sqlite3_busy_timeout(db_->handle, 2000);
  if (!execSql(db_->handle, "PRAGMA journal_mode=WAL; CREATE TABLE IF NOT EXISTS schema_meta(version INTEGER NOT NULL);")) {
    error_ = "library.schema";
    protectLibraryFiles(path_);
    return;
  }

  bool present = false;
  const int version = readVersion(db_->handle, present);
  if (!present) {
    if (!execSql(db_->handle, createVersion2Sql())) {
      error_ = "library.schema";
      protectLibraryFiles(path_);
      return;
    }
  } else if (version == 1) {
    if (!migrateFrom1()) {
      error_ = "library.schema";
      protectLibraryFiles(path_);
      return;
    }
  } else if (version != kSchemaVersion) {
    error_ = version > kSchemaVersion ? "library.version" : "library.schema";
    protectLibraryFiles(path_);
    return;
  }

  sqlite3_stmt* statement = nullptr;
  const char* check = "SELECT id, name, visual_hex, notes FROM user_material LIMIT 0";
  if (sqlite3_prepare_v2(db_->handle, check, -1, &statement, nullptr) != SQLITE_OK) {
    error_ = "library.schema";
    protectLibraryFiles(path_);
    return;
  }
  sqlite3_finalize(statement);
  schemaVersion_ = kSchemaVersion;
  ok_ = true;
  protectLibraryFiles(path_);
}

Library::~Library() {
  if (db_ != nullptr) {
    if (db_->handle != nullptr) sqlite3_close(db_->handle);
    delete db_;
  }
}

bool Library::migrateFrom1() {
  if (!tableExists(db_->handle, kMaterialTable) || !tableExists(db_->handle, kPresetTable)) return false;
  if (!columnExists(db_->handle, kMaterialTable, "id") || !columnExists(db_->handle, kMaterialTable, "name") ||
      !columnExists(db_->handle, kMaterialTable, "visual_hex")) {
    return false;
  }
  if (!columnExists(db_->handle, kPresetTable, "id") || !columnExists(db_->handle, kPresetTable, "label") ||
      !columnExists(db_->handle, kPresetTable, "thickness_nm")) {
    return false;
  }
  if (!execSql(db_->handle, "BEGIN IMMEDIATE")) return false;

  bool present = false;
  const int version = readVersion(db_->handle, present);
  if (present && version == kSchemaVersion) return execSql(db_->handle, "COMMIT");
  if (!present || version != 1) {
    execSql(db_->handle, "ROLLBACK");
    return false;
  }
  if (!columnExists(db_->handle, kMaterialTable, "notes") &&
      !execSql(db_->handle, "ALTER TABLE user_material ADD COLUMN notes TEXT NOT NULL DEFAULT ''")) {
    execSql(db_->handle, "ROLLBACK");
    return false;
  }
  if (!columnExists(db_->handle, kPresetTable, "group_name") &&
      !execSql(db_->handle, "ALTER TABLE user_preset ADD COLUMN group_name TEXT NOT NULL DEFAULT 'user'")) {
    execSql(db_->handle, "ROLLBACK");
    return false;
  }
  if (!execSql(db_->handle, "UPDATE schema_meta SET version = 2") || !execSql(db_->handle, "COMMIT")) {
    execSql(db_->handle, "ROLLBACK");
    return false;
  }
  return true;
}

bool Library::addMaterial(const UserMaterial& material, std::string& error) {
  if (!ok_) {
    error = error_;
    return false;
  }
  if (!validToken(material.id)) {
    error = "library.range";
    return false;
  }
  if (material.name.empty()) {
    error = "name.empty";
    return false;
  }
  if (material.name.size() > kMaxNameBytes) {
    error = "name.too_long";
    return false;
  }
  if (material.notes.size() > kMaxNotesBytes) {
    error = "notes.too_long";
    return false;
  }
  if (!validHex(material.visualHex)) {
    error = "library.range";
    return false;
  }
  const int count = countRows(db_->handle, kMaterialTable);
  if (count < 0) {
    error = "library.schema";
    return false;
  }
  if (static_cast<std::size_t>(count) >= kMaxRows) {
    error = "library.too_many";
    return false;
  }

  sqlite3_stmt* statement = nullptr;
  const char* sql = "INSERT INTO user_material(id, name, visual_hex, notes) VALUES (?, ?, ?, ?)";
  if (sqlite3_prepare_v2(db_->handle, sql, -1, &statement, nullptr) != SQLITE_OK) {
    error = "library.schema";
    return false;
  }
  sqlite3_bind_text(statement, 1, material.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 2, material.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 3, material.visualHex.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 4, material.notes.c_str(), -1, SQLITE_TRANSIENT);
  const int rc = sqlite3_step(statement);
  sqlite3_finalize(statement);
  protectLibraryFiles(path_);
  if (rc == SQLITE_CONSTRAINT) {
    error = "library.duplicate";
    return false;
  }
  if (rc != SQLITE_DONE) {
    error = "library.schema";
    return false;
  }
  return true;
}

bool Library::removeMaterial(const std::string& id, std::string& error) {
  if (!ok_) {
    error = error_;
    return false;
  }
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_->handle, "DELETE FROM user_material WHERE id=?", -1, &statement, nullptr) != SQLITE_OK) {
    error = "library.schema";
    return false;
  }
  sqlite3_bind_text(statement, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  const bool stepped = sqlite3_step(statement) == SQLITE_DONE;
  sqlite3_finalize(statement);
  protectLibraryFiles(path_);
  if (!stepped) {
    error = "library.schema";
    return false;
  }
  if (sqlite3_changes(db_->handle) == 0) {
    error = "library.missing";
    return false;
  }
  return true;
}

std::vector<UserMaterial> Library::materials() const {
  std::vector<UserMaterial> rows;
  if (!ok_) return rows;
  sqlite3_stmt* statement = nullptr;
  const char* sql = "SELECT id, name, visual_hex, notes FROM user_material ORDER BY name, id";
  if (sqlite3_prepare_v2(db_->handle, sql, -1, &statement, nullptr) != SQLITE_OK) return rows;
  while (sqlite3_step(statement) == SQLITE_ROW) {
    UserMaterial material;
    const auto textAt = [&](int column) {
      const unsigned char* text = sqlite3_column_text(statement, column);
      return text == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(text));
    };
    material.id = textAt(0);
    material.name = textAt(1);
    material.visualHex = textAt(2);
    material.notes = textAt(3);
    rows.push_back(std::move(material));
  }
  sqlite3_finalize(statement);
  return rows;
}

bool Library::addPreset(const UserPreset& preset, std::string& error) {
  if (!ok_) {
    error = error_;
    return false;
  }
  if (!validToken(preset.id) || !validToken(preset.group)) {
    error = "library.range";
    return false;
  }
  if (!validLabel(preset.label, error)) return false;
  if (preset.thicknessNm <= 0 || preset.thicknessNm > units::kMaxPartNm) {
    error = "library.range";
    return false;
  }
  const int count = countRows(db_->handle, kPresetTable);
  if (count < 0) {
    error = "library.schema";
    return false;
  }
  if (static_cast<std::size_t>(count) >= kMaxRows) {
    error = "library.too_many";
    return false;
  }

  sqlite3_stmt* statement = nullptr;
  const char* sql = "INSERT INTO user_preset(id, label, thickness_nm, group_name) VALUES (?, ?, ?, ?)";
  if (sqlite3_prepare_v2(db_->handle, sql, -1, &statement, nullptr) != SQLITE_OK) {
    error = "library.schema";
    return false;
  }
  sqlite3_bind_text(statement, 1, preset.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 2, preset.label.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(statement, 3, preset.thicknessNm);
  sqlite3_bind_text(statement, 4, preset.group.c_str(), -1, SQLITE_TRANSIENT);
  const int rc = sqlite3_step(statement);
  sqlite3_finalize(statement);
  protectLibraryFiles(path_);
  if (rc == SQLITE_CONSTRAINT) {
    error = "library.duplicate";
    return false;
  }
  if (rc != SQLITE_DONE) {
    error = "library.schema";
    return false;
  }
  return true;
}

bool Library::removePreset(const std::string& id, std::string& error) {
  if (!ok_) {
    error = error_;
    return false;
  }
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_->handle, "DELETE FROM user_preset WHERE id=?", -1, &statement, nullptr) != SQLITE_OK) {
    error = "library.schema";
    return false;
  }
  sqlite3_bind_text(statement, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  const bool stepped = sqlite3_step(statement) == SQLITE_DONE;
  sqlite3_finalize(statement);
  protectLibraryFiles(path_);
  if (!stepped) {
    error = "library.schema";
    return false;
  }
  if (sqlite3_changes(db_->handle) == 0) {
    error = "library.missing";
    return false;
  }
  return true;
}

std::vector<UserPreset> Library::presets() const {
  std::vector<UserPreset> rows;
  if (!ok_) return rows;
  sqlite3_stmt* statement = nullptr;
  const char* sql = "SELECT id, label, thickness_nm, group_name FROM user_preset ORDER BY label, id";
  if (sqlite3_prepare_v2(db_->handle, sql, -1, &statement, nullptr) != SQLITE_OK) return rows;
  while (sqlite3_step(statement) == SQLITE_ROW) {
    UserPreset preset;
    const auto textAt = [&](int column) {
      const unsigned char* text = sqlite3_column_text(statement, column);
      return text == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(text));
    };
    preset.id = textAt(0);
    preset.label = textAt(1);
    preset.thicknessNm = sqlite3_column_int64(statement, 2);
    preset.group = textAt(3);
    rows.push_back(std::move(preset));
  }
  sqlite3_finalize(statement);
  return rows;
}

}  // namespace timberlab::storage
