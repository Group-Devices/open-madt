# madt

Standalone MADT server source project.

MADT stands for `Multi-Application Driver Terminal`.

In public transport terminology, MADT is the driver-cabin HMI terminal used to display information from onboard applications and virtual devices, and to send user actions back to them over the onboard IP network.

This project provides the MADT server and its minimal runtime support used to host that browser-driven terminal flow.

## Build dependencies

Current standalone build expects:
- Qt5 Widgets
- Qt5 WebEngineWidgets
- nlohmann-json
- libuuid
- OpenSSL development files
- libevent 2.2.1 alpha development files

The project expects libevent `libevent-2.2.1-alpha-dev.tar.gz`, published from the official libevent release page, to be built into a local prefix and then passed to CMake with `-DMADT_LIBEVENT_ROOT=<prefix>`.

## Build libevent

This repository includes a helper that downloads and installs the expected libevent release into a local prefix:

```bash
./scripts/build-libevent.sh "$PWD/.deps/libevent"
```

For offline or pre-fetched validation, the helper also accepts a local source override:

```bash
LIBEVENT_SOURCE_DIR=/path/to/libevent-source ./scripts/build-libevent.sh "$PWD/.deps/libevent"
```

## Build

Typical build:

```bash
cmake -S . -B build -DMADT_LIBEVENT_ROOT="$PWD/.deps/libevent/install"
cmake --build build
```

The local logging shim uses a compile-time threshold controlled by `MADT_LOG_LEVEL`, which defaults to `INFO`. Accepted values are `DEBUG`, `INFO`, `WARN`, and `ERROR`.

## Runtime configuration

At runtime, the server optionally reads `madt-config.json` from its working
directory.

Currently supported keys:
- `tabMapPassword`: password used by `GetTabMap`
- `controlPassword`: password used by `Stop` and `Restart`
- `controlPsk`: optional PSK used for token-based `Stop` / `Restart` authorization
- `controlNonceTtlSeconds`: lifetime of `GetRandom` control nonces
- `tabLifetimeByConnection`: when `true`, tabs created through a TCP connection
  are deleted when that connection closes; when `false`, tab lifetime is
  decoupled from socket lifetime
- `soundPlayerCommand`: local command used to play configured sound files,
  default `aplay`
- `soundFiles`: mapping from standard MADT sound aliases such as
  `SystemNotification` to local file paths
- `backlight`: optional object describing LCD brightness control with
  `command`, `path`, `maxValue`, and/or `maxValuePath`
- `audioVolume`: optional object describing runtime volume control with either
  `script` or the legacy `command` + `controlName`
- `tabBarVisible`: show or hide the tab bar
- `tabBarEdge`: one of `top`, `bottom`, `left`, `right`
- `tabBarWidth`: tab width in pixels
- `tabBarHeight`: tab height in pixels
- `tabBarShowLabels`: show the numeric tab labels
- `tabBarShowTooltips`: show the tab URL as tooltip text
- `tabBarUseScrollButtons`: enable or disable scroll buttons on the tab bar
- `extraZonePlacement`: one of `top`, `bottom`, `free`
- `extraZoneHeight`: extra zone height in pixels for `top` / `bottom`;
  ignored when `extraZonePlacement` is `free`
- `extraZoneRect`: rectangle used when `extraZonePlacement` is `free`, with
  `x`, `y`, `width`, and `height` in pixels; ignored for `top` / `bottom`
- `shortcutsEnabled`: globally enable shortcut support
- `shortcutLauncherVisible`: show or hide the shortcut launcher button
- `shortcutLauncherLabel`: text displayed on the launcher button
- `shortcutLauncherCorner`: one of `top-left`, `top-right`
- `shortcutPopupTitle`: title shown above the shortcut grid
- `shortcutPopupColumns`: number of columns in the shortcut popup grid
- `shortcutIconWidth`: shortcut button icon width
- `shortcutIconHeight`: shortcut button icon height
- `shortcutMaxCount`: maximum number of live shortcuts accepted by the UI
- `shortcutAutoClose`: close the popup after shortcut activation
- `volume`, `brightness`, `contrast`, `language`, `defaultActiveMode`,
  `activeMode`, `defaultVisualMode`, `visualMode`, `extra1`, `extra2`:
  initial values returned by `GetSettings`; `SetSettings` updates the
  live MADT state and applies supported runtime effects such as browser
  volume propagation and backlight brightness, either directly or through
  configured scripts

If `tabLifetimeByConnection` is omitted, the standalone `open-madt` default is
`false`.

If the tab bar and extra zone keys are omitted, the standalone defaults are:
- `tabBarVisible=true`
- `tabBarEdge=top`
- `tabBarWidth=96`
- `tabBarHeight=48`
- `extraZonePlacement=top`
- `extraZoneHeight=160`
- `extraZoneRect={ "x": 0, "y": 0, "width": 640, "height": 160 }`

## Conformance Testing

This repository includes a live-server protocol conformance runner:

```bash
python3 ./scripts/madt_conformance.py --host 127.0.0.1 --port 25000
```

Useful options:

```bash
python3 ./scripts/madt_conformance.py \
  --host 127.0.0.1 \
  --port 25000 \
  --tab-map-password '<tab-map-password>' \
  --control-psk '<control-psk>'
```

By default the script is non-destructive. `Stop` / `Restart` are only executed
when `--control-action` is provided together with `--i-understand`.

## Client Simulator

For manual protocol testing, a simple MADT client simulator is available:

```bash
python3 ./scripts/madt_client_sim.py --pretty get-info
python3 ./scripts/madt_client_sim.py --pretty get-settings
python3 ./scripts/madt_client_sim.py --pretty play-sound --sound-id SystemNotification
python3 ./scripts/madt_client_sim.py --pretty new-web-tab --url 'data:text/html,<html><body>Hello</body></html>'
python3 ./scripts/madt_client_sim.py --pretty new-shortcut --url 'https://example.org'
```

Authenticated examples:

```bash
python3 ./scripts/madt_client_sim.py --pretty get-tab-map --password '<tab-map-password>'
python3 ./scripts/madt_client_sim.py --pretty stop --control-psk '<control-psk>'
```

## License

This project is distributed under the GNU General Public License v3.0.

See [LICENSE](LICENSE) for the full license text.

## Third-Party Notices

This project uses Qt for its GUI layer.

Qt is licensed separately from this project and remains subject to the
license terms of the Qt modules used in a given build and distribution.

See [NOTICE](NOTICE) for the Qt notice included with this repository.
