import QtQuick 2.7
import Lomiri.Components 1.3
import QtQuick.Layouts 1.3
import QtMultimedia 5.0
import "../backend/QrCode.js" as QrCode

Page {
    id: page
    property var ajaxController
    property var selectedDevice: null
    property bool deviceEditMode: false
    property string editDeviceName: ""
    property string editRoomName: ""
    property string editSensitivity: "Low"
    property string editOperatingMode: "Immediately"
    property string pickerMode: ""
    property string infoDialogTitle: ""
    property string infoDialogText: ""

    function showInfoDialog(title, text) {
        infoDialogTitle = title
        infoDialogText = text
        infoDialog.visible = true
    }

    function currentDevice() {
        return selectedDevice && selectedDevice.id ? ajaxController.deviceById(selectedDevice.id, selectedDevice) : selectedDevice
    }

    function deviceValue(key) {
        var device = currentDevice()
        if (!device || device[key] === undefined || device[key] === null) {
            return ""
        }
        return String(device[key])
    }

    function hasDeviceValue(key) {
        return deviceValue(key).length > 0
    }

    function deviceBool(key) {
        var value = deviceValue(key).toLowerCase()
        return value === "yes" || value === "true" || value === "enabled" || value === "active"
    }

    function deviceTruthy(device, key) {
        if (!device || device[key] === undefined || device[key] === null) {
            return false
        }
        var value = String(device[key]).toLowerCase()
        return value === "yes" || value === "true" || value === "enabled" || value === "active"
    }

    function startDeviceEdit() {
        var device = currentDevice()
        ajaxController.fetchDeviceDetails(device)
        editDeviceName = device && device.name ? device.name : ""
        editRoomName = device && device.roomName ? device.roomName : ""
        editSensitivity = device && device.sensitivity ? device.sensitivity : "Low"
        editOperatingMode = device && device.operatingMode ? device.operatingMode : "Immediately"
        deviceEditMode = true
    }

    function modeButtonTextColor(mode) {
        return theme.palette.normal.backgroundText
    }

    function statusColor() {
        if (ajaxController.liveAlarmActive) {
            return "#dc2626"
        }
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
        if (ajaxController.liveAlarmActive) {
            return ajaxController.liveAlarmText && ajaxController.liveAlarmText.length > 0
                   ? ajaxController.liveAlarmText
                   : i18n.tr("Alarm")
        }
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
        if (ajaxController.liveAlarmActive) {
            return "panic"
        }
        if (ajaxController.panicSending) {
            return "panic"
        }
        if (ajaxController.alarmMode === "Disarmed") {
            return "disarmed"
        }
        if (ajaxController.alarmMode === "Night mode") {
            return "night"
        }
        return "armed"
    }

    function modeIconName(mode) {
        if (mode === "Disarmed") {
            return "disarmed"
        }
        if (mode === "Night mode") {
            return "night"
        }
        if (mode === "Panic") {
            return "panic"
        }
        return "armed"
    }

    function deviceImageSource(device) {
        if (!device) {
            return ""
        }
        var fields = [ "deviceImage", "imageUrl", "imageUri", "photoUrl", "pictureUrl" ]
        for (var i = 0; i < fields.length; i++) {
            var value = device[fields[i]]
            if (value !== undefined && value !== null && String(value).length > 0) {
                return String(value)
            }
        }
        return ""
    }

    function signalLevel(device) {
        if (device && device.signalBars !== undefined && device.signalBars !== null) {
            var bars = parseInt(String(device.signalBars))
            if (!isNaN(bars)) {
                return Math.max(0, Math.min(4, bars))
            }
        }
        var raw = device && device.signalStrength !== undefined ? String(device.signalStrength).toLowerCase() : ""
        if (raw.length === 0 || raw.indexOf("unknown") >= 0 || raw.indexOf("unspecified") >= 0) {
            return -1
        }
        if (raw.indexOf("excellent") >= 0 || raw.indexOf("perfect") >= 0
                || raw.indexOf("high") >= 0 || raw.indexOf("strong") >= 0
                || raw.indexOf("stark") >= 0 || raw === "4") {
            return 4
        }
        if (raw.indexOf("good") >= 0 || raw.indexOf("normal") >= 0
                || raw.indexOf("ok") >= 0 || raw === "3") {
            return 3
        }
        if (raw.indexOf("low") >= 0 || raw.indexOf("weak") >= 0
                || raw.indexOf("låg") >= 0 || raw === "2") {
            return 2
        }
        if (raw.indexOf("bad") >= 0 || raw.indexOf("poor") >= 0
                || raw.indexOf("none") >= 0 || raw.indexOf("no signal") >= 0
                || raw === "1") {
            return 1
        }
        if (raw.indexOf("offline") >= 0 || raw.indexOf("disconnect") >= 0 || raw === "0") {
            return 0
        }
        return raw.length > 0 ? 3 : -1
    }

    function signalActiveColor() {
        return theme.palette.normal.backgroundText
    }

    function alarmSoundSource() {
        if (!ajaxController.liveAlarmActive) {
            return ""
        }
        var base = "file://" + ajaxRemoteInstallDir + "/assets/sounds/"
        if (ajaxController.liveAlarmType === "panic") {
            return base + "panic.wav"
        }
        if (ajaxController.liveAlarmType === "fire") {
            return base + "fire.wav"
        }
        return base + "intrusion.wav"
    }

    SoundEffect {
        id: alarmAudio
        source: alarmSoundSource()
        loops: SoundEffect.Infinite
        volume: 1.0
        onStatusChanged: console.log("AjaxRemote alarm audio status:", status, "source:", source)
    }

    Connections {
        target: ajaxController
        onLiveAlarmActiveChanged: {
            if (ajaxController.liveAlarmActive) {
                alarmAudio.source = alarmSoundSource()
                alarmAudio.play()
            } else {
                alarmAudio.stop()
            }
        }
        onLiveAlarmTypeChanged: {
            if (ajaxController.liveAlarmActive) {
                alarmAudio.stop()
                alarmAudio.source = alarmSoundSource()
                alarmAudio.play()
            }
        }
    }

    function batteryPercent(device) {
        if (!device || device.batteryLevel === undefined || device.batteryLevel === null) {
            return -1
        }
        var number = parseInt(String(device.batteryLevel).replace(/[^0-9]/g, ""))
        return isNaN(number) ? -1 : Math.max(0, Math.min(100, number))
    }

    header: PageHeader {
        contents: Item {
            anchors.fill: parent

            Row {
                id: headerLeftActions
                anchors {
                    left: parent.left
                    leftMargin: units.gu(0.6)
                    verticalCenter: parent.verticalCenter
                }
                spacing: units.gu(0.2)

                Item {
                    width: units.gu(4.4)
                    height: units.gu(4.4)

                    Icon {
                        anchors.centerIn: parent
                        width: units.gu(2.4)
                        height: width
                        name: "home"
                        color: roomsMouse.pressed ? theme.palette.normal.activity
                              : theme.palette.normal.backgroundText
                    }

                    MouseArea {
                        id: roomsMouse
                        anchors.fill: parent
                        onClicked: roomsOverlay.visible = true
                    }
                }

                Item {
                    width: units.gu(4.4)
                    height: units.gu(4.4)

                    Icon {
                        anchors.centerIn: parent
                        width: units.gu(2.4)
                        height: width
                        name: "preferences-system"
                        color: devicesMouse.pressed ? theme.palette.normal.activity
                              : theme.palette.normal.backgroundText
                    }

                    MouseArea {
                        id: devicesMouse
                        anchors.fill: parent
                        onClicked: devicesOverlay.visible = true
                    }
                }
            }

            Row {
                id: headerRightActions
                anchors {
                    right: parent.right
                    rightMargin: units.gu(0.6)
                    verticalCenter: parent.verticalCenter
                }
                spacing: units.gu(0.2)

                Item {
                    width: units.gu(4.4)
                    height: units.gu(4.4)

                    Icon {
                        anchors.centerIn: parent
                        width: units.gu(2.4)
                        height: width
                        name: "message"
                        color: messagesMouse.pressed ? theme.palette.normal.activity
                              : theme.palette.normal.backgroundText
                    }

                    MouseArea {
                        id: messagesMouse
                        anchors.fill: parent
                        onClicked: {
                            ajaxController.fetchMessages()
                            messagesOverlay.visible = true
                        }
                    }
                }

                Item {
                    width: units.gu(4.4)
                    height: units.gu(4.4)

                    Icon {
                        anchors.centerIn: parent
                        width: units.gu(2.4)
                        height: width
                        name: "view-grid-symbolic"
                        color: qrMouse.pressed ? theme.palette.normal.activity
                              : theme.palette.normal.backgroundText
                    }

                    MouseArea {
                        id: qrMouse
                        anchors.fill: parent
                        onClicked: {
                            ajaxController.startQrLogin()
                            loginOverlay.visible = true
                        }
                    }
                }
            }

            Rectangle {
                anchors {
                    verticalCenter: parent.verticalCenter
                    horizontalCenter: parent.horizontalCenter
                }
                width: Math.max(units.gu(8),
                                Math.min(parent.width - (2 * Math.max(headerLeftActions.width, headerRightActions.width)) - units.gu(2),
                                         spaceTitle.implicitWidth + units.gu(2)))
                height: units.gu(4.6)
                radius: units.gu(0.7)
                color: spaceMouse.pressed ? theme.palette.normal.base : "transparent"
                border.width: 0

                Label {
                    id: spaceTitle
                    anchors {
                        left: parent.left
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                        margins: units.gu(1)
                    }
                    text: ajaxController.selectedSystemName.length > 0
                          ? ajaxController.selectedSystemName
                          : i18n.tr("Alarm Remote")
                    horizontalAlignment: Text.AlignHCenter
                    elide: Text.ElideRight
                }

                MouseArea {
                    id: spaceMouse
                    anchors.fill: parent
                    enabled: ajaxController.spaces.length > 0
                    onClicked: spacesOverlay.visible = true
                }
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: theme.palette.normal.background
    }

    Flickable {
        id: mainFlickable
        property real refreshThreshold: units.gu(6)
        property bool refreshReady: contentY < -refreshThreshold && ajaxController.authenticated && !ajaxController.busy
        function triggerPullRefresh() {
            if (contentY < -refreshThreshold && ajaxController.authenticated && !ajaxController.busy) {
                ajaxController.refreshStatus(false)
            }
        }
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
        onDragEnded: triggerPullRefresh()
        onMovementEnded: triggerPullRefresh()

        Rectangle {
            width: parent.width
            height: units.gu(4)
            y: -height
            color: "transparent"

            Label {
                anchors.centerIn: parent
                text: ajaxController.busy ? i18n.tr("Refreshing")
                      : mainFlickable.refreshReady ? i18n.tr("Release to refresh")
                      : i18n.tr("Pull to refresh")
                fontSize: "x-small"
                color: theme.palette.normal.backgroundSecondaryText
            }
        }

    ColumnLayout {
        id: remoteColumn
        width: mainFlickable.width - units.gu(4.8)
        height: mainFlickable.height - units.gu(3.6)
        x: units.gu(2.4)
        y: units.gu(2.4)
        spacing: units.gu(1.6)

        Rectangle {
            id: statusPanel
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.minimumHeight: units.gu(22)
            radius: units.gu(0.8)
            color: ajaxController.liveAlarmActive && panicFlash.dim ? "#ffffff" : statusColor()
            opacity: ajaxController.panicSending && !ajaxController.liveAlarmActive && panicFlash.dim ? 0.58 : 1
            clip: false

            property bool textOnDark: ajaxController.alarmMode !== "Unknown"
                                      && !(ajaxController.liveAlarmActive && panicFlash.dim)

            Timer {
                id: panicFlash
                property bool dim: false
                interval: ajaxController.liveAlarmActive ? 120 : 420
                repeat: true
                running: ajaxController.panicSending || ajaxController.liveAlarmActive
                onTriggered: dim = !dim
                onRunningChanged: {
                    if (!running) {
                        dim = false
                    }
                }
            }

            Canvas {
                id: statusGlyph
                width: units.gu(12)
                height: width
                anchors.horizontalCenter: parent.horizontalCenter
                y: -units.gu(2.2)
                property string mode: statusIconName()
                property color glyphColor: statusPanel.textOnDark ? "white" : theme.palette.normal.backgroundText

                onModeChanged: requestPaint()
                onGlyphColorChanged: requestPaint()
                onPaint: {
                    var ctx = getContext("2d")
                    ctx.clearRect(0, 0, width, height)

                    var cx = width / 2
                    var cy = height / 2
                    var r = Math.min(width, height) * 0.34
                    ctx.strokeStyle = glyphColor
                    ctx.fillStyle = glyphColor
                    ctx.lineWidth = Math.max(units.dp(4), width * 0.055)
                    ctx.lineCap = "round"
                    ctx.lineJoin = "round"

                    if (mode === "night") {
                        ctx.beginPath()
                        ctx.arc(cx + r * 0.02, cy - r * 0.02, r * 0.98, -1.4, 1.98, false)
                        ctx.arc(cx - r * 0.32, cy - r * 0.28, r * 0.84, 1.85, -1.25, true)
                        ctx.closePath()
                        ctx.fill()
                        return
                    }

                    if (mode === "disarmed") {
                        ctx.beginPath()
                        ctx.rect(cx - r * 0.78, cy - r * 0.04, r * 1.56, r * 1.08)
                        ctx.stroke()

                        ctx.beginPath()
                        ctx.moveTo(cx - r * 0.46, cy - r * 0.06)
                        ctx.lineTo(cx - r * 0.46, cy - r * 0.48)
                        ctx.bezierCurveTo(cx - r * 0.46, cy - r * 1.12,
                                          cx + r * 0.64, cy - r * 1.12,
                                          cx + r * 0.64, cy - r * 0.42)
                        ctx.stroke()
                        return
                    }

                    if (mode === "panic") {
                        ctx.beginPath()
                        ctx.moveTo(cx, cy - r * 1.08)
                        ctx.lineTo(cx + r * 0.98, cy + r * 0.82)
                        ctx.lineTo(cx - r * 0.98, cy + r * 0.82)
                        ctx.closePath()
                        ctx.stroke()

                        ctx.beginPath()
                        ctx.moveTo(cx, cy - r * 0.46)
                        ctx.lineTo(cx, cy + r * 0.2)
                        ctx.stroke()

                        ctx.beginPath()
                        ctx.arc(cx, cy + r * 0.5, ctx.lineWidth * 0.55, 0, Math.PI * 2)
                        ctx.fill()
                        return
                    }

                    ctx.beginPath()
                    ctx.rect(cx - r * 0.78, cy - r * 0.04, r * 1.56, r * 1.08)
                    ctx.stroke()

                    ctx.beginPath()
                    ctx.moveTo(cx - r * 0.48, cy - r * 0.06)
                    ctx.lineTo(cx - r * 0.48, cy - r * 0.48)
                    ctx.bezierCurveTo(cx - r * 0.48, cy - r * 1.1,
                                      cx + r * 0.48, cy - r * 1.1,
                                      cx + r * 0.48, cy - r * 0.48)
                    ctx.lineTo(cx + r * 0.48, cy - r * 0.06)
                    ctx.stroke()
                }
            }

            Text {
                id: statusTitleLabel
                anchors.centerIn: parent
                width: parent.width - units.gu(4)
                height: ajaxController.liveAlarmActive ? units.gu(7.2) : units.gu(5.2)
                text: statusTitle()
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: ajaxController.liveAlarmActive ? units.gu(2.3) : units.gu(3.6)
                font.bold: true
                color: statusPanel.textOnDark ? "white" : theme.palette.normal.backgroundText
                wrapMode: ajaxController.liveAlarmActive ? Text.WordWrap : Text.NoWrap
                maximumLineCount: ajaxController.liveAlarmActive ? 3 : 1
                elide: ajaxController.liveAlarmActive ? Text.ElideNone : Text.ElideRight
            }

            Item {
                id: statusMetaArea
                anchors {
                    left: parent.left
                    right: parent.right
                    top: statusTitleLabel.bottom
                    bottom: connectionBadge.top
                    topMargin: units.gu(0.4)
                    bottomMargin: units.gu(0.6)
                    leftMargin: units.gu(2)
                    rightMargin: units.gu(2)
                }

                Column {
                    anchors {
                        left: parent.left
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: units.gu(0.6)

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
                id: connectionBadge
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: -height / 2
                width: connectionLabel.width + units.gu(2.4)
                height: units.gu(3.2)
                radius: height / 2
                color: ajaxController.authenticated ? "#16a34a" : theme.palette.normal.background
                border.width: ajaxController.authenticated ? 0 : units.dp(1)
                border.color: statusPanel.textOnDark ? "#e5e7eb" : theme.palette.normal.backgroundSecondaryText

                Label {
                    id: connectionLabel
                    anchors.centerIn: parent
                    text: ajaxController.authenticated ? i18n.tr("ONLINE")
                          : i18n.tr("OFFLINE")
                    fontSize: "x-small"
                    font.bold: true
                    color: ajaxController.authenticated ? "white"
                           : statusPanel.textOnDark ? "#e5e7eb"
                           : theme.palette.normal.backgroundSecondaryText
                }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: units.gu(1)
        }

        Rectangle {
            property bool selected: ajaxController.alarmMode === "Armed"

            Layout.fillWidth: true
            Layout.preferredHeight: units.gu(7.7)
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

            Text {
                anchors.centerIn: parent
                width: parent.width
                height: parent.height
                text: i18n.tr("Arm")
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: units.gu(3.6)
                font.bold: true
                color: modeButtonTextColor("Armed")
                elide: Text.ElideRight
            }

            MouseArea {
                anchors.fill: parent
                onClicked: ajaxController.arm()
            }
        }

        Rectangle {
            property bool selected: ajaxController.alarmMode === "Disarmed"

            Layout.fillWidth: true
            Layout.preferredHeight: units.gu(7.7)
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

            Text {
                anchors.centerIn: parent
                width: parent.width
                height: parent.height
                text: i18n.tr("Disarm")
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: units.gu(3.6)
                font.bold: true
                color: modeButtonTextColor("Disarmed")
                elide: Text.ElideRight
            }

            MouseArea {
                anchors.fill: parent
                onClicked: ajaxController.disarm()
            }
        }

        Rectangle {
            property bool selected: ajaxController.alarmMode === "Night mode"

            Layout.fillWidth: true
            Layout.preferredHeight: units.gu(7.7)
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

            Text {
                anchors.centerIn: parent
                width: parent.width
                height: parent.height
                text: i18n.tr("Night")
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: units.gu(3.6)
                font.bold: true
                color: modeButtonTextColor("Night mode")
                elide: Text.ElideRight
            }

            MouseArea {
                anchors.fill: parent
                onClicked: ajaxController.enableNightMode()
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 0
        }

        Rectangle {
            id: panicButton
            Layout.fillWidth: true
            Layout.preferredHeight: units.gu(7.7)
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
            Layout.preferredHeight: units.gu(0.4)
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
                    color: "transparent"

                    Rectangle {
                        anchors {
                            left: parent.left
                            top: parent.top
                            bottom: parent.bottom
                        }
                        width: units.dp(3)
                        visible: modelData.id === ajaxController.selectedSpaceId
                        color: modelData.mode === "Armed" ? "#b91c1c"
                               : modelData.mode === "Disarmed" ? "#15803d"
                               : modelData.mode === "Night mode" ? "#4f46e5"
                               : theme.palette.normal.activity
                    }
                }

                Row {
                    anchors {
                        fill: parent
                        leftMargin: units.gu(2)
                        rightMargin: units.gu(2)
                    }
                    spacing: units.gu(1)

                    Canvas {
                        width: units.gu(3.4)
                        height: width
                        anchors.verticalCenter: parent.verticalCenter
                        property string mode: modeIconName(modelData.mode)
                        property color glyphColor: modelData.mode === "Armed" ? "#b91c1c"
                                                   : modelData.mode === "Disarmed" ? "#15803d"
                                                   : modelData.mode === "Night mode" ? "#4f46e5"
                                                   : theme.palette.normal.backgroundSecondaryText
                        onModeChanged: requestPaint()
                        onGlyphColorChanged: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            var cx = width / 2
                            var cy = height / 2
                            var r = Math.min(width, height) * 0.32
                            ctx.strokeStyle = glyphColor
                            ctx.fillStyle = glyphColor
                            ctx.lineWidth = Math.max(units.dp(2), width * 0.07)
                            ctx.lineCap = "round"
                            ctx.lineJoin = "round"
                            if (mode === "night") {
                                ctx.beginPath()
                                ctx.arc(cx + r * 0.02, cy - r * 0.02, r * 0.98, -1.4, 1.98, false)
                                ctx.arc(cx - r * 0.32, cy - r * 0.28, r * 0.84, 1.85, -1.25, true)
                                ctx.closePath()
                                ctx.fill()
                                return
                            }
                            ctx.beginPath()
                            ctx.rect(cx - r * 0.78, cy - r * 0.04, r * 1.56, r * 1.08)
                            ctx.stroke()
                            ctx.beginPath()
                            if (mode === "disarmed") {
                                ctx.moveTo(cx - r * 0.46, cy - r * 0.06)
                                ctx.lineTo(cx - r * 0.46, cy - r * 0.48)
                                ctx.bezierCurveTo(cx - r * 0.46, cy - r * 1.12,
                                                  cx + r * 0.64, cy - r * 1.12,
                                                  cx + r * 0.64, cy - r * 0.42)
                            } else {
                                ctx.moveTo(cx - r * 0.46, cy - r * 0.06)
                                ctx.lineTo(cx - r * 0.46, cy - r * 0.5)
                                ctx.bezierCurveTo(cx - r * 0.46, cy - r * 1.08,
                                                  cx + r * 0.46, cy - r * 1.08,
                                                  cx + r * 0.46, cy - r * 0.5)
                                ctx.lineTo(cx + r * 0.46, cy - r * 0.06)
                            }
                            ctx.stroke()
                        }
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
        id: roomsOverlay
        anchors.fill: parent
        z: 20
        visible: false
        color: theme.palette.normal.background

        PageHeader {
            id: roomsHeader
            title: i18n.tr("Rooms")
            trailingActionBar.actions: [
                Action {
                    text: i18n.tr("Close")
                    iconName: "close"
                    onTriggered: roomsOverlay.visible = false
                }
            ]
        }

        ListView {
            id: roomsList
            anchors {
                top: roomsHeader.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            model: ajaxController.rooms
            delegate: ListItem {
                height: units.gu(7)

                Row {
                    anchors {
                        fill: parent
                        leftMargin: units.gu(2)
                        rightMargin: units.gu(2)
                    }
                    spacing: units.gu(1.2)

                    Icon {
                        width: units.gu(3)
                        height: width
                        anchors.verticalCenter: parent.verticalCenter
                        name: "navigation-menu"
                        color: theme.palette.normal.backgroundSecondaryText
                    }

                    Column {
                        width: parent.width - units.gu(4.2)
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: units.gu(0.2)

                        Label {
                            width: parent.width
                            text: modelData.name || i18n.tr("Room")
                            elide: Text.ElideRight
                        }

                        Label {
                            width: parent.width
                            text: modelData.id
                            fontSize: "x-small"
                            color: theme.palette.normal.backgroundSecondaryText
                            elide: Text.ElideMiddle
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: roomsList.count === 0
                text: i18n.tr("No rooms loaded")
                color: theme.palette.normal.backgroundSecondaryText
            }
        }
    }

    Rectangle {
        id: devicesOverlay
        anchors.fill: parent
        z: 20
        visible: false
        color: theme.palette.normal.background

        PageHeader {
            id: devicesHeader
            title: i18n.tr("Devices")
            trailingActionBar.actions: [
                Action {
                    text: i18n.tr("Close")
                    iconName: "close"
                    onTriggered: devicesOverlay.visible = false
                }
            ]
        }

        ListView {
            id: devicesList
            anchors {
                top: devicesHeader.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            model: ajaxController.devices
            delegate: ListItem {
                id: deviceDelegate
                property bool pressed: false
                property var deviceData: modelData
                height: units.gu(9.6)

                Rectangle {
                    anchors {
                        fill: parent
                        leftMargin: units.gu(1)
                        rightMargin: units.gu(1)
                    }
                    color: deviceDelegate.pressed ? theme.palette.normal.base : "transparent"
                }

                Row {
                    anchors {
                        fill: parent
                        leftMargin: units.gu(2)
                        rightMargin: units.gu(2)
                    }
                    spacing: units.gu(1.2)

                    Item {
                        id: deviceImageBox
                        width: units.gu(5.8)
                        height: units.gu(5.8)
                        anchors.verticalCenter: parent.verticalCenter
                        property string imageSource: deviceImageSource(modelData)

                        Rectangle {
                            anchors.fill: parent
                            radius: units.gu(0.7)
                            color: "transparent"
                        }

                        Image {
                            id: deviceImage
                            anchors.fill: parent
                            anchors.margins: units.dp(2)
                            source: deviceImageBox.imageSource
                            visible: deviceImageBox.imageSource.length > 0 && status !== Image.Error
                            fillMode: Image.PreserveAspectFit
                        }

                        Canvas {
                            anchors.centerIn: parent
                            width: units.gu(4.2)
                            height: width
                            visible: deviceImageBox.imageSource.length === 0 || deviceImage.status === Image.Error
                            property bool hub: ajaxController.isHubDevice(modelData)
                            onHubChanged: requestPaint()
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                var cx = width / 2
                                var cy = height / 2
                                var r = Math.min(width, height) * 0.34
                                ctx.strokeStyle = theme.palette.normal.backgroundSecondaryText
                                ctx.fillStyle = theme.palette.normal.backgroundSecondaryText
                                ctx.lineWidth = Math.max(units.dp(2), width * 0.055)
                                ctx.lineCap = "round"
                                ctx.lineJoin = "round"

                                if (hub) {
                                    ctx.beginPath()
                                    ctx.rect(cx - r * 0.85, cy - r * 1.05, r * 1.7, r * 2.1)
                                    ctx.stroke()
                                    ctx.beginPath()
                                    ctx.arc(cx, cy - r * 0.35, r * 0.16, 0, Math.PI * 2)
                                    ctx.fill()
                                    ctx.beginPath()
                                    ctx.arc(cx, cy + r * 0.25, r * 0.16, 0, Math.PI * 2)
                                    ctx.fill()
                                    return
                                }

                                ctx.beginPath()
                                ctx.rect(cx - r * 0.72, cy - r * 0.88, r * 1.44, r * 1.76)
                                ctx.stroke()
                                ctx.beginPath()
                                ctx.arc(cx, cy, r * 0.34, 0, Math.PI * 2)
                                ctx.stroke()
                            }
                        }
                    }

                    Column {
                        width: parent.width - units.gu(7)
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: units.gu(0.2)

                        Label {
                            width: parent.width
                            text: modelData.name || i18n.tr("Ajax device")
                            elide: Text.ElideRight
                        }

                        Label {
                            width: parent.width
                            text: modelData.roomName && modelData.roomName.length > 0
                                  ? modelData.roomName
                                  : i18n.tr("No room")
                            fontSize: "small"
                            color: theme.palette.normal.backgroundSecondaryText
                            elide: Text.ElideRight
                        }

                        Row {
                            width: parent.width
                            height: units.gu(2)
                            spacing: units.gu(1.3)

                            Row {
                                visible: !ajaxController.isHubDevice(modelData)
                                height: parent.height
                                spacing: units.dp(2)
                                anchors.verticalCenter: parent.verticalCenter

                                Repeater {
                                    model: 4

                                    Rectangle {
                                        property int level: signalLevel(deviceDelegate.deviceData)
                                        property color activeColor: signalActiveColor()
                                        width: units.dp(3)
                                        height: units.dp(4 + (index * 3))
                                        anchors.bottom: parent.bottom
                                        radius: units.dp(1)
                                        color: level >= 0 && index < level ? activeColor : "transparent"
                                        opacity: 1
                                        border.width: units.dp(1)
                                        border.color: level >= 0 && index < level
                                                      ? activeColor
                                                      : theme.palette.normal.backgroundSecondaryText
                                    }
                                }
                            }

                            Item {
                                width: units.gu(3.2)
                                height: units.gu(1.6)
                                anchors.verticalCenter: parent.verticalCenter
                                property int battery: batteryPercent(modelData)

                                Rectangle {
                                    anchors {
                                        left: parent.left
                                        verticalCenter: parent.verticalCenter
                                    }
                                    width: units.gu(2.5)
                                    height: units.gu(1.2)
                                    radius: units.dp(2)
                                    border.width: units.dp(1)
                                    border.color: theme.palette.normal.backgroundSecondaryText
                                    color: "transparent"

                                    Rectangle {
                                        anchors {
                                            left: parent.left
                                            top: parent.top
                                            bottom: parent.bottom
                                            margins: units.dp(2)
                                        }
                                        width: parent.width <= units.dp(4) || batteryPercent(modelData) < 0
                                               ? 0
                                               : Math.max(units.dp(2), (parent.width - units.dp(4)) * batteryPercent(modelData) / 100)
                                        radius: units.dp(1)
                                        color: batteryPercent(modelData) >= 0 && batteryPercent(modelData) < 20
                                               ? "#dc2626"
                                               : theme.palette.normal.backgroundSecondaryText
                                    }
                                }

                                Rectangle {
                                    anchors {
                                        left: parent.left
                                        leftMargin: units.gu(2.55)
                                        verticalCenter: parent.verticalCenter
                                    }
                                    width: units.dp(2)
                                    height: units.gu(0.7)
                                    color: theme.palette.normal.backgroundSecondaryText
                                }
                            }

                            Canvas {
                                visible: !ajaxController.isHubDevice(modelData)
                                width: units.gu(2)
                                height: width
                                anchors.verticalCenter: parent.verticalCenter
                                opacity: deviceTruthy(modelData, "armedInNightMode") ? 1 : 0.28
                                onPaint: {
                                    var ctx = getContext("2d")
                                    ctx.clearRect(0, 0, width, height)
                                    var cx = width / 2
                                    var cy = height / 2
                                    var r = Math.min(width, height) * 0.33
                                    ctx.fillStyle = theme.palette.normal.backgroundSecondaryText
                                    ctx.beginPath()
                                    ctx.arc(cx + r * 0.02, cy - r * 0.02, r * 0.98, -1.4, 1.98, false)
                                    ctx.arc(cx - r * 0.32, cy - r * 0.28, r * 0.84, 1.85, -1.25, true)
                                    ctx.closePath()
                                    ctx.fill()
                                }
                            }
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    onPressed: deviceDelegate.pressed = true
                    onReleased: deviceDelegate.pressed = false
                    onCanceled: deviceDelegate.pressed = false
                    onClicked: {
                        page.selectedDevice = ajaxController.deviceById(modelData.id, modelData)
                        ajaxController.fetchDeviceDetails(page.selectedDevice)
                        deviceDetailsOverlay.visible = true
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: devicesList.count === 0
                text: i18n.tr("No devices loaded")
                color: theme.palette.normal.backgroundSecondaryText
            }
        }
    }

    Rectangle {
        id: deviceDetailsOverlay
        anchors.fill: parent
        z: 21
        visible: false
        color: theme.palette.normal.background

        PageHeader {
            id: deviceDetailsHeader
            title: page.currentDevice() && page.currentDevice().name
                   ? page.currentDevice().name
                   : i18n.tr("Device details")
            trailingActionBar.actions: [
                Action {
                    text: page.deviceEditMode ? i18n.tr("Done") : i18n.tr("Edit")
                    iconName: page.deviceEditMode ? "tick" : "settings"
                    onTriggered: {
                        if (page.deviceEditMode) {
                            page.deviceEditMode = false
                            ajaxController.lastStatus = i18n.tr("Device setting changes are not sent to Ajax yet.")
                        } else if (ajaxController.isHubDevice(page.currentDevice())) {
                            ajaxController.lastStatus = i18n.tr("Hub settings are not implemented yet.")
                            page.showInfoDialog(i18n.tr("Hub settings"),
                                                i18n.tr("Hub editing uses a different Ajax settings model and is not implemented yet. This page is read-only for the hub."))
                        } else {
                            page.startDeviceEdit()
                        }
                    }
                },
                Action {
                    text: i18n.tr("Close")
                    iconName: "close"
                    onTriggered: {
                        page.deviceEditMode = false
                        deviceDetailsOverlay.visible = false
                    }
                }
            ]
        }

        ListView {
            id: deviceDetailsList
            visible: !page.deviceEditMode
            anchors {
                top: deviceDetailsHeader.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            model: ajaxController.deviceDetailRows(page.currentDevice())
            delegate: ListItem {
                height: Math.max(units.gu(7), detailValue.paintedHeight + units.gu(3.2))

                Column {
                    anchors {
                        left: parent.left
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: units.gu(2)
                        rightMargin: units.gu(2)
                    }
                    spacing: units.gu(0.35)

                    Label {
                        width: parent.width
                        text: modelData.label
                        fontSize: "small"
                        font.bold: true
                        color: theme.palette.normal.backgroundText
                        elide: Text.ElideRight
                    }

                    Label {
                        id: detailValue
                        width: parent.width
                        text: modelData.value
                        fontSize: "small"
                        color: theme.palette.normal.backgroundSecondaryText
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: deviceDetailsList.count === 0
                text: i18n.tr("No device details loaded")
                color: theme.palette.normal.backgroundSecondaryText
            }
        }

        Flickable {
            id: deviceEditFlickable
            visible: page.deviceEditMode
            anchors {
                top: deviceDetailsHeader.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            contentHeight: deviceEditColumn.height + units.gu(4)
            clip: true

            Column {
                id: deviceEditColumn
                width: parent.width - units.gu(4)
                anchors {
                    left: parent.left
                    top: parent.top
                    leftMargin: units.gu(2)
                    topMargin: units.gu(2)
                }
                spacing: units.gu(1.6)

                Rectangle {
                    width: parent.width
                    height: editWarning.height + units.gu(2)
                    radius: units.gu(0.7)
                    color: theme.palette.normal.base

                    Label {
                        id: editWarning
                        anchors {
                            left: parent.left
                            right: parent.right
                            verticalCenter: parent.verticalCenter
                            leftMargin: units.gu(1.2)
                            rightMargin: units.gu(1.2)
                        }
                        text: i18n.tr("Test mode only. These controls do not save changes to Ajax yet.")
                        fontSize: "small"
                        color: theme.palette.normal.backgroundSecondaryText
                        wrapMode: Text.WordWrap
                    }
                }

                TextField {
                    width: parent.width
                    placeholderText: i18n.tr("Name")
                    text: page.editDeviceName
                    enabled: page.hasDeviceValue("name")
                    onTextChanged: page.editDeviceName = text
                }

                Button {
                    width: parent.width
                    text: page.editRoomName.length > 0 ? page.editRoomName : i18n.tr("Room")
                    enabled: page.hasDeviceValue("roomName") && ajaxController.rooms.length > 0
                    onClicked: {
                        page.pickerMode = "room"
                        devicePickerOverlay.visible = true
                    }
                }

                Rectangle {
                    width: parent.width
                    height: units.dp(1)
                    color: theme.palette.normal.base
                }

                Row {
                    width: parent.width
                    spacing: units.gu(1)

                    Label {
                        width: parent.width - alarmLedSwitch.width - parent.spacing
                        anchors.verticalCenter: alarmLedSwitch.verticalCenter
                        text: i18n.tr("LED indication of alarm")
                        wrapMode: Text.WordWrap
                    }

                    Switch {
                        id: alarmLedSwitch
                        enabled: page.hasDeviceValue("alarmLedIndication")
                        checked: page.deviceBool("alarmLedIndication")
                    }
                }

                Button {
                    width: parent.width
                    text: i18n.tr("Sensitivity") + ": " + (page.hasDeviceValue("sensitivity") ? page.deviceValue("sensitivity") : i18n.tr("Not available"))
                    enabled: page.hasDeviceValue("sensitivity")
                    onClicked: {
                        page.pickerMode = "sensitivity"
                        devicePickerOverlay.visible = true
                    }
                }

                Row {
                    width: parent.width
                    spacing: units.gu(1)

                    Label {
                        width: parent.width - correlationSwitch.width - parent.spacing
                        anchors.verticalCenter: correlationSwitch.verticalCenter
                        text: i18n.tr("Correlation signal processing")
                        wrapMode: Text.WordWrap
                    }

                    Switch {
                        id: correlationSwitch
                        enabled: page.hasDeviceValue("correlationSignalProcessing")
                        checked: page.deviceBool("correlationSignalProcessing")
                    }
                }

                Row {
                    width: parent.width
                    spacing: units.gu(1)

                    Label {
                        width: parent.width - alwaysActiveSwitch.width - parent.spacing
                        anchors.verticalCenter: alwaysActiveSwitch.verticalCenter
                        text: i18n.tr("Always active")
                        wrapMode: Text.WordWrap
                    }

                    Switch {
                        id: alwaysActiveSwitch
                        enabled: page.hasDeviceValue("alwaysActive")
                        checked: page.deviceBool("alwaysActive")
                    }
                }

                Label {
                    width: parent.width
                    text: i18n.tr("Alarm with siren")
                    font.bold: true
                    color: theme.palette.normal.backgroundSecondaryText
                }

                Row {
                    width: parent.width
                    spacing: units.gu(1)

                    Label {
                        width: parent.width - sirenMovementSwitch.width - parent.spacing
                        anchors.verticalCenter: sirenMovementSwitch.verticalCenter
                        text: i18n.tr("If movement is detected")
                        wrapMode: Text.WordWrap
                    }

                    Switch {
                        id: sirenMovementSwitch
                        enabled: page.hasDeviceValue("sirenOnMovement")
                        checked: page.deviceBool("sirenOnMovement")
                    }
                }

                Label {
                    width: parent.width
                    text: i18n.tr("Alarm reaction")
                    font.bold: true
                    color: theme.palette.normal.backgroundSecondaryText
                }

                Button {
                    width: parent.width
                    text: i18n.tr("Operating mode") + ": " + (page.hasDeviceValue("operatingMode") ? page.deviceValue("operatingMode") : i18n.tr("Not available"))
                    enabled: page.hasDeviceValue("operatingMode")
                    onClicked: {
                        page.pickerMode = "operation"
                        devicePickerOverlay.visible = true
                    }
                }

                Row {
                    width: parent.width
                    spacing: units.gu(1)

                    Label {
                        width: parent.width - nightModeSwitch.width - parent.spacing
                        anchors.verticalCenter: nightModeSwitch.verticalCenter
                        text: i18n.tr("Activate when night mode is active")
                        wrapMode: Text.WordWrap
                    }

                    Switch {
                        id: nightModeSwitch
                        enabled: page.hasDeviceValue("armedInNightMode")
                        checked: page.deviceBool("armedInNightMode")
                    }
                }
            }
        }

        Rectangle {
            id: devicePickerOverlay
            anchors.fill: parent
            z: 22
            visible: false
            color: theme.palette.normal.background

            PageHeader {
                id: devicePickerHeader
                title: page.pickerMode === "room" ? i18n.tr("Room")
                       : page.pickerMode === "operation" ? i18n.tr("Operating mode")
                       : i18n.tr("Sensitivity")
                trailingActionBar.actions: [
                    Action {
                        text: i18n.tr("Close")
                        iconName: "close"
                        onTriggered: devicePickerOverlay.visible = false
                    }
                ]
            }

            ListView {
                anchors {
                    top: devicePickerHeader.bottom
                    left: parent.left
                    right: parent.right
                    bottom: parent.bottom
                }
                model: page.pickerMode === "room" ? ajaxController.rooms
                       : page.pickerMode === "operation"
                         ? [ i18n.tr("Immediately"), i18n.tr("Entry/exit"), i18n.tr("Follower") ]
                         : [ i18n.tr("Low"), i18n.tr("Normal"), i18n.tr("High") ]
                delegate: ListItem {
                    height: units.gu(6)

                    Label {
                        anchors {
                            left: parent.left
                            right: parent.right
                            verticalCenter: parent.verticalCenter
                            leftMargin: units.gu(2)
                            rightMargin: units.gu(2)
                        }
                        text: page.pickerMode === "room" ? modelData.name : modelData
                        elide: Text.ElideRight
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: {
                            if (page.pickerMode === "room") {
                                page.editRoomName = modelData.name
                            } else if (page.pickerMode === "operation") {
                                page.editOperatingMode = modelData
                            } else {
                                page.editSensitivity = modelData
                            }
                            devicePickerOverlay.visible = false
                        }
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

        Flickable {
            id: messageTabsFlickable
            anchors {
                top: messagesHeader.bottom
                left: parent.left
                right: parent.right
            }
            height: units.gu(5.2)
            contentWidth: messageTabsRow.width + units.gu(3)
            contentHeight: height
            flickableDirection: Flickable.HorizontalFlick
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            Row {
                id: messageTabsRow
                anchors {
                    left: parent.left
                    verticalCenter: parent.verticalCenter
                    leftMargin: units.gu(1.5)
                }
                spacing: units.gu(1)

                Repeater {
                    model: ajaxController.messageTabs

                    Item {
                        id: messageTab
                        property bool selected: ajaxController.selectedMessageFilter === modelData.id
                        width: tabLabel.implicitWidth + units.gu(1.8)
                        height: units.gu(3.6)

                        Label {
                            id: tabLabel
                            anchors {
                                horizontalCenter: parent.horizontalCenter
                                verticalCenter: parent.verticalCenter
                            }
                            text: modelData.label
                            fontSize: "small"
                            font.bold: messageTab.selected
                            color: messageTab.selected ? theme.palette.normal.backgroundText
                                  : theme.palette.normal.backgroundSecondaryText
                        }

                        Rectangle {
                            anchors {
                                left: parent.left
                                right: parent.right
                                bottom: parent.bottom
                            }
                            height: units.dp(2)
                            visible: messageTab.selected
                            color: theme.palette.normal.backgroundText
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: ajaxController.selectedMessageFilter = modelData.id
                        }
                    }
                }
            }
        }

        ListView {
            id: messagesList
            anchors {
                top: messageTabsFlickable.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            model: ajaxController.filteredMessages(ajaxController.selectedMessageFilter, ajaxController.messages)
            section.property: "dateLabel"
            section.criteria: ViewSection.FullString
            section.delegate: Rectangle {
                width: messagesList.width
                height: units.gu(4)
                color: theme.palette.normal.background

                Label {
                    anchors {
                        left: parent.left
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                        leftMargin: units.gu(1.5)
                        rightMargin: units.gu(1.5)
                    }
                    text: section
                    fontSize: "small"
                    font.bold: true
                    color: theme.palette.normal.backgroundSecondaryText
                    elide: Text.ElideRight
                }
            }
            delegate: ListItem {
                height: Math.max(units.gu(8), messageColumn.height + units.gu(2.4))

                Row {
                    anchors {
                        left: parent.left
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                        margins: units.gu(1.5)
                    }
                    spacing: units.gu(1.2)

                    Rectangle {
                        width: units.gu(4.2)
                        height: width
                        radius: width / 2
                        clip: true
                        anchors.verticalCenter: parent.verticalCenter
                        color: modelData.category === "alarm" ? "#dc2626"
                               : modelData.category === "malfunction" ? "#d97706"
                               : modelData.category === "arming" ? "#4f46e5"
                               : theme.palette.normal.base

                        Image {
                            anchors.fill: parent
                            anchors.margins: units.dp(1)
                            visible: !!(modelData.avatarImage && modelData.avatarImage.length > 0)
                            source: modelData.avatarImage || ""
                            fillMode: Image.PreserveAspectCrop
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: !(modelData.avatarImage && modelData.avatarImage.length > 0)
                            text: modelData.avatar || "A"
                            fontSize: "small"
                            font.bold: true
                            color: modelData.category === "all" ? theme.palette.normal.backgroundText : "white"
                        }
                    }

                    Column {
                        id: messageColumn
                        width: parent.width - units.gu(4.2) - parent.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: units.gu(0.35)

                        Row {
                            width: parent.width
                            spacing: units.gu(1)

                            Label {
                                width: parent.width - messageTime.width - parent.spacing
                                text: modelData.title || i18n.tr("Ajax")
                                font.bold: true
                                wrapMode: Text.WordWrap
                                maximumLineCount: 2
                            }

                            Label {
                                id: messageTime
                                text: modelData.time
                                fontSize: "x-small"
                                color: theme.palette.normal.backgroundSecondaryText
                            }
                        }

                        Label {
                            width: parent.width
                            visible: modelData.text && modelData.text.length > 0
                            text: modelData.text
                            fontSize: "small"
                            color: theme.palette.normal.backgroundSecondaryText
                            wrapMode: Text.WordWrap
                        }
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: messagesList.count === 0
                text: i18n.tr("No messages in this filter")
                color: theme.palette.normal.backgroundSecondaryText
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

            Item {
                width: parent.width
                height: units.gu(6)

                Label {
                    anchors.centerIn: parent
                    text: ajaxController.qrClient.running ? i18n.tr("Restart QR login") : i18n.tr("Start QR login")
                    color: qrRestartMouse.pressed ? theme.palette.normal.activity
                          : theme.palette.normal.backgroundText
                    font.bold: true
                }

                MouseArea {
                    id: qrRestartMouse
                    anchors.fill: parent
                    onClicked: ajaxController.pollQrLogin()
                }
            }

        }
        }
    }

    Rectangle {
        id: infoDialog
        anchors.fill: parent
        z: 40
        visible: false
        color: "#99000000"

        MouseArea {
            anchors.fill: parent
            onClicked: infoDialog.visible = false
        }

        Rectangle {
            width: Math.min(parent.width - units.gu(4), units.gu(42))
            height: infoDialogColumn.height + units.gu(4)
            anchors.centerIn: parent
            radius: units.gu(0.8)
            color: theme.palette.normal.background

            MouseArea {
                anchors.fill: parent
            }

            Column {
                id: infoDialogColumn
                width: parent.width - units.gu(4)
                anchors {
                    top: parent.top
                    left: parent.left
                    margins: units.gu(2)
                }
                spacing: units.gu(1.2)

                Label {
                    width: parent.width
                    text: page.infoDialogTitle
                    fontSize: "large"
                    font.bold: true
                    color: theme.palette.normal.backgroundText
                    wrapMode: Text.WordWrap
                }

                Label {
                    width: parent.width
                    text: page.infoDialogText
                    color: theme.palette.normal.backgroundSecondaryText
                    wrapMode: Text.WordWrap
                }

                Item {
                    width: parent.width
                    height: units.gu(4.8)

                    Label {
                        anchors.centerIn: parent
                        text: i18n.tr("OK")
                        font.bold: true
                        color: okMouse.pressed ? theme.palette.normal.activity
                              : theme.palette.normal.backgroundText
                    }

                    MouseArea {
                        id: okMouse
                        anchors.fill: parent
                        onClicked: infoDialog.visible = false
                    }
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
