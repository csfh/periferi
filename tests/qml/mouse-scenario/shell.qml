import QtQuick
import Quickshell
import Quickshell.Io
import "peri"

// Headless scenario for PeriMouse: the real core and its real processes against tests/qml/mouse-scenario/
// fake-peri (which never touches a device). A fixed sequence of gestures, a MARK line in the fake's call log at
// each stage (each in its own step, well before the action it labels), and SCN lines for the states; run.sh
// compares the exact order of calls. No window. Run with QT_QPA_PLATFORM=offscreen.
ShellRoot {
  PeriMouse {
    id: mouse
    periPath: Quickshell.env("FAKE_PERI")
    moduleId: "razer-deathadder-v3-pro"
  }

  function log(tag) {
    console.log("SCN " + tag + " shown_dpi=" + mouse.info.dpi.x + " shown_poll=" + mouse.info.poll
      + " known_dpi=" + mouse.known.dpi.x + " known_poll=" + mouse.known.poll
      + " pending=" + JSON.stringify(mouse.shown) + " error=" + JSON.stringify(mouse.error))
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
  Item { id: root }

  Component.onCompleted: {
    if (Quickshell.env("QT_QPA_PLATFORM") !== "offscreen") { console.log("refusing: run offscreen"); Qt.quit(); return }
    step(100,   function() { mark("load") })
    step(300,   function() { mouse.refresh("start") })
    step(1200,  function() { log("1-loaded") })

    // A DPI choice is drawn at once, the model keeps the old value, and a read asked for meanwhile is skipped.
    step(1400,  function() { mark("dpi") })
    step(1600,  function() { mouse.chooseDpi(1600); log("2-chosen-immediately"); mouse.refresh("while queued (must be skipped)") })
    step(3300,  function() { log("3-written (the mouse's own read-back is in the model)") })

    // A DPI choice and, 100 ms later, a poll-rate click: both written (the poll click is due first), none dropped.
    step(3500,  function() { mark("two") })
    step(3700,  function() { mouse.chooseDpi(800); mouse.choosePoll(500); log("4-two-chosen") })   // one step: only their delays decide the order
    step(6200,  function() { log("5-both-written") })

    // A poll rate the mouse refuses: the error shows, and the pane draws what is true again.
    step(6400,  function() { mark("refused") })
    step(6600,  function() { mouse.choosePoll(125) })
    step(7900,  function() { log("6-after-refusal") })

    // The same slider chosen twice quickly: one write, with the last value.
    step(8100,  function() { mark("replace") })
    step(8300,  function() { mouse.chooseDpi(1000); mouse.chooseDpi(1200) })                       // one step: no window to slip out of
    step(9700,  function() { log("7-after-replace") })

    // Back to the value the mouse already has: nothing is written. An explicit refresh then reads it.
    step(9900,  function() { mark("unchanged") })
    step(10100, function() { mouse.chooseDpi(1200); log("8-unchanged-chosen") })
    step(10900, function() { mouse.refresh("explicit") })
    step(11600, function() { log("9-after-explicit-refresh") })
    step(11800, function() { console.log("SCN done"); Qt.quit() })
  }
}
