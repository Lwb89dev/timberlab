import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TimberLab

ColumnLayout {
    id: section
    spacing: 8
    visible: app.selection.id !== undefined && (app.partChoices.length > 0 || app.joints.length > 0)

    readonly property var typeKeys: [
        "joint.mortise_tenon",
        "joint.half_lap",
        "joint.dado",
        "joint.groove",
        "joint.rabbet",
        "joint.bridle",
        "joint.castle",
        "joint.miter",
        "joint.dovetail",
        "joint.sliding_dovetail",
        "joint.finger",
        "joint.half_blind_dovetail",
        "joint.tongue_groove",
        "joint.dowel"
    ]

    function typeKey() {
        return typeKeys[typeBox.currentIndex]
    }
    function typeName(type) {
        if (type === "joint.mortise_tenon") return qsTr("Mortise and tenon")
        if (type === "joint.half_lap") return qsTr("Half lap")
        if (type === "joint.dado") return qsTr("Dado")
        if (type === "joint.groove") return qsTr("Groove")
        if (type === "joint.rabbet") return qsTr("Rabbet")
        if (type === "joint.bridle") return qsTr("Bridle")
        if (type === "joint.castle") return qsTr("Castle")
        if (type === "joint.miter") return qsTr("Miter")
        if (type === "joint.dovetail") return qsTr("Dovetail")
        if (type === "joint.sliding_dovetail") return qsTr("Sliding dovetail")
        if (type === "joint.finger") return qsTr("Finger joint")
        if (type === "joint.half_blind_dovetail") return qsTr("Half-blind dovetail")
        if (type === "joint.tongue_groove") return qsTr("Tongue and groove")
        if (type === "joint.dowel") return qsTr("Dowel")
        return type
    }
    function shows(type, key) {
        if (type === "joint.mortise_tenon")
            return key === "length" || key === "width" || key === "thickness" || key === "depth" || key === "offset" || key === "offset2"
        if (type === "joint.half_lap") return key === "length"
        if (type === "joint.dado") return key === "width" || key === "depth" || key === "offset"
        if (type === "joint.groove")
            return key === "length" || key === "width" || key === "depth" || key === "offset" || key === "offset2"
        if (type === "joint.rabbet") return key === "width" || key === "depth"
        if (type === "joint.bridle") return key === "length" || key === "thickness"
        if (type === "joint.castle") return key === "length" || key === "thickness" || key === "count"
        if (type === "joint.miter") return key === "length"
        if (type === "joint.dovetail")
            return key === "length" || key === "width" || key === "thickness" || key === "depth" || key === "offset" || key === "count"
        if (type === "joint.sliding_dovetail")
            return key === "width" || key === "depth" || key === "offset" || key === "offset2"
        if (type === "joint.finger") return key === "length" || key === "count"
        if (type === "joint.half_blind_dovetail")
            return key === "length" || key === "width" || key === "thickness" || key === "depth" || key === "offset" || key === "count"
        if (type === "joint.tongue_groove") return key === "thickness" || key === "depth"
        if (type === "joint.dowel") return key === "thickness" || key === "depth"
        return false
    }
    function syncMate() {
        if (mateBox.count > 0 && mateBox.currentIndex < 0) mateBox.currentIndex = 0
    }

    component ValueField: RowLayout {
        id: field
        property string label: ""
        property string value: ""
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
            property string committedValue: value
            text: committedValue
            selectByMouse: true
            onCommittedValueChanged: if (!activeFocus) text = committedValue
            onEditingFinished: field.committed(text)
        }
    }

    Label { text: qsTr("Joinery"); color: Theme.muted }
    ComboBox {
        id: typeBox
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        model: [
            qsTr("Mortise and tenon"),
            qsTr("Half lap"),
            qsTr("Dado"),
            qsTr("Groove"),
            qsTr("Rabbet"),
            qsTr("Bridle"),
            qsTr("Castle"),
            qsTr("Miter"),
            qsTr("Dovetail"),
            qsTr("Sliding dovetail"),
            qsTr("Finger joint"),
            qsTr("Half-blind dovetail"),
            qsTr("Tongue and groove"),
            qsTr("Dowel")
        ]
    }
    Label { text: qsTr("Mate"); color: Theme.muted }
    ComboBox {
        id: mateBox
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        textRole: "name"
        valueRole: "id"
        model: app.partChoices
    }
    Label {
        text: qsTr("Starting sizes are not a structural rating.")
        color: Theme.muted
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    Button {
        text: qsTr("Add joint")
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        enabled: mateBox.count > 0 && app.selectedId.length > 0
        onClicked: app.addJoint(section.typeKey(), app.selectedId, mateBox.currentValue)
    }

    Repeater {
        model: app.joints
        delegate: ColumnLayout {
            required property var modelData
            Layout.fillWidth: true
            spacing: 8
            Label {
                text: section.typeName(modelData.type) + " — " + (modelData.mateName || "")
                color: Theme.text
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            ValueField {
                visible: section.shows(modelData.type, "length")
                label: qsTr("Length")
                value: modelData.length || ""
                onCommitted: function (text) { app.setJointFromText(modelData.id, "length", text) }
            }
            ValueField {
                visible: section.shows(modelData.type, "width")
                label: qsTr("Width")
                value: modelData.width || ""
                onCommitted: function (text) { app.setJointFromText(modelData.id, "width", text) }
            }
            ValueField {
                visible: section.shows(modelData.type, "thickness")
                label: modelData.type === "joint.dowel" ? qsTr("Diameter") : qsTr("Thickness")
                value: modelData.thickness || ""
                onCommitted: function (text) { app.setJointFromText(modelData.id, "thickness", text) }
            }
            ValueField {
                visible: section.shows(modelData.type, "depth")
                label: qsTr("Depth")
                value: modelData.depth || ""
                onCommitted: function (text) { app.setJointFromText(modelData.id, "depth", text) }
            }
            ValueField {
                visible: section.shows(modelData.type, "offset")
                label: modelData.type === "joint.dovetail" || modelData.type === "joint.half_blind_dovetail" ? qsTr("Slope") : qsTr("Offset")
                value: modelData.offset || ""
                onCommitted: function (text) { app.setJointFromText(modelData.id, "offset", text) }
            }
            ValueField {
                visible: section.shows(modelData.type, "offset2")
                label: modelData.type === "joint.sliding_dovetail" ? qsTr("Slope") : qsTr("Second offset")
                value: modelData.offset2 || ""
                onCommitted: function (text) { app.setJointFromText(modelData.id, "offset2", text) }
            }
            ValueField {
                visible: section.shows(modelData.type, "count")
                label: qsTr("Count")
                value: modelData.count || ""
                onCommitted: function (text) { app.setJointFromText(modelData.id, "count", text) }
            }
            Button {
                text: qsTr("Remove joint")
                Layout.fillWidth: true
                implicitHeight: Theme.controlHeight
                onClicked: app.removeJoint(modelData.id)
            }
        }
    }

    Connections {
        target: app
        function onDocumentChanged() { section.syncMate() }
    }
}
