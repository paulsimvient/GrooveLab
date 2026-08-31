#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"

echo "Lil God Projector v1.6.1 — piano roll JUCE 8 compile fix"

if ! command -v cmake >/dev/null 2>&1; then
  echo "CMake is required. Install it with: brew install cmake"
  exit 1
fi
if ! command -v git >/dev/null 2>&1; then
  echo "git is required. Install Xcode Command Line Tools with: xcode-select --install"
  exit 1
fi
if ! xcode-select -p >/dev/null 2>&1; then
  echo "Xcode Command Line Tools are required. Run: xcode-select --install"
  exit 1
fi

JUCE_DIR="${JUCE_DIR:-$HOME/JUCE-8.0.4}"
BUILD_DIR="${BUILD_DIR:-$HOME/LilGodProjector-v1.8.3-build}"

if [ ! -f "$JUCE_DIR/CMakeLists.txt" ]; then
  echo "Fetching JUCE 8.0.4 into: $JUCE_DIR"
  rm -rf "$JUCE_DIR"
  git clone --branch 8.0.4 --depth 1 https://github.com/juce-framework/JUCE.git "$JUCE_DIR"
fi

echo "Removing stale CMake state from: $BUILD_DIR"
rm -rf "$BUILD_DIR"

cmake -S . -B "$BUILD_DIR" -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_JUCE="$JUCE_DIR"

JOBS="$(sysctl -n hw.ncpu 2>/dev/null || echo 4)"
cmake --build "$BUILD_DIR" --target GrooveLab -j"$JOBS"

APP="$BUILD_DIR/GrooveLab_artefacts/Release/Lil God Projector.app"
if [ ! -d "$APP" ]; then
  echo "Build completed, but the app bundle was not found at:"
  echo "$APP"
  exit 2
fi

echo "Built successfully: $APP"

# Do not accidentally leave an older build running.
pkill -x "Lil God Projector" >/dev/null 2>&1 || true
sleep 0.4
open -n "$APP"
