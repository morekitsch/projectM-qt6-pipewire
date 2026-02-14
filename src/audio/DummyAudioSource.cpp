#include "DummyAudioSource.h"

#include <QtMath>

DummyAudioSource::DummyAudioSource(QObject *parent) : AudioSource(parent) {
  m_timer.setInterval(16);
  connect(&m_timer, &QTimer::timeout, this, [this]() {
    QVector<float> frame(512);
    static float phase = 0.0f;
    for (int i = 0; i < frame.size(); ++i) {
      frame[i] = qSin(phase);
      phase += 0.07f;
    }
    Q_EMIT pcmFrameReady(frame);
  });
}

bool DummyAudioSource::start() {
  if (m_running) {
    return true;
  }
  m_running = true;
  m_timer.start();
  Q_EMIT statusMessage(QStringLiteral("Audio backend: dummy signal (PipeWire unavailable)."));
  return true;
}

void DummyAudioSource::stop() {
  if (!m_running) {
    return;
  }
  m_running = false;
  m_timer.stop();
}

bool DummyAudioSource::isRunning() const { return m_running; }

QString DummyAudioSource::backendName() const { return QStringLiteral("Dummy"); }

QVector<AudioDeviceInfo> DummyAudioSource::availableDevices() const {
  AudioDeviceInfo device;
  device.id = QStringLiteral("dummy");
  device.name = QStringLiteral("Synthetic Signal");
  device.description = QStringLiteral("Built-in dummy generator");
  return {device};
}

QString DummyAudioSource::selectedDeviceId() const { return m_selectedDeviceId; }

void DummyAudioSource::setSelectedDeviceId(const QString &deviceId) { m_selectedDeviceId = deviceId; }
