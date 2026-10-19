#ifndef FOXHOLLOW_QUIT_H_
#define FOXHOLLOW_QUIT_H_

#ifdef __cplusplus
extern "C" {
#endif

void fhExitGameProcess(void);
void fhRequestQuit(void);
int fhQuitRequested(void);

#ifdef __cplusplus
}
#endif

#endif
