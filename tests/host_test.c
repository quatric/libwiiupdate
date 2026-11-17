/* Host-side test of version comparison, release parsing and zip unpacking:
 *   make -f tests/Makefile.host build/host_test && ./build/host_test [releases.json] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "miniz.h"
#include "wiiupdate_internal.h"

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

    char asset[256];
    const char *rel = "[{\"tag_name\":\"v1.1\",\"assets\":[{\"browser_download_url\":\"https://x/a.tar.gz\"},"
                      "{\"browser_download_url\":\"https://x/src.zip\"},{\"browser_download_url\":\"https://x/app-hbc.zip\"}],"
                      "\"body\":\"\\\"browser_download_url\\\":\\\"https://evil/x.zip\\\"\"},"
                      "{\"tag_name\":\"v1.0\",\"assets\":[{\"browser_download_url\":\"https://x/old.dol\"}]},"
                      "{\"tag_name\":\"v0.9\",\"assets\":[{\"browser_download_url\":\"https://x/n.tar.gz\"}]}]";
    CHECK(wiiupdate_release_asset(rel, "v1.1", asset, sizeof asset) == 0 && !strcmp(asset, "https://x/app-hbc.zip"));
    CHECK(wiiupdate_release_asset(rel, "v1.0", asset, sizeof asset) == 0 && !strcmp(asset, "https://x/old.dol"));
    CHECK(wiiupdate_release_asset(rel, "v0.9", asset, sizeof asset) < 0);
    CHECK(wiiupdate_release_asset(rel, "v2", asset, sizeof asset) < 0);

    /* Wii U zips in the same release are passed over, whatever their order. */
    const char *both = "[{\"tag_name\":\"v2.0\",\"assets\":["
                       "{\"browser_download_url\":\"https://x/AppWiiU-2.0-wiiu.zip\"},"
                       "{\"browser_download_url\":\"https://x/AppWii-2.0-hbc.zip\"},"
                       "{\"browser_download_url\":\"https://x/App-aroma.zip\"}]},"
                       "{\"tag_name\":\"v1.9\",\"assets\":["
                       "{\"browser_download_url\":\"https://x/AppWiiU-1.9-wiiu.zip\"},"
                       "{\"browser_download_url\":\"https://x/App.dol\"}]},"
                       "{\"tag_name\":\"v1.8\",\"assets\":["
                       "{\"browser_download_url\":\"https://x/AppWiiU-1.8-wiiu.zip\"}]}]";
    CHECK(wiiupdate_release_asset(both, "v2.0", asset, sizeof asset) == 0 && !strcmp(asset, "https://x/AppWii-2.0-hbc.zip"));
    CHECK(wiiupdate_release_asset(both, "v1.9", asset, sizeof asset) == 0 && !strcmp(asset, "https://x/App.dol"));
    CHECK(wiiupdate_release_asset(both, "v1.8", asset, sizeof asset) < 0);

    /* Several apps in one release: each takes its own. */
    const char *apps = "[{\"tag_name\":\"v3.0\",\"assets\":["
                       "{\"browser_download_url\":\"https://x/BasiliskIIWii-3.0-hbc.zip\"},"
                       "{\"browser_download_url\":\"https://x/SheepShaverWii-3.0-hbc.zip\"},"
                       "{\"browser_download_url\":\"https://x/SheepShaverWiiU-3.0-wiiu.zip\"}]}]";
    wiiupdate_asset_name = "sheepshaver";
    CHECK(wiiupdate_release_asset(apps, "v3.0", asset, sizeof asset) == 0 && !strcmp(asset, "https://x/SheepShaverWii-3.0-hbc.zip"));
    wiiupdate_asset_name = "BasiliskII";
    CHECK(wiiupdate_release_asset(apps, "v3.0", asset, sizeof asset) == 0 && !strcmp(asset, "https://x/BasiliskIIWii-3.0-hbc.zip"));
    wiiupdate_asset_name = "Other";
    CHECK(wiiupdate_release_asset(apps, "v3.0", asset, sizeof asset) < 0);
    wiiupdate_asset_name = NULL;

    /* Unpack: only apps/<Name>/ lands in the app folder, nothing escapes it. */
    mz_zip_archive zw;
    memset(&zw, 0, sizeof zw);
    const char *zip = "build/test.zip";
    CHECK(mz_zip_writer_init_file(&zw, zip, 0));
    mz_zip_writer_add_mem(&zw, "README.md", "readme", 6, MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_add_mem(&zw, "apps/MyApp/boot.dol", "new dol", 7, MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_add_mem(&zw, "apps/MyApp/data/level1.bin", "level", 5, MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_add_mem(&zw, "apps/MyApp/../../escape.txt", "bad", 3, MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_finalize_archive(&zw);
    mz_zip_writer_end(&zw);
    char name[64], err[128];
    CHECK(wiiupdate_zip_app_name(zip, name, sizeof name) == 0 && !strcmp(name, "MyApp"));
    system("rm -rf build/sd && mkdir -p build/sd/apps/Renamed && echo old > build/sd/apps/Renamed/boot.dol"
           " && echo keep > build/sd/apps/Renamed/settings.ini");
    CHECK(wiiupdate_extract(zip, "build/sd/apps/Renamed", NULL, NULL, NULL, err, sizeof err) == 0);
    FILE *f = fopen("build/sd/apps/Renamed/boot.dol", "rb");
    char got[16] = "";
    if (f) { fread(got, 1, sizeof got - 1, f); fclose(f); }
    CHECK(!strcmp(got, "new dol"));
    struct stat st;
    CHECK(stat("build/sd/apps/Renamed/data/level1.bin", &st) == 0);
    CHECK(stat("build/sd/apps/Renamed/settings.ini", &st) == 0);
    CHECK(stat("build/sd/apps/Renamed/README.md", &st) != 0);
    CHECK(stat("build/sd/escape.txt", &st) != 0 && stat("build/escape.txt", &st) != 0);
    CHECK(stat("build/sd/apps/Renamed/boot.dol.wutmp", &st) != 0);
    CHECK(wiiupdate_extract("build/missing.zip", "build/sd/apps/Renamed", NULL, NULL, NULL, err, sizeof err) < 0);

    /* With an update list: listed files are replaced, existing assets are kept,
     * missing assets are added, and an unchanged install is a no-op. */
    static const char *const only[] = { "boot.dol", "meta.xml", NULL };
    mz_zip_writer_init_file(&zw, zip, 0);
    mz_zip_writer_add_mem(&zw, "apps/MyApp/boot.dol", "dol v2", 6, MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_add_mem(&zw, "apps/MyApp/data/level1.bin", "level v2", 8, MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_add_mem(&zw, "apps/MyApp/data/level2.bin", "new", 3, MZ_DEFAULT_COMPRESSION);
    mz_zip_writer_finalize_archive(&zw);
    mz_zip_writer_end(&zw);
    CHECK(wiiupdate_extract(zip, "build/sd/apps/Renamed", only, NULL, NULL, err, sizeof err) == 0);
    f = fopen("build/sd/apps/Renamed/boot.dol", "rb");
    memset(got, 0, sizeof got);
    if (f) { fread(got, 1, sizeof got - 1, f); fclose(f); }
    CHECK(!strcmp(got, "dol v2"));
    f = fopen("build/sd/apps/Renamed/data/level1.bin", "rb");
    memset(got, 0, sizeof got);
    if (f) { fread(got, 1, sizeof got - 1, f); fclose(f); }
    CHECK(!strcmp(got, "level"));
    CHECK(stat("build/sd/apps/Renamed/data/level2.bin", &st) == 0);
    struct stat before, after;
    stat("build/sd/apps/Renamed/boot.dol", &before);
    system("sleep 1");
    CHECK(wiiupdate_extract(zip, "build/sd/apps/Renamed", only, NULL, NULL, err, sizeof err) == 0);
    stat("build/sd/apps/Renamed/boot.dol", &after);
    CHECK(before.st_mtime == after.st_mtime);   /* identical: not rewritten */

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
