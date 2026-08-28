#!/bin/bash
set -e
cd "$(dirname "$0")"
echo "Groove Lab v0.6 — configuring..."
command -v cmake >/dev/null || { echo "Install CMake: brew install cmake"; exit 1; }
command -v xcodebuild >/dev/null || { echo "Install Xcode command line tools: xcode-select --install"; exit 1; }
cmake -S . -B build -G Xcode
cmake --build build --config Release --target GrooveLab
open build/GrooveLab.xcodeproj
