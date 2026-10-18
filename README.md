# libwiiupdate

Checks GitHub for a newer release of a Wii homebrew app, built on libogc.

- Runs on its own thread: brings the network up, asks the GitHub releases API
  over HTTPS and compares the newest release tag with the running version
- Ships its own small mbedTLS (TLS 1.2, certificate chain and host name
  verified against the bundled roots; dates are not checked since many Wii
  clocks are wrong). Its symbols are renamed inside the archive, so apps that
  link their own mbedTLS are unaffected
- Picks the highest numbered, non-prerelease tag, so rolling tags such as
  `latest` are ignored
- Failures are quiet: no network or no reply just means no notice

## Build

Requires devkitPro with devkitPPC and libogc.

```sh
export DEVKITPRO=/opt/devkitpro DEVKITPPC=/opt/devkitpro/devkitPPC
make            # produces lib/libwiiupdate.a
make install    # copies into $DEVKITPRO/portlibs/ppc
```

## Usage

### Text (console) apps

Once at boot, after the console and controllers are up:

```c
#include <wiiupdate.h>

WIIUPDATE_CheckConsole("quatric", "MyApp", APP_VERSION, NULL);
```

It prints `Checking for updates (B to skip)...` followed by `up to date.`,
`skipped.` or `could not check.` and returns. Only when a newer release exists
does it stop, showing the new version and where to get it until A/B is pressed
(or 10 seconds pass).

The last argument returns the `WPAD_BUTTON_*` bits pressed this frame. With
`NULL` the library scans Wii Remotes, Classic Controllers and GameCube pads
itself; apps on libinput-wii pass a small wrapper instead:

```c
static u32 buttons(void) { INPUT_Update(); return INPUT_ButtonsDown(); }
WIIUPDATE_CheckConsole("quatric", "MyApp", APP_VERSION, buttons);
```

### GUI apps

Start the check at launch and show a popup when it finds something:

```c
WIIUPDATE_Start("quatric", "MyApp", APP_VERSION);

while (running) {
    if (WIIUPDATE_TakeNotice()) popup_open = true;  /* true once */
    if (popup_open) {
        draw_box(WIIUPDATE_Message());              /* "Version v1.3.0 is available (you have v1.2.0)." */
        draw_text(WIIUPDATE_Url());                 /* "github.com/quatric/MyApp/releases" */
        if (buttons_down & (WPAD_BUTTON_A | WPAD_BUTTON_B)) popup_open = false;
    }
}
```

### Other calls

* `WIIUPDATE_State()`: `CHECKING`, `UP_TO_DATE`, `AVAILABLE`, `FAILED` or `CANCELLED`.
* `WIIUPDATE_Wait(ms)`: block until the check is done. Apps that bring up the
  network themselves should call this first so the two do not race.
* `WIIUPDATE_Cancel()`: drop the result; the thread winds down by itself.
* `WIIUPDATE_Latest()`, `WIIUPDATE_Current()`, `WIIUPDATE_CompareVersions(a, b)`.

### Using it from an app

Add it as a git submodule, build it before the app and link it:

```sh
git submodule add https://github.com/quatric/libwiiupdate.git libwiiupdate
```

```make
LIBS    := -lwiiupdate $(LIBS)        # ahead of -lwiiuse -lbte -logc
LIBDIRS += $(CURDIR)/libwiiupdate     # include/ and lib/

# after the $(BUILD) rule, so it stays the default goal
$(BUILD): libwiiupdate-lib
.PHONY: libwiiupdate-lib
libwiiupdate-lib:
	@$(MAKE) --no-print-directory -C libwiiupdate
```

The library builds from its own sources (mbedTLS is vendored), so CI needs
nothing beyond devkitPPC and `submodules: true` on checkout.

See `example/main.c` for a complete program.

## Tests

`tests/host_test.c` checks version comparison and release parsing on a desktop;
`tests/Makefile.host` builds the whole network path against plain sockets for an
end-to-end run against GitHub:

```sh
cc -Iinclude -Itests/stub tests/host_test.c source/wiiupdate_version.c -o build/host_test && ./build/host_test
make -f tests/Makefile.host && ./build/host_check quatric GodModeWii 0.1.0
```

## Root certificates

`source/wiiupdate_certs.h` is generated from the Mozilla CA bundle:

```sh
curl -sSLf -o cacert.pem https://curl.se/ca/cacert.pem
python3 scripts/gen_certs.py cacert.pem > source/wiiupdate_certs.h
```

## License

AGPL-3.0, see `LICENSE`. mbedTLS in `vendor/mbedtls` is Apache-2.0 (or
GPL-2.0-or-later), see `vendor/mbedtls/LICENSE`.
