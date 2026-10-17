# Licenses

The application source is released under **GPL-3.0-or-later**. The full text is `LICENSE`. This is a project decision recorded so the dependency obligations stay visible. It is not a lawyer's opinion.

## Why GPL-3.0-or-later

The desktop build links Qt 6 modules that are available under the LGPL, and Open CASCADE under the LGPL-2.1 with the OCCT exception. A GPL-3.0-or-later program can use those libraries and can also use a future Qt module that is GPL-only. The reverse is not true: an MIT or Apache application cannot take a GPL-only Qt module.

Qt Quick 3D and Qt Quick 3D Physics are, on the Qt 6.8 licensing table, GPL-only. This program does not link them. The modules it does link are Core, Gui, Qml, Quick, OpenGL, QuickControls2, and the linguist tools at build time. Those are under Qt's LGPL-3.0 option (and the commercial license, which this project does not use). LGPL still has source and relink obligations if a binary is shipped. Shipping is not happening in this baseline. When it does, the corresponding Qt and OCCT sources, or an offer for them, have to travel with the binary.

## Other components

| Piece | Terms | Role |
|---|---|---|
| Open CASCADE 7.9.2 | LGPL-2.1 with the OCCT exception (`deps/OCCT-7_9_2/LICENSE_LGPL_21.txt`, `OCCT_LGPL_EXCEPTION.txt`) | Solid kernel. Not committed; built locally. |
| Qt 6.8.3 | LGPL-3.0 for the modules listed above. Quick 3D is GPL-only and unused. | UI and OpenGL viewport. Not committed. |
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
