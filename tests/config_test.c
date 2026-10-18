#include "foxhollow_config.h"
#include <assert.h>
#include <math.h>
#include <stdlib.h>
int main(int argc, char** argv) {
  assert(argc == 2);
  assert(fabsf(fhConfigAudioGain() - strtof(argv[1], NULL)) < .0001f);
#if defined(__ANDROID__)
  assert(fhConfigBackend() == BACKEND_OPENGLES);
#endif
  return 0;
}
