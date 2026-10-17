#include "timberlab/platform/paths.hpp"

#include <cstdlib>

namespace timberlab::platform {

std::string userDataDir() {
  if (const char* overrideDir = std::getenv("TIMBERLAB_DATA_DIR")) {
    if (overrideDir[0] != '\0') return overrideDir;
  }
  if (const char* xdg = std::getenv("XDG_DATA_HOME")) {
    if (xdg[0] != '\0') return std::string(xdg) + "/timberlab";
  }
  if (const char* home = std::getenv("HOME")) {
    if (home[0] != '\0') return std::string(home) + "/.local/share/timberlab";
  }
  return "timberlab-data";
}

}  // namespace timberlab::platform
