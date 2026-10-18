#include "foxhollow_cutscene.h"
#include <stdint.h>

extern int getCurSeqNo(void);
static int movieSkipRequested;
static int16_t sequenceSkipRequests[0x55];
extern int16_t gObjSeqSlotSeqIdTable[];
extern int8_t gObjSeqSlotResults[];
extern int8_t gObjSeqSlotPrevResults[];
static int storySlot = -1;
static int storyId;

void fhSetStorySequence(int slot, int sequenceId) {
  storySlot = slot;
  storyId = sequenceId;
}

void fhClearStorySequence(int slot) {
  if (slot == storySlot) {
    storySlot = -1;
    storyId = 0;
  }
}

static int storyActive(void) {
  return storySlot >= 0 && storySlot < 0x55 && storyId > 0 &&
         gObjSeqSlotSeqIdTable[storySlot] == storyId;
}

static int activeSequence(void) {
  if (storyActive()) return storySlot;
  return getCurSeqNo();
}

static int sequenceCanSkip(void) {
  int slot = activeSequence();
  return (slot > 0 || (slot == 0 && storyActive())) && slot < 0x55 && gObjSeqSlotSeqIdTable[slot] > 0 &&
         !fhSequenceSkipRequested(slot, gObjSeqSlotSeqIdTable[slot]) &&
         gObjSeqSlotResults[slot] < 2 && gObjSeqSlotPrevResults[slot] < 2;
}

int fhCanSkipCutscene(void) {
  return sequenceCanSkip() || (!movieSkipRequested && fhMovieCanSkip());
}

int fhSkipCutscene(void) {
  if (sequenceCanSkip()) {
    int slot = activeSequence();
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
  return slot >= 0 && slot < 0x55 && sequenceId > 0 &&
         sequenceSkipRequests[slot] == sequenceId && gObjSeqSlotSeqIdTable[slot] == sequenceId;
}

void fhForgetSequenceSkip(int slot) {
  if (slot >= 0 && slot < 0x55) sequenceSkipRequests[slot] = 0;
  fhClearStorySequence(slot);
}
