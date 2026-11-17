# Changelog

## 0.1.2 (not yet tested on a Force)

- **New PICTURES page** (tile on the overview, and in the < > arrows): the 8 blocks as 8 big squares (306 x 200 px each), each showing the
  picture that goes with the capture loaded there. Tap a square to select that block; with MOVE on, tapping a square swaps it with the
  selected block. The selected block has a yellow frame, a bypassed block is dimmed, a used block without a picture shows its number.
- **Pictures:** put a `.png` or `.jpg` with the same name next to the capture in `THING4000/tones` (`Amp.nam` + `Amp.png`), or name the
  picture with an `"image"` key in the capture's small `.json`. It is shrunk to fit inside its square (any size works). The picture is
  decoded on a worker thread, never the audio thread.
- The pictures are drawn on a spare display layer (the method XYZ 0.0.2 uses on the Force), only while the PICTURES page is open and hidden the moment you leave it.
  If it cannot start (no display layer found) everything else works as before; `THING4000/chain/chain.log` says why, and `chain/chain.off` switches it off.
  `chain/layout.txt` holds the grid's place, if the page is ever moved.
- **Page navigation:** the < > arrows take one step per tap (a repeated event for the same tap is ignored), and only the page buttons and
  arrows change the page: a stale page number written back by the host can no longer flip or skip a page.
- Dark panel look: a darker page background with the THING4000 wordmark; knob strips are transparent so they sit on the art. No control moved
  and no text got smaller. On the overview the PICTURES tile takes the fourth slot of row 2, and the DSP load readout moved below it.
- Quality of life: the Q-Links on the Pictures page are SELECT and MOVE.
- Not in 0.1.2: oversampling, dual mono, drag to reorder, stereo IRs (the left channel is used), IRs longer than 0.5 s.

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
