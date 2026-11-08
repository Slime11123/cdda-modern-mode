# CDDA Modern Mode — 3D 光影与 shaderpacks 接入记录

> 2026-11-08。此文记录已实现的可编程光影**与待完成的高级光照**，不应将现有兼容层误称为完整 Minecraft Iris/Photon。基础方案见 [Photon 光影可行性审查](FP3D_PHOTON_SHADER_FEASIBILITY.md)。

## 已完成（开发源码）

1. **F5/F6 → 光影包**：在统一模式设置中进入光影选择界面；支持刷新、复制目录路径、单击选择、错误状态和质量档位（低/中/高/极高）。
2. **游戏 EXE 相邻目录读取**：通过 `SDL_GetBasePath()` 在游戏目录下创建或扫描 `shaderpacks/`；无需环境变量、命令行或额外启动器。选择写入 `shaderpacks/.selected`，质量写入 `shaderpacks/.quality`。
3. **支持两种原生包形式**：文件夹 `shaderpacks/包名/post.frag`（也接受 `shaders/post.frag`）；ZIP 包内根目录 `post.frag` 或 `shaders/post.frag`。ZIP 解析只读取单个允许文件，不解压到磁盘，限制 ZIP32、32 MiB 最大包、128 KiB GLSL 源码、1 MiB 压缩文件、512 个目录条目，并校验 CRC32。支持 Stored 与 Deflate。
4. **内置功能**：关闭光影、柔光、暖色电影；使用 GPU GLSL 1.20 可编程后处理，包含 Bloom、色调映射、基于深度的近景局部遮蔽及与天气关联的轻量雾化，质量档动态调节采样数。
5. **场景色彩与深度**：3D 场景的浮点 HDR RGBA16F 颜色缓冲、可采样 DEPTH_COMPONENT24 深度纹理，HDR 不可用时退回 RGBA8（仍要求 FBO 完整）。
6. **原版天文/天气驱动**：场景环境光与定向光依据太阳高度、月相、云量/闪电变化；3D 雾参数由原版天气只读快照提供。玩家感知和世界模拟没有被后处理代码修改。
7. **安全回退及解耦**：不兼容的 Minecraft 光影 ZIP 在选择界面标注“不兼容”；自定义 GLSL 编译/链接或 FBO 错误时退回无后处理的 3D；界面和 HUD 不做后处理；仅在 `TILES + ENABLE_FP3D` 时构建。F5/F6 模式、存档、2D 功能未被改写。

## 安装使用

```text
游戏目录/
  cataclysm-tiles.exe
  shaderpacks/
    .selected               # 自动生成，保存光影选择
    .quality                # 自动生成，保存质量档位
    自定义光影.zip
    自定义文件夹/
      post.frag
```

进入一个已加载的世界，在 **3D 画面模式**按 **F5/F6**，点 **“光影包…”** → “刷新光影包列表” → 选择“关闭光影”或包名；可使用“复制文件夹路径”并粘贴到 Windows 资源管理器打开。此页面允许光影在运行时切换；不要求重启游戏。

GLSL 后处理约定（与 Minecraft OptiFine/Iris 并不等价）：

- 顶点输入由项目提供，片元代码须使用 `#version 120`。
- `varying vec2 v_uv`：UV 坐标；`uniform sampler2D u_scene`：3D 世界颜色贴图。
- 可选 `uniform sampler2D u_depth`，`uniform float u_has_depth`：深度纹理是否可用。
- 可选 `uniform vec2 u_texel`，`uniform float u_time`，`uniform float u_quality`（0–3）。
- 可选 `uniform float u_daylight`（0–1）、`u_clouds`、`u_fog_end`、`u_outdoors`：来自原版游戏的只读表现数据。
- 输出 `gl_FragColor = vec4(color, alpha);`，建议保留源画面的 alpha。每个原生包暂仅支持 1 个完整片元 shader，无外部 include、compute、G-buffer、多 Pass 和用户自定义纹理。

示例 `post.frag`：

```glsl
#version 120
uniform sampler2D u_scene;
varying vec2 v_uv;
void main() {
    vec4 c = texture2D(u_scene, v_uv);
    gl_FragColor = vec4(c.rgb * vec3(1.05, 1.02, 0.96), c.a);
}
```

> **Photon / Iris / OptiFine 兼容性说明**：原版 Minecraft 光影包的 `gbuffers_*`、`composite*.fsh`、`shadow`、`shaders.properties`、世界分支、各种 `#include`、labPBR 等，需要完整的 Minecraft 渲染输入数据与 Pass 调度。仅把它的 ZIP 拖到 CDDA `shaderpacks` 目录并不会有 Photon 光影效果。目前会被识别但不可选择，不会误报可用。本文代码是 CDDA 原生后处理包格式。Photon 相关外部源码/纹理**没有**被复制或打包。

## 本机验证

- Windows UCRT64 GCC 16.2 / SDL2 / NVIDIA OpenGL 4.6：`ENABLE_FP3D=ON` `cataclysm-tiles.exe` 成功编译链接。编译日志：外层工作区 `qa/shaderpacks-build.log`。
- 真实 OpenGL 隐藏窗口：验证 GL 2.1 兼容着色器的编译/渲染后像素差异；HDR 颜色 + 可采样深度 FBO + 天气 uniform 渲染；ZIP 内 native post.frag 可编译、Minecraft ZIP / 路径穿越伪条目不可选、坏 GLSL 不崩溃并回退。日志/脚本：外层 `qa/shaderpack_gl_smoke.cpp`、`qa/_compile_shader_smoke.py`。
- 已制作独立 QA 运行版 `runtime/modern-3d-shaderpacks/`：单独的最新瘦身 EXE（约 47.6 MB）、现有 DLL 的硬链接、`data/gfx/lang` 本地 junction，独立 `user` 和 `shaderpacks`，带自制演示 ZIP/文件夹。该目录**依赖开发机其他目录**，不能直接打包发给玩家。
- 最新独立 EXE `--help` 通过、`--jsonverify` 退出码 0；不能以此代替真实长时间 3D 游戏场景验收。

## 尚未完成 / 按原计划继续

- **P3**：完整太阳阴影贴图 / CSM / PCF、多层建筑遮挡投影、稳定的 alpha-cutout 投影。现有屏幕空间近景深度遮蔽不是这些功能。
- **P4**：真实体积光、大气散射、反射/水面效果、完整 Photon 画风（目前只包含颜色及雾的近似表现）。
- **P5**：可选高精度材质法线、完整 PBR、TAA/SSR/GI，Minecraft/Iris shaderpack 兼容层（如决定开发，需独立渲染接口设计）。
- **P6**：多场景 1080p 60 FPS 性能验收、动态预算、真实硬件矩阵、Alt+Tab/F5/resize 长时间显存泄漏测试、玩家 ZIP 打包。目标 FPS 不能提前视为达标。

## 实现文件

- `src/fp3d_shaderpacks.h/.cpp`：列表、选择持久化、GLSL 生命周期、后处理 FBO、深度/天气 uniforms。
- `src/fp3d_shader_archive.h/.cpp`：ZIP32 受限只读解包和 CRC。
- `src/fp3d_input.cpp`：光影包界面和质量档。
- `src/fp3d_compositor.cpp`：HDR+深度场景 FBO、后处理合成、菜单快照、释放与恢复。
- `src/fp3d_scene.cpp`：太阳/月光驱动的定向及环境照明。

**保护性原则**：不覆写既有车辆/家具等未提交改动，不自动 Git 提交或上传；玩家素材、Photon ZIP 和存档不进入 GitHub/发布包。

## 2026-11-08 阶段性推进：P3 近场方向光阴影与 P4 轻量阳光光束

- 新增 `src/fp3d_shadow.h/.cpp`：独立 DEPTH_COMPONENT24 深度 FBO，预留 1024/1536/2048 像素、约 23/30/38 格覆盖的质量配置；**当前仅“极高”档实际启用 2048 贴图**，避免未完成验证的阴影影响默认画质。只从只读快照中的可见墙、关门、部分障碍和已确认存在的天花板生成简化遮挡几何。日照不足、室内时跳过；GL 不支持时回退。
- 内置片元 shader 增加深度逆投影（相机近平面 0.2、远平面 1536、120° 水平视场），投影到太阳深度图，使用 2×2 PCF 软化边缘。尚不包括完整 CSM、车辆/人物精确投影、alpha-cutout 植被投影及透明透光处理。
- 高/极高质量增加基于太阳方向、天气云量及天空深度的轻量屏幕空间阳光光束，**不是真正三维体积光散射**。
- OFF 档恢复项目旧版固定光照，并释放后处理资源；3D 模式关闭时不会执行额外 shadow Pass。
- 仍未完成 P3/P4 全部验收：多层建筑阴影、相机穿越、复杂车辆遮挡、全天候光照及长时间帧率和显存测试尚需回归。

验证：ON 增量编译通过，原生 ZIP / HDR / depth / 损坏 GLSL 后处理测试通过；sunshafts 单独 GPU 红色通道像素 204→219。OFF 模式已在加入 P3 文件后完成 CMake 增量构建并链接通过。新阴影的真实 3D 白天效果须在日照测试存档中核实，不提前记为完整通过。

## 2026-11-08 后续：一键打开光影包文件夹与额外性能回归

本地源码已在 F5/F6 主模式设置页直接新增“打开光影包文件夹”按钮，和“光影包…”按钮并排。光影包详情页保留“打开光影文件夹”按钮。两个按钮复用 fp3d_shaderpacks::open_folder()，通过 SDL_GetBasePath() 定位 EXE 旁的 shaderpacks 目录，必要时自动创建，再调用 Windows ShellExecuteW。失败时保留错误状态，成功时显示已打开提示。

独立 QA 程序在真实目标测试版目录调用了同一函数：opened=1、exists=1、退出码 0，临时 EXE 随后已清理。此步骤证明打开文件夹的代码路径正确，不等于在游戏内手动点击了按钮。

P3 实验性太阳阴影新增 scene.render_generation 缓存：只有发布新的游戏世界快照或 GL 资源发生变动时才重新渲染 2048x2048 深度图；纯摄像机旋转时复用 GPU 纹理。在关闭光影、质量降至极高以下或 GLSL 编译不可用时释放影子深度资源。其余质量档不会执行额外阴影 Pass。

OpenGL 测试扩展：合成阴影深度贴图后，片元红色通道由 204 降到 126；关闭光影后 glIsTexture 返回 false、processed_framebuffer() 为 0。ZIP 合法/不合法格式和错误 GLSL 仍然按预期回退。以上属于功能单测，不代表全部建筑、车辆光照和长时帧率已经验收。

## 2026-11-08 真实游戏画面与阴影修复验收（追加）

**实机 OpenGL 场景结果**：已在独立 QA 游戏副本的 ShaderDaylightLab 白天存档内启用原生 `warm` 光影，取得 2520×1392 的真实 3D 帧缓冲图片，与 `off` 模式进行像素对比。两次运行均显示白天天空、街道、路灯、远景、围挡、怪物及车辆。关闭光影 RGB 平均亮度约 `(102.87,121.68,146.92)`，暖色电影 RGB 平均约 `(100.32,106.37,116.79)`，整帧像素差异为非零（各通道平均绝对差约 `5.42/15.57/30.48`）。原图位于工作区外层 QA 目录 `qa/shader-fbo-off.png` 与 `qa/shader-fbo-warm.png`，双栏对比 `qa/shader-fbo-before-after.png`。

**两项真实缺陷已修复**：

- **Windows CRLF 光影选择恢复**：`.selected` 存储 `warm\r\n` 时，先前二进制行读取会因尾部 CR 不合规而默默使用 `off`。现仅剥除一枚结尾 CR，继续拒绝路径分隔符与嵌入控制符。QA 原生程序确认 `selected=warm`，真实游戏确认 `usable=1`，`post_fbo != 0`。
- **首帧状态初始化**：场景光照和投影以前在第一次着色器编译之前决定是否启用高质量光照，导致当前回合仍使用旧照明。现在 `fp3d_shaderpacks::prepare()` 于每次 3D 世界绘制前执行，真实游戏首帧报告已启用 shader，极高质量下产生阴影贴图。

**阴影 P3 功能推进**：

- 修正深度渲染中太阳方向 `-sun` 与片元采样 `+sun` 不一致的错误。真实 GPU 独立用例绘制 y=3 的矩形遮挡物，结果深度 `0.480436`，与解析预期 `0.480436` 一致；地面接收阴影的深度为 `0.499679`。另有 2500 次随机相机逆投影用例，误差小于 `1e-7`。
- 在**不修改现有车辆模型**的情况下，用快照里可见且未损坏的原生部件提供保守车辆阴影遮挡：车身、车顶、车门、引擎/货舱及底盘；只复用车辆原位置/旋转，不为破损车辆凭空补齐部件，不把玻璃视为不透明实体。此项仍为几何近似，不能视为车身精准实时软阴影完全实现。
- 在 `scene.render_generation` 未变化时缓存太阳深度图，转头不会每帧重新生成 2048² 阴影纹理。仅极高档启用实验性阴影，关闭、降低档位或 GLSL 无法编译时释放相应资源。

**QA 工具修复（不影响正常玩家）**：仅在显式设置 `CDDA_FP3D_CAPTURE_FRAME=1` 时保存真实 3D FBO 到 EXE 同目录 BMP；第一次读取错误并非实际游戏黑屏，而是 SDL 遗留 `GL_PACK_ROW_LENGTH=48`，致 glReadPixels 只覆盖前一小段内存。已在 QA 路径临时设置规范 PACK 参数，读取完成立即恢复原 GL 状态，修正了误判。相机测试角度仅受 `CDDA_FP3D_CAPTURE_YAW/PITCH` 且 capture=1 控制，默认游戏不受影响。内部 QA 还提供文本 sidecar 记录选择、GPU FBO、阴影 ID 和天空参数。

**热重载**：光影管理页的“重新扫描/重载”会使本地 GLSL `post.frag` 重新编译而不必重启游戏；独立 OpenGL 原生 GLSL 修改前/后像素红色值 31→204。`ENABLE_FP3D=ON` 和 `OFF` 两套 CMake 构建均已通过，`--jsonverify` 退出 0，未提交或推送 GitHub。

**仍未验收或尚不支持**：完整 Minecraft/Iris/Photon 光影包（包括其 G-buffer、材质、深度/多阶段渲染 API）、完整体积光与 SSR/PBR、CSM 和多层建筑/车辆精准投影、1080p 全游戏帧率 60 FPS 长时间稳定性、便携发布包。验收时不能将局部后处理/近场实验性阴影称为完整 Photon 移植。

## 2026-11-08 P3 实景阴影独立验证与用户开关

本轮通过 ShaderDaylightLab 隔离存档，在相同场景、同一 warm 光影包、极高画质和相同摄像机视角下单独开关近场太阳阴影（与前一轮 off vs warm 后处理对比不同）。启用时 shadow_texture 为非零；禁用时为 0。差异约占全帧像素 0.8993%，主要集中在远处围挡与建筑区域，包围框 (1279,676)-(2516,796)。文件分别为 qa/shader-shadow-with-shadow.png、qa/shader-shadow-without-shadow.png 和 qa/shader-shadow-only-difference.png。仅证明阴影 Pass 在真实游戏执行，尚不代表多层建筑与移动载具的准确阴影已经全部验收。

F5/F6 → 光影包 → 实验性近场太阳阴影：新增独立复选框，写入 EXE 同级 shaderpacks/.shadows，0=关闭、1=开启；默认启用，但只有内置 warm/cinematic 且画质为极高才真正运行 2048² 阴影 Pass。第三方 CDDA post.frag 不消费内置阴影接口时不再额外生成该深度贴图。关闭勾选或降低画质时释放相应显存。

使用此真实用户配置路径完成 A/B：.shadows=1 时 shadow_texture>0；.shadows=0 时 shadow_texture=0，像素差异同样约 0.8993%。C++ 独立 QA 程序跨进程验证了 0→读0→写1→读1 的持久化流程，测试完毕恢复原配置。

QA 图片截断曾来自 SDL 遗留的 GL_PACK_ROW_LENGTH；现在仅在截图时设置正确 PACK 参数，再恢复，且只有在 SDL_SaveBMP 返回、sidecar 出现、BMP 文件大小完整后才读取。这属于 QA 设施修复，正常运行不额外截图或修改视角。

3D 开启/关闭的 Windows 编译均通过，实际游戏 --jsonverify 退出 0。GLSL 热重载、HDR/深度、独立阴影采样与 60 次 FBO 尺寸变化测试通过。不得将内置近场阴影视为 Photon/Iris 完整移植。

剩余：复杂多层建筑、动物/NPC 和车辆准确投影、真实体积光/SSR/PBR、长时间 Alt+Tab/窗口缩放稳定性、1080p 全游戏 60FPS 基准、Photon/Iris G-buffer 与多 Pass 着色器架构。当前为本机测试版，未发布或 Git 推送。


### 原生 ZIP 实机测试与 GPU 后处理微基准补充（2026-11-08）

- 将工作目录 shaderpacks 中的“暮色电影.zip”设置为当前包，使用中文 UTF-8 文件名，在独立 ShaderDaylightLab 白天存档运行。首帧记录 usable=1、post_fbo>0、shadow_texture=0；证明 CDDA 自定义 post.frag ZIP 正常执行，并且没有浪费原生专用阴影贴图资源。实景截图 qa/shader-native-zip-game.png。
- 1080p 独立 OpenGL 后处理测量（RTX 4080 SUPER、glFinish；不包含世界场景构建、shadow map、UI 或游戏逻辑）：低/中/高/极高约 0.0952/0.0855/0.1173/0.1330 ms/post。该值只能评估光影后处理，不代表整体帧率或可达到 60FPS。
- 所有 QA 修改的 .selected、.quality、.shadows 均在独立测试后恢复原字节。中文原生 CDDA 光影 ZIP 与 Minecraft Photon/Iris 格式是不同的接口，后者依然不能直接运行。

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
