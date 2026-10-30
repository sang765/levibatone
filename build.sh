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

SOURCES=$(find src -name '*.cpp' ! -name 'test_*.cpp' | sort)

mkdir -p "$OUT_DIR"

# Stub preloader: the mod links DT_NEEDED libpreloader.so, which the runtime
# linker resolves to the launcher's already-loaded libpreloader.so (same
# soname, loaded by ModManager.ensurePreloaderLoaded before mods).
printf 'void __levibatone_preloader_stub(void) {}\n' > "$OUT_DIR/preloader_stub.cpp"
clang++ -std=c++20 -shared -fPIC -O0 -w \
    -Wl,-soname,libpreloader.so \
    -o "$OUT_DIR/libpreloader.so" "$OUT_DIR/preloader_stub.cpp"

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
    -L"$OUT_DIR" -lpreloader \
    -llog \
    -Wl,--gc-sections \
    -Wl,--unresolved-symbols=ignore-all \
    -Wl,-z,max-page-size=16384 \
    -o "$OUT_DIR/$SO_NAME"

# Own symbols must all be defined; only preloader/system symbols may stay UND.
if readelf --dyn-syms -W "$OUT_DIR/$SO_NAME" | awk '$7=="UND" {print $8}' | grep -q levibatone; then
    echo "error: undefined levibatone symbols:" >&2
    readelf --dyn-syms -W "$OUT_DIR/$SO_NAME" | awk '$7=="UND" {print $8}' | grep levibatone >&2
    exit 1
fi

echo "built $OUT_DIR/$SO_NAME"

if [ "${1:-}" != "--so-only" ]; then
    python3 scripts/package_levipack.py \
        --library "$OUT_DIR/$SO_NAME" \
        --icon assets/levibatone.png \
        --font resources/minecraft.ttf \
        --version-header include/levibatone/Version.hpp \
        --output "$OUT_DIR/LeviBatone.levipack"
fi
