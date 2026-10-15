#!/bin/sh
# Runs the headless PeriLighting scenario against the fake peri and checks the exact order of every call (with the
# stage markers the scenario writes into the log) and the states it printed. Never touches a device.
# Usage: tests/qml/lighting-scenario/run.sh   (needs quickshell)
set -u
here=$(cd "$(dirname "$0")" && pwd)
export FAKE_DIR=$(mktemp -d)
export FAKE_PERI="$here/fake-peri"
trap 'rm -rf "$FAKE_DIR"' EXIT
out=$(QT_QPA_PLATFORM=offscreen timeout 60 quickshell -p "$here" 2>&1 | sed 's/\x1b\[[0-9;]*m//g' | grep 'SCN ' | sed 's/.*SCN /SCN /')
echo "$out"
calls=$(sed 's/^[0-9.]* //' "$FAKE_DIR/calls")
echo "--- calls the fake peri received, in order:"
echo "$calls"
fail=0
expected='MARK load
rgb get
rgb effects
MARK slider
rgb set val=153
MARK coalesce
rgb set val=19
rgb set val=57
MARK groups
rgb set flags=254
rgb set flags=252
MARK race
rgb get
rgb set val=134
MARK refused
rgb set effect=bad
rgb get
MARK toggle
rgb set enable=0'
if [ "$calls" = "$expected" ]; then echo "ok   the exact order of calls: no read over a change, a dragged slider ends on its last value, two group clicks both written"
else echo "FAIL the order of calls differs:"; echo "$expected" > "$FAKE_DIR/expected"; echo "$calls" > "$FAKE_DIR/got"; diff "$FAKE_DIR/expected" "$FAKE_DIR/got"; fail=1; fi
check() { if echo "$out" | grep -q -- "$2"; then echo "ok   $1"; else echo "FAIL $1"; fail=1; fi; }
check "loaded"                                              'SCN 1-loaded brightness=52 hue=10 flags=255 lit=true effect=solid_color shown={}'
check "a slider choice is drawn at once"                    'SCN 2-chosen-immediately brightness=80 .*shown={"brightness":80}'
check "after the answer nothing is pending"                 'SCN 3-answered brightness=80 .*shown={}'
check "a dragged slider shows its last value while moving"  'SCN 4-dragged brightness=30 .*shown={"brightness":30}'
check "a dragged slider ends on its last value"             'SCN 5-dragged-answered brightness=30 .*shown={}'
check "two quick group clicks are both drawn"               'SCN 6-two-groups-chosen .*flags=252 .*shown={"flags":252}'
check "both group clicks are answered"                      'SCN 7-groups-answered .*flags=252 .*shown={}'
check "a change made while a read is in flight is drawn"    'SCN 8-changed-while-read-in-flight brightness=70'
check "a stale read does not overwrite the change"          'SCN 9-after-the-stale-read-finished.* brightness=70 '
check "a refused change shows its error"                    'SCN 11-after-refusal .*the keyboard does not know the effect'
check "a refused change is drawn back to what is true"      'SCN 11-after-refusal .*effect=solid_color shown={}'
check "the toggle is drawn at once"                         'SCN 12-toggled .*lit=false .*shown={"lit":false}'
check "the toggle is answered"                              'SCN 13-toggle-answered .*lit=false .*shown={}'
exit $fail
