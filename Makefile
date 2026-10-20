CC?=gcc
CFLAGS?=-O2 -Wall -Wextra -std=c99
DESTDIR?=
PREFIX?=/usr/local
BINDIR?=$(PREFIX)/bin
AR?=ar
EXT_AR?=a
RANLIB?=ranlib

OTEZIP_OBJS=src/lib/otezip.o

all: otezip libotezip.$(EXT_AR)

libotezip.$(EXT_AR): src/lib/otezip.o
	$(AR) rc $@ $(OTEZIP_OBJS)
	$(RANLIB) $@

otezip: src/main.c src/lib/otezip.c src/include/otezip/zip.h src/include/otezip/config.h
	$(CC) $(CFLAGS) -I src/include -o otezip src/main.c src/lib/otezip.c

mall:
	meson build && ninja -C build
	# Ensure a convenient top-level binary exists for quick invocation
	cp -f build/otezip ./otezip

install: otezip
	mkdir -p $(DESTDIR)$(BINDIR)
	cp -f otezip $(DESTDIR)$(BINDIR)/otezip

uninstall:
	rm -f $(DESTDIR)$(BINDIR)/otezip

test:
	meson build && ninja -C build
	cp -f build/otezip ./otezip
	$(MAKE) -C test
	rm -rf build

# Tests under ASan and UBSan; undefined behavior aborts the run
SAN_FLAGS=-fsanitize=address,undefined -fno-sanitize-recover=undefined
test2:
	meson build -Db_sanitize=address,undefined && ninja -C build
	cp -f build/otezip ./otezip
	UBSAN_OPTIONS=halt_on_error=1 CFLAGS="$(SAN_FLAGS) $(CFLAGS)" $(MAKE) -C test
	rm -rf build

asan: clean
	meson build -Db_sanitize=address && ninja -C build
	cp -f build/otezip ./otezip
	cp -f build/libotezip.$(EXT_AR) ./libotezip.$(EXT_AR)

clean:
	rm -rf build otezip libotezip.$(EXT_AR) $(OTEZIP_OBJS)

# Object file build rules
src/lib/otezip.o: src/lib/otezip.c src/include/otezip/zip.h src/include/otezip/config.h
	$(CC) $(CFLAGS) -fPIC -I src/include -c src/lib/otezip.c -o $@

fmt indent:
	find . -name "*.c" -exec clang-format-radare2 -i {} \;

.PHONY: all mall clean install uninstall test test2 asan fmt-indent
