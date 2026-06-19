#!/usr/bin/env bash

set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
CHECKOUT_DIR="${CHECKOUT_DIR:-$SCRIPT_DIR}"
VCPKG_DIR="${VCPKG_DIR:-$HOME/vcpkg}"
BUILD_PRESET="${BUILD_PRESET:-linux-ninja}"
CMAKE_VERSION="${CMAKE_VERSION:-3.31.8}"
TRACY_REPO_URL="${TRACY_REPO_URL:-https://github.com/wolfpld/tracy.git}"
TRACY_TAG="${TRACY_TAG:-v0.13.1}"
TRACY_DIR="${TRACY_DIR:-$HOME/tracy}"
TRACY_BUILD_DIR="${TRACY_BUILD_DIR:-$TRACY_DIR/out/build/linux-release}"

log() {
  printf '\n[%s] %s\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$*"
}

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

resolve_cmake_arch() {
  case "$(uname -m)" in
    x86_64|amd64)
      printf 'x86_64'
      ;;
    aarch64|arm64)
      printf 'aarch64'
      ;;
    *)
      echo "Unsupported architecture for Kitware CMake installer: $(uname -m)" >&2
      exit 1
      ;;
  esac
}

install_cmake() {
  local requested_version="$1"
  local install_root="/opt/cmake-${requested_version}"

  if command -v cmake >/dev/null 2>&1; then
    local current_version
    current_version="$(cmake --version | awk 'NR==1 { print $3 }')"
    if [ "$current_version" = "$requested_version" ]; then
      log "CMake $requested_version already installed"
      return
    fi
  fi

  local arch
  arch="$(resolve_cmake_arch)"
  local installer_name="cmake-${requested_version}-linux-${arch}.sh"
  local installer_url="https://github.com/Kitware/CMake/releases/download/v${requested_version}/${installer_name}"
  local installer_path="/tmp/${installer_name}"
  local install_bin_root="$install_root"

  log "Installing CMake ${requested_version} from GitHub release"
  curl -L --fail --retry 3 -o "$installer_path" "$installer_url"
  chmod +x "$installer_path"
  sudo mkdir -p "$install_root"
  sudo "$installer_path" --skip-license --exclude-subdir --prefix="$install_root"

  # Older installs may still have the default nested layout:
  # /opt/cmake-<version>/cmake-<version>-linux-<arch>/bin
  if [ ! -x "${install_bin_root}/bin/cmake" ]; then
    local nested_install_root
    nested_install_root="$(find "$install_root" -mindepth 1 -maxdepth 1 -type d -name "cmake-${requested_version}-linux-*" | head -n 1 || true)"
    if [ -n "$nested_install_root" ] && [ -x "${nested_install_root}/bin/cmake" ]; then
      install_bin_root="$nested_install_root"
    fi
  fi

  if [ ! -x "${install_bin_root}/bin/cmake" ]; then
    echo "CMake ${requested_version} install succeeded, but no cmake binary was found under ${install_root}" >&2
    exit 1
  fi

  sudo ln -sf "${install_bin_root}/bin/cmake" /usr/local/bin/cmake
  sudo ln -sf "${install_bin_root}/bin/ctest" /usr/local/bin/ctest
  sudo ln -sf "${install_bin_root}/bin/cpack" /usr/local/bin/cpack
}

log "Updating apt metadata"
sudo apt-get update

log "Installing system dependencies"
sudo apt-get install -y \
  build-essential \
  ca-certificates \
  curl \
  git \
  libgl1-mesa-dev \
  libglm-dev \
  libglfw3-dev \
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

install_cmake "$CMAKE_VERSION"

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
  echo "Expected an existing repository checkout at: $CHECKOUT_DIR" >&2
  exit 1
fi

require_cmd cmake
require_cmd ninja
require_cmd git

log "Synchronizing submodules"
git -C "$CHECKOUT_DIR" submodule sync --recursive
git -C "$CHECKOUT_DIR" submodule update --init --recursive

if [ ! -d "$TRACY_DIR/.git" ]; then
  log "Cloning Tracy into $TRACY_DIR"
  git clone "$TRACY_REPO_URL" "$TRACY_DIR"
else
  log "Tracy already exists at $TRACY_DIR"
fi

log "Checking out Tracy $TRACY_TAG"
git -C "$TRACY_DIR" fetch --tags
git -C "$TRACY_DIR" checkout "$TRACY_TAG"

log "Configuring Tracy standalone profiler"
cmake -S "$TRACY_DIR/profiler" -B "$TRACY_BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release -DLEGACY=ON -DWAYLAND=OFF

log "Building Tracy standalone profiler"
cmake --build "$TRACY_BUILD_DIR"

log "Configuring with preset $BUILD_PRESET"
(
  cd "$CHECKOUT_DIR"
  cmake --preset "$BUILD_PRESET"
)

log "Building with preset $BUILD_PRESET"
(
  cd "$CHECKOUT_DIR"
  cmake --build --preset "$BUILD_PRESET"
)

log "Setup complete"
cat <<EOF

Repo:        $CHECKOUT_DIR
vcpkg:       $VCPKG_DIR
VCPKG_ROOT:  $VCPKG_ROOT
Tracy:       $TRACY_DIR
Tracy build: $TRACY_BUILD_DIR

Next steps:
1. Copy your assets directory into: $CHECKOUT_DIR/assets
2. Run the editor from: $CHECKOUT_DIR/out/build/$BUILD_PRESET/bin
3. Run the Tracy profiler from: $TRACY_BUILD_DIR/tracy-profiler

EOF
