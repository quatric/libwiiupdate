/* libwiiupdate: check GitHub for a newer release of a Wii homebrew app.
 *
 * The check runs on its own thread (network bring-up, HTTPS request to the
 * GitHub releases API, version comparison) so it never blocks the app.
 *
 * GUI apps:
 *
 *     WIIUPDATE_Start("quatric", "MyApp", "1.2.0");
 *     ...each frame...
 *     if (WIIUPDATE_TakeNotice()) show_popup(WIIUPDATE_Message());
 *
 * Text (console) apps, once at boot:
 *
 *     WIIUPDATE_CheckConsole("quatric", "MyApp", "1.2.0", NULL);
 *
 * which prints one line, lets B skip the check, and only stops to show a
 * notice when a newer version is out. Failures are silent apart from a short
 * "could not check" note; the app just carries on.
 */
#ifndef WIIUPDATE_H
#define WIIUPDATE_H

#include <gctypes.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIIUPDATE_IDLE = 0,     /* WIIUPDATE_Start not called yet */
    WIIUPDATE_CHECKING,     /* thread running */
    WIIUPDATE_UP_TO_DATE,   /* no release newer than the running version */
    WIIUPDATE_AVAILABLE,    /* WIIUPDATE_Latest() is newer */
    WIIUPDATE_FAILED,       /* no network, TLS or HTTP error, bad reply */
    WIIUPDATE_CANCELLED,    /* WIIUPDATE_Cancel was called */
} wiiupdate_state;

/* Start a background check of github.com/<owner>/<repo>/releases against
 * current_version (e.g. "1.2.0" or "v1.2.0"). Brings the network up if the
 * app has not. Returns 0 when the thread started. Strings are copied. */
int WIIUPDATE_Start(const char *owner, const char *repo, const char *current_version);

wiiupdate_state WIIUPDATE_State(void);

/* Block until the check thread has finished (cancelled or not) or timeout_ms
 * passes (<0 waits forever). Call before the app brings up or shuts down the
 * network itself, so the two do not race. */
wiiupdate_state WIIUPDATE_Wait(int timeout_ms);

/* Stop caring about the result. The thread finishes in the background; its
 * result is discarded and the state becomes WIIUPDATE_CANCELLED. */
void WIIUPDATE_Cancel(void);

/* Returns 1 exactly once, the first time it is called after a newer release
 * was found. Meant for a GUI popup: poll it every frame. */
int WIIUPDATE_TakeNotice(void);

/* Newest release tag found, e.g. "v1.3.0" ("" until known). */
const char *WIIUPDATE_Latest(void);
/* The running version as passed to WIIUPDATE_Start. */
const char *WIIUPDATE_Current(void);
/* "github.com/<owner>/<repo>/releases" */
const char *WIIUPDATE_Url(void);
/* A short human-readable line for popups, e.g.
 * "Version v1.3.0 is available (you have 1.2.0)." */
const char *WIIUPDATE_Message(void);

/* Compare two version strings numerically ("v1.10" > "1.9.2"). Leading text
 * before the first digit is skipped. Returns <0, 0 or >0. */
int WIIUPDATE_CompareVersions(const char *a, const char *b);

/* Console helper for text-mode apps: prints "Checking for updates..." on the
 * current console, waits for the check while polling buttons, and returns
 * the final state. buttons_down returns WPAD_BUTTON_* bits pressed this frame
 * (e.g. a wrapper around INPUT_Update/INPUT_ButtonsDown); pass NULL to have
 * the library scan Wii Remotes and GameCube pads itself. B skips the check.
 * When an update is available the notice stays up until A/B or 10 seconds. */
wiiupdate_state WIIUPDATE_CheckConsole(const char *owner, const char *repo, const char *current_version,
                                       u32 (*buttons_down)(void));

#ifdef __cplusplus
}
#endif

#endif
