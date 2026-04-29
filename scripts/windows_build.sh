#!/bin/bash
# Sunshine-ntlm Windows Build Script (MSYS2 UCRT64)
# Usage: Open "MSYS2 UCRT64" terminal, then run:
#   ./scripts/windows_build.sh
#
# Options:
#   --skip-deps      Skip installing dependencies
#   --skip-build     Skip build, only install deps
#   --skip-package   Skip packaging
#   --clean          Clean build directory before building
#   --release        Build Release instead of RelWithDebInfo

set -e

# Default options
skip_deps=0
skip_build=0
skip_package=0
clean_build=0
build_type="RelWithDebInfo"

# Parse arguments
for arg in "$@"; do
  case $arg in
    --skip-deps)    skip_deps=1 ;;
    --skip-build)   skip_build=1 ;;
    --skip-package) skip_package=1 ;;
    --clean)        clean_build=1 ;;
    --release)      build_type="Release" ;;
    --help|-h)
      echo "Usage: $0 [--skip-deps] [--skip-build] [--skip-package] [--clean] [--release]"
      exit 0
      ;;
    *)
      echo "Unknown option: $arg"
      exit 1
      ;;
  esac
done

# Check we are in MSYS2 UCRT64
if [[ "$MSYSTEM" != "UCRT64" ]]; then
  echo "ERROR: This script must be run in MSYS2 UCRT64 terminal."
  echo "Please open 'MSYS2 UCRT64' from the Start Menu and try again."
  exit 1
fi

# Get script directory and project root
script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
project_dir="$(cd "$script_dir/.." && pwd)"

echo "========================================="
echo " Sunshine-ntlm Windows Build"
echo "========================================="
echo "Project: $project_dir"
echo "Build type: $build_type"
echo ""

# ---- Step 1: Update MSYS2 ----
if [[ $skip_deps -eq 0 ]]; then
  echo "[1/4] Updating MSYS2 packages..."
  pacman -Syu --noconfirm

  # ---- Step 2: Install dependencies ----
  echo "[2/4] Installing dependencies..."
  dependencies=(
    "git"
    "mingw-w64-ucrt-x86_64-cmake"
    "mingw-w64-ucrt-x86_64-cppwinrt"
    "mingw-w64-ucrt-x86_64-curl-winssl"
    "mingw-w64-ucrt-x86_64-graphviz"
    "mingw-w64-ucrt-x86_64-MinHook"
    "mingw-w64-ucrt-x86_64-miniupnpc"
    "mingw-w64-ucrt-x86_64-nlohmann-json"
    "mingw-w64-ucrt-x86_64-nodejs"
    "mingw-w64-ucrt-x86_64-nsis"
    "mingw-w64-ucrt-x86_64-ninja"
    "mingw-w64-ucrt-x86_64-onevpl"
    "mingw-w64-ucrt-x86_64-openssl"
    "mingw-w64-ucrt-x86_64-opus"
    "mingw-w64-ucrt-x86_64-toolchain"
  )
  pacman -S --needed --noconfirm "${dependencies[@]}"
  echo "Dependencies installed."
else
  echo "[1/4] Skipping MSYS2 update (--skip-deps)"
  echo "[2/4] Skipping dependency install (--skip-deps)"
fi

# ---- Step 3: Init submodules ----
echo "[3/4] Checking submodules..."
cd "$project_dir"
if [[ -z "$(ls -A third-party/moonlight-common-c/enet 2>/dev/null)" ]]; then
  echo "Initializing submodules..."
  git submodule update --init --recursive
else
  echo "Submodules already initialized."
fi

# ---- Step 4: Build ----
if [[ $skip_build -eq 0 ]]; then
  echo "[4/4] Building Sunshine..."

  if [[ $clean_build -eq 1 && -d build ]]; then
    echo "Cleaning build directory..."
    rm -rf build
  fi

  mkdir -p build

  cmake \
    -B build \
    -G Ninja \
    -S . \
    -DCMAKE_BUILD_TYPE="$build_type" \
    -DSUNSHINE_ASSETS_DIR=assets

  ninja -C build

  echo ""
  echo "========================================="
  echo " Build complete!"
  echo "========================================="

  # ---- Package ----
  if [[ $skip_package -eq 0 ]]; then
    echo "Packaging..."
    cd build

    echo "Creating NSIS installer..."
    cpack -G NSIS || echo "WARNING: NSIS packaging failed (nsis may not be installed)"

    echo "Creating ZIP portable..."
    cpack -G ZIP || echo "WARNING: ZIP packaging failed"

    cd "$project_dir"

    echo ""
    echo "========================================="
    echo " Packages created in build/cpack_artifacts/"
    echo "========================================="
  fi
else
  echo "[4/4] Skipping build (--skip-build)"
fi

echo ""
echo "Done!"
