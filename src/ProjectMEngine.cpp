#include "ProjectMEngine.h"

#include <QMetaObject>
#include <QOpenGLContext>
#include <QOpenGLFunctions>
#include <QRegularExpression>
#include <QStringList>

#ifdef HAVE_PROJECTM
#include <projectM-4/audio.h>
#include <projectM-4/callbacks.h>
#include <projectM-4/parameters.h>
#include <projectM-4/render_opengl.h>
#endif

namespace {
int parseMajorVersion(const QString &versionText) {
  const QString trimmed = versionText.trimmed();
  const QStringList parts = trimmed.split(QRegularExpression(QStringLiteral("[^0-9]+")),
                                          Qt::SkipEmptyParts);
  if (parts.isEmpty()) {
    return -1;
  }
  bool ok = false;
  const int major = parts.first().toInt(&ok);
  return ok ? major : -1;
}

#ifdef HAVE_PROJECTM
void onPresetSwitchFailed(const char *presetFilename, const char *message, void *userData) {
  auto *engine = static_cast<ProjectMEngine *>(userData);
  if (engine == nullptr) {
    return;
  }

  const QString fileText = QString::fromUtf8(presetFilename != nullptr ? presetFilename : "");
  const QString messageText = QString::fromUtf8(message != nullptr ? message : "unknown");

  QMetaObject::invokeMethod(
      engine,
      [engine, fileText, messageText]() {
        Q_EMIT engine->statusMessage(
            QStringLiteral("Preset load failed: %1 (%2)").arg(fileText, messageText));
      },
      Qt::QueuedConnection);
}
#endif
} // namespace

ProjectMEngine::ProjectMEngine(QObject *parent) : QObject(parent) {}

ProjectMEngine::~ProjectMEngine() {
#ifdef HAVE_PROJECTM
  if (m_projectM != nullptr) {
    projectm_destroy(m_projectM);
    m_projectM = nullptr;
  }
#endif
}

void ProjectMEngine::setPresetDirectory(const QString &directory) {
  m_presetDirectory = directory;
  m_pendingTexturePath = directory;
}

QString ProjectMEngine::presetDirectory() const { return m_presetDirectory; }

bool ProjectMEngine::loadPreset(const QString &presetPath) {
  if (presetPath.isEmpty()) {
    return false;
  }

  m_activePreset = presetPath;
  m_pendingPresetToLoad = presetPath;
  Q_EMIT presetChanged(m_activePreset);
  Q_EMIT statusMessage(QStringLiteral("Loaded preset: %1").arg(m_activePreset));
  return true;
}

QString ProjectMEngine::activePreset() const { return m_activePreset; }

void ProjectMEngine::applySettings(const QVariantMap &settings) {
  m_settings = settings;
  m_settingsDirty = true;
  Q_EMIT statusMessage(QStringLiteral("Updated projectM settings."));
}

QVariantMap ProjectMEngine::settings() const { return m_settings; }

bool ProjectMEngine::initializeRenderer(int width, int height) {
  m_rendererReady = true;

#ifdef HAVE_PROJECTM
  const QOpenGLContext *ctx = QOpenGLContext::currentContext();
  if (ctx != nullptr && ctx->isOpenGLES()) {
    Q_EMIT statusMessage(
        QStringLiteral("OpenGL ES context detected; projectM requires desktop OpenGL. Using fallback preview."));
    return false;
  }
  if (ctx != nullptr) {
    QOpenGLFunctions *functions = ctx->functions();
    const QString glVersion =
        functions != nullptr
            ? QString::fromUtf8(
                  reinterpret_cast<const char *>(functions->glGetString(GL_VERSION)))
            : QStringLiteral("unknown");
    const QString glslVersion =
        functions != nullptr
            ? QString::fromUtf8(
                  reinterpret_cast<const char *>(functions->glGetString(GL_SHADING_LANGUAGE_VERSION)))
            : QStringLiteral("unknown");
    Q_EMIT statusMessage(QStringLiteral("OpenGL context: GL=%1, GLSL=%2").arg(glVersion, glslVersion));

    const int glslMajor = parseMajorVersion(glslVersion);
    if (glslMajor > 0 && glslMajor < 3) {
      Q_EMIT statusMessage(
          QStringLiteral("GLSL version too old for projectM (need >= 3). Using fallback preview."));
      return false;
    }
  }

  if (m_projectM == nullptr) {
    m_projectM = projectm_create();
    if (m_projectM == nullptr) {
      Q_EMIT statusMessage(QStringLiteral("projectM initialization failed (OpenGL context?)."));
      return false;
    }
    projectm_set_preset_switch_failed_event_callback(m_projectM, &onPresetSwitchFailed, this);
    projectm_load_preset_file(m_projectM, "idle://", false);
  }

  projectm_set_window_size(m_projectM, width, height);
  m_settingsDirty = true;
  m_pendingTexturePath = m_presetDirectory;
  if (!m_activePreset.isEmpty()) {
    m_pendingPresetToLoad = m_activePreset;
  }

  Q_EMIT statusMessage(QStringLiteral("projectM OpenGL renderer active."));
  return true;
#else
  Q_UNUSED(width);
  Q_UNUSED(height);
  Q_EMIT statusMessage(QStringLiteral("projectM library not detected. Running preview fallback."));
  return false;
#endif
}

void ProjectMEngine::resizeRenderer(int width, int height) {
#ifdef HAVE_PROJECTM
  if (m_projectM != nullptr) {
    projectm_set_window_size(m_projectM, width, height);
  }
#else
  Q_UNUSED(width);
  Q_UNUSED(height);
#endif
}

bool ProjectMEngine::renderFrame(uint32_t framebufferObject) {
#ifdef HAVE_PROJECTM
  if (m_projectM == nullptr) {
    return false;
  }

  applyPendingState();
  Q_UNUSED(framebufferObject);
  projectm_opengl_render_frame(m_projectM);
  return true;
#else
  Q_UNUSED(framebufferObject);
  return false;
#endif
}

bool ProjectMEngine::hasProjectMBackend() const {
#ifdef HAVE_PROJECTM
  return m_projectM != nullptr;
#else
  return false;
#endif
}

void ProjectMEngine::resetRenderer() {
#ifdef HAVE_PROJECTM
  if (m_projectM != nullptr) {
    projectm_destroy(m_projectM);
    m_projectM = nullptr;
  }
#endif
  m_rendererReady = false;
  m_settingsDirty = true;
  m_pendingTexturePath = m_presetDirectory;
  if (!m_activePreset.isEmpty()) {
    m_pendingPresetToLoad = m_activePreset;
  }
}

void ProjectMEngine::submitAudioFrame(const QVector<float> &monoFrame) {
#ifdef HAVE_PROJECTM
  if (m_projectM != nullptr && !monoFrame.isEmpty()) {
    projectm_pcm_add_float(m_projectM,
                           monoFrame.constData(),
                           static_cast<unsigned int>(monoFrame.size()),
                           PROJECTM_MONO);
  }
#endif

  Q_EMIT frameReady(monoFrame);
}

void ProjectMEngine::applySettingsToBackend() {
#ifdef HAVE_PROJECTM
  if (m_projectM == nullptr) {
    return;
  }

  projectm_set_mesh_size(m_projectM,
                         static_cast<unsigned int>(m_settings.value(QStringLiteral("meshX"), 32).toUInt()),
                         static_cast<unsigned int>(m_settings.value(QStringLiteral("meshY"), 24).toUInt()));
  projectm_set_fps(m_projectM,
                   static_cast<unsigned int>(m_settings.value(QStringLiteral("targetFps"), 60).toUInt()));
  projectm_set_beat_sensitivity(m_projectM,
                                static_cast<float>(m_settings.value(QStringLiteral("beatSensitivity"), 1.0)
                                                      .toDouble()));
  projectm_set_hard_cut_enabled(m_projectM,
                                m_settings.value(QStringLiteral("hardCutEnabled"), true).toBool());
  projectm_set_hard_cut_duration(m_projectM,
                                 static_cast<unsigned int>(m_settings.value(QStringLiteral("hardCutDuration"), 20)
                                                               .toUInt()));
#endif
}

void ProjectMEngine::applyPendingState() {
#ifdef HAVE_PROJECTM
  if (m_projectM == nullptr) {
    return;
  }

  if (!m_pendingTexturePath.isEmpty()) {
    const QByteArray bytes = m_pendingTexturePath.toUtf8();
    const char *paths[] = {bytes.constData()};
    projectm_set_texture_search_paths(m_projectM, paths, 1);
    m_pendingTexturePath.clear();
  }

  if (m_settingsDirty) {
    applySettingsToBackend();
    m_settingsDirty = false;
  }

  if (!m_pendingPresetToLoad.isEmpty()) {
    const QByteArray bytes = m_pendingPresetToLoad.toUtf8();
    projectm_load_preset_file(m_projectM, bytes.constData(), true);
    m_pendingPresetToLoad.clear();
  }
#endif
}
