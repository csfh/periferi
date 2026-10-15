import QtQuick
import QtTest
import "../../quickshell/peri/menu.js" as Menu
import "../../quickshell/peri/cli.js" as Cli

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
  function withEngine(m, engine) { m.engine = engine; return m }

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
  // The engine travels with every kind of entry, so the panel can tell a keyboard from a mic.
  function test_engine_reaches_every_entry() {
    var mic = withEngine(mod("mic", "Mic", "installed"), "razer")
    var kb = withEngine(mod("drop-ctrl-v2", "Drop CTRL v2", "installed"), "xap")
    var avail = withEngine(mod("other", "Other", "available"), "razer")
    var m = Menu.build([dev("g1", "Mic", "installed", "mic")], [mic, kb, avail])
    compare(m.managed.filter(function(e) { return e.key === "g1" })[0].engine, "razer")
    compare(m.managed.filter(function(e) { return e.module === "drop-ctrl-v2" })[0].engine, "xap")
    compare(m.installable[0].engine, "razer")
  }

  // Only the XAP engine gets the keyboard controls. An unknown engine (a list that could not be
  // read) keeps them, so the keyboard does not lose its panel; anything else never gets them.
  function test_drives_keyboard() {
    compare(Menu.drivesKeyboard({ engine: "xap" }), true)
    compare(Menu.drivesKeyboard({ engine: "" }), true)
    compare(Menu.drivesKeyboard({ engine: "razer" }), false)
    compare(Menu.drivesKeyboard({ engine: "razer90" }), false)
    compare(Menu.drivesKeyboard({ engine: "something-new" }), false)
    compare(Menu.drivesKeyboard(null), false)
    // A device whose module is missing from the list has engine "".
    var m = Menu.build([dev("g1", "Drop CTRL v2", "installed", "drop-ctrl-v2")], [])
    compare(m.managed[0].engine, "")
    compare(Menu.drivesKeyboard(m.managed[0]), true)
  }

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
    var m = Menu.build([dev("g5", "YubiKey", "none", null, "fido")], [])
    compare(m.unmanaged.length + m.managed.length + m.installable.length, 0)
  }

  // A camera is an ordinary device: listed, and managed once a module matches it.
  function test_a_camera_is_not_hidden() {
    var m = Menu.build([dev("g6", "BRIO", "none", null, null)], [])
    compare(keys(m.unmanaged), ["g6"])
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

  // The right-hand pane shows one managed device: the chosen one, else the first.
  function test_select_entry() {
    var list = [{ key: "a", name: "A" }, { key: "b", name: "B" }]
    compare(Menu.selectEntry(list, "b").name, "B")
    compare(Menu.selectEntry(list, "a").name, "A")
    compare(Menu.selectEntry(list, "gone").name, "A")   // removed or unplugged: fall back to the first
    compare(Menu.selectEntry(list, "").name, "A")
    compare(Menu.selectEntry(list, undefined).name, "A")
    compare(Menu.selectEntry([], "a"), null)
    compare(Menu.selectEntry(null, "a"), null)
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
    var c = Cli.command("/home/u/.local/bin/peri", ["use", hostile])
    compare(c.slice(0, 2), ["sh", "-c"])
    compare(c.slice(3), ["sh", "/home/u/.local/bin/peri", "use", hostile])
    verify(c[2].indexOf("rm -rf") < 0 && c[2].indexOf("pwned") < 0)
    verify(c[2].indexOf("\"$@\"") >= 0)
    compare(c[2], Cli.command("/other", ["remove", "y"])[2]) // the script never varies
  }
}
