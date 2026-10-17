#include "near.hpp"
#include "timberlab/storage/library.hpp"
#include "timberlab/storage/preferences.hpp"
#include "timberlab/units/units.hpp"

#include <sqlite3.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>

namespace {

constexpr std::int64_t kMm = timberlab::units::kNmPerMm;

void removeDatabase(const std::filesystem::path& path) {
  std::filesystem::remove(path);
  std::filesystem::remove(std::filesystem::path(path.string() + "-wal"));
  std::filesystem::remove(std::filesystem::path(path.string() + "-shm"));
}

bool ownerOnly(const std::filesystem::path& path) {
  std::error_code ec;
  if (!std::filesystem::exists(path, ec)) return true;
  const auto foreign = std::filesystem::perms::group_all | std::filesystem::perms::others_all;
  const auto mode = std::filesystem::status(path).permissions();
  return (mode & foreign) == std::filesystem::perms::none &&
         (mode & std::filesystem::perms::owner_read) != std::filesystem::perms::none;
}

bool execSql(sqlite3* db, const char* sql) {
  char* message = nullptr;
  const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &message);
  sqlite3_free(message);
  return rc == SQLITE_OK;
}

bool plantVersion1(const std::filesystem::path& path) {
  removeDatabase(path);
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
    sqlite3_close(db);
    return false;
  }
  const bool planted = execSql(
      db,
      "CREATE TABLE schema_meta(version INTEGER NOT NULL);"
      "INSERT INTO schema_meta(version) VALUES (1);"
      "CREATE TABLE user_material(id TEXT PRIMARY KEY NOT NULL, name TEXT NOT NULL, visual_hex TEXT NOT NULL);"
      "CREATE TABLE user_preset(id TEXT PRIMARY KEY NOT NULL, label TEXT NOT NULL, thickness_nm INTEGER NOT NULL);"
      "INSERT INTO user_material(id, name, visual_hex) VALUES ('user_v1oak', 'Shop oak', '#112233');"
      "INSERT INTO user_preset(id, label, thickness_nm) VALUES ('user_v1mm', '18 mm', 18000000);");
  sqlite3_close(db);
  return planted;
}

int rawVersion(const std::filesystem::path& path) {
  sqlite3* db = nullptr;
  if (sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
    sqlite3_close(db);
    return -1;
  }
  sqlite3_stmt* statement = nullptr;
  int version = -1;
  if (sqlite3_prepare_v2(db, "SELECT version FROM schema_meta LIMIT 1", -1, &statement, nullptr) == SQLITE_OK) {
    if (sqlite3_step(statement) == SQLITE_ROW) version = sqlite3_column_int(statement, 0);
    sqlite3_finalize(statement);
  }
  sqlite3_close(db);
  return version;
}

}  // namespace

TEST_CASE("a new library is schema 2 and round trips") {
  const auto path = testDirectory() / "library.sqlite";
  removeDatabase(path);
  std::string error;
  timberlab::storage::UserMaterial material;
  material.id = "user_shopoak";
  material.name = "Shop oak";
  material.visualHex = "#A67C52";
  material.notes = "quercia di bottega";
  {
    timberlab::storage::Library library(path.string());
    REQUIRE(library.ok());
    REQUIRE(library.schemaVersion() == 2);
    REQUIRE(library.addMaterial(material, error));
    timberlab::storage::UserPreset preset;
    preset.id = "user_18mm";
    preset.label = "18 mm";
    preset.group = "user";
    preset.thicknessNm = 18 * kMm;
    REQUIRE(library.addPreset(preset, error));
    REQUIRE(ownerOnly(path));
    REQUIRE(ownerOnly(std::filesystem::path(path.string() + "-wal")));
    REQUIRE(ownerOnly(std::filesystem::path(path.string() + "-shm")));
  }

  timberlab::storage::Library again(path.string());
  REQUIRE(again.ok());
  REQUIRE(again.schemaVersion() == 2);
  const auto materials = again.materials();
  REQUIRE(materials.size() == 1);
  REQUIRE(materials[0].id == material.id);
  REQUIRE(materials[0].name == material.name);
  REQUIRE(materials[0].visualHex == material.visualHex);
  REQUIRE(materials[0].notes == material.notes);
  const auto presets = again.presets();
  REQUIRE(presets.size() == 1);
  REQUIRE(presets[0].thicknessNm == 18 * kMm);
  REQUIRE(presets[0].group == "user");
  REQUIRE(again.removeMaterial(material.id, error));
  REQUIRE(again.materials().empty());
  REQUIRE_FALSE(again.removeMaterial(material.id, error));
  REQUIRE(error == "library.missing");
  REQUIRE(again.removePreset("user_18mm", error));
  REQUIRE(again.presets().empty());
}

TEST_CASE("a version 1 library migrates and keeps its rows") {
  const auto path = testDirectory() / "library-v1.sqlite";
  REQUIRE(plantVersion1(path));
  {
    timberlab::storage::Library library(path.string());
    REQUIRE(library.ok());
    REQUIRE(library.schemaVersion() == 2);
    const auto materials = library.materials();
    REQUIRE(materials.size() == 1);
    REQUIRE(materials[0].id == "user_v1oak");
    REQUIRE(materials[0].name == "Shop oak");
    REQUIRE(materials[0].visualHex == "#112233");
    REQUIRE(materials[0].notes.empty());
    const auto presets = library.presets();
    REQUIRE(presets.size() == 1);
    REQUIRE(presets[0].id == "user_v1mm");
    REQUIRE(presets[0].label == "18 mm");
    REQUIRE(presets[0].thicknessNm == 18 * kMm);
    REQUIRE(presets[0].group == "user");

    timberlab::storage::UserMaterial added;
    added.id = "user_added";
    added.name = "Added after migration";
    added.visualHex = "#ABCDEF";
    std::string error;
    REQUIRE(library.addMaterial(added, error));
  }

  timberlab::storage::Library again(path.string());
  REQUIRE(again.schemaVersion() == 2);
  REQUIRE(again.materials().size() == 2);
  REQUIRE(again.materials()[0].name == "Added after migration");
  REQUIRE(rawVersion(path) == 2);
}

TEST_CASE("a newer library and a preferences file are refused without losing rows") {
  const auto path = testDirectory() / "library-future.sqlite";
  removeDatabase(path);
  {
    timberlab::storage::Library library(path.string());
    REQUIRE(library.ok());
    timberlab::storage::UserMaterial material;
    material.id = "user_keep";
    material.name = "Keep me";
    material.visualHex = "#010203";
    std::string error;
    REQUIRE(library.addMaterial(material, error));
  }
  {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK);
    REQUIRE(execSql(db, "UPDATE schema_meta SET version = 3"));
    sqlite3_close(db);
  }
  timberlab::storage::Library newer(path.string());
  REQUIRE_FALSE(newer.ok());
  REQUIRE(newer.error() == "library.version");
  REQUIRE(newer.materials().empty());
  REQUIRE(rawVersion(path) == 3);
  {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READONLY, nullptr) == SQLITE_OK);
    sqlite3_stmt* statement = nullptr;
    REQUIRE(sqlite3_prepare_v2(db, "SELECT name FROM user_material", -1, &statement, nullptr) == SQLITE_OK);
    REQUIRE(sqlite3_step(statement) == SQLITE_ROW);
    REQUIRE(std::string(reinterpret_cast<const char*>(sqlite3_column_text(statement, 0))) == "Keep me");
    sqlite3_finalize(statement);
    sqlite3_close(db);
  }

  const auto preferencesPath = testDirectory() / "library-prefs.sqlite";
  removeDatabase(preferencesPath);
  {
    timberlab::storage::Preferences preferences(preferencesPath.string());
    REQUIRE(preferences.ok());
    REQUIRE(preferences.set("display_unit", "in"));
  }
  timberlab::storage::Library mistaken(preferencesPath.string());
  REQUIRE_FALSE(mistaken.ok());
  REQUIRE(mistaken.error() == "library.schema");
  timberlab::storage::Preferences still(preferencesPath.string());
  REQUIRE(still.ok());
  REQUIRE(still.schemaVersion() == 1);
  const auto stored = still.get("display_unit");
  REQUIRE(stored.has_value());
  REQUIRE(*stored == "in");
}

TEST_CASE("a library rejects a bad entry and a path that is not a file") {
  const auto path = testDirectory() / "library-reject.sqlite";
  removeDatabase(path);
  timberlab::storage::Library library(path.string());
  REQUIRE(library.ok());
  std::string error;
  timberlab::storage::UserMaterial material;
  material.id = "user_bad";
  material.name = "Named";
  material.visualHex = "#A67C52";
  material.name.clear();
  REQUIRE_FALSE(library.addMaterial(material, error));
  REQUIRE(error == "name.empty");
  material.name.assign(timberlab::storage::Library::kMaxNameBytes + 1, 'a');
  REQUIRE_FALSE(library.addMaterial(material, error));
  REQUIRE(error == "name.too_long");
  material.name = "Named";
  material.notes.assign(timberlab::storage::Library::kMaxNotesBytes + 1, 'n');
  REQUIRE_FALSE(library.addMaterial(material, error));
  REQUIRE(error == "notes.too_long");
  material.notes.clear();
  material.visualHex = "A67C52";
  REQUIRE_FALSE(library.addMaterial(material, error));
  REQUIRE(error == "library.range");
  material.visualHex = "#A67C52";
  material.id = "Has space";
  REQUIRE_FALSE(library.addMaterial(material, error));
  REQUIRE(error == "library.range");
  material.id = "user_bad";
  REQUIRE(library.addMaterial(material, error));
  REQUIRE_FALSE(library.addMaterial(material, error));
  REQUIRE(error == "library.duplicate");
  REQUIRE(library.materials().size() == 1);

  timberlab::storage::UserPreset preset;
  preset.id = "user_thick";
  preset.label = "18 mm";
  preset.group = "user";
  preset.thicknessNm = 0;
  REQUIRE_FALSE(library.addPreset(preset, error));
  REQUIRE(error == "library.range");
  preset.thicknessNm = timberlab::units::kMaxPartNm + 1;
  REQUIRE_FALSE(library.addPreset(preset, error));
  REQUIRE(error == "library.range");
  preset.thicknessNm = timberlab::units::kMaxPartNm;
  REQUIRE(library.addPreset(preset, error));
  REQUIRE(library.presets()[0].thicknessNm == timberlab::units::kMaxPartNm);
  REQUIRE(library.materials().size() == 1);

  const auto dir = testDirectory() / "library-not-a-database";
  std::filesystem::create_directories(dir);
  timberlab::storage::Library blocked(dir.string());
  REQUIRE_FALSE(blocked.ok());
  REQUIRE(blocked.error() == "library.open");
}

TEST_CASE("a library stops at its row cap") {
  const auto path = testDirectory() / "library-full.sqlite";
  removeDatabase(path);
  timberlab::storage::Library library(path.string());
  REQUIRE(library.ok());
  std::string error;
  for (std::size_t i = 0; i < timberlab::storage::Library::kMaxRows; ++i) {
    timberlab::storage::UserMaterial material;
    material.id = "user_row" + std::to_string(i);
    material.name = "Row " + std::to_string(i);
    material.visualHex = "#112233";
    REQUIRE(library.addMaterial(material, error));
  }
  timberlab::storage::UserMaterial extra;
  extra.id = "user_overflow";
  extra.name = "Overflow";
  extra.visualHex = "#112233";
  REQUIRE_FALSE(library.addMaterial(extra, error));
  REQUIRE(error == "library.too_many");
  REQUIRE(library.materials().size() == timberlab::storage::Library::kMaxRows);
}
