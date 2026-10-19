import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TimberLab

Rectangle {
    id: panel
    color: Theme.panel
    signal demoRequested()
    signal renameRequested(string partId, string partName)
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
                    id: rowMouse
                    anchors.fill: parent
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function (mouse) {
                        if (mouse.button === Qt.RightButton) {
                            partMenu.partId = partId
                            partMenu.partName = name
                            partMenu.popup(rowMouse, mouse.x, mouse.y)
                        } else {
                            app.select(partId)
                        }
                    }
                }
                Menu {
                    id: partMenu
                    property string partId: ""
                    property string partName: ""
                    MenuItem {
                        text: qsTr("Rename")
                        onTriggered: panel.renameRequested(partMenu.partId, partMenu.partName)
                    }
                    MenuItem {
                        text: qsTr("Duplicate")
                        onTriggered: {
                            app.select(partMenu.partId)
                            app.duplicateSelected()
                        }
                    }
                    MenuItem {
                        text: qsTr("Delete")
                        onTriggered: {
                            app.select(partMenu.partId)
                            app.deleteSelected()
                        }
                    }
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
