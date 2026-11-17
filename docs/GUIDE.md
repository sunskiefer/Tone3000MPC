# Tone3000MPC user guide

Tone3000MPC is an effect plugin: put it on a track whose input is your guitar, bass or any other sound.

## Pages

The **overview** has one tile per page; tap a tile to open it. **OVERVIEW** and **< >** in the header go back and step through pages.
The 16 Q-Links follow the page you are on; the data wheel scrolls the list or moves the control you touched last.

### Chain
Eight block slots (two rows of four). Tap a block to select it. LEVEL, MIX and BYPASS work on the selected block; **CLEAR** empties it;
**< >** step through the captures in your folder of the right kind. A block is either a **NAM** capture (amp, pedal, outboard) or an **IR**
(cab, space). Signal runs block 1 to 8.

**Reorder:** select a block, tap **MOVE**, then tap the block to swap with. Only those two tiles change.

### Browse
**SOURCE Local** lists what is in the plugin's `Tone3000MPC/tones` folder; **Online** searches TONE3000 (you must be signed in, see Setup).
GEAR, FORMAT and SORT filter. Tap a result: Local loads it into the selected block; Online opens that tone's captures, tap a capture to
download it and load it. **SCROLL** (or the wheel) scrolls the rows, **< PAGE / PAGE >** change page. **DELETE** then a row removes a downloaded capture.
Captures you copy into `tones/` yourself (a `.nam` or a `.wav` IR) are listed too.

### In / Out
In Gain, Out Gain, Dry/Wet, noise gate (threshold), **Calibrate** (TONE3000's input / output level matching with CAL LEVEL), **Normalize**
(captures to a common loudness, NORM TGT), **Input** Left / Right / Sum, **Quality** Full / Lite (Lite uses the lighter size of A2 captures).

### EQ
EQ FOR: Global, or the selected block's Pre or Post EQ. Low shelf, mid (with frequency), high shelf, low cut, high cut.

### Tuner
Turn TUNER on and play one string: note, cents and Hz. MUTE silences the output meanwhile. A = 430 to 450 Hz.

### Presets
**User**: SAVE NEW stores the chain under the next free name (no limit on how many); tap a name to load; OVERWRITE saves over the loaded
one; DELETE then a name removes it. **Factory**: 48 chains (8 layouts x 6 voicings). Recalling one fills each empty slot with the matching
top result from TONE3000 (and downloads it once), or, with AUTO FILL off or when offline, from the captures you already have.

### Setup
**SIGN IN** shows a code and a web address: open it on any phone or computer, enter the code, approve. Sign out any time.
DSP load, the API key in use and the data folder are shown.

## Files
`Tone3000MPC/` next to the plugin holds `tones/` (captures plus a small `.json` per download), `presets/`, `autofill.json`, `auth.json`
(your sign-in; keep private) and an optional `settings.json`: `{"api_key": "t3k_pub_...", "api_domain": "www.tone3000.com"}`.
Updating the plugin keeps this folder.

## Limits in 0.1.0
No oversampling, no dual mono, IRs are mono (left channel) and capped at 0.5 s, captures run at 48 kHz internally (the plugin resamples 44.1 <-> 48 kHz
around the chain). Latency is 96 samples (2.2 ms) plus the convolver block.

## Arranging the panel

Every control's position lives in `layout.conf`. Open `docs/PanelEditor.html` in any browser, drag controls (arrow keys nudge, shift = 10 px), press EXPORT and replace `layout.conf`; only the moved controls change. `docs/CONTROLS.md` is the numbered control map and `docs/img/overlay_*.png` show the ids on each page.
