#ifndef FOXHOLLOW_FRAME_PACING_H_
#define FOXHOLLOW_FRAME_PACING_H_

#include <stdint.h>

static inline uint64_t fhFramePacingDelay(uint64_t now, uint64_t period, uint64_t* next) {
  if (period == 0) {
    *next = 0;
    return 0;
  }
  if (*next == 0) {
    *next = now + period;
  } else if (now >= *next && now - *next >= period) {
    *next = now;
  }
  const uint64_t delay = now < *next ? *next - now : 0;
  *next += period;
  return delay;
}

#endif
