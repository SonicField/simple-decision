#!/bin/sh
# shellcheck disable=SC2015 # The ok/bad reporters intentionally return success.
set -eu

PROGRAM=${PROGRAM:-./simple-decision}
tmp=${TMPDIR:-/tmp}/simple-decision-query.$$
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

checks=0
failures=0
ok() { checks=$((checks + 1)); printf 'ok %d - %s\n' "$checks" "$1"; }
bad() { failures=$((failures + 1)); printf 'not ok - %s\n' "$1" >&2; }

run_status() {
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

log="$tmp/decisions.md"
$PROGRAM add "$log" Alpha --participants=Alex --rationale=One >/dev/null
$PROGRAM add "$log" Beta --participants=Sam --rationale=Two \
    --status=accepted-risk --risk-tags=latency >/dev/null
$PROGRAM add "$log" Gamma --participants=Alex --rationale=Three \
    --status=mitigated --supersedes=D-2 >/dev/null

check_output=$($PROGRAM check "$log")
[ "$check_output" = 'OK: 3 decisions' ] && ok 'check reports exact count' ||
    bad "unexpected check output: $check_output"

$PROGRAM list "$log" >"$tmp/list"
printf 'D-1\tdecided\tAlpha\nD-2\taccepted-risk\tBeta\nD-3\tmitigated\tGamma\n' >"$tmp/want-list"
cmp -s "$tmp/list" "$tmp/want-list" && ok 'list is stable and tab-separated' ||
    bad 'list output differs'

$PROGRAM list "$log" --status=accepted-risk >"$tmp/filtered"
printf 'D-2\taccepted-risk\tBeta\n' >"$tmp/want-filtered"
cmp -s "$tmp/filtered" "$tmp/want-filtered" && ok 'status filter is exact' ||
    bad 'status filter output differs'

reversed_log="$tmp/reversed.md"
$PROGRAM add "$reversed_log" Original --participants=Alex --rationale=One >/dev/null
$PROGRAM add "$reversed_log" Replacement --participants=Sam --rationale=Two \
    --status=reversed --supersedes=D-1 >/dev/null
reversed=$($PROGRAM list "$reversed_log" --status=reversed)
[ "$reversed" = "D-2$(printf '\t')reversed$(printf '\t')Replacement" ] &&
    ok 'reversed status is accepted and filterable' ||
    bad "unexpected reversed-status output: $reversed"

$PROGRAM show "$log" D-2 >"$tmp/show"
printf '%s\n' \
    '### D-2 Beta' \
    '- **Participants:** Sam' \
    '- **Status:** accepted-risk' \
    '- **Supersedes:** none' \
    '- **Risk tags:** latency' \
    '- **Artefacts:** none' \
    '- **Rationale:** Two' >"$tmp/want-show"
cmp -s "$tmp/show" "$tmp/want-show" && ok 'show emits one canonical entry' ||
    bad 'show output differs'

run_status 3 "$PROGRAM" show "$log" D-99
[ ! -s "$tmp/stdout" ] && ok 'missing show emits no stdout' || bad 'missing show polluted stdout'
run_status 4 "$PROGRAM" show "$log" not-an-id
run_status 4 "$PROGRAM" list "$log" --status=pending
run_status 4 "$PROGRAM" list "$log" --bogus=x
run_status 1 "$PROGRAM" check "$tmp/absent.md"

printf '# Decision Log\nFormat: simple-decision/1\n' >"$tmp/header-only.md"
run_status 0 "$PROGRAM" check "$tmp/header-only.md"
grep -q '^OK: 0 decisions$' "$tmp/stdout" && ok 'empty canonical log is valid' ||
    bad 'empty canonical log count differs'

: >"$tmp/empty.md"
run_status 2 "$PROGRAM" check "$tmp/empty.md"

cp "$log" "$tmp/duplicate.md"
sed 's/### D-3 /### D-2 /' "$tmp/duplicate.md" >"$tmp/duplicate-new.md"
mv "$tmp/duplicate-new.md" "$tmp/duplicate.md"
run_status 2 "$PROGRAM" check "$tmp/duplicate.md"
run_status 2 "$PROGRAM" list "$tmp/duplicate.md"

sed '/^- \*\*Rationale:\*\* Three$/d' "$log" >"$tmp/missing-field.md"
run_status 2 "$PROGRAM" check "$tmp/missing-field.md"

sed 's/- \*\*Supersedes:\*\* D-2/- **Supersedes:** D-99/' "$log" >"$tmp/bad-link.md"
run_status 2 "$PROGRAM" check "$tmp/bad-link.md"

sed 's/- \*\*Risk tags:\*\* latency/- **Risk tags:** /' \
    "$log" >"$tmp/empty-optional.md"
run_status 2 "$PROGRAM" check "$tmp/empty-optional.md"

sed '$d' "$log" >"$tmp/truncated.md"
printf '%s' '- **Rationale:** Three' >>"$tmp/truncated.md"
run_status 2 "$PROGRAM" check "$tmp/truncated.md"

cp "$log" "$tmp/nul.md"
printf '\000' >>"$tmp/nul.md"
run_status 2 "$PROGRAM" check "$tmp/nul.md"

ln -s "$log" "$tmp/link.md"
run_status 1 "$PROGRAM" check "$tmp/link.md"

if [ "$failures" -ne 0 ]; then
    printf '%d checks passed, %d failed\n' "$checks" "$failures" >&2
    exit 1
fi
printf '%d checks passed\n' "$checks"
