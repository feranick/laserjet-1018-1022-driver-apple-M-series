# hpzjs - native CUPS driver for HP LaserJet 1018 / 1020 / 1022 (ZjStream)
# Copyright (c) 2026 Nicola Ferralis - SPDX-License-Identifier: MIT

VERSION   := 2026.11.05.1
MODELS    := 1018 1020 1022

UNAME_S   := $(shell uname -s)
SRC       := src/rastertozjs.c src/jbig_enc.c src/halftone.c src/firmware.c
HDR       := $(wildcard src/*.h)

ifeq ($(UNAME_S),Darwin)
  # Universal binary (Apple Silicon + Intel), macOS 11 or later
  ARCHS   ?= -arch arm64 -arch x86_64
  CFLAGS  ?= -O2 -Wall -Wextra -mmacosx-version-min=11.0 $(ARCHS) \
             -Wno-deprecated-declarations
  SDK     := $(shell xcrun --show-sdk-path 2>/dev/null)
  # cupsRaster* moved from libcupsimage into libcups; link both if present
  LDLIBS  := -lcups $(if $(wildcard $(SDK)/usr/lib/libcupsimage*.tbd),-lcupsimage) -lm
  SIGN    := codesign --force --sign -
else
  # Other systems: needs the CUPS development headers (e.g. libcups2-dev)
  CFLAGS  ?= -O2 -Wall -Wextra
  LDLIBS  := -lcups -lm
  SIGN    := true
endif
CFLAGS += -DHPZJS_VERSION=\"$(VERSION)\"

PPDS := $(foreach m,$(MODELS),build/ppd/HP-LaserJet-$(m)-hpzjs.ppd)
DIST := hpzjs-$(VERSION)-macos

all: build/rastertozjs build/commandtozjs $(PPDS)

build:
	mkdir -p build/ppd

build/rastertozjs: $(SRC) $(HDR) | build
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDLIBS)
	$(SIGN) $@

build/commandtozjs: src/commandtozjs.c | build
	$(CC) $(CFLAGS) -o $@ $<
	$(SIGN) $@

# 1018/1020/1022 share the engine; only names, speed and firmware differ.
# Firmware: 1018 -> hp1018.fw, 1020 and 1022 -> hp1020.fw (the 1022 has
# its firmware in ROM; HP's 1022 driver referenced the 1020 image too).
build/ppd/HP-LaserJet-%-hpzjs.ppd: ppd/hpzjs.ppd.in | build
	ppm=12; fw=1020; \
	[ "$*" = "1022" ] && ppm=18; [ "$*" = "1020" ] && ppm=14; \
	[ "$*" = "1018" ] && fw=1018; \
	sed -e "s/@MODEL@/$*/g" -e "s/@MODELNUM@/$*/g" -e "s/@VERSION@/$(VERSION)/g" \
	    -e "s/@PPM@/$$ppm/g" -e "s/@FWMODEL@/$$fw/g" $< > $@

install: all
	sudo ./install.sh

uninstall:
	sudo ./uninstall.sh

# Binary release: dist/hpzjs-VERSION-macos.tar.gz
dist: all
	rm -rf dist/$(DIST)
	mkdir -p dist/$(DIST)/build/ppd
	cp build/rastertozjs build/commandtozjs dist/$(DIST)/build/
	cp $(PPDS) dist/$(DIST)/build/ppd/
	cp install.sh uninstall.sh README.md LICENSE dist/$(DIST)/
	tar -C dist -czf dist/$(DIST).tar.gz $(DIST)
	@echo "Created dist/$(DIST).tar.gz"

clean:
	rm -rf build dist

.PHONY: all install uninstall dist clean
