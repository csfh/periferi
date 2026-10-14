import QtQuick
import QtTest
import "../../quickshell/peri/mic.js" as Mic

TestCase {
  name: "mic"

  // What `peri status` prints for a connected mic (bytes copied from the real device).
  readonly property string full: "module   razer-seiren-v3-chroma\nfirmware 0101\nbrightness 102\nmute     muted\ndial     43\n"

  // --- reading `peri status` ------------------------------------------------------------
  function test_parse_status() {
    var s = Mic.parseStatus(full)
    compare(s.online, true)
    compare(s.brightness, 102)
    compare(s.mute, "muted")
    compare(s.dial, 43)
    compare(s.firmware, "0101")
  }

  function test_unknown_mute_and_dial() {
    var s = Mic.parseStatus("module   m\nfirmware 0101\nbrightness 102\nmute     unknown\ndial     unknown\n")
    compare(s.online, true)
    compare(s.mute, "unknown")
    compare(s.dial, null)
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
    compare(Mic.parseStatus("").mute, "unknown")
  }

  function test_odd_mute_and_dial_values_are_unknown() {
    var s = Mic.parseStatus("module m\nbrightness 5\nmute loud\ndial 4x\n")
    compare(s.mute, "unknown")
    compare(s.dial, null)
  }

  // --- brightness: a percentage of the protocol's 0 to 255 -------------------------------
  function test_brightness_percent_round_trips_the_real_value() {
    compare(Mic.brightnessPercent(102), 40)
    compare(Mic.brightnessPercent(0), 0)
    compare(Mic.brightnessPercent(255), 100)
    compare(Mic.brightnessSetting(40, 0), "brightness=102")   // 40 percent is exactly 102
    compare(Mic.brightnessSetting(100, 0), "brightness=255")
    compare(Mic.brightnessSetting(0, 5), "brightness=0")
  }

  // A release that changes nothing sends nothing: each set may wear the device's memory.
  function test_an_unchanged_value_is_never_sent() {
    compare(Mic.brightnessSetting(40, 102), null)
    compare(Mic.brightnessSetting(100, 255), null)
    compare(Mic.brightnessSetting(41, 102), "brightness=105")
  }

  function test_brightness_is_clamped() {
    compare(Mic.brightnessSetting(150, 0), "brightness=255")
    compare(Mic.brightnessSetting(-20, 7), "brightness=0")
    compare(Mic.brightnessSetting("junk", 7), "brightness=0")
  }

  // --- effects: buttons with no "current" one -------------------------------------------
  function test_effect_settings() {
    compare(Mic.effectSettings("static", "0000ff"), ["effect=static", "color=0000ff"])
    compare(Mic.effectSettings("static", "FF00AA"), ["effect=static", "color=ff00aa"])
    compare(Mic.effectSettings("breathing", ""), ["effect=breathing"])
    compare(Mic.effectSettings("spectrum", "0000ff"), ["effect=spectrum"]) // a colour is ignored
    compare(Mic.effectSettings("none", undefined), ["effect=none"])
  }

  function test_bad_effects_are_not_sent() {
    compare(Mic.effectSettings("static", ""), null)
    compare(Mic.effectSettings("static", "00ff"), null)
    compare(Mic.effectSettings("static", "zz0000"), null)
    compare(Mic.effectSettings("static", "0000ff; rm"), null)
    compare(Mic.effectSettings("rainbow", "0000ff"), null)
    compare(Mic.effectSettings("", ""), null)
  }

  // Every preset must be a colour the daemon accepts, and every button one it knows.
  function test_presets_and_effects_are_valid() {
    Mic.presets.forEach(function(p) { verify(Mic.validHex(p.hex), p.name) })
    compare(Mic.presets.length, 8)
    compare(Mic.effects.map(function(e) { return e.id }), ["static", "breathing", "spectrum", "none"])
    Mic.effects.forEach(function(e) {
      verify(Mic.effectSettings(e.id, "0000ff") !== null, e.id)
    })
  }

  // --- texts ------------------------------------------------------------------------------
  function test_texts() {
    compare(Mic.muteText("muted"), "Muted")
    compare(Mic.muteText("unmuted"), "Not muted")
    verify(Mic.muteText("unknown").indexOf("unknown") >= 0)
    compare(Mic.dialText(43), "Dial 43")
    compare(Mic.dialText(0), "Dial 0")
    compare(Mic.dialText(null), "Dial not turned yet")
  }
}
