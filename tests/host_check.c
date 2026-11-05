/* End-to-end check against the real GitHub API from a desktop, using the
 * library's own TLS configuration and root certificates:
 *   make -f tests/Makefile.host build/host_check
 *   ./build/host_check quatric GodModeWii 0.4.0
 * With a fourth argument (a fake argv[0]) it also installs the update there:
 *   ./build/host_check quatric GodModeWii 0.4.0 build/sd/apps/GodModeWii/boot.dol
 * WIIUPDATE_HttpPost, while an update check runs on its own thread:
 *   ./build/host_check post https://example.org/api '{"json":1}' */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "wiiupdate.h"

int main(int argc, char **argv)
{
    if (argc == 4 && !strcmp(argv[1], "post")) {
        WIIUPDATE_Start("quatric", "libwiiupdate", "0.0.1");
        char *reply = NULL;
        size_t len = 0;
        int st = WIIUPDATE_HttpPost(argv[2], "application/json", argv[3], strlen(argv[3]), "libwiiupdate-test",
                                    &reply, &len, 0, NULL);
        printf("status %d, %u bytes: %.200s\n", st, (unsigned)len, reply ? reply : "");
        wiiupdate_state us = WIIUPDATE_Wait(30000);
        printf("update check alongside: %s\n", us == WIIUPDATE_FAILED ? "failed" : "finished");
        free(reply);
        return st < 0 || us == WIIUPDATE_FAILED;
    }
    if (argc < 4) { fprintf(stderr, "usage: %s owner repo version [app_path]\n", argv[0]); return 2; }
    if (argc > 4) WIIUPDATE_SetAppPath(argv[4]);
    WIIUPDATE_Start(argv[1], argv[2], argv[3]);
    wiiupdate_state st = WIIUPDATE_Wait(30000);
    static const char *names[] = { "idle", "checking", "up to date", "available", "failed", "cancelled", "updated" };
    printf("%s: %s latest=%s installable=%d\n%s\n", WIIUPDATE_Url(), names[st], WIIUPDATE_Latest(),
           WIIUPDATE_CanInstall(), WIIUPDATE_Message());
    if (argc > 4 && WIIUPDATE_CanInstall()) {
        if (WIIUPDATE_Install() < 0) { printf("install did not start\n"); return 1; }
        int last = -1;
        wiiupdate_install_state ist;
        while ((ist = WIIUPDATE_InstallState()) == WIIUPDATE_INSTALL_DOWNLOADING || ist == WIIUPDATE_INSTALL_EXTRACTING) {
            if (WIIUPDATE_Progress() / 10 != last) printf("  %d%%\n", (last = WIIUPDATE_Progress() / 10) * 10);
            usleep(50000);
        }
        WIIUPDATE_Wait(5000);
        printf("install: %s %s -> %s\n", ist == WIIUPDATE_INSTALL_DONE ? "done" : "failed", WIIUPDATE_InstallError(),
               WIIUPDATE_InstallDir());
        return ist != WIIUPDATE_INSTALL_DONE;
    }
    return st == WIIUPDATE_FAILED;
}
