import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs
import QtQuick.Layouts
import QtCore
import TimberLab
import TimberLab.Render

ApplicationWindow {
    id: window
    width: 1440
    height: 900
    minimumWidth: 1080
    minimumHeight: 680
    visible: true
    title: "TimberLab — " + app.projectName
    color: Theme.bg

    Material.theme: Theme.darkMode ? Material.Dark : Material.Light
    Material.primary: Theme.panel
    Material.accent: Theme.accent
    Material.foreground: Theme.text
    Material.background: Theme.bg

    palette.window: Theme.panel
    palette.windowText: Theme.text
    palette.base: Theme.field
    palette.text: Theme.text
    palette.button: Theme.panelRaised
    palette.buttonText: Theme.text
    palette.highlight: Theme.accent
    palette.highlightedText: Theme.onAccent
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

    Settings {
        id: appearanceSettings
        category: "Appearance"
        property bool darkMode: true
    }

    Behavior on color { ColorAnimation { duration: Theme.motion } }

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
            MenuItem { text: qsTr("Export hidden-line sheet…"); onTriggered: openDrawing("hlr-sheet", "svg", qsTr("Export hidden-line sheet"), qsTr("Hidden-line sheet (*.svg)")) }
            MenuItem { text: qsTr("Export hidden-line DXF…"); onTriggered: openDrawing("hlr-dxf", "dxf", qsTr("Export hidden-line DXF"), qsTr("Hidden-line DXF (*.dxf)")) }
            MenuItem { text: qsTr("Export hidden-line PDF…"); onTriggered: openDrawing("hlr-pdf", "pdf", qsTr("Export hidden-line PDF"), qsTr("Hidden-line PDF (*.pdf)")) }
            Menu {
                title: qsTr("Export 3D model")
                MenuItem { text: qsTr("STL, whole project…"); onTriggered: openModel("stl", "project", qsTr("Export STL"), qsTr("STL (*.stl)")) }
                MenuItem { text: qsTr("STL, selected part…"); enabled: app.hasSelection; onTriggered: openModel("stl", "part", qsTr("Export STL"), qsTr("STL (*.stl)")) }
                MenuItem { text: qsTr("STEP, whole project…"); enabled: app.stepAvailable(); onTriggered: openModel("step", "project", qsTr("Export STEP"), qsTr("STEP (*.step *.stp)")) }
                MenuItem { text: qsTr("STEP, selected part…"); enabled: app.stepAvailable() && app.hasSelection; onTriggered: openModel("step", "part", qsTr("Export STEP"), qsTr("STEP (*.step *.stp)")) }
            }
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
        implicitHeight: Theme.toolbarHeight
        leftPadding: 12
        rightPadding: 12
        topPadding: 8
        bottomPadding: 8
        background: Rectangle {
            color: Theme.panel
            border.width: 0
            Rectangle {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.bottom: parent.bottom
                height: 1
                color: Theme.border
            }
            Behavior on color { ColorAnimation { duration: Theme.motion } }
        }
        RowLayout {
            anchors.fill: parent
            spacing: 5

            Rectangle {
                Layout.preferredWidth: 42
                Layout.preferredHeight: 42
                radius: 12
                color: Theme.field
                border.color: Theme.border
                border.width: 1
                Image {
                    anchors.fill: parent
                    anchors.margins: 3
                    source: "qrc:/icons/icon.png"
                    fillMode: Image.PreserveAspectFit
                    mipmap: true
                    smooth: true
                }
            }
            ColumnLayout {
                Layout.preferredWidth: 116
                spacing: -1
                Label {
                    text: "TimberLab"
                    color: Theme.text
                    font.pixelSize: 16
                    font.weight: Font.DemiBold
                }
                RowLayout {
                    spacing: 5
                    Rectangle {
                        width: 6
                        height: 6
                        radius: 3
                        color: app.dirty ? Theme.accent : Theme.ok
                    }
                    Label {
                        text: app.dirty ? qsTr("UNSAVED") : qsTr("SAVED")
                        color: Theme.muted
                        font.pixelSize: 8
                        font.weight: Font.DemiBold
                        font.letterSpacing: 1
                    }
                }
            }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 32; color: Theme.border }

            AppButton { text: qsTr("New"); glyph: "+"; subtle: true; onClicked: askNew() }
            AppButton { text: qsTr("Open"); glyph: "◰"; subtle: true; onClicked: confirmIfDirty("open") }
            AppButton { text: qsTr("Save"); glyph: "↓"; primary: true; onClicked: saveOrAsk() }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 28; color: Theme.border }
            AppButton { glyph: "↶"; compact: true; subtle: true; toolTip: qsTr("Undo"); enabled: app.canUndo; onClicked: app.undo() }
            AppButton { glyph: "↷"; compact: true; subtle: true; toolTip: qsTr("Redo"); enabled: app.canRedo; onClicked: app.redo() }
            Rectangle { Layout.preferredWidth: 1; Layout.preferredHeight: 28; color: Theme.border }
            AppButton { text: qsTr("Board"); glyph: "▰"; subtle: true; onClicked: app.addBoard() }
            AppButton { text: qsTr("Box"); glyph: "◆"; subtle: true; onClicked: app.addBox() }
            AppButton { text: qsTr("Cylinder"); glyph: "●"; subtle: true; onClicked: app.addCylinder() }
            AppButton {
                id: assemblyButton
                text: qsTr("Assembly")
                glyph: "◈"
                subtle: true
                onClicked: assemblyMenu.popup(assemblyButton, 0, assemblyButton.height)
            }
            AppButton {
                id: joineryButton
                text: qsTr("Joinery")
                glyph: "⋈"
                subtle: true
                enabled: app.selectedIds.length === 2
                toolTip: qsTr("Join the two selected boards. The board you selected last becomes the tenon or the first lap.")
                onClicked: openJoineryMenu(joineryButton)
            }
            AppButton { glyph: "⧉"; compact: true; subtle: true; toolTip: qsTr("Duplicate"); onClicked: app.duplicateSelected() }
            AppButton { glyph: "×"; compact: true; destructive: true; toolTip: qsTr("Delete"); onClicked: app.deleteSelected() }
            Item { Layout.fillWidth: true }
            AppButton { glyph: "⛶"; compact: true; subtle: true; toolTip: qsTr("Fit all"); onClicked: viewport.frameAll() }
            AppButton {
                glyph: window.showParts ? "◧" : "▯"
                compact: true
                subtle: true
                toolTip: window.showParts ? qsTr("Hide parts") : qsTr("Show parts")
                onClicked: window.showParts = !window.showParts
            }
            AppButton {
                glyph: window.showInspector ? "◨" : "▯"
                compact: true
                subtle: true
                toolTip: window.showInspector ? qsTr("Hide inspector") : qsTr("Show inspector")
                onClicked: window.showInspector = !window.showInspector
            }
            AppButton {
                glyph: Theme.darkMode ? "☼" : "☾"
                compact: true
                subtle: true
                toolTip: Theme.darkMode ? qsTr("Use light theme") : qsTr("Use dark theme")
                onClicked: {
                    Theme.darkMode = !Theme.darkMode
                    appearanceSettings.darkMode = Theme.darkMode
                }
            }
        }
    }

    RowLayout {
        id: workspace
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8
        opacity: 0
        scale: 0.992

        Item {
            property real animatedWidth: window.showParts ? 292 : 0
            Layout.preferredWidth: animatedWidth
            Layout.fillHeight: true
            clip: true
            Behavior on animatedWidth { NumberAnimation { duration: Theme.motion; easing.type: Easing.OutCubic } }
            PartsPanel {
                width: 292
                height: parent.height
                opacity: window.showParts ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
                onDemoRequested: window.confirmIfDirty("demo")
                onRenameRequested: function (partId, partName) {
                    renameDialog.partId = partId
                    renameField.text = partName
                    renameDialog.open()
                }
            }
        }

        Rectangle {
            id: viewportFrame
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.radiusLarge
            color: Theme.canvas
            border.color: Theme.border
            border.width: 1
            clip: true

            Viewport {
                id: viewport
                anchors.fill: parent
                anchors.margins: 1
                controller: app
                darkMode: Theme.darkMode
            }

            Rectangle {
                anchors.top: parent.top
                anchors.left: parent.left
                anchors.margins: 14
                implicitWidth: projectChipRow.implicitWidth + 20
                implicitHeight: 34
                radius: 10
                color: Theme.darkMode ? "#d9161917" : "#e6ffffff"
                border.color: Theme.border
                Row {
                    id: projectChipRow
                    anchors.centerIn: parent
                    spacing: 7
                    Rectangle {
                        width: 7
                        height: 7
                        radius: 4
                        color: app.dirty ? Theme.accent : Theme.ok
                        anchors.verticalCenter: parent.verticalCenter
                    }
                    Label {
                        text: app.projectName
                        textFormat: Text.PlainText
                        color: Theme.text
                        font.pixelSize: 11
                        font.weight: Font.Medium
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }

            Rectangle {
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: 14
                implicitWidth: viewTools.implicitWidth + 10
                implicitHeight: 42
                radius: 12
                color: Theme.darkMode ? "#d9161917" : "#e6ffffff"
                border.color: Theme.border
                Row {
                    id: viewTools
                    anchors.centerIn: parent
                    spacing: 2
                    AppButton { text: qsTr("TOP"); compact: true; subtle: true; onClicked: viewport.setStandardView("top") }
                    AppButton { text: qsTr("FRONT"); compact: true; subtle: true; onClicked: viewport.setStandardView("front") }
                    AppButton { text: qsTr("RIGHT"); compact: true; subtle: true; onClicked: viewport.setStandardView("right") }
                    AppButton { text: qsTr("ISO"); compact: true; subtle: true; onClicked: viewport.setStandardView("iso") }
                }
            }

            Rectangle {
                id: dragOverlay
                visible: viewport.dragOverlayVisible
                color: Theme.panel
                border.color: Theme.accent
                border.width: 1
                radius: 12
                opacity: visible ? 0.96 : 0
                scale: visible ? 1 : 0.9
                implicitWidth: dragOverlayRow.implicitWidth + 22
                implicitHeight: dragOverlayRow.implicitHeight + 12
                z: 3
                x: viewport.dragOverlayPosition.x - implicitWidth / 2
                y: viewport.dragOverlayPosition.y - implicitHeight / 2
                Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
                Behavior on scale { NumberAnimation { duration: Theme.motionFast; easing.type: Easing.OutBack } }
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

        Item {
            property real animatedWidth: window.showInspector ? 340 : 0
            Layout.preferredWidth: animatedWidth
            Layout.fillHeight: true
            clip: true
            Behavior on animatedWidth { NumberAnimation { duration: Theme.motion; easing.type: Easing.OutCubic } }
            Inspector {
                width: 340
                height: parent.height
                opacity: window.showInspector ? 1 : 0
                Behavior on opacity { NumberAnimation { duration: Theme.motionFast } }
            }
        }
    }

    footer: Rectangle {
        implicitHeight: Theme.statusHeight
        color: Theme.panel
        border.color: Theme.border
        border.width: 0
        Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: Theme.border }
        Behavior on color { ColorAnimation { duration: Theme.motion } }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            spacing: 10
            Rectangle {
                width: 7
                height: 7
                radius: 4
                color: app.signedIn ? Theme.ok : Theme.faint
            }
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
                implicitHeight: 30
                implicitWidth: 136
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
                implicitHeight: 30
                implicitWidth: 74
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
                implicitHeight: 30
                implicitWidth: 132
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
                visible: window.width >= 1280
                text: qsTr("Orbit: drag  ·  Pan: shift-drag or right-drag  ·  Zoom: wheel  ·  Z up")
                color: Theme.muted
                font.pixelSize: 10
            }
        }
    }

    SequentialAnimation {
        id: workspaceIntro
        PauseAnimation { duration: 60 }
        ParallelAnimation {
            NumberAnimation {
                target: workspace
                property: "opacity"
                from: 0
                to: 1
                duration: 320
                easing.type: Easing.OutCubic
            }
            NumberAnimation {
                target: workspace
                property: "scale"
                from: 0.992
                to: 1
                duration: 360
                easing.type: Easing.OutCubic
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
        id: modelDialog
        property string format: "stl"
        property string scope: "project"
        title: qsTr("Export STL")
        fileMode: FileDialog.SaveFile
        defaultSuffix: "stl"
        nameFilters: [qsTr("STL (*.stl)")]
        onAccepted: app.exportModel(selectedFile, format, scope)
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
    function openModel(format, scope, title, filter) {
        modelDialog.format = format
        modelDialog.scope = scope
        modelDialog.title = title
        modelDialog.defaultSuffix = format === "step" ? "step" : "stl"
        modelDialog.nameFilters = [filter]
        modelDialog.open()
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
        if (key === "joint.haunched_tenon") return qsTr("Haunched tenon")
        if (key === "joint.wedged_tenon") return qsTr("Wedged tenon")
        if (key === "joint.loose_tenon") return qsTr("Loose tenon")
        if (key === "joint.cross_lap") return qsTr("Cross lap")
        if (key === "joint.end_lap") return qsTr("End lap")
        if (key === "joint.splined_miter") return qsTr("Splined miter")
        if (key === "joint.biscuit") return qsTr("Biscuit")
        if (key === "joint.pocket_screw") return qsTr("Pocket screw")
        return key
    }
    function formatMm(value) {
        return app.formatLength(value)
    }
    function openContactMenu(x, y) {
        armedJointTypes = app.armedJointTypes()
        contactJointMenu.popup(viewport, x, y)
    }
    function openJoineryMenu(anchor) {
        armedJointTypes = app.armSelectedPair()
        if (armedJointTypes.length === 0) return
        contactJointMenu.popup(anchor, 0, anchor.height)
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

    Component.onCompleted: {
        Theme.darkMode = appearanceSettings.darkMode
        syncFooter()
        workspaceIntro.start()
    }

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
