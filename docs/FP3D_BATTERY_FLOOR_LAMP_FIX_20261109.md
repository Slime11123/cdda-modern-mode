# CDDA 3D - 不死人蓄电池与落地式台灯模型修复（2026-11-09）

## 原图核对

原图位于项目根目录的 `assets/undeadpeople-source/!dda/pngs_normal_furniture_32x32`：

- `appliances/f_battery/f_battery.png`：厚钢框、顶部和底部银灰连接横梁，中央凹陷式暗面板以及绿色与棕色圆指示器。不是普通无面板箱子。
- `decorative/f_floor_lamp/f_floor_lamp.png`：黑色圆形底座、细长灯杆、上窄下宽的白灰色锥台灯罩。
- `decorative/f_floor_lamp_on/f_floor_lamp_on.png`：同一物理形状，色彩偏亮黄、背后有伪 2D 光晕。建模时不可把光晕挤出到三维灯罩。

## 源码实现

- `src/fp3d_furniture.h`：新增 `kind::battery_unit`。
- `src/fp3d_furniture.cpp`：12 部件电池机箱和 6 部件落地灯；对 `f_battery`、`f_floor_lamp_on` 提供直接 ID 匹配，同时保留 `f_floor_lamp` 在原版物体定义下的映射。
- `src/fp3d_scene.cpp`：为灯罩的 `round_furniture_part` 增加独立的顶端缩小比例，实现真正的截锥侧壁和斜向法线；其他圆形家具仍使用原先的 1.0 圆柱比例。
- `src/fp3d_materials.cpp`：为蓄电池机箱、电极横梁、深色凹面板、绿色和琥珀色指示器分配分件 UV；台灯开启/关闭分别采样原本对应图块中灯罩本体，不把灯光光晕当成几何贴图。
- `qa/render_battery_floor_lamp_fixed_preview.py`：根据 C++ 原生静态分件提取几何并以离线等轴视角预览。
- `qa/battery_floor_lamp_fixed_preview.png`：原图与新建分件效果对照。
- 根目录 `查看蓄电池台灯模型.cmd`：双击查看此次图形对照。

注意：`f_battery` 是不死人贴图包中的有效图块 ID，但在当前基线 CDDA 0.I 的家具 JSON 中未发现同名家具定义；因此代码允许该 ID 出现在模组/设备映射时直接采用新的模型。亮灯贴图的形状支持不代表本次实现了随电网供电自动切换动态灯光。

## 校验记录

- 相关三个 C++ 源文件已通过编译，完整二进制编译与运行版部署另行验证。
- 此次仅修家具模型与分面材质，不修改原始不死人贴图包。
- 图片为离线 C++ 几何预览而非游戏实机截图。


## 最终状态记录（20:14 本地构建）

- 相关 `fp3d_furniture.cpp`、`fp3d_scene.cpp`、`fp3d_materials.cpp` 已分别编译通过；`qa/check_battery_floor_lamp_models_20261109.py` 检查 PASS。
- 构建目录的 `build/0i1/src/cataclysm-tiles.exe` 修改时间为本地 20:14:37，约 499MB；常用运行版 `runtime/0i1/cataclysm-tiles-models-latest.exe` 为本地 20:14:39，约 48MB（精简版）。
- 两个可执行文件经扫描都包含本次模型 ID `f_battery`、`f_floor_lamp_on`，但精简与非精简版本体积、SHA256 本来不同；本轮没有使用裸文件哈希一致来证明二者字节相同。
- 并行的其他 Ninja 构建仍处于活动状态；本轮不强制覆盖其正在部署的运行文件。**尚未实机打开检查**游戏画面和供电状态。
- 注意 `f_battery` 是不死人原始素材的图块 ID；当前 0.I 主游戏家具 JSON 无同名条目，是否出现在实机由所安装的 Mod、设备及图块映射决定。
