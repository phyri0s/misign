FROM mcr.microsoft.com/devcontainers/cpp:ubuntu-24.04

ARG PODOFO_VERSION=1.1.2

RUN apt-get update && export DEBIAN_FRONTEND=noninteractive \
    && apt-get install -y --no-install-recommends \
        cmake ninja-build clang-format clang-tidy \
        qt6-base-dev qt6-declarative-dev qt6-tools-dev qt6-pdf-dev \
        qml6-module-qtquick qml6-module-qtquick-controls \
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
