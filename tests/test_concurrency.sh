#!/bin/sh
set -eu

PROGRAM=${PROGRAM:-./simple-decision}
TEST_PROGRAM=${TEST_PROGRAM:-./build/simple-decision-test}
tmp=${TMPDIR:-/tmp}/simple-decision-concurrency.$$
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

if [ ! -x "$TEST_PROGRAM" ]; then
    printf 'not ok - test program is not executable: %s\n' "$TEST_PROGRAM" >&2
    exit 1
fi

log="$tmp/concurrent.md"
pids=
i=1
while [ "$i" -le 64 ]; do
    "$PROGRAM" add "$log" "Concurrent $i" --participants="worker-$i" \
        --rationale="Concurrent write $i" >"$tmp/id.$i" 2>"$tmp/err.$i" &
    pids="$pids $!"
    i=$((i + 1))
done
concurrent_failure=0
for pid in $pids; do
    if ! wait "$pid"; then concurrent_failure=1; fi
done
[ "$concurrent_failure" -eq 0 ] && ok 'all 64 concurrent writers succeed' ||
    bad 'a concurrent writer failed'

cat "$tmp"/id.* | sort -u >"$tmp/unique-ids"
[ "$(wc -l <"$tmp/unique-ids" | tr -d ' ')" -eq 64 ] &&
    ok 'all 64 concurrent writers receive unique IDs' ||
    bad 'concurrent IDs are not unique'
[ "$(grep -c '^### D-' "$log")" -eq 64 ] &&
    ok 'all 64 entries are structurally present' || bad 'entry count differs'
run_status 0 "$PROGRAM" check "$log"
grep -q '^OK: 64 decisions$' "$tmp/stdout" && ok 'concurrent log validates' ||
    bad 'concurrent log count differs'

before="$tmp/before.md"
cp "$log" "$before"
run_status 1 env SIMPLE_DECISION_TEST_FAIL=before-rename "$TEST_PROGRAM" add "$log" \
    'Injected failure' --participants=Test --rationale='Must not commit'
cmp -s "$before" "$log" && ok 'failure before rename leaves log byte-identical' ||
    bad 'pre-rename failure changed live log'
if find "$tmp" -name '.simple-decision.tmp.*' -print | grep . >/dev/null; then
    bad 'pre-rename failure left a temporary file'
else
    ok 'pre-rename failure cleans temporary file'
fi

run_status 1 env SIMPLE_DECISION_TEST_FAIL=after-rename "$TEST_PROGRAM" add "$log" \
    'Post-rename failure' --participants=Test --rationale='Must remain complete'
run_status 0 "$PROGRAM" check "$log"
grep -q '^OK: 65 decisions$' "$tmp/stdout" &&
    ok 'post-rename failure leaves one complete entry' ||
    bad 'post-rename failure left an unexpected state'

cp "$log" "$tmp/before-interrupt.md"
marker="$tmp/pause-marker"
env SIMPLE_DECISION_TEST_PAUSE_BEFORE_RENAME="$marker" "$TEST_PROGRAM" add "$log" \
    'Interrupted write' --participants=Test --rationale='Must not become partial' \
    >"$tmp/interrupted-stdout" 2>"$tmp/interrupted-stderr" &
interrupted_pid=$!
attempt=0
while [ ! -f "$marker" ] && [ "$attempt" -lt 500 ]; do
    sleep 0.01
    attempt=$((attempt + 1))
done
if [ -f "$marker" ]; then
    kill -KILL "$interrupted_pid"
    set +e
    wait "$interrupted_pid" 2>/dev/null
    interrupted_status=$?
    set -e
    [ "$interrupted_status" -ne 0 ] && ok 'writer is interrupted before rename' ||
        bad 'interrupted writer unexpectedly succeeded'
    cmp -s "$tmp/before-interrupt.md" "$log" &&
        ok 'interrupted writer leaves live log byte-identical' ||
        bad 'interrupted writer changed live log'
    run_status 0 "$PROGRAM" check "$log"
else
    kill -KILL "$interrupted_pid" 2>/dev/null || true
    wait "$interrupted_pid" 2>/dev/null || true
    bad 'writer did not reach the pre-rename interruption point'
fi
find "$tmp" -name '.simple-decision.tmp.*' -delete

private_log="$tmp/private.md"
(umask 077; "$PROGRAM" add "$private_log" Private \
    --participants=Test --rationale='Respect caller permissions' >/dev/null)
private_mode=$(stat -c %a "$private_log" 2>/dev/null || stat -f %Lp "$private_log")
[ "$private_mode" = 600 ] && ok 'new log respects the process umask' ||
    bad "new log mode ignored umask: $private_mode"

chmod 0600 "$log"
$PROGRAM add "$log" 'Preserve mode' --participants=Test --rationale=Mode >/dev/null
mode=$(stat -c %a "$log" 2>/dev/null || stat -f %Lp "$log")
[ "$mode" = 600 ] && ok 'transaction preserves existing file mode' ||
    bad "file mode changed to $mode"

lock_log="$tmp/lock-link.md"
touch "$tmp/lock-target"
ln -s "$tmp/lock-target" "$lock_log.lock"
run_status 1 "$PROGRAM" add "$lock_log" Summary --participants=Test --rationale=Why
[ ! -e "$lock_log" ] && ok 'symbolic-link lock prevents log creation' ||
    bad 'log was created through a symbolic-link lock'

run_status 1 "$PROGRAM" add "$tmp/missing/decision.md" Summary \
    --participants=Test --rationale=Why

bad_utf8=$(printf '\377')
run_status 4 "$PROGRAM" add "$tmp/bad-utf8.md" "$bad_utf8" \
    --participants=Test --rationale=Why

if [ "$failures" -ne 0 ]; then
    printf '%d checks passed, %d failed\n' "$checks" "$failures" >&2
    exit 1
fi
printf '%d checks passed\n' "$checks"
