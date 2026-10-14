import QtQuick
import Quickshell
import Quickshell.Io
import "menu.js" as Menu
import "cli.js" as Cli

// Every HID peripheral, sorted into what a device menu shows. Like PeriLighting it works in
// any Quickshell config, imports nothing from the host shell and draws nothing; hosts bind
// to the lists below.
//
//   PeriDevices { id: devices }
//   Repeater { model: devices.managed; ... }     // each has .key .name .module .connected
//
// It reads through the `peri` CLI (`peri list --json`, `peri modules --json`); the daemon does
// all the matching. install() and remove() change only the user's module folder, never a
// device, and always refresh afterwards.
Scope {
  id: root

  // Where to find peri. If this path is not executable, `peri` on PATH is used.
  property string periPath: Quickshell.env("HOME") + "/.local/bin/peri"
  property int interval: 30000 // ms between automatic refreshes; 0 disables

  // Devices an installed module drives, plus installed modules whose device is unplugged
  // (connected: false, so they can still be removed). Entries: key, name, vid, pid, module, connected.
  readonly property var managed: menu.managed
  // Modules that are not installed. Entries: key, id, name, version, vid, pid, connected
  // (their device is plugged in right now).
  readonly property var installable: menu.installable
  // Visible devices no module can drive. Entries: key, name, vid, pid, module (null).
  readonly property var unmanaged: menu.unmanaged

  readonly property bool online: state.online   // the last refresh worked
  readonly property string error: state.error   // why the last refresh failed, or ""
  // Why the last install() or remove() failed, or "". Refreshing does not clear it; the next
  // install() or remove() does.
  readonly property string actionError: state.actionError
  readonly property bool busy: state.refreshing || state.acting

  signal refreshed() // a refresh finished, well or not (the lists keep the last good data)

  function refresh() {
    if (busy) { state.pending = true; return }
    state.refreshing = true
    lister.command = Cli.command(root.periPath, ["list", "--json"])
    lister.running = true
  }

  // Copy a bundled module into the user's module folder / delete it again. Ignored while
  // another change or refresh is in flight.
  function install(id) { act("use", id) }
  function remove(id) { act("remove", id) }

  function act(verb, id) {
    if (busy || !id) return
    state.acting = true
    state.actionError = ""
    state.exitCode = -1
    state.outDone = false
    state.errDone = false
    state.actionOut = ""
    state.actionErr = ""
    settleTimer.stop()
    actor.command = Cli.command(root.periPath, [verb, String(id)])
    actor.running = true
  }

  // The last good answers. Replaced together, so a device never shows in two sections.
  property var devices: []
  property var modules: []
  readonly property var menu: Menu.build(devices, modules)

  QtObject {
    id: state
    property bool online: false
    property string error: ""
    property bool refreshing: false
    property bool acting: false
    property bool pending: false
    property var staged: null
    property string actionOut: ""
    property string actionErr: ""
    property string actionError: ""
    property int exitCode: -1
    property bool outDone: false
    property bool errDone: false
  }

  // The exit code and the two output streams can finish in any order. Settle once all
  // three are in, so the real message is shown; a short timer settles anyway if a stream
  // never reports, so a change can never leave the menu busy.
  function settle(force) {
    if (!state.acting || state.exitCode < 0) return
    if (!force && !(state.outDone && state.errDone)) return
    settleTimer.stop()
    var message = (state.actionErr || state.actionOut).trim()
    state.actionError = state.exitCode === 0 ? "" : (message || "the change failed")
    state.acting = false
    refresh()
  }

  Timer { id: settleTimer; interval: 500; onTriggered: root.settle(true) }

  function finish(error) {
    state.online = error === ""
    state.error = error
    state.refreshing = false
    refreshed()
    if (state.pending) { state.pending = false; refresh() }
  }

  // peri prints nothing on stdout when it fails, so unparseable output means failure.
  Process {
    id: lister
    stdout: StdioCollector {
      onStreamFinished: {
        var list = Menu.parse(this.text)
        if (list === null) { root.finish("could not read the device list"); return }
        state.staged = list
        modulesReader.command = Cli.command(root.periPath, ["modules", "--json"])
        modulesReader.running = true
      }
    }
  }

  Process {
    id: modulesReader
    stdout: StdioCollector {
      onStreamFinished: {
        var list = Menu.parse(this.text)
        if (list === null) { root.finish("could not read the module list"); return }
        root.devices = state.staged
        root.modules = list
        state.staged = null
        root.finish("")
      }
    }
  }

  Process {
    id: actor
    stdout: StdioCollector {
      onStreamFinished: { state.actionOut = this.text; state.outDone = true; root.settle(false) }
    }
    stderr: StdioCollector {
      onStreamFinished: { state.actionErr = this.text; state.errDone = true; root.settle(false) }
    }
    onExited: function(exitCode) {
      state.exitCode = exitCode
      settleTimer.restart()
      root.settle(false)
    }
  }

  Timer {
    interval: root.interval
    running: root.interval > 0
    repeat: true
    triggeredOnStart: true
    onTriggered: root.refresh()
  }
}
