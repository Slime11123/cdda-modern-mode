# CDDA Modern Mode — Development Status / 开发状态

**Updated: 2026-11-03 / 更新日期：2026-11-03**
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

- Concrete uphill construction stages now preview the existing single-cell world shapes. Native committed-work completion verifies both lower and upper terrain links; ramp surfaces use ground materials instead of 2D arrow markers. 156 functional cases (13260 assertions), 5457 final graphics assertions and ON/test builds passed. Local launchers use `modern-controls-ramps`; continuous multi-cell ramp geometry, orientation and initial construction placement remain incomplete.

- Added bed frames, closed/open crates, cupboards, lockers, bookcases and racks to the shared world/construction models. Sealed contents remain hidden under native rules. A committed native bed-building activity now verifies interruption, resumption and the completed model. 155 functional cases (13218 assertions), 5375 graphics assertions and ON/OFF builds passed. Local 3D launchers use `modern-controls-storage`; initial placement/component consumption and the full construction lifecycle remain unverified.

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


### 2026-11-02 橱柜贴图分面 / 条件施工方向

不死人橱柜现在按原图分别使用白色顶面、棕色柜门正面和木质侧面，建造蓝图同步；其他图像不套用该分区。条件挖掘楼梯使用本格方向/范围规划符号，不预测隐藏楼层结果。共享工作区功能158用例13326断言及两包图形5554断言通过，ON/测试构建通过；未重复OFF验证。两个启动器指向runtime/modern-controls-furniture-faces，重启生效。详见FP3D_0I1_MODERN_CONTROLS.md本日增量及qa/0i1/modern-furniture-faces-*。其他家具分面、朝向和完整施工链仍待继续。


同日补充：进一步核对并修正不死人木箱、椅子及柜架取图，避免在模型构件上重复完整伪3D图片；不改独立素材或RetroDays。最终图形5611断言及ON/测试构建通过，沿用同一非渲染逻辑的158用例13326功能断言，未重复CPU/OFF。当前启动器runtime/modern-controls-furniture-surfaces；证据qa/0i1/modern-furniture-surfaces-*。


### 2026-11-02 双门储物柜 / 已授权柜内物品显示

按用户标注将储物柜改为完整柜体和双门正面，修复窄边框拉伸条纹；橱柜/储物柜已授权物品图标可穿过柜体显示，墙/楼板及普通生物遮挡保留。159用例13352功能断言、两包6073图形断言/2用例及ON/测试构建通过，运行包帮助入口退出0；未重复OFF构建。启动器runtime/modern-controls-locker-doors，保存退出重开生效。报告FP3D_STORAGE_DOORS_AND_CONTENTS.md；柜门动画及正式游玩验收未标完成。


### 2026-11-03 3D柜体邻墙朝向

橱柜、储物柜和书柜按当前已知墙/窗边界自动调整正面；世界、蓝图和准星同步。功能168用例16826断言、两图块包图形6314断言/2用例、ON/OFF及测试构建通过；运行包帮助入口退出0，实际截图已核对。原版占格/存档未改；P5.6其余施工与模型项目未标完成。启动器runtime/modern-3d-cabinet-facing；详见FP3D_FURNITURE_FACING.md。
