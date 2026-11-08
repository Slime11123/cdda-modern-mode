# CDDA Modern Mode — Alpha 0.3.2

**版本：`alpha0.3.2` | 2026-11-09 | Windows x64 | CDDA 0.I-1（Ito-1）**

> 预发布测试版。项目由 OpenAI GPT 生成并协助开发，是 CDDA 的非官方衍生版本。本次更新整理 Alpha 0.3.1 之后的源码开发成果；尚未对全部游戏世界、显卡、模组或存档完成全面人工验收。

## 本次源码更新

### 现代生存菜单、背包与地图
- 优化综合菜单的背包及真实口袋树形展开操作，调整装备/容器行的交互与样式。部分展开状态可使用角色已有变量保留，不更改原版物品和背包判定。
- 改进综合菜单地图的交互，包括鼠标拖动平移、右键定位上下文操作，以及基于原版 overmap note 的彩色 X 标记。备注与颜色通过原版备注格式持久化，不新增独立地图存档。
- 顶部罗盘可显示附近部分已保存地图备注的彩色 X 方位指示，限制显示数量，避免每帧扫描全部备注。
- README 新增开发者提供的现代背包实机截图（原始图像，无重新生成）。

### 车辆 3D 车身轮廓
- V12 车身拓扑改进：依据原版车辆实际完好车架的逐排连接关系计算车壳与顶棚轮廓。
- 对不连续、破损或不规则的车架避免凭空补齐封闭外壳；必要时退回已有模块式车体绘制。
- 改进车头、车尾、玻璃、车顶边界以及不同车辆宽度的衔接；车辆驾驶、碰撞与存档规则继续由原版处理。

### Photon 风格光影的后续研发（默认不启用）
- 为现有图块材质增加实验性辅助法线与 ORM（遮蔽、粗糙度、金属度）数据生成、材质图集以及 GPU 上传/校验通道。
- 增加面向真实地面和普通墙面的独立表面属性 FBO 测试通道；在隔离 QA 中检查法线方向和纹理采样。
- **重要限制：** 这些模块仍由专用 QA 环境开关控制。普通游玩不默认运行额外的表面 Pass、同步像素读回或完整 PBR 着色，不能称为已实现完整 Minecraft Photon/Iris 光影包支持。
- 继续保留现有 HDR、太阳阴影、雨天反射等已发布画面路径，避免让新的材质试验影响正常玩家的色调和性能。

## 玩家安装（Windows x64）

1. 前往 [GitHub Releases](https://github.com/Slime11123/cdda-modern-mode/releases) 下载 `CDDA-Modern-Mode-alpha0.3.2-Windows-x64.zip`（不是 GitHub 自动生成的 Source code ZIP）。
2. **完整解压**到新的可写目录，直接运行真正的 `cataclysm-tiles.exe`，不要依赖 CMD、第二个 EXE 或开发环境变量。
3. 保留 DLL、`data/`、`gfx/`、`lang/`、`config/` 等随包内容；升级前先备份原版 `user/` 存档及配置。
4. 在游戏内使用 F5/F6 进入画面、3D、现代操作、HUD 和实验性光影设置；不同图块包的外观与兼容性可能不同。

## 第三方资源和已知局限
- [Photon 光影包（Modrinth）](https://modrinth.com/shader/photon-shader/versions) 与 [UndeadPeople 图块包](https://github.com/Xenikos/UndeadPeopleTileset) 为第三方资源。**本仓库与 Windows ZIP 均不提供、不捆绑或重新分发**；用户自行获取并遵守许可。
- Photon 仍属于 CDDA 的实验性视觉适配，**不是完整原版 Minecraft Iris/OptiFine 多阶段光影引擎**。
- 车辆复杂几何、全部地形与地图备注、图块包切换、旧存档升级、多种 GPU/DPI、长时间生存和 Photon 版本兼容仍需更广泛的实机回归。
- 构建源码提交、ZIP 大小与 SHA-256、实际已通过的测试范围以本版 GitHub Release 验证附录为准；未执行的测试不计入“通过”。

---

## English summary

**Alpha 0.3.2** adds work-in-progress improvements to the modern inventory and pocket trees, overmap note editing with colored X markers, drag-to-pan map controls, and nearby compass markers that reuse native CDDA notes. Vehicle V12 geometry follows intact native frame topology and falls back to modular rendering for broken or disconnected shells. Experimental normal/ORM auxiliary materials, GPU atlases, and a surface-attribute framebuffer are available only in gated QA paths; **full Photon/Iris multi-pass execution and default-on PBR shading are NOT implemented**.

The repository README also features an **unaltered developer-supplied inventory screenshot**. This is an unofficial Windows x64 pre-release of CDDA 0.I-1. Directly launch the one real `cataclysm-tiles.exe` in the fully extracted ZIP. **Photon and UndeadPeople are optional external downloads and are not bundled.** See this release's GitHub asset hashes and verification notes for what was actually tested.
