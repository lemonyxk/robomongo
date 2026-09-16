#!/usr/bin/env bash
# Install pinned build dependencies inside this checkout. No sudo/global install.
set -euo pipefail

if [[ "${1:-}" == "--help" ]]; then
    echo "Usage: bash scripts/bootstrap-macos.sh"
    echo "Requires Apple Silicon macOS, Xcode command-line tools, Python 3.10+, CMake 3.22+."
    echo "Optional: ROBOMONGO_JOBS (default 6). Downloads and builds are cached in build/deps."
    exit 0
fi

repo_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
deps_dir="$repo_dir/build/deps"
modern_dir="$deps_dir/modern"
deps_prefix="$modern_dir/prefix"
downloads_dir="$modern_dir/downloads"
logs_dir="$repo_dir/build/logs"
manifest="$repo_dir/scripts/dependencies-macos.json"
jobs="${ROBOMONGO_JOBS:-6}"
[[ "$(uname -s)" == Darwin && "$(uname -m)" == arm64 ]] || {
    echo "Run this script in a native Apple Silicon macOS terminal." >&2
    exit 1
}
for tool in python3 cmake curl tar make perl xcrun; do
    command -v "$tool" >/dev/null || { echo "Missing prerequisite: $tool" >&2; exit 1; }
done
xcrun --find clang >/dev/null
python3 -c 'import sys; assert sys.version_info >= (3, 10), "Python 3.10 or newer is required"'
mkdir -p "$downloads_dir" "$deps_prefix" "$logs_dir"

field() {
    python3 - "$manifest" "$1" "$2" <<'PY'
import json, sys
data = json.load(open(sys.argv[1]))
item = data["qt"] if sys.argv[2] == "qt" else data["archives"][sys.argv[2]]
print(item[sys.argv[3]])
PY
}

verify_archive() {
    python3 - "$1" "$2" <<'PY'
import hashlib, pathlib, sys
path = pathlib.Path(sys.argv[1])
digest = hashlib.sha256()
with path.open('rb') as source:
    for block in iter(lambda: source.read(1024 * 1024), b''):
        digest.update(block)
if digest.hexdigest() != sys.argv[2]:
    raise SystemExit(f'SHA-256 mismatch: {path}. Remove this cached archive and retry.')
PY
}

extract_dependency() {
    local name="$1" target_dir="$2" archive directory url checksum
    directory="$(field "$name" directory)"
    archive="$downloads_dir/$(field "$name" filename)"
    [[ -d "$target_dir/$directory" ]] && return
    url="$(field "$name" url)"
    checksum="$(field "$name" sha256)"
    if [[ ! -f "$archive" ]]; then
        echo "Downloading $name $(field "$name" version)..."
        curl --fail --location --retry 3 --connect-timeout 30 "$url" --output "$archive.part"
        verify_archive "$archive.part" "$checksum"
        mv "$archive.part" "$archive"
    else
        verify_archive "$archive" "$checksum"
    fi
    mkdir -p "$target_dir"
    tar -xzf "$archive" -C "$target_dir"
}

run_logged() {
    local name="$1"
    shift
    echo "$name (log: build/logs/$name.log)"
    if ! "$@" >"$logs_dir/$name.log" 2>&1; then
        tail -80 "$logs_dir/$name.log" >&2
        return 1
    fi
}

qt_version="$(field qt version)"
qt_prefix="$deps_dir/Qt6/$qt_version/macos"
if [[ ! -x "$qt_prefix/bin/qmake" ]] || [[ "$("$qt_prefix/bin/qmake" -query QT_VERSION)" != "$qt_version" ]]; then
    if [[ ! -x "$deps_dir/qt-venv/bin/python" ]]; then
        python3 -m venv "$deps_dir/qt-venv"
    fi
    run_logged qt6-aqt-bootstrap "$deps_dir/qt-venv/bin/python" -m pip install "aqtinstall==$(field qt aqtinstall)"
    # An absolute output path is required so qmake's relocated prefix is valid.
    (
        cd "$logs_dir"
        run_logged qt6-install "$deps_dir/qt-venv/bin/python" -m aqt install-qt \
            mac desktop "$qt_version" clang_64 --outputdir "$deps_dir/Qt6" \
            --archives qtbase qttools qtsvg --timeout 30
    )
fi
lipo "$qt_prefix/lib/QtCore.framework/QtCore" -verify_arch arm64

extract_dependency qscintilla "$deps_dir"
qsci_source="$deps_dir/$(field qscintilla directory)"
qsci_build="$deps_dir/qscintilla-arm64"
if [[ ! -f "$qsci_build/libqscintilla2_qt6.a" ]]; then
    mkdir -p "$qsci_build"
    run_logged qscintilla-configure "$qt_prefix/bin/qmake" "$qsci_source/src/qscintilla.pro" \
        CONFIG+=release CONFIG-=debug CONFIG+=staticlib QMAKE_APPLE_DEVICE_ARCHS=arm64 \
        QMAKE_CXXFLAGS+=-fPIC -o "$qsci_build/Makefile"
    run_logged qscintilla-build make -C "$qsci_build" -j"$jobs"
fi
lipo "$qsci_build/libqscintilla2_qt6.a" -verify_arch arm64

extract_dependency openssl "$modern_dir"
openssl_source="$modern_dir/$(field openssl directory)"
if [[ ! -f "$deps_prefix/.openssl-4.0.2-arm64-macos13.5" || ! -f "$deps_prefix/lib/libssl.a" || ! -f "$deps_prefix/include/openssl/opensslv.h" ]] || \
   ! grep -q 'OPENSSL_VERSION_STR "4.0.2"' "$deps_prefix/include/openssl/opensslv.h"; then
    (
        cd "$openssl_source"
        export MACOSX_DEPLOYMENT_TARGET=13.5
        run_logged openssl4-configure perl ./Configure darwin64-arm64-cc \
            "--prefix=$deps_prefix" --openssldir=/etc/ssl no-shared no-tests -mmacosx-version-min=13.5
        run_logged openssl4-build make -j"$jobs"
        run_logged openssl4-install make install_sw
        touch "$deps_prefix/.openssl-4.0.2-arm64-macos13.5"
    )
fi
lipo "$deps_prefix/lib/libssl.a" -verify_arch arm64

common_cmake=(-DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
    -DCMAKE_OSX_DEPLOYMENT_TARGET=13.5 "-DCMAKE_INSTALL_PREFIX=$deps_prefix"
    -DCMAKE_POSITION_INDEPENDENT_CODE=ON -DCMAKE_EXPORT_NO_PACKAGE_REGISTRY=ON
    "-DOPENSSL_ROOT_DIR=$deps_prefix" -DOPENSSL_USE_STATIC_LIBS=ON
    "-DOPENSSL_INCLUDE_DIR=$deps_prefix/include"
    "-DOPENSSL_SSL_LIBRARY=$deps_prefix/lib/libssl.a"
    "-DOPENSSL_CRYPTO_LIBRARY=$deps_prefix/lib/libcrypto.a")

extract_dependency mongoc "$modern_dir"
if [[ ! -f "$deps_prefix/lib/cmake/mongoc-2.5.3/mongocConfig.cmake" || ! -f "$deps_prefix/lib/libmongoc2.a" ]]; then
    run_logged mongo-c-configure cmake -S "$modern_dir/$(field mongoc directory)" \
        -B "$modern_dir/mongo-c-build" "${common_cmake[@]}" \
        -DENABLE_STATIC=ON -DENABLE_SHARED=OFF -DENABLE_STATIC_LIBBSON_INSTALL=ON \
        -DENABLE_SSL=OPENSSL -DENABLE_SASL=OFF -DENABLE_CLIENT_SIDE_ENCRYPTION=OFF \
        -DENABLE_SNAPPY=OFF -DENABLE_ZLIB=OFF -DENABLE_ZSTD=OFF \
        -DENABLE_TESTS=OFF -DENABLE_EXAMPLES=OFF
    run_logged mongo-c-build cmake --build "$modern_dir/mongo-c-build" --parallel "$jobs"
    run_logged mongo-c-install cmake --install "$modern_dir/mongo-c-build"
fi
lipo "$deps_prefix/lib/libmongoc2.a" -verify_arch arm64

extract_dependency libssh2 "$modern_dir"
if [[ ! -f "$deps_prefix/lib/libssh2.a" || ! -f "$deps_prefix/include/libssh2.h" ]] || \
   ! grep -Eq '^#define LIBSSH2_VERSION +"1\.11\.1"' "$deps_prefix/include/libssh2.h"; then
    run_logged libssh2-configure cmake -S "$modern_dir/$(field libssh2 directory)" \
        -B "$modern_dir/libssh2-build" "${common_cmake[@]}" \
        -DBUILD_STATIC_LIBS=ON -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF \
        -DBUILD_EXAMPLES=OFF -DCRYPTO_BACKEND=OpenSSL -DENABLE_ZLIB_COMPRESSION=OFF
    run_logged libssh2-build cmake --build "$modern_dir/libssh2-build" --parallel "$jobs"
    run_logged libssh2-install cmake --install "$modern_dir/libssh2-build"
fi
lipo "$deps_prefix/lib/libssh2.a" -verify_arch arm64

# GoogleTest is compiled by the application's CMake build.
extract_dependency googletest "$modern_dir"
extract_dependency node "$modern_dir"
node_prefix="$modern_dir/$(field node directory)"
[[ "$("$node_prefix/bin/node" --version)" == "v$(field node version)" ]]
[[ "$("$node_prefix/bin/node" -p process.arch)" == arm64 ]]

runtime_source="$repo_dir/src/robomongo/runtime"
runtime_cache="$deps_dir/mongosh-runtime"
mkdir -p "$runtime_cache"
lock_hash="$(cat "$runtime_source/package.json" "$runtime_source/package-lock.json" | shasum -a 256 | cut -d ' ' -f 1)"
if [[ ! -f "$runtime_cache/.lock-sha256" || ! -d "$runtime_cache/node_modules/@mongosh/shell-api" ]] || \
   [[ "$(cat "$runtime_cache/.lock-sha256" 2>/dev/null || true)" != "$lock_hash" ]]; then
    cp "$runtime_source/package.json" "$runtime_source/package-lock.json" "$runtime_cache/"
    (
        export PATH="$node_prefix/bin:$PATH"
        cd "$runtime_cache"
        run_logged mongosh-npm-ci npm ci --omit=dev --no-audit --no-fund --cache "$deps_dir/npm-cache"
    )
    printf '%s\n' "$lock_hash" > "$runtime_cache/.lock-sha256"
fi
echo "Native dependencies are ready in build/deps."
