#include <SDL3/SDL.h>
#include <dolphin/pad.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <vector>

extern "C" void fhTouchUpdate(void);
namespace {
std::vector<SDL_Finger> fingers;
bool focused = true;
bool active = false;
PADStatus result{};
}
extern "C" {
SDL_Window* SDL_GetKeyboardFocus(void) { return focused ? reinterpret_cast<SDL_Window*>(1) : nullptr; }
bool SDL_GetWindowSize(SDL_Window*, int* w, int* h) { *w = 1000; *h = 1000; return true; }
SDL_TouchID* SDL_GetTouchDevices(int* count) {
  *count = 1;
  auto* ids = static_cast<SDL_TouchID*>(std::malloc(sizeof(SDL_TouchID) * 2));
  ids[0] = 1; ids[1] = 0;
  return ids;
}
SDL_Finger** SDL_GetTouchFingers(SDL_TouchID, int* count) {
  *count = static_cast<int>(fingers.size());
  auto** out = static_cast<SDL_Finger**>(std::malloc(sizeof(SDL_Finger*) * (fingers.size() + 1)));
  for (size_t i = 0; i < fingers.size(); ++i) out[i] = &fingers[i];
  out[fingers.size()] = nullptr;
  return out;
}
void SDL_free(void* p) { std::free(p); }
void PADSetVirtualStatus(u32 port, const PADStatus* status) { assert(port == 0); result = *status; active = true; }
void PADClearVirtualStatus(u32 port) { assert(port == 0); result = {}; active = false; }
}
int main() {
  fhTouchUpdate(); assert(!active);
  fingers = {{1, .89f, .67f, 1}, {2, .80f, .81f, 1}};
  fhTouchUpdate(); assert(active && result.button == (PAD_BUTTON_A | PAD_BUTTON_B));
  fingers.erase(fingers.begin());
  fhTouchUpdate(); assert(result.button == PAD_BUTTON_B);
  fingers.clear(); fhTouchUpdate(); assert(!active);
  fingers = {{3, .16f, .72f, 1}};
  fhTouchUpdate(); assert(result.stickX == 0 && result.stickY == 0);
  fingers[0].x = .40f; fhTouchUpdate(); assert(result.stickX == 100 && result.stickY == 0);
  fingers[0].y = .48f; fhTouchUpdate(); assert(result.stickX == 71 && result.stickY == 71);
  fingers.push_back({8, .16f, .72f, 1});
  fhTouchUpdate(); assert(result.stickX == 71 && result.stickY == 71);
  focused = false; fhTouchUpdate(); assert(!active);
  focused = true; fhTouchUpdate(); assert(!active);
  fingers.clear(); fhTouchUpdate(); assert(!active);
  fingers = {{4, .10f, .18f, 1}, {5, .90f, .18f, 1}};
  fhTouchUpdate(); assert(result.triggerLeft == 255 && result.triggerRight == 255);
  assert(result.button == (PAD_TRIGGER_L | PAD_TRIGGER_R));
  fingers.clear(); fhTouchUpdate(); assert(!active);
  fingers = {{6, .50f, .95f, 1}};
  fhTouchUpdate(); assert(result.button == 0);
  fingers[0].x = .89f; fingers[0].y = .67f;
  fhTouchUpdate(); assert(result.button == 0);
  fingers.clear(); fhTouchUpdate(); assert(!active);
  fingers = {{7, .89f, .67f, 1}}; fhTouchUpdate(); assert(result.button == PAD_BUTTON_A);
  fingers[0].x = .1f; fhTouchUpdate(); assert(result.button == 0);
  fingers.clear(); fhTouchUpdate(); assert(!active);
}
