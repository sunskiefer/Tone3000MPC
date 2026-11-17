# THING4000 0.1.1 (pre-release)

Neural Amp Modeler (NAM A2) amps and pedals plus IR cabs for the Akai Force / MPC OS (ARMv7, MPC OS Gen1). Download captures on your PC, copy them into `THING4000/tones` on the Force, load them into the 8-block chain.

**Not tested on a Force yet.** It passes 39 offline checks on its ARM build under qemu. Real CPU load of A2 captures, touch and the data wheel are unverified.

## What changed since 0.1.0
- Renamed to THING4000 (0.1.0 was published as Tone3000MPC).
- Online search, download and sign-in are switched off (compiled out). The plugin never uses the network and carries no API key; it only loads the captures and IRs you copied in.
- Factory presets fill their slots from your own captures.
- The zip carries `plugin.json` (MPC OS 3.10.1+ listing) and apostrophe-safe installers (`sh -n` checked).

## Files
- `THING4000-0.1.1-mpc-armv7.zip` (SHA-256 `d7b80a1806046272c5f59a9b102375c831b7ee55f72dc2ad6ae7df2e64f20824`)

Licence: PolyForm Noncommercial 1.0.0. Free to use, not for sale. Made by L'Cronx.
