# CDDA Modern Mode — Alpha 0.2.0

**版本 Tag：`alpha0.2.0` · 更新范围：2026-11-05—2026-11-07 · 平台：Windows x64 · CDDA 基线：0.I-1 / Ito-1**

> **实验性预发布（Pre-release）**：本项目由 OpenAI GPT 生成并协助开发，是 CDDA 的非官方衍生项目，不属于 CDDA 或 OpenAI 官方发行。以下区分“源码已实现”与“实际游玩验证”；请勿将编译成功等同于全面稳定性验证。

## 11 月 5 日 — 3D 模型、空间与交互

- 新增/修正 3D 卫浴设施、椅子朝向、玻璃门、破损门及更多 UndeadPeople 风格家具模型与材质。
- 为洗衣设备、木炉、圆凳和玻璃顶冷柜提供更具体的 3D 几何、贴图与朝向；完善楼梯表面、连续阶梯与上下楼方向提示。
- 扩展墙面、血迹、细围栏和楼层连接的视觉呈现；加入针对尸体拾取的简易背包交互。
- 修复 Windows 玩家版单 EXE 启动的历史问题：真正的 `cataclysm-tiles.exe` 无需额外 CMD/启动器即可按 Modern Mode 的默认设置启动 3D（此前的 Alpha.1—Alpha.3 发布验证存在缺陷）。

## 11 月 6 日 — 操作界面、怪物图鉴与天气

- 3D 行动分类改为带分页、返回层级的环形菜单；模型扩展固定梯、排水管、外立面和上层结构。
- 扩展 3D 实体交互、创建角色/设置相关流程与测试代码；对齐 HUD 消息框与状态栏。
- 新增基于原版天气状态驱动的 3D 降水、云、雾、闪电及相关渲染状态恢复，避免影响摄像机画面。
- 开发中的 Tab 生存菜单加入**怪物图鉴**：读取运行时怪物类型（包含当前加载模组）、类别与搜索、属性、图块预览及随存档保存的击杀数。默认 JSON 扫描统计为 **1123 个怪物定义 ID**；不等于同一世界能生成的怪物数量。
- 中英文 README 增加 UndeadPeople（不死人）**推荐但不捆绑**声明，加入用户提供的 3D 实机截图。

## 11 月 7 日 — 制作颜色与新家具修复

- 修正现代生存菜单中制作/建造需求提示，尽量复用原版对“满足/不足”材料、工具的彩色标记。
- 继续完善展示柜（只保留下方柜体）、文件柜底层抽屉、自动售货机、工作台、钢琴、台球桌、铁砧等真实 3D 外形。按贴图的正面/顶部区别选择材质，避免把二维伪 3D 画面直接拉伸成不正确体积。
- **石窑炉 V4**：按小石块逐层围砌曲面窑体，保留真实中空的拱门，并加上可见的内部静态火焰；该火焰目前仅为视觉效果，尚未与点火状态、温度或伤害进行完整联动。
- 累积整理模型/材质/构建/图鉴的开发记录与回归检查；部分模型尚缺完整实机视觉验收。

## 安装和启动

1. 从 [GitHub Releases](https://github.com/Slime11123/cdda-modern-mode/releases) 下载名为 `CDDA-Modern-Mode-alpha0.2.0-Windows-x64.zip` 的 Windows 游戏附件。GitHub 自动生成的 Source code ZIP 不能直接游玩。
2. 将 ZIP **完整解压**到具有读写权限的新目录，不要在压缩包内运行程序。
3. **直接双击真正的 `cataclysm-tiles.exe`**；无需 CMD、额外启动器 EXE 或预设环境变量。保留随包的 DLL、`data/`、`gfx/` 和翻译文件。
4. 在 F5/F6 统一设置中更改 2D/3D、经典/现代操作与 HUD 选择。更新前先备份 `user/` 中的存档和个人配置。

## 推荐图块包与版权

为获得更好的视觉体验，推荐 [UndeadPeople（不死人）图块包](https://github.com/Xenikos/UndeadPeopleTileset)。**本项目不提供、不捆绑、不重新分发该外部图块包**；请自行下载，遵守其许可和署名，并注意版本兼容性。

保留 CDDA 0.I-1 与随包字体/库的原始许可和署名；不包括用户存档、QA 缓存、Photon 光影、额外下载的未知授权素材。有关开源许可，请参见 [项目许可说明](ATTRIBUTION.md)。

## 测试范围与已知限制

- Windows x64 游戏应以 `TILES=ON`、`ENABLE_FP3D=ON` 从**本次提交对应的源码**重新编译，不能复用旧 Alpha.4 的 EXE。
- 发布前应完成：编译链接、必要的资源/JSON 校验、**从 ZIP 全新解压后无参数直接双击唯一游戏 EXE**、标题/窗口模式/OpenGL 与 FP3D 初始化检查、ZIP 与 SHA-256 校验。
- 3D 图鉴图像、怪物击杀显示、复杂家具与石窑炉、广泛模组兼容、长时间生存及所有 UI 操作仍可能存在未完成的实机验收。单纯编译成功不得宣称所有玩法稳定。
- 最终 SHA-256、实际测试结果与提交 ID 以对应 GitHub Release 附件和发行页面为准。

---

## English summary

**Alpha 0.2.0** consolidates November 5–7 development: expanded first-person 3D furniture, bathrooms, stairs, ladders and building details; paged action wheels and character/interaction improvements; native-weather 3D precipitation/clouds/fog/lightning; an in-development monster bestiary with searchable runtime monster definitions and kill counts; color-preserving crafting/construction requirements; revised vending cabinets, display/filing cabinets and a curved stone kiln with a visible interior flame.

The Windows package uses a **single, real `cataclysm-tiles.exe`** that should start the 3D/OpenGL view directly in a resizable window without a CMD launcher. This is an experimental preview, **not a fully gameplay-certified stable release**. We recommend UndeadPeople for visuals, but **do not bundle, provide, or redistribute** its tileset; obtain it separately from its maintainers.
