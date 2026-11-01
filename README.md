# CDDA Modern Mode

[简体中文](README.zh-CN.md) · [Upstream CDDA](https://github.com/CleverRaven/Cataclysm-DDA) · [Development status](docs/DEVELOPMENT_STATUS.md)

**CDDA Modern Mode** is an unofficial modernization project based on **Cataclysm: Dark Days Ahead 0.I-1 (Ito-1)**. Its goal is to add experimental first-person 3D presentation and modern mouse-and-keyboard interfaces while preserving CDDA's original turn-based survival simulation, action resolution, and save format.

> **Status: development preview (November 1, 2026).** This GitHub repository primarily distributes source and technical documentation, not an official CDDA update or a fully validated standalone Windows release.

## What the project includes

| Area | Current development scope |
| --- | --- |
| **Three modes** | Cycle between classic 2D, modern-control 2D, and first-person 3D with F5. Modern 2D currently focuses on independent bindings and movement. |
| **First-person world** | Experimental 3D terrain, building façades, multi-floor structures, floors/stairs, doors, windows, vegetation, sky, day/night, and distant scenery, with tileset-based materials and visible entity sprites. |
| **Modern interaction** | Camera-relative WASD, interaction reticle and hints, hold-E context wheel, Q action wheel, targeted melee and smashing. |
| **Integrated survival UI** | Tab-accessible inventory, equipment and pockets, character status, crafting, construction, missions, map, and visible characters. |
| **Item and craft flows** | Native inventory activities and pocket rules, recipe categories/search/favorites, batch crafting, material checks, and construction previews. |
| **NPC systems** | Mouse-driven dialogue with tileset portraits, two-column trade UI, and selected native guard/follow commands. |
| **Presentation** | HUD, interactable-target indicator, equipped-item swing, introductory character information overlay, and native information-disclosure rules. |
| **Compatibility work** | Retains native AP, turn processing, combat, and saves; ongoing ON/OFF builds, rendering checks, and native-behavior comparisons. |

These are implemented development features, **not a claim that every end-to-end workflow has passed manual gameplay acceptance**. Some specialized actions still use the original CDDA dialogs.

## Controls

| Input | Current behavior |
| --- | --- |
| **F5** | Classic 2D → modern 2D → first-person 3D |
| **F6** | Inspect and configure bindings for each mode |
| **WASD** | Camera-relative 3D movement; independent planar controls in modern 2D |
| **Mouse** | 3D camera, menu controls, and target selection |
| **Left click** | Native melee against valid targets, or smashing of aimed surfaces; friendly/neutral attack confirmation remains |
| **Right click** | Native aim/shoot action, depending on context |
| **E** | Interact; hold to open a target-specific context wheel |
| **Q** | Quick-action wheel |
| **Tab** | Integrated survival panel |
| **M** | Modern map |

Bindings are customizable and may evolve. Native action-point costs, safe-mode rules, and interaction ranges still apply.

## Development status and limitations

The rendering and UI foundation is in place and has substantial targeted automated coverage. **The project is not release-stable.** Full IME composition, manual end-to-end mouse/keyboard workflows, sophisticated vehicle models, broad gameplay and mod compatibility, larger-scene performance, and release packaging still need additional verification.

See the [current status and open items](docs/DEVELOPMENT_STATUS.md), [development log](docs/FP3D_PROGRESS.md), and [modern controls report](docs/FP3D_0I1_MODERN_CONTROLS.md). Additional reports cover [NPC dialogue](docs/FP3D_NPC_DIALOGUE.md), [NPC trade](docs/FP3D_NPC_TRADE.md), [targeted melee](docs/FP3D_RETICLE_MELEE.md), [the three modes](docs/FP3D_THREE_CONTROL_MODES.md), and [entry overlay](docs/FP3D_ENTRY_SUMMARY.md).

## Source build and local runtime

**Development target:** Windows, CDDA 0.I-1, SDL2, CMake, and optional experimental OpenGL 3D integration.

1. Prepare your toolchain and dependencies using upstream [compilation](doc/c++/COMPILING.md) and [CMake](doc/c++/COMPILING-CMAKE.md) instructions.
2. Enable `TILES` for Windows graphics, and `ENABLE_FP3D=ON` for the experimental 3D path (Windows Tiles builds only).
3. Provide appropriately licensed game data and tilesets, then test with an isolated game profile.
4. The local developer launcher `tools/run-fp3d.ps1` expects a prebuilt executable in a sibling `runtime/` directory outside this source repository. **Cloning the source alone is not a one-click installation.**

An end-user Windows release will require separate packaging, dependency and asset-license checks, and clean-install validation.

## Attribution and licensing

- Upstream: [CleverRaven/Cataclysm-DDA](https://github.com/CleverRaven/Cataclysm-DDA)
- Base: official `0.I-1` tag, commit `7b2efa5cea38e4d4d97dd0e63b28b9148623da59`
- Repository: standalone GitHub repository (not a GitHub fork), with upstream authors and origins still credited
- Licensing: CDDA's primary code/content uses **CC BY-SA 3.0**. Some third-party libraries, fonts, and assets have separate license terms. Preserve all applicable notices; see [LICENSE.txt](LICENSE.txt), [attribution notes](docs/ATTRIBUTION.md), and the [upstream README](docs/UPSTREAM_README.md).

This is **not an official CDDA project and is not endorsed by upstream maintainers**. Third-party tileset assets used locally require separate redistribution review; do not publish local development saves or asset bundles without authorization.

For Modern Mode issues and ideas, use this project's [issue tracker](https://github.com/Slime11123/cdda-modern-mode/issues).
