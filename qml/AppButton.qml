import QtQuick
import QtQuick.Controls
import TimberLab

Button {
    id: control

    property string glyph: ""
    property string toolTip: ""
    property bool primary: false
    property bool subtle: false
    property bool destructive: false
    property bool compact: false

    implicitHeight: compact ? 32 : Theme.controlHeight
    implicitWidth: Math.max(compact ? 36 : 58, contentRow.implicitWidth + leftPadding + rightPadding)
    leftPadding: compact ? 9 : 13
    rightPadding: compact ? 9 : 13
    topPadding: 0
    bottomPadding: 0
    hoverEnabled: true
    scale: down ? 0.96 : 1

    contentItem: Row {
        id: contentRow
        spacing: control.glyph.length > 0 && control.text.length > 0 ? 7 : 0
        anchors.centerIn: parent

        Text {
            visible: control.glyph.length > 0
            text: control.glyph
            color: control.enabled
                   ? (control.primary ? Theme.onAccent : (control.destructive ? Theme.danger : Theme.text))
                   : Theme.faint
            font.pixelSize: control.compact ? 14 : 15
            font.weight: Font.DemiBold
            anchors.verticalCenter: parent.verticalCenter
        }
        Text {
            visible: control.text.length > 0
            text: control.text
            color: control.enabled
                   ? (control.primary ? Theme.onAccent : (control.destructive ? Theme.danger : Theme.text))
                   : Theme.faint
            font.pixelSize: control.compact ? 12 : 13
            font.weight: control.primary ? Font.DemiBold : Font.Medium
            elide: Text.ElideRight
            anchors.verticalCenter: parent.verticalCenter
        }
    }

    background: Rectangle {
        radius: control.compact ? 8 : Theme.radius
        color: {
            if (!control.enabled) return Theme.field
            if (control.primary) return control.down ? Theme.accentSoft : Theme.accent
            if (control.destructive) return control.hovered ? Theme.dangerSurface : "transparent"
            if (control.down) return Theme.accentSurface
            if (control.hovered) return Theme.panelHover
            return control.subtle ? "transparent" : Theme.panelRaised
        }
        border.width: control.primary || control.subtle ? 0 : 1
        border.color: control.hovered ? Theme.borderStrong : Theme.border

        Behavior on color { ColorAnimation { duration: Theme.motionFast } }
        Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }
    }

    Behavior on scale {
        NumberAnimation { duration: Theme.motionFast; easing.type: Easing.OutCubic }
    }

    ToolTip.visible: toolTip.length > 0 && hovered
    ToolTip.text: toolTip
    ToolTip.delay: 450
}
