#pragma once

namespace timberlab {

// Headless editor path: the same controller calls the inspector uses.
// Does not open a window. Requires a QCoreApplication.
int runAccept();

}  // namespace timberlab
