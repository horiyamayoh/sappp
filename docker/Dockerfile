# SAP++ CI Environment
#
# This Dockerfile provides an identical environment to GitHub Actions CI.
# Use this for local development to ensure changes pass CI before pushing.
#
# Build:
#   docker build -t sappp-ci docker/
#
# Usage:
#   docker run --rm -v $(pwd):/workspace -w /workspace sappp-ci ./scripts/pre-commit-check.sh
#
FROM ubuntu:24.04

LABEL maintainer="SAP++ Team"
LABEL description="SAP++ CI Environment - identical to GitHub Actions"

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# Install all CI dependencies in a single layer
RUN apt-get update && apt-get install -y --no-install-recommends \
    # Build essentials
    build-essential \
    cmake \
    ninja-build \
    # GCC 14 (C++23)
    gcc-14 \
    g++-14 \
    # Clang 19 (C++23)
    clang-19 \
    clang-format-19 \
    clang-tidy-19 \
    clangd-19 \
    libstdc++-14-dev \
    # Node.js for ajv-cli (schema validation)
    nodejs \
    npm \
    # Git for version control operations
    git \
    # Python for fallback tools
    python3 \
    python3-pip \
    # Utilities
    curl \
    jq \
    ccache \
    ripgrep \
    && rm -rf /var/lib/apt/lists/*

# Install ajv-cli globally for JSON Schema validation
RUN npm install -g ajv-cli ajv-formats

# Set GCC 14 as default
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100

# Set Clang 19 tools as alternatives
RUN update-alternatives --install /usr/bin/clang clang /usr/bin/clang-19 100 && \
    update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-19 100 && \
    update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-19 100 && \
    update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-19 100

# Note: Running as root in CI container is intentional.
# GitHub Actions runners also run as root.
# For local development, docker-ci.sh uses --user to match host UID/GID.
WORKDIR /workspace

# Default command
CMD ["/bin/bash"]
