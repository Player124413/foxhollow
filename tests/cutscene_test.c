#include "foxhollow_cutscene.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>

static int slot;
static int movieSkippable;
int fhMovieCanSkip(void) { return movieSkippable; }
int16_t gObjSeqSlotSeqIdTable[0x58];
int8_t gObjSeqSlotResults[0x58];
int8_t gObjSeqSlotPrevResults[0x58];
int getCurSeqNo(void) { return slot; }

int main(void) {
  memset(gObjSeqSlotSeqIdTable, 1, sizeof(gObjSeqSlotSeqIdTable));
  int invalid[] = {-100, -1, 0, 0x55, 0x58, 255};
  for (unsigned i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
    slot = invalid[i];
    assert(!fhCanSkipCutscene() && !fhSkipCutscene());
    assert(!fhSequenceSkipRequested(slot, 123));
    fhForgetSequenceSkip(slot);
  }
  slot = 7;
  gObjSeqSlotSeqIdTable[slot] = 0;
  assert(!fhSkipCutscene());
  gObjSeqSlotSeqIdTable[slot] = 123;
  gObjSeqSlotResults[slot] = 1;
  gObjSeqSlotPrevResults[slot] = 1;
  assert(fhCanSkipCutscene() && fhSkipCutscene());
  assert(!fhCanSkipCutscene() && !fhSkipCutscene());
  for (int i = 0; i < 0x58; ++i) assert(gObjSeqSlotResults[i] == (i == slot ? 2 : 0));
  for (int frame = 0; frame < 10; ++frame) {
    gObjSeqSlotResults[slot] = 0;
    gObjSeqSlotPrevResults[slot] = 0;
    assert(fhSequenceSkipRequested(slot, 123));
    assert(!fhCanSkipCutscene());
    assert(!fhSequenceSkipRequested(slot + 1, 123));
    assert(!fhSequenceSkipRequested(slot, 124));
  }
  gObjSeqSlotSeqIdTable[slot] = 124;
  assert(!fhSequenceSkipRequested(slot, 123));
  fhForgetSequenceSkip(slot);
  gObjSeqSlotSeqIdTable[slot] = 123;
  assert(!fhSequenceSkipRequested(slot, 123));
  assert(fhSkipCutscene());
  fhForgetSequenceSkip(slot);
  gObjSeqSlotResults[slot] = 0;
  gObjSeqSlotPrevResults[slot] = 2;
  assert(!fhSkipCutscene());
  gObjSeqSlotPrevResults[slot] = 0;
  gObjSeqSlotResults[slot] = 4;
  assert(!fhSkipCutscene() && gObjSeqSlotResults[slot] == 4);
  slot = 0;
  gObjSeqSlotSeqIdTable[12] = 321;
  fhSetStorySequence(12, 321);
  assert(fhCanSkipCutscene() && fhSkipCutscene());
  assert(fhSequenceSkipRequested(12, 321));
  assert(!fhCanSkipCutscene());
  fhForgetSequenceSkip(12);
  assert(!fhCanSkipCutscene());
  fhSetStorySequence(12, 321);
  gObjSeqSlotSeqIdTable[12] = 322;
  assert(!fhCanSkipCutscene());
  fhClearStorySequence(12);
  gObjSeqSlotSeqIdTable[0] = 400;
  fhSetStorySequence(0, 400);
  assert(fhCanSkipCutscene() && fhSkipCutscene());
  assert(fhSequenceSkipRequested(0, 400));
  fhForgetSequenceSkip(0);
  gObjSeqSlotResults[0] = 0;
  assert(!fhCanSkipCutscene());
  assert(!fhTakeMovieSkip());
  movieSkippable = 1;
  assert(fhCanSkipCutscene() && fhSkipCutscene());
  assert(!fhCanSkipCutscene() && !fhSkipCutscene());
  assert(fhTakeMovieSkip() && !fhTakeMovieSkip());
  assert(fhSkipCutscene());
  movieSkippable = 0;
  assert(!fhTakeMovieSkip() && !fhCanSkipCutscene());
  movieSkippable = 1;
  assert(!fhTakeMovieSkip() && fhCanSkipCutscene());
}
