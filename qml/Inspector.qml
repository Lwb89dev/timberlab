import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TimberLab

Rectangle {
    color: Theme.panel

    component ValueField: RowLayout {
        property string label: ""
        property string value: ""
        property string placeholder: ""
        signal committed(string text)
        Layout.fillWidth: true
        Label {
            text: label
            color: Theme.muted
            Layout.preferredWidth: 108
        }
        TextField {
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            property string committed: value
            text: committed
            placeholderText: placeholder
            selectByMouse: true
            onCommittedChanged: if (!activeFocus) text = committed
            onEditingFinished: parent.committed(text)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        Label {
            text: qsTr("Inspector")
            color: Theme.text
            font.pixelSize: 16
            font.bold: true
        }

        Label { text: qsTr("Project"); color: Theme.muted }
        ValueField {
            label: qsTr("Name")
            value: app.projectName
            onCommitted: function (text) { app.projectName = text }
        }

        Label {
            visible: app.selection.id === undefined
            text: qsTr("Select a part to edit its width, height and thickness. Display units do not change the stored size.")
            wrapMode: Text.WordWrap
            color: Theme.muted
            Layout.fillWidth: true
        }

        ColumnLayout {
            visible: app.selection.id !== undefined
            Layout.fillWidth: true
            spacing: 8
            ValueField {
                label: qsTr("Part")
                value: app.selection.name || ""
                onCommitted: function (text) { app.setPartName(app.selectedId, text) }
            }
            ComboBox {
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                textRole: "name"
                valueRole: "id"
                model: app.materialChoices
                onActivated: app.setMaterial(app.selectedId, currentValue)
                Component.onCompleted: syncMaterial()
                function syncMaterial() {
                    const index = indexOfValue(app.selection.materialId)
                    if (index >= 0) currentIndex = index
                }
                Connections {
                    target: app
                    function onDocumentChanged() { parent.syncMaterial() }
                }
            }
            ComboBox {
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                textRole: "label"
                valueRole: "id"
                model: app.thicknessPresets
                displayText: qsTr("Apply stock thickness")
                onActivated: app.applyThicknessPreset(currentValue)
            }
            ValueField {
                visible: app.selection.type !== "cylinder"
                label: qsTr("Width (X)")
                value: app.selection.width || ""
                onCommitted: function (text) { app.setParameterFromText(app.selectedId, "width", text) }
            }
            ValueField {
                visible: app.selection.type !== "cylinder"
                label: qsTr("Thickness (Y)")
                value: app.selection.thickness || ""
                onCommitted: function (text) { app.setParameterFromText(app.selectedId, "thickness", text) }
            }
            ValueField {
                visible: app.selection.type === "cylinder"
                label: qsTr("Radius")
                value: app.selection.radius || ""
                onCommitted: function (text) { app.setParameterFromText(app.selectedId, "radius", text) }
            }
            ValueField {
                label: qsTr("Height (Z)")
                value: app.selection.height || ""
                onCommitted: function (text) { app.setParameterFromText(app.selectedId, "height", text) }
            }
            Label { text: qsTr("Position"); color: Theme.muted }
            ValueField {
                label: "X"
                value: app.selection.x || ""
                onCommitted: function (text) { app.setTranslationFromText(app.selectedId, "x", text) }
            }
            ValueField {
                label: "Y"
                value: app.selection.y || ""
                onCommitted: function (text) { app.setTranslationFromText(app.selectedId, "y", text) }
            }
            ValueField {
                label: "Z"
                value: app.selection.z || ""
                onCommitted: function (text) { app.setTranslationFromText(app.selectedId, "z", text) }
            }
            Label { text: qsTr("Rotation"); color: Theme.muted }
            ValueField {
                label: "Rx"
                value: app.selection.rx || ""
                onCommitted: function (text) { app.setRotationFromText(app.selectedId, "x", text) }
            }
            ValueField {
                label: "Ry"
                value: app.selection.ry || ""
                onCommitted: function (text) { app.setRotationFromText(app.selectedId, "y", text) }
            }
            ValueField {
                label: "Rz"
                value: app.selection.rz || ""
                onCommitted: function (text) { app.setRotationFromText(app.selectedId, "z", text) }
            }
            CheckBox {
                text: qsTr("Visible")
                checked: app.selection.visible === true
                onClicked: app.setVisible(app.selectedId, checked)
            }
            CheckBox {
                text: qsTr("Locked")
                checked: app.selection.locked === true
                onClicked: app.setLocked(app.selectedId, checked)
            }
            TextArea {
                Layout.fillWidth: true
                implicitHeight: 72
                placeholderText: qsTr("Notes")
                text: app.selection.notes || ""
                wrapMode: TextArea.Wrap
                onEditingFinished: app.setNotes(app.selectedId, text)
            }
        }
        Item { Layout.fillHeight: true }
        Label {
            text: qsTr("A board is a real solid: X width, Y thickness, Z height. Z is up.")
            color: Theme.muted
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
}
