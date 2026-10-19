# Third-party code

Libraries vendored as source, so that every build, including a distribution's offline package build, has them. Upgrade each as a set, by copying the files of a newer upstream commit, and update its line below. Don't edit them beyond the fixes listed.

| Folder | Upstream | Version | Licence | Used for |
| --- | --- | --- | --- | --- |
| `SDL_native_midi/` | <https://github.com/libsdl-org/SDL_native_midi> | commit `6423664` (2025-11-17), without the Haiku backend | zlib | MIDI music through the operating system's synth on Windows and macOS |
| `MMPX/` | <https://casual-effects.com/research/McGuire2021PixelArt/>, C port <https://github.com/ITotalJustice/mmpx> | commit `cce3900` (2021-07-15), with the edits listed below | MIT | Upscaled Art: magnifying the table's sprites |
| `stb/stb_vorbis.c` | <https://github.com/nothings/stb> | v1.22, commit `2c980bb` | MIT or public domain | Ogg Vorbis samples of SF3 SoundFonts, for TinySoundFont |
| `TinySoundFont/` | <https://github.com/schellingb/TinySoundFont> | commit `853a0a1` (2026-07-19) | MIT | MIDI music rendered with a SoundFont everywhere else |

`TinySoundFont/TinySoundFont.c` is this project's file: it compiles the implementations of `tsf.h` and `tml.h` once.

`MMPX/` differs from upstream in these edits, to re-apply after an upgrade unless upstream has them:

- `mmpx.c`: `clamp` returns `max` above the range (upstream returned `v`).
- `mmpx.c`: includes `mmpx.h` instead of `<stdint.h>`, and `mmpx_scale2x` takes `uint32_t` sizes, as the header declares (upstream's definition takes `int`, which conflicts with the header once it is included).
- `mmpx.h`: an `extern "C"` guard for C++, and a comment on what `mmpx_scale2x` expects.
- `LICENSE`: added with the MIT text, because upstream states the licence only in `mmpx.c` and its README.

Dear ImGui is vendored too, in `SpaceCadetPinball/`; see `AGENTS.md`.
