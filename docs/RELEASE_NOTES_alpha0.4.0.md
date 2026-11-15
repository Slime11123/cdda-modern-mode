# CDDA Modern Mode — Alpha 0.4.0

Windows x64 · CDDA 0.I-1 (Ito-1) · GitHub Pre-release

本项目由 OpenAI GPT 生成并协助开发，是 CDDA 的非官方衍生版本。此更新以已发布的 **Alpha 0.3.2**（源码 `73ad8c53724b8be967501a5cde490b0afb6f2273`）为比较基线，整理该版之后尚未发布的功能和修复。仍为实验性测试版。

## F6 上帝视角与鼠标操作

- 3D 世界内按 **F6** 在第一人称和 3D 上帝视角之间切换；**F5** 打开统一模式设置，2D 中 F6 仍打开设置。
- 俯视镜头围绕玩家旋转：滚轮缩放，中键拖动旋转，WASD 跟随镜头方向；切回第一人称恢复原来的观察方向。
- 俯视右键在鼠标所指地块打开现代 2D 同款下拉菜单，左键沿用原版选择/移动/攻击判定，E/Q 使用鼠标目标。修复连续右键被忽略、目标随鼠标移动丢失以及菜单弹出位置错误。
- 室内按玩家所在连通房间切开屋顶，其他房间保留屋顶；补齐已显示建筑外部屋顶。
- 玩家、生物、物品与状态贴图跟随俯视相机；修正普通僵尸/NPC 与玩家的尺寸比例。玩家本体在俯视下保持可辨识，移动模式标记独立绘制。
- 俯视画面补接原版声音方向、光标、高亮、瞄准轨迹、车辆方向、命中、弹道、爆炸、区域、动画和战斗文字提示。

## 3D 地形、模型与地面细节

- 3D 使用独立相机几何采集，绘制已加载地图中的实际地形、墙面、楼板、家具和车辆，修复深阳台外墙缺失及森林地面蓝色缺口。3D 地形不再由原版二维视野/地图记忆裁切；不会生成未加载的远方地图，也不把真实露天空洞补成地板。
- 物品、生物与交互继续采用原版感知和规则；原版 2D 绘制、行动消耗、地图记忆和世界模拟保留。早期上帝视角实施文档中“全部地形受原版视野过滤”的描述已被上述后续设计替代。
- 扩充家具与非植被物体的程序化模型、形体分类、比例和摆放，改进桌椅、床、柜体及其他常见设施的外观；外部图块素材仍需自行安装。
- 玻璃柜物品放入内部层板并按柜体缩放，调整柜架、物品及玻璃的绘制顺序；围栏、栏杆和铁丝网加入方向光阴影。
- 雨水槽依据邻接选择直线、端点、转角和连接贴图；STOP 标志改为细杆、圆形红白牌面及白色文字。
- 新增 20 类原版地面覆盖物显示，包括血迹、酸液、胆汁、黏液、污泥、碎屑、蛛网等；支持浓度、多种覆盖物共存、图块回退和移除刷新。
- 修复车辆虚拟顶棚遮罩误删真实路面的问题，保留 Alpha 0.3.2 的 V12 车体轮廓。

## 启动、主菜单、世界创建与存档

- 主菜单背景改为独立沙箱进程中的原版 2D 实时世界演示，使用原版世界和角色行为；演示数据与玩家世界隔离。
- 新增不透明加载首屏、原版海报、阶段文字与轮换提示；等有效背景首帧就绪后一起显示菜单，避免先出现空菜单或未填充纹理。
- 加载期间丢弃提前按下的菜单输入；演示不可用时回退静态背景并提示，菜单仍可使用。补充中文加载文案和窗口缩放处理。
- 重整新建世界界面，调整模组选择、设置、说明和确认布局，保留原版世界生成与模组依赖处理。
- 新增存档浏览界面：按最近游玩排序，搜索角色/世界，显示可用的场景预览、生存天数、模组和难度信息。预览为辅助文件，旧存档无截图时仍可列出，不影响原生保存成功与否。
- 统一现代生存面板的按钮颜色、禁用状态及部分行布局，改善文字和交互辨识度。

## 动态灯光与实验性材质

- 原版启用且有电的灯具、手电筒及手机光源接入内置/Photon 风格视觉桥；最多选择 8 个局部光源，支持色温、衰减、光晕和手持光束，可在 F5 设置中关闭。
- 修正独立电池/落地灯相关供电判断；光源取自原版实际状态。局部光目前没有逐灯真实阴影，复杂电网的完整通断电回归仍有限。
- Alpha 0.3.2 仅有 QA 材质通道，本版新增玩家可主动启用的 **Ultra 实验性 PBR 法线、粗糙度和金属度高光**，默认关闭，细节强度可保存为 0–200%。
- 支持的墙/地面通过 GPU 表面属性通道进行有限微表面光照，正式帧不进行该通道的全帧 GPU→CPU 读回；不支持、关闭或失败时回退原画面。
- 保留已有 Photon 风格色调、双级太阳阴影与雨天 SSR。仍不是完整 Minecraft Photon/Iris/OptiFine 多阶段执行，也不代表完整 labPBR 或全部材质覆盖。

## 性能

- 减少 3D 地形采集中的重复字符串和查找；复用未变化的几何、索引、材质与地形块，门、家具、血迹和空中格变化触发失效。
- 近远两级阴影复用静态投影几何，普通墙与地板合并绘制提交；车辆和特殊几何保留保守更新。
- 本机 RTX 4080 SUPER、1920×1080、Photon Ultra 的指定密集城市存档副本，两次 60 秒连续行走样本平均 **58.44 / 58.43 FPS**。这是发布前开发版的场景记录，不能作为本安装包、其他硬件、所有存档或最低帧率的保证。首次加载、车辆和大规模地图变化仍可能卡顿。

## 安装与升级

1. 下载 `CDDA-Modern-Mode-alpha0.4.0-Windows-x64.zip` 并完整解压到新的可写目录。GitHub 自动生成的 Source code ZIP 不是游戏安装包。
2. 直接双击唯一的游戏程序 **`cataclysm-tiles.exe`**。保留随包 DLL、data、gfx、lang、config 和许可证；无需 CMD 或额外启动器。
3. 升级前备份已有存档和设置，不直接覆盖旧目录。F5 更改画面、控制、HUD 与光影，3D 世界中 F6 切换视角。
4. **Photon 和 UndeadPeople 均不随包分发**，需要玩家自行从原作者渠道获取，并遵守各自许可。相关模型和视觉桥代码不包含这些外部纹理或光影 ZIP。

## 验证范围与已知限制

既有开发报告记录了启动就绪状态机、俯视相机/输入、贴图比例、地面覆盖物、材质、动态灯光、真实地形采集与缓存失效等专项验证。部分报告来自不同开发中间版本，不等于最终安装包全部通过。发布构建的提交、实际执行结果、附件大小及 SHA-256 另列于 GitHub Release 验证附录和 `BUILD-INFO.txt`。

完整 FP3D 测试此前仍有已知失败，包含缓存/门计数、楼层前置条件及窗口焦点相关检查；不宣称完整测试集全通过。最新柜体、屋顶、提示叠加和存档浏览仍需更多游戏内回归。不同 GPU/DPI、图块包、复杂多层建筑、车辆、模组、旧存档迁移和长期游玩尚未全面验证。

## English summary

Alpha 0.4.0 collects unpublished changes since Alpha 0.3.2: F6 first-person/overhead switching with orbit, zoom and pointer-based actions; independent loaded-map terrain capture; room roof cuts, sprite proportions, furniture models, ground fields and native overlays; an isolated live title demo, loading screen, world-creation UI and save browser; native local lights and optional Ultra PBR detail; and geometry/shadow submission caching. F5 opens settings; F6 retains settings behavior in 2D.

This is an unofficial CDDA 0.I-1 Windows x64 prerelease developed with OpenAI GPT. Extract the full ZIP and launch its single real game EXE. Photon and UndeadPeople are not bundled. PBR is limited and off by default; complete Minecraft shaderpack execution is unsupported. The recorded 58.44/58.43 FPS samples describe one development build, scene and GPU, not a universal performance promise. See the release verification appendix for the final package's actual checks and hashes.
