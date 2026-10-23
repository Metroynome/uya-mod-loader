#ifndef MODMENU_BACKEND_H
#define MODMENU_BACKEND_H

#include "types.h"

int menuBackendReady(u32 mode);
u32 menuBackendButtons(u32 mode);
void menuBackendCapture(u32 mode);
void menuBackendRelease(void);
void menuBackendDeactivate(void);
void menuBackendBox(float x, float y, float width, float height, u32 color);
void menuBackendText(float x, float y, u32 color, const char* text);
float menuBackendWidth(const char* text);
int menuBackendLevel(void);
int menuBackendPopupReady(u32 mode);
void menuBackendPause(u32 mode, void (*openMenu)(void));
#endif
