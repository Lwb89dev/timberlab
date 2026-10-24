import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TimberLab

Rectangle {
    id: inspector
    color: Theme.panel
    radius: Theme.radiusLarge
    border.width: 1
    border.color: Theme.border
    clip: true
    property string notesPartId: ""

    Behavior on color { ColorAnimation { duration: Theme.motion } }
    Behavior on border.color { ColorAnimation { duration: Theme.motion } }

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
            textFormat: Text.PlainText
            color: Theme.muted
            font.pixelSize: 11
            font.weight: Font.Medium
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
            width: scroller.width - 28
            x: 14
            y: 14
            spacing: 9
        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1
                Label {
                    text: qsTr("Inspector")
                    color: Theme.text
                    font.pixelSize: 18
                    font.weight: Font.DemiBold
                }
                Label {
                    text: qsTr("PARAMETRIC PROPERTIES")
                    color: Theme.muted
                    font.pixelSize: 9
                    font.weight: Font.DemiBold
                    font.letterSpacing: 1.25
                }
            }
            Rectangle {
                visible: app.selection.multi === true || app.selection.id !== undefined
                implicitWidth: selectionBadge.implicitWidth + 14
                implicitHeight: 26
                radius: 13
                color: Theme.accentSurface
                border.color: Theme.accent
                Label {
                    id: selectionBadge
                    anchors.centerIn: parent
                    text: app.selection.multi === true ? String(app.selection.count) : "1"
                    color: Theme.accentSoft
                    font.bold: true
                    font.pixelSize: 11
                }
            }
        }

        SectionTitle { text: qsTr("Project") }
        ValueField {
            label: qsTr("Name")
            value: app.projectName
            onCommitted: function (text) { app.projectName = text }
        }

        ColumnLayout {
            visible: app.parameters.length > 0
            Layout.fillWidth: true
            spacing: 8
            SectionTitle { text: qsTr("Parameters") }
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
            visible: app.selection.id === undefined && app.selection.multi !== true
            text: qsTr("Select a part to edit its width, height and thickness. Display units do not change the stored size.")
            wrapMode: Text.WordWrap
            color: Theme.muted
            Layout.fillWidth: true
        }
        Label {
            visible: app.selection.multi === true
            text: qsTr("%1 parts selected").arg(app.selection.count)
            wrapMode: Text.WordWrap
            color: Theme.text
            Layout.fillWidth: true
        }

        SectionTitle { text: qsTr("Library") }
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
        AppButton {
            text: qsTr("Add material")
            glyph: "+"
            primary: true
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
                AppButton {
                    text: qsTr("Remove")
                    destructive: true
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
        AppButton {
            text: qsTr("Add preset")
            glyph: "+"
            primary: true
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
                AppButton {
                    text: qsTr("Remove")
                    destructive: true
                    implicitHeight: Theme.controlHeight
                    onClicked: app.removeUserPreset(modelData.id)
                }
            }
        }

        ColumnLayout {
            visible: app.selection.id !== undefined && app.selection.multi !== true
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
                implicitHeight: 48
                textRole: "name"
                valueRole: "id"
                model: app.materialChoices
                property var currentMaterial: currentIndex >= 0 && currentIndex < model.length ? model[currentIndex] : null
                delegate: ItemDelegate {
                    required property var modelData
                    required property int index
                    width: materialBox.width
                    height: 58
                    highlighted: materialBox.highlightedIndex === index
                    contentItem: RowLayout {
                        spacing: 9
                        Rectangle {
                            Layout.preferredWidth: 46
                            Layout.preferredHeight: 38
                            radius: 6
                            clip: true
                            color: Theme.field
                            border.width: 1
                            border.color: Theme.border
                            Image {
                                anchors.fill: parent
                                source: modelData.faceTexture || ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                mipmap: true
                            }
                        }
                        Rectangle {
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 38
                            radius: 6
                            clip: true
                            color: Theme.field
                            border.width: 1
                            border.color: Theme.border
                            Image {
                                anchors.fill: parent
                                source: modelData.endTexture || ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                mipmap: true
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 1
                            Label {
                                Layout.fillWidth: true
                                text: modelData.name
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                color: Theme.text
                                font.weight: Font.Medium
                            }
                            Label {
                                Layout.fillWidth: true
                                text: modelData.category
                                textFormat: Text.PlainText
                                elide: Text.ElideRight
                                color: Theme.muted
                                font.pixelSize: 10
                            }
                        }
                    }
                }
                contentItem: RowLayout {
                    spacing: 9
                    Rectangle {
                        Layout.preferredWidth: 34
                        Layout.preferredHeight: 30
                        radius: 5
                        clip: true
                        color: Theme.field
                        Image {
                            anchors.fill: parent
                            source: materialBox.currentMaterial ? materialBox.currentMaterial.faceTexture : ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            mipmap: true
                        }
                    }
                    Label {
                        Layout.fillWidth: true
                        text: materialBox.displayText
                        textFormat: Text.PlainText
                        elide: Text.ElideRight
                        color: Theme.text
                        verticalAlignment: Text.AlignVCenter
                    }
                }
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
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Repeater {
                    model: [
                        { label: qsTr("Long grain"), source: materialBox.currentMaterial ? materialBox.currentMaterial.faceTexture : "" },
                        { label: qsTr("End grain"), source: materialBox.currentMaterial ? materialBox.currentMaterial.endTexture : "" }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        Layout.preferredHeight: 82
                        radius: 9
                        color: Theme.field
                        border.width: 1
                        border.color: Theme.border
                        clip: true
                        Image {
                            anchors.fill: parent
                            source: modelData.source
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            mipmap: true
                        }
                        Rectangle {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 24
                            color: Theme.darkMode ? "#b3121513" : "#c9fffdf8"
                            Label {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                text: modelData.label
                                color: Theme.text
                                font.pixelSize: 10
                                font.weight: Font.DemiBold
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                    }
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
                        textFormat: Text.PlainText
                        color: Theme.text
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    AppButton {
                        text: qsTr("Release")
                        subtle: true
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
                SectionTitle { text: qsTr("Drive") }
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
                AppButton {
                    text: qsTr("Drive")
                    glyph: "↗"
                    primary: true
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    enabled: driveParameter.count > 0
                    onClicked: app.drivePartField(app.selectedId, driveField.currentValue, driveParameter.currentValue)
                }
            }
            SectionTitle { text: qsTr("Position") }
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
            SectionTitle { text: qsTr("Rotation") }
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
            SectionTitle { text: qsTr("Parent") }
            ComboBox {
                id: parentBox
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                textRole: "name"
                valueRole: "id"
                model: app.parentChoices
                onActivated: app.setParent(app.selectedId, currentValue)
                function syncParent() {
                    const index = indexOfValue(app.selection.parentId || "")
                    if (index >= 0) currentIndex = index
                }
                Component.onCompleted: syncParent()
                Connections {
                    target: app
                    function onDocumentChanged() { parentBox.syncParent() }
                }
            }
            ValueField {
                label: qsTr("Role")
                value: app.selection.role || ""
                onCommitted: function (text) { app.setRole(app.selectedId, text) }
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
                implicitHeight: 86
                placeholderText: qsTr("Notes")
                wrapMode: TextArea.Wrap
                onEditingFinished: app.setNotes(app.selectedId, text)
            }
        Item { Layout.preferredHeight: 6 }
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
