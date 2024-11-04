#include "loginscreen.h"
#include "serverselect.h"

#include <QApplication>
#include <QLocale>
#include <QTranslator>
#include <QVBoxLayout>

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

    QWidget *mainWidget = new QWidget();
    QVBoxLayout *layout = new QVBoxLayout();

    ServerSelect *serverSelect = new ServerSelect();
    layout->addWidget(serverSelect);
    mainWidget->setLayout(layout);
    mainWidget->setMinimumSize(300, 300);
    mainWidget->show();
    return a.exec();
}
