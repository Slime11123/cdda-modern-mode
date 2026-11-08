# CDDA 3D Photon Ultra v11c — 墙面重复三角形阴影修复

更新时间：2026-11-08。

## 原因和实现

用户反馈：开启 Ultra 后，长建筑外墙出现一片片重复的三角形深色暗斑，希望保留车身、建筑、路灯和地面清晰的真实太阳投影。

1. fp3d_shadow.cpp：墙体的可见 3D 模型每格宽 0.98 米，旧太阳阴影遮挡网格也保持 0.98 米宽，留下约 2 厘米间隙。斜射光可穿过连续的细缝产生漏光条纹。现在仅太阳深度 Pass 的实体墙遮挡体半宽由 0.49 改成 0.502 米，微量重叠以封闭接缝。可见墙体、贴图、碰撞和存档不变。
2. fp3d_shaderpacks.cpp：利用深度缓冲的世界坐标导数重建接收表面法线，并按朝向太阳的角度分别计算近级和远级深度偏移。背向太阳的墙、屋顶下表面不应被后处理重复大幅压暗；保留 16% 最低阴影以兼顾双面物体的法线不确定性。
3. 保留 Ultra 双级 28 m / 2048² 与 96 m / 1536² PCF 太阳阴影、平滑交界和世界坐标纹素对齐，保留车辆、邮箱、街灯的实体投影，也保留旧版道路灰矩形修复。
4. 仅 QA 抓帧支持 CDDA_FP3D_QA_CAMERA_NEAR_WALL=1，在真实测试存档里选择一段已加载墙面，不移动玩家、不修改真实地图或存档。

## 验证

- Windows UCRT64 CMake 完整编译、链接成功。
- NVIDIA OpenGL 4.6：Photon GLSL smoke、HDR、60 次渲染目标 resize、太阳阴影开关、Minecraft 原版 final 安全回退、SSR 和体积光测试均通过。太阳阴影 smoke 红色通道开启/关闭为 193 / 204，说明未直接关闭阴影。
- 同一份 ShaderDaylightLab、同一 QA 相机，对比两级阴影开关及强制单级回退。v11b 开关太阳阴影的全屏绝对 RGB 差为 (13.11, 10.90, 9.16)，v11c 则为 (9.74, 7.59, 5.97)，人工检查墙面和柜体上的不规则压暗明显减轻。
- 固定车辆近景：新版蓝车及附近地面仍然有明显深色投影，开关太阳阴影时车辆/道路 ROI RGB 平均绝对差约 (4.82, 4.65, 4.37)，没有为了修墙而关闭全部车影。
- 数据来源：qa/wall-v11b-wall_shadow_on.png、qa/wall-v11b-wall_shadow_off.png、qa/wall-v11c-wall_shadow_on.png、qa/wall-v11c-wall_shadow_off.png、qa/wall-fix-v11c-car-v11c_fixed.png。QA 使用 Photon v1.4 Ultra，所有临时光影设置在结束后还原。
- 注意：QA 镜头不是用户截图的原始那面墙，不能声称该具体长墙已经逐像素验收。若仍有按砖格完全重复的纹理阴影，需要进一步检查当前贴图包原生 2D 图块中烘焙的伪阴影。

## 部署和回退

根目录 启动3D操作修复验证.cmd 通过 Launch-Current-3D-Models.ps1 自动构建，并比较编译 EXE 和启动 EXE 的 SHA256，避免之前时间戳相同却启动旧版。当前 Ultra .quality=3、.shadows=1，仍选用 photon_v1.4.zip，强度不变。
已在没有游戏进程时执行安全 VerifyOnly 构建与安装，实际启动位置 runtime/0i1/cataclysm-tiles-models-latest.exe。隔离回退测试文件 runtime/modern-3d-shaderpacks/cataclysm-tiles-wall-receiver-v11c.exe。
此次不修改玩家存档、不上传 GitHub。
