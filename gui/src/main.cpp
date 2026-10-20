#include <QApplication>
#include "MainWindow.h"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    MainWindow window;
    window.showMaximized(); // Darstellungsebene fuellt den Bildschirm

    return app.exec();
}
