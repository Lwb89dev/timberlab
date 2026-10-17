import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import TimberLab
import TimberLab.Render

ApplicationWindow {
    id: window
    width: 1440
    height: 900
    visible: true
    title: "TimberLab — " + app.projectName
    color: Theme.bg

    palette.window: Theme.panel
    palette.windowText: Theme.text
    palette.base: Theme.field
    palette.text: Theme.text
    palette.button: Theme.panelRaised
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: "#1c1b19"
    palette.mid: Theme.border
    palette.placeholderText: Theme.muted

    property bool showParts: true
    property bool showInspector: true
    property bool allowDirtyQuit: false
    property string pendingAction: ""
    property bool editingText: {
        const item = window.activeFocusItem
        if (!item) return false
        return item instanceof TextInput || item instanceof TextEdit || item instanceof ComboBox
    }

    menuBar: MenuBar {
        Menu {
            title: qsTr("File")
            MenuItem { text: qsTr("New"); onTriggered: askNew() }
            MenuItem { text: qsTr("Open…"); onTriggered: confirmIfDirty("open") }
            MenuItem { text: qsTr("Save"); onTriggered: saveOrAsk() }
            MenuItem { text: qsTr("Save as…"); onTriggered: saveDialog.open() }
        }
        Menu {
            title: qsTr("Edit")
            MenuItem { text: qsTr("Undo"); enabled: app.canUndo; onTriggered: app.undo() }
            MenuItem { text: qsTr("Redo"); enabled: app.canRedo; onTriggered: app.redo() }
            MenuItem { text: qsTr("Duplicate"); onTriggered: app.duplicateSelected() }
            MenuItem { text: qsTr("Delete"); onTriggered: app.deleteSelected() }
        }
        Menu {
            title: qsTr("View")
            MenuItem { text: qsTr("Fit all"); onTriggered: viewport.frameAll() }
            MenuItem { text: qsTr("Top"); onTriggered: viewport.setStandardView("top") }
            MenuItem { text: qsTr("Front"); onTriggered: viewport.setStandardView("front") }
            MenuItem { text: qsTr("Right"); onTriggered: viewport.setStandardView("right") }
            MenuItem { text: qsTr("Isometric"); onTriggered: viewport.setStandardView("iso") }
            MenuItem { text: showParts ? qsTr("Hide parts") : qsTr("Show parts"); onTriggered: showParts = !showParts }
            MenuItem { text: showInspector ? qsTr("Hide inspector") : qsTr("Show inspector"); onTriggered: showInspector = !showInspector }
        }
    }

    header: ToolBar {
        leftPadding: 8
        rightPadding: 8
        background: Rectangle { color: Theme.panel }
        RowLayout {
            anchors.fill: parent
            spacing: 6
            Button { text: qsTr("New"); onClicked: askNew() }
            Button { text: qsTr("Open"); onClicked: confirmIfDirty("open") }
            Button { text: qsTr("Save"); onClicked: saveOrAsk() }
            ToolSeparator {}
            Button { text: qsTr("Undo"); enabled: app.canUndo; onClicked: app.undo() }
            Button { text: qsTr("Redo"); enabled: app.canRedo; onClicked: app.redo() }
            ToolSeparator {}
            Button { text: qsTr("Board"); onClicked: app.addBoard() }
            Button { text: qsTr("Box"); onClicked: app.addBox() }
            Button { text: qsTr("Cylinder"); onClicked: app.addCylinder() }
            Button { text: qsTr("Duplicate"); onClicked: app.duplicateSelected() }
            Button { text: qsTr("Delete"); onClicked: app.deleteSelected() }
            ToolSeparator {}
            Button { text: qsTr("Fit"); onClicked: viewport.frameAll() }
            Item { Layout.fillWidth: true }
        }
    }

    RowLayout {
        anchors.fill: parent
        spacing: 0
        PartsPanel {
            visible: window.showParts
            Layout.preferredWidth: 280
            Layout.fillHeight: true
            onDemoRequested: window.confirmIfDirty("demo")
        }
        Rectangle { visible: window.showParts; width: 1; color: Theme.border; Layout.fillHeight: true }
        Viewport {
            id: viewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            controller: app
        }
        Rectangle { visible: window.showInspector; width: 1; color: Theme.border; Layout.fillHeight: true }
        Inspector {
            visible: window.showInspector
            Layout.preferredWidth: 320
            Layout.fillHeight: true
        }
    }

    footer: Rectangle {
        implicitHeight: 40
        color: Theme.panel
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            Label {
                text: app.status
                textFormat: Text.PlainText
                color: Theme.muted
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
            ComboBox {
                id: unitBox
                implicitHeight: 28
                textRole: "label"
                valueRole: "value"
                model: [
                    { label: qsTr("Millimetres"), value: "mm" },
                    { label: qsTr("Centimetres"), value: "cm" },
                    { label: qsTr("Metres"), value: "m" },
                    { label: qsTr("Inches"), value: "in" },
                    { label: qsTr("Feet"), value: "ft" }
                ]
                onActivated: app.setDisplayUnit(currentValue)
            }
            ComboBox {
                id: fractionBox
                visible: app.displayUnit === "in" || app.displayUnit === "ft"
                implicitHeight: 28
                textRole: "label"
                valueRole: "value"
                model: [
                    { label: "1/2", value: 2 }, { label: "1/4", value: 4 }, { label: "1/8", value: 8 },
                    { label: "1/16", value: 16 }, { label: "1/32", value: 32 }, { label: "1/64", value: 64 }
                ]
                onActivated: app.setFractionPrecision(currentValue)
            }
            ComboBox {
                id: qualityBox
                implicitHeight: 28
                textRole: "label"
                valueRole: "value"
                model: [
                    { label: qsTr("Draft"), value: "draft" },
                    { label: qsTr("Standard"), value: "standard" },
                    { label: qsTr("Fine"), value: "fine" }
                ]
                onActivated: app.meshQuality = currentValue
            }
            Label {
                text: qsTr("Orbit: drag  ·  Pan: shift-drag or right-drag  ·  Zoom: wheel  ·  Z up")
                color: Theme.muted
            }
        }
    }

    FileDialog {
        id: openDialog
        title: qsTr("Open project")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("TimberLab project (*.timberlab)")]
        onAccepted: app.loadProject(selectedFile)
    }
    FileDialog {
        id: saveDialog
        title: qsTr("Save project")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "timberlab"
        nameFilters: [qsTr("TimberLab project (*.timberlab)")]
        onAccepted: app.saveProject(selectedFile)
    }

    Dialog {
        id: confirmDirty
        title: qsTr("Unsaved changes")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Discard | Dialog.Cancel
        Label {
            text: qsTr("Discard unsaved changes?")
            textFormat: Text.PlainText
            color: Theme.text
        }
        onDiscarded: runPending()
        onRejected: pendingAction = ""
    }
    Dialog {
        id: errorDialog
        title: qsTr("TimberLab")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok
        property string body: ""
        Label {
            text: errorDialog.body
            textFormat: Text.PlainText
            color: Theme.text
            wrapMode: Text.WordWrap
            width: 360
        }
    }

    function askNew() { confirmIfDirty("new") }
    function confirmIfDirty(action) {
        if (!app.dirty) {
            pendingAction = action
            runPending()
            return
        }
        pendingAction = action
        confirmDirty.open()
    }
    function runPending() {
        const action = pendingAction
        pendingAction = ""
        if (action === "new") app.newProject()
        else if (action === "open") openDialog.open()
        else if (action === "demo") app.makeDemo()
        else if (action === "quit") {
            allowDirtyQuit = true
            window.close()
        }
    }
    function saveOrAsk() {
        viewport.forceActiveFocus()
        if (app.projectPath.length === 0) saveDialog.open()
        else app.saveCurrent()
    }
    function syncFooter() {
        const unitIndex = unitBox.indexOfValue(app.displayUnit)
        if (unitIndex >= 0 && unitIndex !== unitBox.currentIndex) unitBox.currentIndex = unitIndex
        const fractionIndex = fractionBox.indexOfValue(app.fractionPrecision)
        if (fractionIndex >= 0 && fractionIndex !== fractionBox.currentIndex) fractionBox.currentIndex = fractionIndex
        const qualityIndex = qualityBox.indexOfValue(app.meshQuality)
        if (qualityIndex >= 0 && qualityIndex !== qualityBox.currentIndex) qualityBox.currentIndex = qualityIndex
    }

    onClosing: function (close) {
        if (allowDirtyQuit || !app.dirty) return
        close.accepted = false
        pendingAction = "quit"
        confirmDirty.open()
    }

    Component.onCompleted: syncFooter()

    Connections {
        target: app
        function onDocumentChanged() { syncFooter() }
        function onErrorRaised(message) {
            errorDialog.body = message
            errorDialog.open()
        }
        function onRequestFrameAll() { viewport.frameAll() }
    }

    Shortcut { sequences: [StandardKey.New]; onActivated: askNew() }
    Shortcut { sequences: [StandardKey.Open]; onActivated: confirmIfDirty("open") }
    Shortcut { sequences: [StandardKey.Save]; onActivated: saveOrAsk() }
    Shortcut { sequences: [StandardKey.Undo]; enabled: !editingText; onActivated: app.undo() }
    Shortcut { sequences: [StandardKey.Redo]; enabled: !editingText; onActivated: app.redo() }
    Shortcut { sequences: ["Ctrl+D"]; enabled: !editingText; onActivated: app.duplicateSelected() }
    Shortcut { sequences: [StandardKey.Delete]; enabled: !editingText; onActivated: app.deleteSelected() }
    Shortcut { sequences: ["F"]; enabled: !editingText; onActivated: viewport.frameAll() }
}
