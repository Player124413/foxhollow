#include <assert.h>
#include <stdint.h>
#include "../port/src/musyx_output.c"

static SDL_AudioStreamCallback feed;
static SDL_AudioStream* const output = (SDL_AudioStream*)(uintptr_t)1;
static unsigned samples;
static unsigned received;
static unsigned opened;
static unsigned closed;
static int active;
static bool renderOk = true;
static bool resumeOk = true;
static AIDCallback dmaCallback;
static int dmaRunning;
static void (*enterEngine)(void);
static void (*leaveEngine)(void);

SDL_Mutex* SDL_CreateMutex(void) { return (SDL_Mutex*)(uintptr_t)2; }
void SDL_LockMutex(SDL_Mutex* lock) { assert(lock != NULL); }
void SDL_UnlockMutex(SDL_Mutex* lock) { assert(lock != NULL); }
bool sndPCSetSynchronization(void (*enter)(void), void (*leave)(void)) {
  enterEngine = enter; leaveEngine = leave; return true;
}
bool sndPCConfigure(const SND_PC_CONFIG* config) {
  assert(config->mixRate == 32000 && config->channels == 2); return true;
}
bool sndPCRender(s16* pcm, size_t frames) {
  assert(active && frames <= 160);
  enterEngine();
  if (renderOk) {
    for (size_t i = 0; i < frames * 2; ++i) pcm[i] = (s16)(samples++ % 30000);
  }
  leaveEngine();
  return renderOk;
}
bool SDL_InitSubSystem(SDL_InitFlags flags) { assert(flags == SDL_INIT_AUDIO); return true; }
void SDL_QuitSubSystem(SDL_InitFlags flags) { assert(flags == SDL_INIT_AUDIO); }
const char* SDL_GetError(void) { return "test"; }
float fhConfigAudioGain(void) { return 0.5f; }
SDL_AudioStream* SDL_OpenAudioDeviceStream(SDL_AudioDeviceID id, const SDL_AudioSpec* spec,
                                         SDL_AudioStreamCallback callback, void* userdata) {
  assert(id == SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK && userdata == NULL);
  assert(spec->freq == 32000 && spec->channels == 2 && spec->format == SDL_AUDIO_S16);
  feed = callback; ++opened; return output;
}
bool SDL_SetAudioStreamGain(SDL_AudioStream* stream, float gain) {
  assert(stream == output && gain == 0.5f); return true;
}
bool SDL_ResumeAudioStreamDevice(SDL_AudioStream* stream) {
  assert(stream == output); active = resumeOk; return resumeOk;
}
void SDL_DestroyAudioStream(SDL_AudioStream* stream) {
  assert(stream == output); active = 0; ++closed;
}
bool SDL_PutAudioStreamData(SDL_AudioStream* stream, const void* data, int bytes) {
  assert(stream == output && bytes > 0 && bytes <= 640 && bytes % 4 == 0);
  const s16* pcm = data;
  for (int i = 0; i < bytes / 2; ++i) {
    assert(pcm[i] == (renderOk ? (s16)(received % 30000) : 0)); ++received;
  }
  return true;
}
AIDCallback AIRegisterDMACallback(AIDCallback callback) {
  AIDCallback old = dmaCallback; dmaCallback = callback; fhMusyxDMAChanged(callback); return old;
}
void AIStartDMA(void) { dmaRunning = 1; }
void AIStopDMA(void) { dmaRunning = 0; }
void AIInitDMA(uintptr_t address, u32 bytes) { assert(address && bytes == 640); }
bool SDL_PauseAudioStreamDevice(SDL_AudioStream* stream) { assert(stream == output); active = 0; return true; }
bool SDL_ClearAudioStream(SDL_AudioStream* stream) { assert(stream == output); return true; }
static void movieDma(void) {}
int main(void) {
  fhMusyxConfigure();
  fhMusyxStartOutput();
  assert(feed && active && samples == 0);
  feed(NULL, output, 0, 0);
  assert(samples == 0);
  for (int i = 0; i < 100; ++i) feed(NULL, output, 1280, 1280);
  assert(samples == 64000 && received == samples);
  AIDCallback previous = AIRegisterDMACallback(movieDma);
  assert(previous != NULL && dmaRunning && !active);
  AIRegisterDMACallback(previous);
  assert(!dmaRunning && active);
  renderOk = false;
  feed(NULL, output, 320, 320);
  fhMusyxStopOutput();
  fhMusyxStopOutput();
  assert(opened == 1 && closed == 1 && !active);
  resumeOk = false;
  fhMusyxStartOutput();
  assert(opened == 2 && closed == 2 && !active);
  return 0;
}
