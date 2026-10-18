/* Host-side test of version comparison and release parsing:
 *   cc -Iinclude -Itests tests/host_test.c source/wiiupdate_version.c -o /tmp/t && /tmp/t releases.json */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "wiiupdate.h"

int wiiupdate_newest_tag(const char *json, char *out, size_t out_len);

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

int main(int argc, char **argv)
{
    CHECK(WIIUPDATE_CompareVersions("v1.10", "1.9.2") > 0);
    CHECK(WIIUPDATE_CompareVersions("v1.0", "1.0.0") == 0);
    CHECK(WIIUPDATE_CompareVersions("1.0", "v1.0.1") < 0);
    CHECK(WIIUPDATE_CompareVersions("GodModeWii v0.5.0", "0.4.9") > 0);
    CHECK(WIIUPDATE_CompareVersions("v2.0-beta", "2.0") == 0);

    char tag[48];
    const char *json = "[{\"tag_name\":\"latest\",\"prerelease\":false},"
                       "{\"tag_name\":\"v1.1.0\",\"draft\":false,\"prerelease\":false,\"body\":\"x \\\"tag_name\\\": \\\"v9\\\"\"},"
                       "{\"tag_name\":\"v2.0.0\",\"prerelease\":true},"
                       "{\"tag_name\":\"v1.0.0\",\"prerelease\":false}]";
    CHECK(wiiupdate_newest_tag(json, tag, sizeof tag) == 0);
    CHECK(!strcmp(tag, "v1.1.0"));
    CHECK(wiiupdate_newest_tag("[]", tag, sizeof tag) < 0);

    if (argc > 1) {
        FILE *f = fopen(argv[1], "rb");
        static char buf[1 << 20];
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        buf[n] = 0;
        fclose(f);
        printf("%s -> %s\n", argv[1], wiiupdate_newest_tag(buf, tag, sizeof tag) ? "(none)" : tag);
    }
    printf(failures ? "%d failures\n" : "ok\n", failures);
    return failures != 0;
}
