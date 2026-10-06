#include "appconfig.h"
#include "appcontroller.h"

#include <QApplication>
#include <QGuiApplication>

int main(int argc, char **argv)
{
    AppConfig::applyIdentity();
    QApplication app(argc, argv);
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Отель"));

    AppController controller;
    controller.start();
    return app.exec();
}
