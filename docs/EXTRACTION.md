# Extraction record

`simple-decision` is derived from the decision-log component of the NBS
Framework. This record makes that origin reproducible without retaining a
runtime dependency on NBS.

## Source

- Repository: `nbs-framework`
- Revision: `a1084a3aaab1d933585438b2061ec8c86150f1c2`
- Component: `src/nbs-scribe-log/`
- Original integration tests:
  `tests/automated/test_nbs_scribe_log.sh`
- Original component introduction:
  `fca83f3139acc67906af87dc3c87c6748b9013cb`

The source revision was inspected without modification. A clean exported copy
was built with GCC on Linux and its original test suite reported `58 passed,
0 failed` across 28 scenarios on 2026-09-29.

## Retained ideas

- A deterministic command writes structured Markdown instead of relying on a
  caller to construct it correctly.
- Every field is one line, with control characters rejected before writing.
- Writers coordinate through a separate advisory lock file.
- Decisions carry participants, rationale, status, risks, artefacts, and an
  optional link to the decision they supersede.

## Deliberately removed NBS policy

- No bus event is published.
- No `.nbs` path or agent role is assumed.
- No background service is required.
- No automatic archive or retention policy is applied.
- Chat references are not part of the standalone format.

## Correctness work beyond the source

The NBS component used second-resolution timestamps for IDs. Concurrent
writes were serialised but could still receive the same ID. It appended
directly to the live file, so an I/O failure could leave a partial entry. The
standalone design assigns a monotonically increasing ID while holding the
lock and commits a complete replacement with `fsync` plus atomic `rename`.

