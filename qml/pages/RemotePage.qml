import QtQuick 2.7
import Lomiri.Components 1.3
import QtQuick.Layouts 1.3
import "../backend/QrCode.js" as QrCode

Page {
    id: page
    property var ajaxController

    function modeButtonTextColor(mode) {
        return theme.palette.normal.backgroundText
    }

    function statusColor() {
        if (ajaxController.panicSending) {
            return "#dc2626"
        }
        if (ajaxController.alarmMode === "Armed") {
            return "#b91c1c"
        }
        if (ajaxController.alarmMode === "Disarmed") {
            return "#15803d"
        }
        if (ajaxController.alarmMode === "Night mode") {
            return "#4f46e5"
        }
        if (ajaxController.alarmMode === "Partially armed") {
            return "#d97706"
        }
        return theme.palette.normal.base
    }

    function statusTitle() {
        if (ajaxController.panicSending) {
            return i18n.tr("Panic sending")
        }
        if (ajaxController.alarmMode === "Armed") {
            return i18n.tr("Armed")
        }
        if (ajaxController.alarmMode === "Disarmed") {
            return i18n.tr("Disarmed")
        }
        if (ajaxController.alarmMode === "Night mode") {
            return i18n.tr("Night mode")
        }
        return ajaxController.alarmMode
    }

    function statusIconName() {
        if (ajaxController.panicSending) {
            return "dialog-warning-symbolic"
        }
        if (ajaxController.alarmMode === "Disarmed") {
            return "security-low"
        }
        if (ajaxController.alarmMode === "Night mode") {
            return "night-mode"
        }
        return "security-high"
    }

    header: PageHeader {
        leadingActionBar.actions: [
            Action {
                text: i18n.tr("Messages")
                iconName: "message"
                onTriggered: {
                    ajaxController.fetchMessages()
                    messagesOverlay.visible = true
                }
            }
        ]
        contents: Item {
            anchors.fill: parent

            Rectangle {
                anchors {
                    verticalCenter: parent.verticalCenter
                    left: parent.left
                    right: parent.right
                    margins: units.gu(1)
                }
                height: units.gu(4.6)
                radius: units.gu(0.7)
                color: spaceMouse.pressed ? theme.palette.normal.base : "transparent"
                border.width: 0

                Row {
                    anchors {
                        fill: parent
                        leftMargin: units.gu(1)
                        rightMargin: units.gu(1)
                    }
                    spacing: units.gu(0.6)

                    Label {
                        width: parent.width - spaceChevron.width - parent.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        text: ajaxController.selectedSystemName.length > 0
                              ? ajaxController.selectedSystemName
                              : i18n.tr("Alarm Remote")
                        horizontalAlignment: Text.AlignHCenter
                        elide: Text.ElideRight
                    }

                    Icon {
                        id: spaceChevron
                        width: units.gu(2)
                        height: width
                        anchors.verticalCenter: parent.verticalCenter
                        name: "go-down"
                        visible: ajaxController.spaces.length > 0
                        color: theme.palette.normal.backgroundSecondaryText
                    }
                }

                MouseArea {
                    id: spaceMouse
                    anchors.fill: parent
                    enabled: ajaxController.spaces.length > 0
                    onClicked: spacesOverlay.visible = true
                }
            }
        }
        trailingActionBar.actions: [
            Action {
                text: i18n.tr("QR")
                iconName: "view-grid-symbolic"
                onTriggered: {
                    ajaxController.startQrLogin()
                    loginOverlay.visible = true
                }
            }
        ]
    }

    Rectangle {
        anchors.fill: parent
        color: theme.palette.normal.background
    }

    Flickable {
        id: mainFlickable
        anchors {
            top: page.header.bottom
            left: parent.left
            right: parent.right
            bottom: parent.bottom
        }
        contentWidth: width
        contentHeight: height
        boundsBehavior: Flickable.DragOverBounds
        flickableDirection: Flickable.VerticalFlick
        onMovementEnded: {
            if (contentY < -units.gu(6) && ajaxController.authenticated && !ajaxController.busy) {
                ajaxController.refreshStatus(false)
            }
        }

        Rectangle {
            width: parent.width
            height: units.gu(4)
            y: -height
            color: "transparent"

            Label {
                anchors.centerIn: parent
                text: ajaxController.busy ? i18n.tr("Refreshing") : i18n.tr("Pull to refresh")
                fontSize: "x-small"
                color: theme.palette.normal.backgroundSecondaryText
            }
        }

    ColumnLayout {
        id: remoteColumn
        width: mainFlickable.width - units.gu(6)
        height: mainFlickable.height - units.gu(6)
        x: units.gu(3)
        y: units.gu(3)
        spacing: units.gu(2)

        Rectangle {
            id: statusPanel
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: units.gu(18)
            radius: units.gu(0.8)
            color: statusColor()
            opacity: ajaxController.panicSending && panicFlash.dim ? 0.58 : 1

            property bool textOnDark: ajaxController.alarmMode !== "Unknown"

            Timer {
                id: panicFlash
                property bool dim: false
                interval: 420
                repeat: true
                running: ajaxController.panicSending
                onTriggered: dim = !dim
                onRunningChanged: {
                    if (!running) {
                        dim = false
                    }
                }
            }

            Column {
                anchors.centerIn: parent
                width: parent.width - units.gu(4)
                spacing: units.gu(1.5)

                Icon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: units.gu(10)
                    height: width
                    name: statusIconName()
                    color: statusPanel.textOnDark ? "white" : theme.palette.normal.backgroundText
                }

                Label {
                    width: parent.width
                    text: statusTitle()
                    horizontalAlignment: Text.AlignHCenter
                    fontSize: "x-large"
                    font.bold: true
                    color: statusPanel.textOnDark ? "white" : theme.palette.normal.backgroundText
                    elide: Text.ElideRight
                }

                Label {
                    width: parent.width
                    text: ajaxController.authenticated
                          ? ajaxController.selectedSystemName
                          : ajaxController.connectionState
                    horizontalAlignment: Text.AlignHCenter
                    fontSize: "small"
                    color: statusPanel.textOnDark ? "#e5e7eb" : theme.palette.normal.backgroundSecondaryText
                    elide: Text.ElideRight
                }

                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: connectionLabel.width + units.gu(2)
                    height: units.gu(3)
                    radius: units.gu(1.5)
                    color: ajaxController.authenticated ? "#16a34a"
                           : "transparent"
                    border.width: ajaxController.authenticated ? 0 : units.dp(1)
                    border.color: statusPanel.textOnDark ? "#e5e7eb" : theme.palette.normal.backgroundSecondaryText

                    Label {
                        id: connectionLabel
                        anchors.centerIn: parent
                        text: ajaxController.authenticated ? i18n.tr("ONLINE")
                              : i18n.tr("OFFLINE")
                        fontSize: "x-small"
                        color: ajaxController.authenticated ? "white"
                               : statusPanel.textOnDark ? "#e5e7eb"
                               : theme.palette.normal.backgroundSecondaryText
                    }
                }

                Label {
                    width: parent.width
                    text: ajaxController.lastStatusRefresh.length > 0
                          ? i18n.tr("Updated") + " " + ajaxController.lastStatusRefresh
                          : ajaxController.lastStatus
                    horizontalAlignment: Text.AlignHCenter
                    fontSize: "x-small"
                    color: statusPanel.textOnDark ? "#e5e7eb" : theme.palette.normal.backgroundSecondaryText
                    elide: Text.ElideRight
                }
            }
        }

        Rectangle {
            property bool selected: ajaxController.alarmMode === "Armed"

            Layout.fillWidth: true
            Layout.preferredHeight: units.gu(8.4)
            radius: units.gu(0.8)
            color: "transparent"
            border.width: units.dp(2)
            border.color: selected ? "#b91c1c" : theme.palette.normal.backgroundSecondaryText
            opacity: enabled ? 1 : 0.55
            enabled: !ajaxController.commandBusy

            Rectangle {
                anchors {
                    left: parent.left
                    top: parent.top
                    bottom: parent.bottom
                }
                width: units.gu(1)
                radius: parent.radius
                color: "#b91c1c"
                opacity: parent.selected ? 1 : 0
            }

            Label {
                anchors.centerIn: parent
                text: i18n.tr("Arm")
                fontSize: "x-large"
                font.bold: true
                color: modeButtonTextColor("Armed")
            }

            MouseArea {
                anchors.fill: parent
                onClicked: ajaxController.arm()
            }
        }

        Rectangle {
            property bool selected: ajaxController.alarmMode === "Disarmed"

            Layout.fillWidth: true
            Layout.preferredHeight: units.gu(8.4)
            radius: units.gu(0.8)
            color: "transparent"
            border.width: units.dp(2)
            border.color: selected ? "#15803d" : theme.palette.normal.backgroundSecondaryText
            opacity: enabled ? 1 : 0.55
            enabled: !ajaxController.commandBusy

            Rectangle {
                anchors {
                    left: parent.left
                    top: parent.top
                    bottom: parent.bottom
                }
                width: units.gu(1)
                radius: parent.radius
                color: "#15803d"
                opacity: parent.selected ? 1 : 0
            }

            Label {
                anchors.centerIn: parent
                text: i18n.tr("Disarm")
                fontSize: "x-large"
                font.bold: true
                color: modeButtonTextColor("Disarmed")
            }

            MouseArea {
                anchors.fill: parent
                onClicked: ajaxController.disarm()
            }
        }

        Rectangle {
            property bool selected: ajaxController.alarmMode === "Night mode"

            Layout.fillWidth: true
            Layout.preferredHeight: units.gu(8.4)
            radius: units.gu(0.8)
            color: "transparent"
            border.width: units.dp(2)
            border.color: selected ? "#4f46e5" : theme.palette.normal.backgroundSecondaryText
            opacity: enabled ? 1 : 0.55
            enabled: !ajaxController.commandBusy

            Rectangle {
                anchors {
                    left: parent.left
                    top: parent.top
                    bottom: parent.bottom
                }
                width: units.gu(1)
                radius: parent.radius
                color: "#4f46e5"
                opacity: parent.selected ? 1 : 0
            }

            Label {
                anchors.centerIn: parent
                text: i18n.tr("Night")
                fontSize: "x-large"
                font.bold: true
                color: modeButtonTextColor("Night mode")
            }

            MouseArea {
                anchors.fill: parent
                onClicked: ajaxController.enableNightMode()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: units.gu(2.2)
        }

        Rectangle {
            id: panicButton
            Layout.fillWidth: true
            Layout.preferredHeight: units.gu(8.4)
            radius: units.gu(0.8)
            color: "#dc2626"
            opacity: enabled ? 1 : 0.55
            enabled: !ajaxController.commandBusy

            Column {
                anchors.centerIn: parent
                spacing: units.gu(0.2)

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: panicHold.running ? i18n.tr("Keep holding") : i18n.tr("Panic")
                    fontSize: "x-large"
                    font.bold: true
                    color: "white"
                }

                Label {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: i18n.tr("hold")
                    fontSize: "small"
                    color: "#fee2e2"
                }
            }

            MouseArea {
                anchors.fill: parent
                onPressed: panicHold.restart()
                onReleased: panicHold.stop()
                onCanceled: panicHold.stop()
            }

            Timer {
                id: panicHold
                interval: 1500
                repeat: false
                onTriggered: panicOverlay.visible = true
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 0
        }

    }
    }

    Rectangle {
        id: spacesOverlay
        anchors.fill: parent
        z: 19
        visible: false
        color: theme.palette.normal.background

        PageHeader {
            id: spacesHeader
            title: i18n.tr("Spaces")
            trailingActionBar.actions: [
                Action {
                    text: i18n.tr("Close")
                    iconName: "close"
                    onTriggered: spacesOverlay.visible = false
                }
            ]
        }

        ListView {
            anchors {
                top: spacesHeader.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            model: ajaxController.spaces
            delegate: ListItem {
                height: units.gu(8)

                Rectangle {
                    anchors {
                        fill: parent
                        leftMargin: units.gu(1)
                        rightMargin: units.gu(1)
                    }
                    color: modelData.id === ajaxController.selectedSpaceId ? theme.palette.normal.base : "transparent"
                }

                Row {
                    anchors {
                        fill: parent
                        leftMargin: units.gu(2)
                        rightMargin: units.gu(2)
                    }
                    spacing: units.gu(1)

                    Icon {
                        width: units.gu(3)
                        height: width
                        anchors.verticalCenter: parent.verticalCenter
                        name: modelData.mode === "Disarmed" ? "security-low" : "security-high"
                        color: modelData.mode === "Armed" ? "#b91c1c"
                               : modelData.mode === "Disarmed" ? "#15803d"
                               : modelData.mode === "Night mode" ? "#4f46e5"
                               : theme.palette.normal.backgroundSecondaryText
                    }

                    Column {
                        width: parent.width - units.gu(4) - parent.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: units.gu(0.2)

                        Label {
                            width: parent.width
                            text: modelData.name
                            elide: Text.ElideRight
                        }

                        Label {
                            width: parent.width
                            text: ajaxController.modeLabel(modelData.mode)
                            fontSize: "small"
                            color: theme.palette.normal.backgroundSecondaryText
                            elide: Text.ElideRight
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onClicked: {
                        ajaxController.selectSpace(modelData.id, modelData.name, modelData.mode, true)
                        spacesOverlay.visible = false
                    }
                }
            }
        }
    }

    Rectangle {
        id: messagesOverlay
        anchors.fill: parent
        z: 20
        visible: false
        color: theme.palette.normal.background

        PageHeader {
            id: messagesHeader
            title: i18n.tr("Messages")
            trailingActionBar.actions: [
                Action {
                    text: i18n.tr("Close")
                    iconName: "close"
                    onTriggered: messagesOverlay.visible = false
                }
            ]
        }

        ListView {
            anchors {
                top: messagesHeader.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            model: ajaxController.messages
            delegate: ListItem {
                height: messageColumn.height + units.gu(2)

                Column {
                    id: messageColumn
                    anchors {
                        left: parent.left
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                        margins: units.gu(1.5)
                    }
                    spacing: units.gu(0.4)

                    Label {
                        width: parent.width
                        text: modelData.time
                        fontSize: "small"
                        color: theme.palette.normal.backgroundSecondaryText
                        wrapMode: Text.WordWrap
                    }

                    Label {
                        width: parent.width
                        text: modelData.text
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }

    Rectangle {
        id: loginOverlay
        anchors.fill: parent
        z: 18
        visible: false
        color: theme.palette.normal.background

        PageHeader {
            id: loginHeader
            title: i18n.tr("QR login")
            trailingActionBar.actions: [
                Action {
                    text: i18n.tr("Close")
                    iconName: "close"
                    onTriggered: loginOverlay.visible = false
                }
            ]
        }

        Flickable {
            anchors {
                top: loginHeader.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            contentHeight: loginContent.height + units.gu(4.8)

        Column {
            id: loginContent
            width: parent.width - units.gu(4.8)
            anchors {
                top: parent.top
                left: parent.left
                margins: units.gu(2.4)
            }
            spacing: units.gu(2)

            Label {
                width: parent.width
                text: i18n.tr("QR session")
                fontSize: "small"
                font.bold: true
                color: theme.palette.normal.backgroundSecondaryText
            }

            Rectangle {
                id: qrPanel
                width: Math.min(parent.width, units.gu(34))
                height: width
                anchors.horizontalCenter: parent.horizontalCenter
                color: "white"
                border.width: units.dp(1)
                border.color: "#111111"
                property var qrMatrix: QrCode.matrix(ajaxController.qrPairingPayload)

                onQrMatrixChanged: qrCanvas.requestPaint()

                Canvas {
                    id: qrCanvas
                    anchors.fill: parent
                    anchors.margins: units.gu(1.4)
                    visible: qrPanel.qrMatrix.size > 0

                    onPaint: {
                        var ctx = getContext("2d")
                        ctx.fillStyle = "#ffffff"
                        ctx.fillRect(0, 0, width, height)

                        var matrix = qrPanel.qrMatrix
                        if (!matrix || matrix.size === 0) {
                            return
                        }

                        var quietModules = 4
                        var totalModules = matrix.size + quietModules * 2
                        var moduleSize = Math.floor(Math.min(width, height) / totalModules)
                        var qrSize = moduleSize * matrix.size
                        var offsetX = Math.floor((width - qrSize) / 2)
                        var offsetY = Math.floor((height - qrSize) / 2)

                        ctx.fillStyle = "#111111"
                        for (var row = 0; row < matrix.size; row++) {
                            for (var column = 0; column < matrix.size; column++) {
                                if (matrix.modules[row][column]) {
                                    ctx.fillRect(offsetX + column * moduleSize,
                                                 offsetY + row * moduleSize,
                                                 moduleSize,
                                                 moduleSize)
                                }
                            }
                        }
                    }
                }

                ActivityIndicator {
                    anchors.centerIn: parent
                    running: ajaxController.qrPairingPayload.length === 0 && ajaxController.qrClient.running
                    visible: running
                }

                Label {
                    anchors.centerIn: parent
                    width: parent.width - units.gu(3)
                    visible: ajaxController.qrPairingPayload.length === 0 && !ajaxController.qrClient.running
                    horizontalAlignment: Text.AlignHCenter
                    text: i18n.tr("No active QR")
                    color: "#111111"
                    wrapMode: Text.WordWrap
                }
            }

            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: ajaxController.qrLoginState
                fontSize: "large"
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Label {
                width: parent.width
                horizontalAlignment: Text.AlignHCenter
                text: i18n.tr("Scan this QR from an already logged-in Ajax app. The approved session is saved in protected local storage until you clear it or Ajax revokes it.")
                color: theme.palette.normal.backgroundSecondaryText
                wrapMode: Text.WordWrap
            }

            Button {
                width: parent.width
                height: units.gu(7)
                text: ajaxController.qrClient.running ? i18n.tr("Restart QR login") : i18n.tr("Start QR login")
                onClicked: ajaxController.pollQrLogin()
            }

            Rectangle {
                width: parent.width
                height: units.dp(1)
                color: theme.palette.normal.base
            }

            Label {
                width: parent.width
                text: i18n.tr("Developer session")
                fontSize: "small"
                font.bold: true
                color: theme.palette.normal.backgroundSecondaryText
            }

            TextField {
                id: serverUrlField
                width: parent.width
                placeholderText: i18n.tr("API base URL")
                text: ajaxController.sessionSettings.apiBaseUrl
            }

            TextField {
                id: systemNameField
                width: parent.width
                placeholderText: i18n.tr("System name")
                text: ajaxController.sessionSettings.systemName
            }

            TextField {
                id: tokenField
                width: parent.width
                placeholderText: i18n.tr("Opaque session/token")
                echoMode: TextInput.Password
            }

            Row {
                width: parent.width
                spacing: units.gu(1)

                CheckBox {
                    id: rememberSessionCheck
                    checked: true
                }

                Label {
                    width: parent.width - rememberSessionCheck.width - parent.spacing
                    anchors.verticalCenter: rememberSessionCheck.verticalCenter
                    text: i18n.tr("Remember token in protected local storage")
                    color: theme.palette.normal.backgroundSecondaryText
                    wrapMode: Text.WordWrap
                }
            }

            Label {
                width: parent.width
                text: i18n.tr("Protected local storage is used for development sessions. Do not paste production secrets unless you accept that local-device risk.")
                fontSize: "x-small"
                color: LomiriColors.orange
                wrapMode: Text.WordWrap
            }

            Button {
                width: parent.width
                height: units.gu(7)
                text: i18n.tr("Login with session token")
                onClicked: {
                    if (ajaxController.manualSessionLogin(serverUrlField.text, tokenField.text, systemNameField.text, rememberSessionCheck.checked)) {
                        tokenField.text = ""
                        loginOverlay.visible = false
                    }
                }
            }

            Button {
                width: parent.width
                height: units.gu(7)
                text: i18n.tr("Restore saved session")
                enabled: ajaxController.hasStoredSession
                onClicked: {
                    ajaxController.restoreSession()
                    loginOverlay.visible = false
                }
            }

            Button {
                width: parent.width
                height: units.gu(7)
                text: i18n.tr("Clear saved session")
                enabled: ajaxController.hasStoredSession
                onClicked: ajaxController.clearStoredSession()
            }

            Button {
                width: parent.width
                height: units.gu(7)
                text: i18n.tr("Logout current session")
                enabled: ajaxController.authenticated
                onClicked: ajaxController.logout()
            }

        }
        }
    }

    Rectangle {
        id: panicOverlay
        anchors.fill: parent
        z: 30
        visible: false
        color: "#cc000000"

        MouseArea {
            anchors.fill: parent
        }

        Rectangle {
            width: Math.min(parent.width - units.gu(4), units.gu(42))
            height: panicColumn.height + units.gu(4)
            anchors.centerIn: parent
            color: theme.palette.normal.background
            radius: units.gu(0.7)

            Column {
                id: panicColumn
                anchors {
                    left: parent.left
                    right: parent.right
                    top: parent.top
                    margins: units.gu(2)
                }
                spacing: units.gu(1.5)

                Label {
                    width: parent.width
                    text: i18n.tr("Trigger panic alarm?")
                    fontSize: "large"
                    font.bold: true
                    wrapMode: Text.WordWrap
                }

                Label {
                    width: parent.width
                    text: i18n.tr("This action is deliberately guarded. On a connected Ajax session it sends a live panic alarm.")
                    color: theme.palette.normal.backgroundSecondaryText
                    wrapMode: Text.WordWrap
                }

                Button {
                    width: parent.width
                    height: units.gu(7)
                    text: i18n.tr("Trigger panic")
                    color: LomiriColors.red
                    onClicked: {
                        ajaxController.triggerPanic()
                        panicOverlay.visible = false
                    }
                }

                Button {
                    width: parent.width
                    height: units.gu(7)
                    text: i18n.tr("Cancel")
                    onClicked: panicOverlay.visible = false
                }
            }
        }
    }
}
