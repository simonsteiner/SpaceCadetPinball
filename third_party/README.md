# Third-party code

Libraries vendored as source, so that every build, including a distribution's offline package build, has them. Upgrade each as a set, by copying the files of a newer upstream commit, and update its line below. Don't edit them beyond the fixes listed.

| Folder | Upstream | Version | Licence | Used for |
| --- | --- | --- | --- | --- |
| `SDL_native_midi/` | <https://github.com/libsdl-org/SDL_native_midi> | commit `6423664` (2025-11-17), without the Haiku backend | zlib | MIDI music through the operating system's synth on Windows and macOS |
| `MMPX/` | <https://casual-effects.com/research/McGuire2021PixelArt/>, C port <https://github.com/ITotalJustice/mmpx> | commit `cce3900` (2021-07-15), with the edits listed below | MIT | Upscaled Art: magnifying the table's sprites |
| `stb/stb_vorbis.c` | <https://github.com/nothings/stb> | commit `2c980bb` (2026-08-02) | MIT or public domain | Ogg Vorbis samples of SF3 SoundFonts, for TinySoundFont |
| `TinySoundFont/` | <https://github.com/schellingb/TinySoundFont> | commit `853a0a1` (2026-07-19) | MIT | MIDI music rendered with a SoundFont everywhere else |
| `fonts/` | Exo 2, Noto Sans, Noto Sans Hebrew and Noto Sans Arabic from <https://github.com/google/fonts>; DSEG7 Classic from <https://github.com/keshikan/DSEG> | google/fonts commit `47d54ca` (2026-10-16); DSEG v0.46 | SIL Open Font License 1.1 (`OFL-*.txt`) | Space Cadet 2026's text and score digits, embedded at build time |

`TinySoundFont/TinySoundFont.c` is this project's file: it compiles the implementations of `tsf.h` and `tml.h` once.

`MMPX/` differs from upstream in these edits, to re-apply after an upgrade unless upstream has them:

- `mmpx.c`: `clamp` returns `max` above the range (upstream returned `v`).
- `mmpx.c`: includes `mmpx.h` instead of `<stdint.h>`, and `mmpx_scale2x` takes `uint32_t` sizes, as the header declares (upstream's definition takes `int`, which conflicts with the header once it is included).
- `mmpx.h`: an `extern "C"` guard for C++, and a comment on what `mmpx_scale2x` expects.
- `LICENSE`: added with the MIT text, because upstream states the licence only in `mmpx.c` and its README.

`TinySoundFont/` differs from upstream in this edit, to re-apply after an upgrade unless upstream has it:

- `tsf.h`: `tsf_decode_sf3_samples` treats a compressed sample whose `start`/`end` fall outside the `smpl` chunk (`start >= smplLength || end > smplLength || start >= end`) as empty, taking the existing zeroing branch, instead of reading `smplBuffer + start` out of bounds. Without it a SoundFont (SF2 or SF3) with a damaged compressed sample header crashes the game at startup, when it loads the SoundFonts it finds. Upstream still reads out of bounds there, but it refuses issues and patches written with an LLM (README, "Strict No LLM / No AI Policy"), so this patch, made with one, can't be sent; a fix upstream has to be written and reported by a person.

`fonts/` isn't copied by hand: `tools/build-fonts.sh` downloads the pinned files, checks their SHA-256, and writes the fonts and licences. To upgrade, change the pins there and run it. Its fonts differ from upstream:

- Exo 2 Regular and SemiBold, Noto Sans (as `NotoSans-Greek.ttf`), Noto Sans Hebrew and Noto Sans Arabic are static instances of upstream's variable fonts, because ImGui's rasteriser reads only a variable font's default instance (Noto Sans Hebrew's is Thin). Each is cut to the scripts the game uses, without OpenType layout or hinting, which ImGui doesn't use. The licence allows this: neither family reserves a font name.
- DSEG7 Classic Bold is upstream's file unchanged, because its licence reserves the name "DSEG" for unmodified fonts.

Dear ImGui is vendored too, in `SpaceCadetPinball/`; see `AGENTS.md`.
