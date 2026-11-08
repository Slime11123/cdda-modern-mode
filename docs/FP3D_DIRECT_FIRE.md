# 第一人称 3D 准星射击（2026-11-08）

## 需求与交互

- **3D 模式右键按下**立即朝屏幕中央准星开火，不弹出 CDDA 原版瞄准窗口；每次按下触发一次武器当前模式的开火动作（连发数量由当前枪械模式决定），松开不会额外射击。
- 左键仍为准星近战 / 砸击，`R` 等现有装填按键保持不变。
- 原版 2D、现代 2D 的射击菜单和操作保持原状。
- 保留兼容旧按键配置的 `FP3D_AIM` 动作 ID；名称改为准星即时射击。

## 代码路径

1. `fp3d_input.cpp` 接收 `MOUSE_RIGHT_PRESSED`（`data/raw/keybindings.json`），输入手势提交 `FP3D_AIM`。
2. `fp3d_interaction.cpp::resolve` 将 `FP3D_AIM` 转换为 `ACTION_FIRE`；`handle_action.cpp` 检查第一人称模式并直接调用 `fp3d_interaction::fire`，绕过原版 `aim_activity_actor` 与目标选择界面。
3. `fp3d_compositor::firing_target` 使用视野相机当前的 yaw、pitch、camera offset，与 HUD 准星方向一致；`fp3d_targeting::ray` 射线检测前景场景几何、精灵和遮挡，最大检测范围受武器射程限制；若没有视觉命中，则投射到视线方向的有效地图格。
4. `fp3d_interaction::fire` 验证手持枪械、当前模式、弹药与使用限制，通过 `Character::fire_gun` 结算，不调用 UI。枪械散布、射手枪械技能和枪术技能、后坐力、枪械故障、音效、弹药、伤害、技能经验和回合消耗继续采用 CDDA 机制。

## 限制与后续测试

- 3D 渲染使用连续 yaw/pitch 方向选取目标，但 CDDA 的最终弹道仍基于离散网格，不是完全独立的逐帧物理子弹。非常陡的上下射击、距离很远或多层地形时需要实机验证；检测射线暂上限为 64 格。
- 原版 `RELOAD_AND_SHOOT` 等需要 UI 选择弹药的武器，在即时射击时不会自动弹出原版交互；弹药不足会被拒绝。
- 回合制逻辑不变；未实现按住右键的连续实时全自动射击。
- **静态检查**：源码相关对象与 Windows Tiles 可执行文件已经通过 Ninja 构建；`git diff --check`、按键 JSON 检查完成。
- **仍需人工实机验证**：右键单击、近距离/远距离、技能差异、连发和耗弹、障碍/窗口遮挡、上下楼射击、装填和切换 2D 模式。

## 本机测试入口

- 工作区：`C:\Users\StickySlime\Documents\Codex\Projects\cdda3dproject`
- 直接双击 `Start-3D-DirectFire.cmd`；将使用单独的 `runtime\modern-3d-direct-fire\cataclysm-tiles.exe`。
- 此运行目录包含现有 DLL / 光影包的本地副本，独立于此前的车辆与光影实验可执行文件。
- 本地运行调用现有 `tools\run-fp3d.ps1` 并复用 `runtime\ranged-weapons-lab\user` 玩家目录。
- 未提交、未推送 GitHub，也未覆盖其他 Agent 已修改的文件。
