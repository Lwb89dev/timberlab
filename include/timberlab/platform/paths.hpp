#pragma once

#include <string>

namespace timberlab::platform {

// TIMBERLAB_DATA_DIR overrides the location. Otherwise XDG_DATA_HOME/timberlab
// or ~/.local/share/timberlab. Tests and scripted runs should set the override
// so they do not write into the home directory.
std::string userDataDir();

}  // namespace timberlab::platform
