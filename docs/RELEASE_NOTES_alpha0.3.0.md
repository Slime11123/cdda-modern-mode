# CDDA Modern Mode — Alpha 0.3.0

**Git tag:** `alpha0.3.0` · **更新范围：2026 年 11 月 7–8 日** · **平台：Windows x64** · **CDDA 基线：0.I-1 / Ito-1**

> **实验性预发布版本**。项目由 OpenAI GPT 生成并协助开发，属于 CDDA 的非官方衍生项目。这里的“已实现”指源代码中有对应逻辑，不代表所有地图、显卡、硬件和玩法都经过完整人工验收。

## 相对于 Alpha 0.2.0 的主要更新

### 11 月 7 日：车辆与 3D 场景改造开始

- 持续补充高低车辆、车身外壳、驾驶舱、车顶、玻璃、座椅与车架结构；对部分不同方向车体的连接、浮空面和穿模进行修正。
- 扩展家具、街景与建筑结构建模，并增加实机调试和回归记录。实际显示效果与选用的贴图包有关。
- 为后续现代输入、车辆交互和 Photon 风格渲染建立测试与文档基线。

### 11 月 8 日：车辆、结构与跨楼层可见性

- 继续修正车辆车壳包裹不全、车内座椅与车体之间的空隙、车框架显示异常，以及不同结构的材质贴图。
- 补充无线发射塔、铁丝栅栏及其他可见结构的 3D 表现；改进从地面和上层观察多层建筑时的楼板、天花板与下层墙壁绘制。
- 减少地板/天花板与墙、门之间的重合面和随视角移动出现的闪烁；修正部分楼层背面及金属网格下表面光照异常。
- 靠近可攀爬的水管可使用 E 交互选择攀爬方向，继续使用原版的行动和判定规则。

### 11 月 8 日：太阳阴影、Photon 风格光影与反射

- 扩展 HDR、后处理、深度及材质相关的渲染链；增加昼夜太阳方向、多级级联阴影试验、阴影 PCF 与材质法线相关修复。
- 调整室内外光照、墙面随视角变化的斑块阴影、近距离阴影、无线塔/路灯/车辆的投影与阳光遮挡。
- 改善白天室内太暗、湿路反射呈块状、路面灰色矩形、墙壁阴影三角形、画面灰蒙等问题；尝试保持原有更鲜明的 Photon 风格明暗对比。
- 可以从游戏的 F5/F6 光影菜单扫描 `shaderpacks/` 下的光影包，在条件允许时使用 CDDA 自身的天空、阳光、阴影、Bloom、雾和屏幕空间反射实现 **Photon 视觉适配**。
- **兼容边界：** 这不是 Iris / OptiFine / Photon 的完整多 Pass 渲染器，不会保证直接执行 Photon 的全部 `gbuffers`、`deferred`、`composite`、阴影和历史缓冲等着色器。

### 11 月 8 日：现代输入、HUD 与受击反馈

- 3D 射击改为对着鼠标准星触发原版开火，保持原版枪械技能、散布、弹药与结算逻辑；部分需要选择方向的操作由 E 对准目标确认。
- 3D 模式按 Z 蹲伏，视角高度平滑降低；保留原版角色姿态及行动规则。
- 玩家状态效果图标可显示在现代 HUD 左上角的身体健康示意图下方，图标来源优先使用当前图块包中对应的状态覆盖图块。
- 怪物受伤后尝试显示短暂的闪光反馈、伤害和剩余 HP 信息；命中伤害仍由原版游戏规则决定。
- Tab 可进入并关闭综合生存菜单；菜单地图尝试使用已加载图块包的地图图标。怪物图鉴、地图样式和状态图标仍需检查不同图块包的完整覆盖情况。

## 下载及使用

1. 从 [GitHub Releases](https://github.com/Slime11123/cdda-modern-mode/releases) 获取 `CDDA-Modern-Mode-alpha0.3.0-Windows-x64.zip`，不要使用 GitHub 自动生成的 Source code ZIP 当作游戏包。
2. 完整解压到新的可写入目录，**直接双击唯一的 `cataclysm-tiles.exe`**。无需 CMD/附加启动器，需保留所有随包 DLL、`data/`、`gfx/`、`lang/`、`config/`。
3. F5/F6 可以选择 2D/3D 模式、现代操作、HUD 和实验性光影质量。升级前备份旧版本的 `user/` 存档和配置。

## 推荐第三方资源（**不包含在本仓库和游戏发行包中**）

- **Photon 光影包：** 为获得更好的光照、阴影和氛围，建议尝试作者 sixthsurge 的 [Photon 正式下载（Modrinth）](https://modrinth.com/shader/photon-shader/versions)，或查看[作者 Github 仓库](https://github.com/sixthsurge/photon)。自行下载 ZIP 放入游戏 `shaderpacks/`，然后在 F5/F6 光影设置扫描选择。该操作触发的是 **CDDA 的 Photon 风格视觉适配**，不是完整原版 Minecraft 光影兼容。
- **UndeadPeople（不死人）图块包：** [维护者仓库](https://github.com/Xenikos/UndeadPeopleTileset)。由用户自行安装；本项目不提供、不捆绑、不重新分发。

第三方素材的许可和署名以各自项目为准。CDDA、内置图块/字体及运行库的版权声明见本项目 `docs/ATTRIBUTION.md` 和随游戏发行的许可证。

## 构建与测试说明

- Windows 源码构建需开启 `TILES=ON`、`ENABLE_FP3D=ON`；本次玩家 ZIP 应由**对应 Git 提交**编译，不能拿 Alpha 0.2.0 或 Alpha.4 的旧 EXE 冒充新版本。
- 发布前检查：编译、JSON 数据验证、全新解压 ZIP 后直接运行游戏 EXE、OpenGL/FP3D 帧缓冲初始化、普通可调整大小窗口、ZIP 与 GitHub 附件 SHA-256 一致。
- 仍未全面验收：全部复杂车辆、无线塔与多层建筑、图块状态图标、怪物血量反馈、所有地图贴图、长时间生存、多显卡兼容、第三方 Photon ZIP 的各版本兼容和存档迁移。
- **正式源码提交号、玩家 ZIP SHA-256、已执行的验证范围以 GitHub 此版本 Release 页面的校验附录为准。**

---

## English summary

**Alpha 0.3.0** (November 7–8, 2026) expands 3D vehicles, roofs, frame/cabin geometry, radio towers, thin fences, multistorey ceiling/floor rendering and climbing interaction. It improves HDR and lighting, experimental cascaded sunlight shadows, wall shadow artifacts, indoor/outdoor brightness and wet-road reflections; introduces Photon-inspired visual adaptation for selected third-party Minecraft shaderpacks, **not full Photon/Iris multipass execution**. Recent source changes add crouch eye-height smoothing, monster damage/HP feedback, effect icons near the health silhouette, and survival-menu map tileset icons, all still subject to fuller testing. The single Windows `cataclysm-tiles.exe` remains the direct game launch target.

**Photon and UndeadPeople are recommended but neither is included or redistributed**. Obtain them separately from their respective maintainers. This is an experimental pre-release, not a fully gameplay-certified stable release.
