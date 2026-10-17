import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TimberLab

Rectangle {
    id: inspector
    color: Theme.panel
    property string notesPartId: ""

    function syncFlags() {
        visibleBox.checked = app.selection.visible === true
        lockedBox.checked = app.selection.locked === true
    }
    function syncNotes() {
        const id = app.selectedId
        if (notesBox.activeFocus && id === notesPartId) return
        notesBox.text = app.selection.notes || ""
        notesPartId = id
    }

    Component.onCompleted: {
        syncFlags()
        syncNotes()
    }
    Connections {
        target: app
        function onDocumentChanged() {
            inspector.syncFlags()
            inspector.syncNotes()
        }
    }

    component ValueField: RowLayout {
        id: field
        property string label: ""
        property string value: ""
        property string placeholder: ""
        property int labelWidth: 108
        signal committed(string text)
        Layout.fillWidth: true
        Label {
            text: label
            color: Theme.muted
            Layout.preferredWidth: labelWidth
            elide: Text.ElideRight
        }
        TextField {
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            property string committedValue: value
            text: committedValue
            placeholderText: placeholder
            selectByMouse: true
            onCommittedValueChanged: if (!activeFocus) text = committedValue
            onEditingFinished: field.committed(text)
        }
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight + 24
        clip: true
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: column
            width: scroller.width - 24
            x: 12
            y: 12
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

        ColumnLayout {
            visible: app.parameters.length > 0
            Layout.fillWidth: true
            spacing: 8
            Label { text: qsTr("Parameters"); color: Theme.muted }
            Repeater {
                model: app.parameters
                delegate: ValueField {
                    required property var modelData
                    label: modelData.label
                    value: modelData.value
                    labelWidth: 132
                    onCommitted: function (text) { app.setDriverFromText(modelData.id, text) }
                }
            }
            Label {
                text: qsTr("Changing a parameter updates the parts that depend on it.")
                wrapMode: Text.WordWrap
                color: Theme.muted
                Layout.fillWidth: true
            }
        }

        Label {
            visible: app.selection.id === undefined
            text: qsTr("Select a part to edit its width, height and thickness. Display units do not change the stored size.")
            wrapMode: Text.WordWrap
            color: Theme.muted
            Layout.fillWidth: true
        }

        Label { text: qsTr("Library"); color: Theme.muted }
        Label {
            text: qsTr("Custom materials and stock thicknesses are saved on this computer. They are not the project.")
            wrapMode: Text.WordWrap
            color: Theme.muted
            Layout.fillWidth: true
            textFormat: Text.PlainText
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Material")
                color: Theme.muted
                Layout.preferredWidth: 108
                elide: Text.ElideRight
            }
            TextField {
                id: libraryName
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                placeholderText: qsTr("Shop oak")
                selectByMouse: true
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Color")
                color: Theme.muted
                Layout.preferredWidth: 108
                elide: Text.ElideRight
            }
            TextField {
                id: libraryColor
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                placeholderText: "#A67C52"
                selectByMouse: true
            }
        }
        Button {
            text: qsTr("Add material")
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            onClicked: if (app.addUserMaterial(libraryName.text, libraryColor.text)) libraryName.text = ""
        }
        Repeater {
            model: app.userMaterials
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                Label {
                    text: modelData.name
                    color: Theme.text
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
                Button {
                    text: qsTr("Remove")
                    implicitHeight: Theme.controlHeight
                    onClicked: app.removeUserMaterial(modelData.id)
                }
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Label {
                text: qsTr("Thickness")
                color: Theme.muted
                Layout.preferredWidth: 108
                elide: Text.ElideRight
            }
            TextField {
                id: libraryThickness
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                placeholderText: qsTr("18 mm")
                selectByMouse: true
            }
        }
        Button {
            text: qsTr("Add preset")
            Layout.fillWidth: true
            implicitHeight: Theme.controlHeight
            onClicked: if (app.addUserPreset(libraryThickness.text)) libraryThickness.text = ""
        }
        Repeater {
            model: app.userPresets
            delegate: RowLayout {
                required property var modelData
                Layout.fillWidth: true
                Label {
                    text: modelData.label
                    color: Theme.text
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                    textFormat: Text.PlainText
                }
                Button {
                    text: qsTr("Remove")
                    implicitHeight: Theme.controlHeight
                    onClicked: app.removeUserPreset(modelData.id)
                }
            }
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
                id: materialBox
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
                    function onDocumentChanged() { materialBox.syncMaterial() }
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
            Repeater {
                model: app.selection.drives || []
                delegate: ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 4
                    Label {
                        text: modelData.label + " = " + modelData.formula
                        color: Theme.text
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Button {
                        text: qsTr("Release")
                        Layout.fillWidth: true
                        implicitHeight: Theme.controlHeight
                        onClicked: app.releasePartField(app.selectedId, modelData.field)
                    }
                }
            }
            ColumnLayout {
                visible: app.parameters.length > 0
                Layout.fillWidth: true
                spacing: 8
                Label { text: qsTr("Drive"); color: Theme.muted }
                ComboBox {
                    id: driveField
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    textRole: "label"
                    valueRole: "id"
                    model: app.driveFields
                    Component.onCompleted: syncIndex()
                    function syncIndex() {
                        if (count > 0 && currentIndex < 0) currentIndex = 0
                    }
                }
                ComboBox {
                    id: driveParameter
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    textRole: "label"
                    valueRole: "id"
                    model: app.parameters
                    Component.onCompleted: syncIndex()
                    function syncIndex() {
                        if (count > 0 && currentIndex < 0) currentIndex = 0
                    }
                }
                Connections {
                    target: app
                    function onDocumentChanged() { driveParameter.syncIndex() }
                }
                Button {
                    text: qsTr("Drive")
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    enabled: driveParameter.count > 0
                    onClicked: app.drivePartField(app.selectedId, driveField.currentValue, driveParameter.currentValue)
                }
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
                id: visibleBox
                text: qsTr("Visible")
                onClicked: app.setVisible(app.selectedId, checked)
            }
            CheckBox {
                id: lockedBox
                text: qsTr("Locked")
                onClicked: app.setLocked(app.selectedId, checked)
            }
            JointSection { Layout.fillWidth: true }
            TextArea {
                id: notesBox
                Layout.fillWidth: true
                implicitHeight: 72
                placeholderText: qsTr("Notes")
                wrapMode: TextArea.Wrap
                onEditingFinished: app.setNotes(app.selectedId, text)
            }
        }
        Label {
            text: qsTr("A board is a real solid: X width, Y thickness, Z height. Z is up.")
            color: Theme.muted
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        }
    }
}
