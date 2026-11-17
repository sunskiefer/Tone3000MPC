# Changelog

## 0.1.0 (first release, not yet tested on a Force)

- NAM A2 amps, pedals and outboard (NeuralAmpModelerCore) and IR cabs / spaces in an 8-block chain, each block with level, mix,
  bypass and its own pre / post EQ; global EQ, noise gate, input calibration and loudness normalization (TONE3000's rules).
- Browse TONE3000 from the Force (gear, format, sort, pages) and download only the captures you pick; everything plays from
  the local `Tone3000MPC/tones` folder, so captures copied in by hand (for example with L'Cronx Manager) show up too.
- Sign in with TONE3000's device code (no keyboard needed). The community key is the default; `settings.json` can hold your own.
- Overview tiles, Chain, Browse, In / Out, EQ, Tuner, Presets and Setup pages; Q-Links follow the page; the data wheel scrolls
  every list.
- Reorder the chain: select a block, tap MOVE, tap the block to swap with. Only the two tiles change.
- Unlimited user presets and a 48-preset factory bank that fills its slots from TONE3000 (or from your own captures) on first recall.
- Tuner (text readout), DSP load readout, Full / Lite model quality.
- Not in 0.1.0: oversampling, dual mono, drag to reorder, stereo IRs (the left channel is used), IRs longer than 0.5 s (cut with a fade).
