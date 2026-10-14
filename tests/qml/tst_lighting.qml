import QtQuick
import QtTest
import "../../quickshell/peri/lighting.js" as L

TestCase {
  name: "lighting"

  // --- brightness: a percentage of the keyboard's own limit, never of 255 ---------------
  function test_percent_to_val_uses_the_limit() {
    compare(L.percentToVal(100, 191), 191)
    compare(L.percentToVal(0, 191), 0)
    compare(L.percentToVal(50, 191), 96)   // 95.5 rounds up
    compare(L.percentToVal(100, 255), 255)
  }
  function test_percent_to_val_never_exceeds_the_limit() {
    compare(L.percentToVal(150, 191), 191)
    compare(L.percentToVal(-20, 191), 0)
    compare(L.percentToVal(NaN, 191), 0)
    compare(L.percentToVal(100, 0), 0)     // no known limit: never send a brightness
    compare(L.percentToVal(100, -5), 0)
  }
  function test_val_to_percent() {
    compare(L.valToPercent(191, 191), 100)
    compare(L.valToPercent(143, 191), 75)
    compare(L.valToPercent(0, 191), 0)
    compare(L.valToPercent(250, 191), 100) // above the limit still reads as full
    compare(L.valToPercent(143, 0), 0)     // no limit known
  }
  function test_percent_round_trip_stays_inside_the_limit() {
    for (var p = 0; p <= 100; p++) {
      var v = L.percentToVal(p, 191)
      verify(v >= 0 && v <= 191)
      verify(Math.abs(L.valToPercent(v, 191) - p) <= 1)
    }
  }

  // --- hue/saturation plane: hue left to right, saturation bottom to top ----------------
  function test_plane_corners_and_center() {
    compare(L.planeToHueSat(0, 0, 200, 100).hue, 0)
    compare(L.planeToHueSat(0, 0, 200, 100).sat, 255)     // top is fully saturated
    compare(L.planeToHueSat(200, 100, 200, 100).hue, 255)
    compare(L.planeToHueSat(200, 100, 200, 100).sat, 0)   // bottom is white
    compare(L.planeToHueSat(100, 50, 200, 100).hue, 128)
  }
  function test_plane_snaps_points_outside_to_the_edge() {
    var p = L.planeToHueSat(-30, 400, 200, 100)
    compare(p.hue, 0)
    compare(p.sat, 0)
    p = L.planeToHueSat(900, -5, 200, 100)
    compare(p.hue, 255)
    compare(p.sat, 255)
  }
  function test_plane_with_no_size_gives_zero_not_nan() {
    compare(L.planeToHueSat(10, 10, 0, 0).hue, 0)
    compare(L.planeToHueSat(NaN, NaN, 200, 100).hue, 0)
  }
  function test_plane_round_trip() {
    for (var h = 0; h <= 255; h += 5)
      for (var s = 0; s <= 255; s += 5) {
        var at = L.hueSatToPlane(h, s, 510, 255)
        var back = L.planeToHueSat(at.x, at.y, 510, 255)
        compare(back.hue, h)
        compare(back.sat, s)
      }
  }
  function test_marker_position_clamps_wild_values() {
    var at = L.hueSatToPlane(999, -4, 200, 100)
    compare(at.x, 200)
    compare(at.y, 100)
  }

  // --- LED groups: flip one bit of what the board holds ---------------------------------
  function test_set_flag_keeps_the_other_bits() {
    compare(L.setFlag(0xff, 4, false), 0xfb)     // not 0x0b or 0x0f: the high bits survive
    compare(L.setFlag(0xfb, 4, true), 0xff)
    compare(L.setFlag(0x00, 2, true), 0x02)
    compare(L.setFlag(0x0f, 1, false), 0x0e)
    compare(L.setFlag(0xff, 8, true), 0xff)      // already set
  }
  function test_groups_lit_counts_only_the_four_groups() {
    compare(L.groupsLit(0xff), true)
    compare(L.groupsLit(0xf0), false)            // only high bits: nothing is lit
    compare(L.groupsLit(0x00), false)
    compare(L.groupsLit(0x04), true)
  }
  // Turning off the last lit group would put the keyboard dark; the toggle is for that.
  function test_flag_change_refuses_to_go_dark() {
    compare(L.flagChange(0xff, 4, false), 0xfb)
    compare(L.flagChange(0x04, 4, false), null)  // the last group
    compare(L.flagChange(0xf4, 4, false), null)  // high bits do not count as lit
    compare(L.flagChange(0x00, 4, true), 0x04)
    compare(L.flagChange(0x03, 4, true), 0x07)
  }

  // --- coalescing: while a set is running, later ones merge, last value wins ------------
  function test_merge_last_value_wins_per_key() {
    compare(L.mergeSettings([], ["val=10"]), ["val=10"])
    compare(L.mergeSettings(["val=10"], ["val=20"]), ["val=20"])
    compare(L.mergeSettings(["val=10", "hue=5"], ["val=20"]), ["hue=5", "val=20"])
  }
  function test_merge_combines_different_controls() {
    var m = L.mergeSettings(["hue=5"], ["sat=7", "speed=9"])
    compare(m.length, 3)
    verify(m.indexOf("hue=5") >= 0 && m.indexOf("sat=7") >= 0 && m.indexOf("speed=9") >= 0)
  }
  function test_merge_does_not_modify_its_inputs() {
    var a = ["val=1"], b = ["val=2"]
    L.mergeSettings(a, b)
    compare(a, ["val=1"])
    compare(b, ["val=2"])
  }
  function test_merge_ignores_junk() {
    compare(L.mergeSettings(null, ["val=3"]), ["val=3"])
    compare(L.mergeSettings(["val=3"], null), ["val=3"])
    compare(L.mergeSettings([], ["", "novalue"]), [])
  }
  function test_merge_color_is_its_own_key() {
    // color= sets hue, sat and val on the daemon side; a later hue= must still go after it
    compare(L.mergeSettings(["color=#ff0000"], ["hue=9"]), ["color=#ff0000", "hue=9"])
  }

  // A save is refused, visibly, while a change is still running: the confirm row stays open.
  function test_can_save_only_when_nothing_is_in_flight() {
    compare(L.canSave(false, false), true)
    compare(L.canSave(true, false), false)   // a coalesced set is still running
    compare(L.canSave(false, true), false)   // a save is already running
    compare(L.canSave(true, true), false)
  }

  // --- the daemon's replies --------------------------------------------------------------
  function test_parse_values() {
    var v = L.parseValues("enabled  yes\neffect   solid_color\nmode     0x00\nhue      48\nmaxval   191\n")
    compare(v.enabled, "yes")
    compare(v.hue, "48")
    compare(v.maxval, "191")
    compare(v.effect, "solid_color")
    compare(Object.keys(L.parseValues("")).length, 0)
    compare(Object.keys(L.parseValues("garbage\n\n")).length, 0)
  }
  function test_parse_effects() {
    var e = L.parseEffects("effects  solid_color,band_pinwheel_sat,rainbow_beacon\n")
    compare(e.length, 3)
    compare(e[0].name, "solid_color")
    compare(e[0].label, "Solid color")
    compare(e[1].label, "Band pinwheel sat")
    compare(L.parseEffects("effects  a,,b,\n").length, 2)   // stray commas make no blank effect
    compare(L.parseEffects("").length, 0)
    compare(L.parseEffects("effects  \n").length, 0)
    compare(L.parseEffects("err something\n").length, 0)
  }
}
