# CDDA Modern Mode

[简体中文](README.zh-CN.md) | [Upstream Cataclysm: Dark Days Ahead](https://github.com/CleverRaven/Cataclysm-DDA)

**CDDA Modern Mode** is an unofficial, experimental modernization project built on **Cataclysm: Dark Days Ahead (CDDA) 0.I-1 / Ito-1**. It explores first-person 3D presentation and more accessible mouse-and-keyboard controls while keeping CDDA's underlying turn-based survival simulation.

> **Development preview, not a finished game or official CDDA release.** The public repository is intended for source review and development. A verified, standalone Windows download has not yet been published.

## Goals and implemented foundations

- Switch between classic 2D and an experimental first-person 3D view. A separate modernized 2D control mode is under active development.
- Keep original CDDA world rules, turn processing, saves, combat calculations, movement and collision logic rather than rewriting the game as a real-time survival title.
- Present terrain, buildings, multiple floors, sky, vegetation and visible entities through an experimental SDL2/OpenGL 3D renderer.
- Reuse the selected CDDA tileset's visuals where supported, with fallbacks for incomplete mappings.
- Provide modern mouse/keyboard interaction, a multi-panel inventory, equipment/pocket handling, map, status and other UI entry points.

**Current limitations:** The 3D renderer, controls and modern UI are still under active development. Some actions fall back to original CDDA dialogs. Complete end-to-end combat, mouse/IME interaction, complex buildings, vehicle geometry, compatibility and broad performance testing are **not** claimed as finished. See [development reports](docs/FP3D_PROGRESS.md) and [modern controls status](docs/FP3D_0I1_MODERN_CONTROLS.md) for detailed, dated test evidence and remaining work.

## Controls (development builds)

| Action | Current behavior |
| --- | --- |
| Change view/control mode | **F5** switches classic 2D and experimental 3D in the committed baseline; a third modern 2D mode is under development |
| Controls/help configuration | **F6** opens the mode-specific key configuration |
| 3D movement | **WASD**, relative to the camera, submits original CDDA movement actions |
| Look around in 3D | Mouse movement |
| Modern 3D interface | **Tab** opens the integrated interface; **M** opens the map |
| Other interactions | Context-sensitive keys and mouse actions; see in-game bindings (may change) |

Not every development runtime contains every in-progress feature. Keys may differ with custom bindings.

## Build and run

**Target environment:** Windows, CDDA 0.I-1, SDL2 tiles build and optional experimental OpenGL integration. This repository contains game **source code**, not a self-contained executable installer.

1. Follow the upstream build guidance in [compilation documentation](doc/c++/COMPILING.md) and [CMake documentation](doc/c++/COMPILING-CMAKE.md).
2. For an experimental Windows graphics build, enable **TILES** and **ENABLE_FP3D** in CMake (the FP3D option requires Windows + Tiles).
3. Build with the required dependencies, then supply CDDA data files and an appropriate, authorized tileset.
4. The author's local launcher `tools/run-fp3d.ps1` expects a sibling development `runtime/` directory with a prebuilt executable. That directory is **not part of this source repository**. It is not a standalone installer or a universal quick-start.

Do not assume that cloning this repository alone produces a ready-to-run game.

## Project and upstream

- **Project:** CDDA Modern Mode (unofficial, experimental).
- **Upstream:** [CleverRaven/Cataclysm-DDA](https://github.com/CleverRaven/Cataclysm-DDA).
- **Base:** official `0.I-1` tag, commit `7b2efa5cea38e4d4d97dd0e63b28b9148623da59`.
- **Implementation:** modern mode additions maintained on the `feature/fp3d-0i` development branch.
- **Documentation:** [development log](docs/FP3D_PROGRESS.md), [third-party tileset provenance](docs/FP3D_UNDEADPEOPLE.md), [publishing checklist](docs/PUBLISHING.md).

This project is **not affiliated with or endorsed by the official CDDA maintainers**.

## Licenses and third-party assets

CDDA's main source/content license is **Creative Commons Attribution-ShareAlike 3.0 Unported (CC BY-SA 3.0)**; some included dependencies, fonts and other files have separate licenses. Preserve applicable upstream notices and the original [LICENSE.txt](LICENSE.txt). Read [Attribution and redistribution notes](docs/ATTRIBUTION.md).

The project's local development environment uses an external **UndeadPeople** tileset, but those downloaded/generated assets are held outside this Git source tree. Their availability in a local build does **not** establish permission to redistribute them in a GitHub Release. Confirm their separate provenance and licenses before packaging.

## Contributing and support

Please use this project's GitHub issues (once the repository is published) for reports about Modern Mode. For upstream CDDA bugs unrelated to this project, use the [official CDDA issue tracker](https://github.com/CleverRaven/Cataclysm-DDA/issues).

The [original upstream README](docs/UPSTREAM_README.md) is retained for reference and attribution.
