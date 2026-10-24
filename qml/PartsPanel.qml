import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TimberLab

Rectangle {
    id: panel
    color: Theme.panel
    radius: Theme.radiusLarge
    border.width: 1
    border.color: Theme.border
    clip: true

    signal demoRequested()
    signal renameRequested(string partId, string partName)

    function typeName(key) {
        if (key === "board") return qsTr("Board")
        if (key === "box") return qsTr("Box")
        if (key === "cylinder") return qsTr("Cylinder")
        return key
    }
    function typeGlyph(key) {
        if (key === "board") return "▰"
        if (key === "box") return "◆"
        if (key === "cylinder") return "●"
        return "■"
    }

    Behavior on color { ColorAnimation { duration: Theme.motion } }
    Behavior on border.color { ColorAnimation { duration: Theme.motion } }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 14
        spacing: 10

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Label {
                    text: qsTr("Parts")
                    color: Theme.text
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                Label {
                    text: qsTr("PROJECT OUTLINE")
                    color: Theme.muted
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.4
                }
            }
            Rectangle {
                implicitWidth: countText.implicitWidth + 14
                implicitHeight: 26
                radius: 13
                color: Theme.accentSurface
                border.color: Theme.accent
                border.width: 1
                Label {
                    id: countText
                    anchors.centerIn: parent
                    text: String(partsList.count)
                    color: Theme.accentSoft
                    font.pixelSize: 11
                    font.bold: true
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 38
            radius: Theme.radius
            color: Theme.field
            border.color: Theme.border
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8
                Label { text: "⌕"; color: Theme.faint; font.pixelSize: 16 }
                Label {
                    text: qsTr("Select a part to inspect it")
                    color: Theme.muted
                    font.pixelSize: 11
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
                Label { text: "⌘"; color: Theme.faint; font.pixelSize: 10 }
            }
        }

        ListView {
            id: partsList
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: app.components
            spacing: 6
            focus: true
            activeFocusOnTab: true
            Keys.onDeletePressed: app.deleteSelected()

            add: Transition {
                NumberAnimation { properties: "opacity,scale"; from: 0; to: 1; duration: Theme.motion; easing.type: Easing.OutCubic }
            }

            delegate: Rectangle {
                id: partRow
                required property string partId
                required property string name
                required property string partType
                required property bool shown
                required property bool locked
                readonly property bool selected: app.selectedIds.indexOf(partId) >= 0

                width: ListView.view.width
                height: 60
                radius: Theme.radius
                color: selected ? Theme.accentSurface : (rowMouse.containsMouse ? Theme.panelHover : Theme.panelRaised)
                border.color: selected ? Theme.accent : (rowMouse.containsMouse ? Theme.borderStrong : Theme.border)
                border.width: 1

                Behavior on color { ColorAnimation { duration: Theme.motionFast } }
                Behavior on border.color { ColorAnimation { duration: Theme.motionFast } }

                Rectangle {
                    width: 3
                    height: partRow.selected ? 30 : 0
                    radius: 2
                    color: Theme.accent
                    anchors.left: parent.left
                    anchors.verticalCenter: parent.verticalCenter
                    Behavior on height { NumberAnimation { duration: Theme.motion; easing.type: Easing.OutCubic } }
                }

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    onClicked: function (mouse) {
                        if (mouse.button === Qt.RightButton) {
                            partMenu.partId = partId
                            partMenu.partName = name
                            partMenu.popup(rowMouse, mouse.x, mouse.y)
                        } else {
                            partsList.forceActiveFocus()
                            app.select(partId, mouse.modifiers)
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
                    anchors.leftMargin: 11
                    anchors.rightMargin: 6
                    spacing: 9
                    Rectangle {
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 34
                        radius: 9
                        color: partRow.selected ? Theme.accent : Theme.field
                        Label {
                            anchors.centerIn: parent
                            text: panel.typeGlyph(partType)
                            color: partRow.selected ? Theme.onAccent : Theme.accent
                            font.pixelSize: 13
                        }
                        Behavior on color { ColorAnimation { duration: Theme.motionFast } }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Label {
                            text: name
                            textFormat: Text.PlainText
                            color: Theme.text
                            font.pixelSize: 13
                            font.weight: Font.Medium
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                        Label {
                            text: panel.typeName(partType).toUpperCase() + (locked ? "  ·  " + qsTr("LOCKED") : "")
                            textFormat: Text.PlainText
                            color: locked ? Theme.accent : Theme.muted
                            font.pixelSize: 9
                            font.letterSpacing: 0.8
                        }
                    }
                    AppButton {
                        glyph: shown ? "◉" : "○"
                        compact: true
                        subtle: true
                        toolTip: shown ? qsTr("Hide") : qsTr("Show")
                        onClicked: app.setVisible(partId, !shown)
                    }
                }
            }

            Label {
                visible: partsList.count === 0
                anchors.centerIn: parent
                width: parent.width - 30
                text: qsTr("Your workshop is empty. Add a board, box or cylinder to begin.")
                color: Theme.muted
                wrapMode: Text.WordWrap
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: 12
            }
        }

        AppButton {
            text: qsTr("Load demo table")
            glyph: "✦"
            primary: true
            Layout.fillWidth: true
            onClicked: panel.demoRequested()
        }
    }
}
