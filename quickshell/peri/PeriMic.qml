import QtQuick
import Quickshell
import Quickshell.Io
import "mic.js" as M
import "lighting.js" as L
import "cli.js" as Cli
import "debug.js" as D

// A microphone (the Razer Seiren V3 Chroma) as seen by the peri daemon. Like PeriLighting it
// imports nothing from the host shell and draws nothing; hosts bind to the properties below.
//
//   PeriMic { id: mic; moduleId: "razer-seiren-v3-chroma" }
//   MyButton { onClicked: mic.chooseEffect("spectrum") }
//   MyApply { onClicked: mic.apply() }
//
// The mic cannot be asked for its effect or colour, so the daemon keeps two saved LOOKS for it,
// Unmuted and Muted, each an effect, a colour and a brightness. This scope edits one of them at a
// time (`editing`). chooseEffect() and chooseBrightness() only SAVE the look being edited
// (`peri look ... save`, a file, never the mic); apply() is the only call that reaches the mic
// (`peri look ... apply`). A write to the mic may be kept in its own memory, so nothing but a
// deliberate apply() sends one. Reads happen when refresh() is called, which a host does when it
// opens the pane, not on a timer.
//
// Debug mode: setDebug(true) starts an annotated log of everything the pane does (what the user
// pressed, what was decided, every peri command and what it printed) in `debugPath`; see debug.js.
Scope {
  id: root

  property string periPath: Quickshell.env("HOME") + "/.local/bin/peri"
  property string moduleId: ""            // the installed module that drives this mic
  property string editing: "unmuted"      // the look the controls edit; a pane opens on Unmuted

  // The debug log: off until setDebug(true). Lines are appended to debugPath, annotated by debug.js.
  readonly property bool debug: state.debug
  readonly property string debugPath: {
    var base = Quickshell.env("XDG_STATE_HOME")
    return (base ? base : Quickshell.env("HOME") + "/.local/state") + "/peri/pane-debug.txt"
  }

  readonly property bool online: state.online
  readonly property var look: state.looks[root.editing]   // { effect, color, brightness } being edited
  readonly property int deviceBrightness: state.deviceBrightness // the mic's own, 0-255
  // Apply waits for any save to reach the daemon, so it can never send an older look than the one shown.
  readonly property bool canApply: state.online && M.canApply(root.look) && !applier.running
                                   && !saver.running && state.pending === null
  readonly property bool follow: state.follow   // the daemon applies the matching look when the mic is (un)muted
  readonly property bool busy: applier.running
  readonly property string notice: state.notice  // what the last apply sent, or ""
  readonly property string error: state.error !== "" ? state.error : state.audioError
  // The sound-card controls (`peri audio`): { gain, monitor, monitorOn, headphones }, -1 or null
  // when unknown. Live levels, applied as soon as chooseAudio() is called.
  readonly property var audio: state.audio

  // --- the debug log ---------------------------------------------------------------------------
  // Lines wait in a queue until the log file's writer is running, so none is lost at the start.
  function dbg(tag, text) {
    if (!state.debug) return
    state.queue.push(D.line(new Date(), tag, text) + "\n")
    flush()
  }

  function flush() {
    while (sink.running && state.queue.length > 0) sink.write(state.queue.shift())
  }

  function setDebug(on) {
    if (on === state.debug) return
    if (!on) {
      dbg("OPEN", "debug log switched off")
      flush()
      state.debug = false
      stopSink.restart() // let the writer finish the last line before it is closed
      return
    }
    stopSink.stop()
    state.debug = true
    sink.running = true
    state.queue.push(D.header({ when: new Date(), path: root.debugPath, module: root.moduleId,
                                periPath: root.periPath, editing: root.editing, online: state.online,
                                follow: state.follow, looks: state.looks, audio: state.audio }) + "\n")
    dbg("OPEN", "debug log switched on; reading the daemon's recent journal lines about the mic, then refreshing")
    journal.running = true
    flush()
    root.refresh("the debug log was switched on")
  }

  function traceRun(name, proc) {
    if (!state.debug || !proc.running) return
    var args = D.periArgs(proc.command)
    state.started[name] = Date.now()
    dbg("RUN", "peri " + args.join(" "))
    dbg("RUN", "   = " + D.classify(args).note)
  }

  function traceExit(name, exitCode) {
    if (!state.debug) return
    var t0 = state.started[name]
    dbg("EXIT", name + " exited " + exitCode + (exitCode === 0 ? " (ok)" : " (failed)")
        + (t0 ? " after " + (Date.now() - t0) + " ms" : ""))
  }

  function traceText(tag, name, text) {
    if (!state.debug) return
    var t = D.oneLine(text, 400)
    if (t !== "") dbg(tag, name + ": " + t)
  }

  // Read the mic's online state and brightness, and both saved looks. Skipped while an apply is
  // running. Nothing here writes to the mic.
  function refresh(why) {
    if (root.moduleId === "" || applier.running) {
      dbg("REFRESH", "skipped: " + (root.moduleId === "" ? "no module is selected" : "an Apply is running"))
      return
    }
    dbg("REFRESH", "reading the mic's state, both saved looks and the audio levels (" + (why || "requested") + ")")
    state.notice = ""
    if (!status.running) status.running = true
    if (!getUnmuted.running) getUnmuted.running = true
    if (!getMuted.running) getMuted.running = true
    if (!audioGet.running) audioGet.running = true
  }

  function selectLook(which) {
    dbg("GESTURE", "switched to the " + which + " look (the controls now edit it; nothing is sent)")
    if (which === "unmuted" || which === "muted") root.editing = which
  }

  // An effect button: saves the look being edited with that effect, keeping the colours the effect
  // can use. File only.
  function chooseEffect(id) {
    dbg("GESTURE", "effect button \"" + id + "\" on the " + root.editing + " look, which held: " + D.describeLook(root.look))
    var next = M.withEffect(root.look, id)
    if (next !== null) saveLook(root.editing, next)
    else dbg("IGNORED", "\"" + id + "\" is not an effect this pane knows")
  }

  // A swatch: slot 1 is the first colour (it switches to "Colour" if the effect takes none), slot 2
  // the second colour of breathing. File only.
  function chooseColor(slot, hex) {
    dbg("GESTURE", "colour swatch " + hex + " in slot " + slot + " on the " + root.editing + " look, which held: " + D.describeLook(root.look))
    var next = M.withColor(root.look, slot, hex)
    if (next !== null) saveLook(root.editing, next)
    else dbg("IGNORED", "that colour does not apply here (not a valid colour, or the effect takes no second colour)")
  }

  // Slow or Fast for a wave. File only.
  function chooseSpeed(speed) {
    dbg("GESTURE", "speed button \"" + speed + "\" on the " + root.editing + " look, which held: " + D.describeLook(root.look))
    var next = M.withSpeed(root.look, speed)
    if (next !== null) saveLook(root.editing, next)
    else dbg("IGNORED", "only the two wave effects have a speed")
  }

  // "Random colours" for breathing. File only.
  function chooseRandomColors() {
    dbg("GESTURE", "Random colours on the " + root.editing + " look, which held: " + D.describeLook(root.look))
    var next = M.withRandomColors(root.look)
    if (next !== null) saveLook(root.editing, next)
    else dbg("IGNORED", "only breathing can have random colours")
  }

  // Call from a slider's `released`: saves the look being edited with that brightness. File only.
  function chooseBrightness(percent) {
    dbg("GESTURE", "brightness slider released at " + percent + "% on the " + root.editing + " look, which held: " + D.describeLook(root.look))
    var next = M.withBrightness(root.look, percent)
    if (next !== null) saveLook(root.editing, next)
    else dbg("IGNORED", "that is the brightness already saved, so nothing is saved")
  }

  // A released audio slider or the monitor button: key is "gain", "monitor", "headphones" or
  // "monitor_on" (a boolean). These are live sound-card levels, not looks, so they apply at
  // once; a value that would change nothing is not sent, and several quick ones go out together.
  function chooseAudio(key, value) {
    dbg("GESTURE", "audio control " + key + " released at " + value + "; the sound card held: " + D.describeAudio(state.audio))
    var setting = M.audioSetting(key, value, state.audio)
    if (setting === null || root.moduleId === "") {
      dbg("IGNORED", "that would not change anything (or is not a valid value), so nothing is sent")
      return
    }
    dbg("AUDIO", "will set " + setting + " (a live sound-card level, not stored by peri)")
    state.audio = M.withAudio(state.audio, key, value) // shown at once
    state.audioError = ""
    var pending = state.pendingAudio
    pending[key] = setting
    state.pendingAudio = pending
    if (!audioSetter.running) sendAudio()
  }

  function sendAudio() {
    var pending = state.pendingAudio
    var keys = Object.keys(pending)
    if (keys.length === 0) return
    state.pendingAudio = {}
    audioSetter.command = Cli.command(root.periPath, ["audio", root.moduleId, "set"]
      .concat(keys.map(function(k) { return pending[k] })))
    audioSetter.running = true
  }

  // Turn following the mute state on or off (the daemon keeps it with the looks; a file, not the mic).
  function setFollow(on) {
    dbg("GESTURE", "Follow mute button pressed: turning it " + (on ? "on" : "off"))
    if (root.moduleId === "" || follower.running) {
      dbg("IGNORED", "no module is selected, or the previous change is still being saved")
      return
    }
    dbg("FOLLOW", "following the mute state " + (on ? "on: the daemon applies the matching look after each mute change" : "off: only Apply sends a look"))
    state.follow = on // shown at once; the daemon's answer confirms it
    follower.command = Cli.command(root.periPath, ["look", root.moduleId, "follow", on ? "on" : "off"])
    follower.running = true
  }

  // Send the look being edited to the mic. The only call here that does.
  function apply() {
    dbg("GESTURE", "Apply pressed on the " + root.editing + " look, which held: " + D.describeLook(root.look))
    if (!root.canApply || root.moduleId === "") {
      dbg("IGNORED", "Apply did nothing because " + (!state.online ? "the microphone is offline"
          : !M.canApply(root.look) ? "this look has nothing saved to send"
          : applier.running ? "an Apply is already running"
          : "a save has not reached the daemon yet (it would send an older look)"))
      return
    }
    dbg("APPLY", "sending the " + root.editing + " look to the microphone: " + D.describeLook(root.look))
    state.error = ""
    state.notice = ""
    state.errText = ""
    applier.which = root.editing
    applier.command = Cli.command(root.periPath, ["look", root.moduleId].concat(M.applyArgs(root.editing)))
    applier.running = true
  }

  function saveLook(which, look) {
    if (root.moduleId === "") return
    dbg("SAVE", "the " + which + " look is now: " + D.describeLook(look) + " (a file only; the microphone is not touched)")
    var looks = { unmuted: state.looks.unmuted, muted: state.looks.muted }
    looks[which] = look
    state.looks = looks // shown at once; the daemon's answer confirms it or puts it back
    state.error = ""
    state.pending = { which: which, look: look }
    if (!saver.running) sendPending()
  }

  function sendPending() {
    var p = state.pending
    if (p === null) return
    dbg("SAVE", "sending the " + p.which + " look to the daemon to be saved")
    state.pending = null
    saver.which = p.which
    saver.command = Cli.command(root.periPath, ["look", root.moduleId].concat(M.saveArgs(p.which, p.look)))
    saver.running = true
  }

  QtObject {
    id: state
    property bool online: false
    property int deviceBrightness: 0
    property var looks: ({ unmuted: M.emptyLook(), muted: M.emptyLook() })
    property var pending: null      // the newest save waiting for the one in flight: last one wins
    property bool follow: true
    property bool debug: false
    property var queue: []              // debug lines waiting for the log writer
    property var started: ({})          // process name -> when it started, for the debug log
    property var audio: M.emptyAudio()
    property var pendingAudio: ({})   // key -> "key=value", waiting for the one in flight
    property string audioError: ""
    property string notice: ""
    property string error: ""
    property string errText: ""
  }

  function setLook(which, look) {
    var looks = { unmuted: state.looks.unmuted, muted: state.looks.muted }
    looks[which] = look
    state.looks = looks
  }

  Process {
    id: status
    command: Cli.command(root.periPath, ["status", root.moduleId])
    stdout: StdioCollector {
      onStreamFinished: {
        var s = M.parseStatus(this.text)
        root.traceText("OUT", "status", this.text)
        root.dbg("STATE", "microphone online=" + s.online + (s.online ? ", its own brightness " + s.brightness + "/255" : ""))
        state.online = s.online
        if (s.online) state.deviceBrightness = s.brightness
      }
    }
    onRunningChanged: root.traceRun("status", status)
    onExited: function(exitCode) {
      root.traceExit("status", exitCode)
      if (exitCode !== 0) state.online = false
    }
  }

  // The saved looks. A failed read leaves what is shown as it is, and a save in flight wins.
  Process {
    id: getUnmuted
    command: Cli.command(root.periPath, ["look", root.moduleId, "get", "unmuted"])
    stdout: StdioCollector {
      onStreamFinished: {
        var look = M.parseLook(this.text)
        var follow = M.parseFollow(this.text)
        root.traceText("OUT", "look get unmuted", this.text)
        if (follow !== null && !follower.running) state.follow = follow
        if (look !== null && !saver.running && state.pending === null) {
          root.dbg("STATE", "unmuted look read from the daemon: " + D.describeLook(look) + ", follow mute " + (follow ? "on" : "off"))
          root.setLook("unmuted", look)
        } else {
          root.dbg("IGNORED", "the unmuted look read from the daemon was not used: " + (look === null ? "it was not a look" : "a save is in flight, and what was saved last wins"))
        }
      }
    }
    onRunningChanged: root.traceRun("getUnmuted", getUnmuted)
    onExited: function(exitCode) { root.traceExit("getUnmuted", exitCode) }
  }
  Process {
    id: getMuted
    command: Cli.command(root.periPath, ["look", root.moduleId, "get", "muted"])
    stdout: StdioCollector {
      onStreamFinished: {
        var look = M.parseLook(this.text)
        root.traceText("OUT", "look get muted", this.text)
        if (look !== null && !saver.running && state.pending === null) {
          root.dbg("STATE", "muted look read from the daemon: " + D.describeLook(look))
          root.setLook("muted", look)
        } else {
          root.dbg("IGNORED", "the muted look read from the daemon was not used: " + (look === null ? "it was not a look" : "a save is in flight, and what was saved last wins"))
        }
      }
    }
    onRunningChanged: root.traceRun("getMuted", getMuted)
    onExited: function(exitCode) { root.traceExit("getMuted", exitCode) }
  }

  // `peri look ... follow` answers with the setting as the daemon holds it; a failure puts the shown value back.
  Process {
    id: follower
    stdout: StdioCollector {
      onStreamFinished: {
        var follow = M.parseFollow(this.text)
        root.traceText("OUT", "follow", this.text)
        if (follow !== null) state.follow = follow
      }
    }
    stderr: StdioCollector {
      onStreamFinished: { root.traceText("ERR", "follow", this.text); state.errText = this.text.trim() }
    }
    onRunningChanged: root.traceRun("follower", follower)
    onExited: function(exitCode) {
      root.traceExit("follower", exitCode)
      if (exitCode !== 0) {
        state.error = state.errText || "the setting could not be saved"
        root.refresh()
      }
    }
  }

  // `peri look ... save` answers with the look as the daemon saved it, in the format of `get`.
  Process {
    id: saver
    property string which: "unmuted"
    stdout: StdioCollector {
      onStreamFinished: {
        // The answer says which look it is (`look unmuted`), so one that arrives after the next
        // save has started can never land on the wrong look.
        var which = L.parseValues(this.text).look
        var look = M.parseLook(this.text)
        root.traceText("OUT", "save", this.text)
        if (look !== null && (which === "unmuted" || which === "muted") && state.pending === null) {
          root.dbg("STATE", "the daemon saved the " + which + " look as: " + D.describeLook(look))
          root.setLook(which, look)
        }
      }
    }
    stderr: StdioCollector {
      onStreamFinished: { root.traceText("ERR", "save", this.text); state.errText = this.text.trim() }
    }
    onRunningChanged: root.traceRun("saver", saver)
    onExited: function(exitCode) {
      root.traceExit("saver", exitCode)
      if (exitCode !== 0) {
        state.error = state.errText || "the look could not be saved"
        state.pending = null
        root.refresh()
      } else if (state.pending !== null) {
        root.sendPending()
      }
    }
  }

  Process {
    id: applier
    property string which: "unmuted"
    stdout: StdioCollector {
      onStreamFinished: {
        var v = L.parseValues(this.text)
        root.traceText("OUT", "apply", this.text)
        if (v.applied) state.notice = "Sent the " + M.lookLabel(v.applied).toLowerCase() + " to the microphone."
      }
    }
    stderr: StdioCollector {
      onStreamFinished: { root.traceText("ERR", "apply", this.text); state.errText = this.text.trim() }
    }
    onRunningChanged: root.traceRun("applier", applier)
    onExited: function(exitCode) {
      root.traceExit("applier", exitCode)
      if (exitCode !== 0) {
        state.notice = ""
        state.error = state.errText || "the microphone did not accept the look"
      }
      if (!status.running) status.running = true // read the brightness it holds now
    }
  }

  // The sound card's controls. A failed read means the card is not there: everything unknown.
  Process {
    id: audioGet
    command: Cli.command(root.periPath, ["audio", root.moduleId, "get"])
    stdout: StdioCollector {
      onStreamFinished: {
        var a = M.parseAudio(this.text)
        root.traceText("OUT", "audio get", this.text)
        if (audioSetter.running || Object.keys(state.pendingAudio).length > 0) {
          root.dbg("IGNORED", "the audio levels read were not used: a change is in flight")
          return
        }
        state.audio = a !== null ? a : M.emptyAudio()
        root.dbg("STATE", "audio levels: " + D.describeAudio(state.audio))
      }
    }
    onRunningChanged: root.traceRun("audioGet", audioGet)
    onExited: function(exitCode) { root.traceExit("audioGet", exitCode) }
  }

  // `peri audio set` answers with the controls as the sound card now holds them.
  Process {
    id: audioSetter
    stdout: StdioCollector {
      onStreamFinished: {
        var a = M.parseAudio(this.text)
        root.traceText("OUT", "audio set", this.text)
        if (a !== null && Object.keys(state.pendingAudio).length === 0) state.audio = a
      }
    }
    stderr: StdioCollector {
      onStreamFinished: { root.traceText("ERR", "audio set", this.text); state.errText = this.text.trim() }
    }
    onRunningChanged: root.traceRun("audioSetter", audioSetter)
    onExited: function(exitCode) {
      root.traceExit("audioSetter", exitCode)
      if (exitCode !== 0) {
        state.audioError = state.errText || "the sound card did not accept the change"
        state.pendingAudio = {}
        if (!audioGet.running) audioGet.running = true
      } else if (Object.keys(state.pendingAudio).length > 0) {
        root.sendAudio()
      }
    }
  }

  // The log's writer: one `cat >>` kept open while debug is on, fed with write().
  Process {
    id: sink
    stdinEnabled: true
    command: ["sh", "-c", "mkdir -p \"$(dirname \"$1\")\" && exec cat >> \"$1\"", "sh", root.debugPath]
    onRunningChanged: if (running) root.flush()
  }

  // When the log starts, copy what the daemon's journal says about the mic (follow mute, applies).
  Process {
    id: journal
    command: ["sh", "-c", "journalctl --user -u perid --since '-30 min' --no-pager -o cat 2>/dev/null | grep -iE 'follow|razer|seiren' | tail -25"]
    stdout: StdioCollector {
      onStreamFinished: {
        var lines = this.text.split("\n").filter(function(l) { return l.trim() !== "" })
        root.dbg("DAEMON", lines.length === 0 ? "nothing about the mic in the daemon's journal for the last 30 minutes"
                                              : lines.length + " journal line(s) from the last 30 minutes follow")
        lines.forEach(function(l) { root.dbg("DAEMON", l) })
      }
    }
  }

  Timer {
    id: stopSink
    interval: 600
    onTriggered: if (!state.debug) sink.running = false
  }
}
