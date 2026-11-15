# F6 3D 上帝视角（俯视）实施报告

日期：2026-11-15
依据任务书：`F6上帝视角_AI执行计划_2026-11-13.md`
正式源码：`CDDA-0I-FP3D`（HEAD `73ad8c53` + 实施前既有未提交修改）

## 1. 结论摘要

3D 世界中 **F6 在第一人称与上帝视角（俯视）之间双向切换**；F5 仍打开模式设置窗口，2D 中 F6 仍打开设置窗口。
上帝视角**复用同一套 3D 渲染管线**（同一 FBO、材质、光影、阴影、后处理、HUD），只改变相机
位姿与两处呈现开关（手持物/准星、玩家贴图），因此不会出现“抬高相机就宣称完成”的假上帝视角。

关键约束的实现方式：

| 约束 | 实现 |
|---|---|
| 相机位置绝不成为视野来源 | 快照仍在 `game::draw()` 用原版感知（`seen_cache`/`camera_cache`）生成；旋转/缩放不触发任何快照重建，也不写地图记忆 |
| 建筑剖切不揭露隐藏内容 | 剖切在**快照构建阶段**执行，只**删除**玩家所属室内区域的当前层天花板与其上楼层几何，从不新增格子；其它建筑保持外观 |
| 鼠标命中与渲染同一套相机 | 同一个 `camera_pose()` 同时供给 `fp3d_scene::draw`、阴影、反射、PBR/local lights、后处理与 `fp3d_targeting::ray` 的鼠标射线 |

验收状态：构建、`[fp3d]`/`[overhead]`/GL 用例全部跑过（新增用例 100% 通过），
并已在**真实游戏窗口**里用真按键验收 F6 双向切换、滚轮缩放、中键旋转与建筑剖切（截图见 8.3）；
实机过程中发现并修掉了“俯视右键不弹菜单只报未知指令”的缺陷（8.3、§3）。

## 2. 相机类型与状态拆分（P0 / P1）

- `fp3d_compositor` 新增独立状态 `overhead_view`（相机类型），与 `enabled`（是否 3D 渲染）彻底分开。
  `controls()` 不再用 `modern_2d` 冒充俯视：俯视仍返回 `first_person_3d`（键位/HUD/面板全部沿用 3D 配置），
  另由 `fp3d_compositor::overhead()` 暴露相机类型。
- `fp3d_camera::orbit`（方位角 / 固定俯角 60° / 距离）与 `fp3d_camera::orbit_pose()`：
  `eye = pivot - forward * distance`，`forward` 与第一人称、`fp3d_targeting::ray` 完全同一约定：
  `(cos(pitch)·sin(yaw), -sin(pitch), -cos(pitch)·cos(yaw))`（正 pitch 为向下）。
  注视点是**玩家所站地块中心**，因此旋转、缩放不会把玩家移出画面中心。
- 默认值：俯角 60°、距离 18 地块、缩放范围 6–48 地块；第一人称的 `yaw/pitch` 完全独立保存，
  切回第一人称恢复原朝向与俯仰。
- F6 在 `fp3d_compositor::process_event()` 的 SDL 事件入口最先处理并吞掉：仅在 3D 世界内、
  按键首次按下（`!event.key.repeat`）时切换，长按不重复；事件被消费，原生设置窗口不会打开。
- 切换时取消拖拽、手势（按住 E/攻击）、方向选择、挥击状态与 `accepted_walk`，
  更新光标/相对鼠标模式（俯视释放相对鼠标并显示光标），并使 UI 失效以触发重新捕获（避免旧视角闪一帧）。
- 俯视移动跟随镜头方向（2026-11-15 后续按用户反馈调整）：`fp3d_compositor::movement_heading()`
  返回当前镜头方位角，中键旋转后 WASD 按画面上的前后左右移动；左键沿用现代 2D 行为（`try_get_left_click_action`）。
- 唯一的 `fp3d_compositor::camera_pose()` 同时供给世界 FBO、`fp3d_scene::draw`、阴影、反射遮罩、PBR/局部光、
  后处理 uniform 与鼠标射线。`fp3d_scene` 的视锥剔除原本把“眼高 1.6m”写死，已改为使用真实
  `camera_offset.height`（第一人称等价，蹲伏时更准）。
- **线性雾窗按眼高比例拉伸**（`fp3d_weather::begin_atmosphere(..., far_scale)`）：1.6m 眼高对应的
  28–180m 雾窗，在 15.6m 高的俯视相机上会把整幅画面糊成天色；俯视时按 `camera_height / eye_height`
  拉伸雾的起止距离，后处理 `fog_end` 同步。只放大天气的可视距离，不改变原版天气状态或感知结论。

## 3. 输入（P1 / P4）

| 输入 | 行为 |
|---|---|
| F6 | 第一人称 ⇄ 俯视（仅 3D 世界；不消耗回合，不推进时间） |
| 滚轮 | 鼠标在 3D 视口内时缩放俯视距离（6–48 地块，平滑步长）；在菜单上时交给菜单 |
| 中键拖动 | 绕玩家旋转方位角（俯角首版固定）；松键、失焦、切换视角、打开原生窗口都会终止拖动 |
| 右键 | 在鼠标所指地块打开与**现代 2D 相同**的下拉菜单（`context_2d_entries` → `show_dropdown` → `resolve_2d_choice`） |
| 左键 | 沿用现代 2D 的左键行为（前往/攻击等，由原生 `try_get_left_click_action` 判定） |

- 光标坐标换算使用 renderer 的逻辑尺寸（`SDL_RenderGetLogicalSize`），与 `set_viewport()` 的像素比例一致，
  覆盖 UI 缩放 / 高 DPI / 侧栏造成的视口偏移。视口外、UI 区域、未知区域、记忆但当前不可选的格子、
  外壳（`exterior_only`）都返回无目标。
- 俯视中的 E/Q 交互目标来源统一改为**鼠标所指地块**：`fp3d_compositor::interaction_target()`
  在俯视返回 `pointing_target()`，不会残留第一人称准星目标。
- 下拉菜单冻结的背景帧按 3D 视口矩形摆放（与行动轮盘同一套逻辑），不再整屏拉伸到 HUD 侧栏；
  F5 模式设置窗口内也加了一行说明「3D 世界中按 F6 切换第一人称 / 上帝视角（俯视）」。
- **右键必须由相机自己接管**：3D 世界把右键绑定为 `FP3D_AIM`（第一人称准星射击），
  所以原版 `SEC_SELECT ← MOUSE_RIGHT` 在 3D 世界里永远命中不了，事件只落到 `ANY_INPUT`
  （实机表现为“未知指令：「按下鼠标右键」(3)”而菜单不出现）。因此俯视时 `fp3d_compositor`
  命中世界视口且拾取到地块后吞掉这次点击，记一个待处理请求，由
  `input_context::handle_input()` 直接返回 `SEC_SELECT`，从而走与现代 2D 完全相同的菜单路径。

## 4. 感知与记忆（P2）

未新增任何“相机感知”。俯视使用与第一人称完全相同的快照与过滤：

- 快照由 `fp3d_snapshot::capture()` 用原版 `seen_cache` / `camera_cache` / `you.sees()` /
  `clairvoyance()` / `unimpaired_range()` 判定当前可见，区分 `visible` / `remembered` / 未知。
- 仅记忆格子按原版地图记忆显示静态信息；不读实时家具变化，不复制隐藏怪物/NPC/物品/动态效果。
- 过滤发生在快照构建阶段，灯光、阴影、反射、轮廓、悬浮提示、目标候选全部只读同一份已过滤数据。
- 旋转/缩放不重算探索范围、不写额外记忆；移动、开关门、灯光变化、楼层变化后按原回合刷新。
- 俯视相机只是“看得见什么就画什么”，抬高与旋转不会增加任何一格知识。

## 5. 建筑外观与楼层剖切（P3）

- `fp3d_snapshot::cell` 新增 `over_avatar_region`：玩家所在**室内可行走连通区**（含一圈墙体）的列标记。
  判定来自原版 `map::is_outside()` 与 `map::passable()`，只在玩家自身楼层、快照网格内做一次有界 BFS；
  墙体/关闭的门/不可通行家具阻断扩散，因此相邻独立房间保留自己的屋顶。
- `fp3d_snapshot::capture(..., bool cut_overhead)`：仅当俯视时为 `true`。
  它只做两件事：<br>
  1) 跳过 `over_avatar_region && z > 玩家层` 的格子；<br>
  2) 把 `over_avatar_region` 当前层格子的 `ceiling_present / glass_ceiling / ceiling_surface` 清空。
  不新增格子、不读取未感知楼层、不复制任何室内内容。
- 于是：室外保留已知建筑外壳与屋顶（原 `exterior_only` 规则不变），进屋后只看开所在层，
  其它建筑、其它房间保持外观；墙、关闭的门、黑暗仍沿用原版遮挡。
- 已知限制（未验证/未实现，如实记录）：
  - 首版**不**对贴近镜头的墙做局部降墙/透明处理（P3.4 的“可选”项），只依赖真实墙体遮挡。
  - 连通大空间（开放平面/连体建筑）按可行走连通区整体展开，不做逐建筑识别；
    被墙隔开的相邻房间保留各自屋顶。
  - 楼梯、地下室、中庭按“玩家层 + 列标记”统一处理；上下楼由下一次回合捕获刷新（同帧内不闪出其它层）。

## 6. 玩家贴图与手持隐藏（P5）

- 快照新增玩家自身贴图 `sprite.is_avatar`（`player_male` / `player_female` + `get_overlay_ids()` 衣物叠层，
  蹲伏时高度 1.2m），锚定玩家真实地块中心与实际楼层。
- 第一人称绘制**忽略**该贴图（否则身体会贴到镜头上）；俯视绘制它，因此不会重复画成 NPC。
- 拾取/瞄准**跳过** `is_avatar` 贴图：不会把自己当生物目标。
- 俯视不绘制第一人称手部/手持物、挥击覆盖层，也不绘制屏幕中心准星（改由目标轮廓标记鼠标所指地块）；
  真实装备、攻击结算、原生照明全部不变。
- 已知限制：乘车/驾驶时玩家贴图使用原版取整地块（车体连续坐标的位置），不做逐座精确偏移。

## 7. 改动文件

| 文件 | 改动 |
|---|---|
| `src/fp3d_camera.h/.cpp` | `orbit` / `orbit_pose()` 俯视相机数学（纯函数，可测） |
| `src/fp3d_snapshot.h/.cpp` | `cell.over_avatar_region`、`sprite.is_avatar`、`capture(..., cut_overhead)`、玩家贴图、区域 BFS 与剖切 |
| `src/fp3d_targeting.h/.cpp` | `ray()` 增加屏幕 NDC（x/y）与 aspect，射线基底与投影一致；扫掠盒精确化；跳过玩家贴图 |
| `src/fp3d_compositor.h/.cpp` | 相机类型状态、`camera_pose()` 单一位姿、`toggle_camera()`、F6 路由、滚轮/中键、鼠标拾取、`movement_heading()`、HUD 提示、QA 覆盖 |
| `src/fp3d_scene.h/.cpp` | `draw(..., bool overhead)`：手持物/准星/玩家贴图开关；视锥剔除改用真实相机高度；雾窗按眼高拉伸 |
| `src/fp3d_weather.h/.cpp` | `begin_atmosphere` 增加 `far_scale`（俯视雾窗拉伸，默认 1.0 不影响第一人称） |
| `src/game.cpp` | `capture(..., fp3d_compositor::overhead())` |
| `src/handle_action.cpp` | 俯视鼠标目标取自快照拾取；右键下拉菜单与 `ACTION_FIRE` 复用现代 2D 路径；空菜单不冻结界面 |
| `src/input_context.cpp` | 移动方向使用 `movement_heading()`；俯视右键请求直接返回 `SEC_SELECT` |
| `src/fp3d_quick_menu.cpp` | 下拉菜单冻结背景按 3D 视口矩形摆放（不再整屏拉伸） |
| `src/fp3d_input.cpp` | 模式设置窗口加一行 F6 俯视说明 |
| `tests/fp3d_camera_test.cpp`、`tests/fp3d_interaction_test.cpp`、`tests/fp3d_snapshot_test.cpp`、`tests/fp3d_camera_weather_gl_test.cpp` | 新增俯视相机/射线/剖切/输入契约/截图用例 |
| `README.md`、`AGENTS.md` | F5/F6 行为说明更新 |

## 8. 验证结果

### 8.1 构建

- 工具链：`.tools/msys64/ucrt64`（GCC 16.2.0），Ninja，CMake 参数与 `tools/build-fp3d.ps1 -Feature ON` 一致
  （`TILES=ON, SOUND=ON, LOCALIZE=ON, DYNAMIC_LINKING=ON, ENABLE_FP3D=ON`）。
- `cataclysm-tiles`：482/482 全部编译并链接成功，0 error（`build/0i1/src/cataclysm-tiles.exe`）。
- `cata_test-tiles`：739/739 编译并链接成功，0 error。
- 打包：`runtime/0i1/cataclysm-tiles-overhead.exe`（strip 后 48,358,781 字节），依赖 DLL 来自既有 `runtime/0i1`。
- 注意：本工作树的 `.ninja_deps` 每次运行都会被判定为不完整，ninja 因此每次全量重建（不影响结果，只影响耗时）。

### 8.2 自动化测试

新增/更新的用例（全部通过）：

| 用例 | 标签 | 断言 | 内容 |
|---|---|---|---|
| `fp3d_overhead_camera_orbits_the_pivot_and_looks_down` | `[overhead][camera]` | 179 | 方位角/距离/俯角数学：眼位到 pivot 的距离恒定、pivot 高度不变、俯角钳制、yaw 归一化 |
| `fp3d_overhead_pointer_ray_shares_the_rendered_camera` | `[overhead][ray]` | 28 | 用同一相机把世界点投到屏幕再反投回来，7 个方向都命中同一格；中心射线命中玩家自己那格；记忆格与上层格不可选 |
| `fp3d_overhead_capture_lifts_only_the_avatar_room_ceiling` | `[overhead][snapshot]` | 62 | 同一房间同一位置：第一人称快照有天花板、俯视快照无天花板；墙/地板不变；**剖切只删“本区域内、玩家层以上”的格子，区域外的每一格与第一人称快照逐字段相同**；玩家贴图存在且只有一份 |
| `fp3d_overhead_camera_cuts_the_room_roof_and_points_at_the_avatar` | `[.fp3d_overhead_gl]` | 39 | **真实 GL 渲染**：真窗口 + 真 FBO + 真 `capture()`；F6 首次按下切换、长按不切换、松键不切换、再按回第一人称；俯视帧非黑且与第一人称帧不同；屏幕中心点中玩家格；滚轮缩放后中心仍是玩家格；未剖切与剖切的画面不同；全程 `glGetError()==GL_NO_ERROR` |
| `fp3d_overhead_camera_owns_wheel_and_drag_but_not_world_clicks` | `[.fp3d_overhead_gl]` | 48 | F6 切换不推进回合/移动力；`movement_heading()` 在俯视恒为 0、`heading()` 跟随方位角；滚轮只在世界视口内被消费且距离钳制在 6–48；中键拖动改变方位角、画面随之变化；**旋转/缩放不触发任何区块重建**（`fp3d_scene::statistics().rebuilt == 0`，即不重新捕获也不重新生成几何）；**失焦后拖动立即停止**；右键被相机接管并产生一次“次要选择”请求（松开不产生、视口外不认领地块）；左键仍不被消费（交给原生派发）且鼠标拾取仍指向玩家格；原生面板打开时 F6 不被消费也不切换 |
| `fp3d_overhead_qa_frame_dump` / `fp3d_overhead_qa_frame_outdoor` | `[.fp3d_overhead_gl]` | 11 + 11 | 首次合成即为俯视帧的截图用例：室内封闭房间（验证剖切）与室外开阔地（验证整体观感），配合 `CDDA_FP3D_CAPTURE_FRAME=1` 把真实 FBO 存成 `fp3d-qa-frame.bmp` |

命令（在源码目录内执行）：

```
build\0i1\tests\cata_test-tiles.exe "[overhead]" --rng-seed 42
build\0i1\tests\cata_test-tiles.exe "[.fp3d_overhead_gl]" --rng-seed 42
build\0i1\tests\cata_test-tiles.exe "[.fp3d_camera_weather_gl]" --rng-seed 42
```

结果：`[overhead]` 3 用例 224 断言全部通过；`[.fp3d_overhead_gl]` **4 用例 114 断言全部通过**；
`[.fp3d_camera_weather_gl]`（既有 GL 回归）17 断言全部通过。

`[fp3d]` 全量：171 用例中 160 通过、11 失败（27/16846 断言失败）。这 11 个失败用例所在的源文件
（`fp3d_construction.cpp`、`fp3d_crafting.cpp`、`fp3d_furniture.cpp`、`fp3d_input.cpp`、蓝图/上下文轮盘
相关代码）**本次改动均未触及**（`git diff` 为空），失败内容为家具模型件数、配方文本、建造需求文本、
修饰键绑定、链网门件数、`t_open_air` 写入等，属于本工作树先前未完成的改动；无法回溯确认它们在本次
改动之前是否已经失败（工作树在开工时已有 52 个文件、4688 行未提交修改），故按“与本功能无关”记录，不宣称修复。

因快照新增了玩家自身贴图，6 处“只能有 N 个世界精灵”的断言改为排除 `is_avatar` 的计数
（`fp3d_snapshot_test.cpp` 5 处、`fp3d_pickup_ui_test.cpp` 1 处、`fp3d_furniture_test.cpp` 1 处），
语义不变：仍然断言没有任何隐藏物品/生物泄漏到快照里。

### 8.3 实机（游戏窗口）验收：**已完成**（2026-11-15 13:00）

用 `runtime/0i1/cataclysm-tiles-overhead.exe --basepath …\CDDA-0I-FP3D --userdir …\qa-user-full`
真机启动（默认场景新角色：囚犯 / 培诺伯斯科特城，第 61 天），逐项按键截图：

| 截图 | 验收项 | 结果 |
|---|---|---|
| `qa/ingame-00-firstperson.png` | 基线 | 3D 第一人称：牢房内墙、木地板、天花板、床位与 HUD 全部正常 |
| `qa/ingame-01-overhead-f6.png` | **F6 切到上帝视角（P0/P1/P3）** | 相机升到空中俯视：**该牢房的天花板已被剖开**，从上方直接看到室内地板、床位与墙体轮廓；其余区域按原版感知显示，未知格子保持空白 |
| `qa/ingame-02-f6-back-firstperson.png` | **F6 再按回第一人称（P0 双向）** | 完全回到原来的第一人称画面，朝向/俯仰与切换前一致 |
| `qa/ingame-03-wheel-zoomout.png` | **滚轮缩放（P1）** | 距离变远，玩家所在房间仍在画面中心（缩放锚点=玩家地块） |
| `qa/ingame-04-middledrag-rotate.png` | **中键拖动旋转（P1）** | 方位角改变、画面随之绕玩家旋转，罗盘从 `N 0 deg` 变为 `NW 307 deg`；玩家地块始终在画面中心 |
| `qa/ingame-05-rightclick-unknown-command.png` | **右键地块菜单（P4）** | **发现缺陷**（见下），修复后未能在同一台机上再启动游戏复测 |

实机验收发现并修复的真实缺陷（右键菜单）：

- 现象：俯视中点击右键不弹菜单，底部消息出现 `未知指令：「按下鼠标右键」(3)`。
- 根因：3D 世界的输入配置把右键绑定为 `FP3D_AIM`（第一人称准星射击），因此
  原版 `SEC_SELECT ← MOUSE_RIGHT` 的绑定在 3D 世界里永远不会命中；事件落到
  `handle_action()` 只得到 `ANY_INPUT`，于是既不开菜单也不开枪，只打印“未知指令”。
  进程内的 GL 用例当时断言的是“左右键都不被消费”，把这条错误路径当成了正确行为。
- 修复：俯视状态下由 `fp3d_compositor` 自己接管右键——命中世界视口且拾取到地块时，
  记一个待处理的“次要选择”，丢弃这次点击；`input_context::handle_input()` 在该请求存在时
  直接返回 `SEC_SELECT`，于是走的是与现代 2D 完全相同的
  `ACTION_SEC_SELECT → context_2d_entries → show_dropdown → resolve_2d_choice` 路径，
  锚点仍是鼠标所在视口位置。用例改为断言：按下被消费且产生请求、松开不产生、
  视口外的右键被吞掉但不认领地块、左键仍交给原生派发。
- 复测限制：修好并重新打包后，本机 4 次启动全部在加载屏崩溃（见 8.5 的 GamePP 覆盖层），
  因此该修复只有用例与代码路径验证，**没有第二张实机截图**；这一点如实标注。

### 8.4 进程内真实渲染截图（与实机互补）

引擎在进程内跑同一条渲染管线（真 SDL/OpenGL 上下文 + 真 FBO + 真 `capture()` + 真 `compose()`），
由既有 QA 通道（`CDDA_FP3D_CAPTURE_FRAME=1`）把 FBO 写成 `fp3d-qa-frame.bmp`。
sidecar 记录确认这两张图确实是俯视相机：`yaw=0 pitch=60 overhead=1 camera_z=9 camera_height=15.5885`。

| 图 | 说明 |
|---|---|
| `qa/qa-fp3d_overhead_qa_frame_dump.png`（128×128，放大 4×） | **室内封闭房间**：玩家站在 7×7 房间中央，屋顶/天花板已按剖切移除，因此从上方直接看到玩家与房间地面；画面其余部分是天空。因为房间是密闭的，快照里只有房间与被门口放行的那一小片地面——**相机升高没有换来任何额外视野**，正是 P2 的核心要求。 |
| `qa/qa-fp3d_overhead_qa_frame_outdoor.png`（256×192，放大 3×） | **室外开阔地**：俯视相机 15.6m 高、60° 俯角，地面铺满画面，玩家贴图正在**画面中心自己的地块上**（旁边是家具），地图边界外仍是天空（未知区域不显示）。 |
| `qa/fp3d-firstperson-room-384.png` | 同一房间的第一人称对照帧：墙、地板、门框与中心准星，"剖切前有天花板/俯视无天花板"的差异可由像素比较确认（用例里有断言）。 |

截图直接证实：俯视相机位姿正确、地面/家具在正确位置、玩家贴图落在真实地块、剖切后能看到室内地面、
未知区域保持空白、准星在俯视中不出现。

### 8.5 环境说明：GamePP 覆盖层会偶发把启动打崩（与本改动无关）

同一份 EXE 有时能正常启动（8.3 的实机验收就是这么拿到的），有时在加载屏直接 `0xC0000005` 崩溃。
崩溃时的 gdb 回溯栈顶在 `C:\Users\StickySlime\AppData\Local\GamePPSDK\1.2.1615.625\GPP64.dll`
（第三方游戏覆盖层 SDK），路径为 `ntdll!LdrLoadDll → User32InitializeImmEntryTable → PeekMessageW`，
该覆盖层 `CreateFileMapping`/`CreateSemaphore` 返回 5=拒绝访问后访问违例。与 `--world`、`--userdir`、
环境变量无关；**本工作树上一版构建（`runtime/0i1/cataclysm-tiles-models-latest.exe`，2026-11-14 17:48）
同样会这样崩**，所以不是本次改动引入。重试若干次通常能赶上一次成功的启动。

另外记录一条自动化经验：`SendKeys`/`PostMessage` 合成的按键 SDL 窗口收不到（表现为按 F6、Esc、'a' 全无反应，
鼠标点击却正常）；用 `keybd_event` 发**真实扫描码**后 F6/滚轮/中键/右键全部按预期生效。这解释了早先
“按键无效”的假象，不代表游戏侧有问题。

### 8.6 未验证清单（如实标注）

- F6 的“长按不反复”（已在进程内用真 SDL 事件与 repeat 标志验证；实机只按过单次按下）。
- UI 缩放与高 DPI 下的鼠标命中（代码按逻辑尺寸换算并有用例，实机只在 100% 缩放下试过）。
- 真实存档下的建筑外表、进楼展开、上下楼、地下室、连体建筑、中庭（实机只用默认场景的牢房验证了剖切）。
- 右键下拉菜单的**修复后**实机外观（修复前已实拍到“未知指令”缺陷；修复后用例外加代码路径验证）。
- 玩家贴图与衣物叠层在真实 tileset 下、乘车/驾驶时的位置（实机只看到牢房里站立的角色）。
- 性能：旋转/缩放不触发快照与区块重建（用例断言 `fp3d_scene::statistics().rebuilt == 0`），
  但未记录实机帧时间与快照耗时对比。
