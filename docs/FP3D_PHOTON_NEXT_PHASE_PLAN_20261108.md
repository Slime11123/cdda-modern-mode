# CDDA 3D Photon 风格光影下一阶段实施计划（P3–P6）

> 版本：2026-11-08 v1.0（在现有 v18 墙面法线修复、v10b 双级太阳阴影、Raymarch v3 / 湿路 v6b 上继续）
> 状态：**待实施的后续计划**，不是本文件所列功能已完成的证明。本文件只规划，不修改现有源代码。
> 工程位置：CDDA-0I-FP3D；影响面限定 ENABLE_FP3D=ON 的 3D 渲染；保留 2D、存档与图块包兼容性。
> 概念区分：CDDA 原生后处理 ≠ 有限 MC final.fsh 兼容 ≠ Photon ZIP 的 CDDA 视觉适配 ≠ 原版 Iris/Photon 全功能运行。

## 0. 现状快照与已知边界

| 模块 | 已有实现 / 证据 | 未关闭风险 |
|---|---|---|
| P1 后处理 | HDR RGBA16F、深度纹理、Bloom、色调、GLSL 后处理、关闭与编译失败回退；文档 FP3D_SHADERPACKS_IMPLEMENTATION.md | 多窗口/长时间 GPU 资源测试仍需补 |
| P2 天气昼夜 | 原版太阳/月相、天气、室内外连续照明与湿路效果；文档 FP3D_DAYLIGHT_WET_ROAD_V6B_QA.md | 夜间及极端天气真实地图覆盖 |
| P3 阴影 | Ultra 双级 2048²/28m + 1536²/96m、3×3 PCF、真实车辆网格阴影、纹素对齐；v18 修复近墙假阴影 | 透明/alpha-cutout、复杂楼层、贴近墙/转向回归 |
| P4 反射体积 | SSR 深度步进最多 16 步、8 步遮挡体积阳光、独立地面反射材质 FBO、湿路连续性 v6b | 离屏内容缺失、跨帧闪烁、天空与水面波纹真实性 |
| P5 材质 | 反射遮罩有材质分类与粗糙度近似；屏幕空间可由深度重建几何法线 | **无完整逐材质法线/roughness/metalness atlas、真正 BRDF、motion vector/TAA** |
| P6 性能 | 已有 GPU smoke、局部后处理开销与若干真实游戏 A/B | 无统一完整帧 GPU/CPU 基线、跨地图/设备 FPS 验收 |

**验收原则**：现有文档中的 QA 截图、GPU smoke 及 BUILD_EXIT=0 只能支持其对应场景；不能推断用户遇到的所有阴影问题已消失、不能当作全游戏 60 FPS 证据。P3–P6 可以并行进行低风险审查，但必须按下列依赖合并到默认启动器。

## 1. 不变的工程约束

1. 必须保留 CDDA 原版回合制、AI、命中、游戏时间、RNG、感知与存档语义。渲染输入**只读**已有 scene_snapshot；不可借 G-buffer、阴影/反射 Pass 额外显示原本不可感知的 NPC、物品、室内或地下数据。
2. 材质来源必须随当前图块包切换。UndeadPeople 等图块**不强制**提供 Minecraft labPBR 数据；任何缺失辅助贴图都使用中性法线与按类别的安全默认值。
3. HUD、准星、菜单、背包、健康、地图和文字不得被 Bloom、TAA、光照或 SSR 污染；只处理 3D 世界 FBO。
4. OpenGL/GLSL 的实际兼容最低线以当前 SDL2 借用上下文与 GLSL 1.20 自测为准；不能未经检测就要求 GL 3.3、MRT、compute 或 Vulkan。功能能力检测后分级启用；失败恢复当前安全路径。
5. 不覆盖在用 EXE、不清理用户存档、不上传第三方 Photon ZIP/贴图或用户素材至 GitHub。不因为计划完成而自动发布。
6. 先保证默认 Low/Medium、原生 Off、F5 2D/3D 的稳定性，Ultra 才启用耗费较高的实验效果。独立回滚开关应保持可用。

## 2. 推荐依赖与落地顺序

P3-R 阴影稳定性收尾 → P5-A 材质注册表与辅助纹理离线/按需缓存 → P5-B 几何材质输出 →
P5-C 法线/PBR 光照 → P4-R SSR 空间稳定化 → P4-T 可选时域反射 → P4-V 体积光/天空细化 →
P6 性能与发行验收。

- 可以先做 P5-A 而不改变画面；先验缓存正确性，再接入渲染。
- P5-B 是真正的架构分界线：现有固定管线仅有颜色/深度，不包含逐像素材质 UV 与切线；**不能把屏幕空间几何法线恢复误写成法线贴图已经生效**。
- P4-T 依赖可靠深度、法线、前后帧矩阵和变化区域标识；如果做不到，保留空间滤波 SSR，不强推 TAA。
- Photon 全量多 Pass 兼容不作为 P3–P6 的默认交付要求，作为后续独立研究议题。

## 3. P3-R — 双级阴影的正确性和画面回归（优先级：最高）

**相关源码**：src/fp3d_shadow.cpp/.h、fp3d_shaderpacks.cpp、fp3d_scene.cpp、fp3d_vehicle_renderer.*、fp3d_compositor.cpp。

| 子任务 | 实现动作 | 验收闸门 |
|---|---|---|
| P3-R1 近墙与多楼层 | 将 v18 的归一化导数法线修复设为回归基线；增加侧墙、墙角、阳台/楼板底面、井道、室内窗框的固定视角 A/B | 近/远距离与旋转视角不出现跟随视角变化的黑色三角；正常投影仍在 |
| P3-R2 几何一致性 | 校核墙厚/门板洞口/窗/扶手/塔架/车壳，保证可见模型与 shadow caster 同 transform、开闭状态和缺损情况 | 门洞和车辆空洞不会填满黑影；四层及更高层下方投影与可见层对应 |
| P3-R3 Alpha/透明 | 先给围栏、叶片和交叉精灵建立可选 alpha-test shadow；透明玻璃默认不作为实体遮挡 | 不会投下整块矩形“纸片阴影”；玻璃不成为黑色实体 |
| P3-R4 数值稳定 | 以斜率/光照方向调整 bias、接收法线、近远 cascade 混合与边缘衰减；维持世界纹素锁定 | 走格、转头、远近切换与太阳低角度无明显 acne、漏光、阴影跳跃 |
| P3-R5 可控降级 | 影子关闭、Ultra 双级失败→原单级 38m、后处理失败→无光影路径；保留独立选项 | 任意失败不崩溃、不黑屏、F5 及保存照常 |

**完成判定**：与 v18、v10b、车辆 v7/v9f 参考在相同世界、时间、相机、分辨率下形成截图三联（Off/旧版/新版）和 GPU 深度图统计。人工检查不能由单一像素差分取代。

## 4. P5-A — 自动法线与材质参数缓存（最先实施的 P5）

**目标**：在不要求第三方资源包附带 normal map 的情况下，为**当前实际采用的图块**准备可选纹理与材质参数，但暂不宣称画面已有真实 PBR。

**推荐新模块**：src/fp3d_material_registry.h/.cpp、src/fp3d_material_aux.h/.cpp（文件名建议，尚未创建）；接入 src/fp3d_materials.cpp/.h 中已有 tileset/revision/lookup/bind 生命周期。

1. **材质键**：由当前 tileset 标识 + atlas revision + 纹理来源/UV 及季节/变体 + 几何表面类别组成。不同图块包、分辨率、纹理变体禁止串缓存。优先复用内存，不要求写磁盘。
2. **语义分类**：将表面分为石/砖、沥青、泥土、草、木、金属、玻璃、水、布、通用/未知等；允许少量 JSON/规则覆盖，不把任意彩色像素直接推断为金属。
3. **默认值**：未知材质用中性切线空间法线 (0.5,0.5,1.0) 与保守 roughness；玻璃、水、金属按类别单独设参数，所有值显式 clamp。不得把 alpha 通道当高度图。
4. **自动法线生成**：先对 tile sprite 去噪、可控灰度/高度推断，再用 Sobel/Scharr 求局部梯度；强度按材质调节；像素风小图低强度或直接中性，避免纹理文字/图案被误识别为凸起。
5. **边缘保护**：每张图块独立带 padding/邻域 clamp 后生成，处理 atlas UV 采样泄露、透明边缘、旋转/镜像 UV；不能直接跨 atlas 不相关方块取梯度。
6. **辅助纹理格式**：优先建立与原 color atlas 相同 UV 排布的独立 normal + ORM(roughness/metallic/可选 AO) 辅助纹理；真实尺寸/通道/GPU 格式在能力检测后选择。禁止改动原版 atlas 原始像素。
7. **失效与预算**：tileset 热切换、季节变化、图块缓存 revision、GL context loss、重建时释放/重建；缓存总大小、生成耗时和冷启动尖峰必须可记录。优先仅生成可见材质并渐进填充。
8. **人工辅助输入（后置）**：未来允许同图块包旁的可选自有 normal/ORM 覆盖；明确不自动打包别人制作的版权贴图。

**验收闸门 P5-A**：
- 两款不同图块包热切换往返 20 次，normal/ORM 贴图 key 与 UV 对应正确；回到 Off 无多余 GL 资源。
- 深/浅纹理、纯色、透明树叶、道路条纹、砖墙、车辆贴图有独立 Debug 预览，法线向量可归一化，无紫色/黑斑；纯色应接近中性。
- 禁用法线时画面与基线在允许误差内一致；**此阶段不以画质提升作为完成证据**。

## 5. P5-B — 为可编程光照补齐几何材质输入（结构性任务）

**关键现状**：src/fp3d_scene.cpp 当前保有固定管线/显示列表，src/fp3d_compositor.cpp 有 HDR/depth FBO、已有反射材质 mask FBO；post-process 无每一面的切线/UV/物体 ID。先实现兼容桥，不立即全量重写渲染器。

- 定义可见表面数据：世界法线、UV、材质索引/roughness、可见物体类别，必要时反射/历史稳定性 ID；优先不透明建筑墙/地面/屋顶，后接车辆和复杂家具。
- 选择实现前必须做小原型比较：
  - A：以现有 scene depth 配合**额外材质/法线 Pass** 输出表面属性（GL 2.1 可行但重绘几何成本高）；
  - B：逐步把不透明几何改为可编程顶点/片元并输出多 attachment（MRT 需要实测扩展与 context 支持）；
  - C：混合路径，旧 display list 绘制未覆盖的复杂物体，缺失数据回退几何法线/材质默认值。
- 不能用屏幕坐标代替真实 UV；墙、天花、地面及车辆必须各有正确 tangent/bitangent 构建，处理镜像、相反朝向及 UV 旋转。
- 为可见性/遮挡源建立一次明确的只读范围和 pass contract，所有额外 pass 与主画面共用相机/深度测试和 scene generation。
- 后处理接口新增只读选填的 surface-data handles、有效性/尺寸/格式标识；shaderpacks 自定义 post.frag 不应因此失效。
- 输出统一 Debug View：color / world-normal / normal-map / roughness / material ID / depth / shadow cascade / reflection mask / SSR confidence。Debug View 仅开发配置可用，不能混进正式玩法 HUD。

**验收闸门**：固定相机下墙、地面、车辆可视表面 material ID 与颜色 atlas UV 对齐；经过多层天花板、损坏墙体、切换图块包后不出现模型颜色错位或新曝光；对比开关材质 Pass 的完整 GPU 帧时间，超出预设预算则停止扩大覆盖范围。

## 6. P5-C — 真正的法线光照与渐进 PBR

1. 优先在**墙/地面/屋顶**上使用切线空间法线，变换到世界空间后参加太阳直射/环境光与反射 BRDF；必须**保留几何法线**用于阴影 receiver bias，不能把高频细节法线直接用于 shadow map 偏移造成 acne。
2. 使用线性光照空间处理 diffuse/specular，显示之前再经现有 HDR tonemap/gamma；小心 SDL 原贴图的 sRGB 与浮点 FBO 格式区别，避免灰蒙蒙或过饱和。
3. 先采用 roughness + metallic 的受限 GGX/Schlick，保持能量守恒；没有反射探针/环境立方图时金属不能凭空获得精确室内环境镜像。水/玻璃保持独立透明方案。
4. 室内间接光和 AO、接触阴影不可重复相乘形成黑墙；添加 PBR 强度、法线强度、roughness 覆盖的独立设置，默认保持原 Photon 桥接色调。
5. 道路/砖墙/金属/木板各做 Normal On/Off 同机、同帧、同曝光 A/B；如果生成图使贴图失去像素风格，应能按材质/图块关闭。

**验收闸门**：法线细节随太阳/局部光变化，而非随摄像机转头随机转动；常见墙面无 v18 类型黑三角回归；同一场景 PBR Off 保留当前版本的暖色、亮度和阴影美感。

## 7. P4-R/T — 反射空间稳定 + 可选时域路径

### P4-R 优先：不依赖历史缓冲的 SSR 稳定化
- 复用现有材质 FBO 中 roughness/湿度与遮挡深度；完善深度厚度阈值、边缘置信度、法线朝向以及接触处 SSR 断裂处理。
- 现有最多 16 次深度步进升级时先比较 16/24/32 质量档，限制射线长度；粗糙表面用受限模糊/分层采样，避免整块反光发白。
- 雨天连续路面必须保持 v6b 的无 1m 网格接缝原则，不能引入随镜头平移的分块膜；干路不凭空湿润，水面与道路走不同参数。

### P4-T 后置：时域重投影/历史
- 在有前帧 view-projection、上一帧深度/法线/表面签名、有效 world origin 之前**不得**启用历史累积。车辆等动态物体未提供运动向量时禁用该区历史，禁止拖影。
- 重投影反向检验深度、法线、材质 ID 和遮挡变化；对屏幕外历史/脱屏像素直接拒绝。
- F5 2D/3D、Alt+Tab、resize、世界重载、存档切换、传送、跨层、贴图包更换、切换光影/质量档、GL 重建时强制清空历史；镜头瞬转或天气突变按阈值降低权重。
- 如果时域在视角快速摆动、乘车、狭小门廊中效果更差，保留 P4-R 作为默认，TAA/temporal SSR 只允许 Ultra 自愿开启。

**验收闸门**：雨天道路、玻璃旁、车辆旁转头、进入建筑、快速开关背包、楼层切换各录连续 30–60 秒；比较时域开关下的闪烁和拖影，同时记录额外显存和帧耗。不能仅凭静帧声称稳定。

## 8. P4-V — 体积光/天空及高质量雨夜（后置）

- 现有 8 步、shadow-map 遮挡体积太阳光先做分辨率预算/动态采样。光线穿过窗户、路灯、细框时要求方向一致，室内不因 avatar outdoors 标记切断室外光。
- 程序云/天空雾改进时遵循天气、原版昼夜、室内可见天空；无数据时采用保守视觉近似，不宣传为 Photon 原始物理大气/体积云。
- 点光源和夜雨仅从原版允许感知/已加载的光源派生：灯柱、火焰、手电等，限制每块区域最大灯数，不能额外感知暗处实体。
- 水面 Fresnel、微波法线优先低成本可选；半透明顺序/窗玻璃优先于高成本彩色全局光照。
- GI / LPV / 光线追踪 / 完全执行 Photon shadercomp 不列入这次默认版本目标。

## 9. P6 — 性能、回归、发布门槛

### 测试场景矩阵（固定时段/摄像机/图块包/分辨率）
| 场景 | 重点 |
|---|---|
| ShaderDaylightLab 及原先异常墙面 | v18 黑三角、材质法线稳定、28m/96m 级联 |
| 市区四车/街灯/邮箱 | 实体阴影、网格车辆、SSR、GPU 负载 |
| 室内走廊/窗框/门槛 | 天空可见性、玻璃、AO 过黑、SSR 边界 |
| 三到五层建筑/塔/天花 | 远近多层可见，楼板投影，层间穿透 |
| 雨天湿路/水面/污水/晴朗路面 | SSR 膜无网格、干湿区分、反射伪影 |
| 夜间与低角度日照 | 光源预算、太阳阴影偏移与雾 |
| 两款图块包热切换及 F5/F6 | cache 重建、材质一致性、GL 资源回收 |

### 三类不可混淆的性能指标
- **整游戏**：1080p/固定 GPU，平均 FPS、1% low、p95/p99 全帧耗时、CPU 游戏/提交/渲染和 GPU 全帧耗时；目标尽量 60 FPS (16.7ms/frame)，须实测而非保证。
- **子 Pass**：阴影近/远级、材质生成、surface pass、SSR、体积光、Bloom、PBR 各自 GPU ms 与显存 MB；已有“后处理单次 0.xms”**不等于**全游戏成本。
- **资源与稳定性**：加载后首 10 分钟冷/热缓存、连续 30 分钟实机，100 次 F5/F6/质量切换、50 次 resize/Alt+Tab（建议门槛，尚未测试），记录 GL errors、峰值显存、持续增长、崩溃。

### 质量档预计策略（配置建议，实际按测量调节）
| 功能 | Low | Medium | High | Ultra |
|---|---|---|---|---|
| HDR / tonemap | 安全回退 | On | On | On |
| Bloom / AO | 最少采样/可关 | 限制采样 | 提升采样 | 高质量 |
| 法线 | Off | 可选弱法线 | On（简单表面） | On（更多表面） |
| PBR 光照 | Off | 仅材质默认粗糙度 | 可选受限 BRDF | On（满足 GPU 预算时） |
| 太阳阴影 | Off 或最小化 | 单级 | 单级或按预算升级 | 现有双级 CSM |
| SSR | Off | Off/低采样 | 可选空间 SSR | 高质量 SSR |
| 时域 SSR | Off | Off | Off | 实验开关（稳定后才默认） |
| 体积光 | Off | Off/低步数 | 可选低步数 | 8 步或自适应 |

### 每阶段完成必备文件与回滚
1. 新功能放独立开关，Off 与当前画面可做 A/B；失败自动回退并写明错误原因，不能因为 Shader 编译失败出现黑屏。
2. docs/ 和 qa/ 中有实际测试记录：编译命令/退出码、硬件/分辨率、运行版本或 commit、参数、同场景截图、帧耗、问题列表、回退方法。
3. ENABLE_FP3D=ON 构建/链接及 --jsonverify；ENABLE_FP3D=OFF 回归（如有环境依赖失败如实记录，不能冒充通过）；原版 2D、F5、存读档、玩家感知安全性专项检查。
4. **不自动 commit/push、不替换玩家正在运行的 EXE、不触碰原始存档**。合并至实际启动器必须再次比较新旧 EXE SHA256 并完成真实 3D 游戏验证。
5. 任一门槛失败仅回退该阶段开关或新模块，既有 v18、v10b、v6b 的画面行为必须可恢复。

## 10. 最小可执行工单顺序

| 工单 | 预计修改点 | 本工单可交付与停机条件 |
|---|---|---|
| 01 — 修订回归 | fp3d_shadow / shaderpacks + qa | 原位置近墙、多层、车辆阴影矩阵；不通过先修 P3 |
| 02 — 材质分类表 | fp3d_materials + 新 material registry | 实际 tileset 分类、默认 roughness/metalness、独立单元测试 |
| 03 — 法线/ORM 生成 | 新 material_aux + atlas revision hook | atlas 预览与缓存/切包测试；暂不接着色 |
| 04 — 材质表面 Pass 原型 | fp3d_scene / compositor | 同 UV / 法线方向 / depth 绑定，单场景 GPU 预算对照 |
| 05 — 渐进 PBR | shaderpacks / 新 shader pass | 墙/地面法线能见度及 Off 对照，GL 能力不足自动禁用 |
| 06 — SSR 稳定化 | mcshader + reflectivity FBO | 雨天连续路面和运动录像对照，避免新格子裂缝 |
| 07 — 时域实验 | compositor/history optional | 运动矢量或安全拒绝历史机制；不通过不上默认 |
| 08 — 光照精修与全套 QA | shadow / sky / post + qa | 多天气/多层/跨图块包全帧 FPS，回退与最终发布检查 |

**最先动手**：01（固定当前版本真实阴影回归）与 02（只读材质分类）。不要在未建立有 UV/切线/表面属性的管线前，直接宣称已实现 Photon 的法线贴图。尤其不要以彩色贴图边缘检测代替真实材质信息而改坏用户喜欢的现有阴影/色彩。

## 11. 关联现有文档

- FP3D_PHOTON_SHADER_FEASIBILITY.md — 初始 P0–P6 目标。
- FP3D_SHADERPACKS_IMPLEMENTATION.md — 当前已实现 OpenGL/Photon 视觉桥。
- MC_SHADERPACK_COMPATIBILITY.md — 原生/有限 MC final/Photon 视觉桥的严格边界。
- FP3D_DUAL_CASCADE_SHADOW_V10B.md — 双级阴影参数及 QA。
- FP3D_PHOTON_ULTRA_WALL_NORMAL_V18.md — 真实墙面阴影根因及 QA。
- FP3D_DEPTH_RAYMARCH_RENDER_PIPELINE.md、FP3D_DAYLIGHT_WET_ROAD_V6B_QA.md — SSR 与体积光现状。

> 本文件中“计划”“推荐”“验收闸门”均指未来要做的事；只有第 0 节为截至 2026-11-08 的既有源码与记录的归纳。


## 2026-11-08 施工追踪：工单 01–03 首轮

已真正修改源码：新增 src/fp3d_material_aux.h/.cpp、为 fp3d_materials.h/.cpp 加入当前图块 atlas 的按需 auxiliary(id) CPU 法线/ORM 数据与缓存失效机制。本轮**未启用 PBR 画面效果**。

- 工单 01（阴影回归）**阶段性通过**：车辆深度几何 16/16 静态检查；新编译游戏在 ShaderDaylightLab、Photon v1.4 Ultra 的实际 GPU/3D 场景显示 4 辆车、156 部件和有效双级阴影，开启/关闭画面有实际差异；近墙阴影 On/Off/旋转测试 v18 的 ROI 差异约 (1.724, 1.614, 1.322)，未回归为原黑色斜三角。用户原截图地点及长期动态仍未验收。
- 工单 02（材质分类）**第一版实现**：道路/石/土/木/金属/玻璃/水/植被/车辆/未知类别，保守 roughness 与 metallic 默认参数；材质分类 C++ 单测通过。
- 工单 03（自动法线）**CPU 生成功能完成，GPU 接入待办**：按需从本图块包 atlas 采样单一 64×64 tile，生成弱法线和 ORM，透明边缘保护，热切换/季节变化释放缓存；10/10 结构检查通过。尚未创建 GPU Normal/ORM atlas、切线及逐材质着色输出，不产生新画面效果。
- 工单 04–08 尚未实施。ENABLE_FP3D=ON 新程序 CMake/Ninja 构建链接成功、隔离 Photon GPU 回归通过；原 EXE/玩家存档/启动器没有覆盖，也没有推送 GitHub。

**具体源码、QA 命令/输出、截图和下一步闸门**：参见 [FP3D_PHOTON_P3_P5A_EXECUTION_20261108.md](FP3D_PHOTON_P3_P5A_EXECUTION_20261108.md)。下一次实施从工单 03 的 GPU 辅助 atlas 与工单 04 的表面属性 Pass 开始，不能将现有 CPU 法线误称为已上屏 PBR。


## 2026-11-08 第二轮执行：GPU 辅助图集与 P5-B 调试预览

继 CPU 自动法线/ORM 之后，新增 GPU Normal/ORM 双 RGBA8 atlas，2048×2048，与 color atlas 的 64×64 图块槽对齐；仅在隔离 QA 标志下预算生成 32 槽并上传到 OpenGL。第一次真实 GPU 读回揭示 SDL/OpenGL pixel pack/unpack 行距状态问题，修复并增加状态归还后，实际 glGetTexImage 与 CPU 数据对应验证 PASS。Photon v1.4 Ultra 实景中近/远级阴影仍有效；QA 小窗口已经能直接采样显示两张 GPU 材质贴图，关闭 QA GPU 功能时无额外图集分配。Windows ENABLE_FP3D=ON 编译链接通过；P5-A 10 项、车辆阴影 16 项及 GPU 接口 13 项检查通过。

**阶段仍未完成 PBR 画面光照**：没有逐材质表面 UV/切线/G-buffer 的 world Pass，不能说普通游戏中的墙面已出现真实 normal map。下一工单为单墙/地面 Surface Pass 原型及受控法线光照，不应现在把 QA GPU 纹理默认打开给玩家。完整实施/QA/回退记录见 [FP3D_PHOTON_P5_GPU_ATLAS_EXECUTION_20261108.md](FP3D_PHOTON_P5_GPU_ATLAS_EXECUTION_20261108.md)。


## 2026-11-09 第三轮执行：工单 04 的 Surface Pass 原型

已真正落地独立世界空间表面法线与粗糙度输出：新建 src/fp3d_surface_pass.h/.cpp，通过只读当前 snapshot 的可见地面/墙面几何、原场景深度、GPU Normal/ORM 图集与 GLSL 1.20，生成 RGBA8 Surface FBO（RGB=世界法线，A=roughness），并有 QA 右上角纹理采样预览。

在 ShaderDaylightLab + Photon v1.4 Ultra 实机的主场景/原深度关联验证中，近远阴影均保留；视锥裁剪后提交 833 地面表面、60 墙面，FBO 有效 2,442,992 像素。其中 12,879 像素为向上法线、2,430,113 像素为竖直墙面法线（近墙测试）；这是从真实 GPU 输出读回的方向类别，而非提交面数的猜测。正常 Photon 画面不开 Debug 预览时差异近于零，预览开启时右上角能直接看到结果。CMake ENABLE_FP3D=ON 构建、实际 OpenGL 回归、17 项代码约束检查 PASS。

**重要限制**：该实现仅在显式 QA 环境变量下运行。当前 GPU→CPU 整张图读回、方向统计的原型路径实测 15–68ms CPU/驱动同步耗时，不可作为默认游戏 PBR 渲染。P5-C 仍需去掉正式路径 readback、量测 GPU Pass 耗时、实现独立可关闭的 BRDF/法线受光，并继续推进墙外其他复杂几何。没有玩家 EXE 更新或 GitHub push。

完整源码、测试证据、性能限制及下轮工单见 [FP3D_PHOTON_P5_SURFACE_PASS_EXECUTION_20261109.md](FP3D_PHOTON_P5_SURFACE_PASS_EXECUTION_20261109.md)。
