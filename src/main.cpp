#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QSurfaceFormat>

int main(int argc, char *argv[]) {
  QCoreApplication::setAttribute(Qt::AA_UseDesktopOpenGL);

  QSurfaceFormat format = QSurfaceFormat::defaultFormat();
  format.setRenderableType(QSurfaceFormat::OpenGL);
  format.setVersion(3, 3);
  format.setProfile(QSurfaceFormat::CoreProfile);
  QSurfaceFormat::setDefaultFormat(format);

  QApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("projectM-community"));
  QCoreApplication::setApplicationName(QStringLiteral("qt6mplayer"));

  MainWindow window;
  window.show();

  return app.exec();
}
