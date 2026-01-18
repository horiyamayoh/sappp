# SAP++ CI Environment
#
# This Dockerfile provides an identical environment to GitHub Actions CI.
# Use this for local development to ensure changes pass CI before pushing.
#
# Build:
#   docker build -t sappp-ci docker/
#
# Usage:
#   docker run --rm -v $(pwd):/workspace -w /workspace sappp-ci ./scripts/ci-check.sh
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
    # Clang 18 (C++23)
    clang-18 \
    clang-format-18 \
    clang-tidy-18 \
    clangd-18 \
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
    && rm -rf /var/lib/apt/lists/*

# Install ajv-cli globally for JSON Schema validation
RUN npm install -g ajv-cli ajv-formats

# Set GCC 14 as default
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100

# Create non-root user for better security
RUN useradd -m -s /bin/bash developer
USER developer
WORKDIR /workspace

# Default command
CMD ["/bin/bash"]
