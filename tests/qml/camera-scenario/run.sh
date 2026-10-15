#!/bin/sh
# Runs the headless PeriCamera scenario against the fake peri and checks (1) the exact order of every call the
# fake received, with the stage markers the scenario writes into the log, and (2) the states it printed.
# Never touches a device. Usage: tests/qml/camera-scenario/run.sh   (needs quickshell)
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
# The whole story, in order. A `controls` right after a marker that asked for a read while a choice was queued
# or a slider was held would show up here as an extra line.
expected='MARK load
controls brio
MARK slider
set brio brightness=160
controls brio
MARK buttons
set brio white_balance_automatic=0
set brio auto_exposure=1
controls brio
MARK two-sliders
set brio brightness=150
set brio saturation=200
controls brio
MARK refused
set brio contrast=200
controls brio
MARK held-read
controls brio
MARK held-write
set brio brightness=140
MARK released
controls brio
MARK stuck-hold
controls brio'
if [ "$calls" = "$expected" ]; then
  echo "ok   the exact order of calls: reads only when allowed, every choice written once, in order"
else
  echo "FAIL the order of calls differs:"; echo "$expected" > "$FAKE_DIR/expected"; echo "$calls" > "$FAKE_DIR/got"; diff "$FAKE_DIR/expected" "$FAKE_DIR/got"; fail=1
fi

check() { if echo "$out" | grep -q -- "$2"; then echo "ok   $1"; else echo "FAIL $1"; fail=1; fi; }
check "controls load"                                    'SCN 1-loaded model_brightness=128'
check "a chosen slider value is shown at once, model still old" 'SCN 2-chosen-immediately model_brightness=128 .*shown={"brightness":160}'
check "after the write and read the model has it, nothing pending" 'SCN 3-after-write-and-read model_brightness=160 .*shown={}'
check "a second choice during a write is shown too"      '4-second-choice-during-first-write.*"white_balance_automatic":0,"auto_exposure":1'
check "both button choices were applied"                 'SCN 5-both-applied .*model_wb=0 model_ae=1 shown={}'
check "two sliders chosen 100 ms apart are both shown"   'SCN 6-two-chosen .*shown={"brightness":150,"saturation":200}'
check "both sliders were written"                        'SCN 7-both-sliders-applied model_brightness=150 .*model_sat=200 .*shown={}'
check "a refused write keeps its error visible"          'SCN 8-after-refusal .*contrast cannot be changed'
check "a refused write leaves the model true, nothing pending" 'SCN 8-after-refusal model_brightness=150 model_contrast=128 .*shown={}'
check "a read finishing while a slider is held changes nothing" 'SCN 9-read-finished-while-held.* model_sat=200 '
check "the held read is applied on release"              'SCN 10-after-release.* model_sat=77 '
check "a write finishing while held keeps the model"     'SCN 11-held-after-write.* model_brightness=150 .*shown={"brightness":140}'
check "after the release the read updates the model"     'SCN 12-after-release.* model_brightness=140 .*shown={}'
exit $fail
