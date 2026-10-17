#include "timberlab/storage/archive.hpp"

#include <miniz.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace timberlab::storage {

std::string sha256Hex(const std::string& data);

namespace {

IoResult fail(const char* code) {
  IoResult result;
  result.error = code;
  return result;
}

std::string readFile(const std::string& path, std::string& error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "storage.io";
    return {};
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

std::string zipEntry(mz_zip_archive& zip, const char* name, std::string& error) {
  int index = mz_zip_reader_locate_file(&zip, name, nullptr, 0);
  if (index < 0) {
    error = "storage.format";
    return {};
  }
  size_t size = 0;
  void* data = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(index), &size, 0);
  if (data == nullptr) {
    error = "storage.io";
    return {};
  }
  std::string text(static_cast<char*>(data), size);
  mz_free(data);
  return text;
}

}  // namespace

IoResult saveProject(const std::string& path, const Document& document) {
  const std::string model = document.toJson();
  nlohmann::json manifest = {{"format", "timberlab"},
                             {"formatVersion", 1},
                             {"createdBy", "TimberLab 0.1.0"},
                             {"model", "model.json"},
                             {"modelSha256", sha256Hex(model)}};
  const std::string manifestText = manifest.dump(2);

  const std::filesystem::path target(path);
  const std::filesystem::path temporary = target.string() + ".tmp";
  std::error_code ec;
  if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ec);

  mz_zip_archive zip;
  mz_zip_zero_struct(&zip);
  if (!mz_zip_writer_init_file(&zip, temporary.string().c_str(), 0)) return fail("storage.io");
  const bool manifestOk =
      mz_zip_writer_add_mem(&zip, "manifest.json", manifestText.data(), manifestText.size(), MZ_BEST_COMPRESSION) ==
      MZ_TRUE;
  const bool modelOk = mz_zip_writer_add_mem(&zip, "model.json", model.data(), model.size(), MZ_BEST_COMPRESSION) == MZ_TRUE;
  if (!manifestOk || !modelOk || !mz_zip_writer_finalize_archive(&zip)) {
    mz_zip_writer_end(&zip);
    std::filesystem::remove(temporary);
    return fail("storage.io");
  }
  mz_zip_writer_end(&zip);

  if (std::filesystem::exists(target)) {
    std::filesystem::copy_file(target, target.string() + ".bak", std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      std::filesystem::remove(temporary);
      return fail("storage.backup");
    }
  }
  std::filesystem::rename(temporary, target, ec);
  if (ec) {
    std::filesystem::remove(target, ec);
    std::filesystem::rename(temporary, target, ec);
  }
  if (ec) return fail("storage.io");
  return IoResult{true, {}};
}

IoResult loadProject(const std::string& path, Document& document) {
  std::string ignored;
  const std::string bytes = readFile(path, ignored);
  if (bytes.empty() && ignored == "storage.io") return fail("storage.io");
  if (!bytes.empty() && bytes[0] == '{') {
    std::string error;
    if (!Document::fromJson(bytes, document, error)) return fail(error.c_str());
    return IoResult{true, {}};
  }

  mz_zip_archive zip;
  mz_zip_zero_struct(&zip);
  if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) return fail("storage.format");
  std::string error;
  const std::string manifestText = zipEntry(zip, "manifest.json", error);
  const std::string model = error.empty() ? zipEntry(zip, "model.json", error) : std::string();
  mz_zip_reader_end(&zip);
  if (!error.empty()) return fail(error.c_str());

  nlohmann::json manifest;
  try {
    manifest = nlohmann::json::parse(manifestText);
  } catch (const nlohmann::json::exception&) {
    return fail("storage.format");
  }
  if (manifest.value("format", "") != "timberlab" || manifest.value("formatVersion", 0) != 1) {
    return fail("storage.format");
  }
  if (manifest.value("modelSha256", "") != sha256Hex(model)) return fail("storage.checksum");
  if (!Document::fromJson(model, document, error)) return fail(error.c_str());
  return IoResult{true, {}};
}

}  // namespace timberlab::storage
