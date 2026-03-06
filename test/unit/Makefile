CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += -I../../src/include
LDFLAGS ?=

# Define test targets
TESTS = test_deflate test_mzip_deflate test_lzma test_zstd test_lzfse test_empty_zip

all: $(TESTS)

test_deflate: test_deflate.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lz

test_mzip_deflate: test_mzip_deflate.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_zstd: test_zstd.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_lzma: test_lzma.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_lzfse: test_lzfse.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_empty_zip: test_empty_zip.c ../../src/lib/otezip.c ../../src/include/otezip/zip.h ../../src/include/otezip/config.h
	$(CC) $(CFLAGS) -o $@ $< ../../src/lib/otezip.c $(LDFLAGS)

clean:
	rm -f $(TESTS)

run: all
	@echo "Running unit tests..."
	@for test in $(TESTS); do \
		echo "\nRunning $$test:"; \
		./$$test; \
	done

.PHONY: all clean run
