CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
CFLAGS += -I../../src/include
LDFLAGS ?=

# Every test is rebuilt when any library source changes
SRCS = $(wildcard ../../src/lib/*.c) $(wildcard ../../src/include/otezip/*.h)

# Tests that #include the sources they exercise
STANDALONE = test_mzip_deflate test_lzma test_zstd test_lzfse test_deflate_reserved \
	test_deflate_dynhdr test_deflate_distance test_deflate_tail test_lzma_stream test_source_oom test_lzfse_bounds
# Tests linked against the library
LIBRARY = test_empty_zip test_zipbomb test_append_badzip test_zip_overlap test_short_deflate \
	test_set_file_compression
TESTS = test_deflate $(STANDALONE) $(LIBRARY)

# Known failures from open bugs in BUGSTODO.md (#18, #33, #37). They still run;
# one that starts passing fails the run until it is removed from this list.
XFAIL = test_set_file_compression test_lzma test_zstd

all: $(TESTS)

# Checks system zlib against the reference implementation
test_deflate: test_deflate.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lz

$(STANDALONE): %: %.c $(SRCS)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

$(LIBRARY): %: %.c $(SRCS)
	$(CC) $(CFLAGS) -o $@ $< ../../src/lib/otezip.c $(LDFLAGS)

clean:
	rm -f $(TESTS)

run: all
	@failed=0; \
	for t in $(TESTS); do \
		case " $(XFAIL) " in *" $$t "*) xfail=1 ;; *) xfail=0 ;; esac; \
		if ./$$t > $$t.log 2>&1; then \
			if [ $$xfail = 1 ]; then echo "XPASS $$t (remove it from XFAIL)"; failed=1; \
			else echo "PASS  $$t"; fi; \
		else \
			if [ $$xfail = 1 ]; then echo "XFAIL $$t"; \
			else echo "FAIL  $$t"; cat $$t.log; failed=1; fi; \
		fi; \
		rm -f $$t.log; \
	done; \
	exit $$failed

.PHONY: all clean run
