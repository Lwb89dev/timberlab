import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TimberLab

Rectangle {
    color: Theme.panel
    signal demoRequested()
    function typeName(key) {
        if (key === "board") return qsTr("Board")
        if (key === "box") return qsTr("Box")
        if (key === "cylinder") return qsTr("Cylinder")
        return key
    }
    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        Label {
            text: qsTr("Parts")
            color: Theme.text
            font.pixelSize: 16
            font.bold: true
        }
        Label {
            text: qsTr("Free parts. No template is required.")
            color: Theme.muted
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: app.components
            spacing: 4
            delegate: Rectangle {
                required property string partId
                required property string name
                required property string partType
                required property bool shown
                required property bool locked
                width: ListView.view.width
                height: 48
                radius: Theme.radius
                color: partId === app.selectedId ? "#3a3128" : Theme.panelRaised
                border.color: partId === app.selectedId ? Theme.accent : Theme.border
                border.width: 1
                MouseArea {
                    anchors.fill: parent
                    onClicked: app.select(partId)
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 6
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 0
                        Label {
                            text: name
                            textFormat: Text.PlainText
                            color: Theme.text
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: typeName(partType)
                            textFormat: Text.PlainText
                            color: Theme.muted
                            font.pixelSize: 11
                        }
                    }
                    ToolButton {
                        text: shown ? qsTr("Hide") : qsTr("Show")
                        onClicked: app.setVisible(partId, !shown)
                    }
                }
            }
        }
        Button {
            text: qsTr("Demo table")
            Layout.fillWidth: true
            onClicked: demoRequested()
        }
    }
}
