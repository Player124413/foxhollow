#ifndef FOXHOLLOW_TOUCH_H
#define FOXHOLLOW_TOUCH_H
#ifdef __cplusplus
extern "C" {
#endif
union SDL_Event;
void fhTouchInit(const char* userPath);
void fhTouchUpdate(void);
void fhTouchDraw(void);
void fhTouchHandleEvent(const union SDL_Event* event);
int fhTouchMenuOpen(void);
int fhTouchConfigure(void);
#ifdef __cplusplus
}
#endif
#endif
