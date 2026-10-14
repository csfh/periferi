import QtQuick
import QtTest
import "../../adapters/omarchy/centering.js" as C

TestCase {
  name: "center"

  // The panel's own placement, copied from Ui/KeyboardPanel.qml: top bar y = barH + gap,
  // bottom bar y = screenH - barH - contentH - gap.
  function topY(screenH, barH, contentH) { return barH + C.centerGap(screenH, contentH, barH) }
  function bottomY(screenH, barH, contentH) {
    return screenH - barH - contentH - C.centerGap(screenH, contentH, barH)
  }

  function test_top_bar_card_is_centred() {
    compare(topY(1440, 40, 600), 420)         // (1440 - 600) / 2
    compare(topY(1080, 30, 501), 290)         // odd leftover rounds, off by at most 1px
  }
  function test_bottom_bar_card_is_centred() {
    compare(bottomY(1440, 40, 600), 420)
  }
  function test_gap_never_negative() {
    compare(C.centerGap(500, 600, 40), 0)     // taller than the screen
    compare(C.centerGap(NaN, 600, 40), 0)
  }
  function test_limit_keeps_the_card_clear_of_the_bar_and_margins() {
    compare(C.limit(1200, 1920, 0, 12), 1200)       // fits as asked
    compare(C.limit(1200, 1000, 0, 12), 976)        // 1000 - 2 * 12
    compare(C.limit(900, 800, 40, 12), 696)         // 800 - 2 * (40 + 12)
  }
  function test_limit_with_no_room_keeps_what_was_asked() {
    compare(C.limit(300, 50, 40, 12), 300)          // a nonsense screen must not give a zero card
    compare(C.limit(0, 1920, 0, 12), 1)
  }
}
