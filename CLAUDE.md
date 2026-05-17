# CLAUDE.md

## Project Overview

This is a fork of [Training Mode - Community Edition (TM-CE)](https://github.com/AlexanderHarrison/TrainingMode-CommunityEdition), itself a fork of UnclePunch's training modpack for Super Smash Bros. Melee. It patches a Melee ISO to add training features. The codebase is approximately half C and half GameCube (PowerPC) assembly.

The IDE is **CLion**.

## Build

```sh
./build.sh "Super Smash Bros. Melee (USA) (En,Ja) (v1.02).iso"
```

The Melee ISO lives in the project root. Do not move, rename, or commit it.

Optional second argument:
- `release` — optimized release build
- `build/<name>.dat` — rebuild only that dat (e.g. `build/edgeguard.dat`)
- `build/codes.gct` — rebuild only the ASM gecko codes

Requires DevKitPro (GameCube/PPC package), and on non-Windows also `xdelta3`. On Windows, run `windows_setup.bat` first.

Output is `TM-CE.iso`.

## Project Structure

| Directory / File | Purpose |
|---|---|
| `src/` | C source for all events and support code. **Primary work area.** |
| `MexTK/include/` | Headers exposing internal Melee functions and structs. Read-only reference. |
| `MexTK/*.txt` | Symbol tables used by the `hmex` compiler. Do not edit without understanding the m-ex system. |
| `ASM/` | PowerPC gecko-code assembly. **Do not modify without explicit user permission.** |
| `dats/` | Binary HSD dat files (HUD models, animations). Edit with HSDRawViewer, not by hand. |
| `build/` | Compiler output (gitignored). |
| `bin/` | Build tool executables (`gc_fst`, `hmex`, `hgecko`, `xdelta`). |

## Hard Rules

- **Never edit any file under `ASM/` without explicit permission from the user.** Assembly changes are high-risk and the user handles them personally.
- Do not edit files in `MexTK/` (headers or symbol tables) unless specifically asked.
- Do not modify `build.sh`, `clean.sh`, or binary files in `bin/` without being asked.
- Do not commit the Melee ISO.

## C Development Guidelines

All new feature work happens in `src/`. The language is C targeting the GameCube (PowerPC), compiled by `hmex` against the MexTK headers.

- The simplest event to learn from is `src/powershield.c` — start there.
- To add a new event: add `src/<name>.c`, register it in `src/events.c`, and add a `mex_build` line in `build.sh`.
- Use `TMLOG(...)` for debug logging (only active in non-release builds). Toggle the on-screen console with L/R+Z.
- Use `OSReport(const char *fmt, ...)` (like `printf`) for logging that works in **both debug and release builds** and is readable in Dolphin's log window while the game is running.
- Use `bp()` to set a breakpoint for the Dolphin debugger. Load `GTME01.map` in Dolphin for symbols.
- Draw graphics with `GFX_Start` / `GFX_AddVtx`, or higher-level `HUD_*` functions.

## OSDs (On-Screen Displays)

OSD logic lives in `src/osds.c`. Adding a new OSD also requires changes to `ASM/training-mode/Globals.s` and `ASM/training-mode/Onscreen Display/Toggle UI/Load Alt Text When Loaded With L.asm` — **get user approval before touching those ASM files**.