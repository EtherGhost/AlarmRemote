import QtQuick 2.7
import Qt.labs.settings 1.0
import AjaxRemote.Backend 1.0

QtObject {
    id: controller

    property string connectionState: i18n.tr("Not connected")
    property string loginState: i18n.tr("Not authenticated")
    property string selectedSystemName: ""
    property string alarmMode: "Unknown"
    property string lastStatus: i18n.tr("Not connected.")
    property bool authenticated: false
    property bool busy: false
    property bool commandBusy: false
    property bool panicSending: false
    property bool liveAlarmActive: false
    property string liveAlarmType: ""
    property string liveAlarmText: ""
    property double alarmIgnoreBeforeMs: 0
    property bool silentMessageRefresh: false
    property var seenRemoteMessageIds: ({})
    property bool qrLoginStarted: false
    property string qrLoginState: "Not started"
    property string qrPairingPayload: ""
    property string apiBaseUrl: ""
    property string activeSessionToken: ""
    property int activeSessionTokenBytes: 0
    property string activeUserId: ""
    property string activeUserRole: ""
    property string activeUserLogin: ""
    property string activeSessionCookie: ""
    property string selectedSpaceId: ""
    property var spaces: []
    property bool statusPollingEnabled: true
    property bool silentSpaceRefresh: false
    property string pendingTargetMode: ""
    property int pendingRefreshAttempts: 0
    property string lastStatusRefresh: ""
    property bool hasStoredSession: false
    property bool debugInventoryFetched: false
    property var rooms: []
    property var devices: []
    property var deviceDetailFetchQueue: []
    property var deviceDetailFetchRequested: ({})
    property var messages: []
    property string selectedMessageFilter: "all"
    property int pendingRefreshMaxAttempts: 10
    property var messageTabs: [
        { "id": "all", "label": i18n.tr("All") },
        { "id": "alarm", "label": i18n.tr("Alarms") },
        { "id": "malfunction", "label": i18n.tr("Malfunctions") },
        { "id": "arming", "label": i18n.tr("Arming modes") }
    ]

    property LocalSecretStore secretStore: LocalSecretStore {
    }

    property Settings sessionSettings: Settings {
        category: "session"
        property string apiBaseUrl: ""
        property string systemName: ""
        property string sessionToken: ""
        property string sessionSource: ""
        property string userId: ""
        property string userRole: ""
        property string userLogin: ""
        property string sessionCookie: ""
        property string spaceId: ""
    }

    Component.onCompleted: {
        migratePlaintextSessionIfNeeded()
        hasStoredSession = secretStore.hasSession()
        if (hasStoredSession) {
            restoreSession()
        }
    }

    property Timer statusPollTimer: Timer {
        interval: 30000
        repeat: true
        running: controller.authenticated
                 && controller.statusPollingEnabled
                 && controller.activeSessionToken.length > 0
        onTriggered: controller.refreshStatus(true)
    }

    property Timer commandRefreshTimer: Timer {
        interval: 250
        repeat: false
        onTriggered: controller.refreshPendingState()
    }

    property Timer deviceDetailFetchTimer: Timer {
        interval: 450
        repeat: false
        onTriggered: controller.processDeviceDetailFetchQueue()
    }

    property Timer liveMessagePollTimer: Timer {
        interval: 3000
        repeat: true
        running: controller.authenticated
                 && controller.selectedSpaceId.length > 0
                 && controller.activeSessionToken.length > 0
        onTriggered: controller.fetchMessages(true)
    }

    property QrLoginClient qrClient: QrLoginClient {
        onStateChanged: {
            qrLoginState = state
        }
        onQrPayloadReady: {
            qrLoginStarted = true
            qrPairingPayload = payload
            qrLoginState = i18n.tr("Scan QR in Ajax app")
            connectionState = i18n.tr("QR login pending")
            loginState = i18n.tr("Waiting for approval in official Ajax app")
            lastStatus = i18n.tr("QR session created. Scan it from an already logged-in Ajax app.")
        }
        onLoginSucceeded: {
            authenticated = true
            qrLoginStarted = false
            qrLoginState = i18n.tr("Approved")
            qrPairingPayload = ""
            activeSessionToken = sessionTokenHex
            activeSessionTokenBytes = sessionTokenHex.length / 2
            activeUserId = userId
            activeUserRole = userRole
            activeUserLogin = userLogin
            activeSessionCookie = sessionCookie
            apiBaseUrl = "mobile-gw.prod.ajax.systems"
            selectedSystemName = i18n.tr("Ajax session")
            alarmMode = "Unknown"
            connectionState = i18n.tr("Connected")
            loginState = i18n.tr("QR session active. Discovering Ajax space.")
            if (saveActiveSession("qr")) {
                lastStatus = i18n.tr("QR login approved and saved. Looking up Ajax space.")
            } else {
                lastStatus = i18n.tr("QR login approved, but local session storage failed.")
            }
            configureApiClient()
            discoverSpaces()
        }
        onFailed: {
            qrLoginStarted = false
            qrPairingPayload = ""
            connectionState = authenticated ? i18n.tr("Connected") : i18n.tr("Not connected")
            loginState = authenticated ? loginState : i18n.tr("QR login failed")
            lastStatus = message
        }
    }

    property AjaxApiClient apiClient: AjaxApiClient {
        onBusyChanged: {
            controller.busy = apiClient.busy
        }
        onSpacesListed: {
            controller.spaces = spaces
            applySpaceList(spaces)
        }
        onSpacesDiscovered: {
            if (selectedSpaceId.length === 0) {
                selectSpace(spaceId, spaceName, "Unknown", false)
            }
            if (!silentSpaceRefresh) {
                lastStatus = count > 1
                             ? i18n.tr("Using Ajax space: %1.").arg(selectedSystemName)
                             : i18n.tr("Ajax space ready: %1.").arg(selectedSystemName)
            }
        }
        onCommandSucceeded: {
            if (command === "State") {
                if (mode.length > 0) {
                    alarmMode = mode
                    if (isNormalAlarmMode(mode)) {
                        clearLiveAlarm()
                    }
                }
                lastStatusRefresh = Qt.formatTime(new Date(), "HH:mm:ss")
                if (!silentSpaceRefresh) {
                    lastStatus = mode.length > 0 ? i18n.tr("Status updated: %1.").arg(modeLabel(mode)) : i18n.tr("Status updated.")
                    appendMessage(i18n.tr("Ajax"), lastStatus, "arming")
                }
                silentSpaceRefresh = false
                return
            }
            if (command === "Panic") {
                panicSending = false
            }
            commandBusy = false
            lastStatus = i18n.tr("%1 succeeded.").arg(commandLabel(command))
            appendMessage(i18n.tr("Ajax"), i18n.tr("%1 succeeded for %2.").arg(commandLabel(command)).arg(selectedSystemName), messageCategoryForCommand(command))
            if (pendingTargetMode.length > 0) {
                pendingRefreshAttempts = 0
                commandRefreshTimer.restart()
            }
        }
        onFailed: {
            silentSpaceRefresh = false
            panicSending = false
            commandBusy = false
            pendingTargetMode = ""
            pendingRefreshAttempts = 0
            lastStatus = message
            appendMessage(i18n.tr("Ajax"), message, "malfunction")
        }
        onInventoryLoaded: {
            updateDevicesIfChanged(devices)
            if (selectedSpaceId.length > 0) {
                apiClient.fetchSpaceRooms(selectedSpaceId)
            }
            enqueueMissingDeviceDetails()
        }
        onRoomsLoaded: {
            updateRoomsIfChanged(rooms)
            updateDevicesIfChanged(mergeRoomNames(controller.devices, controller.rooms))
            enqueueMissingDeviceDetails()
        }
        onDevicesLoaded: {
            updateDevicesIfChanged(mergeRoomNames(mergeDeviceDetails(controller.devices, devices), controller.rooms))
            enqueueMissingDeviceDetails()
        }
        onMessagesLoaded: {
            applyRemoteMessages(messages, silentMessageRefresh)
            silentMessageRefresh = false
        }
        onAlarmEventsLoaded: {
            if (prependRemoteMessages(messages)) {
                lastStatus = i18n.tr("Alarm event received.")
            }
        }
    }

    function startQrLogin() {
        qrLoginStarted = true
        qrLoginState = i18n.tr("Creating QR session")
        qrPairingPayload = ""
        connectionState = i18n.tr("QR login pending")
        loginState = i18n.tr("Opening Ajax QR session")
        lastStatus = i18n.tr("Creating QR login session with Ajax.")
        qrClient.start()
    }

    function pollQrLogin() {
        startQrLogin()
    }

    function completeQrLogin() {
        if (!qrLoginStarted) {
            startQrLogin()
            return
        }
        qrLoginState = i18n.tr("Completion blocked")
        lastStatus = i18n.tr("QR completion endpoint is unknown. No Ajax session was stored.")
    }

    function migratePlaintextSessionIfNeeded() {
        if (secretStore.hasSession() || sessionSettings.sessionToken.length === 0) {
            return
        }

        var migrated = secretStore.storeSession(
                    sessionSettings.apiBaseUrl,
                    sessionSettings.systemName,
                    sessionSettings.sessionToken,
                    sessionSettings.sessionSource,
                    sessionSettings.userId,
                    sessionSettings.userRole,
                    sessionSettings.userLogin,
                    sessionSettings.sessionCookie,
                    sessionSettings.spaceId)
        if (migrated) {
            clearLegacySessionSettings()
            lastStatus = i18n.tr("Stored session migrated to protected local storage.")
        } else {
            lastStatus = i18n.tr("Stored plaintext session migration failed.")
        }
    }

    function clearLegacySessionSettings() {
        sessionSettings.apiBaseUrl = ""
        sessionSettings.systemName = ""
        sessionSettings.sessionToken = ""
        sessionSettings.sessionSource = ""
        sessionSettings.userId = ""
        sessionSettings.userRole = ""
        sessionSettings.userLogin = ""
        sessionSettings.sessionCookie = ""
    }

    function saveActiveSession(source) {
        var saved = secretStore.storeSession(apiBaseUrl,
                                             selectedSystemName,
                                             activeSessionToken,
                                             source,
                                             activeUserId,
                                             activeUserRole,
                                             activeUserLogin,
                                             activeSessionCookie,
                                             selectedSpaceId)
        if (saved) {
            clearLegacySessionSettings()
        }
        hasStoredSession = secretStore.hasSession()
        return saved
    }

    function restoreSession() {
        var saved = secretStore.loadSession()
        if (!saved || !saved.sessionToken || saved.sessionToken.length === 0) {
            lastStatus = secretStore.storageStatus.length > 0
                         ? secretStore.storageStatus
                         : i18n.tr("No stored protected session.")
            authenticated = false
            hasStoredSession = false
            return
        }

        authenticated = true
        apiBaseUrl = saved.apiBaseUrl || ""
        activeSessionToken = saved.sessionToken
        activeSessionTokenBytes = activeSessionToken.length / 2
        activeUserId = saved.userId || ""
        activeUserRole = saved.userRole || ""
        activeUserLogin = saved.userLogin || ""
        activeSessionCookie = saved.sessionCookie || ""
        selectedSpaceId = saved.spaceId || sessionSettings.spaceId
        spaces = []
        selectedSystemName = saved.systemName && saved.systemName.length > 0 ? saved.systemName : i18n.tr("Stored Ajax session")
        connectionState = i18n.tr("Connected")
        loginState = saved.sessionSource === "qr"
                     ? i18n.tr("Saved QR session loaded from protected local storage.")
                     : i18n.tr("Stored opaque token loaded from protected local storage.")
        alarmMode = "Unknown"
        lastStatus = selectedSpaceId.length > 0 ? i18n.tr("Stored session and space restored.") : i18n.tr("Stored session restored. Looking up Ajax space.")
        configureApiClient()
        discoverSpaces(selectedSpaceId.length > 0)
    }

    function manualSessionLogin(baseUrl, token, systemName, remember) {
        var cleanBaseUrl = baseUrl.trim()
        var cleanToken = token.trim()
        var cleanSystemName = systemName.trim()

        if (cleanToken.length === 0) {
            lastStatus = i18n.tr("Login blocked: session/token field is empty.")
            return false
        }

        authenticated = true
        qrLoginStarted = false
        qrLoginState = i18n.tr("Manual session")
        qrPairingPayload = ""
        apiBaseUrl = cleanBaseUrl
        activeSessionToken = cleanToken
        activeSessionTokenBytes = cleanToken.length / 2
        activeUserId = ""
        activeUserRole = ""
        activeUserLogin = ""
        activeSessionCookie = ""
        selectedSpaceId = ""
        spaces = []
        selectedSystemName = cleanSystemName.length > 0 ? cleanSystemName : i18n.tr("Manual Ajax session")
        alarmMode = "Unknown"
        connectionState = i18n.tr("Developer session")
        loginState = remember ? i18n.tr("Opaque token saved in protected local storage.") : i18n.tr("Opaque token kept for this run only.")
        lastStatus = i18n.tr("Developer session accepted.")

        if (remember) {
            if (!saveActiveSession("manual")) {
                lastStatus = secretStore.storageStatus
            }
        }
        hasStoredSession = secretStore.hasSession()

        return true
    }

    function clearStoredSession() {
        apiClient.stopSpaceStream()
        apiClient.stopAlarmStream()
        resetDeviceDetailFetchQueue()
        liveAlarmActive = false
        liveAlarmType = ""
        liveAlarmText = ""
        seenRemoteMessageIds = ({})
        secretStore.clearSession()
        clearLegacySessionSettings()
        sessionSettings.spaceId = ""
        spaces = []
        hasStoredSession = false
        lastStatus = i18n.tr("Stored protected session cleared.")
        lastStatusRefresh = ""
    }

    function logout() {
        qrClient.cancel()
        apiClient.stopSpaceStream()
        apiClient.stopAlarmStream()
        resetDeviceDetailFetchQueue()
        liveAlarmActive = false
        liveAlarmType = ""
        liveAlarmText = ""
        seenRemoteMessageIds = ({})
        authenticated = false
        qrLoginStarted = false
        qrLoginState = i18n.tr("Not started")
        qrPairingPayload = ""
        selectedSystemName = ""
        apiBaseUrl = ""
        activeSessionToken = ""
        activeSessionTokenBytes = 0
        activeUserId = ""
        activeUserRole = ""
        activeUserLogin = ""
        activeSessionCookie = ""
        selectedSpaceId = ""
        spaces = []
        alarmMode = "Unknown"
        pendingTargetMode = ""
        pendingRefreshAttempts = 0
        panicSending = false
        commandBusy = false
        lastStatusRefresh = ""
        connectionState = i18n.tr("Not connected")
        loginState = i18n.tr("Not authenticated")
        lastStatus = i18n.tr("Logged out from current session.")
    }

    function guardedAction(label) {
        if (!authenticated) {
            lastStatus = i18n.tr("%1 blocked: not authenticated.").arg(label)
            return false
        }
        if (selectedSystemName.length === 0) {
            lastStatus = i18n.tr("%1 blocked: no verified hub/space selected.").arg(label)
            return false
        }
        return true
    }

    function appendMessage(title, text, category, avatar) {
        var now = new Date()
        var next = messages.slice(0)
        var cleanTitle = title && title.length > 0 ? title : i18n.tr("Ajax")
        var cleanCategory = category && category.length > 0 ? category : inferredMessageCategory(cleanTitle, text)
        next.unshift({
            "time": Qt.formatTime(now, "HH:mm:ss"),
            "dateKey": Qt.formatDate(now, "yyyy-MM-dd"),
            "dateLabel": Qt.formatDate(now, "yyyy-MM-dd") === Qt.formatDate(new Date(), "yyyy-MM-dd")
                         ? i18n.tr("Today")
                         : Qt.formatDate(now, Qt.DefaultLocaleShortDate),
            "title": cleanTitle,
            "text": text,
            "category": cleanCategory,
            "avatar": avatar && avatar.length > 0 ? avatar : cleanTitle.charAt(0).toUpperCase()
        })
        messages = next
    }

    function normalizeRemoteMessages(source) {
        var result = []
        for (var i = 0; i < source.length; i++) {
            var item = {}
            for (var key in source[i]) {
                item[key] = source[i][key]
            }
            if (item.dateLabel === "Today") {
                item.dateLabel = i18n.tr("Today")
            }
            if (!item.avatar || item.avatar.length === 0) {
                item.avatar = item.title && item.title.length > 0 ? item.title.charAt(0).toUpperCase() : "A"
            }
            result.push(item)
        }
        return result
    }

    function remoteMessageKey(item) {
        if (item && item.id && String(item.id).length > 0) {
            return String(item.id)
        }
        return [
            item && item.dateKey ? item.dateKey : "",
            item && item.time ? item.time : "",
            item && item.category ? item.category : "",
            item && item.title ? item.title : "",
            item && item.text ? item.text : ""
        ].join("|")
    }

    function rememberRemoteMessages(list) {
        var seen = seenRemoteMessageIds
        for (var i = 0; i < list.length; i++) {
            seen[remoteMessageKey(list[i])] = true
        }
        seenRemoteMessageIds = seen
    }

    function clearLiveAlarm() {
        liveAlarmActive = false
        liveAlarmType = ""
        liveAlarmText = ""
        alarmIgnoreBeforeMs = Date.now()
        rememberRemoteMessages(messages)
    }

    function messageTimestampMs(item) {
        if (item && item.timestampMs !== undefined && item.timestampMs !== null) {
            var value = parseFloat(String(item.timestampMs))
            return isNaN(value) ? 0 : value
        }
        return 0
    }

    function isIgnoredAlarmMessage(item) {
        var timestamp = messageTimestampMs(item)
        return timestamp > 0 && alarmIgnoreBeforeMs > 0 && timestamp <= alarmIgnoreBeforeMs
    }

    function hasNewAlarmMessages(list) {
        for (var i = 0; i < list.length; i++) {
            if (list[i].category !== "alarm") {
                continue
            }
            if (isIgnoredAlarmMessage(list[i])) {
                continue
            }
            if (!seenRemoteMessageIds[remoteMessageKey(list[i])]) {
                return true
            }
        }
        return false
    }

    function firstNewAlarmMessage(list) {
        for (var i = 0; i < list.length; i++) {
            if (list[i].category !== "alarm") {
                continue
            }
            if (isIgnoredAlarmMessage(list[i])) {
                continue
            }
            if (!seenRemoteMessageIds[remoteMessageKey(list[i])]) {
                return list[i]
            }
        }
        return null
    }

    function alarmTypeForMessage(item) {
        var tag = item && item.eventTag !== undefined ? parseInt(item.eventTag) : 0
        if (tag === 99) {
            return "panic"
        }
        if (tag === 58 || tag === 60 || tag === 61 || tag === 65 || tag === 66
                || tag === 67 || tag === 68 || tag === 69 || tag === 70 || tag === 74) {
            return "fire"
        }
        if (tag === 113 || tag === 111 || (item && item.category === "alarm")) {
            return "intrusion"
        }
        return "alarm"
    }

    function alarmTypeLabel(type) {
        if (type === "panic") {
            return i18n.tr("Panic alarm")
        }
        if (type === "fire") {
            return i18n.tr("Fire alarm")
        }
        if (type === "intrusion") {
            return i18n.tr("Intrusion alarm")
        }
        return i18n.tr("Alarm")
    }

    function setLiveAlarmFromMessage(item) {
        var type = alarmTypeForMessage(item)
        var context = ""
        if (type === "panic" && item && item.sourceName && String(item.sourceName).length > 0) {
            context = String(item.sourceName)
        } else if (item && item.roomName && String(item.roomName).length > 0) {
            context = String(item.roomName)
        } else if (type !== "panic" && item && item.text && String(item.text).length > 0) {
            context = String(item.text)
        } else if (type !== "panic" && item && item.title && String(item.title).length > 0) {
            context = String(item.title)
        }
        liveAlarmType = type
        liveAlarmText = context.length > 0
                        ? i18n.tr("%1: %2").arg(alarmTypeLabel(type)).arg(context)
                        : alarmTypeLabel(type)
        liveAlarmActive = true
    }

    function hasRemoteMessageBaseline() {
        for (var key in seenRemoteMessageIds) {
            return true
        }
        return false
    }

    function applyRemoteMessages(source, silent) {
        var next = normalizeRemoteMessages(source)
        var hasBaseline = hasRemoteMessageBaseline()
        var newAlarm = hasBaseline ? firstNewAlarmMessage(next) : null
        var hasNewAlarm = newAlarm !== null
        messages = next
        rememberRemoteMessages(next)

        if (hasNewAlarm) {
            setLiveAlarmFromMessage(newAlarm)
            lastStatus = i18n.tr("Alarm event received.")
            return
        }
        if (!silent) {
            lastStatus = next.length > 0
                         ? i18n.tr("Loaded %1 Ajax messages.").arg(next.length)
                         : i18n.tr("No Ajax messages returned.")
        }
    }

    function prependRemoteMessages(source) {
        var incoming = normalizeRemoteMessages(source)
        if (incoming.length === 0) {
            return false
        }

        var fresh = []
        for (var n = 0; n < incoming.length; n++) {
            var key = remoteMessageKey(incoming[n])
            if (seenRemoteMessageIds[key]) {
                continue
            }
            fresh.push(incoming[n])
        }
        if (fresh.length > 0) {
            messages = fresh.concat(messages)
            rememberRemoteMessages(fresh)
            var alarm = null
            for (var a = 0; a < fresh.length; a++) {
                if (fresh[a].category === "alarm") {
                    if (isIgnoredAlarmMessage(fresh[a])) {
                        continue
                    }
                    alarm = fresh[a]
                    break
                }
            }
            if (alarm) {
                setLiveAlarmFromMessage(alarm)
                return true
            }
        }
        return false
    }

    function isNormalAlarmMode(mode) {
        return mode === "Armed"
               || mode === "Disarmed"
               || mode === "Night mode"
               || mode === "Partially armed"
    }

    function inferredMessageCategory(title, text) {
        var haystack = ((title || "") + " " + (text || "")).toLowerCase()
        if (haystack.indexOf("panic") >= 0 || haystack.indexOf("larm") >= 0 || haystack.indexOf("alarm") >= 0) {
            return "alarm"
        }
        if (haystack.indexOf("malfunction") >= 0 || haystack.indexOf("fault") >= 0
                || haystack.indexOf("failed") >= 0 || haystack.indexOf("rejected") >= 0
                || haystack.indexOf("blocked") >= 0 || haystack.indexOf("fel") >= 0
                || haystack.indexOf("misslyck") >= 0) {
            return "malfunction"
        }
        if (haystack.indexOf("arm") >= 0 || haystack.indexOf("disarm") >= 0
                || haystack.indexOf("night") >= 0 || haystack.indexOf("status updated") >= 0
                || haystack.indexOf("tillkoppl") >= 0 || haystack.indexOf("frånkoppl") >= 0
                || haystack.indexOf("natt") >= 0) {
            return "arming"
        }
        return "all"
    }

    function messageCategoryForCommand(command) {
        if (command === "Panic") {
            return "alarm"
        }
        if (command === "Arm" || command === "Disarm" || command === "Night mode") {
            return "arming"
        }
        return "all"
    }

    function filteredMessages(filter, source) {
        var selected = filter && filter.length > 0 ? filter : "all"
        var list = source || messages
        if (selected === "all") {
            return list
        }

        var result = []
        for (var i = 0; i < list.length; i++) {
            if (list[i] && list[i].category === selected) {
                result.push(list[i])
            }
        }
        return result
    }

    function normalizedValue(value) {
        if (value === undefined || value === null) {
            return ""
        }
        return String(value)
    }

    function stableListKey(list) {
        var rows = []
        for (var i = 0; i < list.length; i++) {
            var item = list[i]
            var keys = []
            for (var key in item) {
                keys.push(key)
            }
            keys.sort()
            var parts = []
            for (var k = 0; k < keys.length; k++) {
                parts.push(keys[k] + "=" + normalizedValue(item[keys[k]]))
            }
            rows.push(parts.join("|"))
        }
        rows.sort()
        return rows.join("\n")
    }

    function updateRoomsIfChanged(nextRooms) {
        if (stableListKey(rooms) !== stableListKey(nextRooms)) {
            rooms = nextRooms
        }
    }

    function updateDevicesIfChanged(nextDevices) {
        if (stableListKey(devices) !== stableListKey(nextDevices)) {
            devices = nextDevices
        }
    }

    function resetDeviceDetailFetchQueue() {
        deviceDetailFetchTimer.stop()
        deviceDetailFetchQueue = []
        deviceDetailFetchRequested = ({})
    }

    function hasDeviceSignalStrength(device) {
        return device
                && device.signalStrength !== undefined
                && device.signalStrength !== null
                && String(device.signalStrength).length > 0
    }

    function enqueueMissingDeviceDetails() {
        if (!authenticated || selectedSpaceId.length === 0 || devices.length === 0) {
            return
        }

        var requested = deviceDetailFetchRequested
        var queue = deviceDetailFetchQueue.slice(0)
        var added = false

        for (var i = 0; i < devices.length; i++) {
            var device = devices[i]
            if (!device || isHubDevice(device) || hasDeviceSignalStrength(device)) {
                continue
            }
            if (!device.id || !device.hubId || requested[device.id]) {
                continue
            }
            requested[device.id] = true
            queue.push({ "id": device.id, "hubId": device.hubId })
            added = true
        }

        if (!added) {
            return
        }

        deviceDetailFetchRequested = requested
        deviceDetailFetchQueue = queue
        console.log("AjaxRemote device detail queue:", queue.length)
        if (!deviceDetailFetchTimer.running) {
            deviceDetailFetchTimer.restart()
        }
    }

    function processDeviceDetailFetchQueue() {
        if (!authenticated || selectedSpaceId.length === 0 || deviceDetailFetchQueue.length === 0) {
            return
        }
        if (busy || commandBusy || panicSending) {
            deviceDetailFetchTimer.restart()
            return
        }

        var queue = deviceDetailFetchQueue.slice(0)
        var next = queue.shift()
        deviceDetailFetchQueue = queue

        if (next && next.id && next.hubId) {
            console.log("AjaxRemote fetching queued device detail, remaining:", queue.length)
            configureApiClient()
            apiClient.fetchHubDevice(next.hubId, next.id)
        }

        if (queue.length > 0) {
            deviceDetailFetchTimer.restart()
        }
    }

    function isHubDevice(device) {
        if (!device) {
            return false
        }
        var kind = device.kind ? String(device.kind).toLowerCase() : ""
        return kind === "hub" || kind === "passive hub"
    }

    function mergeRoomNames(deviceList, roomList) {
        var roomsById = {}
        for (var r = 0; r < roomList.length; r++) {
            roomsById[roomList[r].id] = roomList[r].name
        }

        var merged = []
        for (var i = 0; i < deviceList.length; i++) {
            var item = {}
            for (var key in deviceList[i]) {
                item[key] = deviceList[i][key]
            }
            if (item.roomId && roomsById[item.roomId]) {
                item.roomName = roomsById[item.roomId]
            }
            merged.push(item)
        }
        return merged
    }

    function mergeDeviceDetails(baseList, detailList) {
        var result = []
        var byId = {}

        for (var i = 0; i < baseList.length; i++) {
            var item = {}
            for (var baseKey in baseList[i]) {
                item[baseKey] = baseList[i][baseKey]
            }
            if (item.id && item.id.length > 0) {
                byId[item.id] = item
            }
            result.push(item)
        }

        for (var d = 0; d < detailList.length; d++) {
            var detail = detailList[d]
            var id = detail.id || ""
            var target = id.length > 0 ? byId[id] : null

            if (!target) {
                target = {}
                for (var newKey in detail) {
                    target[newKey] = detail[newKey]
                }
                if (id.length > 0) {
                    byId[id] = target
                }
                result.push(target)
                continue
            }

            for (var key in detail) {
                if ((key === "name" || key === "roomName")
                        && target[key] !== undefined
                        && target[key] !== null
                        && String(target[key]).length > 0) {
                    continue
                }
                if (detail[key] !== undefined && detail[key] !== null && String(detail[key]).length > 0) {
                    target[key] = detail[key]
                }
            }
        }

        return result
    }

    function deviceDetailRows(device) {
        if (!device) {
            return []
        }
        var labels = {
            "name": i18n.tr("Name"),
            "roomName": i18n.tr("Room"),
            "roomId": i18n.tr("Room ID"),
            "id": i18n.tr("Device ID"),
            "kind": i18n.tr("Type"),
            "serviceState": i18n.tr("Service state"),
            "deviceColor": i18n.tr("Device color"),
            "boxType": i18n.tr("Box type"),
            "chimeStatus": i18n.tr("Chime status"),
            "restoreRequired": i18n.tr("Restore required"),
            "sortingKey": i18n.tr("Sorting key"),
            "channels": i18n.tr("Channels"),
            "nonZombieChannelsCount": i18n.tr("Active channels"),
            "videoEdgeType": i18n.tr("Video edge type"),
            "temperature": i18n.tr("Temperature"),
            "signalStrength": i18n.tr("Signal strength"),
            "connection": i18n.tr("Connection"),
            "batteryLevel": i18n.tr("Battery level"),
            "lock": i18n.tr("Lock/cover"),
            "sensitivity": i18n.tr("Sensitivity"),
            "correlationSignalProcessing": i18n.tr("Correlation signal processing"),
            "alwaysActive": i18n.tr("Always active"),
            "disable": i18n.tr("Disable"),
            "oneTimeDisable": i18n.tr("One-time disable"),
            "operatingMode": i18n.tr("Operating mode"),
            "armedInNightMode": i18n.tr("Arm when night mode is enabled"),
            "alarmLedIndication": i18n.tr("LED indication of alarm"),
            "sirenOnMovement": i18n.tr("If movement is detected"),
            "interactionDisabled": i18n.tr("Interaction disabled"),
            "deviceIndex": i18n.tr("Device index"),
            "marketingDeviceIndex": i18n.tr("Displayed device index"),
            "deviceMarketingId": i18n.tr("Marketing ID")
        }
        var preferred = isHubDevice(device)
                ? [
                    "name", "id", "kind", "connection", "batteryLevel",
                    "serviceState", "deviceColor", "boxType", "restoreRequired",
                    "interactionDisabled", "deviceIndex", "marketingDeviceIndex",
                    "deviceMarketingId", "sortingKey"
                ]
                : [
                    "name", "roomName", "roomId", "id",
                    "temperature", "signalStrength", "connection", "batteryLevel",
                    "lock", "sensitivity", "correlationSignalProcessing",
                    "alwaysActive", "disable", "oneTimeDisable", "operatingMode",
                    "armedInNightMode", "alarmLedIndication", "sirenOnMovement",
                    "kind",
                    "serviceState", "deviceColor", "boxType", "chimeStatus",
                    "restoreRequired", "channels", "nonZombieChannelsCount",
                    "videoEdgeType", "interactionDisabled", "deviceIndex",
                    "marketingDeviceIndex", "deviceMarketingId", "sortingKey"
                ]
        var showMissing = {
            "temperature": true,
            "signalStrength": true,
            "connection": true,
            "batteryLevel": true,
            "lock": true,
            "sensitivity": true,
            "correlationSignalProcessing": true,
            "alwaysActive": true,
            "disable": true,
            "oneTimeDisable": true,
            "operatingMode": true,
            "armedInNightMode": true
        }
        var seen = {}
        var rows = []

        for (var i = 0; i < preferred.length; i++) {
            var key = preferred[i]
            if (device[key] !== undefined && device[key] !== null && String(device[key]).length > 0) {
                rows.push({ "label": labels[key] || key, "value": String(device[key]) })
                seen[key] = true
            } else if (showMissing[key]) {
                rows.push({ "label": labels[key] || key, "value": i18n.tr("Not available") })
                seen[key] = true
            }
        }

        var keys = []
        for (var field in device) {
            if (!seen[field]) {
                keys.push(field)
            }
        }
        keys.sort()
        for (var k = 0; k < keys.length; k++) {
            rows.push({ "label": labels[keys[k]] || keys[k], "value": String(device[keys[k]]) })
        }
        return rows
    }

    function deviceById(id, fallback) {
        if (!id || id.length === 0) {
            return fallback || null
        }
        for (var i = 0; i < devices.length; i++) {
            if (devices[i].id === id) {
                return devices[i]
            }
        }
        return fallback || null
    }

    function modeLabel(mode) {
        if (mode === "Armed") {
            return i18n.tr("Armed")
        }
        if (mode === "Disarmed") {
            return i18n.tr("Disarmed")
        }
        if (mode === "Night mode") {
            return i18n.tr("Night mode")
        }
        if (mode === "Partially armed") {
            return i18n.tr("Partially armed")
        }
        if (mode === "Unknown") {
            return i18n.tr("Unknown")
        }
        return mode
    }

    function commandLabel(command) {
        if (command === "Arm") {
            return i18n.tr("Arm")
        }
        if (command === "Disarm") {
            return i18n.tr("Disarm")
        }
        if (command === "Night mode") {
            return i18n.tr("Night mode")
        }
        if (command === "Panic") {
            return i18n.tr("Panic")
        }
        if (command === "State") {
            return i18n.tr("State")
        }
        return command
    }

    function configureApiClient() {
        apiClient.setSession(activeSessionToken, activeUserId, activeUserRole, activeUserLogin, activeSessionCookie)
    }

    function applySpaceList(list) {
        if (!list || list.length === 0) {
            return
        }

        var selected = null
        for (var i = 0; i < list.length; i++) {
            if (list[i].id === selectedSpaceId || list[i].id === sessionSettings.spaceId) {
                selected = list[i]
                break
            }
        }
        if (selected === null) {
            selected = list[0]
        }

        var previousMode = alarmMode
        selectSpace(selected.id, selected.name, selected.mode, false)
        var changed = previousMode !== alarmMode
        if (changed || !silentSpaceRefresh) {
            lastStatusRefresh = Qt.formatTime(new Date(), "HH:mm:ss")
        }
        if (pendingTargetMode.length > 0) {
            if (alarmMode === pendingTargetMode) {
                pendingTargetMode = ""
                pendingRefreshAttempts = 0
            } else if (pendingRefreshAttempts < pendingRefreshMaxAttempts) {
                commandRefreshTimer.restart()
            } else {
                pendingTargetMode = ""
                pendingRefreshAttempts = 0
            }
        }
        if (!silentSpaceRefresh) {
            lastStatus = i18n.tr("Status updated: %1.").arg(modeLabel(alarmMode))
        }
        silentSpaceRefresh = false
    }

    function selectSpace(spaceId, spaceName, mode, explicitSelection) {
        var changedSpace = selectedSpaceId !== spaceId
        selectedSpaceId = spaceId
        selectedSystemName = spaceName && spaceName.length > 0 ? spaceName : i18n.tr("Ajax space")
        sessionSettings.spaceId = selectedSpaceId
        sessionSettings.systemName = selectedSystemName
        if (activeSessionToken.length > 0 && secretStore.hasSession()) {
            secretStore.updateSpace(selectedSpaceId, selectedSystemName)
        }
        if (mode && mode.length > 0 && mode !== "Unknown") {
            alarmMode = mode
        }
        if (explicitSelection) {
            lastStatus = i18n.tr("Selected Ajax space: %1.").arg(selectedSystemName)
        }
        configureApiClient()
        apiClient.startSpaceStream(selectedSpaceId)
        apiClient.startAlarmStream(selectedSpaceId)
        if (changedSpace) {
            debugInventoryFetched = false
            resetDeviceDetailFetchQueue()
            clearLiveAlarm()
            seenRemoteMessageIds = ({})
        }
        debugFetchInventoryOnce()
    }

    function discoverSpaces(silent) {
        if (!authenticated || activeSessionToken.length === 0) {
            lastStatus = i18n.tr("Space discovery blocked: no Ajax session.")
            return
        }
        if (busy) {
            if (!silent) {
                lastStatus = i18n.tr("Status refresh skipped: Ajax request already running.")
            }
            return
        }
        silentSpaceRefresh = !!silent
        configureApiClient()
        if (!silentSpaceRefresh) {
            lastStatus = i18n.tr("Looking up Ajax space.")
        }
        apiClient.discoverSpaces()
    }

    function refreshStatus(silent) {
        discoverSpaces(silent)
    }

    function refreshPendingState() {
        if (pendingTargetMode.length === 0) {
            return
        }
        if (busy) {
            if (pendingRefreshAttempts < pendingRefreshMaxAttempts) {
                commandRefreshTimer.restart()
            } else {
                pendingTargetMode = ""
                pendingRefreshAttempts = 0
            }
            return
        }
        pendingRefreshAttempts += 1
        refreshStatus(true)
    }

    function debugFetchInventoryOnce() {
        if (debugInventoryFetched || !authenticated || selectedSpaceId.length === 0 || busy) {
            return
        }
        debugInventoryFetched = true
        configureApiClient()
        apiClient.debugFetchLightDevices(selectedSpaceId)
    }

    function fetchDeviceDetails(device) {
        if (!device || !device.id || !device.hubId || busy || !authenticated) {
            return
        }
        configureApiClient()
        apiClient.fetchHubDevice(device.hubId, device.id)
    }

    function requireSpace(label) {
        if (!guardedAction(label)) {
            return false
        }
        if (selectedSpaceId.length === 0) {
            lastStatus = i18n.tr("%1 blocked: no Ajax space selected yet.").arg(label)
            discoverSpaces()
            return false
        }
        configureApiClient()
        return true
    }

    function arm() {
        if (requireSpace(i18n.tr("Arm"))) {
            lastStatus = i18n.tr("Sending Arm command.")
            clearLiveAlarm()
            commandBusy = true
            pendingTargetMode = "Armed"
            pendingRefreshAttempts = 0
            apiClient.arm(selectedSpaceId)
        }
    }

    function disarm() {
        if (requireSpace(i18n.tr("Disarm"))) {
            lastStatus = i18n.tr("Sending Disarm command.")
            clearLiveAlarm()
            commandBusy = true
            pendingTargetMode = "Disarmed"
            pendingRefreshAttempts = 0
            apiClient.disarm(selectedSpaceId)
        }
    }

    function enableNightMode() {
        if (requireSpace(i18n.tr("Night mode"))) {
            lastStatus = i18n.tr("Sending Night mode command.")
            clearLiveAlarm()
            commandBusy = true
            pendingTargetMode = "Night mode"
            pendingRefreshAttempts = 0
            apiClient.enableNightMode(selectedSpaceId)
        }
    }

    function triggerPanic() {
        if (requireSpace(i18n.tr("Panic"))) {
            lastStatus = i18n.tr("Sending Panic command.")
            commandBusy = true
            panicSending = true
            apiClient.triggerPanic(selectedSpaceId)
        }
    }

    function fetchMessages(silent) {
        if (!authenticated || selectedSpaceId.length === 0) {
            if (!silent) {
                lastStatus = i18n.tr("Messages blocked: no Ajax space selected.")
            }
            return messages
        }
        if (busy) {
            if (!silent) {
                lastStatus = i18n.tr("Messages refresh skipped: Ajax request already running.")
            }
            return messages
        }
        configureApiClient()
        silentMessageRefresh = !!silent
        if (!silentMessageRefresh) {
            lastStatus = i18n.tr("Loading Ajax messages.")
        }
        apiClient.fetchMessages(selectedSpaceId)
        return messages
    }
}
