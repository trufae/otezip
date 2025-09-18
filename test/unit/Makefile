CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra
LDFLAGS ?=

# Define test targets
TESTS = test_deflate test_mzip_deflate test_zstd test_lzfse

all: $(TESTS)

test_deflate: test_deflate.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS) -lz

test_mzip_deflate: test_mzip_deflate.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_zstd: test_zstd.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

test_lzfse: test_lzfse.c
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
