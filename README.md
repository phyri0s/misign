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

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the Git workflow, commit conventions and the DCO.
