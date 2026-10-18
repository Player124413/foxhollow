#include <borealis/presentation.hpp>
#include <SDL3/SDL_hints.h>

extern "C" void fhAndroidInit(void) {
  SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
  borealis::presentation::set_preferred_frame_rate(60.0f);
}
