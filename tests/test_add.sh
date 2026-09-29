#!/bin/sh
# shellcheck disable=SC2015 # The ok/bad reporters intentionally return success.
set -eu

PROGRAM=${PROGRAM:-./simple-decision}
tmp=${TMPDIR:-/tmp}/simple-decision-add.$$
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

pass=0
fail=0

ok() {
    pass=$((pass + 1))
    printf 'ok %d - %s\n' "$pass" "$1"
}

bad() {
    fail=$((fail + 1))
    printf 'not ok - %s\n' "$1" >&2
}

expect_status() {
    expected=$1
    shift
    set +e
    "$@" >"$tmp/stdout" 2>"$tmp/stderr"
    actual=$?
    set -e
    if [ "$actual" -eq "$expected" ]; then
        ok "exit $expected: $*"
    else
        bad "expected exit $expected, got $actual: $*"
    fi
}

if [ ! -x "$PROGRAM" ]; then
    printf 'not ok - program is not executable: %s\n' "$PROGRAM" >&2
    exit 1
fi

log="$tmp/decisions.md"
id1=$($PROGRAM add "$log" 'Choose the small design' \
    --participants='Alex, Sam' \
    --rationale='It has a bounded contract.' \
    --risk-tags=complexity \
    --artefacts=abc123)
[ "$id1" = D-1 ] && ok 'first ID is D-1' || bad "first ID was $id1"

expected="$tmp/expected"
printf '%s\n' \
    '# Decision Log' \
    'Format: simple-decision/1' \
    '' \
    '---' \
    '### D-1 Choose the small design' \
    '- **Participants:** Alex, Sam' \
    '- **Status:** decided' \
    '- **Supersedes:** none' \
    '- **Risk tags:** complexity' \
    '- **Artefacts:** abc123' \
    '- **Rationale:** It has a bounded contract.' >"$expected"
cmp -s "$expected" "$log" && ok 'canonical Markdown is exact' || bad 'canonical Markdown differs'

id2=$($PROGRAM add "$log" 'Keep the format append-only' \
    --participants=Alex --rationale='History must remain inspectable.' \
    --supersedes=D-1)
[ "$id2" = D-2 ] && ok 'second ID advances without sleeping' || bad "second ID was $id2"
grep -q '^### D-1 Choose the small design$' "$log" &&
    ok 'superseding preserves the old entry' || bad 'old entry was changed'
grep -q '^- \*\*Supersedes:\*\* D-1$' "$log" &&
    ok 'superseding records an explicit link' || bad 'supersedes link missing'

unicode_log="$tmp/unicode.md"
$PROGRAM add "$unicode_log" 'Prefer café ☕' \
    --participants='Zoë' --rationale='Readable 世界.' >/dev/null
grep -q 'Prefer café ☕' "$unicode_log" && ok 'valid UTF-8 is preserved' || bad 'UTF-8 was changed'

expect_status 4 "$PROGRAM" add "$tmp/missing.md" Summary --participants=Alex
expect_status 4 "$PROGRAM" add "$tmp/bad-status.md" Summary \
    --participants=Alex --rationale=Why --status=pending
expect_status 4 "$PROGRAM" add "$tmp/newline.md" "bad
heading" --participants=Alex --rationale=Why
expect_status 4 "$PROGRAM" add "$tmp/tab.md" Summary \
    "--participants=Alex	Sam" --rationale=Why
expect_status 4 "$PROGRAM" add "$tmp/link.md" Summary \
    --participants=Alex --rationale=Why --supersedes=D-99
expect_status 4 "$PROGRAM" add "$tmp/duplicate-option.md" Summary \
    --participants=Alex --participants=Sam --rationale=Why
expect_status 4 "$PROGRAM" unknown

if grep -R -n -E 'nbs-bus|\.nbs|auto.archive' src >/dev/null 2>&1; then
    bad 'source contains an NBS or auto-archive dependency'
else
    ok 'source has no NBS, bus, or auto-archive dependency'
fi

if [ "$fail" -ne 0 ]; then
    printf '%d checks passed, %d failed\n' "$pass" "$fail" >&2
    exit 1
fi
printf '%d checks passed\n' "$pass"
