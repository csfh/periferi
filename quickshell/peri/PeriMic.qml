import QtQuick
import Quickshell
import Quickshell.Io
import "mic.js" as M
import "lighting.js" as L
import "cli.js" as Cli

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
Scope {
  id: root

  property string periPath: Quickshell.env("HOME") + "/.local/bin/peri"
  property string moduleId: ""            // the installed module that drives this mic
  property string editing: "unmuted"      // the look the controls edit; a pane opens on Unmuted

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

  // Read the mic's online state and brightness, and both saved looks. Skipped while an apply is
  // running. Nothing here writes to the mic.
  function refresh() {
    if (root.moduleId === "" || applier.running) return
    state.notice = ""
    if (!status.running) status.running = true
    if (!getUnmuted.running) getUnmuted.running = true
    if (!getMuted.running) getMuted.running = true
    if (!audioGet.running) audioGet.running = true
  }

  function selectLook(which) {
    if (which === "unmuted" || which === "muted") root.editing = which
  }

  // An effect button: saves the look being edited with that effect, keeping the colours the effect
  // can use. File only.
  function chooseEffect(id) {
    var next = M.withEffect(root.look, id)
    if (next !== null) saveLook(root.editing, next)
  }

  // A swatch: slot 1 is the first colour (it switches to "Colour" if the effect takes none), slot 2
  // the second colour of breathing. File only.
  function chooseColor(slot, hex) {
    var next = M.withColor(root.look, slot, hex)
    if (next !== null) saveLook(root.editing, next)
  }

  // Slow or Fast for a wave. File only.
  function chooseSpeed(speed) {
    var next = M.withSpeed(root.look, speed)
    if (next !== null) saveLook(root.editing, next)
  }

  // "Random colours" for breathing. File only.
  function chooseRandomColors() {
    var next = M.withRandomColors(root.look)
    if (next !== null) saveLook(root.editing, next)
  }

  // Call from a slider's `released`: saves the look being edited with that brightness. File only.
  function chooseBrightness(percent) {
    var next = M.withBrightness(root.look, percent)
    if (next !== null) saveLook(root.editing, next)
  }

  // A released audio slider or the monitor button: key is "gain", "monitor", "headphones" or
  // "monitor_on" (a boolean). These are live sound-card levels, not looks, so they apply at
  // once; a value that would change nothing is not sent, and several quick ones go out together.
  function chooseAudio(key, value) {
    var setting = M.audioSetting(key, value, state.audio)
    if (setting === null || root.moduleId === "") return
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
    if (root.moduleId === "" || follower.running) return
    state.follow = on // shown at once; the daemon's answer confirms it
    follower.command = Cli.command(root.periPath, ["look", root.moduleId, "follow", on ? "on" : "off"])
    follower.running = true
  }

  // Send the look being edited to the mic. The only call here that does.
  function apply() {
    if (!root.canApply || root.moduleId === "") return
    state.error = ""
    state.notice = ""
    state.errText = ""
    applier.which = root.editing
    applier.command = Cli.command(root.periPath, ["look", root.moduleId].concat(M.applyArgs(root.editing)))
    applier.running = true
  }

  function saveLook(which, look) {
    if (root.moduleId === "") return
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
        state.online = s.online
        if (s.online) state.deviceBrightness = s.brightness
      }
    }
    onExited: function(exitCode) {
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
        if (follow !== null && !follower.running) state.follow = follow
        if (look !== null && !saver.running && state.pending === null) root.setLook("unmuted", look)
      }
    }
  }
  Process {
    id: getMuted
    command: Cli.command(root.periPath, ["look", root.moduleId, "get", "muted"])
    stdout: StdioCollector {
      onStreamFinished: {
        var look = M.parseLook(this.text)
        if (look !== null && !saver.running && state.pending === null) root.setLook("muted", look)
      }
    }
  }

  // `peri look ... follow` answers with the setting as the daemon holds it; a failure puts the shown value back.
  Process {
    id: follower
    stdout: StdioCollector {
      onStreamFinished: {
        var follow = M.parseFollow(this.text)
        if (follow !== null) state.follow = follow
      }
    }
    stderr: StdioCollector {
      onStreamFinished: state.errText = this.text.trim()
    }
    onExited: function(exitCode) {
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
        if (look !== null && (which === "unmuted" || which === "muted") && state.pending === null)
          root.setLook(which, look)
      }
    }
    stderr: StdioCollector {
      onStreamFinished: state.errText = this.text.trim()
    }
    onExited: function(exitCode) {
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
        if (v.applied) state.notice = "Sent the " + M.lookLabel(v.applied).toLowerCase() + " to the microphone."
      }
    }
    stderr: StdioCollector {
      onStreamFinished: state.errText = this.text.trim()
    }
    onExited: function(exitCode) {
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
        if (audioSetter.running || Object.keys(state.pendingAudio).length > 0) return
        state.audio = a !== null ? a : M.emptyAudio()
      }
    }
  }

  // `peri audio set` answers with the controls as the sound card now holds them.
  Process {
    id: audioSetter
    stdout: StdioCollector {
      onStreamFinished: {
        var a = M.parseAudio(this.text)
        if (a !== null && Object.keys(state.pendingAudio).length === 0) state.audio = a
      }
    }
    stderr: StdioCollector {
      onStreamFinished: state.errText = this.text.trim()
    }
    onExited: function(exitCode) {
      if (exitCode !== 0) {
        state.audioError = state.errText || "the sound card did not accept the change"
        state.pendingAudio = {}
        if (!audioGet.running) audioGet.running = true
      } else if (Object.keys(state.pendingAudio).length > 0) {
        root.sendAudio()
      }
    }
  }
}
