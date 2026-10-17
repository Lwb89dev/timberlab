#include "timberlab/storage/library.hpp"

#include "timberlab/storage/sha256.hpp"
#include "timberlab/units/units.hpp"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <filesystem>
#include <initializer_list>

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

const char* createVersion3Sql() {
  return "CREATE TABLE IF NOT EXISTS user_material("
         "id TEXT PRIMARY KEY NOT NULL, name TEXT NOT NULL, visual_hex TEXT NOT NULL, "
         "notes TEXT NOT NULL DEFAULT '', revision INTEGER NOT NULL DEFAULT 1);"
         "CREATE TABLE IF NOT EXISTS user_preset("
         "id TEXT PRIMARY KEY NOT NULL, label TEXT NOT NULL, thickness_nm INTEGER NOT NULL,"
         "group_name TEXT NOT NULL DEFAULT 'user', revision INTEGER NOT NULL DEFAULT 1);"
         "INSERT INTO schema_meta(version) VALUES (3);";
}

nlohmann::json materialPayload(const UserMaterial& material) {
  return {{"id", material.id},
          {"kind", "material"},
          {"name", material.name},
          {"notes", material.notes},
          {"visualHex", material.visualHex}};
}

nlohmann::json presetPayload(const UserPreset& preset) {
  return {{"group", preset.group},
          {"id", preset.id},
          {"kind", "preset"},
          {"label", preset.label},
          {"thicknessNm", preset.thicknessNm}};
}

std::string envelopeText(const nlohmann::json& payload, const std::string& id, const std::string& kind,
                         std::int64_t revision) {
  const nlohmann::json envelope = {{"contentHash", sha256Hex(payload.dump())},
                                   {"id", id},
                                   {"kind", kind},
                                   {"payload", payload},
                                   {"revision", revision}};
  return envelope.dump();
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
    if (!execSql(db_->handle, createVersion3Sql())) {
      error_ = "library.schema";
      protectLibraryFiles(path_);
      return;
    }
  } else if (version == 1) {
    if (!migrateFrom1() || !migrateFrom2()) {
      error_ = "library.schema";
      protectLibraryFiles(path_);
      return;
    }
  } else if (version == 2) {
    if (!migrateFrom2()) {
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
  const char* check = "SELECT id, name, visual_hex, notes, revision FROM user_material LIMIT 0";
  if (sqlite3_prepare_v2(db_->handle, check, -1, &statement, nullptr) != SQLITE_OK) {
    error_ = "library.schema";
    protectLibraryFiles(path_);
    return;
  }
  sqlite3_finalize(statement);
  const char* presetCheck = "SELECT id, label, thickness_nm, group_name, revision FROM user_preset LIMIT 0";
  if (sqlite3_prepare_v2(db_->handle, presetCheck, -1, &statement, nullptr) != SQLITE_OK) {
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

bool Library::migrateFrom2() {
  if (!tableExists(db_->handle, kMaterialTable) || !tableExists(db_->handle, kPresetTable)) return false;
  if (!execSql(db_->handle, "BEGIN IMMEDIATE")) return false;
  bool present = false;
  const int version = readVersion(db_->handle, present);
  if (present && version == kSchemaVersion) return execSql(db_->handle, "COMMIT");
  if (!present || version != 2) {
    execSql(db_->handle, "ROLLBACK");
    return false;
  }
  if (!columnExists(db_->handle, kMaterialTable, "revision") &&
      !execSql(db_->handle, "ALTER TABLE user_material ADD COLUMN revision INTEGER NOT NULL DEFAULT 1")) {
    execSql(db_->handle, "ROLLBACK");
    return false;
  }
  if (!columnExists(db_->handle, kPresetTable, "revision") &&
      !execSql(db_->handle, "ALTER TABLE user_preset ADD COLUMN revision INTEGER NOT NULL DEFAULT 1")) {
    execSql(db_->handle, "ROLLBACK");
    return false;
  }
  if (!execSql(db_->handle, "UPDATE schema_meta SET version = 3") || !execSql(db_->handle, "COMMIT")) {
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
  const char* sql = "INSERT INTO user_material(id, name, visual_hex, notes, revision) VALUES (?, ?, ?, ?, 1)";
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
  const char* sql = "SELECT id, name, visual_hex, notes, revision FROM user_material ORDER BY name, id";
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
    material.revision = sqlite3_column_int64(statement, 4);
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
  const char* sql = "INSERT INTO user_preset(id, label, thickness_nm, group_name, revision) VALUES (?, ?, ?, ?, 1)";
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
  const char* sql = "SELECT id, label, thickness_nm, group_name, revision FROM user_preset ORDER BY label, id";
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
    preset.revision = sqlite3_column_int64(statement, 4);
    rows.push_back(std::move(preset));
  }
  sqlite3_finalize(statement);
  return rows;
}

namespace {

std::string columnText(sqlite3_stmt* statement, int column) {
  const unsigned char* text = sqlite3_column_text(statement, column);
  return text == nullptr ? std::string() : std::string(reinterpret_cast<const char*>(text));
}

bool sameKeys(const nlohmann::json& payload, std::initializer_list<const char*> keys) {
  if (payload.size() != keys.size()) return false;
  for (const char* key : keys) {
    if (!payload.contains(key)) return false;
  }
  return true;
}

struct IncomingRecord {
  std::string id;
  std::string kind;
  std::int64_t revision = 0;
  std::string contentHash;
  nlohmann::json payload;
};

bool readEnvelope(const std::string& plaintext, IncomingRecord& incoming, std::string& error) {
  nlohmann::json root;
  try {
    root = nlohmann::json::parse(plaintext);
  } catch (const nlohmann::json::exception&) {
    error = "sync.mismatch";
    return false;
  }
  if (!root.is_object() || !root.contains("id") || !root["id"].is_string() || !root.contains("kind") ||
      !root["kind"].is_string() || !root.contains("revision") || !root["revision"].is_number_integer() ||
      !root.contains("contentHash") || !root["contentHash"].is_string() || !root.contains("payload") ||
      !root["payload"].is_object()) {
    error = "sync.mismatch";
    return false;
  }
  incoming.id = root["id"].get<std::string>();
  incoming.kind = root["kind"].get<std::string>();
  incoming.revision = root["revision"].get<std::int64_t>();
  incoming.contentHash = root["contentHash"].get<std::string>();
  incoming.payload = root["payload"];
  if (incoming.kind != "material" && incoming.kind != "preset") {
    error = "sync.mismatch";
    return false;
  }
  if (incoming.payload.value("id", std::string()) != incoming.id ||
      incoming.payload.value("kind", std::string()) != incoming.kind) {
    error = "sync.mismatch";
    return false;
  }
  if (sha256Hex(incoming.payload.dump()) != incoming.contentHash) {
    error = "sync.mismatch";
    return false;
  }
  return true;
}

bool materialFromEnvelope(const IncomingRecord& incoming, UserMaterial& material, std::string& error) {
  if (!sameKeys(incoming.payload, {"id", "kind", "name", "notes", "visualHex"})) {
    error = "sync.mismatch";
    return false;
  }
  if (!incoming.payload["name"].is_string() || !incoming.payload["notes"].is_string() ||
      !incoming.payload["visualHex"].is_string()) {
    error = "sync.mismatch";
    return false;
  }
  material.id = incoming.id;
  material.name = incoming.payload["name"].get<std::string>();
  material.notes = incoming.payload["notes"].get<std::string>();
  material.visualHex = incoming.payload["visualHex"].get<std::string>();
  material.revision = incoming.revision;
  if (!validToken(material.id) || !validHex(material.visualHex) || material.revision < 1) {
    error = "library.range";
    return false;
  }
  if (material.name.empty()) {
    error = "name.empty";
    return false;
  }
  if (material.name.size() > Library::kMaxNameBytes) {
    error = "name.too_long";
    return false;
  }
  if (material.notes.size() > Library::kMaxNotesBytes) {
    error = "notes.too_long";
    return false;
  }
  return true;
}

bool presetFromEnvelope(const IncomingRecord& incoming, UserPreset& preset, std::string& error) {
  if (!sameKeys(incoming.payload, {"group", "id", "kind", "label", "thicknessNm"})) {
    error = "sync.mismatch";
    return false;
  }
  if (!incoming.payload["group"].is_string() || !incoming.payload["label"].is_string() ||
      !incoming.payload["thicknessNm"].is_number_integer()) {
    error = "sync.mismatch";
    return false;
  }
  preset.id = incoming.id;
  preset.group = incoming.payload["group"].get<std::string>();
  preset.label = incoming.payload["label"].get<std::string>();
  preset.thicknessNm = incoming.payload["thicknessNm"].get<std::int64_t>();
  preset.revision = incoming.revision;
  if (!validToken(preset.id) || !validToken(preset.group) || preset.revision < 1) {
    error = "library.range";
    return false;
  }
  if (!validLabel(preset.label, error)) return false;
  if (preset.thicknessNm <= 0 || preset.thicknessNm > units::kMaxPartNm) {
    error = "library.range";
    return false;
  }
  return true;
}

}  // namespace

bool Library::findMaterial(const std::string& id, UserMaterial& material) const {
  if (!ok_) return false;
  sqlite3_stmt* statement = nullptr;
  const char* sql = "SELECT id, name, visual_hex, notes, revision FROM user_material WHERE id=?";
  if (sqlite3_prepare_v2(db_->handle, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
  sqlite3_bind_text(statement, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(statement) != SQLITE_ROW) {
    sqlite3_finalize(statement);
    return false;
  }
  material.id = columnText(statement, 0);
  material.name = columnText(statement, 1);
  material.visualHex = columnText(statement, 2);
  material.notes = columnText(statement, 3);
  material.revision = sqlite3_column_int64(statement, 4);
  sqlite3_finalize(statement);
  return true;
}

bool Library::findPreset(const std::string& id, UserPreset& preset) const {
  if (!ok_) return false;
  sqlite3_stmt* statement = nullptr;
  const char* sql = "SELECT id, label, thickness_nm, group_name, revision FROM user_preset WHERE id=?";
  if (sqlite3_prepare_v2(db_->handle, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
  sqlite3_bind_text(statement, 1, id.c_str(), -1, SQLITE_TRANSIENT);
  if (sqlite3_step(statement) != SQLITE_ROW) {
    sqlite3_finalize(statement);
    return false;
  }
  preset.id = columnText(statement, 0);
  preset.label = columnText(statement, 1);
  preset.thicknessNm = sqlite3_column_int64(statement, 2);
  preset.group = columnText(statement, 3);
  preset.revision = sqlite3_column_int64(statement, 4);
  sqlite3_finalize(statement);
  return true;
}

bool Library::replaceMaterial(const UserMaterial& material, std::string& error) {
  if (!ok_) {
    error = error_;
    return false;
  }
  UserMaterial existing;
  const bool found = findMaterial(material.id, existing);
  if (!found) {
    const int count = countRows(db_->handle, kMaterialTable);
    if (count < 0) {
      error = "library.schema";
      return false;
    }
    if (static_cast<std::size_t>(count) >= kMaxRows) {
      error = "library.too_many";
      return false;
    }
  }
  sqlite3_stmt* statement = nullptr;
  const char* sql =
      "INSERT INTO user_material(id, name, visual_hex, notes, revision) VALUES (?, ?, ?, ?, ?) "
      "ON CONFLICT(id) DO UPDATE SET name=excluded.name, visual_hex=excluded.visual_hex, "
      "notes=excluded.notes, revision=excluded.revision";
  if (sqlite3_prepare_v2(db_->handle, sql, -1, &statement, nullptr) != SQLITE_OK) {
    error = "library.schema";
    return false;
  }
  sqlite3_bind_text(statement, 1, material.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 2, material.name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 3, material.visualHex.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 4, material.notes.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(statement, 5, material.revision);
  const int rc = sqlite3_step(statement);
  sqlite3_finalize(statement);
  protectLibraryFiles(path_);
  if (rc != SQLITE_DONE) {
    error = "library.schema";
    return false;
  }
  return true;
}

bool Library::replacePreset(const UserPreset& preset, std::string& error) {
  if (!ok_) {
    error = error_;
    return false;
  }
  UserPreset existing;
  const bool found = findPreset(preset.id, existing);
  if (!found) {
    const int count = countRows(db_->handle, kPresetTable);
    if (count < 0) {
      error = "library.schema";
      return false;
    }
    if (static_cast<std::size_t>(count) >= kMaxRows) {
      error = "library.too_many";
      return false;
    }
  }
  sqlite3_stmt* statement = nullptr;
  const char* sql =
      "INSERT INTO user_preset(id, label, thickness_nm, group_name, revision) VALUES (?, ?, ?, ?, ?) "
      "ON CONFLICT(id) DO UPDATE SET label=excluded.label, thickness_nm=excluded.thickness_nm, "
      "group_name=excluded.group_name, revision=excluded.revision";
  if (sqlite3_prepare_v2(db_->handle, sql, -1, &statement, nullptr) != SQLITE_OK) {
    error = "library.schema";
    return false;
  }
  sqlite3_bind_text(statement, 1, preset.id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 2, preset.label.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(statement, 3, preset.thicknessNm);
  sqlite3_bind_text(statement, 4, preset.group.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(statement, 5, preset.revision);
  const int rc = sqlite3_step(statement);
  sqlite3_finalize(statement);
  protectLibraryFiles(path_);
  if (rc != SQLITE_DONE) {
    error = "library.schema";
    return false;
  }
  return true;
}

bool Library::sealMaterial(const std::string& id, const unsigned char key[32], SealedBlob& sealed, std::string& error) {
  sealed.bytes.clear();
  if (!ok_) {
    error = error_;
    return false;
  }
  UserMaterial material;
  if (!findMaterial(id, material)) {
    error = "library.missing";
    return false;
  }
  const nlohmann::json payload = materialPayload(material);
  return sealAes256Gcm(key, envelopeText(payload, material.id, "material", material.revision), sealed, error);
}

bool Library::sealPreset(const std::string& id, const unsigned char key[32], SealedBlob& sealed, std::string& error) {
  sealed.bytes.clear();
  if (!ok_) {
    error = error_;
    return false;
  }
  UserPreset preset;
  if (!findPreset(id, preset)) {
    error = "library.missing";
    return false;
  }
  const nlohmann::json payload = presetPayload(preset);
  return sealAes256Gcm(key, envelopeText(payload, preset.id, "preset", preset.revision), sealed, error);
}

bool Library::materialPlaintext(const std::string& id, std::string& plaintext, std::string& error) {
  plaintext.clear();
  if (!ok_) {
    error = error_;
    return false;
  }
  UserMaterial material;
  if (!findMaterial(id, material)) {
    error = "library.missing";
    return false;
  }
  plaintext = envelopeText(materialPayload(material), material.id, "material", material.revision);
  return true;
}

bool Library::presetPlaintext(const std::string& id, std::string& plaintext, std::string& error) {
  plaintext.clear();
  if (!ok_) {
    error = error_;
    return false;
  }
  UserPreset preset;
  if (!findPreset(id, preset)) {
    error = "library.missing";
    return false;
  }
  plaintext = envelopeText(presetPayload(preset), preset.id, "preset", preset.revision);
  return true;
}

bool Library::importEnvelope(const unsigned char key[32], const SealedBlob& sealed, std::string& winner,
                             std::string& error) {
  winner.clear();
  if (!ok_) {
    error = error_;
    return false;
  }
  std::string plaintext;
  if (!openAes256Gcm(key, sealed, plaintext, error)) return false;
  return importPlaintext(plaintext, winner, error);
}

bool Library::importPlaintext(const std::string& plaintext, std::string& winner, std::string& error) {
  winner.clear();
  if (!ok_) {
    error = error_;
    return false;
  }
  IncomingRecord incoming;
  if (!readEnvelope(plaintext, incoming, error)) return false;

  SyncRecord remote;
  remote.id = incoming.id;
  remote.kind = incoming.kind;
  remote.revision = incoming.revision;
  remote.contentHash = incoming.contentHash;
  SyncRecord local;
  local.id = incoming.id;
  local.kind = incoming.kind;
  if (incoming.kind == "material") {
    UserMaterial current;
    if (findMaterial(incoming.id, current)) {
      local.revision = current.revision;
      local.contentHash = sha256Hex(materialPayload(current).dump());
    }
  } else {
    UserPreset current;
    if (findPreset(incoming.id, current)) {
      local.revision = current.revision;
      local.contentHash = sha256Hex(presetPayload(current).dump());
    }
  }
  if (!chooseRevision(local, remote, winner, error)) return false;
  if (winner != "remote") return true;

  if (incoming.kind == "material") {
    UserMaterial material;
    if (!materialFromEnvelope(incoming, material, error) || !replaceMaterial(material, error)) {
      winner.clear();
      return false;
    }
    return true;
  }
  UserPreset preset;
  if (!presetFromEnvelope(incoming, preset, error) || !replacePreset(preset, error)) {
    winner.clear();
    return false;
  }
  return true;
}

}  // namespace timberlab::storage
