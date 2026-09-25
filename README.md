# Misign

A desktop application for signing PDFs by hand: open a document, place a signature box, draw your signature with a stylus, touchpad or mouse, then save a signed copy. Everything stays local, with no network traffic.

- **Platforms**: Windows and Linux (macOS postponed)
- **Stack**: Qt 6.11 + C++ (CMake), QML UI, Qt PDF and PoDoFo, Qt Test
- **Architecture**: hexagonal (independent domain, one adapter per technology)
- **License**: [GPLv3](LICENSE)

## Layout

```
misign/
├── .github/workflows/   # ci.yml, codeql.yml, deploy-dev.yml, deploy-prod.yml
├── .devcontainer/       # Docker development environment
├── docs/adr/            # architecture decision records
├── scripts/             # development tools (Qt installation)
├── src/
│   ├── domain/          # strokes, smoothing, coordinates
│   ├── application/     # use cases
│   ├── adapters/        # PDF, input, storage
│   └── ui/              # user interface
├── tests/
│   ├── unit/            # domain
│   ├── integration/     # use cases with real adapters
│   └── fixtures/pdf/    # reference corpus
├── CHANGELOG.md
└── README.md
```

## Development

Stylus and touchpad input must always be tested on the host system, including under WSL: the stylus driver stays on the Windows side.

### Dev container

The dev container (VS Code or JetBrains) ships CMake, Qt 6.11 with Qt PDF, PoDoFo and the PDF tools (`qpdf`, Ghostscript). Open the repository and choose "Reopen in Container".

The application window is displayed through X11: WSLg on Windows, the host X server or XWayland on Linux (you may need `xhost +local:` there). Container builds go to `build-container/`, so they never clash with host builds in `build/`.

### On the host

Requirements: CMake 3.25+, Ninja, a C++20 compiler and Qt 6.11 with Qt PDF. Install Qt with the project script, which pins the version used everywhere (see [ADR 0006](docs/adr/0006-qt-version.md)):

```sh
python3 -m venv ~/.venvs/aqt && ~/.venvs/aqt/bin/pip install aqtinstall
~/.venvs/aqt/bin/python scripts/install_qt.py --output-dir ~/Qt   # prints the Qt prefix
export CMAKE_PREFIX_PATH=~/Qt/6.11.3/gcc_64
```

On Windows, pass `--arch win64_msvc2022_64`. Instead of exporting `CMAKE_PREFIX_PATH`, you can set it in a local `CMakeUserPresets.json` (ignored by Git).

On Linux, Qt also needs the OpenGL and XCB system libraries. On Ubuntu:

```sh
sudo apt install libgl-dev libegl-dev libxkbcommon-dev libxkbcommon-x11-0 libxcb-cursor0 \
  libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 libxcb-render-util0 libxcb-shape0 libxcb-xkb1
```

### Build and test

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug              # all tests
ctest --preset debug -L unit      # unit tests only
./build/debug/src/ui/misign       # run the application (build-container/ in the dev container)
```

Use the `release` preset for an optimized build. Pass `-DMISIGN_WARNINGS_AS_ERRORS=ON` to fail on compiler warnings, and `-DMISIGN_REQUIRE_TEST_TOOLS=ON` to fail the configuration when `qpdf` or Python 3 is missing instead of skipping their tests (CI sets both).

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the Git workflow, commit conventions and the DCO.
