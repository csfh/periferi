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
   - [x] 2a: the scan covers every vendor; FIDO (any interface on usage page `0xF1D0`) is
     classified **per physical device** and hidden from `peri list` (`--all` shows it, labelled).
     `peri monitor` refuses security keys, `peri xap` also needs an `ff51:0058` control interface,
     and no module can match a hidden device. Cameras were hidden the same way until 2026-10-15,
     when the owner chose to show them (so a webcam can have a module): they are ordinary devices now.
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
     Seiren or DeathAdder is refused until a module for them is installed (the Seiren's and the DeathAdder's
     are in the catalog; the panel has a pane for the DeathAdder, seen in the real shell).

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

- [ ] Razer Seiren V3 Chroma: no official docs; only unmerged OpenRazer PRs (lighting only), so we wrote our own `razer` engine (frame codec, feature-report transport over hidraw). Done: the module is in the catalog, `peri status <module-id>` reads firmware (`0101`, meaning unproven) and brightness (0-255) through the daemon, both verified on the real mic and kept as test vectors. Its controls report on hidraw: `05 11 xx` is the tap-to-mute state (`01` muted, `00` unmuted: inferred, not seen; a nine-minute recording on 2026-10-16 held room sound while the last report was `00`, and the first guess had been the opposite), `05 0f NN` is the dial level (`0x14`-`0x64`). `peri set <module-id> brightness=N` works (verified 2026-10-14, twice: 102 to 60 to 102, readbacks matched and the owner saw the light dim; the storage byte 0 did not keep the change live-only, the stored value followed, so each set may wear the device's memory and a slider must not drive it). The mic cannot be asked for its current effect or colour: the reference's standard-LED getters (class 03, commands 81 and 82) get no answer from it (an idle 53-byte report), and the extended matrix class has no effect getter. So a colour or effect test cannot be restored from a readback; it needs the owner to say what the light is doing now. `peri set <module-id> effect=<none|static|breathing|spectrum> [color=rrggbb]` works (static blue, then breathing with random colours as the restore; the owner confirmed it worked as expected). Not tried yet: spectrum, one-colour breathing, `none`. The daemon used to read those reports to apply the matching look on every tap ("Follow mute"). That was removed on 2026-10-16: `peri status` no longer has `mute=` or `dial=`, nothing watches the mic, and the firmware shows its own look while muted. The mute byte had been read backwards from one reading, which made every tap apply the wrong look. The mic pane is in the panel. It edits two saved looks, Unmuted and Muted (an effect, a colour for static, a brightness), kept by the daemon in `~/.config/peri/state/<module-id>.toml` because the mic cannot report any of it: `peri look <module-id> get|save|apply <unmuted|muted>`. Editing only saves; the Apply button is the only thing that writes to the mic, and nothing applies a look on a tap. The mic holds one look at a time and its muted look belongs to its firmware: the frame that sets the Mute Effect colour is not known, so a look saved as Muted may never show: whether an applied look is visible while the mic is muted has not been seen (next step: capture the vendor software setting it, see `docs/capturing-synapse.md`). The highlights are the saved settings, not what the mic shows. Also read through the sound card (`peri audio`: gain, monitor mix, headphones), and more effects (wave, two-colour breathing) built from the public reference but not yet seen working on the real mic. Starlight (effect 7 in that reference) and Wheel (effect 0x0a) were tried and the mic shows no colours for them, so both were removed. Two more getters answer on the real mic (2026-10-16, `peri razer <node> display-switch|tap`, zero arguments, read-only): class `0f` command `92` returns `00 00 01` and command `93` returns `00 02 00 01`. Their setters, `0f 12` and `0f 13`, are named "display switch" (headphone, mic-gain, voice-activated and mute-effect modules) and "tap" (tap-to-mute behaviour) in a public note on the V3 Pro (yterry/razer-seiren-macos, docs/PROTOCOL.md) that took them from the vendor software; the first argument byte (tried 0 to 7) is echoed back and selects nothing. With the first byte at 1, the second byte of `0f 92` is a module id that is echoed, and the value after it is `01` for modules 0 to 3 and `00` for 4 to 7: four switchable display modules, all on, which matches the four named in the public note (headphone, mic-gain, voice-activated, mute-effect), order unproven. `0f 93` ignores the second byte and always returns `01 02 00 01`; what its bytes mean is not known. Neither command has room for a colour, so the mute colour is somewhere else. `peri razer <node> display-set <0-3> <0|1>` is the one write the probe can make (`0f 12`, args `01 <module> <value>`); on 2026-10-16, with the owner's go-ahead and a restore value read first, module 3 was set to 0 and read back as 0, then set to 1 and read back as 1, the other three modules unchanged. The owner watched the ring: muted it went black, with module 3 off it showed the last applied look (red) while still muted, with module 3 back on it went black again, and unmuted it showed that red look. So module 3 is the mute effect, and on this mic its colour is black or off; the command that sets the colour is still unknown. The same log caught the tap reports as the owner tapped: `05 11 01` when they muted and `05 11 00` when they unmuted, which settles the mute byte. What is NOT known: the Mute Effect colour, the Peaking and volume indicators, tap remapping and the audio effects (noise gate, compressor, EQ, ...); `peri decode --scan` and `docs/capturing-synapse.md` turn a Windows USB capture of Synapse into the list of commands. Software-driven effects (Audio meter, Fire) are not built. Control interface `ff53:0004`, report ID 7, 64-byte feature report  **[research]**
- [x] Razer DeathAdder V3 Pro (`1532:00c3`): done 2026-10-15, the owner said "It seems to work" after using the pane. The reads and two writes work. Sent so far (2026-10-14): firmware get, once with the owner's OK through `peri razer90 hidraw0 firmware` and once more, same bytes, by mistake through a throwaway daemon that saw the real installed module; then battery, charging, DPI and poll-rate reads, once each with the owner's OK. Then, one command at a time through the live daemon with the owner's OK, DPI 3200 to 1600 and back and poll rate 1000 to 500 and back (each read back from the mouse and restored; the mouse stores every DPI set). Every reply was status ok, echoed transaction `0x1f`, carried the request's checksum rule (XOR of bytes 2..87, arguments under it) and is a test vector. The firmware arguments (`02 00`) are not understood. OpenRazer names `00c3` the V3 Pro's wireless identity (wired is `00c2`), so it may be the receiver; the mouse answered at once. The control interface is interface 0, usage `0001:0002` (its 90-byte vendor feature item sits inside the Mouse collection), so a module matches that, not a vendor page. Built: the frame codec `src/razer90.{c,h}`, the guarded transport (`1532:00c3` on `0001:0002` only), the CLI's read-only allowlist, the catalog module `razer-deathadder-v3-pro` (engine `razer90`), the daemon's `peri status` and `peri set <module-id> dpi=..|poll=..`, and the panel pane  **[research]**
  "Full support" for this mouse means (the goal set 2026-10-14), in order; each first send of a new frame needs the owner's OK on its exact bytes, and the goal text is not that OK:
  - [x] firmware read (`peri razer90 <node> firmware`, `peri status <module-id>`), seen working
  - [x] battery, charging, DPI and poll-rate reads, seen working 2026-10-14 (owner's OK, one send each through `peri razer90 hidraw0 <query>`): battery raw 103 (40% if 0-255: unproven), not charging, 3200:3200 DPI, 1000 Hz; the four replies are test vectors
  - [x] `peri status <module-id>` includes them, stopping at the first read the mouse does not answer: seen working 2026-10-14 (firmware 0200, battery 103, not charging, 3200:3200, 1000 Hz)
  - [x] DPI set (`04/05`, `peri set <module-id> dpi=<n>`) and poll-rate set (`00/05`, `poll=<125|500|1000>`), seen working 2026-10-14 with the owner's OK, through the live daemon: 3200 to 1600 and back (read back 1600:1600, then 3200:3200) and 1000 to 500 and back (read back 500, then 1000). The mouse's own read-back is the evidence; the owner did not report feeling the DPI change. The DPI set is always stored by the mouse (VARSTORE), so each set may be a memory write. The daemon reads the current value first as the restore target (no read, no write; it also refuses a split x/y DPI or an unknown poll code), enforces the module's DPI range (100 from the driver's floor, 30000 from Razer's rated sensor), and the pane sends on a slider's release after a pause only. Gotcha: `peri use` installs a SNAPSHOT of the catalog entry, so after a catalog change re-run `peri use <id>` (the first set was refused with 'the module sets no dpi range' until then)
  - [x] panel pane (`MouseControls.qml` over `PeriMouse.qml` and `mouse.js`, tests in `tst_mouse.qml`): battery, DPI slider and presets, poll rate; rendered offscreen against a fake peri with canned replies, online and asleep, and then seen in the owner's real shell on 2026-10-15 (their screenshot: battery 39%, DPI 3200 and 1000 Hz highlighted, controls on). No click on it has been reported yet; the daemon's writes it would send were tried by hand. Controls are on (`PeriMouse.writable` is true since both sets were seen working); a click or a slider release now writes, as on the mic pane. Install: `just plugin` and `omarchy restart shell` (the owner's steps)
  - Optional, open (the raw OpenRazer driver also gives this mouse these; each needs new frames, each first send needs the owner's OK on the exact bytes, and none was asked for yet):
    - [ ] DPI stages: read and set the stored list of DPI presets (`04/86`, 0x26 bytes, and the matching set)
    - [ ] the low-battery threshold: read and set (the driver's `charge_low_threshold` attribute; its exact frames are not looked up yet)
    - [ ] the idle time: how long before the mouse sleeps, read and set (the driver's `device_idle_time` attribute; its exact frames are not looked up yet)
    - Not possible on this identity: the faster poll rates (4000 and 8000 Hz belong to the HyperPolling dongle, with another command, and `00c3` is not that dongle)
  - Excluded: device mode (`00/04`, never), the serial (`00/82`, never recorded: the repo is public), lighting (the owner confirmed on 2026-10-14 that this mouse has none)

- [ ] Logitech BRIO (`046d:085e`, a UVC webcam driven by `uvcvideo`): the owner asked for it on 2026-10-15, chose to show cameras and to count the Logitech extras (field of view, HDR, RightLight) as part of the job. Plan, in small steps, each first write needing the owner's OK:
  - [x] cameras are no longer hidden (the BRIO lists as `046d:085e none`)
  - Read from the BRIO on 2026-10-15 (query only, nothing streamed or written): its only HID interface is the consumer control `000c:0001` (so a module anchored there is unambiguous); `video0` is the COLOUR camera (YUYV, MJPG, NV12) and `video2` the INFRARED one (8-bit GREY), `video1` and `video3` are their metadata nodes, all four on USB interface `1.0`, so pick the node by capture capability, pixel format and index, never by `/dev/video` number. Both capture nodes show the same user controls with the same values (one shared processing unit), and the IR node has no focus or zoom. `pan_absolute` reads 10800, not its default 0, so SOMETHING keeps camera settings: treat every camera write as possibly stored until a replug after the first approved write shows otherwise. The USB descriptor lists eight Logitech extension units (unit id, GUID, control count): 14 `{2c49d16a-32b8-4485-3ea8-643a152362f2}` 6, 6 `{23e49ed0-1178-4f31-ae52-d2fb8a8d3b48}` 14, 8 `{69678ee4-410f-40db-a850-7420d7d8240e}` 8, 9 `{1f5d4ca9-de11-4487-840d-50933c8ec8d1}` 18, 10 `{49e40215-f434-47fe-b158-0e885023e51b}` 11, 11 `{ffe52d21-8030-4e2c-82d9-f587d00540bd}` 6, 12 `{0f3f95dc-2632-4c4e-92c9-a04782f43bc8}` 2, 21 `{5a6d654c-7e35-4d4e-810d-069d15e0f79b}` 1. What each does is not established. Hard rule for the extras: never send an extension-unit write (`SET_CUR`) outside an allowlist of selectors a documented source names, and never query undocumented ones (some units are firmware and test functions): treat it like the Razer device mode
  - [x] (built and read working 2026-10-15) a `v4l2` engine: the module matches the BRIO's HID interface (consumer control `000c:0001`) to identify the device, and the daemon finds its capture node (`/dev/video*` with the video-capture capability) through sysfs; control ioctls only (the allowlist is `QUERYCAP`, `QUERY_EXT_CTRL`, `QUERYMENU`, `G_CTRL`, the one write `S_CTRL` limited to a fixed list of standard control ids, and `UVCIOC_CTRL_QUERY` limited to GET requests and a one-byte `SET_CUR` (0 to 2) of selector 5 of the one extension unit whose GUID the camera's own descriptor lists as `49e40215-...`), never `STREAMON` and never a frame read, kept true by one `ioctl(` call site behind the allowlist (a ctest checks `camera.c`); the `/dev/video*` nodes already carry the user's ACL, so no udev rule
  - [x] `peri status <module-id>` lists the standard controls (seen working 2026-10-15 through the daemon with the owner's OK, queries only: all 17 controls matched the earlier `v4l2-ctl` listing, `pan_absolute` 10800 included; the owner has not said whether the camera's light came on) (read on 2026-10-15 by hand: brightness, contrast, saturation, sharpness, gain, backlight compensation, power-line frequency, white balance auto and temperature, exposure auto and time and dynamic frame rate, focus auto and absolute, zoom 100-500, pan and tilt)
  - [x] `peri set <module-id> <control>=<value>`: SEEN WORKING 2026-10-15 with the owner's OK, one command each through the live daemon: `brightness` 128 to 160 and back to 128 (the owner watched the OBS colour preview, said it was brighter, and after the restore said "it went back"; the daemon's read-back is only the driver's cache). Zoom and the stepped pan and tilt were seen working too (2026-10-15, owner's OK, one command each, OBS on the colour node): `zoom_absolute` 100 to 200 (about 2x, as the owner saw), `pan_absolute` 10800 to -7200 (the owner's face moved to their left in the picture) and back, `tilt_absolute` 0 to 7200 (the picture moved down) and back, `zoom_absolute` back to 100; units are arc-seconds (a step of 3600 is one degree), and pan and tilt only move the picture while zoomed in. All values were restored to what they were (zoom 100, pan 10800, tilt 0). A switch and a menu were seen working too (2026-10-15, owner's OK, one command each): `white_balance_automatic` 1 to 0 and back, three flips, the owner saw the colours change each time and the control `white_balance_temperature` became active (and inactive again); `auto_exposure` 3 to 1 (Manual; the owner: "very dark", the exposure stays fixed) and back to 3, with `exposure_time_absolute` becoming active and then inactive again; asking for the menu option 2, which the camera does not offer, was refused with nothing sent. Not yet tried: `exposure_dynamic_framerate`, `power_line_frequency`, and whether a set survives a replug (not tested). Built: One control per request, named as `peri status` shows it; only a fixed list of standard V4L2 ids can ever be written through this path (a vendor control a driver maps into V4L2 under its own id is refused inside `peri_cam_ioctl`), with one exception by name, `field_of_view`, which is routed to its own checked extension-unit write (below); the control must not be read-only or inactive, the value must be in range and on the control's step, a menu index must exist, and the current value is read first as the restore target. `VIDIOC_S_CTRL` is the one write on the allowlist, `S_EXT_CTRLS` is not. The reply shows `before`, the value sent and the camera's own read-back; treat every set as possibly stored (the camera keeps its pan)
  - [x] a panel pane (2026-10-15): `peri controls <module-id>` (daemon `controls` request, read-only) reports every control with type, range, step, value and flags and each menu's options; `camera.js` (tests in `tst_camera.qml`), `PeriCamera.qml` and `CameraControls.qml` draw it from that list in two balanced columns: a slider per range, Off/On for a switch (a 0-to-1 integer such as backlight compensation counts as one), a button per menu option, inactive controls greyed, only controls the daemon says may be set. Rendered offscreen against a fake peri carrying the real camera's reply. Read and write rules of the pane (2026-10-15, after the owner saw a slider snap back after release: `PanelSlider` sets its knob back to the model's value on release, so the pane now keeps what was just chosen, `shown`, until the camera has been read again): a choice is drawn at once; every control has its own queue entry (a slider waits 600 ms of quiet, a button 40 ms), entries are written one at a time in order and never dropped; the camera is read only when nothing is queued, nothing is running and no slider is held; a list of controls that arrives while a slider is held waits for the release; a write's result is drawn but never replaces the controls (a full read always follows the last write); a refused write keeps its error and the pane shows what is true; a slider reports its release even if destroyed and opening the pane resets a stuck hold. `tests/qml/camera-scenario/run.sh` (a ctest when `quickshell` is found) drives the real core against a fake `peri` and checks the exact order of calls; both old bugs, put back, make it fail. The same fixes were then made in the mouse, keyboard and mic panes (2026-10-15, after an audit of every slider and write path in the panel), each with its own headless scenario and mutation-checked: the mouse's DPI slider and its preset and poll-rate highlights no longer snap back and a poll-rate click is no longer dropped while a DPI write runs; the keyboard's brightness and speed knobs, hue marker, toggle, group and effect highlights no longer snap back, two quick LED-group clicks both stick (the second used to be built on stale flags and the first was lost), a read never starts over a change and a read that finishes after a change was sent is dropped (this includes the 30 second poll); the mic's saves are queued per look (a save for one look used to replace another's waiting save), a refused save no longer throws away the newer queued save, and a read that began before a save or an audio change can no longer overwrite it. The generic helpers are in `quickshell/peri/choices.js` (tested in `tst_choices.qml`); the four scenarios are ctest tests labelled `scenario` (`ctest -LE scenario` skips their 20 seconds). Seen in the owner's real shell on 2026-10-15 after `just plugin` and `omarchy restart shell`: the sliders and buttons of the camera, mouse, keyboard and mic panes worked (the owner: "they seem to work"). Left alone on purpose: the module Install and Remove buttons ignore a click while the list is busy (they are dimmed then). The controls are ON (`PeriCamera.writable` true since 2026-10-15, when an integer, the stepped pan and tilt, a switch, a menu and the field of view had each been tried by hand); a click or a slider release now writes, as on the other panes. Install: `just plugin` and `omarchy restart shell` (the owner's steps)
  - Field of view: the control is selector 5 of the Logitech extension unit with GUID `49e40215-f434-47fe-b158-0e885023e51b`, one byte, with a 65/78/90 degree menu, as third-party notes for this camera describe (github.com/shawnyeager/briotune, BRIO.md; github.com/Nek/fov-switcher, PROTOCOL.md, which covers the newer `086b` and says the `085e` is "deliberately not treated as protocol-compatible"). This camera's own descriptor lists the unit (unit 10, `bmControls 0x6fba`: bit n means selector n+1, so selectors 2, 4, 5, 6 and 8 exist, 5 yes and 7 no). Still unknown: which byte is which angle on the `085e` (the `086b` notes say 90=0, 78=1, 65=2): read the camera's own `GET_MIN/MAX/DEF/CUR`, then find the mapping by trying each value with the owner watching a preview, and whether the preview needs a restart of the stream to show a change
  - [x] field of view reads SEEN WORKING 2026-10-15 (owner's go-ahead, one command each, `peri fov brio <query>`): `len` 01 00 (the control is 1 byte), `info` 03 (readable and writable, not disabled by an automatic mode), `cur` 00, `min` 00, `max` 02, `def` 00, `res` 01: three positions 0, 1 and 2, currently 0 (the default). Which position is which angle on this camera is still unknown (a write test settles it). Built earlier: `peri fov <module-id> <cur|min|max|res|len|info|def> [size]` (daemon `fov` request) sends one GET query per call (the kernel itself sends `GET_LEN` and `GET_INFO` the first time it touches an extension-unit control, and refuses with `ENOBUFS` a size that is not the control's length, per `uvc_xu_ctrl_query` in drivers/media/usb/uvc/uvc_ctrl.c; the Logitech recording-LED control is on the kernel's privacy block list); the unit is found by GUID in the camera's descriptor at run time and approved only if it lists selector 5 (checked against this camera's real descriptor with a throwaway reader: unit 10, `bmControls 0x6fba`), and the allowlist inside `peri_cam_ioctl` refuses any other unit, selector, a SET, an odd size or a missing buffer (tests under ASan). Next: the owner's OK per read, then the value-to-angle mapping, then a write with its own OK
  - [x] field of view write: SEEN WORKING 2026-10-15 with the owner's OK, one command each through the live daemon, with OBS on the colour node: `peri fov brio set 1`, `set 2`, `set 0`, and a second round `set 1`, `set 2`, `set 0`, each confirmed by the camera's own read-back (a real query of the device) and by the owner's eyes after restarting the OBS source: 0 is the WIDEST view, 1 is more zoomed in than 0, 2 is more zoomed in than 1. The values are ordered like the three angles in the third-party notes (90, 78, 65 degrees); the exact angles were not measured. The setting was left at 0 (the default). Friendlier since (2026-10-15, SEEN WORKING with the owner's OK, OBS on the colour node: `peri set brio field_of_view=medium` showed the middle view and `peri fov brio set wide` restored the widest, each confirmed by the camera's read-back and the owner's eyes): `peri fov <module-id> set <wide|medium|narrow|0-2>` and `peri set <module-id> field_of_view=<wide|medium|narrow|0-2>` take names (wide 0, medium 1, narrow 2, as the owner saw them on this camera: another camera's order may differ), and `peri controls` and `peri status` list the field of view as one more row so the pane draws it as a menu (Wide, Medium, Narrow); the row is appended only when the camera's descriptor has the unit and its state reads, and its flag is `s` only when the camera says it is writable. The pane stays read-only like the rest. Not found out: whether a change shows in a stream that is already running without restarting it (the owner restarted the source each time after the first), and whether the setting survives a replug (not tested). `peri fov <module-id> set <0-2>`: the allowlist in `peri_cam_ioctl` lets a SET_CUR through only for the descriptor-approved unit, selector 5, one byte, value 0 to 2 (the camera's own max); the daemon first reads `len` (must be 1), `info` (must say writable and not disabled), `max` and `cur` (the restore target), writes nothing if any of that fails and does not send a write of the value it already has
  - [ ] untried standard controls: `exposure_dynamic_framerate` (low-light compensation, a switch; the effect is hard to see, so the camera's own report is the main evidence) and `power_line_frequency` (a menu: Disabled, 50 Hz, 60 Hz; the effect shows only under mains light). Each needs the owner's OK on the exact command and its restore (the current values are 1 and 60 Hz)
  - [ ] does a write survive a replug? Set one value (the field of view is the clearest to see: narrow), replug the camera, read it back, restore it. The camera keeps at least its pan (it read 10800, not its default 0, before any write), so every write is treated as possibly stored until this is tested
  - [ ] does a field-of-view change show in a stream that is already running, without restarting it? (the owner restarted the OBS source after each change)
  - [ ] RightLight and HDR: no public documentation found for any BRIO, so they are not built and nothing can be built from here. Open only if a source appears; then a frame needs the owner's OK like any other
  - Not asked for: a recording-LED control (unit 11 / peripheral GUID `ffe52d21-...`, selector 9) is claimed by third-party notes for the 085e, but the kernel keeps it on its privacy block list and it was never wanted
  - [x] the Logitech extras: field of view is built and seen working (see above)

## Housekeeping

- [x] The CLI (shell environment) and the daemon (systemd's) may resolve `$XDG_DATA_HOME` differently. Setup installs and checks in one process, and the user unit it writes pins those resolved directories (`Environment=XDG_CONFIG_HOME` and `XDG_DATA_HOME`) so the daemon uses them rather than the user manager's defaults. The runtime directory is not pinned: it is per session and has no default. The udev rule carries only vid:pid, built in that same setup process.
- [ ] A mock transport so the device frames can be tested without the keyboard (the pure parts have unit tests, and the pane scenarios run against a fake peri)
- [ ] Daemon logging
- [ ] License and contribution files (not decided)
