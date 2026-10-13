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
     Seiren or DeathAdder is refused until a module for them exists.

Rules for this work:
- The catalog holds only modules that really exist. Today that is the CTRL v2 (XAP); with it
  installed, "Available modules" is empty. No invented entries.
- Security keys (FIDO, usage page `0xF1D0`) are hidden by default and `peri monitor` refuses them (done in 2a).
- The menu's Install is a user-level copy only. A module needing a new udev match tells the
  user to run `peri setup` in a terminal (no `pkexec` from a click). The udev rule is generated
  from installed modules' match fields.

## Tier 1: widget and CLI polish

Each is one small turn. Use the standalone adapter for fast iteration, since
Omarchy's shell caches plugin code (changes to a loaded widget need `omarchy restart shell`).

- [ ] Brightness slider; top of the slider maps to the board's real cap (read it back, do not hard-code)  **[ready]**
- [ ] Effect picker; `peri rgb effects` lists names  **[ready]**
- [ ] Color picker (hue and saturation) plus a few swatches  **[ready]**
- [ ] Speed slider  **[ready]**
- [ ] LED-group toggles (keys, underglow, modifiers, indicators). Bit meanings are QMK convention, so confirm each by watching the keyboard  **[ready]**
- [ ] "Save to keyboard" button with confirmation  **[ready]**

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

- [ ] Razer Seiren V3 Chroma: no documented protocol. Control interface `ff53:0004`, report ID 7, 63-byte feature report. Realistic route is capturing Razer's own software traffic (Windows VM with USB capture). Cheap first step: `peri monitor` on its button reports  **[research]**
- [ ] Razer DeathAdder V3 Pro (`1532:00c3`): unknown, left alone for now  **[research]**

## Housekeeping

- [ ] The CLI (shell environment) and the daemon (systemd's) may resolve `$XDG_DATA_HOME` differently. Setup now installs and checks in one process, so it no longer loops, but a module the CLI installs could be invisible to the daemon, and the udev rule is built from the CLI's view. Pin both to one data dir (for example by passing it to the unit)  **[ready]**
- [ ] More unit tests (only the color conversion has one) and a mock transport so features can be tested without the keyboard
- [ ] Daemon logging
- [ ] License and contribution files (not decided)
