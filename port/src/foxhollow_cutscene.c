#include "foxhollow_cutscene.h"
#include <stdint.h>

extern int getCurSeqNo(void);
static int movieSkipRequested;
static int16_t sequenceSkipRequests[0x55];
extern int16_t gObjSeqSlotSeqIdTable[];
extern int8_t gObjSeqSlotResults[];
extern int8_t gObjSeqSlotPrevResults[];

static int sequenceCanSkip(void) {
  int slot = getCurSeqNo();
  return slot > 0 && slot < 0x55 && gObjSeqSlotSeqIdTable[slot] > 0 &&
         !fhSequenceSkipRequested(slot, gObjSeqSlotSeqIdTable[slot]) &&
         gObjSeqSlotResults[slot] < 2 && gObjSeqSlotPrevResults[slot] < 2;
}

int fhCanSkipCutscene(void) {
  return sequenceCanSkip() || (!movieSkipRequested && fhMovieCanSkip());
}

int fhSkipCutscene(void) {
  if (sequenceCanSkip()) {
    int slot = getCurSeqNo();
    sequenceSkipRequests[slot] = gObjSeqSlotSeqIdTable[slot];
    gObjSeqSlotResults[slot] = 2;
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

int fhSequenceSkipRequested(int slot, int sequenceId) {
  return slot > 0 && slot < 0x55 && sequenceId > 0 &&
         sequenceSkipRequests[slot] == sequenceId && gObjSeqSlotSeqIdTable[slot] == sequenceId;
}

void fhForgetSequenceSkip(int slot) {
  if (slot >= 0 && slot < 0x55) sequenceSkipRequests[slot] = 0;
}
