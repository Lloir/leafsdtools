#ifdef QY8XXX

#ifndef DISPLAY_DIRECTDRAW_H
#define DISPLAY_DIRECTDRAW_H

// Common

#define COLOR_WHITE      0xFFFF // RGB(255,255,255)
#define COLOR_LIGHT_GRAY 0xC618 // RGB(192,192,192)
#define COLOR_DARK_GRAY  0x630C // RGB(128,128,128)
#define COLOR_BLACK      0x0000 // RGB(0,0,0)

// Debug parameters
#define LOG_PIXELS 0
#define LOG_SURFACES 0
// Function pointers

// Functions
int SetupRenderContext();
int LockSurface(DWORD surface, DWORD* output);
int LockSurfaceAlt(DWORD surface, DWORD* output);
int UnlockSurface(DWORD surface);
int SwapBuffers();
int InitGraphic();
int DestroySurfaces();
int ScrollScreen();
void AdjustTextPosition(int x, int y);
void AdjustBoundaries(int x);
int RenderChar(BYTE c, int x, int y, WORD color, int scale);
int RenderStringWithScale(const BYTE* str, int scale);
int RenderString(const BYTE* str);
void PrintScreenWithScale(const BYTE* str, int scale);
void PrintScreen(const BYTE* str);
int DrawBackground(DWORD color);
void PrintToScreen(int scale, const char* format, ...);
void DrawRect(int x, int y, int w, int h, WORD color, WORD* pixels, bool transparent = false);
void DrawChar(BYTE c, int x, int y, WORD color, int scale, WORD* pixels, bool transparent);
int ClearArea(int x, int y, int w, int h);
int RenderBtnText(int x, int y, const char* text, WORD color, int scale, WORD* pixels);
int RenderButton(int x_right, int y_top, int w, int h, const char* text);
int RenderButtonWithState(int x_right, int y_top, int w, int h, const char* text, bool state);
void ResetTextRenderer();
void DrawProgressBar(int x, int y, int w, int h, int progress, int max, WORD color);

#endif // DISPLAY_DIRECTDRAW_H

#endif