#pragma once

#include "AudioSource.h"

#ifdef HAVE_PIPEWIRE
#include <pipewire/core.h>
#include <pipewire/stream.h>
#include <spa/utils/hook.h>
#endif

#include <atomic>
#include <mutex>
#include <thread>

struct pw_main_loop;
struct pw_context;
struct pw_core;
struct pw_stream;

class PipeWireAudioSource : public AudioSource {
  Q_OBJECT

public:
  explicit PipeWireAudioSource(QObject *parent = nullptr);
  ~PipeWireAudioSource() override;

  bool start() override;
  void stop() override;
  bool isRunning() const override;
  QString backendName() const override;
  QVector<AudioDeviceInfo> availableDevices() const override;
  QString selectedDeviceId() const override;
  void setSelectedDeviceId(const QString &deviceId) override;

private:
  static void onProcess(void *userdata);
#ifdef HAVE_PIPEWIRE
  static void onStateChanged(void *userdata,
                             enum pw_stream_state oldState,
                             enum pw_stream_state state,
                             const char *error);
  static void onCoreError(void *userdata, uint32_t id, int seq, int res, const char *message);
#endif
  void processBuffer();
  void runMainLoop();
  void shutdown();
#ifdef HAVE_PIPEWIRE
  static QVector<AudioDeviceInfo> probeDevices(QString *errorMessage);
#endif

  std::atomic<bool> m_running{false};
  std::thread m_loopThread;
  mutable std::mutex m_deviceMutex;
  QString m_selectedDeviceId;

  pw_main_loop *m_mainLoop = nullptr;
  pw_context *m_context = nullptr;
  pw_core *m_core = nullptr;
  pw_stream *m_stream = nullptr;

#ifdef HAVE_PIPEWIRE
  spa_hook m_streamListener = {};
  spa_hook m_coreListener = {};
  bool m_streamListenerAttached = false;
  bool m_coreListenerAttached = false;
#endif
  int m_sampleRate = 48000;
  int m_channels = 2;
};
