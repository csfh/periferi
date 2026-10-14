import QtQuick
import QtTest
import "../../quickshell/peri/mouse.js" as Mouse

TestCase {
  name: "mouse"

  // What `peri status` prints for the DeathAdder (the daemon's keys, through the CLI's table).
  readonly property string status: "module   razer-deathadder-v3-pro\nfirmware 0200\nbattery  255\ncharging no\ndpi      800:800\npoll     1000\n"

  function test_parse_status() {
    var s = Mouse.parseStatus(status)
    compare(s.online, true)
    compare(s.battery, 255)
    compare(s.batteryPercent, 100)
    compare(s.charging, false)
    compare(s.dpi.x, 800)
    compare(s.dpi.y, 800)
    compare(s.poll, 1000)
  }

  // An answered read that was not ok prints "unknown": that field is empty, the others still show.
  function test_unknown_fields() {
    var s = Mouse.parseStatus("module m\nfirmware 0200\nbattery unknown\ncharging unknown\ndpi unknown\npoll unknown\n")
    compare(s.online, true)
    compare(s.battery, -1)
    compare(s.batteryPercent, -1)
    compare(s.charging, null)
    compare(s.dpi, null)
    compare(s.poll, 0)
    compare(Mouse.batteryText(s), "Battery  -")
    compare(Mouse.dpiText(s), "DPI  -")
  }

  // A refusal or nothing at all is offline: never show stale numbers as live.
  function test_offline() {
    compare(Mouse.parseStatus("").online, false)
    compare(Mouse.parseStatus("razer-deathadder-v3-pro: the device did not answer\n").online, false)
    compare(Mouse.parseStatus("module m\nbattery 10\n").online, false)    // no firmware
  }

  function test_a_poll_code_the_daemon_does_not_know_is_not_a_rate() {
    compare(Mouse.parseStatus("module m\nfirmware 0200\npoll 0\n").poll, 0)
    compare(Mouse.parseStatus("module m\nfirmware 0200\npoll 8000\n").poll, 0)   // not offered by this pane
  }

  function test_battery_text() {
    var s = Mouse.parseStatus("module m\nfirmware 0200\nbattery 128\ncharging yes\n")
    compare(Mouse.batteryText(s), "Battery  50%  (charging)")
  }

  function test_dpi_text_and_selection() {
    var s = Mouse.parseStatus(status)
    compare(Mouse.dpiText(s), "DPI  800")
    compare(Mouse.dpiSelected(800, s), true)
    compare(Mouse.dpiSelected(1600, s), false)
    var split = Mouse.parseStatus("module m\nfirmware 0200\ndpi 800:1600\n")
    compare(Mouse.dpiText(split), "DPI  800 x 1600")
    compare(Mouse.dpiSelected(800, split), false)      // only when both axes agree
    compare(Mouse.pollSelected(1000, s), true)
    compare(Mouse.pollSelected(500, s), false)
    compare(Mouse.dpiSelected(800, Mouse.parseStatus("")), false)
  }

  // The slider can never offer what the daemon would refuse; arguments are only built for values
  // the pane may send.
  function test_arguments() {
    compare(Mouse.dpiArg(1600), "dpi=1600")
    compare(Mouse.dpiArg(5), "dpi=100")
    compare(Mouse.dpiArg(99999), "dpi=30000")
    compare(Mouse.dpiArg("nonsense"), "dpi=100")
    compare(Mouse.pollArg(500), "poll=500")
    compare(Mouse.pollArg(250), "")
    compare(Mouse.pollArg(8000), "")
  }

  function test_parse_set() {
    var r = Mouse.parseSet("module   m\nsent     dpi:1600\nbefore   800:800\ndevice   ok\nreadback 1600:1600\nstored   maybe\n")
    compare(r.ok, true)
    compare(r.dpi.x, 1600)
    compare(r.poll, 0)
    r = Mouse.parseSet("module m\nsent poll:500\nbefore 1000\ndevice ok\nreadback 500\nstored maybe\n")
    compare(r.ok, true)
    compare(r.poll, 500)
    compare(r.dpi, null)
    r = Mouse.parseSet("module m\nsent dpi:800\nbefore 1:1\ndevice ok\nreadback unavailable\nstored maybe\n")
    compare(r.ok, true)
    compare(r.dpi, null)
    compare(Mouse.parseSet("m: the change was not accepted\n").ok, false)
  }
}
