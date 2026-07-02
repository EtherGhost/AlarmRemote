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
    property var messages: []

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
        interval: 500
        repeat: false
        onTriggered: controller.refreshPendingState()
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
            appendMessage(i18n.tr("Login"), i18n.tr("QR session created. Waiting for approval."))
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
                appendMessage(i18n.tr("Login"), i18n.tr("QR login approved. Session token saved locally."))
            } else {
                lastStatus = i18n.tr("QR login approved, but local session storage failed.")
                appendMessage(i18n.tr("Login"), i18n.tr("QR login approved, but local session storage failed."))
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
            appendMessage(i18n.tr("Login"), message)
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
                appendMessage(i18n.tr("Login"), i18n.tr("Ajax space ready: %1.").arg(selectedSystemName))
            }
        }
        onCommandSucceeded: {
            if (command === "State") {
                if (mode.length > 0) {
                    alarmMode = mode
                }
                lastStatusRefresh = Qt.formatTime(new Date(), "HH:mm:ss")
                if (!silentSpaceRefresh) {
                    lastStatus = mode.length > 0 ? i18n.tr("Status updated: %1.").arg(modeLabel(mode)) : i18n.tr("Status updated.")
                }
                silentSpaceRefresh = false
                return
            }
            if (command === "Panic") {
                panicSending = false
            }
            commandBusy = false
            lastStatus = i18n.tr("%1 succeeded.").arg(commandLabel(command))
            appendMessage(i18n.tr("Now"), i18n.tr("%1 succeeded for %2.").arg(commandLabel(command)).arg(selectedSystemName))
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
            appendMessage(i18n.tr("Ajax"), message)
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
            appendMessage(i18n.tr("Login"), i18n.tr("Stored session migrated to protected local storage."))
        } else {
            appendMessage(i18n.tr("Login"), i18n.tr("Stored plaintext session migration failed."))
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
        appendMessage(i18n.tr("Login"), i18n.tr("Stored session restored."))
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

        appendMessage(i18n.tr("Login"), remember ? i18n.tr("Developer session saved locally.") : i18n.tr("Developer session active for this run."))
        return true
    }

    function clearStoredSession() {
        secretStore.clearSession()
        clearLegacySessionSettings()
        sessionSettings.spaceId = ""
        spaces = []
        hasStoredSession = false
        lastStatus = i18n.tr("Stored protected session cleared.")
        lastStatusRefresh = ""
        appendMessage(i18n.tr("Login"), i18n.tr("Stored protected session cleared."))
    }

    function logout() {
        qrClient.cancel()
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
        appendMessage(i18n.tr("Login"), i18n.tr("Current session cleared from memory."))
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

    function appendMessage(time, text) {
        var next = messages.slice(0)
        next.unshift({ "time": time, "text": text })
        messages = next
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
            } else if (pendingRefreshAttempts < 6) {
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
            appendMessage(i18n.tr("Login"), i18n.tr("Selected Ajax space: %1.").arg(selectedSystemName))
        }
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
        pendingRefreshAttempts += 1
        refreshStatus(true)
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
            commandBusy = true
            pendingTargetMode = "Armed"
            pendingRefreshAttempts = 0
            apiClient.arm(selectedSpaceId)
        }
    }

    function disarm() {
        if (requireSpace(i18n.tr("Disarm"))) {
            lastStatus = i18n.tr("Sending Disarm command.")
            commandBusy = true
            pendingTargetMode = "Disarmed"
            pendingRefreshAttempts = 0
            apiClient.disarm(selectedSpaceId)
        }
    }

    function enableNightMode() {
        if (requireSpace(i18n.tr("Night mode"))) {
            lastStatus = i18n.tr("Sending Night mode command.")
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

    function fetchMessages() {
        lastStatus = i18n.tr("Showing local messages. Ajax event log endpoint is not implemented yet.")
        return messages
    }
}
