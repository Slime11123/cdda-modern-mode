# 3D 排水管 E 键攀爬轮盘

适用：3D 现代交互，短按/长按 E 指向一格内排水管，复用现有楼梯样式的轮盘，不改变 2D 操作。

- 地面：`t_gutter_downspout`、`t_vitrified_downspout`。轮盘列出「爬上」「爬下」，当前仅「爬上」可选。
- 屋顶：`t_gutter_drop`、`t_vitrified_gutter_drop`。当前仅「爬下」可选。
- 有效交互距离：角色所站格或相邻格（同层且可见）；站在水管格而准星没有其他有效目标时可回退到脚下水管。
- 选择后重新确认目标是否仍为同一格、同一种地形；非水管、远程、换层、驾驶中的操作拒绝。
- 「爬上」执行原版 `ACTION_MOVE_UP`；「爬下」执行原版 `ACTION_MOVE_DOWN`。本功能 **不会直接传送到楼上/楼下**，沿用原版攀爬能力、天花板、身体状态、装备负重、滑落风险、落脚点和边缘检查。原版对上落脚点/下落边缘的二次选择或警告仍可能出现。

代码：`src/fp3d_interaction.cpp`。回归用例：`tests/fp3d_interaction_test.cpp`，标签 `[pipe_wheel]`。

2026-11-08 验证：`git diff --check` 通过；本地现有 CMake Windows 3D 编译、链接目标 `cataclysm-tiles` 通过。**尚未运行新增测试和实机上下攀爬**。因用户当时正在运行 `runtime/0i1/cataclysm-tiles-models-latest.exe`，为了避免强关游戏，未覆盖运行中 EXE。结束游戏后运行工作区 `启动最新3D模型.cmd`，其脚本会重新编译并部署。
