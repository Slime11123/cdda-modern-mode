# CDDA 0.I-1 怪物数据清单与图鉴

统计日期：2026-11-06。基于仓库默认 data/json 里 type=MONSTER 且带 id 的定义；abstract 原型不计入，未启用模组不计入。

## 默认游戏怪物总览

怪物 ID 共 **1123** 个，涉及 62 份 JSON 文件；JSON 解析错误 0 份。
同一怪物可有多个 species，以下采用展示优先级互斥分类。部分属性来自 copy-from 简化继承解析；运行中以游戏完成加载的最终 mtype 为准。

| 分类 | 数量 | 示例 |
| --- | ---: | --- |
| 动物 | 381 | bat、black bear、black bear cub、Cerbearus |
| 丧尸 | 321 | mercenary officer、mercenary ghost、headless horror、breather |
| 昆虫与蛛类 | 140 | giant ant、giant acidic ant、acidic ant larva、giant acidic queen ant |
| 异界与恐怖 | 68 | absence、albino penguin、amigara horror、pulsating chunk of unknown material |
| 机器人与改造体 | 59 | EMP hack、broken cyborg、C-4 hack、M240 autonomous CROWS II |
| 人类与狂暴者 | 45 | feral troglobite、hospitalized person、panicked person、terrified parent |
| 真菌 | 29 | fungal ant、giant fungal bee、alpha fungal bee、fungal bee |
| 其他 | 27 | ancient red dragon、generator、inflatable dragon、inflatable ghost |
| 变异生物 | 25 | alpha razorclaw、big rat、giant crayfish larva、colossal crayfish |
| 植物 | 19 | biollante、biollante sprig、biollante sprout、creeper hub |
| 黏液与异常体 | 9 | slime、brain slime、big slime、sewer slime |

## Species 交叉计数

| Species | 数量 |
| --- | ---: |
| ZOMBIE | 321 |
| HUMAN | 213 |
| MAMMAL | 143 |
| FISH | 108 |
| INSECT | 70 |
| FUNGUS | 55 |
| AMPHIBIAN | 53 |
| BIRD | 51 |
| FERAL | 41 |
| ROBOT | 41 |
| INSECT_FLYING | 31 |
| NETHER | 27 |
| SPIDER | 26 |
| MUTANT | 25 |
| NETHER_EMANATION | 22 |
| ABERRATION | 19 |
| REPTILE | 17 |
| PLANT | 15 |
| ROBOT_FLYING | 14 |
| MOLLUSK | 9 |
| CYBORG | 8 |
| MIGO | 7 |
| CENTIPEDE | 7 |
| WORM | 7 |
| UNKNOWN | 6 |
| LEECH_PLANT | 6 |
| SLIME | 6 |
| HORROR | 5 |
| KRAKEN | 4 |
| HALLUCINATION | 3 |
| NETHER_BURROWING | 1 |

## 已实现图鉴范围

- 使用统一 Tab 生存菜单的新图鉴页，兼容 2D/3D 现代操作；原版经典模式不动。
- 类型清单从运行时 MonsterGenerator 中读取，自动包含当前加载世界的模组怪物，不维护写死名单。
- 互斥分类、名称/原始 ID 搜索、详细属性、原版翻译文本、图块包怪物图像（缺少时显示占位）。
- 查询不读取实体实时生命或地图位置，不触发行动，不写入存档。
- 统计定义种类并非当前世界的刷怪种类，生成受怪物组、地图生成与模组规则限制。
- CSV 中名称是未经翻译的游戏 JSON 原文；游戏中使用翻译后的 mtype 名称。

## 后续可扩展

- 遭遇解锁/击杀统计需单独接入游戏日志与存档机制；刷新模组/切换世界时应重新建索引。
- 当前未实现出生点和精确掉落概率；这些属性不在 MONSTER 本身全部定义，需参考生成表和战利品表。

## 数据与参考

- data/json/monsters/、data/json/species.json、data/json 其他 MONSTER 定义文件。
- doc/JSON/MONSTERS.md、doc/JSON/JSON_INFO.md。
- 逐 ID 索引见 docs/MONSTER_CATALOG_0I1.csv。

## 本次验证结果（2026-11-06）

- Windows ENABLE_FP3D=ON、TILES=ON：CMake Ninja 目标 cataclysm-tiles 编译并链接成功（退出码 0）。
- 独立验证运行时：项目外层 runtime/modern-bestiary（图鉴版本 EXE 与 49 个运行 DLL），不覆盖旧版 runtime。
- 独立入口：项目外层 启动怪物图鉴验证.cmd，沿用 run-fp3d.ps1 的用户配置/存档目录。
- EXE --help 在独立运行时启动检查退出码 0，标准错误为空。
- 尚未完成人工进入实际世界、点击分类、搜索中文及怪物画像的完整视觉操作验证；不标为实机 UI 全部通过。
- 目录索引生成器：tools/generate-bestiary-catalog.py，使用 python 即可重新扫描当前默认 JSON 生成 CSV 和本报告；运行中模组图鉴始终由加载后的游戏数据提供。
- 本任务未进行 git commit、push 或 GitHub Release。
- JSON 校验启动参数（--jsonverify）进程退出码为 0、标准错误为空；标准输出记录 4 条字体文件路径警告。该结果不等同于实际进入游戏并点击图鉴验收。

## 图鉴怪物击杀数（2026-11-06）

- 每个怪物的左侧目录条目改为两行：名称在上，红色“击杀数：N”在下；右侧详情的怪物名称正下方也显示红色击杀数。
- 使用 `g->get_kill_tracker().kill_count(mon.id)` 获取原版随存档保存的怪物 ID 击杀记录；未击杀时自动显示 0，切换怪物、重新进入图鉴后取最新统计。原版该计数也可包含随行队友击杀，不应描述为严格仅玩家亲手击杀。
- 保留按物种分类与搜索；双行列表依旧使用 ImGuiListClipper，仅渲染可见条目；文本不会改变回合、RNG 和游戏状态。
- 本次重新构建 Windows TILES/ENABLE_FP3D 通过，图鉴验证包 `runtime/modern-bestiary/cataclysm-tiles.exe` 已更新，`--help` 启动检查退出 0、stderr 为空。
- 原有 `启动怪物图鉴验证.cmd` 可直接使用；当前没有验证进入实际游戏存档后的视觉效果，保留待人工验收。
