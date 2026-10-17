# periferi

Linux control for peripherals: a daemon, a CLI and a Quickshell panel for the Drop CTRL v2 keyboard and the Razer Seiren V3 Chroma microphone.

A daemon (`perid`) owns the devices and speaks to them; the CLI (`peri`) and the shell panel are thin clients of it. Devices are described by small declarative **modules** in `catalog/`, each naming one of the built-in protocol engines. It works with stock firmware only: periferi never flashes anything.

| Device | Engine | What works |
|--------|--------|------------|
| Drop CTRL v2 keyboard | QMK XAP | RGB effect, colour, speed, brightness (capped by the board's own limit), LED groups |
| Razer Seiren V3 Chroma microphone | Razer vendor reports (reverse engineered) | Effects (colour, breathing, spectrum, wave, off), brightness, two saved looks, and the sound card's gain, monitor and headphone levels |
| Razer DeathAdder V3 Pro mouse | Razer long vendor frame (public references, checked on the device) | Reads firmware, battery, charging, DPI and poll rate; sets DPI and poll rate (125, 500 or 1000 Hz). No lighting on this model |

periferi is unofficial and not affiliated with Drop, Razer or QMK. The Seiren's protocol is not published: what is here was worked out from public sources and by watching the device, and some Synapse settings (the mute light colour, tap remapping, the audio effects) are not supported. Writes to a device may be kept in its own memory. The mouse's DPI and poll rate are sent by `peri set` and by the panel's controls (the mouse stores every DPI set). The microphone's saved looks are sent by `peri look apply`, and by the panel's Apply button. Nothing watches the mic. The project's tests never touch hardware.

## Build and install

You need `clang` (with C23 support), `cmake` 3.25 or newer, `ninja`, `just` and the ALSA development headers (`libasound2-dev` on Debian and Ubuntu). The panel needs Quickshell, and the QML tests need Qt 6's `qmltestrunner`.

```
just build      # configure and build peri and perid
just install    # copy them to ~/.local/bin
just setup      # install the udev rule (asks for admin rights) and start the user service
```

`just setup` runs `peri setup`, which writes a udev rule so your user may open the devices of the modules you have installed, then installs and starts the `perid` systemd user service. Run it yourself in a terminal.

## Use

```
peri list                              # the HID devices and which module drives each
peri modules                           # the modules in the catalog and the ones installed
peri use drop-ctrl-v2                  # install a module (the Seiren's is razer-seiren-v3-chroma)

peri rgb get                           # the keyboard's lighting
peri rgb set hue=170 sat=255

peri status razer-seiren-v3-chroma     # firmware, brightness
peri look razer-seiren-v3-chroma save unmuted effect=breathing color=00ff40 brightness=200
peri look razer-seiren-v3-chroma apply unmuted
peri audio razer-seiren-v3-chroma get  # gain, monitor mix, headphones
```

`peri` with no arguments prints every command. The microphone cannot report its own effect or colour, so periferi keeps two **looks** for it (Unmuted and Muted) in `~/.config/peri/state/`. Apply sends one to the mic when you ask; the mic's own firmware decides what it shows while muted.

For the panel, `just plugin` installs it as an Omarchy shell widget; `omarchy restart shell` loads it. The Quickshell core in `quickshell/peri/` imports nothing from the host shell, so other hosts can use it.

## Develop and test

```
just test       # build and run the unit tests and the headless pane scenarios (no hardware needed)
just qml-check  # lint the Quickshell side
just dev        # rebuild, install and restart the service on every change
```

`ctest --test-dir build` runs the same tests. The plan and what is known about each device are in [ROADMAP.md](ROADMAP.md); [docs/capturing-synapse.md](docs/capturing-synapse.md) explains how to record Razer's software talking to the Seiren, which is how the missing settings could be learned. [AGENTS.md](AGENTS.md) has the working rules for contributors and coding agents.

## License

MIT. See [LICENSE](LICENSE). Copyright (c) 2026 Christoffer Hallas.

The TOML parser in `vendor/tomlc17` is MIT licensed by CK Tan; see `vendor/tomlc17/LICENSE`.

If you send a pull request or other contribution, you agree to the [CLA](CLA.md).
