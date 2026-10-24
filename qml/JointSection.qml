import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import TimberLab

ColumnLayout {
    id: section
    spacing: 8
    visible: app.selection.id !== undefined && app.selection.multi !== true && (app.partChoices.length > 0 || app.joints.length > 0)

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
        "joint.dowel",
        "joint.haunched_tenon",
        "joint.wedged_tenon",
        "joint.loose_tenon",
        "joint.cross_lap",
        "joint.end_lap",
        "joint.splined_miter",
        "joint.biscuit",
        "joint.pocket_screw"
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
        if (type === "joint.haunched_tenon") return qsTr("Haunched tenon")
        if (type === "joint.wedged_tenon") return qsTr("Wedged tenon")
        if (type === "joint.loose_tenon") return qsTr("Loose tenon")
        if (type === "joint.cross_lap") return qsTr("Cross lap")
        if (type === "joint.end_lap") return qsTr("End lap")
        if (type === "joint.splined_miter") return qsTr("Splined miter")
        if (type === "joint.biscuit") return qsTr("Biscuit")
        if (type === "joint.pocket_screw") return qsTr("Pocket screw")
        return type
    }
    function fieldLabel(type, key) {
        if (key === "length") {
            if (type === "joint.end_lap") return qsTr("Lap length")
            if (type === "joint.pocket_screw") return qsTr("Counterbore length")
            return qsTr("Length")
        }
        if (key === "width") {
            if (type === "joint.loose_tenon") return qsTr("Pocket length")
            if (type === "joint.biscuit") return qsTr("Disc diameter")
            if (type === "joint.end_lap") return qsTr("Notch width")
            if (type === "joint.pocket_screw") return qsTr("Counterbore diameter")
            return qsTr("Width")
        }
        if (key === "thickness") {
            if (type === "joint.dowel") return qsTr("Diameter")
            if (type === "joint.loose_tenon") return qsTr("Pocket thickness")
            if (type === "joint.biscuit" || type === "joint.splined_miter") return qsTr("Kerf thickness")
            if (type === "joint.pocket_screw") return qsTr("Pilot diameter")
            return qsTr("Thickness")
        }
        if (key === "depth") {
            if (type === "joint.end_lap") return qsTr("Notch depth")
            if (type === "joint.splined_miter") return qsTr("Kerf depth")
            return qsTr("Depth")
        }
        if (key === "offset") {
            if (type === "joint.dovetail" || type === "joint.half_blind_dovetail") return qsTr("Slope")
            if (type === "joint.loose_tenon" || type === "joint.biscuit") return qsTr("Pitch")
            if (type === "joint.splined_miter") return qsTr("Kerf offset")
            if (type === "joint.pocket_screw") return qsTr("Distance from end")
            return qsTr("Offset")
        }
        if (key === "offset2") {
            if (type === "joint.sliding_dovetail") return qsTr("Slope")
            if (type === "joint.pocket_screw") return qsTr("Spacing")
            return qsTr("Second offset")
        }
        if (key === "length2") return type === "joint.wedged_tenon" ? qsTr("Kerf depth") : qsTr("Haunch length")
        if (key === "width2") return qsTr("Kerf width")
        return qsTr("Count")
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
        if (type === "joint.haunched_tenon") return key !== "width2" && key !== "count"
        if (type === "joint.wedged_tenon") return true
        if (type === "joint.loose_tenon" || type === "joint.biscuit")
            return key === "thickness" || key === "width" || key === "depth" || key === "offset" || key === "count"
        if (type === "joint.cross_lap") return key === "depth" || key === "offset" || key === "offset2"
        if (type === "joint.end_lap") return key === "length" || key === "width" || key === "depth" || key === "offset"
        if (type === "joint.splined_miter")
            return key === "length" || key === "thickness" || key === "depth" || key === "offset"
        if (type === "joint.pocket_screw")
            return key === "length" || key === "width" || key === "thickness" || key === "offset" || key === "offset2" || key === "count"
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
            textFormat: Text.PlainText
            color: Theme.muted
            font.pixelSize: 11
            font.weight: Font.Medium
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

    SectionTitle { text: qsTr("Joinery") }
    ComboBox {
        id: typeBox
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        model: section.typeKeys.map(function (key) { return section.typeName(key) })
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
    AppButton {
        text: qsTr("Add joint")
        glyph: "+"
        primary: true
        Layout.fillWidth: true
        implicitHeight: Theme.controlHeight
        enabled: mateBox.count > 0 && app.selectedId.length > 0
        onClicked: app.addJoint(section.typeKey(), app.selectedId, mateBox.currentValue)
    }

    Repeater {
        model: app.joints
        delegate: Rectangle {
            required property var modelData
            Layout.fillWidth: true
            implicitHeight: jointContent.implicitHeight + 20
            radius: Theme.radius
            color: Theme.field
            border.color: Theme.border
            border.width: 1
            ColumnLayout {
                id: jointContent
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8
                Label {
                    text: section.typeName(modelData.type) + " — " + (modelData.mateName || "")
                    textFormat: Text.PlainText
                    color: Theme.accentSoft
                    font.weight: Font.DemiBold
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                ValueField {
                    visible: section.shows(modelData.type, "length")
                    label: section.fieldLabel(modelData.type, "length")
                    value: modelData.length || ""
                    onCommitted: function (text) { app.setJointFromText(modelData.id, "length", text) }
                }
                ValueField {
                    visible: section.shows(modelData.type, "width")
                    label: section.fieldLabel(modelData.type, "width")
                    value: modelData.width || ""
                    onCommitted: function (text) { app.setJointFromText(modelData.id, "width", text) }
                }
                ValueField {
                    visible: section.shows(modelData.type, "thickness")
                    label: section.fieldLabel(modelData.type, "thickness")
                    value: modelData.thickness || ""
                    onCommitted: function (text) { app.setJointFromText(modelData.id, "thickness", text) }
                }
                ValueField {
                    visible: section.shows(modelData.type, "depth")
                    label: section.fieldLabel(modelData.type, "depth")
                    value: modelData.depth || ""
                    onCommitted: function (text) { app.setJointFromText(modelData.id, "depth", text) }
                }
                ValueField {
                    visible: section.shows(modelData.type, "offset")
                    label: section.fieldLabel(modelData.type, "offset")
                    value: modelData.offset || ""
                    onCommitted: function (text) { app.setJointFromText(modelData.id, "offset", text) }
                }
                ValueField {
                    visible: section.shows(modelData.type, "offset2")
                    label: section.fieldLabel(modelData.type, "offset2")
                    value: modelData.offset2 || ""
                    onCommitted: function (text) { app.setJointFromText(modelData.id, "offset2", text) }
                }
                ValueField {
                    visible: section.shows(modelData.type, "length2")
                    label: section.fieldLabel(modelData.type, "length2")
                    value: modelData.length2 || ""
                    onCommitted: function (text) { app.setJointFromText(modelData.id, "length2", text) }
                }
                ValueField {
                    visible: section.shows(modelData.type, "width2")
                    label: section.fieldLabel(modelData.type, "width2")
                    value: modelData.width2 || ""
                    onCommitted: function (text) { app.setJointFromText(modelData.id, "width2", text) }
                }
                ValueField {
                    visible: section.shows(modelData.type, "count")
                    label: section.fieldLabel(modelData.type, "count")
                    value: modelData.count || ""
                    onCommitted: function (text) { app.setJointFromText(modelData.id, "count", text) }
                }
                AppButton {
                    text: qsTr("Remove joint")
                    destructive: true
                    Layout.fillWidth: true
                    implicitHeight: Theme.controlHeight
                    onClicked: app.removeJoint(modelData.id)
                }
            }
        }
    }

    Connections {
        target: app
        function onDocumentChanged() { section.syncMate() }
    }
}
