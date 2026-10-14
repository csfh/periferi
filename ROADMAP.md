# periferi roadmap

Linux control for keyboard and microphone RGB and device settings, built in C:
a daemon (`perid`) that owns the devices, a CLI (`peri`) and shell widgets as
thin clients, and later downloadable **modules** that teach the daemon about
more devices.

How we work: one small feature per turn, daemon first, CLI and UI second.
Stock firmware only. No custom flashing.

Tags: **[ready]** can start now, **[needs: x]** waits on another item,
**[research]** an unknown to resolve first, **[no]** not possible on stock firmware.

## Done

- [x] `peri setup`: udev rule (via `pkexec`) and the `perid` systemd user service
- [x] `perid` daemon, unix socket, `peri ping` and `peri list` (grouped by physical device)
- [x] `peri monitor <node>`: read-only report dump
- [x] QMK XAP transport on the Drop CTRL v2, plus `peri xap` with a read-only allowlist
- [x] `peri rgb get | set | save` through the daemon, including `color=#rrggbb`
- [x] Persistence verified: `save` survives a replug
- [x] Quickshell core (`quickshell/peri`), Omarchy widget with an on/off popup, standalone adapter
- [x] `just dev`, `just test`, `just qml-check`, `just plugin [install|remove]`

## What stock firmware cannot do

The CTRL v2 exposes one global lighting config: effect, hue, saturation,
brightness, speed, and `flags` (which LED groups are lit). So:

- [no] Per-key colors
- [no] Different colors for keys and underglow
- [no] Animations driven from the computer

Possible: all 44 built-in effects, any single color, speed, which groups are lit.

## Safety rules

- Nothing writes to hardware without the user's explicit approval of that first write.
- `peri xap` and the daemon only send allowlisted routes. Bootloader jump (`01 07`)
  and EEPROM reset (`01 09`) exist in this firmware and stay refused **even when the
  keyboard is unlocked**. One secure unlock opens every secure route.
- `peri blob <node>` is the one read-only exception to "allowlisted routes only": it sends the two read
  routes `01 05` (blob length) and `01 06` (a 32-byte chunk at a u16 offset), only to an XAP control
  interface, and never writes. Its output is binary (gzip JSON) and is inspected outside the code.
- Brightness is never sent above the module's `limits.max_brightness` (the board's own limit); the daemon
  refuses `val=` above it, clamps `color=` to it, and refuses both if the module gives no limit.
- Automation (themes, idle, schedules) uses volatile `set` only and never `save`.
  `save` writes the keyboard's EEPROM and is always a manual action.
- Unnumbered HID reports need a leading `0x00` byte on write.
- Never address a device by hidraw node number; it changes on re-enumeration.

## Next up: the device menu (pulls the module format forward from Tier 5)

The panel becomes a menu of every HID peripheral:

```
[ devices with an installed module ]   <- each row expands into its controls
[ ... ]
------------------------------------
> Available modules                    <- catalog entries not installed yet
> Devices without a module             <- connected HID devices nobody handles
```

"Installed" and "available" only mean something once a module exists, so this is
built in three small steps, daemon first:

1. **Minimal module + catalog.**
   - [x] 1a: parser vendored (`vendor/tomlc17`, pinned), `module.toml` format and loader
     (`src/module.c`, unit-tested including rejection rules), bundled catalog
     (`catalog/drop-ctrl-v2`), daemon `modules` request, `peri modules`.
   - [x] 1b: `peri use <id>` / `peri remove <id>` (user-level copy under
     `$XDG_DATA_HOME/peri/modules`, directory-fd based, never recursive), `rgb` only touches a
     device that an installed module matches on VID, PID, usage page and usage (error suggests
     `peri use <id>`), and `peri setup` installs the bundled CTRL module once, when the modules
     directory does not exist yet, so a later `peri remove` sticks.

   Format, as designed: `module.toml` (id, name, match on VID/PID/usage page,
   `engine = "xap"`), a catalog directory bundled in the repo, installs under
   `$XDG_DATA_HOME/peri/modules/<id>/`, and `peri use | remove | modules`. `rgb` is gated on an
   installed module, and `peri setup` installs the bundled CTRL module so the working widget
   does not break. Needs a small vendored TOML parser.
2. **Inventory.** All physical HID devices (grouped by USB path), read from sysfs only;
   a node is never opened to describe it.
   - [x] 2a: the scan covers every vendor; FIDO (any interface on usage page `0xF1D0`) and
     camera (a video-class sibling interface) are classified **per physical device** and hidden
     from `peri list` (`--all` shows them, labelled). `peri monitor` refuses security keys,
     `peri xap` also needs an `ff51:0058` control interface, and no module can match a hidden device.
     Real YubiKey, BRIO, Seiren and CTRL descriptors are test fixtures.
   - [x] 2b: non-USB HID is grouped by `HID_PHYS` + `HID_UNIQ` (USB unchanged); the daemon's
     `devices` request gives each physical device a status (installed / available / none) from
     module matching on any of its interfaces; `peri list` shows it, `peri list --json` emits it
     (`group`, `vid`, `pid`, `name`, `status`, `module`, `hidden`; hidden devices only with `--all`).
3. **The menu.** Core plus the Omarchy adapter; the CTRL row expands into the existing lighting controls.
   - [x] 3a: data layer, no UI. `peri modules --json`; `quickshell/peri/menu.js` (pure: sorts devices and
     modules into managed / installable / unmanaged, unplugged-but-installed modules stay reachable
     so they can be removed) with QML tests in ctest; `PeriDevices.qml` (generic core: `managed`,
     `installable`, `unmanaged`, `refresh()`, `install(id)`, `remove(id)`); a headless live dump
     (`quickshell -p tests/qml/live`).
   - [x] 3b: the Omarchy panel (`adapters/omarchy/Lighting.qml`): Devices (each row opens to the lighting
     toggle, a `peri setup` hint and Remove), expandable Available modules (Install) and Devices
     without a module. Install/Remove are dimmed while busy; errors show in the panel. Confirmed on
     the real shell (it caches compiled QML: after changing the widget, `omarchy restart shell`).
     Known limit: the lighting toggle shows for any connected managed device while the keyboard answers;
     it should follow a module capability once `peri modules --json` carries capabilities.
   - [x] 3c: `src/udev.c` generates the rule from installed modules' vid:pid (numbers only);
     `peri setup` installs it only when it differs (`--no-udev` skips the admin step, and `just reload`
     / `just dev` use it); `peri modules` shows `access` (`ok` / `setup`); the panel shows the hint.
     Installed with `peri setup`; this narrows device access to installed modules'
     devices: after the next replug or reboot (existing ACLs may linger until then), `peri monitor` on a
     Seiren or DeathAdder is refused until a module for them is installed (the Seiren's is in the catalog
     and has no controls yet; the DeathAdder has none).

Rules for this work:
- The catalog holds only modules that really exist. Today that is the CTRL v2 (XAP); with it
  installed, "Available modules" is empty. No invented entries.
- Security keys (FIDO, usage page `0xF1D0`) are hidden by default and `peri monitor` refuses them (done in 2a).
- The menu's Install is a user-level copy only. A module needing a new udev match tells the
  user to run `peri setup` in a terminal (no `pkexec` from a click). The udev rule is generated
  from installed modules' match fields.

## Drop CTRL v2: what can be configured (stock firmware, XAP 0.3.0)

Read from the board itself (2026-10-13): lighting capabilities `0x12` and rgbmatrix capabilities
`0x3e`. Read as one bit per sub-route, that means only the RGB matrix exists (no backlight, RGB-light or
audio) with all five routes present; this agrees with all three masks. The board's configuration blob
(`peri blob <node>`, gzip JSON, 1615 bytes) says `rgb_matrix.max_brightness = 191`, a 119-LED layout, and
a secure unlock sequence of matrix position `[0,0]` (the Esc key, 5 s timeout, 60 s idle timeout).
The blob is gzip JSON; for now it is read with `peri blob <node> > file` and inspected outside the code.

**On the board** (volatile unless saved):

| Setting | Range | Notes |
|---|---|---|
| Lighting on/off | 0 / 1 | done |
| Effect | 44 built-ins, ids `0x00`-`0x2B` | list read from the enabled-effects mask |
| Color | hue and saturation, 0-255 | swatches send hue/sat only, never brightness |
| Brightness | 0 to **191** | the cap is the board's own; the daemon refuses more |
| Speed | 0-255 | no per-effect table of which effects ignore it |
| LED groups | 4 flag bits: 1 modifiers (29 LEDs), 2 underglow (32), 4 keys (56), 8 indicators (2, shared with modifiers) | the layout agrees with the QMK convention. Watched on the board 2026-10-13: 4 = the key LEDs, 1 = the modifier keys, 8 = no visible change (its 2 LEDs are modifiers too), 2 = flag flips as expected, what went dark not described |
| Save to keyboard | | writes EEPROM, explicit confirmation only, never from a slider or preset |

**Read-only:** XAP and QMK versions, board ids, manufacturer, product, hardware id, keymap (8 layers, keycodes;
layout from the blob).

**Locked, never sent:** key remapping, bootloader, EEPROM reset (secure routes).

**Impossible on stock firmware:** per-key or per-zone colors, host-driven animations, audio, plain backlight, macros.

**Host-side** (peri manages, not the board): presets, re-apply after replug or resume, lights off on lock or
idle, follow the theme accent, schedules. These wait on the Tier 2 foundations.

## Tier 1: widget and CLI polish

Each is one small turn. Use the standalone adapter for fast iteration, since
Omarchy's shell caches plugin code (changes to a loaded widget need `omarchy restart shell`).

The panel is a larger two-pane window like Omarchy's own power and network panels (a `Panel` with a
`KeyboardPanel`; `PanelContent.qml` holds the panes): devices on the left, the selected device's
controls on the right with no folds. It replaced a small popup that crammed everything together.
Built in the panel. Guided hardware test run 2026-10-13 (volatile only, no Save, restored from a snapshot afterwards): brightness 50% read 96/191 with colour and speed untouched; colour picker moved hue/sat and left brightness alone; speed slider value equalled the keyboard's readback (184); effect click changed only the effect; LED chips flipped exactly their own bit (0xfb, 0xfe, 0xfd, 0xf7) and the last group refused to switch off (0xf4):

- [ ] Brightness slider: 0-100% of the board's own limit (191, from its module); the daemon refuses more, `color=` is clamped to it  **[built]**
- [ ] Effect list: the firmware's own 44 effects (`peri rgb effects`)  **[built]**
- [ ] Color: swatches, hue and saturation sliders; they never touch brightness  **[built]**
- [ ] Speed slider  **[built]**
- [ ] LED groups: four switches that flip one bit of what the board holds; the last lit group cannot be switched off. The board's LED layout agrees with the QMK bit meanings (1 modifiers, 2 underglow, 4 keys, 8 indicators); watched on the board: keys, modifier keys, indicators = no visible change; underglow not described  **[verified on hardware, 2026-10-13]**
- [x] "Save to keyboard": removed from the panel on request (2026-10-13). `peri rgb save` and `PeriLighting.save()` remain; persistence is better done by the daemon re-applying a stored look on plug-in (Tier 2/3)
- [ ] Writes are coalesced: while one `peri rgb set` runs, later ones merge (last value per key wins), so a dragged slider ends where it was released  **[built, tested]**
- [ ] `peri modules` shows the catalog's version even when the installed copy is older, so a stale module (for example one without `limits.max_brightness`) looks current. Add an "update available" state and show it in the panel  **[ready]**
- [ ] Read-only board info in the panel (XAP and QMK versions, product, hardware id), via the existing allowlisted queries  **[ready]**
- [ ] Keymap viewer: layout from the board's blob (119 LEDs, `LAYOUT_tkl_ansi`), 8 layers, keycodes  **[needs: a gzip and JSON reader in C; zlib]**

## Tier 2: daemon foundations

Prerequisite for all automation.

- [ ] Stable device identity (not hidraw numbers); more than one device  **[ready]**
- [ ] Event loop and hotplug; no blocking up to 5 s per request  **[ready]**
- [ ] Re-apply lighting after replug or resume  **[needs: identity, hotplug]**
- [ ] Test whether volatile settings survive suspend  **[research]**
- [ ] Presets in `~/.config/peri` (`peri preset save|apply|list`)  **[needs: identity]**

## Tier 3: automation (volatile only)

- [ ] Lights off on lock, idle or suspend; restored afterward  **[needs: hotplug, re-apply]**
- [ ] Keyboard follows the Omarchy theme accent through the `theme-set` hook. `~/.config/omarchy/hooks/theme-set` already exists with another tool's managed block: read it first and install through `omarchy hook install`, no clobbering  **[needs: presets]**
- [ ] Time-of-day schedule  **[needs: presets]**

## Tier 4: keymap

- [ ] Read-only keymap viewer (8 layers; routes `04 02` and `04 03` confirmed on this board)  **[ready]**
- [ ] Config blob reader (1,615 bytes, route `01 06` with an offset) to draw the keyboard  **[ready; needs allowlist entry for payload routes]**
- [ ] Live remapping  **[research]**: needs a physical unlock sequence that is not identified yet; also query `05 01` for the remapping capabilities. The bootloader/EEPROM refusal above must stay in force.

## Tier 5: modules and distribution

- [ ] TOML module format: match (VID, PID, usage page), transport, capabilities, verbs, optional `.so` escape hatch. Design it against the CTRL's XAP routes as the first real example  **[ready]**
- [ ] `peri use / update / remove` against a local index first; ed25519-signed manifests and a lockfile later  **[needs: module format]**
- [ ] Arch packaging: system udev rule, user unit, release build  **[ready]**
- [ ] Omarchy plugin as its own git repo so `omarchy plugin add <url>` works (manifest must be at the repo root)  **[ready]**
- [ ] More XAP keyboards. Honest note: XAP lives on a QMK branch and few boards ship it, so this is a small pool  **[research]**

## Tier 6: other hardware (expensive, unproven)

- [ ] Razer Seiren V3 Chroma: no official docs; only unmerged OpenRazer PRs (lighting only), so we wrote our own `razer` engine (frame codec, feature-report transport over hidraw). Done: the module is in the catalog, `peri status <module-id>` reads firmware (`0101`, meaning unproven) and brightness (0-255) through the daemon, both verified on the real mic and kept as test vectors. Its controls report on hidraw: `05 11 xx` is the tap-to-mute state (`00` muted, `01` unmuted; ALSA's `Mic Capture Switch` read off while muted, one reading), `05 0f NN` is the dial level (`0x14`-`0x64`). `peri set <module-id> brightness=N` works (verified 2026-10-14, twice: 102 to 60 to 102, readbacks matched and the owner saw the light dim; the storage byte 0 did not keep the change live-only, the stored value followed, so each set may wear the device's memory and a slider must not drive it). The mic cannot be asked for its current effect or colour: the reference's standard-LED getters (class 03, commands 81 and 82) get no answer from it (an idle 53-byte report), and the extended matrix class has no effect getter. So a colour or effect test cannot be restored from a readback; it needs the owner to say what the light is doing now. `peri set <module-id> effect=<none|static|breathing|spectrum> [color=rrggbb]` works (static blue, then breathing with random colours as the restore; the owner confirmed it worked as expected). Not tried yet: spectrum, one-colour breathing, `none`. `peri status` also reports `mute=muted|unmuted|unknown` and `dial=<level>|unknown`, followed live by the daemon from the mic's own reports (verified 2026-10-14 with a timed log: two taps flipped it, the dial moved 33 to 63 and back, an unplug read as not connected, and the replug re-attached within seconds with the state back to unknown). The mic pane is in the panel. It edits two saved looks, Unmuted and Muted (an effect, a colour for static, a brightness), kept by the daemon in `~/.config/peri/state/<module-id>.toml` because the mic cannot report any of it: `peri look <module-id> get|save|apply <unmuted|muted>`. Editing only saves; the Apply button is the only thing that writes to the mic, and nothing applies a look on a tap. The highlights are the saved settings, not what the mic shows. Also read through the sound card (`peri audio`: gain, monitor mix, headphones), and more effects (wave, two-colour breathing) built from the public reference but not yet seen working on the real mic. Starlight (effect 7 in that reference) and Wheel (effect 0x0a) were tried and the mic shows no colours for them, so both were removed. What is NOT known: the Mute Effect colour, the Peaking and volume indicators, tap remapping and the audio effects (noise gate, compressor, EQ, ...); `peri decode --scan` and `docs/capturing-synapse.md` turn a Windows USB capture of Synapse into the list of commands. Software-driven effects (Audio meter, Fire) are not built. Control interface `ff53:0004`, report ID 7, 64-byte feature report  **[research]**
- [ ] Razer DeathAdder V3 Pro (`1532:00c3`): unknown, left alone for now  **[research]**

## Housekeeping

- [ ] The CLI (shell environment) and the daemon (systemd's) may resolve `$XDG_DATA_HOME` differently. Setup now installs and checks in one process, so it no longer loops, but a module the CLI installs could be invisible to the daemon, and the udev rule is built from the CLI's view. Pin both to one data dir (for example by passing it to the unit)  **[ready]**
- [ ] More unit tests (only the color conversion has one) and a mock transport so features can be tested without the keyboard
- [ ] Daemon logging
- [ ] License and contribution files (not decided)
