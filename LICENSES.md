# Licenses

The application source is released under **GPL-3.0-or-later**. The full text is `LICENSE`. This is a project decision recorded so the dependency obligations stay visible. It is not a lawyer's opinion.

## Why GPL-3.0-or-later

The desktop build links Qt 6 modules that offer an LGPL-3.0-only alternative, and Open CASCADE under LGPL-2.1 (not "or later") with the OCCT header exception. A GPL-3.0-or-later program can use those libraries as separate shared libraries. It must not absorb OCCT's source: the headers do not grant an upgrade to LGPL-3 or GPL-3. An MIT or Apache application could not later take a GPL-only Qt module. OpenSSL 3 (Apache-2.0) is compatible with GPL-3 and is not compatible with GPL-2-only.

The modules this program links were read from the installed Qt 6.8.3 SPDX files (`deps/qt/6.8.3/gcc_64/sbom`, not committed). Core, Gui, OpenGL, Qml, Quick, and QuickControls2 are each `Commercial OR LGPL-3.0-only OR GPL-2.0-only OR GPL-3.0-only`. This project elects LGPL-3.0-only. It does not elect the GPL-2.0-only alternative.

Qt Quick 3D is not installed here, so it has no SPDX entry in this tree. Qt's `qtdoc` license page lists Qt Quick 3D, Qt Quick 3D Physics, and Qt Wayland Compositor among the modules that are GPL rather than LGPL. This program does not link them. In the installed Wayland SBOM, `WaylandClient` has the LGPL-3.0-only alternative, while the compositor module and the dmabuf and EGL client-buffer plugins are GPL-3.0-only. The verified run uses xcb. Do not ship those GPL-only plugins as LGPL.

`moc`, `rcc`, `lrelease`, and `lupdate` are GPL-3.0-only with the Qt GPL exception. They run at build time and are not linked into the application. LinguistTools is a host tool. An Android cross build still needs them on the host.

LGPL still has source and relink obligations if a binary is shipped. Shipping is not happening. When it does, TimberLab's source travels with the binary (GPL-3), and the corresponding Qt and OCCT sources, or an offer for them, travel too. Shared libraries are how this Linux build meets the relink requirement. A store package and GPLv3 installation information for a locked tablet have not been checked.

## Other components

| Piece | Terms | Role |
|---|---|---|
| Open CASCADE 7.9.2 | LGPL-2.1 only, plus the OCCT header exception. Not "or later". See `deps/OCCT-7_9_2/LICENSE_LGPL_21.txt` and `OCCT_LGPL_EXCEPTION.txt`. | Solid kernel, shared libraries. Not committed. |
| Qt 6.8.3 | LGPL-3.0-only elected for Core, Gui, OpenGL, Qml, Quick, QuickControls2. Other Qt pieces differ. See above. | UI and viewport. Not committed. |
| nlohmann/json 3.11.3 | MIT | `third_party/nlohmann/json.hpp` |
| miniz 3.0.2 | MIT and Unlicense, see `third_party/miniz/LICENSE` | Zip container |
| Catch2 3.6.0 | Boost Software License 1.0 | Tests, fetched at configure time, not shipped in the app |
| OpenSSL libcrypto | Apache-2.0 | SHA-256 |
| SQLite | Public domain | Preferences |

`third_party/miniz/miniz_export.h` is a three-line static-build stub added in this repository so the split miniz sources compile. It is not an upstream file.

## What this does not decide

- A store build (Microsoft, Apple, Google) may add terms that conflict with the GPL. That has not been checked, and no store package exists.
- Static linking of OCCT or Qt would change the LGPL obligations. This Linux build uses the shared libraries.
- Species names and the stylized colors are project data, not a third-party database.
