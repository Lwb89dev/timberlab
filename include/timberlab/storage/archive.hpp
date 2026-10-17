#pragma once

#include "timberlab/core/document.hpp"

#include <string>

namespace timberlab::storage {

struct IoResult {
  bool ok = false;
  std::string error;
};

// .timberlab is a zip container: manifest.json + model.json.
// The model checksum is SHA-256 of the stored model bytes.
IoResult saveProject(const std::string& path, const Document& document);
IoResult loadProject(const std::string& path, Document& document);

}  // namespace timberlab::storage
