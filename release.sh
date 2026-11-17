#!/usr/bin/env bash
# Package the real release: dist/THING4000-<version>-mpc-armv7.zip (install.sh, uninstall.sh, ARMv7 build, skin), then check it for the catalog.
#   MPC_TOOLS=<open-mpc mpc-vst-plugins>/tools ./release.sh 0.1.1
set -euo pipefail
cd "$(dirname "$0")"
VER="${1:?version, e.g. 0.1.0}"
MPC_VST="${MPC_VST:-$PWD/third_party/mpc-vst-plugins}"
# MPC_TOOLS = the current open-mpc/mpc-vst-plugins tools folder (it writes plugin.json); release_quotes.py makes it L'Cronx-apostrophe safe.
: "${MPC_TOOLS:?set MPC_TOOLS to the open-mpc mpc-vst-plugins tools folder}"
NAME="L'Cronx - VST - THING4000"
./build.sh
mkdir -p dist/stage
rm -rf dist/stage/*
mkdir -p "build/data/THING4000/tones" "build/data/THING4000/presets"
printf 'Put your amp / pedal captures (.nam) and cab IRs (.wav) here. Download them on your PC, then copy them into this folder.\n' > build/data/THING4000/tones/README.txt
python3 tools/release_quotes.py "$MPC_TOOLS" build/reltool
python3 build/reltool/tools/release.py --so build/arm/thing4000.so --skin "build/skin/$NAME" --entry build/pluginlist-entry.xml \
  --version "$VER" --id thing4000 --repo sunskiefer/Tone3000MPC --license PolyForm-Noncommercial-1.0.0 \
  --user-data THING4000 --extra build/data/THING4000:THING4000 \
  --about "NAM A2 amps, pedals and IR cabs: load the captures you downloaded, in a chain of blocks with EQ, gate and calibration, as an effect plugin." -o dist
T=$(mktemp -d); unzip -q dist/THING4000-"$VER"-mpc-armv7.zip -d "$T"   # the release fails unless every installer parses
for f in $(find "$T" -name install.sh -o -name uninstall.sh); do sh -n "$f"; done; rm -rf "$T"
ls -l dist
