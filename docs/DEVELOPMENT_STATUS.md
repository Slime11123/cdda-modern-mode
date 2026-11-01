# CDDA Modern Mode — Development Status / 开发状态

**Updated: 2026-11-01 / 更新日期：2026-11-01**
**Source baseline / 原版基线:** CDDA 0.I-1 (`7b2efa5cea38e4d4d97dd0e63b28b9148623da59`)
**Project state / 状态:** Experimental development preview, not a stable public release.

This is a concise public-facing summary of the current local code and dated technical reports. Validation claims below refer to tests recorded in those reports; they do not imply a new full regression run at the moment of publication.

## Implemented foundations / 已实现基础

| Feature | Description | Verification boundary |
| --- | --- | --- |
| Modes | Classic 2D, modern-controls 2D, and first-person 3D, F5/F6 isolation | Modern 2D is still primarily a movement/keybinding framework |
| 3D world | Multi-floor terrain, geometry, sky, weather-related display, tileset reuse, distant scenery | Special geometry, real-world scenes and long-duration gameplay remain incomplete |
| Performance | Snapshot/draw optimizations and isolated one-minute indoor/stair test scenes | Not a guarantee of 60 FPS in every game scene |
| Interaction | Reticle targeting, Q and E wheels, context prompts, melee/smash, native action delegation | Full device-driven chains and IME/high DPI remain partially unverified |
| UI | Inventory/pockets/equipment, health, crafting, construction, map, missions, visible NPCs | Some advanced confirmations remain native CDDA dialogs |
| NPC | Mouse dialogue, portraits, dual-column trade, guard/follow | Full NPC social/trading lifecycle and all edge cases not fully validated |
| Display | HUD, equipped-item swing, character-entry overlay, native health/knowledge disclosure | Presentation and fonts still under refinement |
| Compatibility | Original AP, game turns, combat and save processing with targeted native comparisons | No comprehensive guarantee for all mods, saves and game versions |

## Recent milestones / 近期进展

- Returned to P5.6 feature work; no further FPS-analysis expansion. Basic table/chair/bed models, raised item display and part-based targeting passed 153 functional cases (13060 assertions), 4813 graphics assertions and ON/OFF builds. Both local 3D launchers use `modern-controls-furniture`. See the dated modern-controls report for boundaries.

- Three-mode F5 switching with separate keybindings and classical 2D preserved.
- Context-sensitive interaction wheels and reticle hints; underlying range, visibility, identity and safety checks remain native.
- Native-rule crafting batch quantities, favorites/categories, material availability and construction-stage previews. Wooden upstairs/frame and basic table/chair/bed previews now share their built-world models; other furniture and the full construction lifecycle remain incomplete.
- Responsive mission/people panels, activity-return refresh and configurable interaction hold duration for the current session.
- Targeted E pickup: native automatic pocket selection for one item, a backpack/source panel for multiple items, and native tileset pile markers.
- Native-style NPC dialogue and two-column trading UI, followed by focused scrolling/render-performance improvements.
- 3D reticle-driven melee/smashing, friendly target confirmation, and equipped-item swing display.
- Introductory 3D character information overlay and information-disclosure/font refinements.

## Known limitations / 尚未完成

- Public, validated Windows installer/release and independently audited bundled third-party assets.
- Complete manual 3D playthrough and all keyboard/mouse/IME/high-DPI workflows.
- Every NPC transaction, dialogue action, vehicle model, and special building geometry.
- Broad long-duration performance testing across real maps and different hardware.
- Complete mod/asset coverage and exhaustive compatibility verification.
- Some suite-wide checks have had failures despite focused regressions passing; consult individual reports rather than interpreting selective tests as whole-project acceptance.

## Evidence / 记录

The source tree includes dated reports and reproducible descriptions:

- [Main chronological development log](FP3D_PROGRESS.md)
- [Modern controls and functional tests](FP3D_0I1_MODERN_CONTROLS.md)
- [Three control modes](FP3D_THREE_CONTROL_MODES.md)
- [NPC mouse dialogue](FP3D_NPC_DIALOGUE.md)
- [NPC trade and performance](FP3D_NPC_TRADE.md)
- [Reticle melee and smashing](FP3D_RETICLE_MELEE.md)
- [3D entry overlay](FP3D_ENTRY_SUMMARY.md)
- [Native information and font](FP3D_NATIVE_INFO_FONT.md)

Build logs, graphics captures and some performance CSVs referenced there are stored in the author's local `qa/` workspace and **are not included** in the public source repository.

## License and distribution / 授权和发布

CDDA is the upstream project; this is an unofficial standalone repository. Preserve the original CC BY-SA 3.0 and all separately licensed components. External local tilesets and other assets are not automatically approved for redistribution. See [ATTRIBUTION.md](ATTRIBUTION.md).

**No full-release certification is implied by this document.**
