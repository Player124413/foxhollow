#include "foxhollow_cutscene.h"
#include <stdint.h>

extern int getCurSeqNo(void);
static int movieSkipRequested;
extern int16_t gObjSeqSlotSeqIdTable[];
extern int8_t gObjSeqSlotResults[];
extern int8_t gObjSeqSlotPrevResults[];

static int sequenceCanSkip(void) {
  int slot = getCurSeqNo();
  return slot > 0 && slot < 0x55 && gObjSeqSlotSeqIdTable[slot] > 0 &&
         gObjSeqSlotResults[slot] < 2 && gObjSeqSlotPrevResults[slot] < 2;
}

int fhCanSkipCutscene(void) {
  return sequenceCanSkip() || (!movieSkipRequested && fhMovieCanSkip());
}

int fhSkipCutscene(void) {
  if (sequenceCanSkip()) {
    gObjSeqSlotResults[getCurSeqNo()] = 2;
    return 1;
  }
  if (!movieSkipRequested && fhMovieCanSkip()) {
    movieSkipRequested = 1;
    return 1;
  }
  return 0;
}

int fhTakeMovieSkip(void) {
  int requested = movieSkipRequested;
  movieSkipRequested = 0;
  return requested && fhMovieCanSkip();
}
