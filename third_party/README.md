# Third-party code

Libraries vendored as source, so that every build, including a distribution's offline package build, has them. Upgrade each as a set, by copying the files of a newer upstream commit, and update its line below. Don't edit them beyond the fixes listed.

| Folder | Upstream | Version | Licence | Used for |
| --- | --- | --- | --- | --- |
| `SDL_native_midi/` | <https://github.com/libsdl-org/SDL_native_midi> | commit `6423664` (2025-11-17), without the Haiku backend | zlib | MIDI music through the operating system's synth on Windows and macOS |
| `MMPX/` | <https://casual-effects.com/research/McGuire2021PixelArt/>, C port <https://github.com/ITotalJustice/mmpx> | commit `cce3900` (2021-07-15), with its `clamp` fixed (it returned `v` instead of `max` above the range) and a C++ guard in the header | MIT | Upscaled Art: magnifying the table's sprites |
| `stb/stb_vorbis.c` | <https://github.com/nothings/stb> | v1.22, commit `2c980bb` | MIT or public domain | Ogg Vorbis samples of SF3 SoundFonts, for TinySoundFont |
| `TinySoundFont/` | <https://github.com/schellingb/TinySoundFont> | commit `853a0a1` (2026-07-19) | MIT | MIDI music rendered with a SoundFont everywhere else |

`TinySoundFont/TinySoundFont.c` is this project's file: it compiles the implementations of `tsf.h` and `tml.h` once.

Dear ImGui is vendored too, in `SpaceCadetPinball/`; see `AGENTS.md`.
