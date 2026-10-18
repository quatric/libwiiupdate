#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "wiiupdate.h"

/* ── Versions ────────────────────────────────────────────────────────────── */

int WIIUPDATE_CompareVersions(const char *a, const char *b)
{
    while (*a && !isdigit((unsigned char)*a)) a++;
    while (*b && !isdigit((unsigned char)*b)) b++;
    while (*a || *b) {
        unsigned long x = strtoul(a, (char **)&a, 10);
        unsigned long y = strtoul(b, (char **)&b, 10);
        if (x != y) return x < y ? -1 : 1;
        /* Only "." continues the numeric part; "-beta", " (wii)" etc. end it. */
        a = (*a == '.' && isdigit((unsigned char)a[1])) ? a + 1 : "";
        b = (*b == '.' && isdigit((unsigned char)b[1])) ? b + 1 : "";
    }
    return 0;
}

static int has_digit(const char *s)
{
    for (; *s; s++)
        if (isdigit((unsigned char)*s)) return 1;
    return 0;
}

/* Pull every non-prerelease "tag_name" out of a /releases reply and keep the
 * highest one. A rolling tag such as "latest" has no digits and is skipped. */
int wiiupdate_newest_tag(const char *json, char *out, size_t out_len)
{
    static const char key[] = "\"tag_name\":";
    const char *p = json;
    out[0] = 0;
    while ((p = strstr(p, key))) {
        if (p > json && p[-1] == '\\') { p++; continue; }
        p += sizeof key - 1;
        while (*p == ' ') p++;
        if (*p++ != '"') continue;
        const char *end = strchr(p, '"');
        if (!end) break;
        char tag[48];
        size_t n = (size_t)(end - p) < sizeof tag - 1 ? (size_t)(end - p) : sizeof tag - 1;
        memcpy(tag, p, n);
        tag[n] = 0;
        p = end;

        const char *next = strstr(p, key);
        const char *pre = strstr(p, "\"prerelease\":");
        int prerelease = 0;
        if (pre && (!next || pre < next)) {
            pre += 13;
            while (*pre == ' ') pre++;
            prerelease = !strncmp(pre, "true", 4);
        }
        if (prerelease || !has_digit(tag)) continue;
        if (!out[0] || WIIUPDATE_CompareVersions(tag, out) > 0) snprintf(out, out_len, "%s", tag);
    }
    return out[0] ? 0 : -1;
}
