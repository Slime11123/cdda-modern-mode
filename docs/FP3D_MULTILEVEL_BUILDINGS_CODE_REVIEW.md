# CDDA 0.I-1 FP3D 多层建筑显示：源码审查与实施计划

> 审查日期：2026-10-27
> 审查对象：`C:\Users\StickySlime\Documents\Codex\Projects\cdda3dproject\CDDA-0I-FP3D`
> Git 基线：`e24db516`
> 状态：**已实际读取当前工作树源码；本文件是实施计划，不是已修复、已编译或已通过游戏实机验收的补丁。**
> 注意：`source/` 是废弃的 SDL3 实验目录，不应在其中继续开发本项功能。

## 一、明确结论

单层问题由 FP3D 表现层造成，而非 CDDA 原版地图缺少多 Z 层支持：

1. `src/fp3d_snapshot.cpp:202` 使用 `player + tripoint_rel_ms(dx,dy,0)`：快照只能采集当前 z；`:172-173` 的缓存入口固定当前层。
2. `src/fp3d_snapshot.h:25-56` 的 `cell` 有绝对三维位置，却没有 `has_floor`、露天空洞、楼梯、坡道等结构语义；`scene_snapshot` 不记录渲染高度区间。
3. `src/fp3d_scene.cpp:20-39` 所有地面顶点 Y=0；`:181-185` 将 `offset.y()` 用作 OpenGL Z，但完全忽略 `offset.z()`；`:222` 的墙底固定在 0。
4. `src/fp3d_scene.cpp:169` 镜头固定 `-1.6f` 眼高；视锥测试约 `:189-193`、精灵约 `:259-295` 均以单层高度计算。
5. `src/fp3d_scene.cpp:144-151` 的 `known_wall()` 对每扇门窗使用 `std::any_of(scene.cells)`，多层后性能会进一步恶化。
6. `src/game.cpp:4206-4211` 在游戏绘制主线程调用 `prepare_view` 和 `capture`；`src/fp3d_compositor.cpp:525-527` 将快照交给渲染器。可保留此时序和 2D/3D 切换机制。
7. `src/game_constants.h:28` 定义 `fov_3d_z_range = 10`；`src/map.cpp:9549-9615` 的 `build_map_cache` 对开启 zlevels 的地图可预建全部 z 层的多种基础缓存；`src/map.cpp:7307-7401` 有含楼板阻断的跨层 LOS。
8. `src/map_memory.cpp:277-314`：`prepare_region` 要求入参两个端点位于同一 z，但函数内部**已按 z 视野范围预加载邻近各层记忆**；不要给它传不同 z 的端点，也不必为每层盲目重复加载。
9. `src/map.cpp:2639-2649` 有 `has_floor`；`:2367-2377` 有 `is_open_air`；`src/mapdata.h:268-275` 定义 `GOES_DOWN/GOES_UP/NO_FLOOR/RAMP`。可复用原版结构语义。

## 二、目标与不变量

**第一阶段目标：** 室外可见三层以上建筑外立面、屋顶与相邻楼层；进入建筑后有正确的楼板、天花板、楼梯与地下室。楼层高度统一，二楼不能叠在一楼。

**必须保留：** CDDA 原版位置、Z 层、行动回合、碰撞、楼梯移动、存档、视野与记忆；F5 2D/3D 无需重载；图块包材质随已有系统工作；不做独立 3D 世界物理/生成，不为渲染加载世界外的新地图。

**不可泄密：** 不得从未可见/未记忆的上层实时地形、家具、怪物或物品生成室内信息。室外完整轮廓和原版感知规则可能存在冲突，优先做“可见或已记忆的外立面”。如要在未探索区域直接显示整栋建筑的完整隐藏轮廓，必须另外明确设计“非敏感外观代理”与探索信息策略，不可默默读取所有未知楼层。

## 三、建议的最低侵入实现

### P0：固定运行基线和回归闸门

- 在 `CDDA-0I-FP3D` 创建独立 Git 分支/提交点；不要清理或覆盖 `runtime/` 存档。
- 记录 ON/OFF 构建、现有 `tests/fp3d_snapshot_test.cpp`、`tests/fp3d_gl_test.cpp` 的结果。
- 在 2D/3D 切换前后读取玩家绝对 `(x,y,z)`，验证不会改变。
- 保持 `ENABLE_FP3D` 条件编译与旧 2D 运行路径不变。

### P1：在快照中采集多个 z 层（核心）

修改 `src/fp3d_snapshot.h/.cpp`：

- 在 `scene_snapshot` 添加 `min_z/max_z`（或 `min_relative_z/max_relative_z`）和版本/分层统计。
- 在 `cell` 添加语义字段：`floor_present`、`open_air`、`goes_up`、`goes_down`、`is_ramp`，必要时区分屋面类型。可记录“信息来源：live/memory”以防误用。
- 快照采样改为三重循环：`for z in [player.z()-below, player.z()+above]`，再循环 `dy/dx`；构造 `tripoint_rel_ms(dx,dy,z-player.z())`。
- 第一阶段可用上 4 层、下 2 层作为**配置候选值**，受 `OVERMAP_DEPTH`、`OVERMAP_HEIGHT`、`fov_3d_z_range`、地图实际支持 zlevels 和总性能预算裁剪；三层以上室外显示需至少采集上两层。必要时对高层建筑扩展到受控视锥区域。
- 对每一格先完成原版视线与缓存有效性检查。**实时可见**时才读取 `here.ter/furn/has_floor/is_open_air` 等真实世界信息；**不可见**时只读取 `you.get_memorized_tile(abs_pos)` 并从记忆 terrain ID 的静态定义推算允许展示的几何。没有记忆的上层不得作为实体墙/楼板显示。
- 使用 `you.sees(here,p)` 及生物的 `you.sees(here,*critter)`，保留跨层光照/遮挡与感知差异。检查缓存刷新机制；不要对每个 z 都调用 `update_visibility_cache(z)`：它在 `src/map.cpp:6738-6745` 按全局玩家位置做早退，不能假定逐层刷新生效。
- `prepare_view` 可保持一次同 z 端点的 `prepare_map_memory_region`（原版内部已有跨层缓存），确认内存/视野范围后再扩展。
- 为所有快照数据保留三维绝对坐标；不得保存指向游戏 map/Creature 的裸指针。

建议分层扫描伪代码（非直接可编译补丁）：

~~~cpp
const int lower = std::max(-OVERMAP_DEPTH, player.z() - render_below);
const int upper = std::min(OVERMAP_HEIGHT, player.z() + render_above);
for (int z = lower; z <= upper; ++z) {
    for (int dy = -radius; dy <= radius; ++dy) {
        for (int dx = -radius; dx <= radius; ++dx) {
            const tripoint_bub_ms p =
                player + tripoint_rel_ms(dx, dy, z - player.z());
            if (!here.inbounds(p)) continue;
            const bool live_visible = fresh && you.sees(here, p);
            // live_visible 才读取实时结构和实体。
            // 否则仅根据已记忆 ID 构造已知表面。
        }
    }
}
~~~

### P2：统一三维坐标与建筑层高

修改 `src/fp3d_scene.cpp`，必要时 `src/fp3d_camera.h/.cpp`：

- 定义统一 `constexpr float floor_height = 3.0f`（初始视觉参数，不修改 CDDA 逻辑 z）。横向一格仍为 1m 的渲染尺度。
- 以当前玩家 z 为相机相对原点：`world_x = (tile.x-origin.x)`，`world_y = (tile.z-origin.z)*floor_height`，`world_z = (tile.y-origin.y)`。
- 重构 `floor(x,z,material)` 为 `floor(x,world_z,world_y,material)`，明确朝上地面和朝下天花板的法线/贴图；只在结构数据表明楼板存在时画地板，不能给 `t_open_air` 和 `NO_FLOOR` 默认补地。
- 重构墙、窗、门、箱体、植物、可见精灵底部参数，所有局部高度叠加 `world_y`。把固定的墙高 `2.2f` 与 `floor_height` 统一（可预留楼板厚度），避免墙顶与上层楼板间出现通天缝隙。
- `frustum.contains` 的 Y 传入 `world_y + local_height/2 - eye_height`，正确剔除上下楼层；2D 手持物品 HUD 保持屏幕坐标，不叠加楼层高度。
- 玩家上下楼后快照原点改 z 时，世界相对坐标随之变化；第一版瞬时切换正确，第二版再做纯视觉的垂直镜头插值。原版行动是否允许上楼、落下/踩空不受视觉插值影响。
- **不能仅将三重循环加上就完成**：不加入 world_y，所有层仍会相互重叠。

### P3：楼梯、通天洞、屋顶和地下室

- 地板/天花板以对应 z 层的 `has_floor` 与静态 `NO_FLOOR` 语义判断，注意 `map::has_floor` 的边界默认值：必须先 `inbounds`。
- `GOES_UP/GOES_DOWN/RAMP_UP/RAMP_DOWN/RAMP` 可用于可见或已记忆的楼梯/坡道外观；不要仅凭 `GOES_UP` 就粗暴挖掉整个上一层楼板，优先以实际上层 `NO_FLOOR` 或已知洞口匹配。
- 露天空洞 `t_open_air` 应能透视上下层；楼梯/洞口两侧、地面边缘可生成侧缘，避免悬浮平面。
- 现有一层墙、门、窗模型可重复用于 z=1、z=2……；屋顶不是“自动在最顶层盖一块板”，应从已知的实际地图/地形语义还原；没有上层地形的区域不能凭第一层墙推测三层大楼。
- 地下室以负 z 表现。进入室内时应看到楼上地板的背面作为天花板，同时原版 LOS 禁止透视敌人/物品。
- 高层楼房的外墙可以先采用统一立面材质，之后再对墙面、窗户、边缘做分块优化。

### P4：性能与缓存

- 目前 `radius=MAX_VIEW_DISTANCE`（`src/fp3d_snapshot.h:22`）。单层最多约 121×121=14641 格；7 层约 10.2 万格，当前逐格 LOS+逐格 OpenGL 立即模式不宜无限放大。
- 把 `known_wall(scene,p)` 从对整个 `scene.cells` 的 `std::any_of` 改成一次构建的 3D 位置索引（例如 `unordered_set` 或 map），减少门窗邻接检查的重复全表遍历。该查询必须只索引合法展示的快照格。
- 优先近层/视锥内建筑，采样与渲染分开计时；可见/记忆结构按 (chunk,z) 缓存，门窗变化/破坏/层高切换/图块包切换时失效，避免每帧构建全部几何。
- 网格合批或 VBO 是后续性能闸门，当前即时模式原型可用于正确性验证，不能假设 7 层/60 格必然稳定 60FPS。
- 捕获线程仍为游戏线程，渲染线程只用快照拷贝；不要访问未同步的 `map` 指针。
- 保留 FBO、深度缓冲、透明裁切、天空、菜单遮挡和独立 3D 输入相关回归。

## 四、源码级修改清单（按顺序）

| 优先级 | 文件 | 改动 |
|---|---|---|
| P0 | `src/fp3d_snapshot.h` | 增加多层结构字段和 z 范围 |
| P0 | `src/fp3d_snapshot.cpp` | 将 `dz=0` 单层采样改为可见/记忆安全的多层采样 |
| P0 | `src/fp3d_scene.cpp` | 全部几何/精灵改成 z→world Y 映射，修复空洞与天花板 |
| P1 | `src/fp3d_camera.h/.cpp` | 集中楼层高度/眼高参数，检查上楼时相机切换 |
| P1 | `tests/fp3d_snapshot_test.cpp` | 增加多层楼板/LOS/记忆隔离测试 |
| P1 | `tests/fp3d_gl_test.cpp` | 增加可视楼层高度、天花板、深度遮挡截图/像素断言 |
| P2 | `src/fp3d_scene.cpp` | 三维邻接索引、按 chunk 的几何缓存及剔除 |
| 按需 | `src/fp3d_compositor.cpp` / `src/game.cpp` | 仅新增多层调试统计/刷新钩子，不改 2D 世界规则 |

## 五、必须新增的验收用例

1. 地面层看外部真实二楼/三楼墙与窗，Y 高度正确。
2. 多层建筑上有真实屋顶；单层小屋不凭空生成多余楼层。
3. 玩家站一楼时，二楼完整楼板挡住上层室内，天花板方向正确。
4. 楼梯/坡道/电梯井/洞口不被自动地板封死；不允许从楼梯旁穿过实心楼板。
5. 玩家上楼和下楼后，相机与实体站位正确，2D/3D 切换不改变原版 z。
6. 地下室负 z 高度正确；向上看不到未经许可的房间、物品和怪物。
7. 不可见楼层实时开门/破墙之后，记忆层仍显示旧内容；重获视线才更新。
8. 完全未知的上层格不会读取或展示实时结构/生物；已有上层记忆可作为暗色表面。
9. 跨层 LOS 遵从 `map::sees`/`you.sees` 和 `has_floor_or_support`，不能单靠 GPU 深度测试授权可见性。
10. 烟雾、夜晚、失明、墙内/室外、不同楼层 NPC 与怪物：遵循原版感知。
11. 反复 F5 切换、游戏内切图块包、开关菜单、存取档、关门与破坏建筑均无崩溃。
12. 在至少三个不同高度的建筑场景记录 `capture ms`、`draw ms`、内存、FPS，并比对单层基线；测试 `ENABLE_FP3D=OFF` 构建仍通过。

## 六、建议开发提交分解

1. `feat(fp3d): capture safe multi-z geometry` — 仅扩展快照和测试，不改画面。
2. `feat(fp3d): apply layer world heights` — 地板墙门植物精灵均处于正确高度。
3. `feat(fp3d): represent floor gaps and ceiling surfaces` — 楼梯、洞口、地下层、屋顶。
4. `perf(fp3d): index neighbors and cache layered meshes` — 性能与改动失效。
5. `test(fp3d): multi-level LOS, visual and OFF regressions` — 自动化及实机验收。

**完成定义：** 真实二三层建筑在 3D 室外可见；楼层之间不是穿透空壳；原版行动与视野不变；已有 P2/P3/P4 功能正常；自动测试与实机验证记录齐全。当前仅完成审查，以上提交尚未实施。
