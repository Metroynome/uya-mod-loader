#ifndef MODMENU_BACKEND_H
#define MODMENU_BACKEND_H

int menuBackendReady(unsigned int mode);
unsigned int menuBackendButtons(unsigned int mode);
void menuBackendCapture(unsigned int mode);
void menuBackendRelease(void);
void menuBackendDeactivate(void);
void menuBackendBox(float x, float y, float width, float height, unsigned int color);
void menuBackendText(float x, float y, unsigned int color, const char* text);
float menuBackendWidth(const char* text);
int menuBackendLevel(void);
int menuBackendPopupReady(unsigned int mode);
void menuBackendPause(unsigned int mode, void (*openMenu)(void));
#endif
