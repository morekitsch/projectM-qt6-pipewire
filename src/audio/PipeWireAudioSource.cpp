#include "PipeWireAudioSource.h"

#ifdef HAVE_PIPEWIRE
#include <pipewire/keys.h>
#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/param/param.h>
#include <spa/pod/builder.h>
#include <spa/utils/dict.h>
#include <spa/utils/result.h>
#endif

#include <algorithm>
#include <cstdint>
#include <cstring>

#include <QByteArray>
#include <QMetaObject>
#include <QSet>

#ifdef HAVE_PIPEWIRE
namespace {
struct PipeWireDeviceProbeContext {
  pw_main_loop *loop = nullptr;
  int syncSeq = -1;
  bool done = false;
  QString error;
  QVector<AudioDeviceInfo> devices;
  QSet<QString> seenIds;
};

bool isAudioMediaClass(const char *mediaClass) {
  if (mediaClass == nullptr || mediaClass[0] == '\0') {
    return false;
  }
  return QString::fromUtf8(mediaClass).startsWith(QStringLiteral("Audio/"));
}

QString displayNameForNode(const spa_dict *props) {
  if (props == nullptr) {
    return QStringLiteral("PipeWire Node");
  }

#ifdef PW_KEY_NODE_DESCRIPTION
  const char *description = spa_dict_lookup(props, PW_KEY_NODE_DESCRIPTION);
  if (description != nullptr && *description != '\0') {
    return QString::fromUtf8(description);
  }
#endif

#ifdef PW_KEY_NODE_NICK
  const char *nick = spa_dict_lookup(props, PW_KEY_NODE_NICK);
  if (nick != nullptr && *nick != '\0') {
    return QString::fromUtf8(nick);
  }
#endif

  const char *name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
  if (name != nullptr && *name != '\0') {
    return QString::fromUtf8(name);
  }

  return QStringLiteral("PipeWire Node");
}

void onProbeRegistryGlobal(void *userdata,
                           uint32_t id,
                           uint32_t permissions,
                           const char *type,
                           uint32_t version,
                           const spa_dict *props) {
  Q_UNUSED(permissions);
  Q_UNUSED(version);

  auto *probe = static_cast<PipeWireDeviceProbeContext *>(userdata);
  if (probe == nullptr || type == nullptr || props == nullptr) {
    return;
  }

  if (std::strcmp(type, PW_TYPE_INTERFACE_Node) != 0) {
    return;
  }

  const char *mediaClass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
  if (!isAudioMediaClass(mediaClass)) {
    return;
  }

  const QString numericId = QString::number(id);
  const char *nodeName = spa_dict_lookup(props, PW_KEY_NODE_NAME);
  const QString persistentId = (nodeName != nullptr && *nodeName != '\0')
                                   ? QString::fromUtf8(nodeName)
                                   : numericId;
  if (probe->seenIds.contains(persistentId)) {
    return;
  }
  probe->seenIds.insert(persistentId);

  AudioDeviceInfo device;
  device.id = persistentId;
  device.name = displayNameForNode(props);

  const QString mediaClassText =
      mediaClass != nullptr ? QString::fromUtf8(mediaClass) : QStringLiteral("Audio");
  if (nodeName != nullptr && *nodeName != '\0') {
    device.description = QStringLiteral("%1 (node=%2, id=%3)")
                             .arg(mediaClassText, QString::fromUtf8(nodeName), numericId);
  } else {
    device.description = QStringLiteral("%1 (id=%2)").arg(mediaClassText, numericId);
  }

  probe->devices.push_back(device);
}

void onProbeCoreDone(void *userdata, uint32_t id, int seq) {
  auto *probe = static_cast<PipeWireDeviceProbeContext *>(userdata);
  if (probe == nullptr) {
    return;
  }

  if (id == PW_ID_CORE && seq == probe->syncSeq) {
    probe->done = true;
    if (probe->loop != nullptr) {
      pw_main_loop_quit(probe->loop);
    }
  }
}

void onProbeCoreError(void *userdata, uint32_t id, int seq, int res, const char *message) {
  Q_UNUSED(id);
  Q_UNUSED(seq);

  if (res >= 0) {
    return;
  }

  auto *probe = static_cast<PipeWireDeviceProbeContext *>(userdata);
  if (probe == nullptr) {
    return;
  }

  QString detail = QString::fromUtf8(spa_strerror(res));
  if (message != nullptr && *message != '\0') {
    detail = QStringLiteral("%1 (%2)").arg(QString::fromUtf8(message), detail);
  }
  probe->error = detail;
  probe->done = true;
  if (probe->loop != nullptr) {
    pw_main_loop_quit(probe->loop);
  }
}
} // namespace
#endif

PipeWireAudioSource::PipeWireAudioSource(QObject *parent) : AudioSource(parent) {}

PipeWireAudioSource::~PipeWireAudioSource() { stop(); }

bool PipeWireAudioSource::start() {
#ifdef HAVE_PIPEWIRE
  if (m_running) {
    return true;
  }

  m_running = true;
  m_loopThread = std::thread(&PipeWireAudioSource::runMainLoop, this);
  Q_EMIT statusMessage(QStringLiteral("Audio backend: PipeWire (initializing)."));
  return true;
#else
  Q_EMIT errorMessage(QStringLiteral("PipeWire backend was not compiled in."));
  return false;
#endif
}

void PipeWireAudioSource::stop() {
#ifdef HAVE_PIPEWIRE
  const bool wasRunning = m_running.exchange(false);
  if (wasRunning && m_mainLoop != nullptr) {
    pw_main_loop_quit(m_mainLoop);
  }

  if (m_loopThread.joinable()) {
    m_loopThread.join();
  }

  if (wasRunning || m_stream != nullptr || m_core != nullptr || m_context != nullptr || m_mainLoop != nullptr) {
    shutdown();
  }
#endif
}

bool PipeWireAudioSource::isRunning() const { return m_running.load(); }

QString PipeWireAudioSource::backendName() const { return QStringLiteral("PipeWire"); }

QVector<AudioDeviceInfo> PipeWireAudioSource::availableDevices() const {
#ifdef HAVE_PIPEWIRE
  return probeDevices(nullptr);
#else
  return {};
#endif
}

QString PipeWireAudioSource::selectedDeviceId() const {
  std::lock_guard<std::mutex> lock(m_deviceMutex);
  return m_selectedDeviceId;
}

void PipeWireAudioSource::setSelectedDeviceId(const QString &deviceId) {
  std::lock_guard<std::mutex> lock(m_deviceMutex);
  m_selectedDeviceId = deviceId.trimmed();
}

void PipeWireAudioSource::onProcess(void *userdata) {
#ifdef HAVE_PIPEWIRE
  auto *self = static_cast<PipeWireAudioSource *>(userdata);
  if (self != nullptr) {
    self->processBuffer();
  }
#else
  Q_UNUSED(userdata);
#endif
}

#ifdef HAVE_PIPEWIRE
QVector<AudioDeviceInfo> PipeWireAudioSource::probeDevices(QString *errorMessage) {
  if (errorMessage != nullptr) {
    errorMessage->clear();
  }

  QVector<AudioDeviceInfo> devices;
  pw_init(nullptr, nullptr);

  pw_main_loop *loop = pw_main_loop_new(nullptr);
  if (loop == nullptr) {
    if (errorMessage != nullptr) {
      *errorMessage = QStringLiteral("Failed to create PipeWire loop for device probe.");
    }
    pw_deinit();
    return devices;
  }

  pw_context *context = pw_context_new(pw_main_loop_get_loop(loop), nullptr, 0);
  if (context == nullptr) {
    if (errorMessage != nullptr) {
      *errorMessage = QStringLiteral("Failed to create PipeWire context for device probe.");
    }
    pw_main_loop_destroy(loop);
    pw_deinit();
    return devices;
  }

  pw_core *core = pw_context_connect(context, nullptr, 0);
  if (core == nullptr) {
    if (errorMessage != nullptr) {
      *errorMessage = QStringLiteral("Failed to connect to PipeWire core for device probe.");
    }
    pw_context_destroy(context);
    pw_main_loop_destroy(loop);
    pw_deinit();
    return devices;
  }

  auto *registry = static_cast<pw_registry *>(pw_core_get_registry(core, PW_VERSION_REGISTRY, 0));
  if (registry == nullptr) {
    if (errorMessage != nullptr) {
      *errorMessage = QStringLiteral("Failed to get PipeWire registry for device probe.");
    }
    pw_core_disconnect(core);
    pw_context_destroy(context);
    pw_main_loop_destroy(loop);
    pw_deinit();
    return devices;
  }

  PipeWireDeviceProbeContext probe;
  probe.loop = loop;
  spa_hook registryListener = {};
  spa_hook coreListener = {};

  pw_registry_events registryEvents = {};
  registryEvents.version = PW_VERSION_REGISTRY_EVENTS;
  registryEvents.global = &onProbeRegistryGlobal;
  pw_registry_add_listener(registry, &registryListener, &registryEvents, &probe);

  pw_core_events coreEvents = {};
  coreEvents.version = PW_VERSION_CORE_EVENTS;
  coreEvents.done = &onProbeCoreDone;
  coreEvents.error = &onProbeCoreError;
  pw_core_add_listener(core, &coreListener, &coreEvents, &probe);

  probe.syncSeq = pw_core_sync(core, PW_ID_CORE, 0);
  int maxIterations = 50;
  while (!probe.done && maxIterations-- > 0) {
    if (pw_loop_iterate(pw_main_loop_get_loop(loop), 50) < 0) {
      break;
    }
  }
  if (!probe.done && probe.error.isEmpty()) {
    probe.error = QStringLiteral("Timed out while enumerating PipeWire devices.");
  }

  spa_hook_remove(&registryListener);
  spa_hook_remove(&coreListener);

  if (errorMessage != nullptr && !probe.error.isEmpty()) {
    *errorMessage = probe.error;
  }

  devices = probe.devices;
  std::sort(devices.begin(), devices.end(), [](const AudioDeviceInfo &left, const AudioDeviceInfo &right) {
    return QString::compare(left.name, right.name, Qt::CaseInsensitive) < 0;
  });

  pw_core_disconnect(core);
  pw_context_destroy(context);
  pw_main_loop_destroy(loop);
  pw_deinit();
  return devices;
}
#endif

void PipeWireAudioSource::processBuffer() {
#ifdef HAVE_PIPEWIRE
  if (m_stream == nullptr) {
    return;
  }

  pw_buffer *buffer = pw_stream_dequeue_buffer(m_stream);
  if (buffer == nullptr || buffer->buffer == nullptr || buffer->buffer->n_datas == 0) {
    return;
  }

  spa_data &data = buffer->buffer->datas[0];
  if (data.data == nullptr || data.chunk == nullptr || data.chunk->size == 0) {
    pw_stream_queue_buffer(m_stream, buffer);
    return;
  }

  const int bytesPerSample = static_cast<int>(sizeof(float));
  const int frameStride = bytesPerSample * m_channels;
  if (frameStride <= 0) {
    pw_stream_queue_buffer(m_stream, buffer);
    return;
  }

  if (data.chunk->offset >= data.maxsize) {
    pw_stream_queue_buffer(m_stream, buffer);
    return;
  }

  const auto *rawData = static_cast<const uint8_t *>(data.data);
  const uint32_t available = data.maxsize - data.chunk->offset;
  const uint32_t byteCount = std::min<uint32_t>(data.chunk->size, available);
  const int stride = data.chunk->stride > 0 ? static_cast<int>(data.chunk->stride) : frameStride;
  if (stride < frameStride || byteCount == 0U) {
    pw_stream_queue_buffer(m_stream, buffer);
    return;
  }

  const int frameCount = static_cast<int>(byteCount / static_cast<uint32_t>(stride));
  if (frameCount <= 0) {
    pw_stream_queue_buffer(m_stream, buffer);
    return;
  }

  const auto *chunkData = rawData + data.chunk->offset;
  QVector<float> mono(frameCount);
  for (int i = 0; i < frameCount; ++i) {
    const auto *frameSamples =
        reinterpret_cast<const float *>(chunkData + static_cast<size_t>(i) * static_cast<size_t>(stride));
    float accum = 0.0f;
    for (int c = 0; c < m_channels; ++c) {
      accum += frameSamples[c];
    }
    mono[i] = accum / static_cast<float>(m_channels);
  }

  pw_stream_queue_buffer(m_stream, buffer);

  QMetaObject::invokeMethod(this,
                            [this, mono]() { Q_EMIT pcmFrameReady(mono); },
                            Qt::QueuedConnection);
#endif
}

#ifdef HAVE_PIPEWIRE
void PipeWireAudioSource::onStateChanged(void *userdata,
                                         enum pw_stream_state oldState,
                                         enum pw_stream_state state,
                                         const char *error) {
  Q_UNUSED(oldState);
  auto *self = static_cast<PipeWireAudioSource *>(userdata);
  if (self == nullptr) {
    return;
  }

  if (state == PW_STREAM_STATE_STREAMING) {
    QMetaObject::invokeMethod(
        self,
        [self]() {
          Q_EMIT self->statusMessage(
              QStringLiteral("PipeWire stream active (%1 Hz, %2 channels).")
                  .arg(self->m_sampleRate)
                  .arg(self->m_channels));
        },
        Qt::QueuedConnection);
    return;
  }

  if (state == PW_STREAM_STATE_ERROR) {
    const QString detail =
        error != nullptr ? QString::fromUtf8(error) : QStringLiteral("unknown stream error");
    QMetaObject::invokeMethod(
        self,
        [self, detail]() { Q_EMIT self->errorMessage(QStringLiteral("PipeWire stream error: %1").arg(detail)); },
        Qt::QueuedConnection);
    self->m_running = false;
    if (self->m_mainLoop != nullptr) {
      pw_main_loop_quit(self->m_mainLoop);
    }
  }
}

void PipeWireAudioSource::onCoreError(void *userdata, uint32_t id, int seq, int res, const char *message) {
  Q_UNUSED(id);
  Q_UNUSED(seq);

  if (res >= 0) {
    return;
  }

  auto *self = static_cast<PipeWireAudioSource *>(userdata);
  if (self == nullptr) {
    return;
  }

  QString detail = QString::fromUtf8(spa_strerror(res));
  if (message != nullptr && *message != '\0') {
    detail = QStringLiteral("%1 (%2)").arg(QString::fromUtf8(message), detail);
  }

  QMetaObject::invokeMethod(
      self,
      [self, detail]() { Q_EMIT self->errorMessage(QStringLiteral("PipeWire core error: %1").arg(detail)); },
      Qt::QueuedConnection);
  self->m_running = false;
  if (self->m_mainLoop != nullptr) {
    pw_main_loop_quit(self->m_mainLoop);
  }
}
#endif

void PipeWireAudioSource::runMainLoop() {
#ifdef HAVE_PIPEWIRE
  pw_init(nullptr, nullptr);

  const auto fail = [this](const QString &message) {
    QMetaObject::invokeMethod(this, [this, message]() { Q_EMIT errorMessage(message); }, Qt::QueuedConnection);
    m_running = false;
    shutdown();
  };

  m_mainLoop = pw_main_loop_new(nullptr);
  if (m_mainLoop == nullptr) {
    fail(QStringLiteral("Failed to create PipeWire main loop."));
    return;
  }

  m_context = pw_context_new(pw_main_loop_get_loop(m_mainLoop), nullptr, 0);
  if (m_context == nullptr) {
    fail(QStringLiteral("Failed to create PipeWire context."));
    return;
  }

  m_core = pw_context_connect(m_context, nullptr, 0);
  if (m_core == nullptr) {
    fail(QStringLiteral("Failed to connect to PipeWire core."));
    return;
  }

  pw_properties *properties = pw_properties_new(PW_KEY_MEDIA_TYPE,
                                                "Audio",
                                                PW_KEY_MEDIA_CATEGORY,
                                                "Capture",
                                                PW_KEY_MEDIA_ROLE,
                                                "Music",
                                                PW_KEY_APP_NAME,
                                                "qt6mplayer",
                                                nullptr);
  if (properties == nullptr) {
    fail(QStringLiteral("Failed to allocate PipeWire stream properties."));
    return;
  }

  QString selectedDevice;
  {
    std::lock_guard<std::mutex> lock(m_deviceMutex);
    selectedDevice = m_selectedDeviceId.trimmed();
  }
  const QString envTarget = QString::fromUtf8(qgetenv("QT6MPLAYER_PIPEWIRE_TARGET")).trimmed();
  const QString configuredTarget = envTarget.isEmpty() ? selectedDevice : envTarget;
  const QByteArray targetObject = configuredTarget.toUtf8();
#ifdef PW_KEY_TARGET_OBJECT
  if (!targetObject.isEmpty()) {
    pw_properties_set(properties, PW_KEY_TARGET_OBJECT, targetObject.constData());
  }
#else
  Q_UNUSED(targetObject);
#endif
#ifdef PW_KEY_STREAM_CAPTURE_SINK
  pw_properties_set(properties, PW_KEY_STREAM_CAPTURE_SINK, "true");
#endif

  m_stream = pw_stream_new(m_core, "qt6mplayer-input", properties);
  if (m_stream == nullptr) {
    fail(QStringLiteral("Failed to create PipeWire stream."));
    return;
  }

  pw_stream_events events = {};
  events.version = PW_VERSION_STREAM_EVENTS;
  events.state_changed = &PipeWireAudioSource::onStateChanged;
  events.process = &PipeWireAudioSource::onProcess;

  pw_stream_add_listener(m_stream, &m_streamListener, &events, this);
  m_streamListenerAttached = true;

  pw_core_events coreEvents = {};
  coreEvents.version = PW_VERSION_CORE_EVENTS;
  coreEvents.error = &PipeWireAudioSource::onCoreError;
  pw_core_add_listener(m_core, &m_coreListener, &coreEvents, this);
  m_coreListenerAttached = true;

  uint8_t paramsBuffer[256];
  spa_pod_builder builder = SPA_POD_BUILDER_INIT(paramsBuffer, sizeof(paramsBuffer));

  spa_audio_info_raw info = {};
  info.format = SPA_AUDIO_FORMAT_F32;
  info.rate = m_sampleRate;
  info.channels = static_cast<uint32_t>(m_channels);
  info.position[0] = SPA_AUDIO_CHANNEL_FL;
  info.position[1] = SPA_AUDIO_CHANNEL_FR;

  const spa_pod *params[1];
  params[0] = spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info);

  const int result = pw_stream_connect(
      m_stream,
      PW_DIRECTION_INPUT,
      PW_ID_ANY,
      static_cast<pw_stream_flags>(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS |
                                   PW_STREAM_FLAG_RT_PROCESS),
      params,
      1);

  if (result < 0) {
    fail(QStringLiteral("Failed to connect PipeWire stream: %1").arg(QString::fromUtf8(spa_strerror(result))));
    return;
  }

  pw_main_loop_run(m_mainLoop);
  const bool stoppedByRequest = !m_running.exchange(false);
  if (!stoppedByRequest) {
    shutdown();
  }
#else
  QMetaObject::invokeMethod(this,
                            [this]() { Q_EMIT errorMessage(QStringLiteral("PipeWire support unavailable.")); },
                            Qt::QueuedConnection);
#endif
}

void PipeWireAudioSource::shutdown() {
#ifdef HAVE_PIPEWIRE
  if (m_streamListenerAttached) {
    spa_hook_remove(&m_streamListener);
    m_streamListenerAttached = false;
  }

  if (m_coreListenerAttached) {
    spa_hook_remove(&m_coreListener);
    m_coreListenerAttached = false;
  }

  if (m_stream != nullptr) {
    pw_stream_destroy(m_stream);
    m_stream = nullptr;
  }

  if (m_core != nullptr) {
    pw_core_disconnect(m_core);
    m_core = nullptr;
  }

  if (m_context != nullptr) {
    pw_context_destroy(m_context);
    m_context = nullptr;
  }

  if (m_mainLoop != nullptr) {
    pw_main_loop_destroy(m_mainLoop);
    m_mainLoop = nullptr;
  }

  pw_deinit();
#endif
}
