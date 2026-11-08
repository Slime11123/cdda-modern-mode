# CDDA 3D 接入 Photon 风格光影：源码可行性审查与实施路线

> 审查日期：2026-10-30。范围：本机 `CDDA-0I-FP3D` 当前源码、用户上传的 `（推荐）photon-main(1).zip`。**本文是可行性与开发计划，不是已接入或已测试 Photon。**不修改原版规则、存档、AI 或探索判定。

## 1. 结论

- **可实现 Photon 风格的光影效果**；**不能把原 Minecraft 光影 ZIP 直接放到 CDDA 并启用**，也不宜声称能零改动跑完整 Photon。
- 建议采用「现有 SDL2/OpenGL 3D 合成器 + 新增可切换的可编程场景/后处理管线 + 按效果移植 Photon GLSL 思想/合规代码」。
- 优先实现色调映射、Bloom、雾、昼夜照明、基础太阳阴影；再实现 SSAO、体积光、反射、云。最后才考虑更重的 LPV/体素彩色光照、TAA、PBR。
- 不重写 2D、游戏逻辑与图块包系统；3D 光影参数独立于游戏存档。启动失败时保留 2D 和无光影 3D 路线。

## 2. 当前代码真实状态（审查 2026-10-30）

| 文件 | 已有能力 / 需要改变 |
|---|---|
| `src/fp3d_compositor.cpp` | 借用 SDL2 `opengl` renderer 的当前 GL context；`SDL_RenderFlush` 前后封装，独立 RGBA8 color texture + depth24 **renderbuffer** FBO，一次 SDL Present。下一阶段需要 HDR 颜色纹理、可采样的 depth texture、可选 normals/motion、ping-pong FBO、shader 管理。|
| `src/fp3d_scene.cpp` | `glBegin`、`glCallList`/display lists、固定管线 `GL_LIGHTING`；环境光 .35、漫反射 .75、固定方向 (-.4,.8,.5)。基本法线已写入部分箱体/地面。需要按 `scene.sky.sun_*` 统一昼夜和方向，并将不透明、透明、天空/精灵、shadow depth pass 分离。|
| `src/fp3d_sky.cpp/.h` | 原生游戏时间、太阳方位/高度、月相已转换为演示数据，天空为颜色渐变几何球和太阳/月亮盘。具备天气/体积云拓展入口，但未实现 Photon 大气散射和云。|
| `src/fp3d_snapshot.cpp/.h` | 拷贝式只读场景快照；有地形、层高、室内顶面、窗、植物、精灵、远景及 `sky`；**没有可供光影直接用的点光源列表、天气参数和法线/材质属性**。必须只从原版允许的数据生成表现层数据，不能因光影泄露黑暗或墙后的物品、NPC 信息。|
| `src/fp3d_materials.cpp` | 由当前 CDDA 图块包实时绘入 1024² / 64² RGBA SDL atlas，`SDL_GL_BindTexture` 供 3D 使用；保留换图块包能力。现只有颜色信息，**不等于** Minecraft labPBR 的 normal/specular/emissive 贴图。可以先做按材质类别推断的粗糙度，再增加可选 PBR 数据映射。|
| `src/fp3d_camera.h` | 120° 横向视场、近平面 0.05、远平面 1536；TAA 与 SSR 必须处理大深度范围及平滑移动导致的历史失效。 |
| `src/sdltiles.cpp` | 原版 SDL 窗口/2D/ImGui 路径。后处理**必须只作用于 3D 场景 FBO**，最后合成到原版窗口；不能把背包、地图、准星、字体等 UI 做景深或泛光。|
| `src/CMakeLists.txt` | 3D 特性受 `ENABLE_FP3D` 保护；保持 OFF 构建无新 OpenGL 依赖、ON 才启用新模块。|

**当前技术障碍：**颜色 FBO 是 RGBA8（不适合 HDR 亮度范围）；depth24 是 renderbuffer（后处理 shader 不能直接采样）；渲染无正式的 shader program/geometry pass；display lists + 旧 fixed-function 状态切换多；资源释放依赖借用 SDL context。

## 3. 上传 Photon ZIP 的检查

- ZIP 完整性已检查（无损坏条目），约 7.4 MB、402 个条目；其中 133 个 `.fsh`、133 个 `.vsh`、8 个 `.csh`、84 个 `.glsl`。
- 存在 `shaders/world0/`、`world1/`、`world-1/`、`include/`、`program/`、`settings.glsl`、`shaders.properties`，对应 Minecraft 多世界、多个合成及材质/阴影渲染阶段。
- 例如 `world0/composite.fsh` 是 `#version 400 compatibility` + `#include "/program/composite0.glsl"`；`shadowcomp.csh` 使用 `#version 430`。GLSL 的 `#include` 路径、`gbuffers_terrain`、`shadow`、`composite`、`MC_VERSION`、`block.properties` 等都依赖 Minecraft/Iris 提供的数据和装载约定。
- 光影配置定义了 Low/Medium/High/Ultra，包含 shadowMapResolution 1024/2048/4096 等；**把源文件编译进 CDDA 并不自动得到正确阴影/反射**，必须先建立兼容的输入/Pass/attachment。
- 压缩包 `LICENSE` 为 SixthSurge 的 MIT 授权；内含 `include/aces/license.md` 为另外的 ACES 授权文本。复用时保留两者的适用版权和授权声明，并逐一检查额外纹理/噪声等第三方资源；不可仅因为 GitHub 仓库开放便忽略标注。
- 此 ZIP 为用户上传的源码快照，不包含可靠的 git commit/tag；不要在技术文档中把它直接认作 2026 年最新 Photon 发布版本。

## 4. 推荐渲染链（保留原本 2D 合成）

```text
原版游戏逻辑 --(只读快照/原版光照与天气参数)--> 3D scene
                                                |
       geometry opaque+alpha-cutout ---> scene HDR FBO (+ depth texture)
          shadow depth pass ---------> directional shadow map
       sky / fog / transparent ------> HDR scene (独立合成顺序)
                                                |
       SSAO (optional) -> fog/volumetric (optional) -> bloom -> tone map
                                                |
                     LDR 3D texture -> 现有 fp3d_compositor 合成
                                                |
                2D/3D 模式 UI 继续由 SDL/ImGui 绘制管理
                                                |
                              原版唯一 SDL_RenderPresent
```

- 优先维持现有 borrowed context + `saved_gl_state` 边界；创建 VAO/VBO/GLSL programs 也需要显式状态归还、resize/context loss 重建和错误回退。是否转向独立 GL 主合成器，应作为性能/维护的后续决策，不是首阶段的前提。
- Shadow pass 需独立输出深度：先只让不透明建筑/地面遮挡阳光。植物 alpha-test 深度、玻璃透光及 2D billboard 生物投影另行处理，避免随玩家镜头旋转的“纸片影子”。
- TAA/SSR/屏幕体积雾必须用可采样深度，必要时增加 normal/roughness/motion 数据；角色切换 F5、存档加载、传送、楼层变更、图块包切换、窗口 resize 时清空历史。
- 天光动态值由 `scene.sky` 的实际日月驱动；天气/火源/手电/城市路灯从原版可见/已知数据提取，不额外计算回合、不修改原版 RNG 和行动点。
- 远景仅是 overmap 粗略轮廓，不能对其采用近景精确阴影、材质反射、车辆投影等假设；切分近景高质量和远景廉价 Pass，防止 1536 远平面导致极大阴影需求。

## 5. 分阶段实施优先级

| 阶段 | 交付 | 验收条件 |
|---|---|---|
| P0 能力审计 | 记录 GL 版本/GLSL/FBO/扩展、当前 FPS、GPU 帧耗和窗口/图块包切换；保留基线。 | ON/OFF 均能构建，原版不回归；记录 1080p 室内/街道/城市典型基线。|
| P1 可编程后处理 | 独立 shader loader、错误回退、HDR RGBA16F、深度纹理、ping-pong targets；ACES/可选色调映射、Bloom、gamma。 | 3D 有明显效果但菜单文字不变；F5/重设尺寸/Alt+Tab 不黑屏；关特效结果可比对。|
| P2 昼夜与雾 | 太阳/月相驱动方向与强度、曝光、晴雨/室内雾；夜间火光/手电只走显示层。 | 不出现“午夜和正午一样亮”，黑暗房间不展示原版未感知的实体。|
| P3 阴影与 AO | 太阳方向 CSM 或受限 shadow map（先一张/近景）+ PCF 软阴影；GTAO/SSAO 从低成本实现起步。| 屋檐/窗/多层建筑阴影正确，室内暗处无穿墙漏光；远景/精灵无镜头跟随投影。|
| P4 Photon 风格 | 体积阳光/雾、天空层云、水面着色/折射/近景 SSR；优先改写适配的独立 GLSL 程序。| 雨天/晴天、湖岸/街道/地下室多场景可用；无深度边界闪烁、伪反射、透明顺序错乱。|
| P5 高画质选项 | HDR 演化、PBR 可选额外纹理、TAA/FXAA/CAS、彩色点光源；暂不强制 LPV/体素 GI。 | 低/中/高/极高四档可切换；相机移动、切换图块包、跨层后的 temporal history 正确清理。|
| P6 性能与发行 | GPU profile、半分辨率 AO/体积光、缓存、动态光源预算、显存监控；独立打包许可证。| 目标在用户 GPU 1080p 尽量稳 60fps，但**须以实机跑分决定是否达标**，不预设通过。|

## 6. 对 UI 与图块包的明确约束

1. 不替换 `fp3d_materials` 的当前图块包来源。不同图块包复用同一渲染器；缺少 normal/specular 等属性时使用安全默认值，不能强制 Minecraft 格式。
2. UI、血量、背包、指令文字和互动提示一律不受 Bloom/景深/曝光影响，暗处 UI 必须依然清楚。
3. 黑暗房间仍允许绘制此前原版允许的结构外壳以避免穿帮；原版不可感知的怪物、物品、详细上层空间绝不因为阴影图/反射图/G-buffer 被揭示。
4. 要显式保留 F5 原版 2D 与 3D 切换，OFF 构建行为不变；若 shader 编译失败只关闭新效果，不能导致游戏崩溃或保存失败。
5. 任何渲染层代码不得改变走格/战斗/驾驶/回合、存档格式、游戏主 RNG；不得把视觉上的灯光和原版“看见/发现”机制混同。

## 7. 工程量与短期建议

- **最小可展示版（P0-P1）**：中等工程量，适合优先实现，能看到比目前明显更有层次的画面。
- **阴影、室内雾、天候、体积光（P2-P4）**：中高工程量，依赖多 Pass、缓存和不同层级几何处理。
- **完全兼容 Photon ZIP 作为插件热加载**：接近为 CDDA 重建 Minecraft/Iris 兼容渲染输入层；成本和风险高，**不建议**把它定为项目目标。
- **推荐立即执行的下一任务**：只对 `fp3d_compositor.cpp` 增加独立 shader/program 生命周期和场景后处理管线，先实现可禁用的 Bloom + 色调映射；不同时推入阴影、体积云、SSR 和完整 PBR，保持现有项目的迭代和回归可控。

来源：用户上传 Photon ZIP 内 README/LICENSE/GLSL/配置；本地 `src/fp3d_*.{cpp,h}` 当前源码；Photon 官方项目 https://github.com/sixthsurge/photon；SDL2 文档 https://wiki.libsdl.org/SDL2/SDL_RenderFlush 。

## 2026-11-08 实施追踪（本段晚于原始可行性审查）

已在当前开发源码中实现第一批可编程后处理、HDR+可采样深度目标、原生 GLSL 后处理包扫描及 ZIP 加载、Minecraft 风格 F5/F6 列表、质量档位、关闭/编译失败回退，以及使用原版太阳/月光快照驱动的场景光源方向和强度。实际变更和 OpenGL 测试记录见 [FP3D_SHADERPACKS_IMPLEMENTATION.md](FP3D_SHADERPACKS_IMPLEMENTATION.md)。

后续同日的更新已经推进到 P3 双级太阳阴影与 3×3 PCF、P4 屏幕空间 SSR 和阴影遮挡体积阳光；v18 处理了近墙假阴影，v6b 修复了雨天道路反射格子裂缝。但完整逐材质法线/PBR、时域历史稳定性、原版 Iris/Photon 多 Pass 兼容及 P6 全帧性能/多设备验收仍未完成。不能把 CDDA 的 Photon 视觉桥描述成“原版 Photon 已完整移植”；所有验证仅对相应场景和 QA 日志成立。**继续开发的工单、依赖顺序与验收标准请以 [FP3D_PHOTON_NEXT_PHASE_PLAN_20261108.md](FP3D_PHOTON_NEXT_PHASE_PLAN_20261108.md) 为准。**
