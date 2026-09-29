# simple-decision

`simple-decision` is a small command-line tool for keeping an append-only,
human-readable decision log. It is intended for people, scripts, and AI agents
that need durable answers to “what did we decide, and why?” without a database
or service.

The interface and file format are specified in [docs/CONTRACT.md](docs/CONTRACT.md).
The relationship to the original NBS component is recorded in
[docs/EXTRACTION.md](docs/EXTRACTION.md).

## Build and install

The only runtime dependency is a POSIX environment. Build from source with a
C11 compiler:

```sh
make
make test
sudo make install
```

`PREFIX` defaults to `/usr/local`; `DESTDIR` supports packaging and staged
installs:

```sh
make install PREFIX=/usr DESTDIR="$package_root"
```

The supported platforms are Linux and macOS on x86-64 and ARM64. The CI matrix
builds and tests those four platform/architecture combinations; Linux is
tested with both GCC and Clang.

## Record a decision

```sh
simple-decision add decisions.md 'Use a recursive-descent parser' \
  --participants='Alex, Sam' \
  --rationale='The grammar is small and the implementation stays inspectable.' \
  --risk-tags=complexity \
  --artefacts=abc123
```

The command prints the new ID, such as `D-1`. Optional statuses are
`decided` (the default), `accepted-risk`, `mitigated`, and `reversed`.

A replacement is another entry, not an edit to history:

```sh
simple-decision add decisions.md 'Use a Pratt parser' \
  --participants='Alex, Sam' \
  --rationale='The expression grammar grew beyond the original assumption.' \
  --supersedes=D-1
```

## Validate and query

```sh
simple-decision check decisions.md
simple-decision list decisions.md
simple-decision list decisions.md --status=accepted-risk
simple-decision show decisions.md D-2
```

`list` is deliberately plain: ID, status, and summary separated by tabs. It is
pleasant to read directly and straightforward to compose with `cut`, `awk`, or
another program. The Markdown log remains the source of truth.

## Human and agent workflow

The useful discipline is the same for a person or an AI agent:

1. Record decisions, not routine status updates.
2. State who participated and why the choice was made.
3. Attach concrete artefacts and risk tags when they help later review.
4. Use `--supersedes` when a later decision replaces an earlier one.
5. Run `simple-decision check` after manual edits and before consuming a log.

An agent can call the executable directly. There is no prompt format, provider
API, background process, hidden state, or project-specific directory.

## Verification

```sh
make test       # real CLI, malformed logs, concurrency, and failure injection
make sanitize   # AddressSanitizer and UndefinedBehaviorSanitizer
make analyze    # GCC static analyser
```

Builds enable `-Wall -Wextra -Wshadow -Werror`. Tests challenge concurrent ID
allocation, interrupted and failed transactions, malformed Markdown, invalid
links, Unicode, symbolic links, permission preservation, and command errors.

## Deliberate limitations

- One log is rewritten transactionally for each add, so this is intended for
  small decision records rather than high-volume event ingestion.
- The whole log is read into memory and is limited to 64 MiB.
- There is no search language; `list`, `show`, and ordinary text tools cover
  the intended use.
- There is no automatic archive or retention policy.
- Advisory locking coordinates `simple-decision` writers, not unrelated tools
  that ignore the lock file.
- Parent-directory symbolic links are allowed; the log and lock paths
  themselves are rejected when they are symbolic links.
- Unicode is validated but not normalised, so visually similar strings can
  remain bytewise distinct.

## Licence

MIT. See [LICENSE](LICENSE).
