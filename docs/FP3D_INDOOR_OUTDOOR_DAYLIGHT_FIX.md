# Photon / CDDA 3D — 室内外阳光连续性修复（v4）

日期：2026-11-08。此修复属于 CDDA 自己的 Photon 视觉适配，不是原版 Iris/Photon 多阶段渲染程序。

## 发现的原因
- 之前太阳阴影 Pass 在玩家 sky.visible=false 或 weather.outdoors=false 时整体停止生成阴影纹理。玩家进屋就导致窗外阴影随之消失。
- Photon 的 8 步阴影体积光与 SSR 直接使用 u_outdoors 全局判定，窗外对象因此也被禁用。
- 普通场景 native_ambient 按玩家是室内还是室外修改整个世界的环境光，室外画面跨门槛会跳变。
- 太阳屏幕方位与光束同样错误地把玩家 outdoors 当成天空是否可见。

## 直接修改的实现
1. src/fp3d_shadow.cpp：太阳深度阴影纹理由日照、太阳高度、质量和地下安全条件决定；地表即使玩家站在房屋内，仍生成同一份合法可见建筑/车身/门窗/屋顶的光照遮挡。关闭门与屋顶遮住直接太阳光，打开的门和透明窗不强制封死洞口。
2. src/fp3d_scene.cpp：增强 3D 的全局 ambient 不再取决于玩家当前的室内外状态；太阳和云量仍按原生时间/天气更新。原版 Off 的旧环境光分支保留。
3. src/fp3d_mcshader.cpp：近场 SSR/体积光按每一像素的原生地形材质遮罩、屏幕深度和太阳阴影纹理计算，不因玩家入屋直接整屏关闭。不存在屏幕外物体凭空生成的反射。
4. src/fp3d_shaderpacks.cpp / .h 与 compositor：sky_visible 与 outdoors 被分开。实际能透门窗看到的天空继续有云层、太阳光束和天幕；关闭屋顶的房间不凭空生成天空。sky_visible 新字段追加在 environment 后面以保持旧 GPU 测试初始化兼容。
5. QA 模式 CDDA_FP3D_QA_FORCE_INDOOR 仅在 CDDA_FP3D_CAPTURE_FRAME=1 时可以模拟室内两项布尔值，绝不修改原生游戏天气和存档。

## 测试
- Windows UCRT64 完整 CMake ENABLE_FP3D=ON 编译+链接退出码 0。OpenGL 旧烟测通过：HDR、60 次纹理尺寸变更、太阳 PCF 阴影、Photon v1.4、简单 Minecraft final.fsh、GLSL 编译失败回退。
- OpenGL 真实 Photon 深度测试：有遮挡时体积光为零；无挡时可见太阳光；只把玩家 outdoors 切为 false 后，体积光像素最大差异 0，水/湿路面反射像素最大差异 0。
- 真实游戏 ShaderDaylightLab 2520x1392，4 辆车、156 个可见车辆部件：正常场景太阳阴影纹理非零；强制模拟 player outdoors=false 与 sky.visible=false 时，阴影纹理依旧非零，Photon 可用，GL read error 为 0。两种视角对应 qa/indoor-light-v4-outside.png 和 qa/indoor-light-v4-indoor_flags.png。窗外地面平均 RGB 均约 85.67/80.52/76.30，亮度比 1.000。
- 新版 EXE --jsonverify 退出 0，PowerShell 启动器语法错误 0，启动 CMD 已改为新文件；原 EXE、光影 ZIP、游戏存档及选项保持原样。

## 限制
- 实机 A/B 模拟的是室内标志，并非真的将角色移动到一栋带窗房子里。证明跨门槛不再全局关闭阳光和室外阴影，不等同于完整建筑每扇窗户的照射验收。
- 真实太阳直射仍需要真实门窗开口和对应太阳方向。正午高角度阳光大部分被完整屋顶遮挡是正常现象，不能把已实现功能当作完整室内多次弹射 GI。
- 特殊材质、玻璃、地下多层建筑、雨雪夜晚和快速移动仍待广泛实机测试；没有迁移原版 Iris/Photon multi-pass。
