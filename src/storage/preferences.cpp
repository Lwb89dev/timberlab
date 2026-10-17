#include "timberlab/storage/preferences.hpp"

#include <sqlite3.h>

#include <filesystem>

namespace timberlab::storage {
namespace {

void ownerOnly(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, ec);
}

void protectPreferenceFiles(const std::string& path) {
  ownerOnly(path);
  ownerOnly(path + "-wal");
  ownerOnly(path + "-shm");
}

}  // namespace

struct Preferences::Sqlite {
  sqlite3* handle = nullptr;
};

Preferences::Preferences(const std::string& path) : db_(new Sqlite), path_(path) {
  const std::filesystem::path file(path);
  std::error_code ec;
  if (file.has_parent_path()) std::filesystem::create_directories(file.parent_path(), ec);
  const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_FULLMUTEX;
  if (sqlite3_open_v2(path.c_str(), &db_->handle, flags, nullptr) != SQLITE_OK) {
    error_ = "preferences.open";
    return;
  }
  sqlite3_busy_timeout(db_->handle, 2000);
  char* message = nullptr;
  const char* sql =
      "PRAGMA journal_mode=WAL;"
      "CREATE TABLE IF NOT EXISTS schema_meta(version INTEGER NOT NULL);"
      "CREATE TABLE IF NOT EXISTS user_preference(key TEXT PRIMARY KEY NOT NULL, value TEXT NOT NULL);";
  if (sqlite3_exec(db_->handle, sql, nullptr, nullptr, &message) != SQLITE_OK) {
    sqlite3_free(message);
    error_ = "preferences.schema";
    protectPreferenceFiles(path_);
    return;
  }

  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_->handle, "SELECT version FROM schema_meta LIMIT 1", -1, &statement, nullptr) != SQLITE_OK) {
    error_ = "preferences.schema";
    protectPreferenceFiles(path_);
    return;
  }
  int version = 0;
  const int step = sqlite3_step(statement);
  if (step == SQLITE_ROW) version = sqlite3_column_int(statement, 0);
  sqlite3_finalize(statement);
  if (version == 0) {
    if (sqlite3_exec(db_->handle, "INSERT INTO schema_meta(version) VALUES (1);", nullptr, nullptr, &message) !=
        SQLITE_OK) {
      sqlite3_free(message);
      error_ = "preferences.schema";
      protectPreferenceFiles(path_);
      return;
    }
    version = 1;
  }
  if (version > 1) {
    error_ = "preferences.version";
    protectPreferenceFiles(path_);
    return;
  }
  schemaVersion_ = version;
  ok_ = true;
  protectPreferenceFiles(path_);
}

Preferences::~Preferences() {
  if (db_ != nullptr) {
    if (db_->handle != nullptr) sqlite3_close(db_->handle);
    delete db_;
  }
}

bool Preferences::set(const std::string& key, const std::string& value) {
  if (!ok_) return false;
  sqlite3_stmt* statement = nullptr;
  const char* sql = "INSERT INTO user_preference(key, value) VALUES (?, ?) ON CONFLICT(key) DO UPDATE SET value=excluded.value";
  if (sqlite3_prepare_v2(db_->handle, sql, -1, &statement, nullptr) != SQLITE_OK) return false;
  sqlite3_bind_text(statement, 1, key.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(statement, 2, value.c_str(), -1, SQLITE_TRANSIENT);
  const bool stored = sqlite3_step(statement) == SQLITE_DONE;
  sqlite3_finalize(statement);
  protectPreferenceFiles(path_);
  return stored;
}

std::optional<std::string> Preferences::get(const std::string& key) const {
  if (!ok_) return std::nullopt;
  sqlite3_stmt* statement = nullptr;
  if (sqlite3_prepare_v2(db_->handle, "SELECT value FROM user_preference WHERE key=?", -1, &statement, nullptr) !=
      SQLITE_OK) {
    return std::nullopt;
  }
  sqlite3_bind_text(statement, 1, key.c_str(), -1, SQLITE_TRANSIENT);
  std::optional<std::string> value;
  if (sqlite3_step(statement) == SQLITE_ROW) {
    const unsigned char* text = sqlite3_column_text(statement, 0);
    if (text != nullptr) value = reinterpret_cast<const char*>(text);
  }
  sqlite3_finalize(statement);
  return value;
}

}  // namespace timberlab::storage
