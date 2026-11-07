# CDDA Modern Mode

> **AI 生成声明：本项目由 OpenAI GPT 生成并协助开发。** 本项目基于开源游戏 Cataclysm: Dark Days Ahead 的源码改造，并非 OpenAI 或 CDDA 官方产品。

[English](README.md) · [发布与下载](https://github.com/Slime11123/cdda-modern-mode/releases) · [开发进度](docs/DEVELOPMENT_STATUS.md) · [CDDA 原版](https://github.com/CleverRaven/Cataclysm-DDA)

**CDDA Modern Mode** 是基于 **Cataclysm: Dark Days Ahead（CDDA）0.I-1 / Ito-1** 的非官方现代化改造项目，在保留原版回合制、行动点、战斗、世界模拟和存档机制的基础上，加入第一人称 3D 画面、现代 2D 操作与鼠标友好的综合界面。

> **状态：Windows 实验性预览版（2026 年 11 月）**。当前开发版本仍可能存在崩溃、输入兼容及性能问题；不是 CDDA 官方产品，也不保证全部玩法已通过完整人工验收。

## Alpha 0.2.0 — 2026 年 11 月 5 日至 7 日

本次里程碑命名为 **Alpha 0.2.0**（标签：`alpha0.2.0`），按开发时间整理如下：

- **11 月 5 日：** 扩展 3D 卫浴、洗衣设备、木炉、椅子、楼梯、方向提示、血迹及尸体拾取交互，修正部分家具材质与模型朝向。
- **11 月 6 日：** 加入分页 3D 行动轮盘、固定梯和排水管模型、角色创建与现代交互增强；新增基于原版天气的雨雪、云雾与闪电；开发可搜索、分类、查看击杀数的怪物图鉴。
- **11 月 7 日：** 恢复制作/建造材料与工具状态的彩色标记；完善自动售货机、展示柜、文件柜、铁砧等 3D 家具；改进由多块小石头堆成、带真实拱形洞口和内部视觉火焰的石窑炉。

部分功能尚未完成全部游戏内与多硬件验收，详见[按日期整理的 Alpha 0.2.0 更新说明](docs/RELEASE_NOTES_alpha0.2.0.md)。

## 下载安装

Windows 玩家可前往 [Releases 页面](https://github.com/Slime11123/cdda-modern-mode/releases) 查看是否提供 Windows ZIP 预览包。下载时优先选择名称含 **Windows-x64** 的压缩包；GitHub 自动生成的「Source code」仅包含源代码，不能直接运行。

1. 将 Windows ZIP **完整解压**至有写入权限的英文路径（建议非 `Program Files`），不要直接在压缩包内双击。
2. **直接双击 `cataclysm-tiles.exe`**（真正的游戏主程序）。Alpha 0.2.0 Windows 版无需 CMD 或第二个 EXE；请保持 DLL、`data/`、`gfx/` 和 `lang/` 等目录结构完整。
3. 新玩家从游戏主菜单新建世界、创建角色；个人设置及存档位于解压目录的 `user/`，**请自行备份**。
4. 在游戏中按 **F5 或 F6** 打开统一模式设置，分别选择 2D/3D 画面、经典/现代键位、原版侧栏/现代 HUD。点击「完成」保存并应用。

若 ZIP 内缺少 `cataclysm-tiles.exe`、`data/`、`gfx/` 或 DLL，它不是完整玩家包。发行包仅包含源码自带且允许再分发的资源，不捆绑另行下载的第三方素材。Windows 版仍为实验性预览，问题反馈请提交至 [Issues](https://github.com/Slime11123/cdda-modern-mode/issues)。

## 推荐图块包：UndeadPeople（不死人）

为了获得**更好的视觉体验**，推荐搭配 **UndeadPeople（不死人 / MSX++UnDeadPeopleEdition）图块包**使用，特别适合 3D 建筑、家具和物品的呈现。

> **本项目不提供、不捆绑、也不重新分发不死人图块包。** 这是独立的第三方素材，请玩家自行从维护者仓库下载并遵守相关许可和署名要求。本项目基于 CDDA 0.I-1，不同版本的图块包可能存在兼容性差异。

**图块包仓库及下载地址：** [Xenikos / UndeadPeopleTileset（GitHub）](https://github.com/Xenikos/UndeadPeopleTileset)。

**安装方法：** 将下载得到的 `MSX++UnDeadPeopleEdition` 文件夹放入游戏的 `gfx/` 目录；在游戏中前往 **选项 → 图形 → 选择图块包（Choose Tileset）**，选择 **UNDEAD_PEOPLE**（以实际列出的名称为准）。如未立即生效，请重新启动游戏。

## 实机演示

以下为项目开发者提供的第一人称 3D 实机截图：

![CDDA Modern Mode — 第一人称 3D 厨房场景实机截图](docs/images/ingame-demo-kitchen-3d.png)

*开发中的实机截图；实际画面会因所选图块包及版本而有所差异。*

## 主要功能

| 类别 | 实现情况 |
| --- | --- |
| **独立画面 / 键位 / HUD** | F5/F6 统一设置：2D/3D 画面、经典/现代操作、原版侧栏/现代 HUD 分别选择并保存 |
| **现代 2D** | WASD、360° 圆形平滑移镜、Tab 综合生存菜单、右键地图情境菜单与 Q 行动轮盘；可共用现代工作台、NPC 对话和交易页面 |
| **第一人称 3D** | 地形、建筑与多层楼板/楼梯、坡道、天空及远景、家具、可见实体和当前图块包贴图 |
| **交互与战斗** | 准星/目标轮廓、E 情境轮盘、Q 快捷轮盘、定点近战、砸击、原版确认与安全检查 |
| **综合生存界面** | 背包/口袋、装备、健康、制作、建造、地图、任务、NPC、变异、仿生装置和武术入口 |
| **制作与建筑** | 配方搜索/收藏/批量、工作台简易制作、原版材料消耗和活动、楼梯/坡道/家具建造预览 |
| **家具与车辆** | 桌椅床、柜体、木箱、书架等 3D 基础几何，部分材质分面与邻墙朝向；车辆操作窗口仍在完善 |
| **NPC** | 鼠标对话、头像、双栏交易、队友守卫/跟随；结算和任务仍由原版规则处理 |

这些功能已进入开发源码，不代表所有界面均完成实际游玩或多设备兼容验收。部分复杂操作仍会调用原版界面。

## 常用操作

| 操作 | 按键 |
| --- | --- |
| 统一模式/按键/HUD 设置 | **F5 / F6** |
| 现代模式移动 | **WASD**（3D 相对视角，2D 平面方向） |
| 3D 视角 / 2D 现代探看 | 鼠标移动（行为因模式而异） |
| 综合生存菜单 | **Tab** |
| 3D 交互 / 长按目标轮盘 | **E** |
| 快捷行动轮盘 | **Q** |
| 现代 2D 地图情境菜单 | 地图上**鼠标右键** |
| 3D 准星攻击 / 砸击 | **鼠标左键**（符合原版目标检查） |

所有按键均应以游戏中当前配置为准；模式选择不会改变原版战斗/时间结算。

## 开发进度、局限及源码编译

目前仍在开发复杂场景性能、车辆与家具精细表现、各类操作的端到端人工验证、中文输入法与不同硬件兼容。开发测试中的「自动回归通过」不代表玩家版本已完整验收。

- [开发状态](docs/DEVELOPMENT_STATUS.md) · [详细日志](docs/FP3D_PROGRESS.md) · [现代控制](docs/FP3D_0I1_MODERN_CONTROLS.md)
- [统一模式设置](docs/FP3D_MODE_SETTINGS.md) · [现代 2D 右键菜单](docs/MODERN_2D_CONTEXT_MENU.md) · [共享简单交互](docs/MODERN_2D_SIMPLE_INTERACTIONS.md)
- [建造流程](docs/FP3D_CONSTRUCTION_START.md) · [家具朝向](docs/FP3D_FURNITURE_FACING.md) · [车辆方案](docs/FP3D_VEHICLE_VISUAL_DESIGN.md)

开发环境为 **Windows / CMake / SDL2 Tiles / OpenGL**。编译源码请先参照[官方 CMake 编译指南](doc/c++/COMPILING-CMAKE.md)，并启用 `TILES=ON`、`ENABLE_FP3D=ON`。本地 `tools/run-fp3d.ps1` 是开发脚本，依赖仓库外的编译输出，不能作为玩家安装器。

## 许可、署名与项目关系

此项目为独立 GitHub 仓库，**并非官方 CDDA 项目**；原版来源及作者贡献不会因独立托管而消失。

- CDDA 原版：[CleverRaven/Cataclysm-DDA](https://github.com/CleverRaven/Cataclysm-DDA)，基于官方 `0.I-1`（`7b2efa5cea38e4d4d97dd0e63b28b9148623da59`）。
- 原版主要代码和游戏内容采用 **CC BY-SA 3.0**；字体、依赖等有各自许可。参阅 [LICENSE.txt](LICENSE.txt)、[版权和素材说明](docs/ATTRIBUTION.md)、[原版 README](docs/UPSTREAM_README.md)。
- 分发包仅应包含明确允许再分发的内容，并保留相应声明；不包含用户个人存档、开发测试记录或未经确认授权的外部图块素材。

欢迎提交 [Issue](https://github.com/Slime11123/cdda-modern-mode/issues) 帮助完善项目。
