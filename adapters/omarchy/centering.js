.pragma library

// Centres Omarchy's KeyboardPanel on the screen, both ways, using only its public properties.
// The panel puts its card `gap` away from the bar edge (and, with centerOnBar, centred along
// the bar). So a gap of half the leftover space, minus the bar, puts the card in the middle
// across the bar's axis too. It works the same for a top or bottom bar (heights) and a left or
// right bar (widths).

// The gap that centres a card of `content` size on a screen of `screen` size, with a bar of
// `bar` size on one edge of that axis. Never negative: the panel clamps to the screen anyway.
function centerGap(screen, content, bar) {
  var gap = Math.round((Number(screen) - Number(content)) / 2 - Number(bar))
  return isFinite(gap) ? Math.max(0, gap) : 0
}

// How large the card may be on one axis: what it wants, but not more than fits with the margin
// on both sides (and the bar's size on both sides too, on the bar's own axis, so the centred
// card never runs under the bar). Computed here rather than with the panel's own
// fittedContent*(), which read `gap` and would loop with centerGap.
function limit(desired, screen, bar, margin) {
  var want = Number(desired)
  var room = Number(screen) - 2 * (Number(bar) + Number(margin))
  var size = Math.round(room > 0 ? Math.min(want, room) : want)
  return isFinite(size) ? Math.max(1, size) : 1
}
