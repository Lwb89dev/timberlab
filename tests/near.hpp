#pragma once

#include <cmath>
#include <filesystem>

inline bool nearRelative(double value, double expected, double relative) {
  const double scale = std::max(1.0, std::abs(expected));
  return std::abs(value - expected) <= relative * scale;
}

inline std::filesystem::path testDirectory() {
  const std::filesystem::path root(TIMBERLAB_TEST_DIR);
  std::filesystem::create_directories(root);
  return root;
}
