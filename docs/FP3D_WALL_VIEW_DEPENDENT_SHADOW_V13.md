# CDDA 3D Ultra v13：墙面近距离与转视角阴影稳定性

日期：2026-11-08

## 用户反馈

- 11 月 8 日 v12 后，在封闭房间的一整面墙上仍出现黑色斜三角；转动第一人称视角时斑块发生变化。
- 后续截图显示室外/室内都可能出现，镜头靠近墙面时更明显，不能只用玩家是否在室内的全局标志来关闭阴影。
- 目标：保留 Photon v1.4/Ultra 近远级阴影、建筑和车辆真实投影，修复墙面近距离错误自阴影。

## 修改位置

- `src/fp3d_shaderpacks.cpp`
  - 太阳阴影接收端仍从深度还原世界位置，但将已确认接近 X/Y/Z 轴的平面法线吸附到对应世界轴，降低屏幕像素导数在面片接缝和近镜头处随视角旋转引起的变化；斜坡/车辆非轴对齐面的法线不受硬性轴吸附。
  - 近级和远级太阳深度贴图投影统一使用沿可见法线偏移后的 `shadow_world`。轴对齐竖墙法线偏移 0.055m，水平/斜面偏移 0.014m，减少墙面接收点和深度遮挡体不完全重合造成的自阴影。
  - 保留原来限制背向太阳表面只接受环境光的 `direct_receiver` 和现有 PCF、双级渐变、深色太阳投影。
  - **不按玩家位置、`u_outdoors` 或 `u_sky_visible` 全局禁用墙面阴影**：窗外、门口真实日照仍通过真实深度图计算。
- `src/fp3d_compositor.cpp`
  - 仅 QA 抓帧模式支持 `CDDA_FP3D_QA_WALL_DISTANCE` 和 `CDDA_FP3D_QA_WALL_YAW`，对比同一墙的近距离/视角旋转；平常游戏不会读取这两个变量来改变用户相机。

## 编译与部署

- Windows UCRT64 CMake: `fp3d_shaderpacks.cpp` 与 `fp3d_compositor.cpp` 增量编译、最终链接均退出码 0。
- 经项目 `Launch-Current-3D-Models.ps1 -VerifyOnly` 自动进行 SHA256 对比并重新部署，真正的下次启动位置是 `runtime/0i1/cataclysm-tiles-models-latest.exe`。
- 最终部署文件 SHA256：`111E6458136C200F3FDDC31180B9DADD4BFD98B9E8B18E76AE5940A04DCADF32`。
- 同步修复根目录 `Launch-Current-3D-Models.ps1` 的重复部署问题：GNU strip 在同一份 Windows 构建上重复执行会改变 EXE 字节，旧启动器误认成“源码已更新”。现在将 CMake 原始 EXE SHA256 与部署后的 EXE SHA256 记录在 `runtime/0i1/.models-latest-build.json`，每次先双重核验，只有源码变动或目标被篡改才重新部署。连续两次 `-VerifyOnly` 的第二次明确显示 `no reinstall needed`，最终文件哈希未变化。
- QA 隔离可执行文件：`runtime/modern-3d-shaderpacks/cataclysm-tiles-wall-stable-v13.exe`。

## 实机自动化回归

1. `qa/_qa_wall_view_stability_v13.py`：实际启动 CDDA 3D + ShaderDaylightLab + Photon v1.4 + Ultra 的近墙镜头，距离约 1.65m、视角 0° / -18° / +18°、强制室内状态、无阴影对照。五帧均 `usable=1`、`qa_read_error=0`；有阴影时近/远两级深度纹理均正常，关闭时均为 0。近墙同视角太阳阴影开/关的整帧平均绝对 RGB 差为 3.620、3.490、3.368。
2. 截图：`qa/v13_wall_near.png`、`qa/v13_wall_near_yawleft.png`、`qa/v13_wall_near_yawright.png`、`qa/v13_wall_near_indoor.png`、`qa/v13_wall_near_off.png`。
3. `qa/_qa_wall_stable_v13_vehicle_shadows.py`：真实场景中四辆车正确存在，双级阴影纹理有内容。新版本车辆附近道路 ROI 在开/关阴影间 RGB 平均绝对差为 4.8167、4.5463、4.2310，证明真实车辆及地面阴影没有被一并关闭；测试完成后恢复所有 QA 光影偏好字节值。
4. 截图：`qa/wall-stable-v13-car-v12_original.png`、`qa/wall-stable-v13-car-v13_fixed.png`、`qa/wall-stable-v13-car-v13_no_shadow.png`。

## 注意

- 上述测试并没有直接覆盖用户最新截图所用的灰色墙材质、房间与太阳角度。QA 中轴对齐近墙没有观察到明显的新三角色块，但不能宣称用户现场已经百分之百解决；用户仍需在原来的位置以相同天气、时间和 Ultra 设置再检查。
- 请完全退出游戏并通过根目录 `启动3D操作修复验证.cmd` 重新打开；现有运行游戏无法热替换 C++ 阴影代码。
- 本次不改存档、原版 2D、已安装光影 ZIP，也没有执行 GitHub 提交/推送。
