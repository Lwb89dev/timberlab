#include "near.hpp"
#include "timberlab/core/document.hpp"
#include "timberlab/storage/archive.hpp"
#include "timberlab/storage/library.hpp"
#include "timberlab/storage/nostr.hpp"
#include "timberlab/storage/preferences.hpp"
#include "timberlab/storage/sha256.hpp"
#include "timberlab/storage/sync.hpp"

#include <openssl/crypto.h>
#include "timberlab/units/units.hpp"

#include <nlohmann/json.hpp>
#include <sqlite3.h>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>
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

TEST_CASE("a new library is schema 3 and round trips") {
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
    REQUIRE(library.schemaVersion() == 3);
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
  REQUIRE(again.schemaVersion() == 3);
  const auto materials = again.materials();
  REQUIRE(materials.size() == 1);
  REQUIRE(materials[0].revision == 1);
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
    REQUIRE(library.schemaVersion() == 3);
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
  REQUIRE(again.schemaVersion() == 3);
  REQUIRE(again.materials().size() == 2);
  REQUIRE(again.materials()[0].name == "Added after migration");
  REQUIRE(again.materials()[0].revision == 1);
  REQUIRE(again.presets()[0].revision == 1);
  REQUIRE(rawVersion(path) == 3);
}

TEST_CASE("a version 2 library gains a revision and keeps its rows") {
  const auto path = testDirectory() / "library-v2.sqlite";
  removeDatabase(path);
  {
    sqlite3* db = nullptr;
    REQUIRE(sqlite3_open_v2(path.string().c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) == SQLITE_OK);
    REQUIRE(execSql(db,
                    "CREATE TABLE schema_meta(version INTEGER NOT NULL);"
                    "INSERT INTO schema_meta(version) VALUES (2);"
                    "CREATE TABLE user_material(id TEXT PRIMARY KEY NOT NULL, name TEXT NOT NULL, visual_hex TEXT NOT NULL, "
                    "notes TEXT NOT NULL DEFAULT '');"
                    "CREATE TABLE user_preset(id TEXT PRIMARY KEY NOT NULL, label TEXT NOT NULL, thickness_nm INTEGER NOT NULL, "
                    "group_name TEXT NOT NULL DEFAULT 'user');"
                    "INSERT INTO user_material(id, name, visual_hex, notes) VALUES ('user_v2oak', 'Shop oak', '#112233', 'kept');"
                    "INSERT INTO user_preset(id, label, thickness_nm, group_name) VALUES ('user_v2mm', '18 mm', 18000000, 'user');"));
    sqlite3_close(db);
  }
  timberlab::storage::Library library(path.string());
  REQUIRE(library.ok());
  REQUIRE(library.schemaVersion() == 3);
  REQUIRE(library.materials().size() == 1);
  REQUIRE(library.materials()[0].notes == "kept");
  REQUIRE(library.materials()[0].revision == 1);
  REQUIRE(library.presets()[0].revision == 1);
  REQUIRE(rawVersion(path) == 3);
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
    REQUIRE(execSql(db, "UPDATE schema_meta SET version = 4"));
    sqlite3_close(db);
  }
  timberlab::storage::Library newer(path.string());
  REQUIRE_FALSE(newer.ok());
  REQUIRE(newer.error() == "library.version");
  REQUIRE(newer.materials().empty());
  REQUIRE(rawVersion(path) == 4);
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

std::string readBytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

TEST_CASE("a sealed library envelope updates the library and leaves the project file") {
  const auto libraryPath = testDirectory() / "library-sync.sqlite";
  const auto projectPath = testDirectory() / "library-sync.timberlab";
  removeDatabase(libraryPath);
  std::filesystem::remove(projectPath);
  unsigned char key[32];
  for (int i = 0; i < 32; ++i) key[i] = static_cast<unsigned char>(20 + i);

  timberlab::Document document;
  document.addBoard();
  std::string error;
  REQUIRE(document.setProjectName("Keep", error));
  REQUIRE(timberlab::storage::saveProject(projectPath.string(), document).ok);
  const std::string projectBefore = readBytes(projectPath);

  timberlab::storage::Library library(libraryPath.string());
  REQUIRE(library.ok());
  timberlab::storage::UserMaterial material;
  material.id = "user_sync";
  material.name = "Shop oak";
  material.visualHex = "#A67C52";
  material.notes = "local";
  REQUIRE(library.addMaterial(material, error));
  REQUIRE(library.materials()[0].revision == 1);

  timberlab::storage::SealedBlob sealed;
  REQUIRE(library.sealMaterial(material.id, key, sealed, error));
  std::string opened;
  REQUIRE(timberlab::storage::openAes256Gcm(key, sealed, opened, error));
  const auto parsed = nlohmann::json::parse(opened);
  REQUIRE(parsed["revision"] == 1);
  REQUIRE(parsed["contentHash"] == timberlab::storage::sha256Hex(parsed["payload"].dump()));

  nlohmann::json remote = parsed;
  remote["revision"] = 2;
  remote["payload"]["notes"] = "remote";
  remote["contentHash"] = timberlab::storage::sha256Hex(remote["payload"].dump());
  REQUIRE(timberlab::storage::sealAes256Gcm(key, remote.dump(), sealed, error));
  std::string winner;
  REQUIRE(library.importEnvelope(key, sealed, winner, error));
  REQUIRE(winner == "remote");
  REQUIRE(library.materials()[0].notes == "remote");
  REQUIRE(library.materials()[0].revision == 2);
  REQUIRE(readBytes(projectPath) == projectBefore);

  REQUIRE(library.importEnvelope(key, sealed, winner, error));
  REQUIRE(winner == "local");
  REQUIRE(library.materials()[0].notes == "remote");
  REQUIRE(readBytes(projectPath) == projectBefore);

  remote["payload"]["notes"] = "conflict";
  remote["contentHash"] = timberlab::storage::sha256Hex(remote["payload"].dump());
  REQUIRE(timberlab::storage::sealAes256Gcm(key, remote.dump(), sealed, error));
  REQUIRE_FALSE(library.importEnvelope(key, sealed, winner, error));
  REQUIRE(error == "sync.conflict");
  REQUIRE(winner.empty());
  REQUIRE(library.materials()[0].notes == "remote");
  REQUIRE(readBytes(projectPath) == projectBefore);

  unsigned char other[32];
  for (int i = 0; i < 32; ++i) other[i] = static_cast<unsigned char>(i);
  REQUIRE_FALSE(library.importEnvelope(other, sealed, winner, error));
  REQUIRE(error == "sync.crypto");
  REQUIRE(library.materials()[0].notes == "remote");
  REQUIRE(readBytes(projectPath) == projectBefore);

  timberlab::storage::UserPreset preset;
  preset.id = "user_syncmm";
  preset.label = "18 mm";
  preset.group = "user";
  preset.thicknessNm = 18 * kMm;
  REQUIRE(library.addPreset(preset, error));
  REQUIRE(library.sealPreset(preset.id, key, sealed, error));
  REQUIRE(timberlab::storage::openAes256Gcm(key, sealed, opened, error));
  nlohmann::json presetRemote = nlohmann::json::parse(opened);
  presetRemote["revision"] = 4;
  presetRemote["payload"]["label"] = "19 mm";
  presetRemote["contentHash"] = timberlab::storage::sha256Hex(presetRemote["payload"].dump());
  REQUIRE(timberlab::storage::sealAes256Gcm(key, presetRemote.dump(), sealed, error));
  REQUIRE(library.importEnvelope(key, sealed, winner, error));
  REQUIRE(winner == "remote");
  REQUIRE(library.presets()[0].label == "19 mm");
  REQUIRE(library.presets()[0].revision == 4);
  REQUIRE(library.materials()[0].notes == "remote");
  REQUIRE(readBytes(projectPath) == projectBefore);
}

TEST_CASE("a nostr library event updates one row and leaves the project file") {
  const auto libraryPath = testDirectory() / "library-event.sqlite";
  const auto projectPath = testDirectory() / "library-event.timberlab";
  removeDatabase(libraryPath);
  std::filesystem::remove(projectPath);
  timberlab::Document document;
  document.addBoard();
  std::string error;
  REQUIRE(document.setProjectName("Keep", error));
  REQUIRE(timberlab::storage::saveProject(projectPath.string(), document).ok);
  const std::string projectBefore = readBytes(projectPath);

  timberlab::storage::Library library(libraryPath.string());
  REQUIRE(library.ok());
  timberlab::storage::UserMaterial material;
  material.id = "user_event";
  material.name = "Shop oak";
  material.visualHex = "#A67C52";
  material.notes = "local";
  REQUIRE(library.addMaterial(material, error));
  std::string plaintext;
  REQUIRE(library.materialPlaintext(material.id, plaintext, error));
  std::string tag;
  REQUIRE(timberlab::storage::libraryTagFor("material", material.id, tag, error));
  unsigned char secret[32];
  REQUIRE(timberlab::storage::decodeNsec("nsec1vl029mgpspedva04g90vltkh6fvh240zqtv9k0t9af8935ke9laqsnlfe5", secret, error));
  unsigned char nonce[32] = {};
  unsigned char aux[32] = {};
  nonce[0] = 11;
  timberlab::storage::Event event;
  REQUIRE(timberlab::storage::sealLibraryEvent(secret, tag, plaintext, 1700001111, nonce, aux, event, error));
  const std::string file = timberlab::storage::libraryEventsToJson({event});
  std::vector<timberlab::storage::Event> events;
  REQUIRE(timberlab::storage::parseLibraryEvents(file, events, error));
  std::string openedTag;
  std::string opened;
  REQUIRE(timberlab::storage::openLibraryEvent(secret, events[0], openedTag, opened, error));
  REQUIRE(opened == plaintext);
  std::string winner;
  REQUIRE(library.importPlaintext(opened, winner, error));
  REQUIRE(winner == "local");
  REQUIRE(readBytes(projectPath) == projectBefore);

  nlohmann::json remote = nlohmann::json::parse(opened);
  remote["revision"] = 2;
  remote["payload"]["notes"] = "relay";
  remote["contentHash"] = timberlab::storage::sha256Hex(remote["payload"].dump());
  REQUIRE(library.importPlaintext(remote.dump(), winner, error));
  REQUIRE(winner == "remote");
  REQUIRE(library.materials()[0].notes == "relay");
  REQUIRE(library.materials()[0].revision == 2);
  REQUIRE(readBytes(projectPath) == projectBefore);

  remote["payload"]["notes"] = "conflict";
  remote["contentHash"] = timberlab::storage::sha256Hex(remote["payload"].dump());
  REQUIRE_FALSE(library.importPlaintext(remote.dump(), winner, error));
  REQUIRE(error == "sync.conflict");
  REQUIRE(library.materials()[0].notes == "relay");
  REQUIRE(readBytes(projectPath) == projectBefore);
  OPENSSL_cleanse(secret, sizeof(secret));
}
