# CDDA Modern Mode v0.1.0-alpha.1 — Windows experimental preview

**Status:** Pre-release / 非正式实验版本
**Date:** 2026-11-04
**Platform:** Windows x64 (SDL2 / OpenGL)

This is an **unofficial** modernization of Cataclysm: Dark Days Ahead 0.I-1 / Ito-1. It is not affiliated with the original CDDA developers. **Not yet certified as a stable or fully tested public build.**

## Included / 包含内容

- First-person 3D world view, multi-floor structures, terrain, furniture and source-tree tilesets.
- Modern 2D mode with mouse-directed camera motion and contextual interaction.
- Unified F5/F6 selection of presentation (2D/3D), classic/modern bindings and HUD/sidebar style.
- Mouse-friendly integrated survival UI and target/context wheels.
- NPC dialogue/trade and workbench/construction interfaces sharing native game rules.
- Native CDDA data, built-in gfx, Windows executable and runtime DLLs, and compiled translations.

## How to play / 使用方法

1. Download the **Windows-x64 ZIP asset** (not GitHub's automatic Source code archive).
2. Extract everything into a writable folder, not Program Files.
3. Double-click **Play-CDDA-Modern-Mode.cmd**. The ZIP has a self-contained data layout and a separate `user/` directory for personal saves and configuration.
4. Start a new game. Press **F5 or F6** to choose 2D/3D display, controls and HUD preferences.

Your saves are in `user/` under the extracted directory. **Back them up manually before updating**. Do not place saves from other installations into this preview without a backup.

## Known issues / 已知问题

- Experimental 3D rendering and mouse workflows are not exhaustively verified on all Windows computers.
- Some modern interfaces delegate confirmations to original CDDA dialogs.
- Long-session gameplay, mod compatibility, vehicle appearance, multi-floor building and performance remain under active development.
- 3D requires a compatible OpenGL-capable graphics driver.

## Credits and licensing

Based on [Cataclysm: Dark Days Ahead](https://github.com/CleverRaven/Cataclysm-DDA) stable tag 0.I-1. Core game code/content: CC BY-SA 3.0. Fonts and binary dependencies carry additional licenses. The package includes the original license notices. It uses **only gfx from the upstream-derived source repository**. Separately downloaded tilesets, personal saves, developer test fixtures, and other externally sourced media are **not bundled**.

See [LICENSE.txt](../LICENSE.txt) and [attribution](ATTRIBUTION.md). Bugs: https://github.com/Slime11123/cdda-modern-mode/issues.
