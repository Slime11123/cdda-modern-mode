# P3-B：3D 车辆零件准星拾取与原版交互

日期：2026-11-08。依照 [车辆总体计划](FP3D_VEHICLE_VISUAL_DESIGN.md) 和 [P3-A 座椅镜头](FP3D_VEHICLE_P3A_SEAT_CAMERA.md) 继续直接实现，继承先前 V8 车顶/玻璃和悬浮平面改动。

## 已实施

1. **原生零件坐标随快照发布**：`src/fp3d_vehicle_renderer.h::part_view` 增加 owned `native_position`；`fp3d_snapshot.cpp` 使用原生 `vehicle::mount_to_tripoint_abs(part.mount)` 生成。该绝对地图格只作为原版交互目标，不用于 3D 网格或车体旋转；渲染线程没有 `vehicle*`、`map*` 和零件引用。
2. **车辆局部射线检测**：`fp3d_targeting.cpp` 在所有正常地形/家具相交测试之后，按实际确认车体 `angle_degrees` 将相机射线转换进同一个车体局部安装坐标系。为车门、轮胎、座椅、车顶、货舱、框架、窗等建立与程序化模型尺寸近似一致的拾取包围盒，反向转换边框。选择最近的一块本机已可见原生零件，不允许后方地形或物品抢占实体命中。**这是局部近似碰撞盒，不是逐三角形像素级拾取，特别是打开并转动的车门仍需实机调整。**
3. **身份绑定与执行前重验**：命中结果保存 `kind::vehicle`、`vehicle_origin`、`vehicle_part`、安装坐标 `mount_x/mount_y`、零件 `id` 和 `native_position`。游戏线程 `fp3d_interaction::valid` 重新调用原版 `map::veh_at`，逐项验证原车仍在原点、零件索引合法、没有被拆卸/隐藏、安装点与类型仍一致、原生地图落点仍一致，还必须满足原版玩家视线、近身距离和同层规则。车辆移动、拆门、改装后旧目标不能被直接执行。
4. **短按 E**：准星命中可开合的完好车门时，通过原版 `ACTION_OPEN/ACTION_CLOSE` 发起原版门锁、动作消耗和开门处理；其他零件返回 `ACTION_EXAMINE`，由原版车辆检查/改装界面处理。没有重做车门状态和零件修理逻辑。
5. **长按 E**：复用现有快速菜单，提供查看/改装车辆、打开门、关闭门、检查零件、查看货舱（`ACTION_EXAMINE_AND_PICKUP`）、拖拽/松开（`ACTION_GRAB`）、当前位置检查和取消。每个选项按实际零件类型、车门开关及驾驶状态可用；提交前再次运行原版验证。**货舱具体物品访问结果仍由原版检查界面与权限决定，本阶段不实现直接跳过车门取物。**
6. **准星提示与部件 HUD**：`fp3d_compositor.cpp` 的 E 提示识别车辆专属零件快照，不依赖旧地形 `interactable` 列表；目标信息缓存区分同格不同零件索引和所属车辆。HUD `fp3d_hud.cpp` 展示当前车辆名、原版翻译零件名；损坏的部件名用警示色。
7. **车内眼高保持一致**：选中零件的边框采用 P3-A 的实际镜头眼高，杜绝站立视角时的错误轮廓偏移。`fp3d_scene.cpp` 其他阴影、MC 光影接入保持原样。

## 涉及代码

- `src/fp3d_vehicle_renderer.h`：车辆零件原版位置只读字段。
- `src/fp3d_snapshot.cpp`：以原生车辆安装点填充只读字段。
- `src/fp3d_targeting.h/.cpp`：`kind::vehicle`、零件身份数据、局部旋转拾取与边框、遮挡。
- `src/fp3d_interaction.cpp`：按原生车辆/零件再验证、E 与长 E 操作、原版 ACTION dispatch。
- `src/fp3d_hud.cpp`：本地化车辆/零件信息。
- `src/fp3d_compositor.cpp`：提示和零件缓存 identity。
- `src/fp3d_scene.cpp`：轮廓显示使用真正镜头高度。

## 本机验证

- `cataclysm-tiles` Windows CMake/Ninja 完整编译链接成功；最后 E 提示改动也已重新编译成功。
- `qa/validate_vehicle_p3b_target.py`、`qa/vehicle-p3b-part-target-validation.json`：24 个确认车头朝向下总计 1,440 组随机刚体变换与其逆变换坐标测试；针对车门/轮胎/车顶/货舱的轴向射线命中与错误方向/超距拒绝；错误原点/地图格/零件索引/安装点/零件类型被拒绝；源码具备必需原版权限和匹配条件。以上为静态 + 几何解析验证，不代表游戏交互已被现场验收。
- 独立 EXE：`runtime/modern-vehicles-interact-p3b/cataclysm-tiles.exe`；49 个 DLL 与 P3-A 以硬链接复用，避免重复占用。
- 启动文件（外层工作区）：`启动车辆P3零件交互测试.cmd` / `Start-VehicleParts-P3B.cmd`。原来 MC 光影主启动器未覆盖，P3-A / V8 仍保留。
- 原版 EXE `--help`、`--jsonverify` 的结果以本次测试文件退出码为准。

## 手工验收（尚未完成）

在独立 P3-B 测试版中进入 3D：
- 站在完好车门前，转动视角对准门板，检查有无 E 提示；短按 E 开门，再按 E 关门；长按 E 见正确菜单。
- 对准轮胎、发动机、座椅、货舱、受损/拆卸零件，确认 HUD 里的零件中文名称及操作对应实际目标。
- 在 0° / 45° / 90° 的车辆、停放废车及受损车旁复测；远处（大于 1 格）、隔墙、楼层不同的车不允许操作。
- 车辆移动或零件改装后旧准星目标不得再指向别的车辆或错误零件。
- 乘车时视角不穿越挡风玻璃、A 柱、车顶，检查 P3-A 车内实际位置。

## 后续工作 P3-C / P4

P3-C：基于实际用户截图补齐打开车门的铰链后几何拾取、原版修理界面的直达交互与货箱细分，完善驾驶中远程视线和原版车撞击回归；P4：所有车辆类型、车顶玻璃遮挡、图块包变化与性能/渲染缓存统计。

本次没有修改游戏存档、原版驾驶/碰撞规则、原版车辆物理，**没有 Git commit、push 或 GitHub Release**。第三方不死人贴图仍在用户本机使用，没有打包再分发。
