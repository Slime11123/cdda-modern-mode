# 不死人家具模型针对截图修复记录（2026-11-09）

## 反馈问题和处理

用户提供七张旧预览截图，指出一批物件被错误复用模型，偏离不死人原图。
根因：先前按关键词把差异很大的非植被物件强行归为 pile / vent / tank / target / windmill / medical_bed 等通用几何。

此次检查与处理了 **23 个图块 ID**，其中 **22 个继续以专用 3D 模型表示，f_midden 使用不死人原始图块的交叉 Billboard**。非植被实体模型覆盖为 295 个 ID，映射到 75 个实际引用的新增几何家族；另有原有 46 种模型。此覆盖仍不是逐项真实游戏美术验收。

### 重做/修正的对象

- `f_bitts`：矮圆系船柱，不再是两根长杆。
- `f_MRI`：空心圆形扫描环、机架与可见检查床，不再用矩形实心机柜代替。
- `f_ash`、`f_leaves_pile`：薄层地面沉积/落叶，不再堆成木托盘。
- `f_hay`：侧卧的草捆圆柱。
- `f_midden`：原图块为藤蔓绿色有机物，按之前要求回退到带原图贴图的交叉 Billboard，**不另建实心模型**。
- `f_rubble`、`f_rubble_landfill`：无规则碎块，取消整齐托盘外形。
- `f_dialysis`、`f_anesthetic`：透析/麻醉设备改为带面板和附件的机器，不再错当床。
- `f_chimney`：分离砖砌烟囱本体与红色双坡屋顶贴图。
- `f_roof_turbine_vent`、`f_vent_pipe`、`f_ventilator`：各自独立的涡轮风帽、细管和医疗呼吸机。
- `f_water_purifier`：矩形净水机柜+前面三个滤筒，不再是单个储水罐。
- `f_archery_target_bale`、`f_archery_target_box`：圆草靶与方箱靶分开建模。
- `f_punching_bag`、`f_target`：悬吊圆筒沙袋与普通标靶分开建模。
- `f_water_mill`、`f_water_mill_active`：水平轴、空心轮圈和辐条构成的竖立水车。
- `f_wind_mill`、`f_wind_mill_active`：旋翼风车，独立于水车；支持绕局部 Z 轴旋转的叶片部件。

### 源码

- `src/fp3d_furniture_catalog.inc` 和 `qa/generate_undead_furniture_catalog.py`：ID 对应关系/几何定义；生成器只在内容真正变化时写回 inc，避免仅查看图集就重复编译。
- `src/fp3d_furniture.h`：几何部件扩展水平圆柱、空心环、斜向部件。
- `src/fp3d_furniture.cpp`：f_midden 显式排除实心模型；保留其他已有家具模型。
- `src/fp3d_scene.cpp`：横放圆柱、空心扫描环与水车环、局部斜向盒体的 OpenGL 几何；不修改原生游戏判定。
- `src/fp3d_materials.cpp`：物理顶面/正面/侧面拆图，烟囱砖身与红色屋顶使用不同的图块区域，草靶端面与侧面分开采样。
- `src/fp3d_snapshot.cpp`：f_midden 按植被式 3D 贴片显示。
- `src/fp3d_light_lab.cpp`：仅添加 `veh_type.h`，修复无关模块的缺失完整类型编译错误；不改动光照功能逻辑。
- `qa/check_furniture_shape_corrections.py`：23 个 ID 的语义映射、几何结构及属性检查。**PASS**。
- `qa/render_furniture_fix_review.py`：仅针对反馈物件生成两张最新截图对照。
- `qa/render_all_undead_furniture_gallery.py`：完整 ID 图集；修复未能显示带动画帧原始图片的问题。

### 本地验收

- 单独相关 C++ 编译通过。
- 完整游戏的 `cataclysm-tiles` 目标重新链接成功，退出码 **0**。
- 根目录 `Launch-Current-3D-Models.ps1 -VerifyOnly` 已部署新编译产物，输出包含：
  - `runtime\0i1\cataclysm-tiles-models-latest.exe`
  - `SHA256=1E6BC853200DBAE263F4A45995F1D3243F87F3BDE924C5055BE3C7D82F54D8F3`（本轮最新版、19:17:51 编译并部署校验成功）
- 已生成 `qa/undead_furniture_fix_review_1.png` 和 `qa/undead_furniture_fix_review_2.png`（**离线几何预览，非实际游戏截图**）。
- `qa/undead_furniture_all_previews/index.html` 为 25 页完整模型图集。
- 根目录 `查看本次修复家具.cmd` 双击可打开反馈物件专用对照图。

仍需用户游戏内对照检查实际贴图、光照、比例、玻璃与遮挡；没有将离线几何图片标为真实游戏截图。

没有提交、推送或发布 GitHub。
