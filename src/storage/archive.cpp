#include "timberlab/storage/archive.hpp"
#include "timberlab/storage/sha256.hpp"

#include <miniz.h>

#include <nlohmann/json.hpp>
#include <openssl/rand.h>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
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

// A new file beside target that only the owner can read. The name is random,
// and O_EXCL with O_NOFOLLOW refuses a file or symlink planted under it, so a
// save into a shared folder cannot be redirected onto another file.
int createPrivateTemp(const std::filesystem::path& target, std::filesystem::path& temporary) {
  static constexpr char kHex[] = "0123456789abcdef";
  for (int attempt = 0; attempt < 16; ++attempt) {
    unsigned char raw[8];
    if (RAND_bytes(raw, sizeof(raw)) != 1) return -1;
    std::string suffix;
    for (const unsigned char byte : raw) {
      suffix.push_back(kHex[byte >> 4]);
      suffix.push_back(kHex[byte & 0x0f]);
    }
    temporary = target.string() + "." + suffix + ".tmp";
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, S_IRUSR | S_IWUSR);
    if (fd >= 0) return fd;
    if (errno != EEXIST) return -1;
  }
  return -1;
}

// The rename is durable only after the folder entry reaches the disk too.
void syncParent(const std::filesystem::path& target) {
  const std::filesystem::path parent = target.has_parent_path() ? target.parent_path() : std::filesystem::path(".");
  const int fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
  if (fd < 0) return;
  ::fsync(fd);
  ::close(fd);
}

bool writeAll(int fd, const char* data, std::size_t size) {
  while (size > 0) {
    const ssize_t written = ::write(fd, data, size);
    if (written < 0 && errno == EINTR) continue;
    if (written <= 0) return false;
    data += written;
    size -= static_cast<std::size_t>(written);
  }
  return true;
}

// Copies source to a private temporary file, then renames it onto
// destination. A rename replaces a symlink at destination rather than
// writing through it.
bool copyPrivate(const std::filesystem::path& source, const std::filesystem::path& destination) {
  const int in = ::open(source.c_str(), O_RDONLY | O_CLOEXEC);
  if (in < 0) return false;
  std::filesystem::path temporary;
  const int out = createPrivateTemp(destination, temporary);
  bool ok = out >= 0;
  char buffer[65536];
  while (ok) {
    const ssize_t got = ::read(in, buffer, sizeof(buffer));
    if (got < 0 && errno == EINTR) continue;
    if (got <= 0) {
      ok = got == 0;
      break;
    }
    ok = writeAll(out, buffer, static_cast<std::size_t>(got));
  }
  ::close(in);
  if (out >= 0) {
    ok = ::fsync(out) == 0 && ok;
    ok = ::close(out) == 0 && ok;
  }
  std::error_code ec;
  if (ok) std::filesystem::rename(temporary, destination, ec);
  if (!ok || ec) {
    if (out >= 0) std::filesystem::remove(temporary, ec);
    return false;
  }
  syncParent(destination);
  return true;
}

// Writes the zip through a private temporary file and flushes it to disk.
bool writeArchive(const std::filesystem::path& target, const std::string& manifestText, const std::string& model,
                  std::filesystem::path& temporary) {
  const int fd = createPrivateTemp(target, temporary);
  if (fd < 0) return false;
  std::FILE* file = ::fdopen(fd, "wb");
  if (file == nullptr) {
    ::close(fd);
    std::error_code ec;
    std::filesystem::remove(temporary, ec);
    return false;
  }
  mz_zip_archive zip;
  mz_zip_zero_struct(&zip);
  bool ok = mz_zip_writer_init_cfile(&zip, file, 0) == MZ_TRUE;
  ok = ok && mz_zip_writer_add_mem(&zip, "manifest.json", manifestText.data(), manifestText.size(),
                                   MZ_BEST_COMPRESSION) == MZ_TRUE;
  ok = ok && mz_zip_writer_add_mem(&zip, "model.json", model.data(), model.size(), MZ_BEST_COMPRESSION) == MZ_TRUE;
  ok = ok && mz_zip_writer_finalize_archive(&zip) == MZ_TRUE;
  mz_zip_writer_end(&zip);
  ok = std::fflush(file) == 0 && ok;
  ok = ::fsync(::fileno(file)) == 0 && ok;
  ok = std::fclose(file) == 0 && ok;
  if (!ok) {
    std::error_code ec;
    std::filesystem::remove(temporary, ec);
  }
  return ok;
}

// The manifest is read by type, because json::value throws on a key of the
// wrong type and a project file is untrusted input.
std::string checkManifest(const std::string& text, const std::string& model) {
  const nlohmann::json manifest = nlohmann::json::parse(text, nullptr, false);
  if (manifest.is_discarded() || !manifest.is_object()) return "storage.format";
  const auto format = manifest.find("format");
  const auto version = manifest.find("formatVersion");
  if (format == manifest.end() || !format->is_string() || format->get<std::string>() != "timberlab") {
    return "storage.format";
  }
  if (version == manifest.end() || !version->is_number_integer() || version->get<std::int64_t>() != 1) {
    return "storage.format";
  }
  const auto checksum = manifest.find("modelSha256");
  if (checksum == manifest.end() || !checksum->is_string() || checksum->get<std::string>() != sha256Hex(model)) {
    return "storage.checksum";
  }
  return {};
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
  std::string bytes = buffer.str();
  // The file can grow between the size check and the read.
  if (bytes.size() > kMaxProjectBytes) {
    error = "storage.too_large";
    return {};
  }
  return bytes;
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
                             {"createdBy", "TimberLab " TIMBERLAB_VERSION},
                             {"model", "model.json"},
                             {"modelSha256", sha256Hex(model)}};
  const std::string manifestText = manifest.dump(2);

  const std::filesystem::path target(path);
  std::error_code ec;
  if (target.has_parent_path()) std::filesystem::create_directories(target.parent_path(), ec);

  std::filesystem::path temporary;
  if (!writeArchive(target, manifestText, model, temporary)) return fail("storage.io");
  if (std::filesystem::exists(target, ec) && !copyPrivate(target, target.string() + ".bak")) {
    std::filesystem::remove(temporary, ec);
    return fail("storage.backup");
  }
  std::filesystem::rename(temporary, target, ec);
  if (ec) {
    std::error_code removeError;
    std::filesystem::remove(temporary, removeError);
    return fail("storage.io");
  }
  syncParent(target);
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

  // The zip is read from the bytes already checked, not opened a second time.
  mz_zip_archive zip;
  mz_zip_zero_struct(&zip);
  if (!mz_zip_reader_init_mem(&zip, bytes.data(), bytes.size(), 0)) return fail("storage.format");
  const std::string manifestText = zipEntry(zip, "manifest.json", error);
  const std::string model = error.empty() ? zipEntry(zip, "model.json", error) : std::string();
  mz_zip_reader_end(&zip);
  if (!error.empty()) return fail(error);

  error = checkManifest(manifestText, model);
  if (!error.empty()) return fail(error);
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

  if (!copyPrivate(backup, target)) return fail("storage.io");
  document = std::move(candidate);
  return IoResult{true, {}};
}

}  // namespace timberlab::storage
