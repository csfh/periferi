# Working on periferi

For people and coding agents. Facts live in the code and in [ROADMAP.md](ROADMAP.md); this file is how to work.

## Build and test

```
just build      # cmake + ninja into build/
just test       # unit tests and the headless pane scenarios (a fake peri), no hardware
just qml-check  # lint the Quickshell files (Qt 6 qmllint, --max-warnings 0)
```

The C code is C23 built with clang, with the sanitizers on in the default Debug build. The QML tests need Qt 6's runner (`/usr/lib/qt6/bin/qmltestrunner`); the Qt 5 one that may be on `PATH` does not work.

## Rules that matter

- **Never write to a device from a test, from CI or without the owner's go-ahead.** The only things that write to the microphone are `peri look <module> apply` and `peri set`; `peri audio set` writes the sound card. Reads and file-only saves are fine. A write may be kept in the device's own memory, so do not loop or auto-send.
- **Try daemon behaviour against a throwaway daemon**: start `build/perid` with its own `XDG_RUNTIME_DIR`, `XDG_CONFIG_HOME` AND `XDG_DATA_HOME` in a temp directory (installed modules are read from the data directory, so without it the daemon sees your real modules and talks to your real devices: check that `peri modules` against it lists nothing before running any device-facing verb), and stop it by its pid. Never `pkill` by name; the real `perid` shares it. Run `peri setup`, `just reload` and `just dev` from your normal shell only: setup pins the directories it sees into the real service unit.
- **An `ok` from a device is not proof an effect works.** The Seiren answers `ok` to frames it does not act on. A frame built from someone else's documentation is unverified until it has been seen working on the device.
- **Do not copy other projects' code.** Byte values and layouts from public references are facts and fine; their source is not. Note where a layout came from in a comment.
- **Devices are addressed by module and USB id, never by `/dev/hidraw` number.** The number changes.
- The Quickshell core (`quickshell/peri/`) must not import the host shell (`qs.*`); `just qml-check` enforces it.

## How we work

Small commits, one feature each, with its tests. Daemon first, then the CLI, then the panel. Comments say why, not what. Update ROADMAP.md when an item finishes. The offscreen preview (`tests/qml/omarchy-preview`) renders the panel without opening a window; it refuses to run unless `QT_QPA_PLATFORM=offscreen`, and its scripted gestures need `PERI_PREVIEW_ISOLATED=1` and a throwaway daemon (see the comment in its `shell.qml`).

## Where things are

- `src/`: `perid` and `peri` and the engines. `razer.c` is the Seiren's frame codec, `store.c` the saved looks, `module.c` the module format.
- `catalog/`: the modules. One `module.toml` per device; the format is described in `catalog/drop-ctrl-v2/module.toml`.
- `quickshell/peri/` and `adapters/`: the panel's core and its Omarchy look.
- `tests/`: C tests and `tests/qml/` for the panel's logic.
- `docs/`: how to capture the vendor software's traffic.
