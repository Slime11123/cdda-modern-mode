# Photon P5-B：真实墙面/地面 Surface Pass 原型与 GPU 法线方向验收

> 实施日期：2026-11-09（当地时间）。承接 FP3D_PHOTON_P5_GPU_ATLAS_EXECUTION_20261108.md。
> 状态：已实现且在隔离测试世界通过真实 OpenGL 回归，**但仍为 QA 专用，并非玩家正式开启的 PBR 效果**。
> 范围：只读 fp3d_snapshot::scene_snapshot，普通游戏、原版 2D、游戏规则与存档不变。

## 1. 实际源代码变更

- 新建 src/fp3d_surface_pass.h/.cpp：独立 RGBA8 表面属性 FBO，共享原场景深度纹理，只清颜色附件、深度只读 (GL_LEQUAL/DepthMask FALSE)，不覆盖 HDR 主场景。
- 兼容 GLSL 1.20 的 Fragment Shader：对真实当前 tileset UV 从 Normal + ORM GPU atlas 采样，用可见地面或墙面切线基 TBN 转换为**世界空间法线**，编码 RGB=world normal，A=roughness。不是 Minecraft Iris/Photon 的多 Pass 直接移植。
- 只提交本地 snapshot 已可见且非 exterior_only 的普通实墙与地面。地面匹配 1m 顶面，墙体匹配 0.98m 方盒四面与每 1m 高度的纹理行裁剪。复杂家具、斜坡、楼板/天花、玻璃、汽车等仍未纳入此 Pass。
- src/fp3d_materials.h/.cpp 新增只读 lookup_auxiliary_uv(id)，保持色彩图集相同的槽索引、2.5px Padding 及 SDL_GL 纹理比率；不绑定 SDL 纹理。
- src/fp3d_compositor.cpp：仅在同时存在 CDDA_FP3D_CAPTURE_FRAME=1、CDDA_FP3D_QA_MATERIAL_GPU=1、CDDA_FP3D_QA_MATERIAL_SURFACE=1 时进入 QA Surface Pass。另设 CDDA_FP3D_QA_SURFACE_PREVIEW=1 时，在测试画面右上角显示表面属性结果；普通游玩模式不会额外创建 FBO。
- FBO/Program/纹理绑定、OpenGL 矩阵、Pack 行距以及生命周期有显式管理；尺寸/深度重建重新关联主场景 depth attachment；上下文有效时释放本模块 GPU 对象。
- 使用 fp3d_camera::frustum 按视锥裁掉不可见几何，保留原始最大距离及多层限制，不扩展游戏底层可见性。

## 2. 真正的 Windows 实机回归

| 检查 | 实测结果 | 备注 |
|---|---|---|
| ENABLE_FP3D=ON CMake/Ninja 构建与链接 | PASS | 外层 qa/material-p5b-surface-build.log |
| 独立表面 FBO + GLSL + normal/ORM + 原场景深度 | PASS | 实机 sidecar surface_valid=1，近远太阳阴影纹理非零 |
| GL 实际提交地面数 | 833 | 视锥裁剪前为 4,424，并不等于世界总格子数 |
| GL 实际提交墙面数 | 60 | 视锥裁剪前为 92 |
| GL 表面属性 FBO 有效像素 | 2,442,992 | 与旧未裁剪版的覆盖完全一致，仅对该测试世界成立 |
| 地面向上法线像素 | 12,879 | 直接从未 tonemap GPU 法线图 R/G/B 读出，G 接近 255 |
| 竖直墙面法线像素 | 2,430,113 | G 接近 128 且水平 X/Z 分量占主导，近墙画面以墙体为主 |
| Surface Pass 不在主画面预览时 | Photon 基线 RGB 差 (0.0033,0.0008,0.0016) | 两次进程捕获，不能当作逐像素数学相等 |
| Surface Pass 调试预览开启时 | RGB 差 (3.1385,3.359,7.3295) | 右上角真实采样输出可见 |
| P5-B 源码约束检查 | 17/17 PASS | 外层 qa/verify_material_surface_contract.py |

测试脚本：外层 qa/verify_material_surface_p5b_runtime.py。测试二进制：外层 runtime/modern-3d-shaderpacks/cataclysm-tiles-p5b-surface-qa.exe。图片：外层 qa/material-p5b-surface-baseline.png、material-p5b-surface-surface.png、material-p5b-surface-preview.png。完整输出：外层 qa/material-p5b-surface-runtime.log。使用已有 ShaderDaylightLab 隔离世界，测试后 ShaderPack 选项均在 finally 恢复。

## 3. 性能与安全边界（必须保留）

**这版不能直接默认给玩家启用！** 当前调试模式除 GPU Surface Pass 外还执行整张 FBO glReadPixels 和 CPU 法线方向统计。这是带同步等待的 QA 检查路径，实测两次约 15.357ms 和 68.2492ms，**不代表纯 GPU Pass 耗时**，但足以证明逐帧读回不可用于常规游戏。
- 正常游戏不创建这些辅助 GPU 材质/Surface FBO，也不会增加逐帧 CPU 读回；目前不会导致玩家光影色调改变。
- 资源不支持或 GLSL/FBO 编译失败应直接禁用可选通道，保留既有 v18 墙面阴影、v10b 双级阴影、v6b 湿路 SSR。
- 隔离测试静帧不能证明跨图块包热切换、车辆运动、所有楼层、所有天气与真实 1080p60 均通过。
- 截至本轮，GPU atlas 的 Normal/ORM 能被正确采样、世界空间法线方向可区别，但没有参加 Photon 正式场景的 BRDF / PBR 阳光着色。

## 4. 下一工单 P5-C（尚未实施）

1. 生产路径禁止 glReadPixels/CPU 循环，只做纯 GPU Surface Pass；独立 GPU Timer Query + 真实完整帧耗验证。
2. 在已覆盖的普通墙/地面实现可完全关闭的法线/PBR 光照原型。加入真实太阳方向、roughness/metallic、受限 GGX/Schlick、线性 HDR/gamma，谨慎避免先前灰蒙蒙的画面、阴影黑三角回归。
3. 定义对游戏默认 Off/Low/Medium/High/Ultra 的资源预算与回滚开关，不支持 GL2.1 功能时沿用旧绘制路径。
4. 缩减每帧额外几何重绘成本，优先复用已建立的场景几何索引/显示列表；对多楼层、门窗、车辆和透明 Cutout 逐步增加覆盖，避免额外感知隐藏游戏对象。
5. 实机 A/B：HDR 色调、早晨/中午/夜间、室内外、阴影 On/Off、两种图块包、5 层建筑、雨天反光、整游戏 FPS 和显存。

## 5. 版本、资源及提交说明

只修改本地 CDDA-0I-FP3D 开发源码与外层 QA，未替换原游戏 EXE/启动器、未更改玩家存档、未上传 GitHub 或创建 Release；其他协作中的车辆和现代 UI 文件未被我覆盖。本工单生成的 QA EXE、截图、日志仅在隔离测试目录。
