#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QVector>
#include <cstdint>

#ifdef HAVE_PROJECTM
#include <projectM-4/projectM.h>
#endif

class ProjectMEngine : public QObject {
  Q_OBJECT

public:
  explicit ProjectMEngine(QObject *parent = nullptr);
  ~ProjectMEngine() override;

  void setPresetDirectory(const QString &directory);
  QString presetDirectory() const;

  bool loadPreset(const QString &presetPath);
  QString activePreset() const;

  void applySettings(const QVariantMap &settings);
  QVariantMap settings() const;

  bool initializeRenderer(int width, int height);
  void resizeRenderer(int width, int height);
  bool renderFrame(uint32_t framebufferObject = 0);
  bool hasProjectMBackend() const;
  void resetRenderer();

public Q_SLOTS:
  void submitAudioFrame(const QVector<float> &monoFrame);

Q_SIGNALS:
  void statusMessage(const QString &message);
  void presetChanged(const QString &presetPath);
  void frameReady(const QVector<float> &monoFrame);

private:
  void applySettingsToBackend();
  void applyPendingState();

  QString m_presetDirectory;
  QString m_activePreset;
  QVariantMap m_settings;
  QString m_pendingPresetToLoad;
  QString m_pendingTexturePath;
  bool m_settingsDirty = false;

#ifdef HAVE_PROJECTM
  projectm_handle m_projectM = nullptr;
#endif
  bool m_rendererReady = false;
};
