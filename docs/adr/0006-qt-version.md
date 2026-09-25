# 0006 — Qt 6.11, installed with aqtinstall everywhere

- **Status**: accepted
- **Date**: 2026-09-25

## Context

Misign is built in three places: the dev container (Linux), CI (Linux and Windows) and developers' machines (WSL, Linux, Windows). Ubuntu 24.04 ships Qt 6.4.2, which reached end of life in 2023 and lacks recent QML APIs (`QQmlApplicationEngine::loadFromModule` needs 6.5). Windows has no system Qt at all.

## Options considered

- **Qt 6.4.2 from Ubuntu**: nothing to download on Linux, but end of life, and Windows would still need a separate installation of the same version.
- **Qt 6.8.3 (LTS)**: open-source LTS patch releases stop after the first year, and 6.8.3 is the last public one. No real support advantage over a newer version.
- **Qt 6.11.3**: latest stable series with several patch releases.

## Decision

- Qt **6.11.3**, the same version everywhere.
- Installed with `scripts/install_qt.py`, which holds the version in one place and wraps aqtinstall. The Qt online installer is not used because it requires a Qt account, which is impractical in CI.
- `CMakeLists.txt` requires Qt 6.11 (`find_package(Qt6 6.11 ...)`, `qt_standard_project_setup(REQUIRES 6.11)`).

`scripts/install_qt.py` works around two aqtinstall 3.3 limitations:

1. The Qt mirrors only publish SHA-1 checksums, while aqtinstall expects SHA-256 by default.
2. Since Qt 6.8, Qt PDF ships in the "extensions" repository, which aqtinstall cannot install for Qt 6.11. The script downloads it directly, checks its SHA-1 and extracts it into the Qt prefix.
3. For Qt 6.11 on Windows, the mirror splits the repository into one directory per compiler (`qt6_6113_msvc2022_64/`), which aqtinstall 3.3 (the latest release) cannot find. On Windows the script installs Qt itself the same way as Qt PDF: it reads the repository's `Updates.xml`, downloads the base package and the extra modules, checks each archive's SHA-1 and extracts it into the Qt prefix. Linux still uses aqtinstall.

## Consequences

- Upgrading Qt means changing `QT_VERSION` in `scripts/install_qt.py` and rebuilding the dev container.
- The workarounds depend on the layout of the Qt mirrors and may break when it changes. They can be removed once aqtinstall supports extensions for recent Qt versions.
- Ubuntu's `qt6-*` packages are no longer used, on the host or in the dev container.
