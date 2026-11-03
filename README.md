# CDDA Modern Mode

[简体中文](README.zh-CN.md) · [Development status](docs/DEVELOPMENT_STATUS.md) · [Upstream CDDA](https://github.com/CleverRaven/Cataclysm-DDA)

**CDDA Modern Mode** is an unofficial modernization project based on **Cataclysm: Dark Days Ahead (CDDA) 0.I-1 / Ito-1**. It explores first-person 3D presentation, an independent modern 2D control mode, and mouse-friendly interfaces while retaining CDDA's original turn-based survival systems, action-point accounting, combat, world simulation, and saves.

> **Status: development preview | Updated November 3, 2026.** This repository primarily provides source code and technical notes. There is no fully validated standalone Windows release yet. The project is not affiliated with or endorsed by the upstream CDDA team.

## Features in development

| Area | Current implementation |
| --- | --- |
| **Three modes** | Switch between classic 2D, modern-controls 2D, and first-person 3D. Classic 2D retains native controls, while the other two modes use distinct keybinding profiles. |
| **Modern 2D** | WASD world movement and the integrated Tab survival panel; 360° smooth mouse-directed map panning outside a circular dead zone, automatically returning to center and disabled at the widest zoom level. |
| **First-person 3D** | Terrain, multi-floor buildings, stairs, ramps, doors/windows, sky, vegetation, and visible entities, combining world geometry with the selected tileset's materials. |
| **Furniture and storage** | Basic 3D tables, chairs, beds, bed frames, crates, cupboards, lockers, bookcases, and shelving; selected furniture uses face-specific textures and native item-visibility rules. |
| **Interaction** | Target reticle and hints, short/hold-E actions and context wheel, Q action wheel, aimed melee and smashing, plus native stairs movement entries. |
| **Survival interface** | Equipment, inventory/pockets, character status, crafting, construction, map, missions, nearby visible people, and more; relevant modern panels are shared by modern 2D and 3D. |
| **Crafting and building** | Recipe categories/search/favorites, batch crafting, material/tool information; workbench-specific crafting panel and previews for selected furniture, stairs, and ramps. |
| **NPCs** | Mouse dialogue with tileset portraits, two-column trade interface, and selected native guard/follow commands. Native dialogue effects and trading rules remain in charge. |
| **HUD and presentation** | 3D HUD, interaction reticle, held-item swing, introductory character overlay; independently selectable classic sidebar or modern HUD. |
| **Native mechanics** | 3D rendering does not replace native damage, AP, turn processing, or save rules. Actions are delegated to original game logic wherever applicable. |

These are features present in the development code, **not a claim of complete real-world gameplay acceptance**. Some specialized confirmations still use original CDDA dialogs.

## Controls

| Input | Description |
| --- | --- |
| **F5** | Cycle classic 2D → modern 2D → first-person 3D |
| **F6** | Configure mode keybindings and select the sidebar/HUD style |
| **WASD** | Camera-relative movement in 3D; planar movement in modern 2D |
| **Mouse movement** | Look around in 3D; smooth map panning outside the central dead zone in modern 2D |
| **Left click** | Native melee or smashing against a valid aimed target in 3D; friendly/neutral attack confirmation remains |
| **Right click** | Native aiming/shooting flow in 3D, depending on context |
| **E** | Interact; hold to open a target-specific wheel, including relevant stair actions |
| **Q** | Quick action wheel in 3D |
| **Tab** | Integrated survival panel in modern 2D or 3D |
| **M** | Modern map in 3D |

Bindings are customizable. HUD style is independent of the F5 mode: choose **Follow mode** (default), **Classic sidebar**, or **3D HUD**. Changing the display style does not alter the native 2D keybindings.

## Recent milestones

- **Modern 2D:** The survival panel opens without requiring any 3D scene capture. Mouse-directed map viewing uses a circular dead zone and smooth 360° panning across aspect ratios.
- **World rendering:** Additional multi-floor, stair, ramp, and furniture geometry; revised per-face materials for selected cupboards, crates, chairs, and lockers.
- **Interactions and crafting:** E-key stair wheel and workbench crafting panel connected to native actions, with appropriate native item visibility for cabinets and furniture.
- **Scene visibility:** Certain in-line-of-sight 3D world objects remain drawn under low lighting, while occlusion, hidden entities, blindness, and interaction rules retain their restrictions. This does **not** grant native night vision.

Technical notes and test boundaries:

- [Development status](docs/DEVELOPMENT_STATUS.md) · [Progress log](docs/FP3D_PROGRESS.md) · [Modern controls report](docs/FP3D_0I1_MODERN_CONTROLS.md)
- [Three modes](docs/FP3D_THREE_CONTROL_MODES.md) · [Modern 2D survival panel](docs/MODERN_2D_SURVIVAL_MENU.md) · [Modern 2D camera](docs/MODERN_2D_EDGE_PAN.md) · [HUD style](docs/FP3D_UI_STYLE.md)
- [Stair interaction](docs/FP3D_STAIR_WHEEL.md) · [Workbench crafting](docs/FP3D_WORKBENCH_UI.md) · [Storage visuals](docs/FP3D_STORAGE_DOORS_AND_CONTENTS.md) · [Dark-scene behavior](docs/FP3D_DARK_VISIBILITY.md)
- [NPC dialogue](docs/FP3D_NPC_DIALOGUE.md) · [NPC trade](docs/FP3D_NPC_TRADE.md) · [Reticle melee](docs/FP3D_RETICLE_MELEE.md)

## Limitations

**Experimental software, not a stable release.** Remaining work includes complete gameplay playthroughs, real saved-game and mod compatibility, physical mouse/keyboard and Chinese IME testing, detailed vehicle/furniture geometry, multi-cell ramps and full construction workflows, performance across varied hardware and dense city maps, and an authorized redistributable Windows package.

Functional and rendering test results in the linked reports apply to particular test scenarios; they do not imply that every manual interaction or long-duration play session has been validated.

## Build from source

**Development target:** Windows, CDDA 0.I-1, SDL2 Tiles, CMake, and experimental OpenGL 3D.

1. Clone the repository and prepare dependencies using the upstream [build guide](doc/c++/COMPILING.md) and [CMake guide](doc/c++/COMPILING-CMAKE.md).
2. Enable `TILES` for the Windows graphics build and `ENABLE_FP3D=ON` for the experimental 3D path (requires a Windows Tiles build).
3. Supply appropriately licensed game data and tilesets; test with an isolated profile/save directory.
4. `tools/run-fp3d.ps1` is a local developer launcher depending on a prebuilt sibling `runtime/` directory **outside the source repository**. It is not a one-click public installer. Cloning alone does not yield a ready-to-run game.

A player-facing Windows Release requires separate packaging, dependency checks, redistribution-rights review, and clean-install validation.

## Attribution, licenses, and contributions

This is a standalone GitHub repository, not a GitHub Fork. **Standalone hosting does not remove upstream attribution obligations.**

- Upstream: [CleverRaven/Cataclysm-DDA](https://github.com/CleverRaven/Cataclysm-DDA)
- Base: official `0.I-1` tag, commit `7b2efa5cea38e4d4d97dd0e63b28b9148623da59`
- CDDA's primary code and content use **CC BY-SA 3.0**, while some dependencies, fonts, and other assets have separate licenses. See [LICENSE.txt](LICENSE.txt), [attribution notes](docs/ATTRIBUTION.md), and the [upstream README](docs/UPSTREAM_README.md).
- Local third-party tilesets are not automatically approved for redistribution. Verify their rights before packaging.

Issues and suggestions are welcome in the [project issue tracker](https://github.com/Slime11123/cdda-modern-mode/issues).
