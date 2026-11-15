# CDDA 3D — Photon P5-C 实验性法线/PBR 光照（可运行开发版）

> 实施日期：2026-11-09，当地时间。
> 状态：**本地源码已编译、独立游戏实景通过，实验功能可由 F6 主动启用，默认关闭**。
> 前置：FP3D_PHOTON_P5_GPU_ATLAS_EXECUTION_20261108.md、FP3D_PHOTON_P5_SURFACE_PASS_EXECUTION_20261109.md。
> 重要边界：这是 CDDA 中的有限法线/粗糙度/金属度光照原型，**不是原版 Photon/Iris 多阶段或完整 labPBR 兼容**。

## 1. 实际完成的功能

- 新建 src/fp3d_pbr_lighting.h/.cpp，兼容 GLSL 1.20 与当前 OpenGL2 兼容上下文；使用**独立的 HDR RGBA16F FBO（不支持时 RGBA8 回退）**读取原场景颜色、真实深度和 GPU Surface FBO。
- 先按当前图块包生成低强度切线空间法线、roughness 和 metallic，并将两张 2048² 辅助 atlas 的图块槽对应到现有颜色 UV；后续独立 Surface Pass 使用几何切线基变换成**世界空间法线**。
- Surface FBO RGBA8 的 RGB 存世界空间法线，Alpha 用高低各 4 bit 暂存粗糙度和金属度（精度有限，未来要替换为更合适的格式/独立缓冲）；未绘制像素保持 Alpha=0。只覆盖原游戏确实可见的普通墙体、地面，不从隐藏地图/NPC 获取视觉信息。
- PBR 合成 Shader 使用原版太阳世界方向、场景深度重建相机视线，计算法线微表面受光偏差，以及受限 GGX 法线分布、Schlick Fresnel、粗糙度与金属度高光。由于原始场景已在固定管线受过光照，当前**采用小幅额外微表面贡献**避免双重直射导致以前的灰雾、过曝及墙面黑三角。
- **保持原 Photon/MC 视觉桥在后**：PBR 生成的新世界 HDR 纹理继续交给既有 fp3d_shaderpacks::apply()，因此原 Photon 色调、后期、双级阴影和 SSR 仍使用原实现；出错立即返回原 color FBO。
- 修改 src/fp3d_shaderpacks.cpp/.h 与 src/fp3d_input.cpp：F6 光影界面新增「实验性 PBR 法线与材质高光（Ultra）」复选框，默认关闭；新增「PBR 细节强度 (%)」0–200 滑块，默认 100%。分别保存至当前 shaderpacks/.pbr、.pbr_strength；原版 2D/非 Ultra/关闭光影时不运行 PBR。
- 修改 src/fp3d_compositor.cpp：PBR 独立受 Ultra、用户选项、支持原生光照的光影类型与深度资源检查约束；额外辅助纹理在禁用时释放，失败退回之前的世界颜色；运行阶段跳过整帧 glReadPixels 和 glGetTexImage。QA 的额外读回和双遍计时只允许显式截图环境变量启用。
- 修改 src/fp3d_materials.cpp/.h：PBR 普通游玩下每帧最多生成**两个实际可见**的墙/地面 64×64 辅助图，而不是从随机 atlas 顺序预读；不强制资源包附带法线贴图，跨 tileset revision 与季节变化按已有代码重建。QA 的旧 32 槽顺序验证仍保留。
- 修改 src/fp3d_surface_pass.cpp/.h：生产调用以 readback_stats=false 生成纯 GPU Surface FBO，保留 QA readback_stats=true 的方向和覆盖统计；修复程序重复查询 OpenGL 函数地址等额外开销。

## 2. 验证证据（均已实际运行）

| 检查 | 结果 | 范围 |
|---|---|---|
| Windows ENABLE_FP3D=ON 完整 CMake/Ninja 构建 | PASS，BUILD_EXIT=0 | qa/material-p5c-pbr-build.log |
| 独立 C++ 自动法线/材质分类测试 | PASS，CXX_UNIT_EXIT=0 | qa/fp3d_material_aux_test.cpp |
| P5-C 源码安全约束 | 25/25 PASS | qa/verify_material_p5c_contract.py |
| P5-A 原 CPU 及资源安全回归 | 10/10 PASS | qa/verify_material_p5a_integration.py |
| GPU Atlas/Surface Pass 原有检查 | 13/13 + 17/17 PASS | qa/verify_material_gpu_p5b_contract.py、verify_material_surface_contract.py |
| Photon v1.4 Ultra PBR Off/On | PASS，pbr_applied 0→1 | qa/verify_material_p5c_runtime.py：真实 3D 实景与 Surface FBO，双级 shadow 两图均非 0 |
| 关闭光影包或切 Medium | PASS：PBR 纹理与 Surface FBO 都不分配 | 同上，质量条件与设置选项互不误触 |
| PBR 普通渲染不进行整帧纹素读回 | PASS：surface_readback=0, material_aux_verified=0 | 同上；QA 的**最终测试截图**仍需要 glReadPixels，但不是 PBR 内部逐帧读回 |
| F6 强度调至 180% 的墙面测试 | PASS：RGB 绝对均差约 (0.0297,0.0123,0.0176) | qa/material-p5c-off.png、material-p5c-on.png、material-p5c-delta.png；效果较轻微，不能宣传为巨大画质变化 |
| 雨天道路/车辆 SSR 回归 | PASS：Off 和 PBR On 的反射 mask 均 619,376 像素 | qa/verify_material_p5c_wet_runtime.py；GL mask_error=0、双级阴影保留，qa/material-p5c-wet_off.png 和 wet_pbr.png |
| Git diff --check | PASS，无空白格式错误 | LF/CRLF 警告不属于编译错误 |

实测雨天道路 PBR On/Off 平均 RGB 差约 (0.1871,0.1649,0.1791)。这仅证明当前测试场景 SSR 的遮罩没有丢失，不代表动态道路/玻璃反射已完成全部验收。

首次 PBR Surface Pass 运行会包含 GLSL 编译、FBO/纹理分配及驱动初始化，成本不稳定。在较早一次同场景「双遍」QA 中：
- Surface 首次 CPU/驱动耗时约 12.28ms、第二次约 1.85ms；
- PBR 合成首次 CPU/驱动提交约 10.66ms、第二次约 0.044ms；
- 这些是**CPU 端计时**，不是独立 GPU Timestamp，也不是游戏 FPS；本次另一个首次启动曾记录 PBR submit 41.77ms。不可据此保证 1080p60 或稳定帧时间。
- 全局性能评估必须加入 GPU timestamp/每帧各 Pass 的 GPU ms、CPU 主循环帧耗、1% low、整地图/多楼层/大量车辆的显存及稳定性实测。

所有 QA 在 runtime/modern-3d-shaderpacks 独立测试世界 ShaderDaylightLab 运行。测试脚本在 finally 中按原字节恢复选择项、质量、阴影、雨天反射、体积阳光及新 .pbr/.pbr_strength。没有覆盖玩家原存档。

## 3. 可双击的隔离实验版

- 专用测试程序：C:\Users\StickySlime\Documents\Codex\Projects\cdda3dproject\runtime\modern-3d-shaderpacks\cataclysm-tiles-p5c-lighting-qa.exe。
- 新建独立启动器：C:\Users\StickySlime\Documents\Codex\Projects\cdda3dproject\启动PBR实验版.cmd。
- 该启动器使用源码树的 game data，以及 runtime/modern-3d-shaderpacks/user 作为**独立测试配置与存档目录**；不会覆盖原主游戏启动器或真实用户存档。
- 进入测试游戏后，F6 → 光影包选 Photon v1.4（或支持内置原生光照的包）→ 质量「极高」→ 勾选「实验性 PBR 法线与材质高光（Ultra）」→ 调节「PBR 细节强度 (%)」。如画面/帧数不稳定，立即取消勾选或切回中/高画质。
- 新增代码在本地 git 工作树但**未 commit、未 push、未创建 Release**；主运行版 runtime/0i1/cataclysm-tiles-models-latest.exe 没有被替换。

## 4. 尚未完成与下轮建议

1. **P5-C 画质**：丰富当前普通墙/地面之外的面（天花、楼板、门窗、斜坡、车辆、家具），提供更可信的切线 UV；现有规则自动法线不等于艺术家手绘 PBR/纹理高度源。
2. **真实 PBR**：独立线性光照材质模型、全精度 roughness/metallic、支持反射环境采样/玻璃水面材质，与固定管线原已受光照的结果正确融合，而不是现在的小幅 GGX 补偿。
3. **性能预算**：删除正式场景额外几何重复提交瓶颈、使用 GPU timer query 严谨测量、根据帧时间逐级按需/减采样，确保 Ultra 的增强不拖累 Off/Medium。
4. **图形质量回归**：至少两套贴图包各 20 次切换、全天候/低角太阳/复杂五层楼/多车/室内走廊/高速转头，确认 v18 暗三角、v6b 雨地接缝不回归。
5. **P4 反射优化**：SSR 深度稳定/屏幕外降级，时域反射需等前后帧相机与动态物体数据可用再决定是否开启。

> **当前可对外称为「P5-C 实验性 PBR 法线/粗糙度/金属度受光原型实现并通过局部 GPU QA」，不能称为「完整 Photon PBR 或光影工程全部完成」。**
