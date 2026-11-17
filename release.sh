#!/usr/bin/env bash
# Package the real release: dist/Tone3000MPC-<version>-mpc-armv7.zip (install.sh, uninstall.sh, ARMv7 build, skin), then check it for the catalog.
#   ./release.sh 0.1.0
set -euo pipefail
cd "$(dirname "$0")"
VER="${1:?version, e.g. 0.1.0}"
MPC_VST="${MPC_VST:-$PWD/third_party/mpc-vst-plugins}"   # must be current open-mpc/mpc-vst-plugins tools plus the L'Cronx quote escape in release.py (0.1.0 was packed that way; check sh -n on install.sh)
NAME="L'Cronx - VST - Tone3000MPC"
./build.sh
mkdir -p dist/stage
rm -rf dist/stage/*
mkdir -p "build/data/Tone3000MPC/tones" "build/data/Tone3000MPC/presets"
printf 'Put TONE3000 captures (.nam) and IRs (.wav) here, or download them in the plugin.\n' > build/data/Tone3000MPC/tones/README.txt
python3 "$MPC_VST/tools/release.py" --so build/arm/tone3000mpc.so --skin "build/skin/$NAME" --entry build/pluginlist-entry.xml \
  --version "$VER" --id tone3000mpc --repo sunskiefer/Tone3000MPC --license PolyForm-Noncommercial-1.0.0 \
  --user-data Tone3000MPC --extra build/data/Tone3000MPC:Tone3000MPC \
  --about "TONE3000 NAM A2 amps, pedals and IR cabs with online library browsing, as an effect plugin." -o dist
ls -l dist
