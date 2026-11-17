# Testing THING4000

## Offline, ARM (done for 0.1.2)

`tools/armtest.sh` builds `src/engine.cc`, the NAM core and `tools/armtest.cc` into one static ARMv7 binary and runs it under
`qemu-arm-static` (no x86 build of anything). 67 checks, all passing:

- FFT convolver against direct convolution (-134 dB error); 44.1 <-> 48 kHz resampler (round-trip gain 0.9999, sample counts); EQ shelf.
- Empty chain is a clean pass-through; dry path is delayed to line up with the wet path (dry peak == wet peak); no output underruns.
- A real NAM A2 amp and a cab IR (TONE3000's own test files) load, play, bypass, save to and restore from the state chunk (308 bytes).
- MOVE: select, arm, tap another block: swapped, selection follows, MOVE disarms; a value-0 echo of a row tap is ignored.
- Q-Links follow the view (name, display, value both ways); block-scoped controls (level, mix, bypass, pre / post EQ) go to the selected block.
- Online is off: the source cannot be set to Online, sign-in answers "switched off", and Auto Fill online stays off (the network code and the API key are compiled out).
- Factory preset fills its slots from the local library; a user preset saves under the next free name; the tuner reads A2 for 110 Hz.

- Pictures: PNG and JPEG decode, shrink to 256 px, corrupt and missing pictures refused; the 4 x 2 grid renders at 1248 x 408 (selected block's yellow frame, bypass dimmed,
  empty block a plain card, picture in the middle of its square, block number when it has no picture); blocks keep their pictures through MOVE and clear with CLEAR.
- Page navigation: the next / prev arrows step exactly one page (wrapping Setup > Pictures > Chain), a repeated arrow event inside 350 ms is ignored, the automate-off echo
  does nothing, and a host write of an old page number does not change the page.
- The picture thread starts, logs, finds no display under qemu, gives up cleanly and does not block unloading.

Test captures come from the original TONE3000 plugin's `test/files` (MIT).

## Not verified (needs a Force)

- Real CPU load of A2 models on the Force (qemu timing means nothing). Use Quality = Lite and the DSP readout on Setup.
- Touch behaviour of every page, the pop-up lists and the data wheel on the real screen.
- The picture layer on the real screen: that a spare display layer is found, that the page markers are found on the PICTURES page, that the squares sit exactly on the tiles,
  and that the grid hides when you leave the page (everything else works without it; see `THING4000/chain/chain.log`).
- Page navigation on the real screen: that one tap of < > moves exactly one page.
- The wrapper's `.so` load on MPC OS (links clean with `--no-undefined`, exports only `VSTPluginMain`).
