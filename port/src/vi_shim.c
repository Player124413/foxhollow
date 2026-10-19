#include <dolphin/types.h>
#include <dolphin/vi.h>
#include <aurora/aurora.h>
#include <aurora/dvd.h>
#include <aurora/event.h>
#include <SDL3/SDL_timer.h>
#include <stdlib.h>
#include "foxhollow_autosave.h"
#include "foxhollow_compat.h"
#include "foxhollow_config.h"
#include "foxhollow_frame_pacing.h"
#include "foxhollow_mods.h"
#include "foxhollow_quit.h"
#include "foxhollow_splash.h"
#include "shim_log.h"

typedef void (*VIRetraceCallback)(u32 retraceCount);

#if defined(FOXHOLLOW_DEBUG_SHORTCUTS)
void fhDebugOverlayDraw(void);
void fhGalleryDrawOverlay(void);
#endif
#if defined(FOXHOLLOW_DEBUG_INPUT_TIMING)
void fhDebugOverlayFrameBegin(void);
void fhDebugOverlayFramePresented(void);
#endif
void fhGalleryUpdate(void);
#if defined(__ANDROID__)
#include "foxhollow_touch.h"
#include <dolphin/os.h>
extern OSStopwatch gFrameStopwatch;
#endif

static VIRetraceCallback sPreRetraceCallback;
static VIRetraceCallback sPostRetraceCallback;
static void* sNextFrameBuffer;
static u32 sRetraceCount;
static int sFrameOpen;
static int sBlack;
static Uint64 sNextRetraceNs;

void fhGXCompleteFrame(void);

enum { VI_RETRACE_DEFAULT_HZ = 60 };

static void wait_for_retrace_deadline(void) {
  const int limit = fhConfigFrameLimit();
  const uint64_t period = limit == 0 ? 0 : SDL_NS_PER_SECOND / (uint64_t)(limit > 0 ? limit : VI_RETRACE_DEFAULT_HZ);
  uint64_t next = sNextRetraceNs;
  const uint64_t delay = fhFramePacingDelay(SDL_GetTicksNS(), period, &next);
  sNextRetraceNs = next;
  if (delay != 0) {
#if defined(__ANDROID__)
    SDL_DelayNS(delay);
#else
    SDL_DelayPrecise(delay);
#endif
  }
}

static void shutdown_and_exit(void) {
  fhAutosaveShutdown();
  fhModsShutdown();
  aurora_dvd_close();
  aurora_shutdown();
  fhExitGameProcess();
}

static void pump_events(void) {
  const AuroraEvent* event = aurora_update();
  int quitting = fhQuitRequested();

  while (event != NULL && event->type != AURORA_NONE) {
    if (event->type == AURORA_EXIT) {
      quitting = 1;
    }
#if defined(__ANDROID__)
    if (event->type == AURORA_SDL_EVENT) fhTouchHandleEvent(&event->sdl);
#endif
    ++event;
  }
#if defined(__ANDROID__)
  fhTouchUpdate();
#endif
  if (quitting) {
    shutdown_and_exit();
  }
}

void foxhollowFramePumpInit(void) {
  pump_events();
  sFrameOpen = aurora_begin_frame();
}

void VIWaitForRetrace(void) {
  if (sFrameOpen) {
#if defined(FOXHOLLOW_DEBUG_SHORTCUTS)
    fhDebugOverlayDraw();
    fhGalleryDrawOverlay();
#endif
#if defined(__ANDROID__)
    fhTouchDraw();
#endif
    if (sBlack) {
      fhDrawBlackScreen();
    }
    fhGXCompleteFrame();
    aurora_end_frame();
#if defined(FOXHOLLOW_DEBUG_INPUT_TIMING)
    fhDebugOverlayFramePresented();
#endif
    sFrameOpen = 0;
  }
#if defined(__ANDROID__)
  int menuWasOpen = fhTouchMenuOpen();
  if (!fhTouchConfigure()) shutdown_and_exit();
  if (menuWasOpen) {
    OSResetStopwatch(&gFrameStopwatch);
    OSStartStopwatch(&gFrameStopwatch);
  }
#endif
  fhModsUpdate();
  fhGalleryUpdate();
  fhAutosaveUpdate();
  wait_for_retrace_deadline();
  pump_events();
  while (!aurora_begin_frame()) {
    SDL_Delay(8);
    pump_events();
  }
  sFrameOpen = 1;
#if defined(FOXHOLLOW_DEBUG_INPUT_TIMING)
  fhDebugOverlayFrameBegin();
#endif
  fhAIPump();

  sRetraceCount++;
  if (sPreRetraceCallback) {
    sPreRetraceCallback(sRetraceCount);
  }
  if (sPostRetraceCallback) {
    sPostRetraceCallback(sRetraceCount);
  }
}

VIRetraceCallback VISetPreRetraceCallback(VIRetraceCallback cb) {
  VIRetraceCallback prev = sPreRetraceCallback;
  sPreRetraceCallback = cb;
  return prev;
}

VIRetraceCallback VISetPostRetraceCallback(VIRetraceCallback cb) {
  VIRetraceCallback prev = sPostRetraceCallback;
  sPostRetraceCallback = cb;
  return prev;
}

void VISetNextFrameBuffer(void* fb) { sNextFrameBuffer = fb; }
void* VIGetNextFrameBuffer(void) { return sNextFrameBuffer; }
void VISetBlack(BOOL black) { sBlack = black ? 1 : 0; }
u32 VIGetRetraceCount(void) { return sRetraceCount; }
u32 VIGetNextField(void) { return 0; }
u32 VIGetDTVStatus(void) { return 1; }
