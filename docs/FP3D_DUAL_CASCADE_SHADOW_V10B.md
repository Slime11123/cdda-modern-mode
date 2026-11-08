# CDDA 第一人称 3D — v10b 双级太阳阴影与纹素稳定化

更新日期：2026-11-08

## 目标

承接 `FP3D_PHOTON_V9F_GREY_RECTANGLE_ROAD_FIX.md`：保留已修好的近远景道路材质和原有 Photon 配色；让远处车辆、建筑、灯杆得到更连续的太阳阴影，同时在近处提高精度，减少玩家行走过程中阴影贴图“漂移”。

## 实际完成的源码

- `src/fp3d_shadow.h/.cpp`：Ultra (quality=3) 使用**两张独立深度纹理**。近级 2048×2048、半径 28 m；远级 1536×1536、半径 96 m。太阳方向、实际可见的地图几何、车辆网格完全共享，但光源正交投影范围独立；每个场景 generation 只刷新一次贴图，不随鼠标转头重复绘制。
- `src/fp3d_shaderpacks.cpp/.h`：加入 `u_shadow_far`（纹理单元 4）、远级半径/分辨率与两级纹素校正偏移。根据光空间近级边界平滑过渡 PCF (3×3) **阴影系数**，不直接混合两张深度值，避免出现一张不存在的“虚假遮挡体”。远级超出范围仍渐隐，边界外不采样钳制到纹理边缘的深度。
- `src/fp3d_mcshader.cpp`：Photon 视觉适配体积太阳光的沿射线遮挡也支持在近、远级之间过渡，不再到达近级边界就突然失去太阳遮挡。
- `src/fp3d_compositor.cpp`：让渲染器和后处理共享太阳方向、范围、纹理、相机与纹素偏移；QA 侧记录近远级实际纹理/深度占用/对齐偏移。只有使用高质量原生阴影时才执行双级 Pass；不影响原版 2D。
- **稳定化**：两张太阳正交投影的纹素网格都在绝对世界坐标中对齐，计算使用双精度世界锚点，GPU 模型仍使用局部 float。玩家在地图上移动整数格不会让阴影纹素产生额外的亚像素滑动。
- **安全回退**：远级深度 FBO 创建失败、第二次绘制发生 GL 错误，或 QA 明确关闭远级时，重新采用旧版 2048×2048 / 半径 38 m 的单张阴影地图。所有深度纹理随阴影关闭、分辨率变化或释放正常清理，不强行关闭正在运行的游戏。

## 测试结果

- Windows UCRT64 CMake 全程编译及链接返回码 0。
- NVIDIA OpenGL 4.6 Photon v1.4 GLSL、HDR/depth、60 次 post target resize、太阳阴影合成、简易 MC direct-final、错误 GLSL 回退和实际深度 SSR / 体积光测试全部 PASS。
- `qa/_validate_shadow_vehicle_geometry.py`：16 项车辆真实网格投影静态校验 PASS，不再以矩形车辆代理投影。
- `qa/_qa_dual_shadow_v10b_game.py`：在**真实 CDDA 3D 游戏**的 ShaderDaylightLab、同一车辆旁摄像机、Photon v1.4、Ultra 阴影条件下：
  - 双级成功：近深度纹理存在，`shadow_near_radius=28`；远深度纹理存在，`shadow_far_radius=96`；远级深度实际遮挡像素约 **44,810**；4 辆车辆/156 个车辆部件正常加载，OpenGL framebuffer readback error=0。
  - 强制单级回退：`shadow_far_texture=0`，`shadow_near_radius=38`，旧版单图深度占用约 422,838；旧 v9f 与 v10b 回退整屏平均绝对 RGB 差约 (0.116,0.055,0.022)。
  - 开启双级与回退相比，整屏平均绝对 RGB 差约 (5.61,3.77,2.32)，实际像素发生改变。
  - 抓帧图：`qa/csm-v10b-single_v9f.png`、`qa/csm-v10b-dual_v10b.png`、`qa/csm-v10b-fallback_v10b.png`。
- `qa/_validate_texel_snap_v10b.py`：601 组世界原点移动、近/远/回退三种阴影范围的投影对齐，最大纹素小数误差小于 10⁻⁹，PASS。报告 `qa/csm-v10b-world-texel-snap-qa.json`。
- QA 强制回退通过 `CDDA_FP3D_CAPTURE_FRAME=1` 且 `CDDA_FP3D_QA_DISABLE_FAR_SHADOW=1`，不改变正常游戏选项。所有用户选择的光影 ZIP、质量与光影开关在测试完成后恢复原值。

## 当前仍然不是完整 Minecraft 光影

这项实现为 CDDA 原生 OpenGL 的两个级别定向阴影，没有执行 Photon/Iris 的整套 multi-pass / G-buffer。已增加地面水、湿沥青、混凝土的粗糙度/微法线近似，但**尚无完整贴图包 PBR normal/roughness 输入、车辆和建筑精确反射的独立 G-buffer，也未实现时域 SSR 历史缓冲与重投影**。这些需要后续单独开发和跨视角防重影测试。

性能：Ultra 初次为两级共分配约 25 MiB 深度纹理；场景变化时会渲染两次投影几何，实际帧率应继续用用户的车辆/城区场景测试。降低质量或关闭阴影可回退原有渲染路径。

## 运行

根目录 `启动3D操作修复验证.cmd` 当前实际经 `Launch-Current-3D-Models.ps1` 自动编译并部署 `runtime/0i1/cataclysm-tiles-models-latest.exe`，因此它会使用**当前最新 v10b 源码**，不需要手工改 CMD 内的旧 EXE 名称。QA 单独保存的可执行文件：`runtime/modern-3d-shaderpacks/cataclysm-tiles-photon-csm-v10b.exe`。

本轮无 GitHub commit/push，无存档修改；旧的 v9f EXE 仍保留供对照。
