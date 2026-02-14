#pragma once

#include <QObject>
#include <QString>
#include <QVector>

struct AudioDeviceInfo {
  QString id;
  QString name;
  QString description;
};

class AudioSource : public QObject {
  Q_OBJECT

public:
  explicit AudioSource(QObject *parent = nullptr) : QObject(parent) {}
  ~AudioSource() override = default;

  virtual bool start() = 0;
  virtual void stop() = 0;
  virtual bool isRunning() const = 0;
  virtual QString backendName() const = 0;
  virtual QVector<AudioDeviceInfo> availableDevices() const = 0;
  virtual QString selectedDeviceId() const = 0;
  virtual void setSelectedDeviceId(const QString &deviceId) = 0;

Q_SIGNALS:
  void pcmFrameReady(const QVector<float> &monoFrame);
  void statusMessage(const QString &message);
  void errorMessage(const QString &message);
};
