#include "ProjectMEngine.h"

#ifdef HAVE_PROJECTM
#include <projectM-4/audio.h>
#include <projectM-4/parameters.h>
#include <projectM-4/render_opengl.h>
#endif

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
  if (m_projectM == nullptr) {
    m_projectM = projectm_create();
    if (m_projectM == nullptr) {
      Q_EMIT statusMessage(QStringLiteral("projectM initialization failed (OpenGL context?)."));
      return false;
    }

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
