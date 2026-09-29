#!/bin/sh
set -eu

PROGRAM=${PROGRAM:-./simple-decision}
tmp=${TMPDIR:-/tmp}/simple-decision-docs.$$
mkdir -p "$tmp"
trap 'rm -rf "$tmp"' EXIT HUP INT TERM

for command in add list show check; do
    grep -q "simple-decision $command" README.md || {
        printf 'README omits the %s command\n' "$command" >&2
        exit 1
    }
done
grep -q 'make sanitize' README.md
grep -q 'make analyze' README.md
grep -q 'Linux' README.md
grep -q 'macOS' README.md

"$PROGRAM" help >"$tmp/help" 2>"$tmp/help-errors"
[ ! -s "$tmp/help-errors" ]
for command in add list show check; do
    grep -q "simple-decision $command" "$tmp/help"
done
[ "$("$PROGRAM" version)" = 'simple-decision 0.1.0' ]

printf 'documentation checks passed\n'

