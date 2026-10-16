#!/bin/sh
# Build pinned, upstream 3DS packaging tools into this project.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
TOOLS_DIR="$ROOT/tools"
SRC_DIR="$TOOLS_DIR/build/src"
BIN_DIR="$TOOLS_DIR/bin"
LICENSE_DIR="$TOOLS_DIR/licenses"

BANNERTOOL_REPO=https://github.com/carstene1ns/3ds-bannertool.git
BANNERTOOL_COMMIT=734d33be79fd3f8c29c6296158f06ac7c5ca9dcb
PROJECT_CTR_REPO=https://github.com/3DSGuy/Project_CTR.git
PROJECT_CTR_COMMIT=e8f5f529c54ff9b22a2491a480ffa69206bf7b19
PROJECT_CTR_PATCH="$ROOT/packaging/patches/project-ctr-makerom-version-macros.patch"

checkout() {
    repository=$1
    commit=$2
    destination=$3
    if [ ! -d "$destination/.git" ]; then
        rm -rf "$destination"
        git clone --filter=blob:none --no-checkout "$repository" "$destination"
    fi
    git -C "$destination" fetch --depth 1 origin "$commit"
    git -C "$destination" checkout --detach --force "$commit"
    test "$(git -C "$destination" rev-parse HEAD)" = "$commit"
}

command -v cmake >/dev/null 2>&1 || {
    echo "cmake 3.28 or newer is required (Debian: apt-get install cmake)" >&2
    exit 1
}
command -v git >/dev/null 2>&1 || { echo "git is required" >&2; exit 1; }
command -v make >/dev/null 2>&1 || { echo "make is required" >&2; exit 1; }

mkdir -p "$SRC_DIR" "$BIN_DIR" "$LICENSE_DIR"
checkout "$BANNERTOOL_REPO" "$BANNERTOOL_COMMIT" "$SRC_DIR/bannertool"
checkout "$PROJECT_CTR_REPO" "$PROJECT_CTR_COMMIT" "$SRC_DIR/Project_CTR"
test -f "$PROJECT_CTR_PATCH"
git -C "$SRC_DIR/Project_CTR" apply --check "$PROJECT_CTR_PATCH"
git -C "$SRC_DIR/Project_CTR" apply "$PROJECT_CTR_PATCH"

cmake -S "$SRC_DIR/bannertool" -B "$SRC_DIR/bannertool/build" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$TOOLS_DIR"
cmake --build "$SRC_DIR/bannertool/build" --parallel
cmake --install "$SRC_DIR/bannertool/build"

(
    cd "$SRC_DIR/Project_CTR/makerom"
    make clean clean_deps
    make deps
    make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf 1)"
)
install -m 0755 "$SRC_DIR/Project_CTR/makerom/bin/makerom" "$BIN_DIR/makerom"

(
    cd "$SRC_DIR/Project_CTR/ctrtool"
    make clean clean_deps
    make deps
    make -j"$(getconf _NPROCESSORS_ONLN 2>/dev/null || printf 1)"
)
install -m 0755 "$SRC_DIR/Project_CTR/ctrtool/bin/ctrtool" "$BIN_DIR/ctrtool"

install -m 0644 "$SRC_DIR/bannertool/LICENSE.txt" "$LICENSE_DIR/bannertool-MIT.txt"
install -m 0644 "$SRC_DIR/Project_CTR/makerom/LICENSE" "$LICENSE_DIR/makerom-MIT.txt"
# Project_CTR currently has no standalone ctrtool license file; its vendored
# dependency licenses remain in the pinned source checkout.

"$BIN_DIR/bannertool" --version
"$BIN_DIR/makerom" -help | sed -n '1,2p'
"$BIN_DIR/ctrtool" --help 2>&1 | sed -n '1,2p'
