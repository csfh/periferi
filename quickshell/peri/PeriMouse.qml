import QtQuick
import Quickshell
import Quickshell.Io
import "mouse.js" as M
import "cli.js" as Cli
import "choices.js" as Ch

// A mouse (the Razer DeathAdder V3 Pro) as seen by the peri daemon. Like PeriMic it imports nothing
// from the host shell and draws nothing; hosts bind to the properties below.
//
//   PeriMouse { id: mouse; moduleId: "razer-deathadder-v3-pro" }
//   MyButton { onClicked: mouse.chooseDpi(1600) }
//
// Reads happen when refresh() is called, which a host does when it opens the pane, not on a timer
// (a sleeping mouse takes seconds to answer each read). Writes: a DPI or poll-rate set may be kept
// in the mouse's own memory, so chooseDpi() waits for a pause (a slider's released signal fires once
// per wheel notch) and an unchanged value is never sent. What the user chose is drawn at once over what
// the mouse last said (`info`), because PanelSlider snaps its knob back to the model's value on release.
// Each setting has its own queue entry, one write runs at a time and none is dropped, and the mouse is
// never read while a choice is queued or something is running (see choices.js).
Scope {
  id: root

  property string periPath: Quickshell.env("HOME") + "/.local/bin/peri"
  property string moduleId: ""

  // Both sets were seen working on the real mouse (2026-10-14, read back from the device). Set
  // this false and no choose*() call sends anything: the pane shows its controls as unavailable.
  property bool writable: true

  readonly property bool online: state.s.online
  // What the pane draws: the status with what the user just chose laid over it (mouse.js overlay), so a
  // slider or a button never lags behind the click or snaps back when the mouse is released.
  readonly property var info: M.overlay(state.s, state.shown)
  readonly property var known: state.s       // what the mouse last said (tests and debugging)
  readonly property var shown: state.shown   // what the user chose and the mouse has not confirmed yet
  readonly property bool busy: reader.running || writer.running || state.queue.length > 0
  readonly property string error: state.error

  // The device is only read when no choice is waiting to be written and no read or write is running
  // (see choices.js mayRead): a read in either moment would draw the old value over a choice.
  function refresh(why) {
    if (root.moduleId === "" || !Ch.mayRead(state.queue.length, reader.running || writer.running, false)) return
    state.error = ""
    reader.running = true
  }

  // A DPI chosen on the slider or a preset button: shown at once, written after 600 ms of quiet. The DPI
  // set may be kept in the mouse's own memory, so nothing else is allowed to repeat it.
  function chooseDpi(v) {
    if (!root.writable) return
    queueChoice("dpi", M.clampDpi(v), 600)
  }

  // A poll-rate button: shown at once, written after a moment, never dropped when something is running.
  function choosePoll(rate) {
    if (!root.writable || M.pollArg(rate) === "") return
    queueChoice("poll", rate, 300)
  }

  function sameAsDevice(key, v) {
    var s = state.s
    if (!s.online) return false
    return key === "dpi" ? s.dpi !== null && s.dpi.x === v && s.dpi.y === v : s.poll === v
  }

  // Remember a choice and schedule its write. One entry per setting, so a poll-rate click never replaces a
  // DPI choice, and nothing is dropped while a read or write runs: the timer tries again.
  function queueChoice(key, v, delay) {
    if (sameAsDevice(key, v)) { // back where the device has it: nothing to write, nothing to draw over it
      state.shown = Ch.omit(state.shown, key)
      state.queue = Ch.drop(state.queue, key)
      if (state.queue.length === 0) pause.stop()
      else schedule()
      return
    }
    state.shown = Ch.choose(state.shown, key, v)
    state.queue = Ch.enqueue(state.queue, key, v, Date.now() + delay)
    schedule()
  }

  // Wake up when the earliest queued choice is due.
  function schedule() {
    pause.interval = Ch.wait(state.queue, Date.now())
    if (pause.interval > 0) pause.restart()
  }

  QtObject {
    id: state
    property var s: M.parseStatus("")
    property string error: ""
    property var shown: ({})     // "dpi" / "poll" -> what the user just chose, until the device is read again
    property var queue: []       // [{ name, value, due }]: choices waiting to be written, one per setting
    property string inflight: "" // the setting a running write is for
    property int inflightValue: 0
  }

  Timer {
    id: pause
    onTriggered: {
      if (reader.running || writer.running) { // busy: try again, never drop a choice
        pause.interval = 50
        pause.restart()
        return
      }
      var hit = Ch.dequeue(state.queue, Date.now())
      if (hit === null) { root.schedule(); return } // nothing due yet
      state.queue = hit.rest
      var q = hit.entry
      var arg = q.name === "dpi" ? M.dpiArg(q.value) : M.pollArg(q.value)
      if (arg === "" || !root.writable || root.moduleId === "") {
        state.shown = Ch.omit(state.shown, q.name)
        if (state.queue.length > 0) root.schedule()
        return
      }
      state.error = ""
      state.inflight = q.name
      state.inflightValue = q.value
      writer.command = Cli.command(root.periPath, ["set", root.moduleId, arg])
      writer.running = true
    }
  }

  Process {
    id: reader
    command: Cli.command(root.periPath, ["status", root.moduleId])
    stdout: StdioCollector {
      onStreamFinished: {
        state.s = M.parseStatus(this.text)
        // The device has been read again: what it says replaces what the user chose, unless more is on its way.
        state.shown = Ch.settle(state.shown, state.queue.length, writer.running)
      }
    }
    stderr: StdioCollector {
      onStreamFinished: { if (this.text.trim() !== "") state.error = this.text.trim() }
    }
    onExited: function(exitCode) {
      if (exitCode !== 0) {
        state.s = M.parseStatus("")
        state.shown = Ch.settle(state.shown, state.queue.length, writer.running)
      }
    }
  }

  Process {
    id: writer
    stdout: StdioCollector {
      onStreamFinished: {
        var r = M.parseSet(this.text)
        if (!r.ok) return
        // The device's own read-back goes into the status and the user's choice is no longer needed, unless
        // a newer choice for the same setting is waiting (then it stays drawn).
        var key = state.inflight
        state.s = M.patch(state.s, key, state.inflightValue, key === "dpi" ? r.dpi : r.poll)
        if (Ch.drop(state.queue, key).length === state.queue.length) state.shown = Ch.omit(state.shown, key)
      }
    }
    stderr: StdioCollector {
      onStreamFinished: { if (this.text.trim() !== "") state.error = this.text.trim() }
    }
    onExited: function(exitCode) {
      if (exitCode !== 0 && Ch.drop(state.queue, state.inflight).length === state.queue.length)
        state.shown = Ch.omit(state.shown, state.inflight) // a failed write: draw what is true again
      state.inflight = ""
      if (state.queue.length > 0) root.schedule()
    }
  }
}
