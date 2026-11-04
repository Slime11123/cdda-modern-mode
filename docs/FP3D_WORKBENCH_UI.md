# 3D 工作台简易制作界面

2026-11-03：同一套界面也已接入 2D 现代模式。当前入口与验证见 [2D 现代简易互动](MODERN_2D_SIMPLE_INTERACTIONS.md)；下文保留早期 3D 版本记录。

3D 模式下，对橱柜、台面等工作台按 E 时，工作台选项使用现有现代轮盘。选项名称和可用状态来自原版工作台菜单，取消不执行操作。

选择“制作物品”打开简易制作页，复用生存面板的配方分类、搜索、技能/收藏筛选、物品卡片、材料详情和批量制作。窄窗口使用分类下拉框，宽窗口使用分类侧栏。此入口仅显示制作内容，保留生存面板的配色和缩放。

制作请求携带所选工作台的绝对位置，提交时检查同层且距离不超过一格，再使用原版确认、材料检查及 `make_craft` 建立制作活动。制作中的物品放在指定位置。绘制回调仅生成请求，实际操作由面板输入循环提交。

非 3D 模式和 Feature OFF 保留原版入口。重复制作、连续制作、继续未完成品和收取物品的规则保留。

## 验证

- `qa/workbench-ui-regression.log`：119 用例，9983 断言通过。
- `qa/workbench-ui-cpu.log`：制作相关 9 用例，470 断言通过，包括指定台面放置、跨层及超出距离拒绝。
- `qa/workbench-ui-gl-final.log`：5738 图形断言通过；两种图块包，1280×720 / 1920×1080、100% / 150% 缩放。新增简易制作页的布局与无回合/AP/RNG消耗检查。
- 截图：`qa/workbench-ui-gl/*-workbench-*.png`，已检查 720p / 150% 输出。
- ON 客户端编译、打包完成，输出 `runtime/modern-controls-workbench`，根目录“启动3D操作修复验证.cmd”指向此版本。
- 修复启动入口遗漏：“启动3D技术验证.cmd”也指向新版；旧 `runtime/modern-controls-furniture-surfaces` 的程序同步更新，两个程序的 SHA256 一致，避免旧路径继续运行旧界面。
- `qa/workbench-ui-off-build.log`：Feature OFF 客户端编译通过。

以上为自动功能及隐藏窗口图形验证，未记录实体按 E 到完整制作活动的人工验收。

## 入口未生效的实际修复

此前 `iexamine.cpp` 未加入 `src/CMakeLists.txt` 的 `ENABLE_FP3D` 源文件列表，工作台内的新增分支被预处理排除。面板绘制测试没有覆盖此入口，因此此前通过的图形测试不能证明按 E 会打开新版。

已将 `iexamine.cpp` 加入列表，并重新编译、链接、打包。编译对象已核实包含对现代轮盘和现代面板的调用（`qa/workbench-entry-compiled-route.log`），制作相关 9 用例 / 470 断言重新通过（`qa/workbench-entry-cpu.log`）。

最终修复输出：`runtime/modern-controls-workbench-entry-fixed`；两个根目录 3D 启动器均已切换到此目录。SHA256：`83069ED25AD4A26C7BD7C5070049EB2424BA62CF185A0475BC7F1B2B9925D6DF`。旧程序当时被进程占用，未强制关闭用户窗口或覆盖占用中的文件。使用独立存档进行的桌面自动操作被用户停止，未完成实际 E → 制作页的操作验收。
