#include "stdafx.h"
#include "Tweaks.h"
#include "Display.h"
#include "Touch.h"
#include "Logger.h"

const char* tweakLabels[] = {"VIDEO IN MOTION", "DUMP REGISTRY", "EXEC SCRIPT", "EXIT"};

void RenderTweakMenu() {
    int y = 20;
    for (int i = 0; i < 4; i++) {
        RenderButton(780, y, 180, 65, tweakLabels[i]);
        y += 65 + 10;
    }
}

void PatchVideoInMotion() {
    PrintToScreen(1, "Searching for speed lock address...\n");
    // This is a placeholder for the actual memory signature search and patch logic
    // On many WinCE units, patching the return value of a specific API or a flag in the media player process works.
    PrintToScreen(1, "Feature not yet available for this firmware version.\n");
    Sleep(2000);
}

void RunTweaks() {
    ResetTextRenderer();
    DrawBackground(0x0010);
    PrintToScreen(2, "T w e a k s   &   T o o l s");
    
    while (true) {
        RenderTweakMenu();
        LCDTouchEvent* ev = WaitForTouch(INFINITE);
        if (ev) {
            int btn = GetPressedButton(ev->xCoord, ev->yCoord, 780, 20, 180, 65, 10, 4);
            if (btn == 0) PatchVideoInMotion();
            if (btn == 1) PrintToScreen(1, "Registry dump started...\n");
            if (btn == 2) PrintToScreen(1, "Running script from SD...\n");
            if (btn == 3) break;
            
            WaitForScreenUntouch();
        }
    }
}
