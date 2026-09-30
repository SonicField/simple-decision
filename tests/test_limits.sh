#!/bin/sh
# shellcheck disable=SC2015 # The ok/bad reporters intentionally return success.
set -eu

PROGRAM=${PROGRAM:-./simple-decision}
LIMIT_PROGRAM=${LIMIT_PROGRAM:-./build/simple-decision-small-limit}
tmp=${TMPDIR:-/tmp}/simple-decision-limits.$$
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
    if [ "$actual" -eq "$expected" ]; then ok "command exits $expected";
    else bad "expected exit $expected, got $actual"; fi
}

summary_1023=$(awk 'BEGIN { for (i=0; i<1023; i++) printf "s" }')
summary_1024="${summary_1023}s"
field_2047=$(awk 'BEGIN { for (i=0; i<2047; i++) printf "p" }')
field_2048="${field_2047}p"

limit_log="$tmp/limit-boundary.md"
"$LIMIT_PROGRAM" add "$limit_log" Seed --participants=Test \
    --rationale='A valid starting log.' >/dev/null

exact_log="$tmp/exact-limit.md"
cp "$limit_log" "$exact_log"
probe_log="$tmp/exact-probe.md"
cp "$limit_log" "$probe_log"
base_size=$(wc -c <"$probe_log" | tr -d ' ')
"$PROGRAM" add "$probe_log" "$summary_1023" \
    "--participants=$field_2047" --rationale=x >/dev/null
probe_size=$(wc -c <"$probe_log" | tr -d ' ')
extra_rationale=$((4096 - base_size - (probe_size - base_size)))
exact_rationale=$(awk -v count="$((extra_rationale + 1))" \
    'BEGIN { for (i=0; i<count; i++) printf "r" }')
run_status 0 "$LIMIT_PROGRAM" add "$exact_log" "$summary_1023" \
    "--participants=$field_2047" "--rationale=$exact_rationale"
[ "$(wc -c <"$exact_log" | tr -d ' ')" -eq 4096 ] &&
    ok 'append may produce a log exactly at the size limit' ||
    bad 'exact-limit append produced the wrong size'
run_status 0 "$LIMIT_PROGRAM" check "$exact_log"

cp "$limit_log" "$tmp/limit-before.md"
run_status 1 "$LIMIT_PROGRAM" add "$limit_log" "$summary_1023" \
    "--participants=$field_2047" "--rationale=$field_2047"
cmp -s "$tmp/limit-before.md" "$limit_log" &&
    ok 'over-limit append leaves valid log byte-identical' ||
    bad 'over-limit append changed the live log'
run_status 0 "$LIMIT_PROGRAM" check "$limit_log"

run_status 0 "$PROGRAM" add "$tmp/max-summary.md" "$summary_1023" \
    --participants=Test --rationale=Why
run_status 4 "$PROGRAM" add "$tmp/long-summary.md" "$summary_1024" \
    --participants=Test --rationale=Why
run_status 0 "$PROGRAM" add "$tmp/max-field.md" Summary \
    "--participants=$field_2047" --rationale=Why
run_status 4 "$PROGRAM" add "$tmp/long-field.md" Summary \
    "--participants=$field_2048" --rationale=Why
run_status 4 "$PROGRAM" add "$tmp/empty-option.md" Summary \
    --participants= --rationale=Why
run_status 4 "$PROGRAM" add "$tmp/empty-risks.md" Summary \
    --participants=Test --rationale=Why --risk-tags=
run_status 4 "$PROGRAM" add "$tmp/empty-artefacts.md" Summary \
    --participants=Test --rationale=Why --artefacts=

carriage_return=$(printf 'bad\rvalue')
delete_character=$(printf 'bad\177value')
run_status 4 "$PROGRAM" add "$tmp/cr.md" Summary \
    "--participants=$carriage_return" --rationale=Why
run_status 4 "$PROGRAM" add "$tmp/del.md" Summary \
    "--participants=$delete_character" --rationale=Why

for invalid_id in D-0 D-01 D--1 D-1x 1 D-18446744073709551616; do
    run_status 4 "$PROGRAM" show "$tmp/max-summary.md" "$invalid_id"
done

cat >"$tmp/max-id.md" <<'EOF'
# Decision Log
Format: simple-decision/1

---
### D-18446744073709551615 Last possible ID
- **Participants:** Test
- **Status:** decided
- **Supersedes:** none
- **Risk tags:** none
- **Artefacts:** none
- **Rationale:** Exercise overflow handling.
EOF
run_status 0 "$PROGRAM" check "$tmp/max-id.md"
cp "$tmp/max-id.md" "$tmp/max-id-before.md"
run_status 1 "$PROGRAM" add "$tmp/max-id.md" Overflow \
    --participants=Test --rationale=Why
cmp -s "$tmp/max-id-before.md" "$tmp/max-id.md" &&
    ok 'ID exhaustion leaves log unchanged' || bad 'ID exhaustion changed log'

sed 's#Format: simple-decision/1#Format: simple-decision/2#' \
    "$tmp/max-summary.md" >"$tmp/version.md"
run_status 2 "$PROGRAM" check "$tmp/version.md"

sed '/^- \*\*Status:\*\*/{h;d}; /^- \*\*Supersedes:\*\*/{p;x}' \
    "$tmp/max-summary.md" >"$tmp/reordered.md"
run_status 2 "$PROGRAM" check "$tmp/reordered.md"

cp "$tmp/max-summary.md" "$tmp/trailing.md"
printf '\n' >>"$tmp/trailing.md"
run_status 2 "$PROGRAM" check "$tmp/trailing.md"

printf '# Decision Log\nFormat: simple-decision/1\n\n---\n### D-1 Bad UTF-8\n- **Participants:** A\377\n- **Status:** decided\n- **Supersedes:** none\n- **Risk tags:** none\n- **Artefacts:** none\n- **Rationale:** Why\n' >"$tmp/bad-log-utf8.md"
run_status 2 "$PROGRAM" check "$tmp/bad-log-utf8.md"

dd if=/dev/zero of="$tmp/too-large.md" bs=1 count=1 seek=67108864 2>/dev/null
run_status 1 "$PROGRAM" check "$tmp/too-large.md"

if [ "$failures" -ne 0 ]; then
    printf '%d checks passed, %d failed\n' "$checks" "$failures" >&2
    exit 1
fi
printf '%d checks passed\n' "$checks"
