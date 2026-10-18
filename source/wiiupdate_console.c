#include <stdio.h>
#include <gccore.h>
#include <wiiuse/wpad.h>
#include <ogc/lwp_watchdog.h>
#include "wiiupdate.h"

#define CHECK_LIMIT_MS  20000
#define NOTICE_MS       10000

/* Wii Remotes (and their extensions) plus GameCube pads, folded onto WPAD bits. */
static u32 default_buttons(void)
{
    WPAD_ScanPads();
    PAD_ScanPads();
    u32 down = 0;
    for (int i = 0; i < 4; i++) {
        down |= WPAD_ButtonsDown(i);
        u16 pad = PAD_ButtonsDown(i);
        if (pad & PAD_BUTTON_A) down |= WPAD_BUTTON_A;
        if (pad & PAD_BUTTON_B) down |= WPAD_BUTTON_B;
    }
    if (down & WPAD_CLASSIC_BUTTON_A) down |= WPAD_BUTTON_A;
    if (down & WPAD_CLASSIC_BUTTON_B) down |= WPAD_BUTTON_B;
    return down;
}

static int elapsed_ms(u64 since)
{
    return (int)ticks_to_millisecs(diff_ticks(since, gettime()));
}

wiiupdate_state WIIUPDATE_CheckConsole(const char *owner, const char *repo, const char *current_version,
                                       u32 (*buttons_down)(void))
{
    if (!buttons_down) buttons_down = default_buttons;

    printf("Checking for updates (B to skip)... ");
    fflush(stdout);
    if (WIIUPDATE_Start(owner, repo, current_version) < 0) {
        printf("could not check.\n");
        return WIIUPDATE_FAILED;
    }

    u64 start = gettime();
    while (WIIUPDATE_State() == WIIUPDATE_CHECKING) {
        VIDEO_WaitVSync();
        int skip = (buttons_down() & WPAD_BUTTON_B) != 0;
        if (skip || elapsed_ms(start) > CHECK_LIMIT_MS) {
            WIIUPDATE_Cancel();
            printf(skip ? "skipped.\n" : "timed out.\n");
            return WIIUPDATE_CANCELLED;
        }
    }

    wiiupdate_state st = WIIUPDATE_State();
    if (st == WIIUPDATE_UP_TO_DATE) {
        printf("up to date.\n");
    } else if (st == WIIUPDATE_AVAILABLE) {
        WIIUPDATE_TakeNotice();
        printf("\n\n  %s\n  Download it from %s\n\n  Press A to continue.\n\n", WIIUPDATE_Message(),
               WIIUPDATE_Url());
        start = gettime();
        while (elapsed_ms(start) < NOTICE_MS) {
            VIDEO_WaitVSync();
            if (buttons_down() & (WPAD_BUTTON_A | WPAD_BUTTON_B)) break;
        }
    } else {
        printf("could not check.\n");
    }
    return st;
}
