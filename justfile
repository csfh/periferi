set shell := ["bash", "-euo", "pipefail", "-c"]

# Binaries go to ~/.local/bin, which is on PATH.
prefix := env("HOME") + "/.local"

default:
    @just --list

# Configure (if needed) and build peri and perid.
build:
    cmake -S . -B build -G Ninja
    cmake --build build

# Build and copy peri and perid into {{prefix}}/bin.
install: build
    cmake --install build --prefix {{prefix}}

# Install, then reinstall the unit and restart perid. Never touches the udev rule: that needs admin
# rights (a security-key touch), and `dev` runs this on every save. Use `just setup` for the rule.
reload: install
    {{prefix}}/bin/peri setup --no-udev

# The full setup, including the udev rule (asks for admin rights). Run it yourself, in a terminal.
setup: install
    {{prefix}}/bin/peri setup

# Watch the sources: rebuild, install and restart the service on every change.
dev:
    watchexec --clear --watch src --watch CMakeLists.txt -- just reload

# Build and run the unit tests (no hardware needed).
test: build
    # -j4: the headless pane scenarios (about 20 s each) run side by side. `ctest --test-dir build -LE scenario` skips them.
    ctest --test-dir build --output-on-failure -j4

# Check the Quickshell side without installing anything.
qml-check:
    ! grep -rn 'import qs\.' quickshell/ || { echo "the generic core must not import the host shell (qs.*)"; exit 1; }
    d=$(mktemp -d) && trap 'rm -rf "$d"' EXIT && cp -rL adapters/omarchy/. "$d" && omarchy plugin validate "$d"
    # /usr/bin/qmllint is Qt 5 here. The Qt 6 one catches typos but exits 0 on warnings, so --max-warnings 0.
    /usr/lib/qt6/bin/qmllint --signal-handler-parameters disable --max-warnings 0 quickshell/peri/PeriLighting.qml quickshell/peri/PeriDevices.qml quickshell/peri/PeriMic.qml quickshell/peri/PeriMouse.qml quickshell/peri/PeriCamera.qml tests/qml/live/shell.qml
    # The Omarchy and standalone files import host modules (qs.*, PanelWindow) that lint cannot resolve: syntax only.
    qmllint -I /usr/share/omarchy/shell adapters/omarchy/Lighting.qml adapters/omarchy/PanelContent.qml adapters/omarchy/LightingControls.qml adapters/omarchy/MicControls.qml adapters/omarchy/MouseControls.qml adapters/omarchy/CameraControls.qml adapters/omarchy/HuePlane.qml adapters/standalone/shell.qml

# Omarchy shell widget: `just plugin` installs and enables it, `just plugin remove` removes it.
plugin action="install":
    #!/usr/bin/env bash
    set -euo pipefail
    dir=~/.config/omarchy/plugins/peri.lighting
    case "{{action}}" in
      install)
        just qml-check
        rm -rf "$dir"
        install -d "$dir"
        cp -rL adapters/omarchy/. "$dir"/
        omarchy-shell shell rescanPlugins
        omarchy plugin enable peri.lighting
        echo "Installed. If you are updating a widget that is already loaded, the shell may keep"
        echo "running the old code: run 'omarchy restart shell' to be sure."
        ;;
      remove)
        rm -rf "$dir"
        omarchy-shell shell rescanPlugins
        ;;
      *)
        echo "usage: just plugin [install|remove]" >&2
        exit 2
        ;;
    esac

# Follow the daemon's log.
logs:
    journalctl --user -u perid.service -f
