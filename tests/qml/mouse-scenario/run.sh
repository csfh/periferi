#!/bin/sh
# Runs the headless PeriMouse scenario against the fake peri and checks the exact order of every call (with the
# stage markers the scenario writes into the log) and the states it printed. Never touches a device.
# Usage: tests/qml/mouse-scenario/run.sh   (needs quickshell)
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
status razer-deathadder-v3-pro
MARK dpi
set razer-deathadder-v3-pro dpi=1600
MARK two
set razer-deathadder-v3-pro poll=500
set razer-deathadder-v3-pro dpi=800
MARK refused
set razer-deathadder-v3-pro poll=125
MARK replace
set razer-deathadder-v3-pro dpi=1200
MARK unchanged
status razer-deathadder-v3-pro'
if [ "$calls" = "$expected" ]; then echo "ok   the exact order of calls: no read over a queued choice, every choice written once, in order, an unchanged value never written"
else echo "FAIL the order of calls differs:"; echo "$expected" > "$FAKE_DIR/expected"; echo "$calls" > "$FAKE_DIR/got"; diff "$FAKE_DIR/expected" "$FAKE_DIR/got"; fail=1; fi
check() { if echo "$out" | grep -q -- "$2"; then echo "ok   $1"; else echo "FAIL $1"; fail=1; fi; }
check "loaded"                                        'SCN 1-loaded shown_dpi=3200 shown_poll=1000 known_dpi=3200'
check "a DPI choice is drawn at once, the model keeps the old value" 'SCN 2-chosen-immediately shown_dpi=1600 .*known_dpi=3200 .*pending={"dpi":1600}'
check "after the write the mouse own value is in the model and nothing is pending" 'SCN 3-written.* shown_dpi=1600 .*known_dpi=1600 .*pending={}'
check "a DPI choice and a poll click are both shown" 'SCN 4-two-chosen shown_dpi=800 shown_poll=500 .*pending={"dpi":800,"poll":500}'
check "both were written"                              'SCN 5-both-written shown_dpi=800 shown_poll=500 known_dpi=800 known_poll=500 pending={}'
check "a refused poll rate shows its error"            'SCN 6-after-refusal .*poll cannot be changed'
check "a refused poll rate is drawn back to what is true" 'SCN 6-after-refusal shown_dpi=800 shown_poll=500 known_dpi=800 known_poll=500 pending={}'
check "two quick DPI choices: the last one is written" 'SCN 7-after-replace shown_dpi=1200 .*known_dpi=1200 .*pending={}'
check "a value the mouse already has is not written or kept"   'SCN 8-unchanged-chosen shown_dpi=1200 .*known_dpi=1200 .*pending={}'
check "an explicit refresh reads it again"             'SCN 9-after-explicit-refresh shown_dpi=1200 shown_poll=500 known_dpi=1200 known_poll=500 pending={}'
exit $fail
