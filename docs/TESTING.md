# Contract verification

This document maps the externally observable claims in
[`CONTRACT.md`](CONTRACT.md) to tests that would fail if the behavior changed.
Descriptions refer to the named checks printed by the shell suites.

## Commands and output

| Contract claim | Direct evidence |
|---|---|
| `add` writes a canonical entry and prints its ID | `first ID is D-1`; `canonical Markdown is exact` |
| Optional fields and defaults are represented canonically | `canonical Markdown is exact`; `second ID advances without sleeping` |
| All four statuses are accepted and exact filtering is supported | `status filter is exact`; `reversed status is accepted and filterable` |
| `list` emits ID, status, and summary in file order | `list is stable and tab-separated` |
| `show` emits one canonical entry without a separator | `show emits one canonical entry` |
| `check` reports the exact decision count | `check reports exact count`; `empty canonical log is valid` |
| Help and version commands and aliases work and reject extra arguments | `tests/test_docs.sh`; the four trailing-argument checks in `tests/test_add.sh` |
| Normal output and diagnostics use separate streams | exact-output checks throughout `tests/test_add.sh` and `tests/test_query.sh` |
| Every output-producing command detects closed stdout | six named checks in `tests/test_output.sh` |
| Broken pipes become status 1 rather than `SIGPIPE` termination | `broken pipe is an operational failure` |

## Format and history

| Contract claim | Direct evidence |
|---|---|
| IDs are unpadded positive decimals, strictly increasing in file order | invalid-ID loop; `duplicate.md`; `second ID advances without sleeping` |
| Fields have exactly the documented order and all values are non-empty | `missing-field.md`; `reordered.md`; empty required and optional-field checks |
| Text is valid UTF-8 without ASCII controls | `valid UTF-8 is preserved`; bad UTF-8, newline, tab, carriage-return, and DEL checks |
| Supersedes is `none` or an earlier ID | `superseding records an explicit link`; `bad-link.md`; missing-link add check |
| A valid log ends with a newline and has no extra trailing records | `truncated.md`; `trailing.md` |
| A successful add preserves existing history and appends a new entry | `superseding preserves the old entry`; canonical output comparisons |
| Superseding does not edit the earlier entry | `superseding preserves the old entry`; `superseding records an explicit link` |

## Transactions, concurrency, and paths

| Contract claim | Direct evidence |
|---|---|
| Concurrent writers receive distinct IDs and leave a valid complete log | the three 64-writer checks plus `concurrent log validates` |
| A pre-rename failure or interruption leaves the live log byte-identical | the before-rename injection and interrupted-writer checks |
| A reader during a paused transaction sees the complete previous log | `reader sees the complete previous log before rename` |
| A post-rename failure leaves one complete new entry | `post-rename failure leaves one complete entry` |
| Failed ID delivery can occur after a committed add | `failed ID delivery leaves a complete committed entry` |
| Log and lock paths reject symbolic links | symbolic-link lock and symbolic-link log checks |
| Parent-directory symbolic links are allowed | the two parent-directory symbolic-link checks |
| New files respect umask; replacement preserves ordinary mode bits and clears special bits | three named permission checks in `tests/test_concurrency.sh` |

## Limits and status codes

| Contract claim | Direct evidence |
|---|---|
| Summary and field byte limits are enforced at their boundaries | maximum and over-limit cases in `tests/test_limits.sh` |
| The log may reach exactly 64 MiB but may not exceed it | exact-limit and over-limit transaction tests using the reduced-limit build; oversized sparse-file check |
| ID exhaustion is reported without changing the log | `ID exhaustion leaves log unchanged` |
| Status 1 reports operational failures | missing path, symlink, output, transaction, and size-limit checks |
| Status 2 reports structurally invalid logs | malformed format, ordering, link, truncation, NUL, and UTF-8 checks |
| Status 3 reports a missing decision | missing `show` check |
| Status 4 reports invalid arguments | option, status, text, field-limit, and ID checks |

Durability across immediate power loss is explicitly outside the executable
suite. The `fsync` and rename ordering is reviewed in `src/decision.c`, while
process-level failures on either side of rename are tested directly.
