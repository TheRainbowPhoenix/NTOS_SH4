#!/bin/bash
# bootstrap.sh — resume the Edge working environment after a sandbox reset.
# Everything needed: pip tools, extracted refs, rizin build (if missing).
# Usage: bash edge/bootstrap.sh   (run from repo root)
set -ex
cd "$(dirname "$0")/.."   # repo root

PIP_PKGS="pefile capstone lief meson ninja cmake pillow"
python3 -m pip install $PIP_PKGS || pip install $PIP_PKGS

# refs already in git; extract to a scratch dir outside the repo tree
SCRATCH=${EDGE_SCRATCH:-$HOME/../my-project/edge_ref}
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

# rizin + rzghidra: build from source if not already at opt/rizin
if [ ! -x /home/z/my-project/opt/rizin/bin/rizin ]; then
  # keep a copy of the build script in-repo
  mkdir -p /home/z/my-project/scripts
  cp edge/tools/build_rizin.sh /home/z/my-project/scripts/build_rizin.sh 2>/dev/null || true
  nohup bash /home/z/my-project/scripts/build_rizin.sh \
    > /home/z/my-project/scripts/rizin_build.log 2>&1 &
  echo "rizin build started in background (log: scripts/rizin_build.log)"
else
  echo "rizin already present"
fi
echo "bootstrap done"
