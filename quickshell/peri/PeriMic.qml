import QtQuick
import Quickshell
import Quickshell.Io
import "mic.js" as M
import "cli.js" as Cli

// A microphone (the Razer Seiren V3 Chroma) as seen by the peri daemon. Like PeriLighting it
// imports nothing from the host shell and draws nothing; hosts bind to the properties below.
//
//   PeriMic { id: mic; moduleId: "razer-seiren-v3-chroma" }
//   MySlider { value: mic.brightness; onReleased: v => mic.setBrightness(v) }
//
// It reads `peri status <module>` and writes `peri set <module> ...`. Two facts shape it:
//   - The mic cannot report its effect or colour. The daemon keeps a record of what peri last
//     sent it, and lastEffect / lastColor expose that record: it is not what the mic shows, and a
//     change made any other way is not in it.
//   - A set may be kept in the mic's own memory. So a change is only ever sent from a deliberate
//     gesture: setBrightness() belongs in a slider's `released` handler (never `moved`, and never
//     a binding), and after the last release the pane waits `quiet` ms and sends ONE set, so a
//     wheel scrolled over the slider (one `released` per notch) or a quick second drag is not a
//     burst of writes. A value equal to what the mic already has is not sent at all.
Scope {
  id: root

  property string periPath: Quickshell.env("HOME") + "/.local/bin/peri"
  property string moduleId: ""   // the installed module that drives this mic
  property int quiet: 600        // ms of quiet after the last brightness release before the set

  readonly property bool online: state.online
  readonly property int brightness: state.brightness   // percent of the protocol's 0-255
  readonly property string mute: state.mute             // "muted", "unmuted" or "unknown"
  readonly property var dial: state.dial                // the level as reported, or null
  readonly property string lastEffect: state.effect     // what peri last sent: "static", "breathing",
                                                        // "spectrum", "none", or "" if never
  readonly property string lastColor: state.color       // six hex digits for "static", else ""
  readonly property var last: ({ effect: state.effect, color: state.color }) // for mic.js's helpers
  readonly property bool busy: setter.running || debounce.running
  readonly property string error: state.error

  // Read the mic's state. Skipped while a write is waiting or running, so a refresh never puts
  // an old value back under a slider that was just released.
  function refresh() {
    if (root.moduleId === "" || query.running || setter.running || debounce.running) return
    query.running = true
  }

  // Call from a slider's `released` only. The new value shows at once; the mic's answer to the
  // refresh that follows the write confirms it or puts the old one back.
  function setBrightness(percent) {
    if (!state.online || root.moduleId === "") return
    state.brightness = Math.max(0, Math.min(100, Math.round(percent)))
    state.wanted = percent
    debounce.restart()
  }

  // A button press: "static" with a colour (six hex digits), or "breathing", "spectrum", "none".
  function setEffect(id, hex) {
    var settings = M.effectSettings(id, hex)
    if (settings !== null) send(settings)
  }

  function send(settings) {
    if (root.moduleId === "" || setter.running) return
    state.error = ""
    state.errText = ""
    setter.command = Cli.command(root.periPath, ["set", root.moduleId].concat(settings))
    setter.running = true
  }

  QtObject {
    id: state
    property bool online: false
    property int rawBrightness: 0   // 0-255 as the mic holds it
    property int brightness: 0      // percent, what the slider shows
    property real wanted: 0         // the percent of the last release, waiting to be sent
    property string mute: "unknown"
    property var dial: null
    property string effect: ""
    property string color: ""
    property string error: ""
    property string errText: ""
  }

  function parse(text) {
    var s = M.parseStatus(text)
    state.online = s.online
    if (!s.online) return
    state.rawBrightness = s.brightness
    state.brightness = M.brightnessPercent(s.brightness)
    state.mute = s.mute
    state.dial = s.dial
    state.effect = s.effect
    state.color = s.color
  }

  Timer {
    id: debounce
    interval: root.quiet
    onTriggered: {
      var setting = M.brightnessSetting(state.wanted, state.rawBrightness)
      if (setting !== null) root.send([setting])
      else root.refresh()
    }
  }

  Process {
    id: query
    command: Cli.command(root.periPath, ["status", root.moduleId])
    stdout: StdioCollector {
      onStreamFinished: root.parse(this.text)
    }
    onExited: function(exitCode) {
      if (exitCode !== 0) state.online = false
    }
  }

  Process {
    id: setter
    stderr: StdioCollector {
      onStreamFinished: state.errText = this.text.trim()
    }
    onExited: function(exitCode) {
      state.error = exitCode === 0 ? "" : (state.errText || "the microphone did not accept the change")
      Qt.callLater(root.refresh) // read back what the mic holds now
    }
  }
}
