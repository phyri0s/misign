# Misign

A desktop application for signing PDFs by hand: open a document, place a signature box, draw your signature with a stylus, touchpad or mouse, then save a signed copy. Everything stays local, with no network traffic.

- **Platforms**: Windows and Linux (macOS postponed)
- **Stack**: Qt 6 + C++ (CMake), QML UI, Qt PDF and PoDoFo, Qt Test
- **Architecture**: hexagonal (independent domain, one adapter per technology)
- **License**: [GPLv3](LICENSE)

## Layout

```
misign/
├── .github/workflows/   # ci.yml, deploy-dev.yml, deploy-prod.yml
├── .devcontainer/       # Docker development environment
├── docs/adr/            # architecture decision records
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

The development environment is provided as a dev container (VS Code or JetBrains): open the repository and choose "Reopen in Container". It ships CMake, Qt 6, PoDoFo and the PDF tools (`qpdf`, Ghostscript).

Stylus and touchpad input must always be tested on the host system, including under WSL: the stylus driver stays on the Windows side.

### Build and test

Requirements: CMake 3.25+, Ninja, a C++20 compiler and Qt 6.4+ (Core, Gui, Qml, Quick, Test). Outside the dev container, point CMake at your Qt installation, e.g. `export CMAKE_PREFIX_PATH=~/Qt/6.8.0/gcc_64`, or add a local `CMakeUserPresets.json` (ignored by Git).

```sh
cmake --preset debug
cmake --build --preset debug
ctest --preset debug              # all tests
ctest --preset debug -L unit      # unit tests only
./build/debug/src/ui/misign       # run the application
```

Use the `release` preset for an optimized build. Pass `-DMISIGN_WARNINGS_AS_ERRORS=ON` to fail on compiler warnings.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the Git workflow, commit conventions and the DCO.
