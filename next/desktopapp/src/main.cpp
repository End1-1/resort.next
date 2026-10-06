#include "appconfig.h"
#include "appcontroller.h"
#include "uilanguage.h"

#include <QApplication>

int main(int argc, char **argv)
{
    AppConfig::applyIdentity();
    QApplication app(argc, argv);
    HotelLocale::installUiFont();

    AppController controller;
    controller.start();
    return app.exec();
}
