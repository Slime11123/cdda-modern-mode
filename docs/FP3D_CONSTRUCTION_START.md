# 3D 确认位置后的原版施工链（2026-11-03）

继续P5.6：3D选择合法施工位置后，再次检查实际项目、同层相邻位置、目标身份、材料、工具、知识及原版光照条件，然后调用共用的原版提交步骤。确认前主动刷新制作物品缓存，避免把预览时的旧材料/工具状态当作可用。玩家自身格、失效项目、超距/跨层或已变化目标被拒绝。

共用提交步骤保留原版partial_con检查、consume_items、consume_tools、缓存失效和ACT_BUILD赋值；原版2D选点界面继续调用同一段原有逻辑。初始派系确认仍由原入口处理，未添加新的世界规则或存档字段。对照76cc9f43核对提交主体没有重新实现扣材算法，只有函数提取及返回值变化，记录qa/0i1/modern-construction-start-native-body-review.log。正式游戏可执行文件已核实包含新3D开工入口，记录modern-construction-start-game-entry-symbol.log。

## 已自动验证

1. 原版木桌项目从地图上的4块2x4、1块wood_panel和8枚nail开始，锤子/锯子提供原版品质。调用实际3D确认后入口，检查材料真正从来源消失并进入原版半成品记录、工具保留、ACT_BUILD位置正确；重复确认不重复扣材/开工。驱动原版工作活动直至完成f_table，检查实际AP消耗、半成品删除、完成快照与蓝图模型一致。
2. 原版完成床项目从真实床架及地上一张床垫开始，实际消费床垫，经过ACT_BUILD完成f_bed；没有直接制造半成品或启用DEBUG_HS赠送材料。
3. 预览缓存后失去知识、移走锤子/所有材料、目标地形变化、超距、跨层、原点不符、自身格、项目失效、夜间等被拒绝，资源/活动/时间/AP/RNG保持对应拒绝前状态。

专项135断言/3用例通过：qa/0i1/modern-construction-start-focused-fixed.log。首次编译遇到原版construction字符串ID模板特化的定义顺序问题，调整3D入口定义位置后通过。首轮夜间场景只修改时钟而未刷新原版自然光缓存，错误地仍处于白天亮度；改用原有set_time测试辅助刷新光照后通过，首次5条失败及修正记录保留。

## 整体检查与交付

- 功能171用例/16961断言，退出0：modern-construction-start-cpu.log，过滤[fp3d],[npc_talk],[rng]。
- 两包图形6315断言/2用例，退出0：modern-construction-start-gl.log。
- 测试、ON/OFF构建通过：modern-construction-start-tests-build-night.log、modern-construction-start-on-build-fixed.log、modern-construction-start-off-build-fixed.log；两运行包帮助入口退出0，哈希见modern-construction-start-package-smoke.json。
- 两个3D启动器更新runtime/modern-3d-construction-start，沿用runtime/0i1/user；保存退出重开生效，没有结束当前游戏。正式包SHA256：BFAD198FD7E0CFDACB6771C47E93864D3889E629E942395F496F2627E324C9E5。

所有日志位于qa/0i1。数量包含检查时共享工作区的其他任务用例；此次提交不包含其UI/现代2D/NPC等修改。自动检查只使用独立QA目录，未修改正式存档。

## 验收边界

本次从已确认目标的生产入口开始，覆盖真实扣材到原版工作活动完工；不代表实体鼠标选点、初始派系询问、材料替代/取消弹窗已经实机验收。活动测试每tick提供100 AP并推进一秒，未跑施工过程的完整do_turn世界循环；没有把这种活动驱动称作完整世界回合验证。锤锯为品质工具，钉子的充能数量作为材料消耗已验证；充能工具/车辆伪工具的电量消耗仍需后续专项。P5.6的完整人工操作链及其他未完成项目保留。
