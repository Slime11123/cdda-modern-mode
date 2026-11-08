# CDDA FP3D — 车辆附近灰色矩形与光影修复（v9f）

日期：2026-11-08。用户截图：图 1 红车旁有一块灰色巨大矩形，图 2 曾经道路和路灯投影较自然。

## 实际根因：不是太阳阴影贴图

之前把图 1 当作“灰色太阳阴影”，需要纠正。现在有独立、实机可重复的证据：同一原生 3D 测试世界里，把太阳阴影贴图和体积太阳光都完全关闭，这块硬边灰色地面区域仍然存在（QA: qa/vehicle-shadow-near-v9d-car3-no_shadow.png）。故这部分不是太阳阴影，而是近/远景路面混合问题；截图中正常的车辆阴影属于另一种视觉现象。

`src/fp3d_snapshot.cpp` 原版把远景路/高速 overmap 归入合法的 `t_pavement`。该 CDDA 基线没有通用 `t_road` 地形/贴图 ID；临时尝试用 `t_road` 导致紫白缺失贴图，已撤销。远景贴图需要继续采用 CDDA 的合法 terrain ID。

`src/fp3d_scene.cpp` 原版把每个约 12×12 米的远景路面 `floor()` 网格整体缩放，却只使用一次 atlas UV，致使一小块铺装贴图被拉伸成一个灰色大区域。新版保留来源 `t_pavement`，对距离原生玩家附近、尺寸受控的道路 LOD patch，按实际每米 1 块的 UV 尺度细分绘制；较远街区仍用原有的单网格简化渲染。近景/远景视觉切换处不再平铺一个大型灰色贴图。此处不会访问、改变未加载地图，只用已知 overmap topology 视觉 patch。

## 相关光照修正

- `src/fp3d_shaderpacks.cpp` 删除原先太阳阴影区域直接叠加固定蓝灰色间接光的算法：这会使深色沥青阴影被错误地增亮。保留日间环境光、3×3 PCF 太阳贴图及车辆/家具实际几何。
- `src/fp3d_mcshader.cpp` 限制屏幕近处不透明物体上的体积太阳光累加，防止在车身/路面形成不合理的整体泛亮；远处大气光和天空仍可展示光束。
- 已撤回本轮对原来固定 GL_FOG 和天气雾强度的额外更改，不把所有天气都变淡；天空云层、原有光影调色尽量保持。
- 下一阶段 PBR 基础：独立反射材料 RGB 通道分别保留实际湿润反射率、材质粗糙度和微法线强度，水/沥青/混凝土分开计算，基于世界坐标的微法线和 Fresnel 近似减轻反射“贴屏”感。尚非完整的 Minecraft Iris/Photon PBR pipeline、CSM 或时域 SSR。

## 确认测试

- UCRT64 Windows ENABLE_FP3D 构建、完整链接返回 0；OpenGL Photon GLSL 与场景深度 SSR、太阳体积光、原有 60 次目标重建等测试成功。
- `qa/_qa_vehicle_car3_shadow_isolation_v9d.py` 用同一游戏场景/同一 QA 镜头分别测试阴影 + 体积光、只保留阴影、完全禁用阴影：矩形灰色路面始终存在，验证问题不是阴影开关造成。
- 不兼容 `t_road` 的错误尝试有截图 `qa/vehicle-road-v9e-car3-no_shadow.png`（紫白色缺纹理），最终已还原为有效的 `t_pavement`。
- 正确修复 `qa/_qa_v9f_road_ab.py` 在同一镜头关闭太阳阴影的条件下比较旧 v9d 与新 v9f，原来车辆右边平整的灰色块变成连续的重复路面纹理，截图 `qa/vehicle-road-v9f-car3-no_shadow.png`。源代码正常场景同样使用该路径；游戏存档没有修改。
- 其他 QA 图片 `qa/vehicle-shadow-v9d-car0.png` … `car3.png`，每辆车的旁视镜头均来自真实 CDDA 场景，非导入/合成截图。

## 尚待完成

真正两级以上级联太阳阴影、带真正 tileset normal/roughness 输入的完整物体 PBR、时域历史缓冲和相机运动重投影、红车原截图相同机位实景及多层建筑/大型车辆全方位视觉验收尚待后续开发。本轮不会宣称这些已经完成。

## 使用

根目录 `启动3D操作修复验证.cmd` 实际会调用 `Launch-Current-3D-Models.ps1`，自动编译当前源码并更新启动预览 EXE。这是当前真实启动路径，而不是旧的固定 v6/v7 EXE。

另保留隔离测试 EXE `runtime/modern-3d-shaderpacks/cataclysm-tiles-photon-road-tiles-v9f.exe`。所有 QA 临时场景和光影设置测试结束后还原；旧可执行文件和玩家存档保留。本轮不提交或上传 GitHub。
