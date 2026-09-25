FROM mcr.microsoft.com/devcontainers/cpp:ubuntu-24.04

ARG PODOFO_VERSION=1.1.2
ARG AQTINSTALL_VERSION=3.3.0

# Toolchain, Qt build and runtime dependencies (OpenGL, XCB), PoDoFo dependencies, PDF tools
RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get install -y --no-install-recommends \
        cmake ninja-build clang-format clang-tidy python3-venv \
        libgl-dev libegl-dev libxkbcommon-dev libxkbcommon-x11-0 libfontconfig1 libdbus-1-3 \
        libxcb-cursor0 libxcb-icccm4 libxcb-image0 libxcb-keysyms1 libxcb-randr0 \
        libxcb-render-util0 libxcb-shape0 libxcb-xkb1 \
        libfreetype-dev libssl-dev libxml2-dev zlib1g-dev libfontconfig-dev \
        libjpeg-dev libpng-dev libtiff-dev \
        qpdf ghostscript \
    && rm -rf /var/lib/apt/lists/*

# Ubuntu 24.04 only ships PoDoFo 0.9.8, which lacks incremental updates: build from source
RUN git clone --depth 1 --branch ${PODOFO_VERSION} https://github.com/podofo/podofo.git /tmp/podofo \
    && cmake -S /tmp/podofo -B /tmp/podofo/build -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DPODOFO_BUILD_LIB_ONLY=ON \
    && cmake --build /tmp/podofo/build \
    && cmake --install /tmp/podofo/build \
    && ldconfig \
    && rm -rf /tmp/podofo

# Qt, at the version pinned in scripts/install_qt.py, exposed at /opt/qt
COPY scripts/install_qt.py /tmp/install_qt.py
RUN python3 -m venv /tmp/aqt \
    && /tmp/aqt/bin/pip install --no-cache-dir aqtinstall==${AQTINSTALL_VERSION} \
    && /tmp/aqt/bin/python /tmp/install_qt.py --output-dir /opt/Qt \
    && ln -s "$(/tmp/aqt/bin/python /tmp/install_qt.py --output-dir /opt/Qt --print-prefix)" /opt/qt \
    && rm -rf /tmp/aqt /tmp/install_qt.py

ENV LANG=C.UTF-8 \
    CMAKE_PREFIX_PATH=/opt/qt \
    PATH=/opt/qt/bin:${PATH}
