# LZRC Work Log

## 2026-08-07

- Reviewed and clarified the implementation specification before coding.
- Established the test-first workflow, CMake presets, and GoogleTest/CTest test target.
- Added exhaustive tests for `Mul8x8` and both 16-bit bound implementations, plus 32-bit boundary tests, before adding their implementation.
- Implemented the square-table multiplication and bound functions. Visual Studio 2026 Debug passed all 3 arithmetic tests, including exhaustive 16-bit bound verification.
- Added tests first for adaptive 16-bit and 32-bit carryless range coders, including table/native byte equivalence and buffer errors.
- Implemented both range coder widths and fixed C linkage at the test boundary. Visual Studio 2026 Debug passed all 11 tests.
- Added tests first for 1- through 8-bit tree round trips on table-16, native-16, and native-32 cores.
- Implemented the profile-selecting range facade and zero-based storage for the specification's conceptual 1-origin bit-tree contexts. Visual Studio 2026 Debug passed all 16 tests.
- Added CARC tests first for profile-specific context counts, every length/offset boundary, and invalid tokens.
- Implemented CARC token encoding/decoding with fixed MSB-first direct bits and exact profile context storage. Visual Studio 2026 Debug passed all 20 tests.
- Added LZSS tests first for profile parameters, profile 0's oldest-first search, higher-profile longest/nearest selection, overlap, maximum lengths, and invalid streams.
- Implemented profile 0's one-pass search and profile 1-4 hash-chain matching. Corrected one test fixture whose trailing byte made its asserted token non-final. Visual Studio 2026 Debug passed all 27 tests.
- Added public API and CLI tests first for all-profile round trips, deterministic output, errors, the five-byte container, and `c0`-`c4`/`d` commands.
- Implemented the public buffer API and CLI. Visual Studio 2026 Debug passed all 34 tests.
- Added real maximum-window boundary tests, including profile 4's 16 MiB limit, and maximum-match split tests. Visual Studio 2026 Debug passed all 36 tests.
- Added public build, CLI, and library API documentation, install rules, and Linux CI using the Linux CMake preset.
- Added malformed unary/unused-offset rejection and deterministic random boundary-size tests. Visual Studio 2026 Debug passed all 39 tests.
- Verified all C sources with GCC in strict C99 warning-as-error syntax mode and verified the CMake install layout for the library, CLI, and public headers.
- Measured the 29,040-byte implementation specification as a reference corpus: profile 0 produced 17,331 bytes (59.68%), profile 1 12,510 bytes (43.08%), profile 2 11,370 bytes (39.15%), and profiles 3/4 11,213 bytes (38.61%).

## 2026-08-07 — Cost-aware matching experiment

- Created `codex/cost-aware-matching-experiment` from `main`; the clean branch baseline passed all 39 tests.
- Added tests first for static token costs, unprofitable far matches, and choosing a shorter near match with greater estimated savings.
- Implemented static cost-aware candidate selection for profiles 1-4 while preserving profile 0's specified search.
- Corrected one test fixture after a preceding zero run unintentionally formed a match across the intended token boundary; cost expectations were unchanged.
- Passed all 42 tests and compared independent Release builds of `main` and the experiment.
- Profiles 2-4 improved across all tested ordinary and generated corpora, by up to 2.31%; profile 1 remained effectively neutral with small regressions in several samples. Full results are in `COST_AWARE_MATCHING_EXPERIMENT.md`.
- Added a failing regression test for profile 1's longest-match rule, then limited cost-aware selection to profiles 2-4. All 43 tests passed.
- Verified that profile 1 output is byte-for-byte identical to `main` on all ordinary samples and that the new decoder restores `main`-produced profile 2-4 streams exactly.
- Updated the implementation specification and experiment report, then selected the profile 2-4 policy for integration into `main`.
