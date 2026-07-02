import QtQuick 2.7
import Lomiri.Components 1.3
import "backend"

MainView {
    id: root
    objectName: "mainView"
    applicationName: "ajaxremote.cloudsite"
    automaticOrientation: true

    width: desktopLarge ? units.gu(45) : units.gu(85)
    height: desktopLarge ? units.gu(80) : units.gu(80)

    Component.onCompleted: {
        if (desktopDarkMode) {
            theme.name = "Ubuntu.Components.Themes.SuruDark"
        }
    }

    AjaxController {
        id: ajaxController
    }

    PageStack {
        id: pageStack
        anchors.fill: parent

        Component.onCompleted: push(Qt.resolvedUrl("pages/RemotePage.qml"), {
            "ajaxController": ajaxController
        })
    }
}
