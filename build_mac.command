#!/bin/bash
set -e
cd "$(dirname "$0")"
echo "Lil God Projector — configuring..."
command -v cmake >/dev/null || { echo "Install CMake: brew install cmake"; exit 1; }

# Xcode generator breaks on paths with parentheses: iCloud Drive (Archive)
JUCE_DIR="${JUCE_DIR:-$HOME/JUCE-8.0.4}"
BUILD_DIR="${BUILD_DIR:-$HOME/GrooveLab-build}"
# Also replace the app the Dock usually launches.
LAUNCH_APP="$HOME/GrooveLabNative-build/GrooveLab_artefacts/Release/Lil God Projector.app"
if [ ! -f "$JUCE_DIR/CMakeLists.txt" ]; then
  echo "Downloading JUCE 8.0.4 to $JUCE_DIR ..."
  git clone --branch 8.0.4 --depth 1 https://github.com/juce-framework/JUCE.git "$JUCE_DIR"
fi

cmake -S . -B "$BUILD_DIR" -G "Unix Makefiles" \
  -DCMAKE_BUILD_TYPE=Release \
  -DFETCHCONTENT_SOURCE_DIR_JUCE="$JUCE_DIR"
cmake --build "$BUILD_DIR" --target GrooveLab -j"$(sysctl -n hw.ncpu)"

APP="$BUILD_DIR/GrooveLab_artefacts/Release/Lil God Projector.app"
if [ -d "$APP" ]; then
  echo "Built: $APP"
  if [ -n "${LAUNCH_APP:-}" ]; then
    mkdir -p "$(dirname "$LAUNCH_APP")"
    rm -rf "$LAUNCH_APP"
    cp -R "$APP" "$LAUNCH_APP"
    echo "Installed: $LAUNCH_APP"
    open "$LAUNCH_APP"
  else
    open "$APP"
  fi
else
  echo "Build finished. App not found at $APP"
fi
