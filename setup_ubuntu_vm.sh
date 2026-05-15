#!/usr/bin/env bash

set -euo pipefail

REPO_URL="${REPO_URL:-https://github.com/VladChira/hybrid-renderer.git}"
REPO_BRANCH="${REPO_BRANCH:-main}"
CHECKOUT_DIR="${CHECKOUT_DIR:-$HOME/hybrid-renderer}"
VCPKG_DIR="${VCPKG_DIR:-$HOME/vcpkg}"
BUILD_PRESET="${BUILD_PRESET:-linux-ninja}"

log() {
  printf '\n[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*"
}

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

log "Updating apt metadata"
sudo apt-get update

log "Installing system dependencies"
sudo apt-get install -y \
  build-essential \
  ca-certificates \
  cmake \
  curl \
  git \
  libgl1-mesa-dev \
  libwayland-dev \
  libx11-dev \
  libxcursor-dev \
  libxi-dev \
  libxinerama-dev \
  libxkbcommon-dev \
  libxrandr-dev \
  libxxf86vm-dev \
  mesa-utils \
  ninja-build \
  pkg-config \
  tar \
  unzip \
  zip

log "Checking OpenGL availability"
if command -v glxinfo >/dev/null 2>&1; then
  if [ -n "${DISPLAY:-}" ]; then
    if glxinfo >/tmp/hybrid_glxinfo.txt 2>/tmp/hybrid_glxinfo.err; then
      renderer="$(grep -m1 'OpenGL renderer string' /tmp/hybrid_glxinfo.txt || true)"
      version="$(grep -m1 'OpenGL version string' /tmp/hybrid_glxinfo.txt || true)"
      vendor="$(grep -m1 'OpenGL vendor string' /tmp/hybrid_glxinfo.txt || true)"
      log "OpenGL check passed"
      printf '%s\n' "${vendor:-OpenGL vendor string not reported}"
      printf '%s\n' "${renderer:-OpenGL renderer string not reported}"
      printf '%s\n' "${version:-OpenGL version string not reported}"
    else
      log "OpenGL check could not run via glxinfo; continuing anyway"
      cat /tmp/hybrid_glxinfo.err || true
    fi
  else
    log "DISPLAY is not set; skipping live OpenGL probe and continuing"
  fi
else
  log "glxinfo is unavailable; skipping OpenGL probe and continuing"
fi

if [ ! -d "$VCPKG_DIR/.git" ]; then
  log "Cloning vcpkg into $VCPKG_DIR"
  git clone https://github.com/microsoft/vcpkg.git "$VCPKG_DIR"
else
  log "vcpkg already exists at $VCPKG_DIR"
fi

log "Bootstrapping vcpkg"
"$VCPKG_DIR/bootstrap-vcpkg.sh"

export VCPKG_ROOT="$VCPKG_DIR"

if ! grep -Fq 'export VCPKG_ROOT=' "$HOME/.bashrc" 2>/dev/null; then
  log "Persisting VCPKG_ROOT in ~/.bashrc"
  printf '\nexport VCPKG_ROOT="%s"\n' "$VCPKG_DIR" >> "$HOME/.bashrc"
fi

if [ ! -d "$CHECKOUT_DIR/.git" ]; then
  log "Cloning repository into $CHECKOUT_DIR"
  git clone --branch "$REPO_BRANCH" --recurse-submodules "$REPO_URL" "$CHECKOUT_DIR"
else
  log "Repository already exists at $CHECKOUT_DIR"
fi

require_cmd cmake
require_cmd ninja
require_cmd git

log "Synchronizing submodules"
git -C "$CHECKOUT_DIR" submodule sync --recursive
git -C "$CHECKOUT_DIR" submodule update --init --recursive

log "Configuring with preset $BUILD_PRESET"
cmake --preset "$BUILD_PRESET" -S "$CHECKOUT_DIR"

log "Building with preset $BUILD_PRESET"
cmake --build --preset "$BUILD_PRESET" -S "$CHECKOUT_DIR"

log "Setup complete"
cat <<EOF

Repo:        $CHECKOUT_DIR
vcpkg:       $VCPKG_DIR
VCPKG_ROOT:  $VCPKG_ROOT

Next steps:
1. Copy your assets directory into: $CHECKOUT_DIR/assets
2. Run the editor from: $CHECKOUT_DIR/out/build/$BUILD_PRESET/bin

EOF
