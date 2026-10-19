#include "foxhollow_config.h"
#include <musyx/musyx.h>
#include <musyx/pc.h>
#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

#define FH_MUSYX_MIX_RATE 32000
#define FH_MUSYX_CHANNELS 2
#define FH_MUSYX_CHUNK_FRAMES 160

static SDL_Mutex* sMusyxLock;
static SDL_AudioStream* sMusyxOutput;
static int sMusyxConfigured;

static void fhMusyxEnter(void) { SDL_LockMutex(sMusyxLock); }
static void fhMusyxLeave(void) { SDL_UnlockMutex(sMusyxLock); }

static void SDLCALL fhMusyxFeed(void* userdata, SDL_AudioStream* stream, int additional, int total) {
  s16 pcm[FH_MUSYX_CHUNK_FRAMES * FH_MUSYX_CHANNELS];
  (void)userdata;
  (void)total;
  while (additional > 0) {
    int frames = additional / 4 + (additional % 4 != 0);
    if (frames > FH_MUSYX_CHUNK_FRAMES) frames = FH_MUSYX_CHUNK_FRAMES;
    int bytes = frames * FH_MUSYX_CHANNELS * (int)sizeof(s16);
    if (!sndPCRender(pcm, (size_t)frames)) memset(pcm, 0, (size_t)bytes);
    if (!SDL_PutAudioStreamData(stream, pcm, bytes)) break;
    additional -= bytes;
  }
}

void fhMusyxConfigure(void) {
  SND_PC_CONFIG config = {FH_MUSYX_MIX_RATE, FH_MUSYX_CHANNELS};
  sMusyxConfigured = 0;
  if (sMusyxLock == NULL) sMusyxLock = SDL_CreateMutex();
  if (sMusyxLock == NULL || !sndPCSetSynchronization(fhMusyxEnter, fhMusyxLeave) ||
      !sndPCConfigure(&config)) {
    fprintf(stderr, "[foxhollow] MusyX PC configuration rejected\n");
    return;
  }
  sMusyxConfigured = 1;
}

void fhMusyxStopOutput(void) {
  if (sMusyxOutput != NULL) {
    SDL_DestroyAudioStream(sMusyxOutput);
    sMusyxOutput = NULL;
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
  }
}

void fhMusyxStartOutput(void) {
  SDL_AudioSpec spec;
  fhMusyxStopOutput();
  if (!sMusyxConfigured || !SDL_InitSubSystem(SDL_INIT_AUDIO)) return;
  SDL_zero(spec);
  spec.format = SDL_AUDIO_S16;
  spec.channels = FH_MUSYX_CHANNELS;
  spec.freq = FH_MUSYX_MIX_RATE;
  sMusyxOutput = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, fhMusyxFeed, NULL);
  if (sMusyxOutput == NULL) {
    fprintf(stderr, "[foxhollow] MusyX audio output failed: %s\n", SDL_GetError());
    SDL_QuitSubSystem(SDL_INIT_AUDIO);
    return;
  }
  SDL_SetAudioStreamGain(sMusyxOutput, fhConfigAudioGain());
  if (!SDL_ResumeAudioStreamDevice(sMusyxOutput)) {
    fprintf(stderr, "[foxhollow] MusyX audio resume failed: %s\n", SDL_GetError());
    fhMusyxStopOutput();
    return;
  }
  fprintf(stderr, "[foxhollow] MusyX output: device-paced PCM rate=%d\n", FH_MUSYX_MIX_RATE);
}
