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
      + " muted=" + pct(mic.savedLooks.muted.brightness) + " gain=" + mic.audio.gain
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
  function gate(on, name) {
    var p = Qt.createQmlObject('import Quickshell.Io; Process { }', root, "gate")
    p.command = ["sh", "-c", (on ? "touch" : "rm -f") + " \"$FAKE_DIR/gate." + name + "\""]
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
    // The fake holds the refused save in flight (gate) until the newer one has been chosen: no timing window.
    step(5200,  function() { mark("failure") })
    step(5300,  function() { gate(true, "write") })
    step(5500,  function() { mic.chooseBrightness(39) })     // 99/255: the fake refuses it
    step(5550,  function() { mic.selectLook("muted") })
    step(5600,  function() { mic.chooseBrightness(10) })
    step(5900,  function() { gate(false, "write") })
    step(7600,  function() { log("6-after-the-refused-save") })

    // Reads that began before a save and an audio change, and finish after them, must not overwrite them.
    step(7800,  function() { mark("races") })
    step(8000,  function() { gates(true) })
    step(8200,  function() { mic.refresh("held reads") })
    step(8500,  function() { mic.chooseBrightness(50) })     // saves the Muted look (the one being edited)
    step(8600,  function() { mic.chooseAudio("gain", 40) })
    step(9900,  function() { log("7-changed-while-reads-in-flight"); gates(false) })
    step(10700, function() { log("8-after-the-stale-reads-finished (must still show the changes)") })
    step(10900, function() { console.log("SCN done"); Qt.quit() })
  }
}
