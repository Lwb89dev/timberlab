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
    property var armedJointTypes: []
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
            MenuSeparator {}
            MenuItem { text: qsTr("Export cut list CSV…"); onTriggered: cutListCsvDialog.open() }
            MenuItem { text: qsTr("Export cut list JSON…"); onTriggered: cutListJsonDialog.open() }
            MenuItem { text: qsTr("Export front view…"); onTriggered: openDrawing("front", "svg", qsTr("Export front view"), qsTr("Front view wire (*.svg)")) }
            MenuItem { text: qsTr("Export top view…"); onTriggered: openDrawing("top", "svg", qsTr("Export top view"), qsTr("Top view wire (*.svg)")) }
            MenuItem { text: qsTr("Export right view…"); onTriggered: openDrawing("right", "svg", qsTr("Export right view"), qsTr("Right view wire (*.svg)")) }
            MenuItem { text: qsTr("Export sheet…"); onTriggered: openDrawing("sheet", "svg", qsTr("Export sheet"), qsTr("Sheet wire (*.svg)")) }
            MenuItem { text: qsTr("Export DXF wire…"); onTriggered: openDrawing("dxf", "dxf", qsTr("Export DXF wire"), qsTr("DXF wire (*.dxf)")) }
            MenuItem { text: qsTr("Export PDF wire…"); onTriggered: openDrawing("pdf", "pdf", qsTr("Export PDF wire"), qsTr("PDF wire (*.pdf)")) }
            MenuItem { text: qsTr("Export front dimensions…"); onTriggered: openDrawing("dimensions", "svg", qsTr("Export front dimensions"), qsTr("Front dimensions (*.svg)")) }
            MenuSeparator {}
            MenuItem { text: qsTr("Export library events…"); enabled: app.canPublishLibrary; onTriggered: libraryExportDialog.open() }
            MenuItem { text: qsTr("Import library events…"); enabled: app.canPublishLibrary; onTriggered: libraryImportDialog.open() }
            MenuSeparator {}
            MenuItem { text: qsTr("Restore backup…"); onTriggered: confirmRestore.open() }
        }
        Menu {
            title: qsTr("Edit")
            MenuItem { text: qsTr("Undo"); enabled: app.canUndo; onTriggered: app.undo() }
            MenuItem { text: qsTr("Redo"); enabled: app.canRedo; onTriggered: app.redo() }
            MenuItem { text: qsTr("Duplicate"); onTriggered: app.duplicateSelected() }
            MenuItem { text: qsTr("Delete"); onTriggered: app.deleteSelected() }
        }
        Menu {
            title: qsTr("Account")
            MenuItem { text: qsTr("Sign in with secret key…"); onTriggered: nsecDialog.open() }
            MenuItem { text: qsTr("Sign in with bunker…"); onTriggered: bunkerDialog.open() }
            MenuItem { text: qsTr("Sign in with Amber"); onTriggered: app.loginWithAmber() }
            MenuSeparator {}
            MenuItem { text: qsTr("Sign out"); enabled: app.signedIn; onTriggered: app.logout() }
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

    Menu {
        id: assemblyMenu
        MenuItem { text: qsTr("Table"); onTriggered: app.addAssembly("table") }
        MenuItem { text: qsTr("Cabinet"); onTriggered: app.addAssembly("carcass") }
        MenuItem { text: qsTr("Drawer box"); onTriggered: app.addAssembly("drawer") }
        MenuItem { text: qsTr("Frame"); onTriggered: app.addAssembly("frame") }
        MenuItem { text: qsTr("Shelving"); onTriggered: app.addAssembly("shelving") }
        MenuItem { text: qsTr("Bedside table"); onTriggered: app.addAssembly("bedside") }
    }

    Menu {
        id: contactJointMenu
        property real menuX: 0
        property real menuY: 0
        Instantiator {
            model: window.armedJointTypes
            delegate: MenuItem {
                required property string modelData
                text: window.jointTypeName(modelData)
                onTriggered: app.addArmedJoint(modelData)
            }
            onObjectAdded: (index, object) => contactJointMenu.insertItem(index, object)
            onObjectRemoved: (index, object) => contactJointMenu.removeItem(object)
        }
    }

    Dialog {
        id: renameDialog
        title: qsTr("Rename part")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        property string partId: ""
        ColumnLayout {
            width: 320
            TextField {
                id: renameField
                Layout.fillWidth: true
                selectByMouse: true
            }
        }
        onAccepted: {
            if (partId.length > 0) app.setPartName(partId, renameField.text)
        }
        onOpened: {
            renameField.forceActiveFocus()
            renameField.selectAll()
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
            Button {
                id: assemblyButton
                text: qsTr("Assembly")
                onClicked: assemblyMenu.popup(assemblyButton, 0, assemblyButton.height)
            }
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
            onRenameRequested: function (partId, partName) {
                renameDialog.partId = partId
                renameField.text = partName
                renameDialog.open()
            }
        }
        Rectangle { visible: window.showParts; width: 1; color: Theme.border; Layout.fillHeight: true }
        Viewport {
            id: viewport
            Layout.fillWidth: true
            Layout.fillHeight: true
            controller: app

            Rectangle {
                id: dragOverlay
                visible: viewport.dragOverlayVisible
                color: Theme.panelRaised
                border.color: Theme.accent
                border.width: 1
                radius: Theme.radius
                opacity: 0.92
                implicitWidth: dragOverlayRow.implicitWidth + 16
                implicitHeight: dragOverlayRow.implicitHeight + 8
                z: 1
                x: viewport.dragOverlayPosition.x - implicitWidth / 2
                y: viewport.dragOverlayPosition.y - implicitHeight / 2
                Row {
                    id: dragOverlayRow
                    anchors.centerIn: parent
                    spacing: 8
                    Label {
                        text: "X: " + window.formatMm(viewport.dragOverlayX)
                        color: viewport.dragSnapX ? Theme.accent : Theme.text
                        font.pixelSize: 12
                        textFormat: Text.PlainText
                    }
                    Label {
                        text: "Y: " + window.formatMm(viewport.dragOverlayY)
                        color: viewport.dragSnapY ? Theme.accent : Theme.text
                        font.pixelSize: 12
                        textFormat: Text.PlainText
                    }
                    Label {
                        text: "Z: " + window.formatMm(viewport.dragOverlayZ)
                        color: viewport.dragSnapZ ? Theme.accent : Theme.text
                        font.pixelSize: 12
                        textFormat: Text.PlainText
                    }
                }
            }
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
                text: app.signedIn ? app.accountNpub : qsTr("Not signed in")
                textFormat: Text.PlainText
                color: Theme.muted
                elide: Text.ElideMiddle
                Layout.maximumWidth: 280
            }
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
    FileDialog {
        id: cutListCsvDialog
        title: qsTr("Export cut list")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "csv"
        nameFilters: [qsTr("Cut list (*.csv)")]
        onAccepted: app.exportCutList(selectedFile, "0")
    }
    FileDialog {
        id: cutListJsonDialog
        title: qsTr("Export cut list and bill of materials")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("Cut list and BOM (*.json)")]
        onAccepted: app.exportCutList(selectedFile, "0")
    }
    FileDialog {
        id: drawingDialog
        property string kind: "front"
        title: qsTr("Export front view")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "svg"
        nameFilters: [qsTr("Front view wire (*.svg)")]
        onAccepted: app.exportDrawing(selectedFile, kind)
    }
    FileDialog {
        id: libraryExportDialog
        title: qsTr("Export library events")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "json"
        nameFilters: [qsTr("Library events (*.json)")]
        onAccepted: app.exportLibraryEvents(selectedFile)
    }
    FileDialog {
        id: libraryImportDialog
        title: qsTr("Import library events")
        fileMode: FileDialog.OpenFile
        nameFilters: [qsTr("Library events (*.json)")]
        onAccepted: app.importLibraryEvents(selectedFile)
    }
    Dialog {
        id: nsecDialog
        title: qsTr("Sign in with a secret key")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            width: 380
            Label {
                text: qsTr("The key stays in memory for this session. It is not saved and it is not sent to a relay.")
                textFormat: Text.PlainText
                color: Theme.text
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            TextField {
                id: nsecField
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                placeholderText: "nsec1…"
                Layout.fillWidth: true
            }
        }
        onAccepted: {
            app.loginWithNsec(nsecField.text)
            nsecField.text = ""
        }
        onRejected: nsecField.text = ""
    }
    Dialog {
        id: bunkerDialog
        title: qsTr("Sign in with a bunker")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            width: 380
            Label {
                text: qsTr("Paste a bunker:// link. Its secret is not saved. Sign-in finishes only after the bunker returns your public key.")
                textFormat: Text.PlainText
                color: Theme.text
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            TextField {
                id: bunkerField
                echoMode: TextInput.Password
                inputMethodHints: Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                placeholderText: "bunker://"
                Layout.fillWidth: true
            }
        }
        onAccepted: {
            app.loginWithBunker(bunkerField.text)
            bunkerField.text = ""
        }
        onRejected: bunkerField.text = ""
    }
    Dialog {
        id: confirmRestore
        title: qsTr("Restore backup")
        modal: true
        anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        Label {
            text: qsTr("Replace the project with the backup? The backup is checked first. An invalid backup leaves the project file as it is.")
            textFormat: Text.PlainText
            color: Theme.text
            wrapMode: Text.WordWrap
            width: 360
        }
        onAccepted: app.restoreBackup()
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
    function openDrawing(kind, suffix, title, filter) {
        drawingDialog.kind = kind
        drawingDialog.title = title
        drawingDialog.defaultSuffix = suffix
        drawingDialog.nameFilters = [filter]
        drawingDialog.open()
    }
    function saveOrAsk() {
        viewport.forceActiveFocus()
        if (app.projectPath.length === 0) saveDialog.open()
        else app.saveCurrent()
    }
    function jointTypeName(key) {
        if (key === "joint.mortise_tenon") return qsTr("Mortise and tenon")
        if (key === "joint.half_lap") return qsTr("Half lap")
        if (key === "joint.dado") return qsTr("Dado")
        if (key === "joint.groove") return qsTr("Groove")
        if (key === "joint.rabbet") return qsTr("Rabbet")
        if (key === "joint.bridle") return qsTr("Bridle")
        if (key === "joint.castle") return qsTr("Castle")
        if (key === "joint.miter") return qsTr("Miter")
        if (key === "joint.dovetail") return qsTr("Dovetail")
        if (key === "joint.sliding_dovetail") return qsTr("Sliding dovetail")
        if (key === "joint.finger") return qsTr("Finger joint")
        if (key === "joint.half_blind_dovetail") return qsTr("Half-blind dovetail")
        if (key === "joint.tongue_groove") return qsTr("Tongue and groove")
        if (key === "joint.dowel") return qsTr("Dowel")
        return key
    }
    function formatMm(value) {
        return app.formatLength(value)
    }
    function openContactMenu(x, y) {
        armedJointTypes = app.armedJointTypes()
        contactJointMenu.popup(viewport, x, y)
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
    Connections {
        target: viewport
        function onContactMenuRequested(x, y) { openContactMenu(x, y) }
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
