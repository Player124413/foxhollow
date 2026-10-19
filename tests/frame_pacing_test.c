#include "foxhollow_frame_pacing.h"

#include <assert.h>

int main(void) {
  const uint64_t period = 16666666;
  uint64_t next = 0;
  uint64_t now = 1000000000;
  assert(fhFramePacingDelay(now, period, &next) == period);
  assert(next == now + period * 2);
  now += period + 1000000;
  assert(fhFramePacingDelay(now, period, &next) == period - 1000000);

  next = now + period;
  now += period * 8;
  assert(fhFramePacingDelay(now, period, &next) == 0);
  assert(next == now + period);
  assert(fhFramePacingDelay(now + 1000000, period, &next) == period - 1000000);

  next = now;
  assert(fhFramePacingDelay(now + period, period, &next) == 0);
  assert(next == now + period * 2);
  assert(fhFramePacingDelay(now, 0, &next) == 0);
  assert(next == 0);

  next = 0;
  now = 1000000000;
  for (unsigned frame = 0; frame < 1000; ++frame) {
    const uint64_t delay = fhFramePacingDelay(now, period, &next);
    assert(delay <= period);
    now += delay;
    assert(next == now + period);
    now += 1000000;
  }

  next = 1000000000;
  now = next;
  for (unsigned frame = 0; frame < 1000; ++frame) {
    now += period * 3;
    assert(fhFramePacingDelay(now, period, &next) == 0);
    assert(next == now + period);
  }

  next = 500;
  assert(fhFramePacingDelay(510, 20, &next) == 0);
  assert(next == 520);
  assert(fhFramePacingDelay(515, 20, &next) == 5);
  assert(next == 540);
}
