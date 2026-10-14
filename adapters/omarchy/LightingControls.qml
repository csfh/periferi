import QtQuick
import qs.Commons
import qs.Ui

// The keyboard's lighting controls, in Omarchy's look, laid out for a wide pane: switches,
// brightness, speed, color and LED groups on the left; the effect list on the right.
// No logic of its own: every value comes from, and every change goes to, the generic core
// (PeriLighting, passed in as `peri`), which does the clamping, bit-flipping and write
// coalescing and is tested on its own.
Column {
  id: root

  required property var peri
  property QtObject bar: null
  property string moduleId: ""     // the module driving this keyboard, for the update hint

  spacing: Style.space(10)

  component Caption: Text {
    width: parent ? parent.width : implicitWidth
    wrapMode: Text.WordWrap
    color: Color.muted
    font.family: Style.font.family
    font.pixelSize: Style.font.caption
  }

  component Label: Text {
    color: Color.foreground
    font.family: Style.font.family
    font.pixelSize: Style.font.body
  }

  Row {
    id: body
    width: parent.width
    spacing: Style.space(24)

    // --- left: on/off, brightness, speed, color -----------------------------------------------
    Column {
      id: leftCol
      width: (body.width - body.spacing) / 2
      spacing: Style.space(8)

      Toggle {
        width: parent.width
        label: "Lighting"
        description: root.peri.lit ? root.peri.effect.replace(/_/g, " ") : "Off"
        checked: root.peri.lit
        onClicked: root.peri.set(["enable=" + (root.peri.lit ? "0" : "1")])
      }

      // A percentage of the keyboard's own limit, never of 255.
      Label { text: "Brightness  " + root.peri.brightness + "%" }
      PanelSlider {
        width: parent.width
        bar: root.bar
        minimum: 0
        maximum: 100
        step: 1
        integer: true
        enabled: root.peri.canBrightness
        opacity: enabled ? 1 : 0.4
        value: root.peri.brightness
        onMoved: function(v) { root.peri.setBrightness(v) }
        onReleased: function(v) { root.peri.setBrightness(v) }
      }
      Caption {
        visible: !root.peri.canBrightness
        text: "Brightness is off until the module is updated: run `peri use " + root.moduleId + "`."
      }

      Label { text: "Speed  " + root.peri.speed }
      PanelSlider {
        width: parent.width
        bar: root.bar
        minimum: 0
        maximum: 255
        step: 1
        integer: true
        value: root.peri.speed
        onMoved: function(v) { root.peri.set(["speed=" + Math.round(v)]) }
        onReleased: function(v) { root.peri.set(["speed=" + Math.round(v)]) }
      }

      // Color sets hue and saturation only. It never touches brightness.
      Label { text: "Color" }
      HuePlane {
        width: parent.width
        hue: root.peri.hue
        sat: root.peri.sat
        onPicked: function(h, s) { root.peri.set(["hue=" + h, "sat=" + s]) }
      }
      Caption { text: "Hue " + root.peri.hue + "  ·  Saturation " + root.peri.sat }
      Caption { text: "Some effects ignore the color, and rainbow-style ones set their own." }
      // LED groups: [bit, label]. From the board's own LED layout: 29 modifier, 32 underglow,
      // 56 key LEDs, and 2 indicators that are also modifiers.
      Label { text: "LED groups" }
      Row {
        id: groupRow
        width: parent.width
        spacing: Style.space(6)

        Repeater {
          model: [[4, "Keys"], [1, "Modifiers"], [2, "Underglow"], [8, "Indicators"]]
          delegate: Button {
            required property var modelData
            width: (groupRow.width - groupRow.spacing * 3) / 4
            selected: (root.peri.flags & modelData[0]) !== 0
            text: modelData[1]
            onClicked: root.peri.setGroup(modelData[0], !selected)
          }
        }
      }
      Caption {
        text: "Lit groups are highlighted. Indicators shows no change on this keyboard: its two "
          + "LEDs are modifiers too. The last group cannot be switched off here; use the Lighting "
          + "switch."
      }
    }

    // --- right: effects --------------------------------------------------------
    Column {
      id: rightCol
      width: leftCol.width
      spacing: Style.space(8)

      Label { id: effectLabel; text: "Effect" }
      ListView {
        width: parent.width
        // As tall as the left column, so the two sides line up and the list shows as much as fits.
        height: Math.max(Style.space(240), leftCol.height - effectLabel.height - rightCol.spacing)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        model: root.peri.effects
        // Start with the current effect in view, not at the top of 44.
        function showCurrent() {
          for (var i = 0; i < root.peri.effects.length; i++)
            if (root.peri.effects[i].name === root.peri.effect) { positionViewAtIndex(i, ListView.Center); return }
        }
        onCountChanged: showCurrent()
        Component.onCompleted: showCurrent()
        delegate: Button {
          required property var modelData
          width: ListView.view.width
          leftAlign: true
          selected: modelData.name === root.peri.effect
          text: modelData.label
          onClicked: root.peri.setEffect(modelData.name)
        }
      }
    }
  }

  Text {
    visible: root.peri.error !== ""
    width: parent.width
    wrapMode: Text.WordWrap
    text: root.peri.error
    color: Color.urgent
    font.family: Style.font.family
    font.pixelSize: Style.font.caption
  }
}
