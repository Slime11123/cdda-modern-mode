# CDDA 3D — 不死人贴图床具、支柱、围栏与街道模型修复

日期：2026-11-07。源码基线为 CDDA 0.I-1，3D 由 ENABLE_FP3D 控制。用户本地贴图包：assets/tilesets/MSX++UnDeadPeopleEdition，含 normal_furniture.png、normal_terrain.png、large_stuff.png、large_terrain.png。没有把第三方贴图包复制到 Git 仓库或发布包。

## 修复的实际 CDDA ID

| 类型 | 原版 ID | 修改内容 |
|---|---|---|
| 床 | f_bed, f_bed_down | 木床架、木腿、床头板、底板、布料床垫、薄被和枕头分离建模。床上掉落物贴床面水平显示，不再竖立漂浮。 |
| 空床架 | f_bed_frame | 保留木质横梁和底板，并让其上的掉落物平放。 |
| 支柱 | t_column, t_little_column, t_pillar, t_column_halfway | 独立混凝土柱底座、细柱身和柱帽，避免将 2D 大型柱影像立起来遮住整格。 |
| 白色栅栏 | t_fence | 白色木栅条、端头、上下两条横梁；四方向邻接自动拼接。 |
| 高木栅栏 | t_privacy_fence | 紧密竖向木板和背面横梁。 |
| 混凝土栏杆 | t_concrete_railing | 真实竖向混凝土立柱和上下横梁，原先铺在地上的贴图问题修复。 |
| 其他栏杆 | t_railing, t_metal_railing, t_guardrail, t_guardrail_bg_dp, t_guardrail_hw_air | 木质/金属/公路护栏分别使用实际结构和不同材质区域。 |
| 路灯 | f_street_light | 立柱、灯臂、灯罩和下部灯面，排除 2D 贴图中已经画好的投影。 |
| 信号灯 | f_traffic_light | 灯柱、信号箱与正面灯片。 |
| 电线杆 | f_utility_pole | 木杆、两级横担及端部支件。 |
| 邮箱 | f_mailbox, f_vitrified_mailbox | 立柱、托板、信箱外壳、门板和升起的信号旗。 |
| 室内立灯 | f_floor_lamp | 灯柱、底座、圆形灯罩。 |

## 关键技术细节

1. 不死人 f_bed 的原图仅是一片布料纹理，不能将它同时铺到直立木框。床面使用该布料图；木框改用本地 f_table 的木纹区域。为了防止截图中红色和粉色床上物品悬浮，fp3d_scene 将放在床或床架上的掉落物四边形水平绘制，fp3d_targeting 使用相同高度进行射线选取。
2. t_column 等地形仍保持原版 terrain ID 和游戏逻辑分类，但 fp3d_scene 和 fp3d_targeting 共用新的 terrain_model_parts 几何，从整块墙面效果变为独立立柱。地面使用混凝土纹理而不是大号带投影的立柱原图。
3. t_fence、t_concrete_railing 等地形通过 fence_connects 检测兼容的邻居，再生成对应实体结构。高木围栏、混凝土栏杆与护栏采取不同部件，不把整张 2D 假透视图片当成立面。
4. 路灯、邮箱和其他设施通过 fp3d_furniture::describe 识别原版 ID，纳入静态分块缓存。超过层高的路灯、电线杆部件纳入 chunk 高度裁剪判定。
5. 非相关普通地形不会生成额外的柱/栅栏几何，射线拾取也避免无谓的四邻居检查。模型只影响 3D 外观和可视交互目标，不改变原版移动、攀爬、物理碰撞或存档规则。

## 修改过的文件

- src/fp3d_furniture.h / .cpp
- src/fp3d_snapshot.h / .cpp
- src/fp3d_materials.cpp
- src/fp3d_scene.cpp
- src/fp3d_targeting.cpp

## 构建、验证、启动

- CMake/Ninja 编译 cataclysm-tiles 成功；程序 --help 返回码 0；CDDA --jsonverify 返回码 0，标准错误为空。数据校验 stdout 的字体路径提醒不致命。
- 新独立运行包：runtime/modern-bed-street-models/cataclysm-tiles.exe，保留原有 V3、V4 以及所有存档。
- 已更新常用 启动3D操作修复验证.cmd 指向该版本；可选英文入口 Start-BedStreetModels.cmd。
- 未提交或推送 GitHub，未更新正式 Release，未复制 UndeadPeople 第三方贴图包。
- 尚未完成游戏内图形实机验收。建议进入截图中的房屋，检查 2 张床和红/粉床上物、t_column、t_fence 与 t_concrete_railing，然后到街上检查 f_street_light、f_mailbox、f_traffic_light。

## 边界

3D 模型是静态、简化结构。路灯亮度、电力状态、栏杆跨越、物品实体碰撞仍由 CDDA 原版处理；不同 UndeadPeople 版本可能需要微调贴图裁剪位置。
## 2026-11-07 实机截图反馈：铁丝网、栅栏、邮箱、路灯 V2

依据用户上传的四张最新实机截图修复模型，而非直接放大二维图块。

- 图1 `t_chainfence*` 铁丝网：去掉两米高带 2D 假阴影的整板，不再把 `$fence` 图块当面材质。新增真正可透视的交叉钢丝网 `chain_link_mesh()`（OpenGL GL_LINES）、镀锌立柱及上下张紧横杆，背后的物体能从网眼中看到。鼠标拾取仍使用现有近似栅网平面。
- 图2 `t_fence`：取消反复拉伸有黑边的 2D 栅栏像素。每半格改为两根细栅条、窄帽和两条横梁；整体高度 1.16，采用不死人栅栏的米白色哑光漆面。额外统一金属栏杆的灰色和木栏杆的棕木色，不再显示斑马条纹。
- 图3 `f_mailbox`：原本同时画了红色顶面、伪投影与木柱的像素裁剪造成白灰色箱体。保留 5 个三维部件，分别使用棕色木杆、深红金属箱体、深色正面投递门、亮红信号旗；关闭错误的原图整体 UV 映射。
- 图4 `f_street_light`：原 64x80 像素的灯头图案含二维透视和阴影，映射到灯罩侧面与底面后出现错误黑白块。将灯罩外壳分配为干净灰金属，发光面分配为浅暖色，保留原来的圆形金属路灯杆和横臂。该视觉修复不改变 CDDA 实际灯光/电力数据。

源码文件仅增量修改 src/fp3d_snapshot.cpp 和 src/fp3d_scene.cpp。此前床架和其他街道模型的修改仍保留。未复制或再分发不死人贴图素材。

新独立运行目录：runtime/modern-fence-wire-v2。常用 启动3D操作修复验证.cmd 已切换到 V2；另有 启动铁丝网邮箱路灯V2.cmd 及 Start-FenceWireV2.cmd。旧版运行包和全部存档保留。

验证：编译链接成功；EXE --help 退出 0，CDDA JSON 校验退出 0，错误流为空（标准输出有非致命字体路径提醒）。需要用户在截图同一场景进行实机画面视觉验收；没有声称已经完成该项。
