# Cost-Aware Matching Experiment

## Hypothesis

Profiles 1 through 4 currently choose the longest LZSS match without considering
the number of bits required to encode its length and offset. A shorter nearby
match can therefore be cheaper than a slightly longer distant match, and some
three-byte far/huge matches cost more than three literals.

The initial experiment left profile 0's specified oldest-first search unchanged
and applied cost-aware selection to profiles 1 through 4. Results showed that
profile 1 was neutral or slightly worse, so the adopted implementation retains
profile 1's longest-match rule and applies cost-aware selection only to profiles
2 through 4. Ties prefer the longer match and then the smaller offset.

## Static cost model

A literal is estimated as one flag bit plus eight value bits, or 9 bits. Match
cost is one flag bit plus the initial, unadapted length and offset coding widths:

| Item | Estimated bits |
|---|---:|
| mini length | 5 |
| more length | 10 |
| extend length | 11 |
| near offset | 9 |
| short offset | 14 |
| medium offset | 19 |
| far offset | 24 |
| huge offset | 29 |

For example, a three-byte huge match is estimated at `1 + 5 + 29 = 35`
bits, versus 27 bits for three literals, so it is rejected.

This estimate intentionally does not inspect adaptive CARC state. It provides
a deterministic first experiment while preserving the separation between the
LZSS and CARC layers.

## Tests

Tests were added before implementation for:

- the static token cost table;
- rejecting an unprofitable three-byte far match;
- preferring a five-byte near match over a six-byte far match when its savings
  are greater;
- preserving profile 1's longest-match selection;
- retaining maximum-window matches when a four-byte match remains profitable;
- all existing round-trip, malformed-stream, maximum-window, CLI, and arithmetic
  cases.

All 43 tests pass with Visual Studio 2026 Debug. All C sources also pass GCC's
strict C99 warning-as-error syntax check.

## Ordinary source files

The `main` branch and this branch were built separately in Release mode and run
on identical inputs. Sizes include the five-byte CLI header. Negative values
are improvements.

| Sample | Profile 1 | Profile 2 | Profile 3 | Profile 4 |
|---|---:|---:|---:|---:|
| Implementation specification (29,040 B) | 0 B | -27 B (-0.24%) | -32 B (-0.29%) | -32 B (-0.29%) |
| `gtest.cc` (276,437 B) | 0 B | -466 B (-0.69%) | -956 B (-1.51%) | -964 B (-1.52%) |
| `gtest_unittest.cc` (276,288 B) | 0 B | -287 B (-0.54%) | -590 B (-1.18%) | -584 B (-1.17%) |
| `gmock-matchers.h` (235,743 B) | 0 B | -368 B (-0.77%) | -536 B (-1.18%) | -540 B (-1.19%) |

## Repeated long-distance corpora

The corpora are reproducible with `tools/generate_offset_corpora.cmake`.

| Corpus | Profile 1 | Profile 2 | Profile 3 | Profile 4 |
|---|---:|---:|---:|---:|
| 29 KiB block repeated, 1,183,200 B | 0 B | -39 B (-0.19%) | -35 B (-0.18%) | -32 B (-0.20%) |
| 276 KiB block repeated, 2,211,496 B | 0 B | -4,190 B (-0.79%) | -954 B (-1.16%) | -965 B (-1.31%) |
| >1 MiB composite block repeated, 2,318,110 B | 0 B | -3,869 B (-0.74%) | -10,644 B (-2.31%) | -4,968 B (-2.05%) |

## Conclusion

Static cost-aware matching improves every tested profile 2-4 ordinary source
file and repeated-distance corpus, without increasing probability-model memory.
The gains are broader than those from the offset-context experiment, though the
maximum specialized gain is smaller.

Profile 1's longest-match rule was therefore retained, giving byte-for-byte
identical output to `main` for every ordinary sample. The cost-aware policy was
adopted for profiles 2 through 4. Since this changes only the encoder's token
selection, the file format and decoder remain compatible; files produced by
`main` were also decoded successfully with the adopted implementation.

A later experiment could replace the static estimate with a limited lookahead
or a cost estimate derived from live CARC probabilities. That would be more
accurate but would couple the LZSS and entropy-coding layers.
