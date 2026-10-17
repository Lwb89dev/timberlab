#include "near.hpp"
#include "timberlab/core/document.hpp"
#include "timberlab/storage/archive.hpp"
#include "timberlab/storage/preferences.hpp"

#include <miniz.h>
#include <nlohmann/json.hpp>

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

TEST_CASE("a project archive round trips") {
  timberlab::Document document;
  document.addBoard();
  document.addBox();
  std::string error;
  REQUIRE(document.setProjectName("Stored", error));
  const auto path = testDirectory() / "round.timberlab";
  const auto saved = timberlab::storage::saveProject(path.string(), document);
  REQUIRE(saved.ok);
  timberlab::Document loaded;
  const auto opened = timberlab::storage::loadProject(path.string(), loaded);
  REQUIRE(opened.ok);
  REQUIRE(loaded.name() == "Stored");
  REQUIRE(loaded.components().size() == 2);
  REQUIRE(loaded.components().front().widthNm == document.components().front().widthNm);
}

bool ownerOnly(const std::filesystem::path& path) {
  const auto foreign = std::filesystem::perms::group_all | std::filesystem::perms::others_all;
  const auto mode = std::filesystem::status(path).permissions();
  return (mode & foreign) == std::filesystem::perms::none &&
         (mode & std::filesystem::perms::owner_read) != std::filesystem::perms::none;
}

TEST_CASE("a second save keeps the new project and the previous backup") {
  timberlab::Document document;
  document.addBoard();
  std::string error;
  REQUIRE(document.setProjectName("First", error));
  const auto path = testDirectory() / "twice.timberlab";
  REQUIRE(timberlab::storage::saveProject(path.string(), document).ok);
  REQUIRE(document.setProjectName("Second", error));
  REQUIRE(timberlab::storage::saveProject(path.string(), document).ok);
  REQUIRE(ownerOnly(path));
  REQUIRE(ownerOnly(path.string() + ".bak"));

  timberlab::Document loaded;
  REQUIRE(timberlab::storage::loadProject(path.string(), loaded).ok);
  REQUIRE(loaded.name() == "Second");
  timberlab::Document backup;
  REQUIRE(timberlab::storage::loadProject(path.string() + ".bak", backup).ok);
  REQUIRE(backup.name() == "First");
}

TEST_CASE("an oversized project file is rejected") {
  const auto path = testDirectory() / "huge.timberlab";
  {
    std::ofstream output(path, std::ios::binary);
    output << "x";
  }
  std::filesystem::resize_file(path, timberlab::storage::kMaxProjectBytes + 1);
  timberlab::Document rejected;
  const auto opened = timberlab::storage::loadProject(path.string(), rejected);
  REQUIRE_FALSE(opened.ok);
  REQUIRE(opened.error == "storage.too_large");
}

TEST_CASE("garbage and a bad checksum are rejected") {
  const auto garbage = testDirectory() / "garbage.timberlab";
  {
    std::ofstream output(garbage, std::ios::binary);
    output << "this is not a project";
  }
  timberlab::Document rejected;
  const auto bad = timberlab::storage::loadProject(garbage.string(), rejected);
  REQUIRE_FALSE(bad.ok);

  timberlab::Document document;
  document.addBoard();
  const std::string model = document.toJson();
  nlohmann::json manifest = {{"format", "timberlab"},
                             {"formatVersion", 1},
                             {"model", "model.json"},
                             {"modelSha256", "0000000000000000000000000000000000000000000000000000000000000000"}};
  const std::string manifestText = manifest.dump();
  const auto path = testDirectory() / "bad-checksum.timberlab";
  mz_zip_archive zip;
  mz_zip_zero_struct(&zip);
  REQUIRE(mz_zip_writer_init_file(&zip, path.string().c_str(), 0));
  REQUIRE(mz_zip_writer_add_mem(&zip, "manifest.json", manifestText.data(), manifestText.size(), MZ_BEST_COMPRESSION));
  REQUIRE(mz_zip_writer_add_mem(&zip, "model.json", model.data(), model.size(), MZ_BEST_COMPRESSION));
  REQUIRE(mz_zip_writer_finalize_archive(&zip));
  mz_zip_writer_end(&zip);

  const auto checksum = timberlab::storage::loadProject(path.string(), rejected);
  REQUIRE_FALSE(checksum.ok);
  REQUIRE(checksum.error == "storage.checksum");
}

std::string fileBytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

TEST_CASE("restore copies a valid backup and leaves the project when the backup is bad") {
  timberlab::Document document;
  document.addBoard();
  std::string error;
  REQUIRE(document.setProjectName("First", error));
  const auto path = testDirectory() / "restore.timberlab";
  REQUIRE(timberlab::storage::saveProject(path.string(), document).ok);
  REQUIRE(document.setProjectName("Second", error));
  REQUIRE(timberlab::storage::saveProject(path.string(), document).ok);

  const std::string second = fileBytes(path);
  {
    std::ofstream output(path.string() + ".bak", std::ios::binary | std::ios::trunc);
    output << "not a backup";
  }
  timberlab::Document untouched;
  untouched.addBox();
  const auto rejected = timberlab::storage::restoreBackup(path.string(), untouched);
  REQUIRE_FALSE(rejected.ok);
  REQUIRE(rejected.error == "storage.format");
  REQUIRE(fileBytes(path) == second);
  REQUIRE(untouched.components().size() == 1);
  REQUIRE(untouched.components().front().type == timberlab::ComponentType::Box);

  REQUIRE(std::filesystem::remove(path.string() + ".bak"));
  const auto missing = timberlab::storage::restoreBackup(path.string(), untouched);
  REQUIRE_FALSE(missing.ok);
  REQUIRE(missing.error == "storage.restore");
  REQUIRE(fileBytes(path) == second);

  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "damaged project";
  }
  timberlab::Document refused;
  const auto loaded = timberlab::storage::loadProject(path.string(), refused);
  REQUIRE_FALSE(loaded.ok);
  REQUIRE(fileBytes(path) == "damaged project");

  REQUIRE(timberlab::storage::saveProject(path.string(), document).ok);
  timberlab::Document older;
  older.addBoard();
  REQUIRE(older.setProjectName("First", error));
  REQUIRE(timberlab::storage::saveProject((path.string() + ".hold"), older).ok);
  std::filesystem::copy_file(path.string() + ".hold", path.string() + ".bak",
                             std::filesystem::copy_options::overwrite_existing);
  {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "damaged project";
  }
  timberlab::Document ignored;
  REQUIRE_FALSE(timberlab::storage::loadProject(path.string(), ignored).ok);
  REQUIRE(fileBytes(path) == "damaged project");
  timberlab::Document restored;
  REQUIRE(timberlab::storage::restoreBackup(path.string(), restored).ok);
  REQUIRE(restored.name() == "First");
  timberlab::Document reloaded;
  REQUIRE(timberlab::storage::loadProject(path.string(), reloaded).ok);
  REQUIRE(reloaded.name() == "First");
  REQUIRE(fileBytes(path) != "damaged project");
}

TEST_CASE("raw model json still loads") {
  timberlab::Document document;
  document.addBoard();
  const auto path = testDirectory() / "raw.json";
  {
    std::ofstream output(path);
    output << document.toJson();
  }
  timberlab::Document loaded;
  const auto opened = timberlab::storage::loadProject(path.string(), loaded);
  REQUIRE(opened.ok);
  REQUIRE(loaded.components().size() == 1);
}

TEST_CASE("preferences survive reopening the database") {
  const auto path = testDirectory() / "prefs.sqlite";
  std::filesystem::remove(path);
  timberlab::storage::Preferences preferences(path.string());
  REQUIRE(preferences.ok());
  REQUIRE(preferences.schemaVersion() == 1);
  REQUIRE(preferences.set("display_unit", "in"));
  REQUIRE(ownerOnly(path));
  timberlab::storage::Preferences again(path.string());
  const auto stored = again.get("display_unit");
  REQUIRE(stored.has_value());
  REQUIRE(*stored == "in");
}

TEST_CASE("a preferences path that cannot be opened stays a stable code") {
  const auto dir = testDirectory() / "not-a-database";
  std::filesystem::create_directories(dir);
  timberlab::storage::Preferences preferences(dir.string());
  REQUIRE_FALSE(preferences.ok());
  REQUIRE(preferences.error() == "preferences.open");
}
