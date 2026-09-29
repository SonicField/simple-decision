# `simple-decision` contract

## Purpose and boundary

`simple-decision` manages one append-only Markdown decision log. It records
small, explicit decisions and lets humans or programs validate and query them.
It does not run a service, send events, call an AI system, rotate files, or
apply a retention policy.

## Commands

```text
simple-decision add LOG SUMMARY --participants=TEXT --rationale=TEXT [OPTIONS]
simple-decision list LOG [--status=STATUS]
simple-decision show LOG ID
simple-decision check LOG
simple-decision help
simple-decision version
```

`add` accepts these optional fields:

```text
--artefacts=TEXT
--risk-tags=TEXT
--status=decided|accepted-risk|mitigated|reversed
--supersedes=ID
```

The default status is `decided`; the other optional text fields default to
`none`. `--supersedes` must name an earlier entry in the same valid log.

`list` emits one tab-separated line per decision in file order:

```text
ID<TAB>STATUS<TAB>SUMMARY
```

`--status` filters by exact status. `show` emits one canonical Markdown entry
without its separator. `check` emits `OK: N decisions` for a valid log. Normal
results go to standard output and diagnostics go to standard error.

## File format

The complete version 1 grammar is:

```markdown
# Decision Log
Format: simple-decision/1

---
### D-1 Choose the small implementation
- **Participants:** Alex, Sam
- **Status:** decided
- **Supersedes:** none
- **Risk tags:** complexity
- **Artefacts:** abc123
- **Rationale:** It has the smallest independently testable contract.
```

- IDs are `D-` followed by an unpadded positive decimal integer.
- IDs increase strictly in file order. `add` assigns one more than the largest
  existing ID while holding the log lock.
- Each field occupies exactly one line in the order shown above.
- Summary, participants, and rationale must not be empty.
- Text must be valid UTF-8 and must not contain ASCII control characters.
- A supersedes value is either `none` or the ID of an earlier entry.
- The file must end with a newline.

Any manual edit is allowed if the resulting file still satisfies this
grammar. `check` is the authority for that question.

## Append-only meaning

Each successful `add` preserves every byte of the existing valid log and adds
one separator and entry at the end. Superseding a decision adds a new entry
whose `Supersedes` field links to the older entry. It does not rewrite the old
entry or change its status.

The implementation may replace the filesystem object transactionally to make
the append durable. “Append-only” describes the logical history, not the
particular write system call.

## Concurrency and durability

Writers serialise through an exclusive advisory lock on `LOG.lock`. A writer
validates the current log, chooses its ID, writes the complete new contents to
a same-directory temporary file, calls `fsync`, atomically renames the file,
and then calls `fsync` on the containing directory.

A reported successful add is therefore visible as one complete entry. A
failure before rename leaves the previous log intact. A directory-sync error
after rename is reported even though the new, structurally complete version
may be visible; durability across immediate power loss is then unknown.

The log and lock path themselves must not be symbolic links. Parent-directory
symbolic links are not rejected. Readers do not take the writer lock: atomic
rename means they see either the previous complete file or the next complete
file. A new log's permissions respect the process umask; later transactions
preserve the existing log's permission bits.

## Limits

- Log path: at most 4095 bytes.
- Summary: at most 1023 bytes.
- Each other text field: at most 2047 bytes.
- Log input: at most 64 MiB.
- Number of decisions: limited by available memory and the maximum ID
  `D-18446744073709551615`.

The program processes bytes and validates UTF-8; it does not normalise Unicode.
Visually similar strings may therefore remain distinct.

## Exit status

| Status | Meaning |
|---:|---|
| 0 | Success |
| 1 | Filesystem, allocation, locking, or other operational failure |
| 2 | The log is structurally invalid |
| 3 | Requested decision does not exist |
| 4 | Invalid command-line arguments |
