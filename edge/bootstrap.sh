#!/bin/bash
# bootstrap.sh — resume the Edge working environment after a sandbox reset.
# Usage: bash edge/bootstrap.sh   (run from repo root)
#
# What it sets up:
#   - pip tooling (pefile capstone lief meson ninja cmake pillow)
#   - extracted refs/edge artifacts under /home/z/my-project/edge_ref/
#   - rizin + rz-ghidra (from the edge-tools branch tarball; falls back to
#     a source build via edge/tools/build_rizin.sh)
#   - zig 0.14.0 (needed to build x86-64 PE payloads for NTOS_SH4_linux)
set -ex
cd "$(dirname "$0")/.."   # repo root

OPT=/home/z/my-project/opt
PIP_PKGS="pefile capstone lief meson ninja cmake pillow"
python3 -m pip install $PIP_PKGS || pip install $PIP_PKGS

# refs already in git; extract to a scratch dir outside the repo tree
SCRATCH=${EDGE_SCRATCH:-/home/z/my-project/edge_ref}
mkdir -p "$SCRATCH"
if [ ! -d "$SCRATCH/edgeapp" ]; then
  unzip -q -o refs/edge/Microsoft.MicrosoftEdge_38.14393.0.0_x86__8wekyb3d8bbwe.zip -d "$SCRATCH/edgeapp"
fi
if [ ! -d "$SCRATCH/dlls" ]; then
  unzip -q -o refs/edge/DLL.zip -d "$SCRATCH/dlls"
fi
if [ ! -d "$SCRATCH/oldie" ]; then
  unzip -q -o refs/edge/OLD_IE.zip -d "$SCRATCH/oldie"
fi

mkdir -p "$OPT"

# ---- rizin + rz-ghidra (prebuilt on the edge-tools branch) ----
if [ ! -x "$OPT/rizin/bin/rizin" ]; then
  if git show origin/edge-tools:rizin/rizin-linux-x86_64.tar.gz > "$OPT/rizin.tar.gz" 2>/dev/null; then
    tar -C "$OPT" -xzf "$OPT/rizin.tar.gz"
    echo "rizin restored from edge-tools branch"
  else
    git fetch origin edge-tools || true
    if git show origin/edge-tools:rizin/rizin-linux-x86_64.tar.gz > "$OPT/rizin.tar.gz" 2>/dev/null; then
      tar -C "$OPT" -xzf "$OPT/rizin.tar.gz"
      echo "rizin restored from edge-tools branch (after fetch)"
    else
      echo "no prebuilt rizin; building from source (slow)"
      bash edge/tools/build_rizin.sh
    fi
  fi
fi
"$OPT/rizin/bin/rizin" -v || true

# ---- zig 0.14.0 (single-file download; builds PE payloads) ----
if [ ! -x "$OPT/zig/zig" ]; then
  curl -sSL --max-time 300 -o "$OPT/zig.tar.xz" \
    "https://ziglang.org/download/0.14.0/zig-linux-x86_64-0.14.0.tar.xz"
  tar -C "$OPT" -xf "$OPT/zig.tar.xz"
  mv "$OPT/zig-linux-x86_64-0.14.0" "$OPT/zig"
fi
"$OPT/zig/zig" version

# ---- NTOS_SH4_linux env + sample e2e ----
make -C edge/ntos_linux test || echo "WARN: ntos_linux sample e2e failed"

echo "bootstrap done"
