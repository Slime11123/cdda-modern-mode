# 0.I-1 回归验证补充

更新：2026-10-26。这里只记录实际结果；未完成项目不计入通过。

## 存取档与状态

所有世界都是项目内独立测试副本，没有读写真实玩家存档。

1. 无修改原版创建 Karns 世界、Mabel Dukes 角色，保存退出，再读入。
2. 将该用户目录复制到独立 Probe 目录，由开启合成的程序读入同一世界；原版地图与角色正常显示。
3. 原版和 Probe 各自从相同初始副本读入，不进行移动，保存退出。使用发布包附带的 `zzip.exe` 解压**检查副本**中的存档，比较 JSON。

初始存档是 `#TWFiZWwgRHVrZXM=.sav.zzip`，SHA-256 为 `78232D15A5B3E58821EC10DC3F04715797D3C49C46656F15524D675DACE4205C`。不是寻找不到 `.sav` 就意味着没有保存；0.I-1 本次实际使用压缩格式。

原版与 Probe 再保存后，以下字段一致：角色姓名、位置 `[3564,1860,0]`、游戏时间 `5212800`、行动点 `100`、耐力 `8500`、身体生命值、背包。结果位于项目根 `qa/0i1/control-probe-canonical-comparison.json`。

初始耐力为 8650，读档再保存后两种程序均为 8500，因此该差异也发生在原版对照中。穿戴物对照有一处描述差异：内裤装饰为 black / pink hearts；其他对应字段一致。原版数据 `data/json/items/armor/undergarment.json` 的该变体使用 `<color>` 描述。这里只记录观察和数据依据，不宣称整个存档相同。

资料目录：`qa/0i1/regression-original/`、`regression-control/`、`regression-probe/`、`control-save-inspection/`、`probe-save-inspection/`。

## 原版规则自动测试

本机相同 0.I-1 源码构建了 `cata_test-tiles.exe`。测试工作目录为 `CDDA-0I-FP3D/`，DLL 搜索路径包含已有 UCRT64 工具链的 bin。

```powershell
../build/0i1/tests/cata_test-tiles.exe '[gates],[melee]~[.]' --rng-seed 424242 --user-dir '../qa/0i1/engine-tests-standard'
```

通过：15 个用例、71,989 项断言。日志 `qa/0i1/standard-gates-melee-tests.log`，构建日志 `regression-tests-build.log`。单独执行 `doors_should_be_able_to_open_and_close` 也通过：1 用例、10 项断言，日志 `doors-only-tests.log`。

额外显式启用隐藏用例 `Character_attacking_a_zombie`（标签 `[.melee]`）发生访问异常，退出码 `-1073741819`，日志 `gates-melee-tests.log`。它不在上述标准选择中。源码 `tests/melee_test.cpp` 的 `full_attack_details()` 会解引用 `get_wielded_item()`，而该用例包含徒手 NPC；这是一个待调查线索，未经有效调用栈或无 feature 对照复现，不认定为根因。本次没有修改上游测试或战斗代码来消除异常。

一次调试器诊断没有得到有效调用栈，已中止，不计为测试通过。调试过程中终止过独立 Probe 测试进程，未保存的测试开门/移动丢失；这不作为渲染器自行崩溃的证据，也不影响原始测试副本。

后续分支隔离：持械 plank 分支通过 2 项断言；徒手分支单独运行仍发生相同访问异常。临时只给日志辅助函数增加武器空值检查后，不再崩溃，完整隐藏用例得到 4 项断言通过、1 项失败：徒手命中率 `0.492` 未超过旧测试下界 `0.5`。这支持“空武器日志解引用导致崩溃”的诊断，同时暴露该隐藏用例另有预期不匹配；没有将其改成通过。

诊断日志：`hidden-melee-armed.log`、`hidden-melee-unarmed.log`、`hidden-melee-log-guard.log`。临时检查已撤销，`tests/melee_test.cpp` 恢复后 SHA-256 为 `F48A07E863B2210F80B753F70FA57BDD478975DB2340EF05339280D565136BDC`，与诊断前一致，测试程序也已重新构建恢复原版辅助函数。战斗实现、随机数与断言均未为此修改。

## 实机显示与交互

- 用户明确确认实体 F5 两次可令立方体消失并重新出现。先前误把原版窗口作为目标的测试无效，已排除。
- 原版新世界移动、背包及整理界面已实际显示；Probe 中也通过原版开门命令打开了避难所木门。
- UltimateCataclysm 和 RetroDays 均实际载入同一测试世界。2026-10-26 最终程序日志确认 `Loaded tileset: retrodays`；立方体位于左侧，右侧状态栏可读。
- 新测试配置采用 Consolas 等宽字体；初期比例字体与格宽不匹配导致的裁切没有通过修改游戏字体代码处理。
- 早期桌面自动化未能操作原版调试菜单；后续已定位中文输入法并通过基础攻击，见下节。排查用调试/菜单快捷键仅配置在独立 QA 副本，启动器默认配置不包含这些快捷键。

自动规则测试、实机基础攻击、重启换包及游戏内热切换的证据分别记录在下文，不能相互替代。当前阶段结论以主报告及持续执行记录为准。

## 实机战斗补验（已完成基础攻击与存档）

后续定位到中文输入法组合输入影响连续游戏按键：例如数字 32452 对应“组”，连续 z/u 在组合输入中会反复产生 z 编辑事件，随后提交汉字。切为英文输入后，方向键和游戏菜单可继续操作。不能将该现象归因于 3D。此前邀请人工操作菜单的补验已不再依赖用户回复。

使用 `tools/qa/combat_fixture/` 中的原版 EVENT EOC，只在独立 QA 世界载入时将普通僵尸放在角色东侧一格；没有修改战斗规则或怪物定义。原版和 Probe 分别从初始世界副本开始测试。

- 原版：普通方向攻击先出现 miss，随后明确记录拳击造成 1 点伤害；僵尸正常反击。成功保存并返回主菜单。证据 `qa/0i1/combat-original-hit.png`、`combat-original-state.json`。
- Probe：立方体开启、同窗口 UI 正常；向东攻击记录拳击造成 1 点伤害，侧栏显示该次行动耗时 64，保存后的实际剩余行动点为 36；成功保存返回主菜单。证据 `combat-probe-hit.png`、`combat-probe-state.json`。
- 两份保存后的消息均包含实际攻击结果，压缩存档可用官方工具解压并读取。两次战斗的随机结果不同，不作战后状态相同的声明；此前静止存取档状态对照仍独立有效。

数值解释依据：`handle_action.cpp` 用行动前后 moves 差值设置 `movecounter`，`display.cpp::move_count_and_mode_text_color()` 将其显示在侧栏。不能把该显示值当成 `Character::get_moves()` 的剩余行动点；初版报告的这处解释已校正。

这项补验覆盖基础攻击、反击、原版行动消耗和保存，不代表自然生成僵尸的完整生存场景或玩家独立击杀闭环已经实现。后续关闭门、游戏内换包结果见下节。

## 门与游戏内热切换补验

原版 `doors-tiles-original` 副本正常向北移动三格，通过原版方向移动打开木门，再使用关门命令关闭。时间从 08:00:03 到 08:00:04，消息同时记录开、关动作；门外区域由实时明亮视野变为地图记忆。保存后读入 Probe 副本，继续重复开、关动作，立方体及侧栏均正常。截图 `doors-original-roundtrip.png`、`doors-probe-roundtrip.png`。

原版随后在同一已载入世界打开原版 Options，将 UltiCa 切换为 RetroDays 并确认保存设置。地图立即换成 RetroDays，角色及时间仍为原场景的 08:00:04。日志记录 `01:10:29.839 Loaded tileset: retrodays`，截图 `tiles-original-hotreload.png`。为绕开桌面工具的 Esc 注入限制，仅这些 QA 副本将原版 Options 动作绑定到 z；启动器默认按键不变。

Probe 菜单测试发现立方体遮住选项面板左侧编号，已增加原版输入上下文通知并实机复验：Options 等菜单期间暂停测试图形及鼠标角度更新，返回世界恢复。截图 `p1-menu-unobscured.png`。

修复版在同一载入世界完成 UltiCa→RetroDays 热切换，角色和游戏时间保持 08:00:06，回到世界后立方体自动恢复。日志 `p1-ui-final/config/debug.log` 记录 `01:21:43.661 Loaded tileset: retrodays`；截图 `p1-ui-fixed-hotreload.png`。原版换包报告中的缺失图块条目继续使用原版回退，不是 OpenGL 错误。

最终修复代码重新完成 50 轮自动切换：200 帧、100 次切换、一次目标分配；随后缩放、全屏往返完成。日志 `p1-ui-final-selftest/config/debug.log`。重新编译 Feature OFF 并检查直接 OpenGL 导入为 False，再恢复 ON 构建；日志 `p1-ui-feature-off-build.log`、`p1-ui-feature-off-check.json`、`p1-ui-feature-on-restore.log`。

实机 UI 测试包 SHA-256 为 `AF4E49E2AFADD11ECC16B43A2240820D91E1662A3681C1AD45857E9E5041D5E7`，默认启动器/自动自测包为 `177C05B1575C02D659E688134FC413FAC4654545834EB324D783153F502AB44E`。同一构建重复 strip 打包的逐字节比较仅有 PE 时间戳和校验和字段差异（偏移 136、216–217）；两者功能代码相同。
