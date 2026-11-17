#!/usr/bin/env bash
# Build Tone3000MPC for MPC OS Gen1 (armv7-a hard-float, glibc <= 2.31). No Docker.
#   ./build.sh            builds the plugin .so (build/arm/tone3000mpc.so) and the packaged skin folder
#   ./build.sh so         only the engine .so (no skin)
set -euo pipefail
cd "$(dirname "$0")"
MPC_VST="${MPC_VST:-$PWD/third_party/mpc-vst-plugins}"
ZIG="${ZIG:-python3 -m ziglang}"
NAME="L'Cronx - VST - Tone3000MPC"
python3 tools/gen_params.py
mkdir -p build/host build/arm
[ -x build/host/shadow_art ] || gcc -O2 -w -I"$MPC_VST/tools/vendor/force-shadow/tools" -x c -o build/host/shadow_art "$MPC_VST/tools/shadow_art.c" -lm
rm -rf build/skin
SHADOW_TITLE_FONT="$PWD/art/fonts/TitilliumWeb-SemiBold.ttf" SHADOW_ART="$PWD/build/host/shadow_art" python3 "$MPC_VST/tools/gen_vst.py" vst.json
python3 tools/knob_art.py "build/skin/$NAME/Plugin Skins"
TGT="-target arm-linux-gnueabihf.2.31 -mcpu=cortex_a17"
COMMON="-O2 -fPIC -fvisibility=hidden -ffunction-sections -fdata-sections -DNDEBUG -w"
INC="-Isrc -Ibuild -I$MPC_VST/wrapper -Ithird_party/NeuralAmpModelerCore -Ithird_party/NeuralAmpModelerCore/Dependencies/nlohmann -Ithird_party/eigen -Ithird_party/curl-include"
DEFS="-DNAM_ENABLE_A2_FAST -DEIGEN_MPL2_ONLY"
OBJS=""
build_obj() {   # src obj
  if [ ! -f "$2" ] || [ "$1" -nt "$2" ] || [ -n "$(find src -newer "$2" -name '*.h' 2>/dev/null)" ]; then
    $ZIG c++ $TGT $COMMON -std=c++20 $INC $DEFS -c "$1" -o "$2"
  fi
}
for f in src/engine.cc $(find third_party/NeuralAmpModelerCore/NAM -name '*.cpp' | sort); do
  o="build/arm/$(echo "$f" | tr / _).o"
  build_obj "$f" "$o" &
  OBJS="$OBJS $o"
  while [ "$(jobs -r | wc -l)" -ge 6 ]; do sleep 0.2; done
done
wait
$ZIG cc $TGT $COMMON -std=gnu11 -Ibuild -c "$MPC_VST/wrapper/vst2_wrap.c" -o build/arm/vst2_wrap.o
printf '{\n  global: VSTPluginMain;\n  local: *;\n};\n' > build/arm/exports.map
$ZIG c++ $TGT -shared -fPIC -Wl,--no-undefined -Wl,--version-script=build/arm/exports.map -Wl,--gc-sections -Wl,-s \
  $OBJS build/arm/vst2_wrap.o -lm -ldl -lpthread -o build/arm/tone3000mpc.so
echo "-> build/arm/tone3000mpc.so"
