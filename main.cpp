#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QSize>
#include <QUrl>
#include <QtQml>

#include "src/AjaxApiClient.h"
#include "src/LocalSecretStore.h"
#include "src/QrLoginClient.h"

#ifndef AJAXREMOTE_VERSION
#define AJAXREMOTE_VERSION "development"
#endif

int main(int argc, char *argv[])
{
    const bool desktopLarge = qEnvironmentVariableIsSet("CLICKABLE_DESKTOP_MODE");
    if (desktopLarge) {
        qputenv("QT_AUTO_SCREEN_SCALE_FACTOR", "0");
        qputenv("QT_SCALE_FACTOR", "2");
        qputenv("QT_FONT_DPI", "192");
        qputenv("GRID_UNIT_PX", "24");
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("ajaxremote.cloudsite"));
    qmlRegisterType<AjaxApiClient>("AjaxRemote.Backend", 1, 0, "AjaxApiClient");
    qmlRegisterType<LocalSecretStore>("AjaxRemote.Backend", 1, 0, "LocalSecretStore");
    qmlRegisterType<QrLoginClient>("AjaxRemote.Backend", 1, 0, "QrLoginClient");

    QQuickView view;
    view.engine()->addImportPath(QStringLiteral("qrc:/qml"));
    view.rootContext()->setContextProperty(QStringLiteral("ajaxRemoteAppVersion"), QStringLiteral(AJAXREMOTE_VERSION));
    view.rootContext()->setContextProperty(QStringLiteral("ajaxRemoteInstallDir"), QCoreApplication::applicationDirPath());
    view.rootContext()->setContextProperty(QStringLiteral("desktopLarge"), desktopLarge);
    view.rootContext()->setContextProperty(QStringLiteral("desktopDarkMode"),
        desktopLarge && !qEnvironmentVariableIsSet("AJAXREMOTE_DESKTOP_LIGHT_MODE"));
    view.setSource(QUrl(QStringLiteral("qrc:/Main.qml")));
    view.setResizeMode(QQuickView::SizeRootObjectToView);
    if (desktopLarge) {
        view.resize(QSize(540, 960));
    }
    view.show();

    return app.exec();
}
