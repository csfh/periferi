import QtQuick
import Quickshell
import "peri"

// Check against the real daemon, with no window. Prints the three sections after every step
// and exits. Run with: quickshell -p tests/qml/live
//
// Default: read-only; refreshes three times (the real widget reuses its processes forever).
// PERI_LIVE_ACTIONS=1 also runs install("nope") (must fail), remove("drop-ctrl-v2") and
// install("drop-ctrl-v2"). Those change only the user's module folder, never a device,
// and leave drop-ctrl-v2 installed again.
ShellRoot {
  id: root

  property var steps: {
    var list = [function() { peri.refresh() }, function() { peri.refresh() }]
    if (Quickshell.env("PERI_LIVE_ACTIONS") === "1")
      list.push(function() { peri.install("nope") },
                function() { peri.remove("drop-ctrl-v2") },
                function() { peri.install("drop-ctrl-v2") })
    return list
  }
  property int next: 0
  property int refreshes: 0

  PeriDevices {
    id: peri
    interval: 0
    onRefreshed: {
      root.refreshes++
      var line = "REFRESH " + root.refreshes + " online=" + peri.online
      if (peri.error) line += " error=" + peri.error
      if (peri.actionError) line += " actionError=" + peri.actionError
      console.log(line)
      var show = function(title, list) {
        console.log("  " + title + " (" + list.length + ")")
        list.forEach(function(e) { console.log("    " + JSON.stringify(e)) })
      }
      show("MANAGED", peri.managed)
      show("INSTALLABLE", peri.installable)
      show("UNMANAGED", peri.unmanaged)
      if (root.next < root.steps.length) root.steps[root.next++]()
      else Qt.quit()
    }
  }
  Component.onCompleted: peri.refresh()
  Timer { interval: 15000; running: true; onTriggered: { console.log("TIMEOUT"); Qt.quit() } }
}
