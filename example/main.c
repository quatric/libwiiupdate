/* Minimal text-mode app that checks for a newer release at boot. */
#include <stdio.h>
#include <stdlib.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <wiiupdate.h>

int main(void)
{
    VIDEO_Init();
    WPAD_Init();
    PAD_Init();
    GXRModeObj *mode = VIDEO_GetPreferredMode(NULL);
    void *xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(mode));
    console_init(xfb, 20, 20, mode->fbWidth, mode->xfbHeight, mode->fbWidth * VI_DISPLAY_PIX_SZ);
    VIDEO_Configure(mode);
    VIDEO_SetNextFramebuffer(xfb);
    VIDEO_SetBlack(false);
    VIDEO_Flush();
    VIDEO_WaitVSync();

    printf("\x1b[2;0HExample 1.0\n\n");
    WIIUPDATE_CheckConsole("quatric", "GodModeWii", "0.1.0", NULL);

    printf("\nPress HOME to exit.\n");
    for (;;) {
        WPAD_ScanPads();
        if (WPAD_ButtonsDown(0) & WPAD_BUTTON_HOME) exit(0);
        VIDEO_WaitVSync();
    }
}
