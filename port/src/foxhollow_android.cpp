#include <borealis/presentation.hpp>

extern "C" void fhAndroidInit(void) {
  borealis::presentation::set_preferred_frame_rate(60.0f);
}
