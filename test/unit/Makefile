CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += -I../../src/include
LDFLAGS ?=

# Define test targets
TESTS = test_deflate test_mzip_deflate test_lzma test_zstd test_lzfse test_empty_zip test_zipbomb test_deflate_reserved test_deflate_dynhdr test_append_badzip test_deflate_distance test_zip_overlap test_lzma_stream test_deflate_tail test_source_oom

all: $(TESTS)

test_deflate: test_deflate.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lz

test_mzip_deflate: test_mzip_deflate.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_deflate_reserved: test_deflate_reserved.c ../../src/lib/deflate-dec.inc.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_deflate_dynhdr: test_deflate_dynhdr.c ../../src/lib/deflate-dec.inc.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_deflate_distance: test_deflate_distance.c ../../src/lib/deflate-dec.inc.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_deflate_tail: test_deflate_tail.c ../../src/lib/deflate-dec.inc.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_zstd: test_zstd.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_lzma: test_lzma.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_lzma_stream: test_lzma_stream.c ../../src/lib/lzma-dec.inc.c ../../src/lib/lzma-enc.inc.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_lzfse: test_lzfse.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_empty_zip: test_empty_zip.c ../../src/lib/otezip.c ../../src/include/otezip/zip.h ../../src/include/otezip/config.h
	$(CC) $(CFLAGS) -o $@ $< ../../src/lib/otezip.c $(LDFLAGS)

test_zipbomb: test_zipbomb.c ../../src/lib/otezip.c ../../src/include/otezip/zip.h ../../src/include/otezip/config.h
	$(CC) $(CFLAGS) -o $@ $< ../../src/lib/otezip.c $(LDFLAGS)

test_append_badzip: test_append_badzip.c ../../src/lib/otezip.c ../../src/include/otezip/zip.h ../../src/include/otezip/config.h
	$(CC) $(CFLAGS) -o $@ $< ../../src/lib/otezip.c $(LDFLAGS)

test_zip_overlap: test_zip_overlap.c ../../src/lib/otezip.c ../../src/include/otezip/zip.h ../../src/include/otezip/config.h
	$(CC) $(CFLAGS) -o $@ $< ../../src/lib/otezip.c $(LDFLAGS)

test_source_oom: test_source_oom.c ../../src/lib/otezip.c ../../src/include/otezip/zip.h ../../src/include/otezip/config.h
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TESTS)

run: all
	@echo "Running unit tests..."
	@for test in $(TESTS); do \
		echo "\nRunning $$test:"; \
		./$$test; \
	done

.PHONY: all clean run
