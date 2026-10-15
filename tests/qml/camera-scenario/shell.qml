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

  Component.onCompleted: {
    if (Quickshell.env("QT_QPA_PLATFORM") !== "offscreen") { console.log("refusing: run offscreen"); Qt.quit(); return }
    step(100,   function() { mark("load"); cam.refresh("start") })
    step(1000,  function() { log("1-loaded") })

    // A slider choice is drawn at once; the read asked for while it is queued must be skipped.
    step(1200,  function() { mark("slider"); cam.chooseValue("brightness", 160); log("2-chosen-immediately"); cam.refresh("while queued (must be skipped)") })
    step(2900,  function() { log("3-after-write-and-read") })

    // Two button choices, the second while the first write is running: neither dropped, in order.
    step(3000,  function() { mark("buttons"); cam.choose("white_balance_automatic", 0) })
    step(3150,  function() { cam.choose("auto_exposure", 1); log("4-second-choice-during-first-write") })
    step(5400,  function() { log("5-both-applied") })

    // Two DIFFERENT sliders chosen 100 ms apart: both must be written, in order (one queue entry per control).
    step(5500,  function() { mark("two-sliders"); cam.chooseValue("brightness", 150) })
    step(5600,  function() { cam.chooseValue("saturation", 200); log("6-two-chosen") })
    step(7700,  function() { log("7-both-sliders-applied") })

    // A write the camera refuses: the error stays visible, and the pane goes back to what is true.
    step(7800,  function() { mark("refused"); cam.chooseValue("contrast", 200) })
    step(10000, function() { log("8-after-refusal") })

    // A read that finishes while a slider is held must not replace the controls until it is let go.
    step(10100, function() { mark("held-read"); poke("s/^saturation=.*/saturation=77/") })
    step(10200, function() { cam.refresh("slow read") })
    step(10250, function() { cam.hold(true) })
    step(10700, function() { log("9-read-finished-while-held (model must not change)") })
    step(10800, function() { cam.hold(false) })
    step(10950, function() { log("10-after-release (model has the new value)") })

    // A write that finishes while a slider is held: the model stays, and no read happens, until the release.
    step(11000, function() { mark("held-write"); cam.chooseValue("brightness", 140) })
    step(11750, function() { cam.hold(true) })           // the write is in flight (it started at ~11600)
    step(12600, function() { log("11-held-after-write (model must not change)") })
    step(12700, function() { mark("released"); cam.hold(false) })
    step(13500, function() { log("12-after-release (read happened, model updated)") })

    // A hold left by a destroyed slider must not block reading for good.
    step(13600, function() { mark("stuck-hold"); cam.hold(true); cam.refresh("stuck (must be skipped)") })
    step(13800, function() { cam.resetHold(); cam.refresh("after the pane reopened") })
    step(14600, function() { console.log("SCN done"); Qt.quit() })
  }
}
