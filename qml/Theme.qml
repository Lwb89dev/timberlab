pragma Singleton
import QtQuick

QtObject {
    property bool darkMode: true

    readonly property color bg: darkMode ? "#0f1210" : "#eeeae1"
    readonly property color panel: darkMode ? "#171a18" : "#faf8f3"
    readonly property color panelRaised: darkMode ? "#202420" : "#ffffff"
    readonly property color panelHover: darkMode ? "#2a2f2a" : "#f3ede3"
    readonly property color field: darkMode ? "#111411" : "#f5f1e9"
    readonly property color border: darkMode ? "#343a34" : "#ddd5c8"
    readonly property color borderStrong: darkMode ? "#4a5149" : "#c9bba8"
    readonly property color text: darkMode ? "#f4f1e9" : "#24231f"
    readonly property color muted: darkMode ? "#9ca39a" : "#716c63"
    readonly property color faint: darkMode ? "#676e66" : "#a39b90"
    readonly property color accent: darkMode ? "#dca15a" : "#ad672b"
    readonly property color accentSoft: darkMode ? "#f2c477" : "#c9823e"
    readonly property color accentSurface: darkMode ? "#33291f" : "#f4e3cc"
    readonly property color onAccent: darkMode ? "#21160b" : "#fffaf2"
    readonly property color danger: darkMode ? "#ec7d6c" : "#b64135"
    readonly property color dangerSurface: darkMode ? "#35201e" : "#f8dfdb"
    readonly property color ok: darkMode ? "#98b875" : "#557d3a"
    readonly property color canvas: darkMode ? "#171b18" : "#e8e5dd"

    readonly property int controlHeight: 38
    readonly property int radius: 10
    readonly property int radiusLarge: 14
    readonly property int toolbarHeight: 68
    readonly property int statusHeight: 36
    readonly property int motionFast: 110
    readonly property int motion: 180
}
