# Tone3000MPC v0.1.0

**TONE3000's NAM A2 amps, pedals and IR cabs, as an effect plugin for MPC OS on the Akai Force, with the whole TONE3000 library one tap away.**

Tone3000MPC runs [NeuralAmpModelerCore](https://github.com/sdatkinsonaudio/NeuralAmpModelerCore) (the NAM A2 engine) headless on the Force's
ARMv7, in an eight-block chain of NAM captures and IRs, with per-block level / mix / bypass / EQ, global EQ, gate, calibration and loudness
normalization following TONE3000's own plugin. A new Force screen browses TONE3000, and downloads only the captures you choose into a folder on the Force.

> [!NOTE]
> **Status: 0.1.0, not yet tested on a Force.** It passes 36 offline checks on its ARM build under qemu (see `TESTING.md`). The TONE3000 sign-in and download
> code and the CPU load of A2 captures on the Force are the things only a real run can confirm.

![Overview](docs/img/overview.png)

## Use
Overview tiles: Chain, Browse, In / Out, EQ, Tuner, Presets, Setup. Select a block, load a capture from Browse (Local or Online), play. Reorder blocks with
MOVE. 48 factory chains and unlimited user presets. See `docs/GUIDE.md`.

![Chain](docs/img/chain.png) ![Browse](docs/img/browse.png)

## Needs
Akai Force or MPC with MPC OS Gen1 (ARMv7, glibc 2.31 or older). For online browsing: a network connection and a free TONE3000 account.

## Credits and licence
Our code: [PolyForm Noncommercial 1.0.0](LICENSE): free to use, not for sale. Bundled third-party code belongs to its authors under their own licences
(`VENDORED.md`, `third_party/licenses/`): NeuralAmpModelerCore and nlohmann/json (MIT), Eigen (MPL 2.0), curl headers (curl licence).
TONE3000 and its open-source plugin (MIT) are the reference for the web API and the gain rules. Captures you download keep their authors' terms.
Made by L'Cronx, Croydon, UK. Free; tips welcome at ko-fi.com/lcronx.
