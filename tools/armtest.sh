#!/usr/bin/env bash
# ARM self-test: builds src/engine.cc + the NAM core + tools/armtest.cc into one static ARMv7 executable (musl, no x86 anywhere)
# and runs it under qemu-arm-static. Test captures are taken from the original TONE3000 repo's test files.
#   QEMU=/path/to/qemu-arm-static  FILES=<dir with .nam/.wav test files>  tools/armtest.sh
set -euo pipefail
cd "$(dirname "$0")/.."
ZIG="${ZIG:-python3 -m ziglang}"
QEMU="${QEMU:-qemu-arm-static}"
FILES="${FILES:-/home/claude/t3k_src/tone3000-plugin/test/files}"
mkdir -p build/test
TGT="-target arm-linux-musleabihf -mcpu=cortex_a17"
INC="-Isrc -Ithird_party/mpc-vst-plugins/wrapper -Ithird_party/NeuralAmpModelerCore -Ithird_party/NeuralAmpModelerCore/Dependencies/nlohmann -Ithird_party/eigen -Ithird_party/curl-include"
DEFS="-DNAM_ENABLE_A2_FAST -DEIGEN_MPL2_ONLY"
OBJS=""
[ -f build/chain_box.h ] || { echo "build/chain_box.h is missing: run ./build.sh once (tools/panel_art.py writes it)"; exit 1; }
CO="build/test/chainpics.o"
if [ ! -f "$CO" ] || [ src/chainpics.c -nt "$CO" ] || [ build/chain_box.h -nt "$CO" ] || [ src/chainpics.h -nt "$CO" ]; then
  $ZIG cc $TGT -O2 -std=gnu11 -w -Isrc -Ibuild -c src/chainpics.c -o "$CO"
fi
OBJS="$CO"
for f in src/engine.cc tools/armtest.cc $(find third_party/NeuralAmpModelerCore/NAM -name '*.cpp' | sort); do
  o="build/test/$(echo "$f" | tr / _).o"
  if [ ! -f "$o" ] || [ "$f" -nt "$o" ] || [ -n "$(find src -newer "$o" -name '*.h')" ]; then
    $ZIG c++ $TGT -O2 -std=c++20 -w $INC $DEFS -c "$f" -o "$o" &
  fi
  OBJS="$OBJS $o"
  while [ "$(jobs -r | wc -l)" -ge 6 ]; do sleep 0.2; done
done
wait
$ZIG c++ $TGT -static $OBJS -o build/test/armtest
rm -rf /tmp/t3kdata && mkdir -p /tmp/t3kdata/tones
python3 - <<'PY'
from PIL import Image
Image.new("RGB", (300, 200), (200, 40, 40)).save("/tmp/t3kdata/tones/a2-amp-test.png")
Image.new("RGB", (1200, 900), (40, 160, 60)).save("/tmp/t3kdata/tones/cab-ir-test-2.jpg", quality=92)
open("/tmp/t3kdata/tones/corrupt.png", "wb").write(b"\x89PNG\r\n\x1a\nnot really a picture")
PY
cp "$FILES"/a2-amp-test.nam "$FILES"/a2-amp-cab-test.nam "$FILES"/cab-ir-test-2.wav "$FILES"/reverb-ir-mono-test.wav /tmp/t3kdata/tones/
"$QEMU" build/test/armtest /tmp/t3kdata
