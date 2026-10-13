import QtQuick
import QtTest
import "../../quickshell/peri/menu.js" as Menu

TestCase {
  name: "menu"

  function dev(group, name, status, module, hidden) {
    return { group: group, vid: "359b", pid: "0009", name: name, status: status,
             module: module === undefined ? null : module,
             hidden: hidden === undefined ? null : hidden }
  }
  function mod(id, name, status, access) {
    return { id: id, name: name, version: "0.1.0", vid: "359b", pid: "0009", status: status,
             access: access === undefined ? (status === "installed" ? "ok" : null) : access }
  }
  function keys(list) { return list.map(function(e) { return e.key }) }

  function test_installed_and_connected() {
    var m = Menu.build([dev("g1", "Drop CTRL v2", "installed", "drop-ctrl-v2")],
                       [mod("drop-ctrl-v2", "Drop CTRL v2", "installed")])
    compare(keys(m.managed), ["g1"])
    compare(m.managed[0].connected, true)
    compare(m.managed[0].module, "drop-ctrl-v2")
    compare(m.installable.length, 0)
    compare(m.unmanaged.length, 0)
  }

  // After `peri remove`: the keyboard is plugged in and its module is available.
  function test_available_goes_to_installable_only() {
    var m = Menu.build([dev("g1", "Drop CTRL v2", "available", "drop-ctrl-v2")],
                       [mod("drop-ctrl-v2", "Drop CTRL v2", "available")])
    compare(m.managed.length, 0)
    compare(m.unmanaged.length, 0)
    compare(keys(m.installable), ["module:drop-ctrl-v2"])
    compare(m.installable[0].connected, true)
    compare(m.installable[0].id, "drop-ctrl-v2")
  }

  function test_available_but_not_plugged_in() {
    var m = Menu.build([], [mod("drop-ctrl-v2", "Drop CTRL v2", "available")])
    compare(m.installable.length, 1)
    compare(m.installable[0].connected, false)
  }

  function test_status_none_is_unmanaged() {
    var m = Menu.build([dev("g2", "Seiren", "none")], [])
    compare(keys(m.unmanaged), ["g2"])
    compare(m.managed.length, 0)
    compare(m.installable.length, 0)
  }

  // Installed, but the keyboard is unplugged: it must stay reachable so it can be removed.
  function test_installed_but_unplugged_stays_in_managed() {
    var m = Menu.build([], [mod("drop-ctrl-v2", "Drop CTRL v2", "installed")])
    compare(keys(m.managed), ["module:drop-ctrl-v2"])
    compare(m.managed[0].connected, false)
    compare(m.installable.length, 0)
  }

  // Two keyboards on one module: two rows, and no extra "unplugged" row.
  function test_two_devices_one_module() {
    var m = Menu.build([dev("g1", "Drop CTRL v2", "installed", "drop-ctrl-v2"),
                        dev("g3", "Drop CTRL v2", "installed", "drop-ctrl-v2")],
                       [mod("drop-ctrl-v2", "Drop CTRL v2", "installed")])
    compare(keys(m.managed), ["g1", "g3"])
  }

  // An installed module that is not in the bundled catalog still shows up.
  function test_installed_module_only_on_disk() {
    var m = Menu.build([dev("g9", "Custom", "installed", "my-mod")],
                       [mod("my-mod", "My mod", "installed")])
    compare(keys(m.managed), ["g9"])
  }

  // The device list and module list were read a moment apart: do not lose the device.
  function test_available_for_an_unknown_module_is_unmanaged() {
    var m = Menu.build([dev("g1", "Drop CTRL v2", "available", "drop-ctrl-v2")], [])
    compare(keys(m.unmanaged), ["g1"])
    compare(m.installable.length, 0)
  }

  function test_hidden_devices_are_dropped() {
    var m = Menu.build([dev("g5", "YubiKey", "none", null, "fido"),
                        dev("g6", "BRIO", "none", null, "camera")], [])
    compare(m.unmanaged.length + m.managed.length + m.installable.length, 0)
  }

  function test_sorted_by_name_then_key_not_scan_order() {
    var m = Menu.build([dev("g2", "seiren", "none"), dev("g1", "Zed", "none"),
                        dev("g4", "Alpha", "none"), dev("g3", "Alpha", "none")], [])
    compare(keys(m.unmanaged), ["g3", "g4", "g2", "g1"])
  }

  function test_empty_and_bad_input() {
    var m = Menu.build(null, undefined)
    compare(m.managed.length + m.installable.length + m.unmanaged.length, 0)
    m = Menu.build("nope", {})
    compare(m.managed.length + m.installable.length + m.unmanaged.length, 0)
  }

  // `setup` says `peri setup` has to update the udev rule for this module's device.
  function test_setup_flag_follows_the_modules_access() {
    var m = Menu.build([dev("g1", "Drop CTRL v2", "installed", "drop-ctrl-v2")],
                       [mod("drop-ctrl-v2", "Drop CTRL v2", "installed", "setup")])
    compare(m.managed[0].setup, true)
    m = Menu.build([dev("g1", "Drop CTRL v2", "installed", "drop-ctrl-v2")],
                   [mod("drop-ctrl-v2", "Drop CTRL v2", "installed", "ok")])
    compare(m.managed[0].setup, false)
  }

  function test_setup_flag_on_an_unplugged_module() {
    var m = Menu.build([], [mod("drop-ctrl-v2", "Drop CTRL v2", "installed", "setup")])
    compare(m.managed[0].connected, false)
    compare(m.managed[0].setup, true)
    m = Menu.build([], [mod("drop-ctrl-v2", "Drop CTRL v2", "installed", "ok")])
    compare(m.managed[0].setup, false)
  }

  // Installable modules are not installed, so there is no rule to wait for.
  function test_setup_flag_is_false_when_the_module_is_unknown_or_old_peri() {
    var m = Menu.build([dev("g9", "Custom", "installed", "my-mod")], [])
    compare(m.managed[0].setup, false)
    var old = { id: "drop-ctrl-v2", name: "x", version: "1", vid: "359b", pid: "0009", status: "installed" }
    m = Menu.build([], [old]) // a peri that predates `access`
    compare(m.managed[0].setup, false)
  }

  function test_parse() {
    compare(Menu.parse("[]"), [])
    compare(Menu.parse('[{"a": 1}]').length, 1)
    compare(Menu.parse(""), null)           // peri prints nothing when it fails
    compare(Menu.parse("not json"), null)
    compare(Menu.parse('{"a": 1}'), null)   // an object is not a list
    compare(Menu.parse("null"), null)
  }

  // The id is its own argv element after the script, so it can never be shell code.
  function test_command_keeps_arguments_separate() {
    var hostile = "x; rm -rf ~ $(touch /tmp/pwned) `id`"
    var c = Menu.command("/home/u/.local/bin/peri", ["use", hostile])
    compare(c.slice(0, 2), ["sh", "-c"])
    compare(c.slice(3), ["sh", "/home/u/.local/bin/peri", "use", hostile])
    verify(c[2].indexOf("rm -rf") < 0 && c[2].indexOf("pwned") < 0)
    verify(c[2].indexOf("\"$@\"") >= 0)
    compare(c[2], Menu.command("/other", ["remove", "y"])[2]) // the script never varies
  }
}
