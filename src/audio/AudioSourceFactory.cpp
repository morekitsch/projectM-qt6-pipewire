#include "AudioSourceFactory.h"

#include "DummyAudioSource.h"
#include "PipeWireAudioSource.h"

AudioSource *createAudioSource(QObject *parent) {
#ifdef HAVE_PIPEWIRE
  return new PipeWireAudioSource(parent);
#else
  return new DummyAudioSource(parent);
#endif
}
