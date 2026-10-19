#include "foxhollow_quit.h"

#include <stdio.h>
#include <stdlib.h>

static int sQuitRequested;

void fhRequestQuit(void) { sQuitRequested = 1; }

int fhQuitRequested(void) { return sQuitRequested; }

void fhExitGameProcess(void) {
#if defined(__ANDROID__)
  fflush(NULL);
  _Exit(EXIT_SUCCESS);
#else
  exit(EXIT_SUCCESS);
#endif
}
