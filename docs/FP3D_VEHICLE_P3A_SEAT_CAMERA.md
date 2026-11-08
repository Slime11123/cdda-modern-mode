# P3-A：CDDA 车辆原版座位绑定的 3D 驾驶／乘坐相机

日期：2026-11-08。接续 [车辆总体实施计划](FP3D_VEHICLE_VISUAL_DESIGN.md) 的 P3「驾驶、乘坐、交互」，以及 [V8 车顶悬浮面修复](FP3D_VEHICLE_V8_FLOATING_PLANE_FIX.md)。

## 本次已直接实现的内容

1. **车内位置来自 CDDA 原版数据**：在游戏线程 `fp3d_snapshot::capture()` 中，只对 `avatar.in_vehicle == true` 且车辆当前原版安装位置具有未损坏 `BOARDABLE` 零件的情况采集。位置使用 `veh_at(you.pos_bub())`、`vpart_position::mount_pos()`、`vehicle::pos_abs()`、`pivot_anchor[0]`、`pivot_rotation[0]`；不读取原版经离散重排的 `precalc` 坐标，也不构造任何伪造的座位。
2. **纯只读车内镜头快照**：新增 `fp3d_camera::rider_pose` 和 `scene_snapshot::rider`，仅发布世界坐标、原版当前确认角度、座位安装挂点、眼高和驾驶员状态。OpenGL 线程不持有 `vehicle*`、`map*`、`vehicle_part*`。
3. **真 3D 车内座位视角**：有原版有效 `SEAT` 的位置，采用眼高 1.42 世界单位；其余有效 `BOARDABLE` 挂点采用眼高 1.55。退出车辆、加载存档、切换 2D/3D 时重置车内镜头并回到常规 1.60 的站立眼高。
4. **已确认位置平滑，而非重做 CDDA 车辆物理**：新增 `rider_follower`，仅在相同座位、同一高度层、相邻确认位置（≤ 2 格）间进行约 90 ms 的视觉平滑，使用 smoothstep；换座、离车、传送、远距移动不对镜头外推，不更改原版玩家所在格、行动消耗、碰撞、燃油、驾车权限或转向模拟。
5. **原版车体转向同步 + 自由观察**：当且仅当同一原生座位上的车辆角度改变，相机在原始鼠标 yaw 基础上增加 `std::remainder( confirmed_angle_delta, 360 )`；不会每帧跟随预期 `turn_dir`，也不夺走玩家的自由视线。
6. **准星 / 场景保持同一相机**：`fp3d_compositor::visual_camera()` 为当前 3D 画面、`fp3d_targeting::ray()` 和目标缓存提供同一组 `x / y / height`。`fp3d_scene::draw()` 以新眼高转换 OpenGL 相机，玻璃深度排序也跟随当前眼高。原版攻击、远程射击和 3D 交互仍通过原生行动系统；本阶段不绕开范围、权限、可见性和判定。
7. **HUD 提示**：在 3D HUD 增加「车内：驾驶视角（CDDA 原版车辆控制）」或「车内：乘坐视角（操作遵循 CDDA 原版）」，状态来自 `vehicle::player_in_control(map, avatar)`，可区分真正驾驶与普通乘客／临时坐车。
8. **完整兼容旧视角**：不在车内时 `fp3d_camera::follower`、世界镜头和瞄准工作流程不变；无原生车辆挂点时完全回退原有视角。

## 涉及文件

- `src/fp3d_camera.h/.cpp`：拥有数据的 `rider_pose`、连续相机跟随及原生车体 heading 同步。
- `src/fp3d_snapshot.h/.cpp`：从当前游戏线程采集被载具承载的玩家坐席，原生座位控制验证。
- `src/fp3d_compositor.cpp`：相机统一入口，镜头状态复位，确认转向增量，自由观察，HUD。
- `src/fp3d_targeting.cpp`：准星射线发射高度与相机真实眼高同步。
- `src/fp3d_scene.cpp`：仅把 GL 视图换成统一的镜头 `offset.height`；光影原有处理保持。
- `src/fp3d_vehicle_renderer.cpp`：车窗排序距离使用同一眼高；没有变更原版实体数据。
- 变更仅在工作区，暂不 Git commit/push，保留同时开发中的 MC 光影、家具模型等其他未提交代码。

## 已执行的结构验证

`qa/validate_vehicle_p3a_native_seat.py`，结果 `qa/vehicle-p3a-seat-camera-validation.json`：

- 原版 4×4 / 甲壳虫 / 旅行车 / 皮卡的共 11 个座位挂点，分别对 0° 到 345° 的 24 个方向验证刚性坐标。
- 镜头位置和原版确认朝向不受游戏地图格取整干扰。
- 验证同座位约 90ms 平滑曲线不外推；换座、不同层级或非同一座位立即重置。
- 验证相机与射线使用相同眼高、2D/3D 模式切换重置、驾驶权限来自原版。
- Windows CMake/Ninja `cataclysm-tiles` 最终编译和链接成功（退出码 0）；独立 P3-A EXE `--help` 及 `--jsonverify` 均退出码 0，stderr 为空；JSON stdout 仅有非致命的测试路径字体文件缺失提示。独立包路径：`runtime/modern-vehicles-driver-p3a`，使用硬链接复用原 V8 的 49 个 DLL。启动入口为外层项目中的 `启动车辆P3驾驶视角测试.cmd` 或 `Start-VehicleDriver-P3A.cmd`。原有光影开发启动入口没有被覆盖。

## 下一阶段 P3-B

- **车辆部件精确拾取**：准星与 3D 车门、轮胎、货舱的碰撞几何映射到原生 `vehicle` 零件 `mount + index`，在游戏线程重新验证可见性和合法距离再调用原版车辆操作（开关门、检查、拾取/货舱和修车）。
- **视觉车内遮挡**：实机测试时检查转向镜头与车顶/A 柱是否过近，必要时只在车内对镜头近距离车身启用画面裁切，而非删除真实零件。
- **完成 P3-P4 验收**：进入/离开、换座、受损、碰撞、坡道、不同高度层和废弃车辆；P4 后再进行性能测量。

## 还未完成的人工验收

本阶段不能仅凭编译声明“车内实际可玩”。需要用户使用新测试版在同一辆车里实际检查进入、转弯、下车、换座、镜头是否贴住座位，以及准星与原版行为的一致性。普通车外模型/V8悬浮平面问题也不因 P3-A 而被宣称完全解决。
