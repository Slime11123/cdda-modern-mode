# CDDA 3D：原版供电灯具、手机与手电筒动态光照实施记录

> 日期：2026-11-09。本地开发阶段，兼容旧版 GLSL 1.20/OpenGL 2.1，针对 Photon v1.4 视觉适配。
> 状态：实际源码已接入，完整 Windows 构建通过；独立真实游戏画面中的固定灯、便携灯、关灯及隔墙效果均已测试。
> **重要：真实“有电电网开关”的端到端游戏存档仍待测试，不能把模拟光源截图视为验证过真实电网。**

## 1. 真实 CDDA 状态读取

- 新增 src/fp3d_snapshot.h/.cpp 的 emitted_light 只读光源快照，提供绝对位置、光源类型、原版发光量、发光高度与来源。没有使用 live map/vehicle/item 指针进入图形线程。
- 电网/车辆灯：从原版可见车辆部件中只选 part.enabled、part.is_available()、part.is_light()、info.bonus>0 的源；再用 connected_battery_power_level(here).first>0 验证该车辆连接的供电网络还有储电。灯具关闭、损坏、电量归零或不可见时不采集。
- 原版家具 f_street_light 自身没有 light_emitted 值；不能仅根据其 ID 模型亮灯。真正开启、供电的灯部件才被采集。
- 角色携带设备使用 you.active_light()；原版已处理电池和激活状态。识别 flashlight_on、smart_phone_flashlight、smart_phone_locked_flashlight、cell_phone_flashlight，并将开启的手电筒/手机作为 3D 摄像机方向光束。已停用/不发光的物品不会主动创建光源。
- 地图上可见地形和家具读取 light_emitted；发光火焰等字段按 field 原版定义；可见物品读取原版 getlight_emit()。不读取 remembered 未探索地图里的隐藏电器。

## 2. 新的 Photon 灯光管线

- 新建 src/fp3d_local_lights.h/.cpp；每帧按距离与发光强度最多挑选 8 个光源，远离视点者不处理。
- 由原场景 Depth FBO 重建可见像素世界空间位置，按距离平方衰减产生局部暖色/冷色光。手机/手电筒的聚光束随着鼠标转向移动，其他灯具形成泛光光源和局部发光光晕。
- 从屏幕深度几何导数恢复墙面朝向，压制背墙受到不合理光照的情况。它不等于完整点光源 Shadow Map，窗、墙角仍可能漏光。
- 先把动态灯光加到 Photon/内置世界颜色，再交给原来的色调映射和阴影/SSR 后处理。初版曾在色调映射后加光导致整墙发白；新版色彩更稳定，保留墙壁纹理。
- 不额外创建完整画面 FBO，也不在普通每帧做 GPU→CPU 光照读回。无法编译额外着色代码时回退旧 Shader，不改动用户原始 Photon ZIP。
- F6 光影菜单增加“动态灯光（电网 / 手电筒 / 手机）”开关；默认开启，可在 shaderpacks/.local_lights 保存。关闭光影包、低质量、不支持的原始 final.fsh 不保证启用该效果。

## 3. Windows 构建与真实 OpenGL 回归

| QA 项目 | 结果 |
| --- | --- |
| Windows ENABLE_FP3D=ON 编译链接 | PASS，native-local-lights-build.log |
| 原版电网/角色/材质源码约束 | 28/28 PASS，verify_native_light_source_contract.py |
| Photon v1.4 Ultra 局部灯 GLSL 编译 | PASS，local_lights_compiled=1 |
| 双级太阳阴影同时保留 | PASS，两张 GL 纹理非 0 |
| 模拟固定暖色灯位于墙前 | PASS，RGB 差 (74.112, 57.598, 30.967) |
| 模拟同一灯移到墙后 | PASS，RGB 差 (4.972, 3.768, 1.928)，大幅低于墙前 |
| 模拟手机冷色照明 | PASS，RGB 差 (39.307, 43.162, 49.327) |
| 动态灯光选项 Off | PASS，RGB 差 (0.013, 0.003, 0.006) |
| ShaderPack 关闭 | PASS，无动态灯着色程序 |
| 测试完成后自动恢复光影/PBR 用户偏好 | PASS |
| git diff --check | PASS，仅有 Git 默认 CRLF 提示 |

- qa/verify_native_dynamic_lights_runtime.py 实际运行 3D 游戏程序、加载隔离 ShaderDaylightLab 测试世界，并从 2520×1392 的游戏 FBO 读取真实图像；不是离线生成假图。
- 六组图片分别保存在 qa/native-lights-baseline.png、native-lights-powered_fixture.png、native-lights-behind_wall.png、native-lights-held_phone.png、native-lights-switch_off.png、native-lights-shader_off.png；合并预览为 qa/native-lights-contact-v2.png。
- 日志：qa/native-lights-runtime.log，QA_EXIT=0。
- **测试范围必须说明：ShaderDaylightLab 原生光源数量为 0。为了验证材质/GPU 投射、遮挡和切换，渲染器在 CDDA_FP3D_CAPTURE_FRAME=1 的 QA 模式下人工放入了固定暖灯/手机灯的位置。QA 光源不进入真实场景快照，不改原版电网和存档，因此尚不能证明真实电网在实际玩家世界里经过“接通/断电”测试。**
- 已为上述真实来源逻辑实现独立源码约束检测，包括开关、损坏、联网电量、开启手机、原版发光强度和对记忆地图的限制；这些源码检测不能替代实际游戏供电状态测试。

## 4. 使用

- 启动器：C:\Users\StickySlime\Documents\Codex\Projects\cdda3dproject\启动动态灯光实验版.cmd
- 测试 EXE：runtime/modern-3d-shaderpacks/cataclysm-tiles-native-light-qa.exe
- 隔离配置和测试存档：runtime/modern-3d-shaderpacks/user
- 进入游戏 F6：选择 Photon v1.4 / 内置视觉适配；勾选“动态灯光（电网 / 手电筒 / 手机）”。
- 把手电筒或智能手机切到实际发光的“开”状态，靠近墙面观察光照随视角移动；有电网的房间应观察灯亮/关闭与电池耗尽的变化。
- 如因 GPU 或驱动不兼容导致卡顿，F6 可关闭动态灯光，原游戏光影路径恢复正常。

## 5. 后续必须验收

1. 在真实具有灯具开关、电池或发电机以及电缆的独立测试世界做通电→关灯→断电→恢复的四项 A/B，并记录真实光源数量。
2. 实际道具完成手机开启/关闭/电量耗尽以及手电筒转向验证；不能只用渲染层 QA 人工灯。
3. 实现真正受墙体完整遮挡的逐灯阴影、表面发光灯罩、跨层灯光/窗户透光，并且优先控制 8 灯时的 GPU 帧耗与体积效果。
4. 持续测试多车辆、多层楼、夜间、雨天、两种 tileset，确认不回退 v10b/v18 阴影与 v6b 雨天 SSR。

## 6. 不影响正式游戏的范围

只在本地源码、qa、单独实验运行目录实施；没有覆盖正式游戏 exe/主启动器、没有改玩家存档，没有 Git commit / push / 新建 Release。既存 PBR、UI、车辆修改保持原状。


## 2026-11-09 实际通电测试存档验收（本节更新此前的待测状态）

已在用户常用目录 runtime/0i1/user/save/光影电网测试 中生成并用原版游戏存档流程保存真实电网试验场景，而不是继续使用渲染层人工光源。游戏数据有：开启且联网的立式灯 1 盏、同电网关闭的立式灯 1 盏、隔离无电灯 1 盏、大电池及原版连接电网均 50000 单位电量、预装 56 电量的手电筒、50 电量的智能手机及备用满电电池盒。

真实游戏的电网合并常以 power_cord 作为位置返回的第一部件；生成器现在使用绝对安装位置和真实 ap_standing_lamp 部件 ID 去启用或关闭灯体。原测试隔断墙挡住了出生点视线，现将隔墙挪至西侧，让通电灯位于玩家前方可见区域，隔墙后保留无电对照灯。

项目原常用启动器 Launch-Current-3D-Models.ps1 已重新编译并把新版安装到 runtime/0i1/cataclysm-tiles-models-latest.exe，没有交付额外的游戏版本。使用同一个常用 EXE 与同一个常用存档路径，运行 qa/verify_live_grid_common_exe.py 自动进入真实存档，在 Photon v1.4 的 3D 帧中得到 native_light_sources=1、local_lights_selected=1、local_lights_compiled=1、local_lights_qa_probe=0。切换动态光照 OFF 后 real source 仍为 1、GPU local_lights_selected=0，两帧 RGB 平均绝对差 (77.9179,65.8183,48.4917)，实景验证 PASS。

实景原图：qa/live-powergrid-native_on.png 与 qa/live-powergrid-native_off.png。合并对照图：qa/光影电网测试_真实电网光照对照.png。测试存档包含测试说明.txt。玩家原来的四个存档世界未覆盖，并在 runtime/0i1/backup/light-lab-20261109 留有测试占位存档与旧版 EXE 备份。测试脚本自动恢复原 FP3D_VIEW=2d、shaderpacks 设置和 lastworld.json。

范围限制：已验证真实通电灯进入 Photon 与 F6 动态灯光开/关对照；尚未自动化验证手动拔电缆、电池耗尽、原版电器开关的完整跨存档生命周期，亦未实现逐灯完整阴影和 Iris/Photon 全部管线。


## 2026-11-09 手电筒/手机随第一人称视角转向修复

- 修复 src/fp3d_local_lights.cpp 的动态手持光束方向：此前聚光方向是 (-sin(yaw)*cos(pitch), +sin(pitch), -cos(yaw)*cos(pitch))，与 native_lamp_world() 的画面中心反投影不一致；现更正为 (+sin(yaw)*cos(pitch), -sin(pitch), -cos(yaw)*cos(pitch))，匹配 3D 准星中心射线。
- 将由原版 active_light() 和已启动 flashlight_on / phone_flashlight 确认的玩家手持 beam 光源，从“固定回合制快照的格子中心”改为每帧跟随 smooth frame_camera 的 x/z 和 eye_height - 0.18 米位置。移动时光源不再滞留于上一格；相机 yaw/pitch 每个 SDL mouse motion 更新，而不是需要新回合。
- 手机光照沿用同一束光逻辑；其他原版电网、车辆灯与静态火焰仍固定世界坐标，不会随着玩家视角错误转动。
- 手电筒聚光改为 4% 周边弱散射 + 96% smoothstep(0.67,0.93,dot) 主光束，中心更明亮，背后弱散射较此前 16% 更小；原 Photon 色调映射与灯具电源逻辑不变。
- qa/verify_flashlight_aim_math.py：63 组 yaw/pitch 包括左右、上下边界，逐一与 native_lamp_world() 逆视角矩阵核对，向量误差为 0；QA 原版光源分类、相机位置匹配源代码验证 PASS。两份源文件也通过基于相同 Ninja 参数的独立 g++ -fsyntax-only 验证。
- qa/verify_flashlight_view_rotation.py：在临时截图模式中只更改相机 yaw/pitch 而不推动游戏回合，用于 GPU A/B 对照；QA 专用竖直俯仰入口为 CDDA_FP3D_QA_WALL_PITCH，仅在开启 QA 帧捕获时生效。
- 当前 GPU QA 的外部阻碍：项目数据文件 data/json/monsters/LIXA_monsters.json 被 CDDA JSON 加载器报错（无效/未识别 copy-from 字段），导致测试游戏加载场景失败；该怪物数据文件属于独立开发改动，本次没有更改它，也不把此次 GPU 对照描述为成功。
