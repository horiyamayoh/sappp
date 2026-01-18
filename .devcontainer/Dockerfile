# SAP++ Dev Container Environment
#
# VS Code Dev Container 用のDockerfile
# CI環境と同一のツールチェーンを提供しつつ、開発体験を向上
#
FROM ubuntu:24.04

LABEL maintainer="SAP++ Team"
LABEL description="SAP++ Development Container - identical to CI environment"

# Avoid interactive prompts during package installation
ENV DEBIAN_FRONTEND=noninteractive
ENV TZ=UTC

# Install all development dependencies
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
    # Clang Frontend dependencies (optional)
    libclang-18-dev \
    # Node.js for ajv-cli (schema validation)
    nodejs \
    npm \
    # Git for version control
    git \
    # Development utilities
    curl \
    wget \
    jq \
    ccache \
    ripgrep \
    less \
    vim \
    # SSH for git operations
    openssh-client \
    # Man pages for development
    man-db \
    # Locale support
    locales \
    && rm -rf /var/lib/apt/lists/*

# Generate locale
RUN locale-gen en_US.UTF-8
ENV LANG=en_US.UTF-8
ENV LANGUAGE=en_US:en
ENV LC_ALL=en_US.UTF-8

# Install ajv-cli globally for JSON Schema validation
RUN npm install -g ajv-cli ajv-formats

# Set GCC 14 as default
RUN update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 && \
    update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100

# Set Clang 18 tools as alternatives
RUN update-alternatives --install /usr/bin/clang clang /usr/bin/clang-18 100 && \
    update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-18 100 && \
    update-alternatives --install /usr/bin/clangd clangd /usr/bin/clangd-18 100 && \
    update-alternatives --install /usr/bin/clang-format clang-format /usr/bin/clang-format-18 100 && \
    update-alternatives --install /usr/bin/clang-tidy clang-tidy /usr/bin/clang-tidy-18 100

# Create non-root user for development
ARG USERNAME=developer
ARG USER_UID=1000
ARG USER_GID=$USER_UID

RUN groupadd --gid $USER_GID $USERNAME 2>/dev/null || groupmod -n $USERNAME $(getent group $USER_GID | cut -d: -f1) \
    && useradd --uid $USER_UID --gid $USER_GID -m $USERNAME 2>/dev/null || usermod -l $USERNAME -d /home/$USERNAME -m $(getent passwd $USER_UID | cut -d: -f1) \
    && apt-get update \
    && apt-get install -y sudo \
    && echo $USERNAME ALL=\(root\) NOPASSWD:ALL > /etc/sudoers.d/$USERNAME \
    && chmod 0440 /etc/sudoers.d/$USERNAME \
    && rm -rf /var/lib/apt/lists/*

# Set up developer home directory
USER $USERNAME
WORKDIR /home/$USERNAME

# Configure git defaults
RUN git config --global init.defaultBranch main \
    && git config --global core.editor "code --wait" \
    && git config --global pull.rebase false

# Create workspace directory
RUN mkdir -p /home/$USERNAME/workspace
WORKDIR /home/$USERNAME/workspace

# Default shell
ENV SHELL=/bin/bash
CMD ["/bin/bash"]
