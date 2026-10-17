# ADR 0004 — Application license

## Status

Accepted as the provisional project license. Not a legal review.

## Decision

GPL-3.0-or-later for TimberLab's own source.

Checked against the Qt 6.8.3 SBOM installed in `deps/qt/6.8.3/gcc_64/sbom` and against the OCCT 7.9.2 headers. This is still not a legal review. The module terms are not uniform, so the program elects one Qt alternative and refuses the others.

The linked Qt modules (Core, Gui, OpenGL, Qml, Quick, QuickControls2) are each `LicenseRef-Qt-Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only`. This project elects **LGPL-3.0-only**. It does not elect GPL-2.0-only: that alternative cannot be combined with a GPL-3 application. Qt's GPL-3.0-only alternative is also unused, so Qt stays replaceable under the LGPL.

OCCT headers say "GNU Lesser General Public License version 2.1" with `OCCT_LGPL_EXCEPTION.txt`. They do not say "or any later version", so OCCT is LGPL-2.1-only plus that header exception, not LGPL-2.1-or-later. TimberLab does not copy OCCT source into the GPL tree. The Linux build links the shared libraries. LGPL-2.1 section 6 allows a work that uses the library under other terms when the user can relink. Relicensing OCCT itself under GPL-3 is not available and is not claimed.

MIT or Apache was not chosen for TimberLab. Either would forbid linking a later GPL-only Qt module. OpenSSL 3, which this program uses for SHA-256, is Apache-2.0, and Apache-2.0 is compatible with GPL-3. It is not compatible with GPL-2-only, which is another reason the application is GPL-3.

Qt Quick 3D is not installed in this prefix and is not linked. Qt's published 6.8 table lists Quick 3D and Quick 3D Physics as commercial or GPL-3.0-only, without the LGPL alternative the modules above have. Do not add them on the assumption that "Qt is LGPL".

Build tools in the same prefix (`moc`, `rcc`, `lrelease`, `lupdate`) are `GPL-3.0-only WITH Qt-GPL-exception-1.0`. They are not linked into the application. Qt Wayland's compositor module, and the dmabuf and EGL client-buffer plugins, are `GPL-3.0-only` in this SBOM. `WaylandClient` itself has the LGPL-3.0-only alternative. The verified Linux run uses the xcb platform plugin. Do not ship those GPL-only plugins as if they were LGPL. A GPL-3 application may load them; a proprietary fork may not.

## Consequences

Details and the third-party table are in `LICENSES.md`. A binary distribution has to ship TimberLab source (GPL-3) and offer the corresponding Qt and OCCT sources, with shared libraries the user can replace. The version-tag `.deb` is that distribution for Linux amd64: shared Qt and OCCT libraries, the git tag as TimberLab source, and the upstream archive URLs in `packaging/copyright`. A store package, and GPLv3's installation-information requirement on a locked tablet, are unchecked. Static linking would add the LGPL relink obligation that shared libraries already satisfy.
