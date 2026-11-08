# Photon P5：GPU 法线/ORM 图集与调试采样（第二轮实施）

> 日期：2026-11-08。承接 FP3D_PHOTON_P3_P5A_EXECUTION_20261108.md，属于 FP3D_PHOTON_NEXT_PHASE_PLAN_20261108.md 工单 03 的后续。
> **本轮已完成：CPU 法线/ORM → 独立 OpenGL 图集、单槽实时上传、纹素读回验证、QA 内实际纹理采样调试窗口。**
> **尚未完成：地面/墙体/车辆逐像素表面参数 Pass、法线变换到世界空间、BRDF/PBR 实际照明、普通玩家 UI 功能开关、跨图块包 GPU 真机切换回归。**
> 普通游戏画面仍然走之前的 Photon 视觉桥。此阶段不等于完整 Minecraft ShaderPack/Photon 移植。

## 1. 实际源代码变更

1. 新建 src/fp3d_material_gpu.h/.cpp，提供受当前 OpenGL 上下文保护的状态 sync / current / verify_texels / draw_qa_preview / release。
2. 仅在正式颜色 atlas 的 slot 对齐位置建立两张 **2048×2048 RGBA8**、GL_NEAREST 的 GPU 纹理：normal 与 ORM。每张约 16MiB，两张理论基础显存约 32MiB（不包括驱动额外分配、CPU 像素缓存和其他场景 FBO）。
3. 为未生成的图块预先写入中性法线与保守 ORM 数据，而不是允许 GPU 采样未初始化内存；只更新已在 CPU 缓存中的图块的 64×64 区域（glTexSubImage2D）。
4. 扩展 src/fp3d_materials.cpp/.h：按颜色 atlas 插入顺序记录 slot_keys 与 prefetch cursor；新增有上限的 prefetch_auxiliary(budget) 和辅助条目快照 auxiliary_tiles()。图块包切换、季节变化、release() 时重新建立与颜色槽同步的 CPU 缓存。
5. 扩展 src/fp3d_compositor.cpp：**仅在 CDDA_FP3D_CAPTURE_FRAME=1 且 CDDA_FP3D_QA_MATERIAL_GPU=1 的隔离 QA** 中，在 SDL Renderer 仍可用时预算生成 32 块 CPU 辅助图，SDL_RenderFlush 后在借用的 OpenGL 上下文上传。QA sidecar 报告 normal/orm 纹理号、上传槽数、GPU readback 是否一致；其余游戏路径不创建这些 GPU 纹理。
6. 可再设 CDDA_FP3D_QA_MATERIAL_PREVIEW=1；只在 QA 画面左上角显示 2 块基于**实际 GPU 图集采样**的 normal/ORM 调试小图，不改变 HUD、存档、玩家的默认光影选择。
7. 纹理绑定和 GL_PACK/UNPACK_ALIGNMENT、ROW_LENGTH、SKIP_ROWS、SKIP_PIXELS 状态都会在上传/读回后恢复；第一次 QA 发现 GPU 像素不匹配，明确修复了这些 SDL/OpenGL pack/unpack 行距状态之后重跑获得 PASS。
8. src/fp3d_compositor.cpp 在当前 GL 上下文内清理 GPU 材质纹理；当 material_revision 不同，下一次 sync 先销毁旧图集再生成新图集。GL 不支持时仅禁用可选 GPU 辅助图集，不牵连原 Photon post/shadow Pass。

## 2. 本地真实验收

| 检查 | 结果 | 来源与限制 |
|---|---|---|
| Windows ENABLE_FP3D=ON 整体 CMake/Ninja | PASS | qa/material-p5b-gpu-build.log，新模块编译并链接 cataclysm-tiles.exe |
| P5-A CPU 材质回归 | 10/10 PASS | qa/verify_material_p5a_integration.py |
| P3 车辆几何回归 | 16/16 PASS | qa/_validate_shadow_vehicle_geometry.py；静态结构检查 |
| GPU 图集生命周期与接入检查 | 13/13 PASS | qa/verify_material_gpu_p5b_contract.py；静态检查 |
| Photon v1.4 Ultra 实际场景 GPU 图集 | PASS | qa/verify_material_gpu_p5b_runtime.py：真实 ShaderDaylightLab，上传 32 槽，normal/ORM 两纹理存在，OpenGL glGetTexImage 数据与 CPU 对照成功 |
| 隔离视觉调试采样 | PASS | material_aux_previewed=1，预览 On 与 GPU-only 画面平均绝对 RGB 差 (4.9488, 4.062, 1.5439)，表明小窗口真的在测试画面产生实际变化 |
| 不启用 QA GPU 的 Photon 基线 | PASS | material_aux_uploaded=0, normal_tex=0, orm_tex=0, near/far shadow >0、qa_read_error=0；正常与 GPU-only 平均 RGB 差 (0.0418, 0.0094, 0.0214)，为同场景抓帧不完全同时产生的小差异，不能作为每像素完美一致承诺 |
| shaderpacks 测试偏好 | 已恢复 | runtime/modern-3d-shaderpacks 的 .selected/.quality/.shadows/.wet_ssr/.shadow_volume 在 finally 恢复原字节 |

测试程序独立放在 runtime/modern-3d-shaderpacks/cataclysm-tiles-p5b-gpu-qa.exe；截图位于 qa/material-p5b-with_gpu.png、material-p5b-preview.png、material-p5b-without_gpu.png；详细输出为 qa/material-p5b-gpu-runtime.log。测试启动器与原始玩家存档没有覆盖。

## 3. 保留的限制与 P5-B 后续

- **不能将该 GPU 法线 atlas 视为已经作用于墙壁光照**。现阶段只把数据放入显存并在 QA 中直接预览；正式 world scene 仍为兼容固定管线。
- 仍需建立 visible surface 的真实 UV、切线、几何法线与材质 ID；特别是墙面、天花、车辆、交叉贴图等切线方向与透明遮挡。
- 不应直接向 shaderpacks 默认通道绑定新的纹理单元（会和现有 sunshadow/SSR sampler 冲突）。先定义独立 Surface Pass / 可用纹理单元检测 / Debug View，随后才允许 Ultra 独立开关 PBR。
- 真正的动态角色、车辆材质 PBR、时域 SSR、跨图块包 20 次 GPU 切换、低配置兼容矩阵和完整 GPU 帧耗仍未验收。
- 计划下一步：工单 04 的**单墙+地面表面属性 Pass 原型**（单独 QA FBO / depth shared / UV 和切线对照），随后为可见墙面加入受控法线光照；保持原画风一键回退。

## 4. 发布与安全界限

本轮无 GitHub commit/push、无 Release、未覆盖 runtime/0i1/cataclysm-tiles-models-latest.exe 和正式启动脚本，没有改动 AI/行动回合/原版存档和玩家数据。新增源文件是当前本地工作树的未提交变更；须连同对应头文件保留，未来由独立 agent 合并时检查 CMake GLOB 的重新配置。
