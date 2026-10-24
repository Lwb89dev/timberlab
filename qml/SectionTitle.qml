import QtQuick
import QtQuick.Layouts
import TimberLab

Item {
    id: root
    property string text: ""
    property string detail: ""
    implicitHeight: detail.length > 0 ? 38 : 26
    Layout.fillWidth: true

    Column {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 2

        RowLayout {
            width: parent.width
            spacing: 8
            Rectangle {
                width: 18
                height: 2
                radius: 1
                color: Theme.accent
                Layout.alignment: Qt.AlignVCenter
            }
            Text {
                text: root.text.toUpperCase()
                color: Theme.text
                font.pixelSize: 11
                font.weight: Font.DemiBold
                font.letterSpacing: 1.25
                Layout.alignment: Qt.AlignVCenter
            }
            Rectangle {
                height: 1
                color: Theme.border
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
            }
        }
        Text {
            visible: root.detail.length > 0
            text: root.detail
            color: Theme.muted
            font.pixelSize: 11
            elide: Text.ElideRight
            width: parent.width
        }
    }
}
