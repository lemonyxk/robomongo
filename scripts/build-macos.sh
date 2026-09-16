#!/usr/bin/env bash
# Reproducible native Apple Silicon Release build, tests, and app packaging.
set -euo pipefail
if [[ "${1:-}" == "--help" ]]; then
    echo "Usage: bash scripts/build-macos.sh [extra CMake configure arguments...]"
    echo "Optional: ROBOMONGO_JOBS=6, ROBOMONGO_BUILD_DIR=/absolute/build/path"
    echo "Default output: build/arm64/install/Robo 3T.app"
    exit 0
fi
repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
build_dir="${ROBOMONGO_BUILD_DIR:-$repo_dir/build/arm64}"
jobs="${ROBOMONGO_JOBS:-6}"
bash "$repo_dir/scripts/bootstrap-macos.sh"
mkdir -p "$build_dir" "$repo_dir/build/logs"
cmake -S "$repo_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 -DCMAKE_OSX_DEPLOYMENT_TARGET=13.5 \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DBUILD_TESTING=ON \
    "-DCMAKE_INSTALL_PREFIX=$build_dir/install" "$@" \
    2>&1 | tee "$repo_dir/build/logs/arm64-configure.log"
cmake --build "$build_dir" --parallel "$jobs" \
    2>&1 | tee "$repo_dir/build/logs/arm64-build.log"
ctest --test-dir "$build_dir" --output-on-failure \
    2>&1 | tee "$repo_dir/build/logs/arm64-tests.log"
cmake --install "$build_dir" 2>&1 | tee "$repo_dir/build/logs/arm64-install.log"
printf '\nBuilt: %s/install/Robo 3T.app\n' "$build_dir"
