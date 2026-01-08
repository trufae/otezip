# Agent Instructions for the OTEZIP project

## Project Structure & Module Organization

- Source: `src/lib/otezip.c`, `src/main.c`, headers in `src/include/otezip/zip.h`, config in `src/include/otezip/config.h`.
- Algorithms: minimalistic, but fully functional implementations for every compression algorithm `src/lib/*.inc.c` (deflate, lzma, brotli, zstd, lzfse).
- Tests: integration script `test/test.sh` and unit tests in `test/unit/`. Sample assets in `test/` (e.g., `test.zip`, sample files).

## Build, Test, and Development Commands

- Build default binary: `make`
- Build with more algorithms: `make all-compression` (or toggle defines in `config.h`)
- Install/uninstall: `make install` / `make uninstall`
- Run tests: `make -C test` (builds unit tests, runs integration script)
- Quick usage examples:
  - Create: `./otezip -c out.zip file1 file2 -z1` (deflate)
  - List: `./otezip -l out.zip`
  - Extract: `./otezip -x out.zip`

## Coding Style & Naming Conventions

- Language: C (C99). Aim for warning-free builds with `-Wall`/`-Wextra`.
- Indentation: tabs preferred (match existing files). Keep lines concise.
- File naming: core in `otezip.*`; algorithms as `name-*.inc.c`; tests as `test_*.c` and `test_*.sh`.
- Preprocessor: guard features with `OTEZIP_ENABLE_*` and reuse `OTEZIP_METHOD_*` IDs from `config.h`.

## Testing Guidelines
- Unit tests: add focused tests in `test/unit/` and register in `test/unit/Makefile`.
- Integration: extend `test/test.sh` for end-to-end flows (create, list, extract). For nonstandard ZIP methods (e.g., Brotli=97), prefer verifying with `otezip` extraction; `unzip` may not support them.
- Run locally with `make -C test`; ensure added tests are deterministic and cleanup temp dirs.

## Commit & Pull Request Guidelines
- Commits: short, imperative summaries (e.g., "Fix deflate edge case"). Group related changes and keep scope tight.
- PRs: include a clear description, rationale, and a brief list of changes. Link issues where applicable.
- Verification: show how you built and tested (commands/output), and note any algorithm flags changed.

## Configuration Tips
- Enable/disable algorithms via `config.h` or `COMPRESSION_FLAGS` in the Makefile. Ensure fallbacks work (code auto-falls back to STORE when compression is ineffective).
