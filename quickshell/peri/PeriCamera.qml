import QtQuick
import Quickshell
import Quickshell.Io
import "camera.js" as C
import "cli.js" as Cli

// A webcam (the Logitech BRIO) as seen by the peri daemon. Like PeriMouse it imports nothing from the
// host shell and draws nothing; hosts bind to the properties below.
//
//   PeriCamera { id: cam; moduleId: "brio" }
//   MySlider { onReleased: cam.chooseValue("brightness", v) }
//
// Reads happen when refresh() is called, which a host does when it opens the pane, not on a timer. A
// control's inactive state depends on the others (exposure time is held while exposure is automatic), so
// every write is followed by one more read. Writes: the camera may keep a setting, a slider's released
// signal fires once per wheel notch, so chooseValue() waits for a pause; an unchanged value is never sent,
// and only one write runs at a time.
Scope {
  id: root

  property string periPath: Quickshell.env("HOME") + "/.local/bin/peri"
  property string moduleId: ""

  // On since every kind of control was tried by hand on the real camera (2026-10-15): an integer
  // (brightness, zoom), the stepped pan and tilt, a switch (auto white balance), a menu (exposure mode)
  // and the field of view. Set it false and nothing is sent: the pane shows its controls as unavailable.
  property bool writable: true

  readonly property bool online: state.s.online
  readonly property var controls: state.s.controls
  readonly property var rows: C.rows(state.s.controls)
  readonly property var columns: C.columns(C.rows(state.s.controls))
  readonly property bool busy: reader.running || writer.running
  readonly property string error: state.error
  // What the user just chose and the camera has not been read back for yet (control name -> value). The view
  // draws this instead of the camera's value (camera.js shownValue), so a knob never jumps back and forth.
  readonly property var shown: state.shown

  // The camera is only ever read when no choice is waiting to be written, no read or write is running, and
  // no slider is being dragged (see camera.js mayRead): a read in any of those moments would draw the old
  // value over a choice, or rebuild the controls under the user's hand.
  function refresh(why) {
    if (!read()) return
    state.error = "" // an explicit refresh (the pane opening, the Refresh button) starts clean
  }

  // Start a read if one is allowed now; the automatic read after a write uses this directly, so it never
  // wipes the error a refused write has just left.
  function read() {
    if (root.moduleId === "" || !C.mayRead(state.queue.length, reader.running || writer.running, state.holding > 0)) return false
    reader.running = true
    return true
  }

  // The view says a slider is being held (true) or let go (false). A new list of controls that arrived while
  // it was held is applied when the last one is let go.
  function hold(on) {
    state.holding = Math.max(0, state.holding + (on ? 1 : -1))
    if (state.holding === 0) applyDeferred()
  }

  // A backstop: when the pane opens, forget any hold a destroyed slider left behind.
  function resetHold() {
    state.holding = 0
    applyDeferred()
  }

  function applyDeferred() {
    if (state.deferred === null) return
    var m = state.deferred
    state.deferred = null
    apply(m)
  }

  // Take a freshly read list of controls as the truth: what the camera says replaces what the user chose,
  // unless something else is already on its way.
  function apply(model) {
    state.s = model
    state.shown = C.settle(state.shown, state.queue.length, writer.running || state.holding > 0)
  }

  // A slider's value: validated against what the camera listed, shown at once, then written after 600 ms of
  // quiet (only the last choice for that control counts).
  function chooseValue(name, v) {
    var c = C.byName(state.s.controls, name)
    if (!root.writable || !c) return
    queueChoice(c, C.snap(c, v), 600)
  }

  // A switch or a menu button: shown at once and written after a moment.
  function choose(name, v) {
    var c = C.byName(state.s.controls, name)
    if (!root.writable || !c) return
    queueChoice(c, v, 40)
  }

  // Remember a choice and schedule its write. The choice is drawn at once. Every control keeps its own entry,
  // so a choice for one control never replaces another's, and the writes go one at a time: nothing is dropped
  // when something is running, the timer tries again.
  function queueChoice(c, v, delay) {
    if (C.setArg(c, v) === "" && !C.isCurrent(c, v)) return // not a value this control takes right now
    state.shown = C.choose(state.shown, c.name, v)
    if (C.isCurrent(c, v)) { // back where the camera has it: nothing to write for this control
      state.queue = C.drop(state.queue, c.name)
      if (state.queue.length === 0) pause.stop()
      else schedule()
      return
    }
    state.queue = C.enqueue(state.queue, c.name, v, Date.now() + delay)
    schedule()
  }

  // Wake up when the earliest queued choice is due.
  function schedule() {
    pause.interval = C.wait(state.queue, Date.now())
    if (pause.interval > 0) pause.restart()
  }

  QtObject {
    id: state
    property var s: C.parseControls("")
    property string error: ""
    property var shown: ({})
    property var queue: []      // [{ name, value, due }]: choices waiting to be written, one per control
    property int holding: 0     // sliders currently being dragged
    property var deferred: null // a list of controls that arrived while a slider was held
  }

  Timer {
    id: pause
    onTriggered: {
      if (reader.running || writer.running) { // busy: try again, never drop a choice
        pause.interval = 50
        pause.restart()
        return
      }
      var hit = C.dequeue(state.queue, Date.now())
      if (hit === null) { root.schedule(); return } // nothing due yet
      state.queue = hit.rest
      var q = hit.entry
      var c = C.byName(state.s.controls, q.name)
      var arg = c ? C.setArg(c, q.value) : ""
      if (arg === "" || !root.writable || root.moduleId === "") {
        // The control can no longer take it (an automatic mode took it over, say): say so, and read the
        // camera so the pane shows what is true.
        state.error = (c ? c.title : q.name) + " cannot be changed right now"
        if (state.queue.length > 0) root.schedule(); else after.restart()
        return
      }
      state.error = ""
      writer.command = Cli.command(root.periPath, ["set", root.moduleId, arg])
      writer.running = true
    }
  }

  Process {
    id: reader
    command: Cli.command(root.periPath, ["controls", root.moduleId])
    stdout: StdioCollector {
      onStreamFinished: {
        var model = C.parseControls(this.text)
        // Never replace the controls under a slider that is being held: wait for the release.
        if (state.holding > 0) state.deferred = model
        else root.apply(model)
      }
    }
    stderr: StdioCollector {
      onStreamFinished: { if (this.text.trim() !== "") state.error = this.text.trim() }
    }
    onExited: function(exitCode) {
      if (exitCode !== 0) {
        var offline = C.parseControls("")
        if (state.holding > 0) state.deferred = offline
        else root.apply(offline)
      }
    }
  }

  // One more read after the last write (success or failure), because a control's inactive state depends on
  // the others and because a failed write must show what is really true. It waits until nothing is running
  // and no slider is held, so read()'s own guard never skips it.
  Timer {
    id: after
    interval: 250
    onTriggered: {
      if (reader.running || writer.running || state.holding > 0) { after.restart(); return }
      if (state.queue.length > 0) return // more choices are waiting: the last write schedules the read
      root.read()
    }
  }

  Process {
    id: writer
    stdout: StdioCollector {
      onStreamFinished: {
        var r = C.parseSet(this.text)
        if (!r.ok || r.value === null) return
        // Draw the camera's own value for that control. The model is NOT replaced here: a full read always
        // follows the last write, and replacing it now could rebuild the controls under a held slider.
        state.shown = C.choose(state.shown, r.name, r.value)
      }
    }
    stderr: StdioCollector {
      onStreamFinished: { if (this.text.trim() !== "") state.error = this.text.trim() }
    }
    onExited: {
      // The next queued choice, else the read that shows what is true.
      if (state.queue.length > 0) root.schedule()
      else after.restart()
    }
  }
}
