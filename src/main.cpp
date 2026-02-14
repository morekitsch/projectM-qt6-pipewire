#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>

int main(int argc, char *argv[]) {
  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("projectM-community"));
  QCoreApplication::setApplicationName(QStringLiteral("qt6mplayer"));

  MainWindow window;
  window.show();

  return app.exec();
}
