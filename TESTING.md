# Testing Tone3000MPC

## Offline, ARM (done for 0.1.0)

`tools/armtest.sh` builds `src/engine.cc`, the NAM core and `tools/armtest.cc` into one static ARMv7 binary and runs it under
`qemu-arm-static` (no x86 build of anything). 36 checks, all passing:

- FFT convolver against direct convolution (-134 dB error); 44.1 <-> 48 kHz resampler (round-trip gain 0.9999, sample counts); EQ shelf.
- Empty chain is a clean pass-through; dry path is delayed to line up with the wet path (dry peak == wet peak); no output underruns.
- A real NAM A2 amp and a cab IR (TONE3000's own test files) load, play, bypass, save to and restore from the state chunk (308 bytes).
- MOVE: select, arm, tap another block: swapped, selection follows, MOVE disarms; a value-0 echo of a row tap is ignored.
- Q-Links follow the view (name, display, value both ways); block-scoped controls (level, mix, bypass, pre / post EQ) go to the selected block.
- Factory preset fills its slots from the local library offline; a user preset saves under the next free name; the tuner reads A2 for 110 Hz.

Test captures come from the original TONE3000 plugin's `test/files` (MIT).

## Not verified (needs a Force)

- Real CPU load of A2 models on the Force (qemu timing means nothing). Use Quality = Lite and the DSP readout on Setup.
- The TONE3000 network part: device-code sign-in, search, model list and download. It follows the open-source client's endpoints
  but could not be run from the build machine (no libcurl on the test target, no account).
- Touch behaviour of every page, the pop-up lists and the data wheel on the real screen.
- The wrapper's `.so` load on MPC OS (links clean with `--no-undefined`, exports only `VSTPluginMain`).
