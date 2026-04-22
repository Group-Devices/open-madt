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

## License

This project is distributed under the GNU General Public License v3.0.

See [LICENSE](LICENSE) for the full license text.

## Third-Party Notices

This project uses Qt for its GUI layer.

Qt is licensed separately from this project and remains subject to the
license terms of the Qt modules used in a given build and distribution.

See [NOTICE](NOTICE) for the Qt notice included with this repository.
