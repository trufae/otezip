CC?=gcc
CFLAGS?=-O2 -Wall
DESTDIR?=
PREFIX?=/usr/local
BINDIR?=$(PREFIX)/bin

all: mzip

mzip: main.c mzip.c mzip.h config.h
	meson build && ninja -C build
	# Ensure a convenient top-level binary exists for quick invocation
	cp -f build/mzip ./mzip

install:
	mkdir -p $(DESTDIR)$(BINDIR)
	cp -f build/mzip $(DESTDIR)$(BINDIR)/mzip

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/mzip

test:
	$(MAKE) -C test

clean:
	rm -rf build

.PHONY: all clean install uninstall test
