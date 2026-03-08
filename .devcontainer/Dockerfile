# Bugdom Linux build environment (matches BUILD.md + build.py)
# Supports native Linux builds and WASM/Emscripten builds
FROM ubuntu:24.04

ARG DEBIAN_FRONTEND=noninteractive

# Build deps: CMake, C++20 compiler, OpenGL (Mesa), Python, pkg-config, X11/Wayland (for SDL on Linux)
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    ca-certificates \
    cmake \
    git \
    libgl1-mesa-dev \
    libwayland-dev \
    libx11-dev \
    libxcursor-dev \
    libxext-dev \
    libxfixes-dev \
    libxi-dev \
    libxinerama-dev \
    libxkbcommon-dev \
    libxrandr-dev \
    libxss-dev \
    libxtst-dev \
    libxxf86vm-dev \
    pkg-config \
    python3 \
    wayland-protocols \
    wget \
    && rm -rf /var/lib/apt/lists/*

# Install Emscripten SDK for WASM builds
ARG EMSDK_VERSION=latest
RUN git clone https://github.com/emscripten-core/emsdk.git /opt/emsdk \
    && /opt/emsdk/emsdk install ${EMSDK_VERSION} \
    && /opt/emsdk/emsdk activate ${EMSDK_VERSION} \
    && echo '. /opt/emsdk/emsdk_env.sh' >> /etc/profile.d/emsdk.sh
