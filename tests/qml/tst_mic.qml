import QtQuick
import QtTest
import "../../quickshell/peri/mic.js" as Mic

TestCase {
  name: "mic"

  // What `peri status` and `peri look ... get` print (shapes copied from the real daemon).
  readonly property string status: "module   razer-seiren-v3-chroma\nfirmware 0101\nbrightness 102\nmute     muted\ndial     43\n"
  function lookText(effect, color, brightness) {
    return "module   razer-seiren-v3-chroma\nlook     unmuted\neffect   " + effect
      + "\ncolor    " + color + "\nbrightness " + brightness + "\n"
  }
  function look(effect, color, brightness) {
    return { effect: effect, color: color, brightness: brightness }
  }

  // --- reading `peri status`: online, and the mic's own brightness ---------------------------
  function test_parse_status() {
    var s = Mic.parseStatus(status)
    compare(s.online, true)
    compare(s.brightness, 102)
  }

  // A refusal or nothing at all means offline: a pane must never show stale numbers as live.
  function test_offline_when_not_a_full_answer() {
    compare(Mic.parseStatus("").online, false)
    compare(Mic.parseStatus("razer-seiren-v3-chroma: the device is not connected\n").online, false)
    compare(Mic.parseStatus("module m\nfirmware 0101\n").online, false)           // no brightness
    compare(Mic.parseStatus("module m\nbrightness 300\n").online, false)           // out of range
    compare(Mic.parseStatus("module m\nbrightness abc\n").online, false)
    compare(Mic.parseStatus("brightness 10\n").online, false)                       // no module
    compare(Mic.parseStatus(undefined).online, false)
  }

  // --- reading a saved look ------------------------------------------------------------------
  function test_parse_look() {
    compare(Mic.parseLook(lookText("static", "0000FF", "102")), look("static", "0000ff", 102))
    compare(Mic.parseLook(lookText("breathing", "none", "40")), look("breathing", "", 40))
    compare(Mic.parseLook(lookText("none", "none", "0")), look("none", "", 0)) // Off is a real effect
    compare(Mic.parseLook(lookText("unset", "none", "unset")), look("", "", -1))
    compare(Mic.parseLook(lookText("spectrum", "none", "unset")), look("spectrum", "", -1))
  }

  // Bad or missing fields spoil only themselves, and a failed read is not an empty look.
  function test_parse_look_bad_fields() {
    compare(Mic.parseLook(lookText("rainbow", "none", "70")), look("", "", 70))
    compare(Mic.parseLook(lookText("static", "none", "70")), look("", "", 70))    // static needs a colour
    compare(Mic.parseLook(lookText("static", "zz", "70")), look("", "", 70))
    compare(Mic.parseLook(lookText("breathing", "0000ff", "70")), look("breathing", "", 70)) // stray colour
    compare(Mic.parseLook(lookText("none", "none", "300")), look("none", "", -1))
    compare(Mic.parseLook(lookText("none", "none", "abc")), look("none", "", -1))
    compare(Mic.parseLook(""), null)
    compare(Mic.parseLook("razer-seiren-v3-chroma: the static effect needs a color=<rrggbb>\n"), null)
    compare(Mic.parseLook("module m\n"), null)                                      // no look line
    compare(Mic.parseLook(undefined), null)
  }

  // --- editing a look: whole-look results, never merged on the daemon -----------------------
  function test_with_effect() {
    var base = look("breathing", "", 40)
    compare(Mic.withEffect(base, "static", "0000FF"), look("static", "0000ff", 40)) // brightness is kept
    compare(Mic.withEffect(look("static", "0000ff", 40), "spectrum", "0000ff"), look("spectrum", "", 40)) // colour dropped
    compare(Mic.withEffect(base, "none", undefined), look("none", "", 40))
    compare(Mic.withEffect(look("", "", -1), "breathing", ""), look("breathing", "", -1))
  }

  function test_bad_effects_are_not_built() {
    var base = look("", "", -1)
    compare(Mic.withEffect(base, "static", ""), null)
    compare(Mic.withEffect(base, "static", "00ff"), null)
    compare(Mic.withEffect(base, "static", "zz0000"), null)
    compare(Mic.withEffect(base, "static", "0000ff; rm"), null)
    compare(Mic.withEffect(base, "rainbow", "0000ff"), null)
    compare(Mic.withEffect(base, "", ""), null)
  }

  function test_with_brightness() {
    compare(Mic.withBrightness(look("static", "0000ff", 0), 40), look("static", "0000ff", 102))
    compare(Mic.withBrightness(look("", "", -1), 100), look("", "", 255))
    compare(Mic.withBrightness(look("", "", -1), 150), look("", "", 255))   // clamped
    compare(Mic.withBrightness(look("", "", 5), -20), look("", "", 0))
    compare(Mic.withBrightness(look("", "", 5), "junk"), look("", "", 0))
  }

  // A release that does not move the value saves nothing.
  function test_an_unchanged_brightness_is_not_saved() {
    compare(Mic.withBrightness(look("none", "", 102), 40), null)
    compare(Mic.withBrightness(look("", "", 255), 100), null)
  }

  // --- the arguments of `peri look` ---------------------------------------------------------
  function test_save_args() {
    compare(Mic.saveArgs("unmuted", look("static", "0000ff", 102)),
            ["save", "unmuted", "effect=static", "color=0000ff", "brightness=102"])
    compare(Mic.saveArgs("muted", look("breathing", "", -1)), ["save", "muted", "effect=breathing"])
    compare(Mic.saveArgs("muted", look("none", "", 0)), ["save", "muted", "effect=none", "brightness=0"])
    compare(Mic.saveArgs("muted", look("", "", 40)), ["save", "muted", "brightness=40"])
    compare(Mic.saveArgs("unmuted", look("", "", -1)), ["save", "unmuted"])      // clears the look
    compare(Mic.saveArgs("muted", look("breathing", "0000ff", -1)), ["save", "muted", "effect=breathing"])
    compare(Mic.applyArgs("muted"), ["apply", "muted"])
  }

  function test_can_apply() {
    verify(Mic.canApply(look("none", "", -1)))
    verify(Mic.canApply(look("", "", 0)))
    verify(!Mic.canApply(look("", "", -1)))
    verify(!Mic.canApply(null))
  }

  // --- the highlight follows the saved look -------------------------------------------------
  function test_selection_follows_the_saved_look() {
    var spec = look("spectrum", "", -1), blue = look("static", "0000ff", 10), none = look("", "", -1)
    verify(Mic.effectSelected("spectrum", spec))
    verify(!Mic.effectSelected("breathing", spec))
    verify(!Mic.effectSelected("spectrum", none))
    verify(!Mic.effectSelected("spectrum", null))
    verify(Mic.swatchSelected("0000ff", blue))
    verify(Mic.swatchSelected("0000FF", blue))
    verify(!Mic.swatchSelected("ff0000", blue))
    verify(!Mic.swatchSelected("0000ff", spec))       // another effect: no swatch is lit
    verify(!Mic.swatchSelected("0000ff", none))
    verify(!Mic.swatchSelected("bad", blue))
  }

  // --- texts ---------------------------------------------------------------------------------
  function test_texts() {
    compare(Mic.lookText(look("", "", -1)), "Nothing saved in this look yet")
    compare(Mic.lookText(null), "Nothing saved in this look yet")
    compare(Mic.lookText(look("static", "0000ff", 102)), "Saved: colour 0000ff, brightness 40%")
    compare(Mic.lookText(look("none", "", -1)), "Saved: off")
    compare(Mic.lookText(look("", "", 255)), "Saved: brightness 100%")
    compare(Mic.lookLabel("unmuted"), "Unmuted look")
    compare(Mic.lookLabel("muted"), "Muted look")
    compare(Mic.lookLabel("other"), "")
    verify(Mic.applyText("muted").indexOf("muted look") >= 0)
    verify(Mic.applyText("muted").indexOf("whatever its mute state") >= 0)
  }

  function test_fixed_lists() {
    Mic.presets.forEach(function(p) { verify(Mic.validHex(p.hex), p.name) })
    compare(Mic.presets.length, 8)
    compare(Mic.effects.map(function(e) { return e.id }), ["static", "breathing", "spectrum", "none"])
    compare(Mic.looks.map(function(l) { return l.id }), ["unmuted", "muted"])
    Mic.effects.forEach(function(e) { verify(Mic.withEffect(look("", "", -1), e.id, "0000ff") !== null, e.id) })
  }
}
