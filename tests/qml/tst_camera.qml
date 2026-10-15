import QtQuick
import QtTest
import "../../quickshell/peri/camera.js" as Cam

TestCase {
  name: "camera"

  // What `peri controls brio` printed on the real camera (2026-10-15), through the CLI's table.
  readonly property string reply: "module   brio\n"
    + "brightness int:0:255:1:128:s\ncontrast int:0:255:1:128:s\nsaturation int:0:255:1:128:s\n"
    + "white_balance_automatic bool:0:1:1:1:s\ngain     int:0:255:1:0:s\n"
    + "power_line_frequency menu:0:2:1:2:s\npower_line_frequency.options 0:Disabled,1:50_Hz,2:60_Hz\n"
    + "white_balance_temperature int:2000:7500:10:3660:si\nsharpness int:0:255:1:128:s\n"
    + "backlight_compensation int:0:1:1:0:s\nauto_exposure menu:0:3:1:3:s\n"
    + "auto_exposure.options 1:Manual_Mode,3:Aperture_Priority_Mode\n"
    + "exposure_time_absolute int:3:2047:1:312:si\nexposure_dynamic_framerate bool:0:1:1:1:s\n"
    + "field_of_view menu:0:2:1:0:s\nfield_of_view.options 0:Wide,1:Medium,2:Narrow\n"
    + "pan_absolute int:-36000:36000:3600:10800:s\ntilt_absolute int:-36000:36000:3600:0:s\n"
    + "focus_absolute int:0:255:5:10:si\nfocus_automatic_continuous bool:0:1:1:1:s\nzoom_absolute int:100:500:1:100:s\n"

  function test_parse_the_real_reply() {
    var s = Cam.parseControls(reply)
    compare(s.online, true)
    compare(s.controls.length, 18)
    var b = Cam.byName(s.controls, "brightness")
    compare(b.type, "int"); compare(b.min, 0); compare(b.max, 255); compare(b.step, 1); compare(b.value, 128)
    compare(b.settable, true); compare(b.inactive, false)
    var pan = Cam.byName(s.controls, "pan_absolute")
    compare(pan.min, -36000); compare(pan.step, 3600); compare(pan.value, 10800)
    var wb = Cam.byName(s.controls, "white_balance_temperature")
    compare(wb.inactive, true)
    var exp = Cam.byName(s.controls, "auto_exposure")
    compare(exp.type, "menu")
    compare(exp.options.length, 2)
    compare(exp.options[1].index, 3)
    compare(exp.options[1].label, "Aperture Priority Mode")
    compare(Cam.byName(s.controls, "power_line_frequency").options[1].label, "50 Hz")
    // the field of view is drawn like any menu, from the camera's own three options
    var fov = Cam.byName(s.controls, "field_of_view")
    compare(fov.type, "menu"); compare(fov.title, "Field of view")
    compare(fov.options.length, 3); compare(fov.options[0].label, "Wide"); compare(fov.options[2].label, "Narrow")
    compare(Cam.valueText(fov), "Wide")
    compare(Cam.setArg(fov, 1), "field_of_view=1")
    compare(Cam.setArg(fov, 3), "")
  }

  function test_only_settable_controls_are_kept() {
    var s = Cam.parseControls("module m\nbrightness int:0:255:1:128:s\nvendor_thing int:0:9:1:0:-\nlocked int:0:9:1:0:r\n")
    compare(s.controls.length, 1)
    compare(s.controls[0].name, "brightness")
  }

  // A refusal, nothing, or a reply with no usable control is offline: never show stale numbers as live.
  function test_offline() {
    compare(Cam.parseControls("").online, false)
    compare(Cam.parseControls("brio: the device is not connected\n").online, false)
    compare(Cam.parseControls("module brio\n").online, false)
    compare(Cam.parseControls("module brio\nbrightness nonsense\n").online, false)
  }

  function test_titles() {
    compare(Cam.titleOf("exposure_dynamic_framerate"), "Low-light compensation")
    compare(Cam.titleOf("white_balance_automatic"), "Auto white balance")
    compare(Cam.titleOf("some_new_control"), "Some new control")
  }

  // The sections, in order; a header only when it has controls; strangers last under Other.
  function test_rows() {
    var s = Cam.parseControls(reply)
    var rows = Cam.rows(s.controls)
    var headers = rows.filter(function(r) { return r.kind === "header" }).map(function(r) { return r.title })
    compare(headers, ["Image", "White balance", "Exposure", "Focus and framing"])
    compare(rows.length, 18 + 4)
    compare(rows[0].kind, "header")
    compare(rows[1].control.name, "brightness")
    var odd = Cam.parseControls("module m\nbrightness int:0:255:1:128:s\nstrange_thing int:0:9:1:0:s\n")
    var orows = Cam.rows(odd.controls)
    compare(orows[orows.length - 2].title, "Other")
    compare(orows[orows.length - 1].control.name, "strange_thing")
    compare(Cam.rows([]).length, 0)
  }

  // Two columns of about the same height, split at a section, nothing lost or reordered.
  function test_columns() {
    var rows = Cam.rows(Cam.parseControls(reply).controls)
    var cols = Cam.columns(rows)
    compare(cols.length, 2)
    compare(cols[0].concat(cols[1]).length, rows.length)
    compare(cols[0][0].kind, "header")
    compare(cols[1][0].kind, "header")
    compare(cols[1][0].title, "Exposure")   // Image + White balance on the left, the rest on the right
    function height(c) { return c.reduce(function(s, r) { return s + (r.kind === "header" || r.control.type === "bool" ? 1 : 2) }, 0) }
    verify(Math.abs(height(cols[0]) - height(cols[1])) <= 6)
    // a single section stays in one column; nothing at all gives two empty columns
    var one = Cam.rows(Cam.parseControls("module m\nbrightness int:0:255:1:128:s\n").controls)
    compare(Cam.columns(one)[0].length, one.length)
    compare(Cam.columns(one)[1].length, 0)
    compare(Cam.columns([])[0].length, 0)
  }

  function test_valid_values() {
    var s = Cam.parseControls(reply)
    var b = Cam.byName(s.controls, "brightness")
    compare(Cam.valid(b, 0), true); compare(Cam.valid(b, 255), true)
    compare(Cam.valid(b, 256), false); compare(Cam.valid(b, -1), false); compare(Cam.valid(b, 1.5), false)
    var pan = Cam.byName(s.controls, "pan_absolute")
    compare(Cam.valid(pan, 7200), true); compare(Cam.valid(pan, 7000), false)
    var bool = Cam.byName(s.controls, "white_balance_automatic")
    compare(Cam.valid(bool, 0), true); compare(Cam.valid(bool, 2), false)
    var menu = Cam.byName(s.controls, "auto_exposure")
    compare(Cam.valid(menu, 1), true); compare(Cam.valid(menu, 3), true)
    compare(Cam.valid(menu, 2), false)   // a hole: the camera does not offer it
    compare(Cam.valid(menu, 0), false)
    // an inactive control is never offered
    compare(Cam.valid(Cam.byName(s.controls, "exposure_time_absolute"), 250), false)
    compare(Cam.valid(null, 1), false)
  }

  function test_snap_and_arguments() {
    var s = Cam.parseControls(reply)
    var pan = Cam.byName(s.controls, "pan_absolute")
    compare(Cam.snap(pan, 7000), 7200)
    compare(Cam.snap(pan, 99999), 36000)
    compare(Cam.snap(pan, -99999), -36000)
    compare(Cam.snap(pan, "nonsense"), -36000)
    var b = Cam.byName(s.controls, "brightness")
    compare(Cam.snap(b, 160.4), 160)
    compare(Cam.setArg(b, 160), "brightness=160")
    compare(Cam.setArg(b, 300), "")
    compare(Cam.setArg(Cam.byName(s.controls, "auto_exposure"), 1), "auto_exposure=1")
    compare(Cam.setArg(Cam.byName(s.controls, "auto_exposure"), 2), "")
    compare(Cam.isCurrent(b, 128), true); compare(Cam.isCurrent(b, 160), false)
  }

  function test_value_text() {
    var s = Cam.parseControls(reply)
    compare(Cam.valueText(Cam.byName(s.controls, "brightness")), "128")
    compare(Cam.valueText(Cam.byName(s.controls, "white_balance_automatic")), "On")
    // an integer control that only takes 0 and 1 is a switch
    compare(Cam.byName(s.controls, "backlight_compensation").type, "bool")
    compare(Cam.valueText(Cam.byName(s.controls, "backlight_compensation")), "Off")
    compare(Cam.valueText(Cam.byName(s.controls, "power_line_frequency")), "60 Hz")
  }

  // `peri set brio field_of_view=medium` and `peri fov brio set medium` reply with the same shape.
  function test_parse_set_field_of_view() {
    var r = Cam.parseSet("module   brio\nfov      set\nsent     field_of_view:1\nname     medium\nbefore   0\nreadback 1\ninfo     03\nstored   maybe\n")
    compare(r.ok, true); compare(r.name, "field_of_view"); compare(r.value, 1)
    // a write that was not needed says so and has no value to show
    compare(Cam.parseSet("module brio\nfov set\nsent none\nname wide\nbefore 0\nreadback 0\nnote already_at_that_value\n").ok, false)
  }

  function test_parse_set() {
    var r = Cam.parseSet("module   brio\nsent     brightness:160\nbefore   128\ndevice   ok\nreadback 160\nstored   maybe\n")
    compare(r.ok, true); compare(r.name, "brightness"); compare(r.value, 160)
    compare(Cam.parseSet("brio: brightness must be from 0 to 255\n").ok, false)
    compare(Cam.parseSet("").ok, false)
  }
}
