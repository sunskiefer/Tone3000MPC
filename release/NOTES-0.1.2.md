# THING4000 0.1.2 (pre-release)

Neural Amp Modeler (NAM A2) amps and pedals plus IR cabs for the Akai Force / MPC OS (ARMv7, MPC OS Gen1). Download captures on your PC, unzip them, copy the `.nam` / `.wav` files into `THING4000/tones` on the Force, load them into the 8-block chain.

**Not tested on a Force yet.** It passes 67 offline checks on its ARM build under qemu. The picture layer on the real screen, the page arrows and the CPU load of A2 captures are unverified.

## What changed since 0.1.1
- **New PICTURES page:** the 8 blocks as 8 big picture squares. Tap to select; with MOVE on, tap to swap. Put a `.png` / `.jpg` with the same name next to a capture to give it a picture (any size; it is shrunk to fit).
- **Page navigation:** one step per tap of < >, and a stale page number written back by the host can no longer flip or skip a page.
- Darker panel look; no control moved and no text got smaller. The overview has a PICTURES tile (the DSP load readout sits below it).

## Files
- `THING4000-0.1.2-mpc-armv7.zip` (SHA-256 `3eba28c220ab37f55aa40d3513fb2d33efa42d4576be04dc5c536ca6d931bd1c`)

Licence: PolyForm Noncommercial 1.0.0. Free to use, not for sale. Made by L'Cronx.
