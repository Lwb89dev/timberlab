#pragma once

#include <string>

namespace timberlab {

// Prints save, load, rebuild, and export times for generated projects. No
// window. The numbers are for the machine that runs it.
int runBench();

// Writes a project of that many boards laid out on a grid, with a mortise and
// tenon on every other neighbouring pair, for the frame-rate run.
int writeBenchProject(const std::string& path, int boards);

}  // namespace timberlab
