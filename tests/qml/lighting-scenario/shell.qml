import QtQuick
import Quickshell
import Quickshell.Io
import "peri"

// Headless scenario for PeriLighting: the real core and its real processes against tests/qml/lighting-scenario/
// fake-peri (which never touches a device). A fixed sequence of gestures, a MARK line in the fake's call log at
// each stage (each in its own step, well before the action it labels), and SCN lines for the states; run.sh
// compares the exact order of calls. No window. Run with QT_QPA_PLATFORM=offscreen.
ShellRoot {
  PeriLighting {
    id: kb
    periPath: Quickshell.env("FAKE_PERI")
    interval: 0 // no polling: the scenario asks for every read itself
  }

  function log(tag) {
    console.log("SCN " + tag + " brightness=" + kb.brightness + " hue=" + kb.hue + " flags=" + kb.flags
      + " lit=" + kb.lit + " effect=" + kb.effect + " shown=" + JSON.stringify(kb.shown) + " error=" + JSON.stringify(kb.error))
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
  function gate(on) {
    var p = Qt.createQmlObject('import Quickshell.Io; Process { }', root, "gate")
    p.command = ["sh", "-c", (on ? "touch" : "rm -f") + " \"$FAKE_DIR/gate.read\""]
    p.running = true
  }
  Item { id: root }

  Component.onCompleted: {
    if (Quickshell.env("QT_QPA_PLATFORM") !== "offscreen") { console.log("refusing: run offscreen"); Qt.quit(); return }
    step(100,   function() { mark("load") })
    step(300,   function() { kb.refresh() })
    step(1200,  function() { log("1-loaded") })

    // A slider choice is drawn at once; the read asked for while it is in flight is skipped.
    step(1400,  function() { mark("slider") })
    step(1600,  function() { kb.setBrightness(80); log("2-chosen-immediately"); kb.refresh() })
    step(2600,  function() { log("3-answered") })

    // A dragged slider: the first value goes at once, the rest merge, and it ends on the last value.
    step(2800,  function() { mark("coalesce") })
    step(3000,  function() { kb.setBrightness(10) })
    step(3100,  function() { kb.setBrightness(20) })
    step(3150,  function() { kb.setBrightness(30); log("4-dragged") })
    step(4400,  function() { log("5-dragged-answered") })

    // Two quick clicks on different LED groups: both stick (the second builds on the first, not on stale flags).
    step(4600,  function() { mark("groups") })
    step(4800,  function() { kb.setGroup(1, false) })
    step(4900,  function() { kb.setGroup(2, false); log("6-two-groups-chosen") })
    step(6200,  function() { log("7-groups-answered") })

    // A read that began before a change and finishes after it is stale and must not overwrite it.
    step(6400,  function() { mark("race") })
    step(6600,  function() { gate(true) })
    step(6800,  function() { kb.refresh() })                 // the read starts and is held in flight by the fake
    step(7000,  function() { kb.setBrightness(70) })         // a change goes while the read is still in flight
    step(8000,  function() { log("8-changed-while-read-in-flight"); gate(false) })
    step(8700,  function() { log("9-after-the-stale-read-finished (must still show the change)") })

    // A change the keyboard refuses: the error shows, the display goes back to what is true, and it is read again.
    step(8900,  function() { mark("refused") })
    step(9100,  function() { kb.setEffect("bad"); log("10-refused-chosen") })
    step(10300, function() { log("11-after-refusal") })

    // The on/off toggle is drawn at once.
    step(10500, function() { mark("toggle") })
    step(10700, function() { kb.set(["enable=0"]); log("12-toggled") })
    step(11700, function() { log("13-toggle-answered"); console.log("SCN done"); Qt.quit() })
  }
}
