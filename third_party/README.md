# Third-party code

Libraries vendored as source, so that every build, including a distribution's offline package build, has them. Upgrade each as a set, by copying the files of a newer upstream commit, and update its line below. Don't edit them.

| Folder | Upstream | Version | Licence | Used for |
| --- | --- | --- | --- | --- |
| `SDL_native_midi/` | <https://github.com/libsdl-org/SDL_native_midi> | commit `6423664` (2025-11-17), without the Haiku backend | zlib | MIDI music through the operating system's synth on Windows and macOS |
| `TinySoundFont/` | <https://github.com/schellingb/TinySoundFont> | commit `853a0a1` (2026-07-19) | MIT | MIDI music rendered with a SoundFont everywhere else |

`TinySoundFont/TinySoundFont.c` is this project's file: it compiles the implementations of `tsf.h` and `tml.h` once.

Dear ImGui is vendored too, in `SpaceCadetPinball/`; see `AGENTS.md`.
