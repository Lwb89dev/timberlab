#include "timberlab/storage/archive.hpp"
#include "timberlab/storage/sha256.hpp"

#include <miniz.h>

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <sstream>

namespace timberlab::storage {
namespace {

IoResult fail(const char* code) {
  IoResult result;
  result.error = code;
  return result;
}

IoResult fail(const std::string& code) { return fail(code.c_str()); }

void ownerOnly(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::permissions(path, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace, ec);
}

std::string readFile(const std::string& path, std::string& error) {
  std::error_code ec;
  const auto size = std::filesystem::file_size(path, ec);
  if (ec) {
    error = "storage.io";
    return {};
  }
  if (size > kMaxProjectBytes) {
    error = "storage.too_large";
    return {};
  }
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    error = "storage.io";
    return {};
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (!input && !input.eof()) {
    error = "storage.io";
    return {};
  }
  return buffer.str();
}

std::string zipEntry(mz_zip_archive& zip, const char* name, std::string& error) {
  const int index = mz_zip_reader_locate_file(&zip, name, nullptr, 0);
  if (index < 0) {
    error = "storage.format";
    return {};
  }
  mz_zip_archive_file_stat stat;
  if (!mz_zip_reader_file_stat(&zip, static_cast<mz_uint>(index), &stat)) {
    error = "storage.io";
    return {};
  }
  if (stat.m_uncomp_size > kMaxProjectBytes) {
    error = "storage.too_large";
    return {};
  }
  size_t size = 0;
  void* data = mz_zip_reader_extract_to_heap(&zip, static_cast<mz_uint>(index), &size, 0);
  if (data == nullptr) {
    error = "storage.io";
    return {};
  }
  if (size > kMaxProjectBytes) {
    mz_free(data);
    error = "storage.too_large";
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
                             {"createdBy", "TimberLab 0.2.0"},
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

  ownerOnly(temporary);
  if (std::filesystem::exists(target)) {
    const std::filesystem::path backup = target.string() + ".bak";
    std::filesystem::copy_file(target, backup, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
      std::filesystem::remove(temporary, ec);
      return fail("storage.backup");
    }
    ownerOnly(backup);
  }
  std::filesystem::rename(temporary, target, ec);
  if (ec) {
    std::error_code removeError;
    std::filesystem::remove(temporary, removeError);
    return fail("storage.io");
  }
  ownerOnly(target);
  return IoResult{true, {}};
}

IoResult loadProject(const std::string& path, Document& document) {
  std::string error;
  const std::string bytes = readFile(path, error);
  if (!error.empty()) return fail(error);
  if (!bytes.empty() && bytes[0] == '{') {
    if (!Document::fromJson(bytes, document, error)) return fail(error);
    return IoResult{true, {}};
  }

  mz_zip_archive zip;
  mz_zip_zero_struct(&zip);
  if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) return fail("storage.format");
  const std::string manifestText = zipEntry(zip, "manifest.json", error);
  const std::string model = error.empty() ? zipEntry(zip, "model.json", error) : std::string();
  mz_zip_reader_end(&zip);
  if (!error.empty()) return fail(error);

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
  if (!Document::fromJson(model, document, error)) return fail(error);
  return IoResult{true, {}};
}

IoResult restoreBackup(const std::string& path, Document& document) {
  const std::filesystem::path target(path);
  const std::filesystem::path backup = target.string() + ".bak";
  std::error_code ec;
  if (!std::filesystem::is_regular_file(backup, ec)) return fail("storage.restore");
  Document candidate;
  const IoResult opened = loadProject(backup.string(), candidate);
  if (!opened.ok) return opened;

  const std::filesystem::path temporary = target.string() + ".tmp";
  std::filesystem::copy_file(backup, temporary, std::filesystem::copy_options::overwrite_existing, ec);
  if (ec) {
    std::filesystem::remove(temporary, ec);
    return fail("storage.io");
  }
  ownerOnly(temporary);
  std::filesystem::rename(temporary, target, ec);
  if (ec) {
    std::error_code removeError;
    std::filesystem::remove(temporary, removeError);
    return fail("storage.io");
  }
  ownerOnly(target);
  document = std::move(candidate);
  return IoResult{true, {}};
}

}  // namespace timberlab::storage
