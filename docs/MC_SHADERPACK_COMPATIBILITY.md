# CDDA 3D — Minecraft / Photon 光影兼容层

日期：2026-11-08。真实功能范围：**有限的 MC 原始 final 着色器支持 + 复杂 Photon ZIP 的 CDDA 视觉适配**。这不是完整 Iris/OptiFine/Photon 运行时，不能保证所有 Minecraft 光影包原样显示。

## 使用方法

将 MC 光影包 ZIP 或解压后的文件夹放入游戏 EXE 同目录的 shaderpacks 文件夹，运行新版 cataclysm-tiles.exe，进入 3D 模式，按 F5/F6 → 光影包 → 重新扫描/重载，再选择光影包。可以直接点“打开光影包文件夹”。设置独立保存在 shaderpacks 目录的 .selected、.quality、.shadows。

## 兼容方式

1. **直接 final.fsh（有限兼容）**：对可独立运行的 Minecraft/OptiFine GLSL 1.20 final shader，读取 shaders/world0/final.fsh 或 shaders/final.fsh，绑定场景颜色到 colortex0/gcolor、深度到 depthtex0/gdepthtex，在单一最终 Pass 中执行原始片元代码。不能直接运行依赖额外材质、include、G-buffer、多颜色缓冲的复杂程序；失败时安全回退。
2. **Minecraft/Iris/Photon ZIP 视觉桥接**：只读解析 shaders.properties、settings.glsl 和 final.fsh 识别格式与部分功能开关。使用 CDDA 项目自己的 HDR、色调映射、Bloom、深度 AO/雾、动态太阳照明、近场 PCF 阴影、天空方向驱动程序化云、简化阳光光束等效果。界面标记“视觉适配”，不冒充原版 Photon composite/gbuffers/shadowcomp GLSL 正在运行。
3. **原有 CDDA 光影包**：文件夹或 ZIP 中的 post.frag / shaders/post.frag，继续按 CDDA 原生接口运行。旧内容不受本次兼容层影响。

## 本机真实测试

- 用户已有 Photon v1.4 ZIP（3,857,333 字节、645 个 ZIP 条目），路径为 runtime/modern-vehicles-roof-v7/shaderpacks/photon_v1.4.zip。
- Photon GitHub 源码 ZIP（7,449,538 字节、434 个条目），路径为 Downloads/Compressed/photon-main.zip。
- 两份文件均已复制进 runtime/modern-3d-shaderpacks/shaderpacks，仅本地游戏测试使用，未推送 Git。
- GPU OpenGL 4.6：简单的 MC GLSL 120 final.fsh 源代码直接运行，红色通道输出 45，测试通过。
- Photon v1.4 真实 ZIP：编译并执行 CDDA 适配片元，独立 GPU RGB 为 193/121/45（没有假称执行其原版全部 Pass）。
- 两份 Photon ZIP 在 ShaderDaylightLab 独立白天存档都得到 2520×1392 真实游戏截图；shader usable=1、post FBO 有效、极高质量太阳阴影贴图有生成。相对于关闭光影，约 89.63% 像素改变。
- 视觉桥接新加世界空间方向驱动的层云，图片中天空已明显呈现云层，而不是单纯的颜色滤镜。
- RTX 4080 SUPER 的 1080p 单独后处理成本约为低/中/高/极高 0.085/0.115/0.151/0.159 毫秒，不包括游戏场景绘制、太阳阴影贴图、CPU 帧时间和 UI，不等于整游戏 FPS。
- 参考截图：qa/mccompat-photon_v1.4.png、qa/mccompat-photon-main.png。
- GL 自测：60 次 FBO 重建、热重载、HDR、深度采样、坏 GLSL 回退、合法/非法 ZIP 检测通过；ENABLE_FP3D=ON 已成功构建并链接。最新本地游戏 --jsonverify 退出码为 0，git diff --check 也为 0。独立 QA 游戏的默认选择已设置为 Photon v1.4、极高画质、实验性阴影开启。

## 现阶段未覆盖

原版 Photon 的 shader 世界分支、完整 G-buffer（材质 ID、法线、金属度、粗糙度、发光、运动矢量）、colortex0–15、多阶段 deferred/composite、Compute、真实 Photon 体积云、真正的 SSR/GI/labPBR、历史缓存及任意完整自定义 Iris 着色器执行。不能保证任意下载的 MC 光影 ZIP 都能保留其独有画风。完整兼容需要先改造 CDDA 固定管线为可输出多 render target 的着色管线。

ENABLE_FP3D=OFF 配置的此次完整重建遇到本机 freetype2 头文件路径错误（ft2build.h）；这是开发环境依赖，不能记为最新 OFF 构建通过。旧版 OFF 回归曾通过，但这一轮应单独修复再确认。

## 继续开发的路线

1. 几何 Pass 输出可见实体的材质、法线、发光、粗糙度/运动矢量，不泄露原版不可见的 NPC 或物品。
2. 安全的 GLSL include/宏预处理、真实多 Pass 计划、MRT attachment、buffer flip 与 temporal history。
3. 逐步运行原始 Minecraft composite/deferred/shadow，优先简单包，再按模块接入 Photon；涉及专有材质的阶段缺数据时必须标注“不支持”。
4. 测试 2D/3D 切换、地图与物品可见性、窗口 resize、Alt+Tab、动态阴影、1080p 全帧 GPU/CPU 时间及第三方光影许可证。

说明：原生 Photon 和相关第三方资源仅从用户电脑复制到本地测试目录，未复制其代码或纹理到正式 Git 仓库，也未创建公开 Release。Photon 作者项目：https://github.com/sixthsurge/photon。

## 2026-11-08 Photon 视觉强度增强与实景 A/B 回归

玩家实机截图反馈 Photon 适配光照不够明显，已增强复杂 Minecraft/Photon 视觉桥：调整太阳直射光与环境光对比、可见深度接触遮蔽、PCF 太阳阴影、亮部 Bloom、轻量太阳光束/晴空光晕、远景雾和有界色彩对比。不会修改原版 Minecraft ZIP、CDDA 2D 或直接执行的 final.fsh。

新增 F5/F6 → 光影包 → MC 视觉增强 (%)：50–200%，默认 145%。保存于游戏 EXE 同级 shaderpacks/.intensity，修改后下一个 3D 世界帧读取 uniform 生效。建议先用 145–170%，投影不准确可以单独关闭实验性太阳阴影。不是 Photon 原生设置。

同一 ShaderDaylightLab 白天场景、同一 Photon v1.4 ZIP、同一 camera，真实游戏 FBO 2520×1392 截图完成 65/145/185% 三档对照；每档 usable=1、post_fbo>0、shadow_texture>0、OpenGL 读取错误 0。
- 65% 与 145%：99.785% 像素发生变化，RGB 通道平均绝对差 5.034 / 8.570 / 12.867，平均亮度 114.27 → 117.32。
- 145% 与 185%：99.653% 像素变化，RGB 平均绝对差 3.069 / 6.738 / 8.980，平均亮度 117.32 → 120.22。
- 185% 截图亮度 251–255 比例 0.0%；为避免暗部过黑，额外限制了阴影压暗幅度和对比曲线。
- 测试图：qa/photon-strength-65.png、photon-strength-145.png、photon-strength-185.png；差异图：qa/photon-strength-difference-65-145.png、photon-strength-difference-145-185.png。
- GPU 验证：NVIDIA OpenGL 4.6 Shader 自测 PASS，60 次 FBO 尺寸更新、mc GLSL final、损坏 Shader 回退保持通过；CMake 3D ON 编译链接成功；--jsonverify exit 0；原 CMD 启动器指向更新后的 EXE。
- 1080p Photon 适配后处理极高档独立 GPU 平均约 0.160ms/post，只包含后处理，不等于全游戏 60FPS。
- QA 期间所有测试选项恢复原字节，原存档未被覆盖，也没有 Git 提交/推送。

限制：这是 CDDA 视觉桥，不等于原版 Photon/Iris 多 Pass、真正 SSR/GI、PBR 或体积散射。多天气/昼夜/动态建筑场景还未全面验收。

## 2026-11-08 阴影错位与伪遮挡修复（P3 增量）

用户反馈目前 Photon 适配的阴影存在异常。本次修复直接进入 CDDA 3D 实现，并保留现有车辆、贴图和存档：

- 统一相机：场景绘制现在和后处理的深度逆投影共用同一帧 fp3d_camera::offset（visual_camera），包含乘车时的 boarded_camera 平滑跟随；修复之前画面使用视觉平滑相机而太阳阴影使用普通 camera.relative_to 的不一致，避免移动/乘车时投影偏移。
- 真正的薄门板：原版阴影将普通关闭门及破损门错误视为 1m 厚实心墙；现按 fp3d_snapshot::door_parts 真实绘制 0.16m 薄门板及损坏时的所有存留部件，孔洞允许太阳透过。门方向按视角渲染已有的许可邻居规则判断，且预建 known_walls 仅一次，避免门数量增多时反复扫描所有 cell。
- 校正普通墙与障碍阴影轮廓：墙约 0.98m 宽，障碍约 0.98m 宽/1.0m 高，匹配主画面几何，不再使用先前过胖或过高的近似遮挡体。
- 阴影边界处理：深度重建后验证 receiver 在光源近平面、远平面内；投影坐标靠近深度图边缘时平滑衰减，不再把光源体积之外 clamp-to-edge 的样本当成黑影。
- 抗锯齿从 2×2 PCF 扩为对称 3×3 PCF，缓解阴影边缘硬阶梯；偏差随太阳高度轻度变化。Photon 桥接下阴影压暗程度上限约 48%，避免过黑，但仍属于简化近场阴影而非真正 Iris/Photon 原渲染。
- 所有新增深度投影仅来自拥有且可见的地图快照，无额外模拟数据查询；不显示原本不可见的实体。

已完成的测试：
- UCRT64 ENABLE_FP3D=ON 编译/链接 exit 0；实际游戏数据 jsonverify exit 0。
- OpenGL 4.6 GPU 合成场景：阴影关闭/开启像素红通道 204→161；故意将接收点放至光源深度体积之外后红通道仍为 204，PASS；60 次后处理帧缓冲尺寸变换、GLSL 重新编译、CDDA ZIP 和有限 MC final Pass 均通过。
- 白天隔离游戏实景，近场太阳阴影开关 A/B：开=非零 shadow_texture，关=0；变化约 0.9662% 像素，限制在实际建筑/屋顶附近，没有大面积天空/路面异常变暗。测试图位于 qa/shader-ui-shadow-enabled.png、qa/shader-ui-shadow-disabled.png、qa/shader-ui-shadow-difference.png。
- Photon v1.4 和 Photon-main 两份真实 ZIP，在 ShaderDaylightLab 测试存档仍能成功加载：usable=1、post FBO 非零、shadow_texture 非零，2520×1392 帧缓冲画面有效，实时程序未崩溃。此前提及的约 99.79% 画面差异是完整 MC 视觉后处理相对于 Off，不是阴影单独的幅度。

**尚未全面验收**：真实乘车时连续转向与相机平滑的长时间视觉测试、建筑多层屋檐、透明材质与植物精确投影、复杂天候与日出低角度阴影、最终整游戏 FPS。3×3 PCF 比 2×2 多读 5 次深度纹理，需要后续全帧 GPU 性能对照。GPU 独立单测/截图 A/B 不代表玩家所有地图问题都解决。

## 2026-11-08 下一阶段：车辆真实网格阴影 + 车辆屋顶幽灵阴影修复

本阶段已直接完成以下本地源码改动：

- 移除 src/fp3d_shadow.cpp 原有每零件一个粗方块的车辆阴影代理 draw_vehicle_occluders()，改为调用 fp3d_vehicle::draw_shadow(scene,radius)。新的深度阴影 Pass 直接复用车辆可见不透明 geometry(v)、相同 root(v,scene) 姿态与现有 topology_signature() / display list 缓存：斜引擎盖、真实车顶缺口、侧窗边框、车轮、开门及破损车辆保留原模型几何。只有没缓存的离屏遮挡车辆才临时调用同一个 geometry(v)。透明 alpha-blended 车窗 draw_glass() 不作为实心遮挡体。
- fp3d_vehicle::draw_shadow() 绑定与原 3D 绘制相同的材质 atlas 并在结束后解绑，避免未预先建立显示列表的车辆丢失贴图遮挡信息；阴影 Pass 本身仍保留 COLOR_MASK=false，只写深度。
- 新增 fp3d_scene::vehicle_roof_sheet_suppressed(position,render_generation)，把主场景的车辆头顶“悬浮屋顶”隐藏结果共享给太阳阴影。当主场景判定一片顶部平面是车辆上方的鬼影并抑制绘制时，阴影 Pass 现在也不再提交这片不可见平面。带 render_generation 校验，避免切换世界/快照时误用旧掩码。真实桥梁、建筑楼层仍按现有判定保留。
- 源数据仅来自游戏已有的、原版许可显示的 scene_snapshot，不修改地图、生物、车辆数据、车身模型或存档。

验证范围和诚实限制：

- Windows UCRT64 ENABLE_FP3D=ON 完整重新编译并链接 EXIT=0。
- NVIDIA OpenGL 4.6 原有 HDR/深度阴影/Photon 后处理/MC final GLSL/60 次帧缓冲尺寸变更回归 PASS。
- QA vehicle-shadow-exact-mesh-qa.json：共 16 项结构性静态断言通过，包含同一刚体姿态、透明层排除、材质绑定、车顶真实缺口、光照覆盖范围和屋顶掩码共享；validate_vehicle_v7_roof_glass.py 的 14 项车顶/玻璃测试通过。
- **车辆画面 A/B 还不能宣布通过**：本轮隔离 --world RangedWeaponsLab/ShaderDaylightLab 两次启动均停在菜单，没有输出完整场景截图；并非被证实的 GL 失败。需要在实际车辆附近进行屏幕对照、不同车况与乘车动态测试，才可确认视觉投影效果。
- 当前游戏启动器已经指向独立新程序 runtime/modern-3d-shaderpacks/cataclysm-tiles-shadow-v2.exe。由于 Windows 拒绝覆盖原 cataclysm-tiles.exe（可能仍被运行程序或其他进程占用），**没有强行终止进程、没有覆盖原文件**。新程序复用同一 runtime 目录中的 DLL、gfx、data、shaderpacks，启动脚本原来的 runtime/0i1/user 存档目录保持不变。--jsonverify 新程序退出 0，PowerShell 启动脚本语法错误 0。
- 本地未执行 Git commit/push，也未提供或上传第三方 Photon ZIP 到 GitHub。当前 Photon 是 CDDA 视觉适配，不代表原版 Iris 多 Pass 完整支持。

接下来仍需：在真正含车辆的测试场景进行日照角度、车身旋转、损坏和车门开闭的阴影 A/B；多层楼板/阳台的原生可见结构阴影精度；阴影 Pass 的整游戏 GPU 帧时间对照以及长时间窗口切换稳定性。

## 2026-11-08 Raymarch v3: scene-depth SSR + shadow-map volumetric sunlight

复杂 Photon 视觉适配已新增独立真实地形材质反射 FBO 和场景深度 SSR（雨天道路和晴天水面）、太阳阴影贴图遮挡体积光（8 步），以及各自的 F5/F6 光影设置开关。实机原场景含 4 辆车/156 个车辆部件，QA 模拟湿路后反射材质像素 1,407,887；真实 FBO 截图 qa/raymarch-v3-photon-real-frame.png 和 qa/raymarch-v3-photon-wet-qa.png。测试仅改 QA 视觉湿润条件，不改天气系统或存档。并非原版 Iris 多阶段光影或离屏光追；详细说明见 docs/FP3D_DEPTH_RAYMARCH_RENDER_PIPELINE.md。

## 2026-11-08 v4 室内外日照与 Photon 连续性修复

太阳深度阴影、SSR、8 步体积光不再被玩家是否处于室外整屏禁用；全场 ambient 不再按玩家跨门槛跳变。把实际天空是否可见与玩家是否在室外分开。测试中的真实 Photon v1.4 游戏画面在 QA 模拟室内标志后，太阳阴影仍有效且窗外道路平均亮度比 1.000。完整说明与限制见 docs/FP3D_INDOOR_OUTDOOR_DAYLIGHT_FIX.md。

## 2026-11-08 v6b 日间室内补光与连续湿路反射

保留路灯、邮箱、车辆实际阴影。Photon 桥现在不会在每个 1m 道路格子留下反射裂缝；湿路 SSR 由连续弱光膜和柔化的附近屏幕空间反射组成。遮阴处的日间间接光更明亮，但无遮阴路面不整体提亮。实机同场景屋檐下平均亮度较 v5 增约 9%，雨天反射遮罩有效 1,484,014 像素。详情：docs/FP3D_DAYLIGHT_WET_ROAD_V6B_QA.md。
