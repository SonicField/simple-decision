#!/bin/sh
# shellcheck disable=SC2015 # The ok/bad reporters intentionally return success.
set -eu

PROGRAM=${PROGRAM:-./simple-decision}
PIPE_HELPER=${PIPE_HELPER:-./build/test_broken_pipe}
tmp=${TMPDIR:-/tmp}/simple-decision-output.$$
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

checks=0
failures=0
ok() { checks=$((checks + 1)); printf 'ok %d - %s\n' "$checks" "$1"; }
bad() { failures=$((failures + 1)); printf 'not ok - %s\n' "$1" >&2; }

expect_output_failure() {
    label=$1
    shift
    set +e
    "$@" 1>&- 2>"$tmp/stderr"
    status=$?
    set -e
    if [ "$status" -eq 1 ]; then
        ok "$label reports closed stdout"
    else
        bad "$label returned $status for closed stdout"
    fi
}

log="$tmp/decisions.md"
"$PROGRAM" add "$log" Seed --participants=Test --rationale=Seed >/dev/null

expect_output_failure add "$PROGRAM" add "$log" Output-failure \
    --participants=Test --rationale='The entry commits first.'
expect_output_failure check "$PROGRAM" check "$log"
expect_output_failure list "$PROGRAM" list "$log"
expect_output_failure show "$PROGRAM" show "$log" D-1
expect_output_failure help "$PROGRAM" help
expect_output_failure version "$PROGRAM" version

check_result=$($PROGRAM check "$log")
[ "$check_result" = 'OK: 2 decisions' ] &&
    ok 'failed ID delivery leaves a complete committed entry' ||
    bad "unexpected post-failure state: $check_result"

set +e
"$PIPE_HELPER" "$PROGRAM" list "$log" 2>"$tmp/pipe-error"
pipe_status=$?
set -e
[ "$pipe_status" -eq 1 ] && ok 'broken pipe is an operational failure' ||
    bad "broken pipe returned $pipe_status"

if [ "$failures" -ne 0 ]; then
    printf '%d checks passed, %d failed\n' "$checks" "$failures" >&2
    exit 1
fi
printf '%d checks passed\n' "$checks"
