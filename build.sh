#!/usr/bin/env bash
# On-device build for LeviBatone using the Termux clang (targets the same
# arm64 Android runtime as the game). For the canonical NDK build use xmake
# (see .github/workflows/build.yml).
#
#   ./build.sh            build .so + package .levipack
#   ./build.sh --so-only  build the .so without packaging
#
# Requirements: pkg install clang; preloader headers via $PRELOADER_INCLUDE
# or a checkout at ../sdk-preloader (git clone --depth 1
# https://github.com/LiteLDev/preloader-android).
set -euo pipefail

cd "$(dirname "$0")"

OUT_DIR=build
SO_NAME=liblevibatone.so
PRELOADER_INCLUDE="${PRELOADER_INCLUDE:-../sdk-preloader/include}"

if [ ! -d "$PRELOADER_INCLUDE/pl" ]; then
    echo "error: preloader headers not found at $PRELOADER_INCLUDE" >&2
    echo "       git clone --depth 1 https://github.com/LiteLDev/preloader-android ../sdk-preloader" >&2
    echo "       or set PRELOADER_INCLUDE=<path to preloader-android/include>" >&2
    exit 1
fi

for dep in third_party/fmt/include third_party/entt/src; do
    if [ ! -d "$dep" ]; then
        echo "error: missing vendored dependency $dep (see README, clone fmt + entt into third_party)" >&2
        exit 1
    fi
done

SOURCES=$(find src -name '*.cpp' | sort)

mkdir -p "$OUT_DIR"
clang++ -std=c++20 -shared -fPIC -O2 -w \
    -fno-rtti -fexceptions \
    -fvisibility=hidden -fvisibility-inlines-hidden \
    -DFMT_HEADER_ONLY=1 \
    -DENTT_PACKED_PAGE=128 -DENTT_SPARSE_PAGE=2048 \
    -Iinclude -Isrc \
    -Ithird_party/fmt/include \
    -Ithird_party/entt/src \
    -I"$PRELOADER_INCLUDE" \
    $SOURCES \
    -llog \
    -Wl,--gc-sections \
    -Wl,--unresolved-symbols=ignore-all \
    -Wl,-z,max-page-size=16384 \
    -o "$OUT_DIR/$SO_NAME"

echo "built $OUT_DIR/$SO_NAME"

if [ "${1:-}" != "--so-only" ]; then
    python3 scripts/package_levipack.py \
        --library "$OUT_DIR/$SO_NAME" \
        --icon assets/levibatone.png \
        --font resources/minecraft.ttf \
        --version-header include/levibatone/Version.hpp \
        --output "$OUT_DIR/LeviBatone.levipack"
fi
