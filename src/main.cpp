#include "MainWindow.h"

#include <QApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QSettings>
#include <QSurfaceFormat>

namespace {
QString normalizeGpuPreference(const QString &value) {
  const QString normalized = value.trimmed().toLower();
  if (normalized.isEmpty()) {
    return {};
  }
  if (normalized == QStringLiteral("auto")) {
    return normalized;
  }
  if (normalized == QStringLiteral("dgpu") || normalized == QStringLiteral("discrete") ||
      normalized == QStringLiteral("d")) {
    return QStringLiteral("dgpu");
  }
  if (normalized == QStringLiteral("igpu") || normalized == QStringLiteral("integrated") ||
      normalized == QStringLiteral("i")) {
    return QStringLiteral("igpu");
  }
  return {};
}

QString resolveGpuPreference() {
  const QString envPreference = normalizeGpuPreference(qEnvironmentVariable("QT6MPLAYER_GPU"));
  if (!envPreference.isEmpty()) {
    return envPreference;
  }

  QSettings settings(QSettings::NativeFormat,
                     QSettings::UserScope,
                     QStringLiteral("projectm-community"),
                     QStringLiteral("qt6mplayer"));
  settings.beginGroup(QStringLiteral("projectm"));
  const QString savedPreference = normalizeGpuPreference(
      settings.value(QStringLiteral("gpuPreference"), QStringLiteral("dgpu")).toString());
  settings.endGroup();
  return savedPreference.isEmpty() ? QStringLiteral("dgpu") : savedPreference;
}

void applyGpuPreference() {
  const QString preference = resolveGpuPreference();
  if (!qEnvironmentVariableIsSet("DRI_PRIME")) {
    if (preference == QStringLiteral("dgpu")) {
      qputenv("DRI_PRIME", QByteArrayLiteral("1"));
    } else if (preference == QStringLiteral("igpu")) {
      qputenv("DRI_PRIME", QByteArrayLiteral("0"));
    }
  }

  if (preference == QStringLiteral("dgpu") &&
      QFileInfo(QStringLiteral("/proc/driver/nvidia/version")).exists()) {
    if (!qEnvironmentVariableIsSet("__NV_PRIME_RENDER_OFFLOAD")) {
      qputenv("__NV_PRIME_RENDER_OFFLOAD", QByteArrayLiteral("1"));
    }
    if (!qEnvironmentVariableIsSet("__GLX_VENDOR_LIBRARY_NAME")) {
      qputenv("__GLX_VENDOR_LIBRARY_NAME", QByteArrayLiteral("nvidia"));
    }
  }
}

void applyQtPlatformPreference() {
  if (qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) {
    return;
  }

  const QByteArray displayValue = qgetenv("DISPLAY");
  if (!displayValue.isEmpty()) {
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("xcb"));
  }
}
} // namespace

int main(int argc, char *argv[]) {
  applyQtPlatformPreference();
  applyGpuPreference();
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
