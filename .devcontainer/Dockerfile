FROM mcr.microsoft.com/devcontainers/cpp:1-ubuntu-24.04

ARG DEBIAN_FRONTEND=noninteractive

RUN apt-get update \
    && apt-get install --yes --no-install-recommends \
        ccache \
        clang-format \
        clangd \
        cmake \
        g++-14 \
        gcc-14 \
        gdb \
        libeigen3-dev \
        libgtest-dev \
        libhdf5-dev \
        liblog4cxx-dev \
        ninja-build \
        nlohmann-json3-dev \
        pkg-config \
        python3 \
        python3-pip \
        python3-venv \
    && rm -rf /var/lib/apt/lists/*

ENV CC=gcc-14 \
    CXX=g++-14 \
    CCACHE_DIR=/home/vscode/.cache/ccache

