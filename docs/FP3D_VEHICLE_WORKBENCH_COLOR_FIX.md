# 车辆工作台的彩色标签渲染修复

2026-11-08。根据玩家截图：车辆工作台的右侧“当前位置的零件”“车辆主要设备”直接显示 `<color_c_yellow>…</color>`、`<color_c_light_green>…</color>`，损坏状态颜色未显示。

## 定位与源码修复

根因在正式源码 `src/veh_interact.cpp` 的 `veh_interact::draw_simple_vehicle()`，而不是图块包或颜色主题。CDDA 原版 `vehicle_part::name()` 会把血量、损坏、退化标记包在 `<color_…>` 标签中；原先车辆工作台对它使用 `ImGui::TextWrapped("%s", part.name().c_str())` 或 `ImGui::Selectable(name.c_str(), …)`，因此标签被当作普通文本显示。

- **当前格零件**：改用 `cataimgui::draw_colored_text(string_format(...), c_white, width)`，分段渲染 `<color_…>` 的损坏、退化和其他标记，仍保留原来 HP/耐久数字。
- **车辆主要设备**：无弹药及有弹药的两种分支都改为同一彩色文字绘制器，保留弹药种类与数量。
- **可选零件列表**：用 CDDA 原有 `remove_color_tags(name)` 去除格式标签以避免破坏可点击 ImGui `Selectable`，并用 `get_color_from_tag(..., report_color_error::no)` 将原版**首个状态颜色**作为整行标签颜色。此列表并不支持逐字混色；右侧详情支持每个颜色段独立着色。
- **其他功能保持**：列表虚拟化、选中条目、键盘搜索、详情滚动、修复/拆卸/安装权限、行动回合、玩家存档完全没有改变。全局颜色解析器 `cata_imgui.cpp` 和 CDDA 原版 `color.cpp` 未修改。

## 测试

- `git diff --check -- src/veh_interact.cpp` 退出码 0。
- 真实 Windows 编译：`cmake --build build/0i1 --target cataclysm-tiles -j 3` 成功、链接成功。
- `qa/validate_vehicle_workbench_colors.py` 完成源码接入回归，以及模拟包含两个独立颜色段的用户截图样例解析；结果 `qa/vehicle-workbench-color-regression.json`。
- 新 EXE：`runtime/modern-vehicle-workbench-color-v1/cataclysm-tiles.exe`；`--help` 和 `--jsonverify` 的退出码都为 0，stderr 为空；测试 basepath 对字体文件的 stdout 仅有非致命提示。
- 独立运行入口：`启动车辆工作台颜色修复.cmd` / `Start-VehicleWorkbench-ColorFix.cmd`。使用同一个 game userdir 继续读取原有存档；与 P3-B 复用 49 个 DLL 硬链接，避免复制浪费空间。
- **未执行实际游戏内车辆工作台截图的人工目视测试**。测试版打开截图中同一辆车，右侧应仅显示彩色标记、零件名及数值，不再出现 `<color_…>` 字样。

## 独立性说明

源仓库位于 `CDDA-0I-FP3D`。没有覆盖原先光影开发主启动脚本、P3-A、P3-B、V8、第三方贴图或任何玩家存档；未提交 Git，未上传 GitHub。
