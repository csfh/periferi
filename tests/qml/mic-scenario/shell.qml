import QtQuick
import Quickshell
import Quickshell.Io
import "peri"

// Headless scenario for PeriMic: the real core and its real processes against tests/qml/mic-scenario/fake-peri
// (which never touches a device). A fixed sequence of gestures, a MARK line in the fake's call log at each stage
// (each in its own step, well before the action it labels), and SCN lines for the states; run.sh compares the calls
// of each stage. No window. Run with QT_QPA_PLATFORM=offscreen.
ShellRoot {
  PeriMic {
    id: mic
    periPath: Quickshell.env("FAKE_PERI")
    moduleId: "mic"
  }

  function pct(v) { return Math.round(v * 100 / 255) }
  function log(tag) {
    console.log("SCN " + tag + " editing=" + mic.editing + " unmuted=" + pct(mic.savedLooks.unmuted.brightness)
      + " muted=" + pct(mic.savedLooks.muted.brightness) + " follow=" + mic.follow + " gain=" + mic.audio.gain
      + " error=" + JSON.stringify(mic.error))
  }
  function step(ms, fn) {
    var t = Qt.createQmlObject('import QtQuick; Timer { repeat: false }', root, "step")
    t.interval = ms
    t.triggered.connect(function() { fn(); t.destroy() })
    t.running = true
  }
  function mark(text) {
    var p = Qt.createQmlObject('import Quickshell.Io; Process { }', root, "mark")
    p.command = ["sh", "-c", "echo MARK " + text + " >> \"$FAKE_DIR/calls\""]
    p.running = true
  }
  function gates(on) {
    var p = Qt.createQmlObject('import Quickshell.Io; Process { }', root, "gate")
    p.command = ["sh", "-c", (on ? "touch" : "rm -f") + " \"$FAKE_DIR/gate.lookread\" \"$FAKE_DIR/gate.audioread\""]
    p.running = true
  }
  Item { id: root }

  Component.onCompleted: {
    if (Quickshell.env("QT_QPA_PLATFORM") !== "offscreen") { console.log("refusing: run offscreen"); Qt.quit(); return }
    step(100,   function() { mark("load") })
    step(300,   function() { mic.refresh("start") })
    step(1200,  function() { log("1-loaded") })

    // A slider choice is drawn at once and saved.
    step(1400,  function() { mark("brightness") })
    step(1600,  function() { mic.chooseBrightness(60); log("2-chosen-immediately") })
    step(2600,  function() { log("3-saved") })

    // Three saves on two looks in quick succession: a save for one look must never replace another's, so
    // all three are written, in order (the old single slot lost the Muted one).
    step(2800,  function() { mark("three-saves") })
    step(3000,  function() { mic.chooseBrightness(70) })
    step(3050,  function() { mic.selectLook("muted") })
    step(3100,  function() { mic.chooseBrightness(30) })
    step(3150,  function() { mic.selectLook("unmuted") })
    step(3200,  function() { mic.chooseBrightness(80); log("4-three-chosen") })
    step(5000,  function() { log("5-three-saved") })

    // A save the daemon refuses must not throw away the newer save queued behind it, and afterwards the pane
    // reads what is really saved.
    step(5200,  function() { mark("failure") })
    step(5400,  function() { mic.chooseBrightness(39) })     // 99/255: the fake refuses it
    step(5450,  function() { mic.selectLook("muted") })
    step(5500,  function() { mic.chooseBrightness(10) })
    step(7500,  function() { log("6-after-the-refused-save") })

    // Follow mute clicked twice while the first is still being saved: the second is not dropped.
    step(7700,  function() { mark("follow") })
    step(7900,  function() { mic.setFollow(false) })
    step(8000,  function() { mic.setFollow(true); log("7-follow-clicked-twice") })
    step(9200,  function() { log("8-follow-answered") })

    // Reads that began before a save and an audio change, and finish after them, must not overwrite them.
    step(9400,  function() { mark("races") })
    step(9600,  function() { gates(true) })
    step(9800,  function() { mic.refresh("held reads") })
    step(10100, function() { mic.chooseBrightness(50) })     // saves the Muted look (the one being edited)
    step(10200, function() { mic.chooseAudio("gain", 40) })
    step(11500, function() { log("9-changed-while-reads-in-flight"); gates(false) })
    step(12300, function() { log("10-after-the-stale-reads-finished (must still show the changes)") })
    step(12500, function() { console.log("SCN done"); Qt.quit() })
  }
}
