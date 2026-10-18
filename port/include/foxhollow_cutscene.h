#ifndef FOXHOLLOW_CUTSCENE_H
#define FOXHOLLOW_CUTSCENE_H
#ifdef __cplusplus
extern "C" {
#endif
int fhCanSkipCutscene(void);
int fhSkipCutscene(void);
int fhTakeMovieSkip(void);
int fhMovieCanSkip(void);
int fhSequenceSkipRequested(int slot, int sequenceId);
void fhForgetSequenceSkip(int slot);
#ifdef __cplusplus
}
#endif
#endif
