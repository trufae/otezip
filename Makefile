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
	meson build-test && ninja -C build-test
	cp -f build-test/mzip ./mzip
	$(MAKE) -C test
	rm -rf build-test

test2:
	meson build-test -Db_sanitize=address && ninja -C build-test
	cp -f build-test/mzip ./mzip
	CFLAGS="-fsanitize=address $(CFLAGS)" $(MAKE) -C test
	rm -rf build-test

clean:
	rm -rf build

.PHONY: all clean install uninstall test test2
