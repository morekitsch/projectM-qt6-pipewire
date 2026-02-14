#pragma once

#include "AudioSource.h"

#include <QTimer>

class DummyAudioSource : public AudioSource {
  Q_OBJECT

public:
  explicit DummyAudioSource(QObject *parent = nullptr);

  bool start() override;
  void stop() override;
  bool isRunning() const override;
  QString backendName() const override;
  QVector<AudioDeviceInfo> availableDevices() const override;
  QString selectedDeviceId() const override;
  void setSelectedDeviceId(const QString &deviceId) override;

private:
  QTimer m_timer;
  bool m_running = false;
  QString m_selectedDeviceId;
};
