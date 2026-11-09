#!/bin/bash
# build_rizin.sh — build rizin + rz-ghidra (decompiler) from source into $OPT
# No root needed: meson/ninja/cmake come from pip. Run from anywhere.
# Usage: nohup bash /home/z/my-project/scripts/build_rizin.sh > /home/z/my-project/scripts/rizin_build.log 2>&1 &
set -ex

MYPROJECT=/home/z/my-project
OPT=$MYPROJECT/opt
SRC=$MYPROJECT/opt/src
PREFIX=$MYPROJECT/opt/rizin
export PATH="$HOME/.local/bin:$PATH"

mkdir -p "$SRC" "$PREFIX"
cd "$SRC"

RIZIN_VER=v0.9.1
RZGHIDRA_VER=v0.9.0

# ---------------------------------------------------------------- rizin
if [ ! -x "$PREFIX/bin/rizin" ]; then
  if [ ! -d rizin ]; then
    git clone --depth 1 --branch "$RIZIN_VER" --recurse-submodules \
      https://github.com/rizinorg/rizin.git rizin
  fi
  cd rizin
  rm -rf build
  meson setup build --prefix="$PREFIX" --buildtype release \
    -Denable_tests=false -Denable_docs=false -Denable_webui=false
  ninja -C build
  ninja -C build install
  cd "$SRC"
else
  echo "rizin already installed at $PREFIX"
fi

# ---------------------------------------------------------------- rz-ghidra
GHIDRA_PLUGIN=$(find "$PREFIX" -name "core_ghidra*" -print -quit 2>/dev/null)
if [ -z "$GHIDRA_PLUGIN" ]; then
  if [ ! -d rz-ghidra ]; then
    git clone --depth 1 --branch "$RZGHIDRA_VER" \
      https://github.com/rizinorg/rz-ghidra.git rz-ghidra
  fi
  cd rz-ghidra
  # decompiler source comes from submodules; make sure they are present
  if [ ! -f submodules/ghidra/Ghidra/Features/Decompiler/src/decompile/cpp/sleigh.cc ]; then
    git submodule update --init --depth 1 || true
  fi
  if [ ! -f submodules/ghidra/Ghidra/Features/Decompiler/src/decompile/cpp/sleigh.cc ]; then
    echo "FATAL: ghidra decompiler source missing after submodule init"
    find . -maxdepth 3 -iname "*decomp*" || true
    exit 1
  fi
  export PKG_CONFIG_PATH="$PREFIX/lib/x86_64-linux-gnu/pkgconfig:$PREFIX/lib/pkgconfig:$PREFIX/lib64/pkgconfig${PKG_CONFIG_PATH:+:$PKG_CONFIG_PATH}"
  export RIZIN_PREFIX="$PREFIX"
  rm -rf build
  meson setup build --prefix="$PREFIX" --buildtype release \
    -Denable_cutter=false -Denable_tests=false
  ninja -C build
  ninja -C build install
else
  echo "rz-ghidra already installed"
fi

echo "=== DONE ==="
ls -la "$PREFIX/bin/"
find "$PREFIX" -name "*ghidra*"
