ifeq ($(strip $(DEVKITPPC)),)
$(error "Please set DEVKITPPC in your environment. export DEVKITPPC=<path to>devkitPPC")
endif

PREFIX  := $(DEVKITPPC)/bin/powerpc-eabi-
CC      := $(PREFIX)gcc
AR      := $(PREFIX)ar
NM      := $(PREFIX)nm
OBJCOPY := $(PREFIX)objcopy
LIBOGC  ?= $(DEVKITPRO)/libogc

MBEDTLS := vendor/mbedtls
ARCH    := -DGEKKO -DHW_RVL -mrvl -mcpu=750 -meabi -mhard-float
CFLAGS  := -O2 -Wall -Wextra $(ARCH) -Iinclude -Iconfig -I$(MBEDTLS)/include -I$(LIBOGC)/include \
           -DMBEDTLS_CONFIG_FILE=\"mbedtls_wiiupdate.h\"
TLSFLAGS := -Os $(ARCH) -Iconfig -I$(MBEDTLS)/include -I$(MBEDTLS)/library -I$(LIBOGC)/include \
           -DMBEDTLS_CONFIG_FILE=\"mbedtls_wiiupdate.h\"

SRCS     := $(wildcard source/*.c)
OBJS     := $(patsubst source/%.c,build/%.o,$(SRCS))
TLS_SRCS := $(wildcard $(MBEDTLS)/library/*.c)
TLS_OBJS := $(patsubst $(MBEDTLS)/library/%.c,build/mbedtls/%.o,$(TLS_SRCS))
LIB      := lib/libwiiupdate.a

all: $(LIB)

# mbedTLS is linked into the archive under a private prefix (mbedtls_* ->
# wiiupdate_mbedtls_*), so apps that bring their own mbedTLS, built with a
# different configuration, link against both without clashes.
$(LIB): $(OBJS) $(TLS_OBJS)
	@mkdir -p lib build/renamed
	@$(NM) $^ | awk '$$NF ~ /^(mbedtls_|psa_|tf_psa_)/ { print $$NF " wiiupdate_" $$NF }' | sort -u > build/renamed.syms
	@for o in $^; do $(OBJCOPY) --redefine-syms=build/renamed.syms $$o build/renamed/$$(echo $$o | tr / _); done
	@rm -f $@
	$(AR) rcs $@ build/renamed/*.o

build/%.o: source/%.c include/wiiupdate.h source/wiiupdate_certs.h config/mbedtls_wiiupdate.h
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/mbedtls/%.o: $(MBEDTLS)/library/%.c config/mbedtls_wiiupdate.h
	@mkdir -p build/mbedtls
	@$(CC) $(TLSFLAGS) -c $< -o $@

install: $(LIB)
	@mkdir -p $(DESTDIR)$(DEVKITPRO)/portlibs/ppc/lib $(DESTDIR)$(DEVKITPRO)/portlibs/ppc/include
	cp $(LIB) $(DESTDIR)$(DEVKITPRO)/portlibs/ppc/lib/
	cp include/wiiupdate.h $(DESTDIR)$(DEVKITPRO)/portlibs/ppc/include/

clean:
	rm -rf build lib

.PHONY: all install clean
