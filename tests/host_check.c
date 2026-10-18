/* End-to-end check against the real GitHub API from a desktop, using the
 * library's own TLS configuration and root certificates:
 *   make -f tests/Makefile.host && ./build/host_check quatric GodModeWii 0.4.0 */
#include <stdio.h>
#include "wiiupdate.h"

int main(int argc, char **argv)
{
    if (argc < 4) { fprintf(stderr, "usage: %s owner repo version\n", argv[0]); return 2; }
    WIIUPDATE_Start(argv[1], argv[2], argv[3]);
    wiiupdate_state st = WIIUPDATE_Wait(30000);
    static const char *names[] = { "idle", "checking", "up to date", "available", "failed", "cancelled" };
    printf("%s: %s latest=%s\n%s\n", WIIUPDATE_Url(), names[st], WIIUPDATE_Latest(), WIIUPDATE_Message());
    return st == WIIUPDATE_FAILED;
}
