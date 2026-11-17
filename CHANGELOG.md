# Changelog

## 0.1.1 (not yet tested on a Force)

- Renamed to THING4000 (0.1.0 was published as Tone3000MPC; the repo keeps its address).
- Online search, download and sign-in are switched off: compiled out, so the community API key and the network code never run, and the
  plugin only loads the captures and IRs you copied into `THING4000/tones`. The Source, Sort, Auto Fill and Sign in / out controls and the
  Setup account readouts are gone from the panel; everything else stays where it was.
- Factory presets fill their slots from the captures in your `tones` folder.
- Release packaged with the plugin.json manifest and apostrophe-safe installers (`sh -n` checked).

## 0.1.0 (first release as Tone3000MPC, not yet tested on a Force)

- NAM A2 amps, pedals and outboard (NeuralAmpModelerCore) and IR cabs / spaces in an 8-block chain, each block with level, mix,
  bypass and its own pre / post EQ; global EQ, noise gate, input calibration and loudness normalization (TONE3000's rules).
- Browse TONE3000 from the Force (gear, format, sort, pages) and download only the captures you pick; everything plays from
  the local `THING4000/tones` folder, so captures copied in by hand (for example with L'Cronx Manager) show up too.
- Sign in with TONE3000's device code (no keyboard needed). The community key is the default; `settings.json` can hold your own.
- Overview tiles, Chain, Browse, In / Out, EQ, Tuner, Presets and Setup pages; Q-Links follow the page; the data wheel scrolls
  every list.
- Reorder the chain: select a block, tap MOVE, tap the block to swap with. Only the two tiles change.
- Unlimited user presets and a 48-preset factory bank that fills its slots from TONE3000 (or from your own captures) on first recall.
- Tuner (text readout), DSP load readout, Full / Lite model quality.
- Not in 0.1.0: oversampling, dual mono, drag to reorder, stereo IRs (the left channel is used), IRs longer than 0.5 s (cut with a fade).
