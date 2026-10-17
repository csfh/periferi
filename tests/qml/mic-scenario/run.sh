#!/bin/sh
# Runs the headless PeriMic scenario against the fake peri and checks the calls of each stage (the reads of a refresh
# start together, so their order is not fixed: they are compared sorted) and the states it printed. Never touches a
# device. Usage: tests/qml/mic-scenario/run.sh   (needs quickshell)
set -u
here=$(cd "$(dirname "$0")" && pwd)
export FAKE_DIR=$(mktemp -d)
export FAKE_PERI="$here/fake-peri"
trap 'rm -rf "$FAKE_DIR"' EXIT
out=$(QT_QPA_PLATFORM=offscreen timeout 90 quickshell -p "$here" 2>&1 | sed 's/\x1b\[[0-9;]*m//g' | grep 'SCN ' | sed 's/.*SCN /SCN /')
echo "$out"
calls=$(sed 's/^[0-9.]* //' "$FAKE_DIR/calls")
echo "--- calls the fake peri received, in order:"
echo "$calls"
fail=0
segment() { echo "$calls" | awk -v m="MARK $1" '$0==m{f=1;next} /^MARK /{f=0} f'; }
same() { # name, expected (exact order)
  if [ "$(segment "$1")" = "$2" ]; then echo "ok   $1: $3"; else echo "FAIL $1:"; echo "$2" > "$FAKE_DIR/e"; segment "$1" > "$FAKE_DIR/g"; diff "$FAKE_DIR/e" "$FAKE_DIR/g"; fail=1; fi
}
sorted() { # name, expected (any order)
  if [ "$(segment "$1" | sort)" = "$(echo "$2" | sort)" ]; then echo "ok   $1: $3"; else echo "FAIL $1:"; echo "$2" | sort > "$FAKE_DIR/e"; segment "$1" | sort > "$FAKE_DIR/g"; diff "$FAKE_DIR/e" "$FAKE_DIR/g"; fail=1; fi
}
reads='status mic
look mic get unmuted
look mic get muted
audio mic get'
sorted load "$reads" "the four reads of a refresh"
same brightness 'look mic save unmuted effect=static color=0000ff brightness=153' "one save"
same three-saves 'look mic save unmuted effect=static color=0000ff brightness=179
look mic save muted effect=static color=ff0000 brightness=77
look mic save unmuted effect=static color=0000ff brightness=204' "three saves on two looks, none lost, in order"
# The refused save and the one queued behind it go in order, then the pane reads what is really saved.
if [ "$(segment failure | head -2)" = 'look mic save unmuted effect=static color=0000ff brightness=99
look mic save muted effect=static color=ff0000 brightness=26' ] && [ "$(segment failure | tail -n +3 | sort)" = "$(echo "$reads" | sort)" ]; then
  echo "ok   failure: the refused save, the newer save behind it, then one refresh"
else echo "FAIL failure:"; segment failure; fail=1; fi
# The held refresh, then the save and the audio change made while it was held.
if [ "$(segment races | head -4 | sort)" = "$(echo "$reads" | sort)" ] && [ "$(segment races | tail -n +5)" = 'look mic save muted effect=static color=ff0000 brightness=128
audio mic set gain=40' ]; then echo "ok   races: the held reads, then the save and the audio change"
else echo "FAIL races:"; segment races; fail=1; fi

check() { if echo "$out" | grep -q -- "$2"; then echo "ok   $1"; else echo "FAIL $1"; fail=1; fi; }
check "loaded"                                            'SCN 1-loaded editing=unmuted unmuted=39 muted=10 gain=65'
check "a slider choice is drawn at once"                  'SCN 2-chosen-immediately editing=unmuted unmuted=60 '
check "saved"                                             'SCN 3-saved editing=unmuted unmuted=60 '
check "all three saves are drawn at once"                 'SCN 4-three-chosen editing=unmuted unmuted=80 muted=30 '
check "all three saves landed"                            'SCN 5-three-saved editing=unmuted unmuted=80 muted=30 '
check "a refused save shows its error"                    'SCN 6-after-the-refused-save .*brightness 99 is refused'
check "a refused save is read back as what is really saved, the newer save kept" 'SCN 6-after-the-refused-save editing=muted unmuted=80 muted=10 '
check "changes made while reads are in flight are drawn"  'SCN 7-changed-while-reads-in-flight .*muted=50 .*gain=40'
check "stale reads do not overwrite the save and the audio change" 'SCN 8-after-the-stale-reads-finished.* muted=50 .*gain=40'
exit $fail
