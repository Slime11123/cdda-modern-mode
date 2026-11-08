# CDDA 3D Photon Ultra v18：近墙阴影三角形根因修复

日期：2026-11-08

## 问题与实际根因

用户连续反馈：开启 Ultra 后，室内及室外灰色墙面出现一大片异常黑色斜三角；走近墙、旋转视角时斑块变化。v12（合并墙体深度遮挡盒）和 v13（偏移接收点、轴法线校正）均未解决实际症状。

最终使用 `ShaderDaylightLab` 原生 3D 世界的反面近墙 QA 摄像机复现：启用 Photon v1.4 + Ultra 后，原始 v14 出现横跨墙下半面的黑块；仅关闭太阳阴影即消失。该 A/B 排除了材质贴图造成黑块的解释。

**根因是 `src/fp3d_shaderpacks.cpp` 后处理 GLSL 中的屏幕空间法线恢复阈值。**

旧实现：

```glsl
vec3 receiver_normal=cross(dFdx(world),dFdy(world));
float normal_length=length(receiver_normal);
if(normal_length>0.00001) receiver_normal/=normal_length;
else receiver_normal=vec3(0.0,1.0,0.0);
```

`dFdx/dFdy` 是米/像素。高分辨率（QA 2520×1392）与近墙（1–2 米）时，二者叉积长度可以约为 10⁻⁶，小于旧固定阈值 10⁻⁵。**合法的竖直墙面法线因而被错误替换成了“向上”**。太阳遮挡图把整片墙当成受日照地面，错误地叠加深色三角阴影；随着视角和距离变化，误判范围也变化。

## 最终源代码解决方案

仅保留在 `src/fp3d_shaderpacks.cpp` 的实质功能修复：

1. 先分别归一化 `dFdx(world)` 与 `dFdy(world)`，再计算叉积并归一化。将有效性判断改为两切向量之间的夹角（`length(cross(unitDx,unitDy))>0.05`），不再以会随屏幕像素密度变化的面积量判定法线。
2. 对真正退化的局部深度采样（例如面片接缝、重叠/非连续片段）保留零法线，避免制造任何虚假的直射光接收法线；不再默认使用 `vec3(0,1,0)`。
3. **没有关闭所有墙体受影，没有禁用墙体投影，没有改阴影强度或 Photon 配色。** 近远双级太阳阴影、车辆/道路投影正常保留。

在排查阶段实验过墙体占用贴图（v14）、强制垂直面禁用阴影（v15）、法线矢量伪色 QA（v16）。v17 已确认**仅正确法线即可消除黑块**。v18 删除实验的 GPU 墙体纹理、面片抑制和调试 GLSL，保留最小且具有物理意义的修复。原来的独立墙体遮挡/级联阴影等旧有功能不受影响。

`src/fp3d_compositor.cpp` 保留严格限制在 `CDDA_FP3D_CAPTURE_FRAME` 下的 QA 摄像机旋转及距离调节，以便重测；正常玩家视角不受影响。根目录 `Launch-Current-3D-Models.ps1` 继续使用此前已修复的 CMake 构建 SHA256 + 部署 SHA256 双重校验，避免每次启动不必要地重打包 EXE。

## A/B 验收

在相同世界、相同视角、相同时间/光影偏好、相同 GPU 设置下：

- 分辨率：2520×1392，Photon v1.4、Ultra，近/远阴影深度贴图均存在且 GPU 读回没有错误。
- 最能反映用户反馈的靠近墙面视角，前后截图保存：
  - 修复前：`qa/v14_opposite_on.png`
  - 修复后：`qa/v18_opposite_on.png`
  - 关闭阴影参考：`qa/v18_opposite_off.png`
  - 并排图片：`qa/wall-normal-v18-before-after.png`
- 面向墙体的固定 ROI (201,334)–(1915,1183) 像素，启用与关闭阴影的平均绝对 RGB 差异：修复前 **(47.7317,46.8938,45.7476)**；修复后 **(1.7238,1.6141,1.3218)**。这表明异常暗化已经从墙体画面中被去除。
- 修复后 Photon 的正常方向阴影仍然生成：车辆附近道路 ROI 阴影开/关 RGB 平均绝对差为 **(4.8145,4.5428,4.2262)**；四辆车正常加载，两个级联 FBO 正常。
- 回归脚本：`qa/_qa_wall_opposite_v18.py`、`qa/_qa_wall_normal_v18_vehicle_shadows.py`（均退出码 0，已恢复所有用于 QA 的光影偏好文件）。

## 运行、文件与边界

- 可用代码已通过 Windows UCRT64 CMake 增量编译和最终链接，`BUILD_EXIT=0`。
- 正式运行文件：`runtime/0i1/cataclysm-tiles-models-latest.exe`，由根目录 `启动3D操作修复验证.cmd` 使用当前源码。
- v18 隔离 QA 可执行文件：`runtime/modern-3d-shaderpacks/cataclysm-tiles-wall-normal-v18.exe`。
- QA 曾在 `runtime/wall-shadow-scene-isolated-v14` 建立最近世界的独立**副本**用于测试，原始存档未写入；修复不需要这个副本。
- 本次没有 GitHub 提交/上传，未修改地图玩法或原版 2D。
- 此处复现的是同类型的真实原生墙面问题，不是用户截图内完全相同的游戏位置；最终仍以用户在原位置重新打开当前 EXE 后的截图为准。
