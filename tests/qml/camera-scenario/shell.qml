import QtQuick
import Quickshell
import Quickshell.Io
import "peri"

// Headless scenario for PeriCamera: the real core and its real processes against tests/qml/camera-scenario/
// fake-peri (which never touches a device). It performs a fixed sequence of gestures, writes a MARK line into
// the fake's call log at each stage, and prints SCN lines; run.sh compares the exact order of calls and the
// printed states. No window is created. Run with QT_QPA_PLATFORM=offscreen.
ShellRoot {
  PeriCamera {
    id: cam
    periPath: Quickshell.env("FAKE_PERI")
    moduleId: "brio"
  }

  function value(name) {
    for (var i = 0; i < cam.controls.length; i++) if (cam.controls[i].name === name) return cam.controls[i].value
    return null
  }
  function log(tag) {
    console.log("SCN " + tag + " model_brightness=" + value("brightness") + " model_contrast=" + value("contrast")
      + " model_sat=" + value("saturation") + " model_wb=" + value("white_balance_automatic") + " model_ae=" + value("auto_exposure")
      + " shown=" + JSON.stringify(cam.shown) + " error=" + JSON.stringify(cam.error))
  }
  function step(ms, fn) {
    var t = Qt.createQmlObject('import QtQuick; Timer { repeat: false }', root, "step")
    t.interval = ms
    t.triggered.connect(function() { fn(); t.destroy() })
    t.running = true
  }
  // A line in the fake's call log, so the runner can tell which stage a call belongs to.
  function mark(text) {
    var p = Qt.createQmlObject('import Quickshell.Io; Process { }', root, "mark")
    p.command = ["sh", "-c", "echo MARK " + text + " >> \"$FAKE_DIR/calls\""]
    p.running = true
  }
  // Change what the fake camera holds behind the pane's back (a value changed by something else).
  function poke(sedExpr) {
    var p = Qt.createQmlObject('import Quickshell.Io; Process { }', root, "poke")
    p.command = ["sh", "-c", "sed -i '" + sedExpr + "' \"$FAKE_DIR/state\""]
    p.running = true
  }
  Item { id: root }

  // Hold a kind of call ("read" or "write") in flight in the fake until open()ed: no timing race.
  function gate(name, on) {
    var p = Qt.createQmlObject('import Quickshell.Io; Process { }', root, "gate")
    p.command = ["sh", "-c", (on ? "touch" : "rm -f") + " \"$FAKE_DIR/gate." + name + "\""]
    p.running = true
  }

  // Every MARK has its own step, at least 100 ms before the action it labels, so its line in the call log can
  // never race the fake's own line for that action.
  Component.onCompleted: {
    if (Quickshell.env("QT_QPA_PLATFORM") !== "offscreen") { console.log("refusing: run offscreen"); Qt.quit(); return }
    step(100,   function() { mark("load") })
    step(300,   function() { cam.refresh("start") })
    step(1200,  function() { log("1-loaded") })

    // A slider choice is drawn at once; the read asked for while it is queued must be skipped.
    step(1400,  function() { mark("slider") })
    step(1600,  function() { cam.chooseValue("brightness", 160); log("2-chosen-immediately"); cam.refresh("while queued (must be skipped)") })
    step(3600,  function() { log("3-after-write-and-read") })

    // Two button choices, the second while the first write is running: neither dropped, in order.
    step(3800,  function() { mark("buttons") })
    step(4000,  function() { cam.choose("white_balance_automatic", 0) })
    step(4150,  function() { cam.choose("auto_exposure", 1); log("4-second-choice-during-first-write") })
    step(5800,  function() { log("5-both-applied") })

    // Two DIFFERENT sliders chosen 100 ms apart: both must be written, in order (one queue entry per control).
    step(6000,  function() { mark("two-sliders") })
    step(6200,  function() { cam.chooseValue("brightness", 150); cam.chooseValue("saturation", 200); log("6-two-chosen") })
    step(8500,  function() { log("7-both-sliders-applied") })

    // A write the camera refuses: the error stays visible, and the pane goes back to what is true.
    step(8700,  function() { mark("refused") })
    step(8900,  function() { cam.chooseValue("contrast", 200) })
    step(10900, function() { log("8-after-refusal") })

    // A read that finishes while a slider is held must not replace the controls until it is let go. The fake
    // holds the read in flight (gate) until the slider is held: no timing window.
    step(11100, function() { mark("held-read") })
    step(11300, function() { poke("s/^saturation=.*/saturation=77/"); gate("read", true) })
    step(11500, function() { cam.refresh("slow read") })
    step(11800, function() { cam.hold(true) })
    step(12000, function() { gate("read", false) })               // the read now finishes, with the slider held
    step(12500, function() { log("9-read-finished-while-held (model must not change)") })
    step(12600, function() { cam.hold(false) })
    step(12800, function() { log("10-after-release (model has the new value)") })

    // A write that finishes while a slider is held: the model stays, and no read happens, until the release.
    step(13000, function() { mark("held-write") })
    step(13200, function() { gate("write", true) })
    step(13300, function() { cam.chooseValue("brightness", 140) })
    step(15000, function() { cam.hold(true) })                    // the write has been blocked in flight since ~13900
    step(15200, function() { gate("write", false) })              // it finishes now, with the slider held
    step(16300, function() { log("11-held-after-write (model must not change)") })
    step(16500, function() { mark("released") })
    step(16700, function() { cam.hold(false) })
    step(17700, function() { log("12-after-release (read happened, model updated)") })

    // A hold left by a destroyed slider must not block reading for good.
    step(17900, function() { mark("stuck-hold") })
    step(18100, function() { cam.hold(true); cam.refresh("stuck (must be skipped)") })
    step(18300, function() { cam.resetHold(); cam.refresh("after the pane reopened") })
    step(19200, function() { console.log("SCN done"); Qt.quit() })
  }
}
