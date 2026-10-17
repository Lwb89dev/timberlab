#pragma once

#include <optional>
#include <string>

namespace timberlab::storage {

// Global user preferences. Separate from project files.
// Schema migrations must keep existing rows.
class Preferences {
 public:
  explicit Preferences(const std::string& path);
  ~Preferences();

  Preferences(const Preferences&) = delete;
  Preferences& operator=(const Preferences&) = delete;

  bool ok() const { return ok_; }
  const std::string& error() const { return error_; }
  int schemaVersion() const { return schemaVersion_; }

  bool set(const std::string& key, const std::string& value);
  std::optional<std::string> get(const std::string& key) const;

 private:
  struct Sqlite;
  Sqlite* db_ = nullptr;
  std::string path_;
  bool ok_ = false;
  std::string error_;
  int schemaVersion_ = 0;
};

}  // namespace timberlab::storage
