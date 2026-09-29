# Verification record

This record captures observed results, including checks that could not be run.
Passing checks show that the implementation resisted these falsification
attempts; they do not prove correctness on every filesystem or platform.

## Goal and falsifiers

The goal is an NBS-independent, append-only Markdown decision log with a small
CLI and no service or retention policy. The implementation would fail that
goal if concurrent writers reused an ID, an interrupted write corrupted the
live log, invalid structure was silently queried, supersession rewrote
history, or any command depended on NBS.

## Upstream baseline

Source revision:
`a1084a3aaab1d933585438b2061ec8c86150f1c2` in `nbs-framework`.

On 2026-09-29, the component, common headers, bus dependency, and original
test were exported with `git archive` to a temporary directory. Building
`nbs-bus`, then running:

```sh
make -C <export>/src/nbs-scribe-log test
```

reported 58 passes and 0 failures across 28 scenarios. The source repository
was not modified.

## Standalone evidence

Observed locally on Linux 6.16, ARM64, with GCC 11.5:

| Command | Observed result |
|---|---|
| `make test CC=gcc` | Passed 74 CLI/adversarial checks plus documentation checks |
| `make analyze CC=gcc` | GCC `-fanalyzer` completed with warnings treated as errors |
| `shellcheck tests/*.sh` | Completed without findings |
| `make sanitize CC=gcc` | ASan and UBSan suite passed, including 64 concurrent writers |
| staged `make install` | Installed binary reported 0.1.0 and completed add/check smoke test |

The tests specifically observed:

- 64 simultaneous processes all succeeded and received 64 distinct IDs;
- the resulting 64-entry log passed strict validation;
- an injected failure before rename left the live log byte-identical;
- killing a writer after temporary-file sync but before rename left the live
  log byte-identical and valid;
- an injected failure after rename left one complete, valid new entry;
- malformed, truncated, reordered, duplicate-ID, bad-link, NUL, and invalid
  UTF-8 logs were rejected;
- boundary-length fields, ID exhaustion, symbolic links, umask handling, and
  permission preservation behaved as documented.

## Not yet observed

- Clang was not installed in the local environment, so the local Clang build
  attempt stopped with `clang: No such file or directory`.
- `actionlint` was not installed, so the workflow received manual inspection
  but no local actionlint result.
- The repository was deliberately not published or pushed. Consequently, the
  six-platform GitHub Actions matrix has not run at this commit.
- No immediate-power-loss test was performed. Filesystem and hardware write
  caches remain outside the scope of process-level failure injection.

