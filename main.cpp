#include "setupwindow.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <qwidget.h>


int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    QTranslator translator;
    const QStringList uiLanguages = QLocale::system().uiLanguages();
    for (const QString &locale : uiLanguages) {
        const QString baseName = "cutefin_" + QLocale(locale).name();
        if (translator.load(":/i18n/" + baseName)) {
            a.installTranslator(&translator);
            break;
        }
    }

    SetupWindow *setupWindow = new SetupWindow();
    return a.exec();
}
