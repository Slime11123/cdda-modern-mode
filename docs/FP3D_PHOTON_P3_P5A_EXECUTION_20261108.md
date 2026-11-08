# Photon P3 阴影回归与 P5-A 自动材质法线：首轮实施记录

> 日期：2026-11-08。关联：FP3D_PHOTON_NEXT_PHASE_PLAN_20261108.md。
> **实际完成：工单 01 的部分回归、工单 02 的第一版材质分类、工单 03 的 CPU 法线/ORM 生成和按需缓存。**
> **未完成：法线/ORM GPU atlas、逐表面 UV/切线 Pass、PBR 着色、时域反射和 P6 全游戏 FPS 测试。**
> 此处的 Photon 指 CDDA 视觉适配，而非原版 Iris/Photon 多 Pass 运行时。

## 1. 已做的源码改动

- src/fp3d_material_aux.h/.cpp（新文件）：纯 C++ 材质分类与纹理微法线生成；为道路、砖石、木材、金属、玻璃、水、土、植被、车辆、未知图块赋予保守的 roughness/metallic/normal_strength 默认值。
- fp3d_material_aux::generate：当前 RGBA8 图块像素转为切线空间弱法线与 ORM 数据（AO、roughness、metallic、原 alpha）。透明轮廓不生成错误高度悬崖，平色为近中性法线，像素风格保守。
- src/fp3d_materials.h/.cpp：新增 auxiliary(id) 按需接口。仅在首次请求时从**当前实际 tileset atlas**中读取一个 64×64 槽并缓存；不是每帧或在 prepare() 中执行。图块包重载、季节变化、release() 都会清理。
- 原有 color atlas、着色器及画面输出路径未变。当前阶段**画面不会因新增法线发生变化**，因为 GPU Normal/ORM atlas 与真正 PBR Shader Pass 尚未接入。
- 新增 API 只在 SDL Renderer 活跃且未绑定 GL 颜色 atlas 时有效；返回指针在缓存失效后不可继续使用。
- 未修改游戏 AI、回合、存档、感知、原版 2D，未引入 Minecraft labPBR 文件作为图块包强制依赖。

## 2. 本轮实际完成的验证

| 项目 | 本轮结果 | 证据 |
|---|---|---|
| C++ 分类/生成器测试 | PASS | 外层 qa/fp3d_material_aux_test.cpp，UCRT64 g++ C++17，覆盖平色法线、对比边缘、透明、ORM、未知材质和非法尺寸 |
| 材质接入源码结构 | 10/10 PASS | 外层 qa/verify_material_p5a_integration.py，核查缓存重置与数据来源 |
| 车辆网格阴影结构 | 16/16 PASS | 外层 qa/_validate_shadow_vehicle_geometry.py（仅静态结构，不等同车辆画面验收） |
| ENABLE_FP3D=ON 整体构建 | PASS | CMake 配置 + Ninja 13/13 编译链接，外层 qa/material-aux-p5-build.log |
| 新编译 EXE Photon 游戏实景 | PASS | 外层 qa/verify_material_p5a_photon_runtime.py + material-p5a-runtime.log：Photon v1.4 Ultra，4 车/156 部件，两级阴影深度纹理均有效，GL 读回错误 0 |
| 阴影 On/Off 差异 | PASS | 实景平均 RGB 绝对差 (6.292, 4.099, 2.411)，光影未因模块增加而丢失 |
| 近墙 v18 阴影恢复 | PASS | 外层 qa/verify_material_p5a_wall_runtime.py + material-p5a-wall.log：On/Off/旋转三次实景，ROI 差 (1.724, 1.614, 1.322) |
| Git 差异格式 | PASS | git diff --check，无新空白错误；有仓库既有 LF/CRLF 提示 |

实景 QA 图片位于外层 qa/material-p5a-photon-shadow-on.png、material-p5a-photon-shadow-off.png、material-p5a-wall_on.png、material-p5a-wall_off.png、material-p5a-wall_rotated.png。使用现有独立测试存档 ShaderDaylightLab，临时 QA shaderpacks 偏好已逐字节恢复。

**测试限制**：没有验证用户原截图位置的全部情况、完整 30 分钟/车辆动态、全天候 FPS 或将 auxiliary(id) 真实上屏着色。静帧和测试世界无法推断用户全部地图没有阴影瑕疵。

## 3. 构建与回退

- 仅改 CDDA-0I-FP3D 开发源码；新 fp3d_*.cpp 自动随 CMake ON 编译，OFF 不含这些模块；本轮没有完整 OFF 构建验收。
- 编译程序为外层 build/0i1/src/cataclysm-tiles.exe；隔离 QA 程序为 runtime/modern-3d-shaderpacks/cataclysm-tiles-p5a-qa.exe。
- **没有覆盖默认启动器和运行版 EXE**，没有修改玩家原始存档、上传 GitHub、创建 Release 或覆盖其他并行开发者正在修改的 HUD/地图文件。
- 保留既有 v18 墙体、v10b 双级阴影和 v6b 湿路画面路径；后续材质着色接入时必须独立开关、失败退回。

## 4. 下一开发闸门

1. 将 CPU Normal/ORM 逐图块同步到可按需更新的独立 GPU 辅助 atlas，声明 GL context、纹理绑定单元、释放/缓存预算；不要改变已有颜色 atlas bind/unbind。
2. 选择一面墙和一块路面实现有限 Surface Pass：输出正确 UV、切线、几何法线、材质类别和深度；和 fixed-function 路线对照 GPU 时间。
3. 加 Normal/ORM/材质 ID 的开发可视化，检查图块包切换、UV 镜像、透明精灵与 atlas 边缘，而非直接启用全部世界的法线光照。
4. 通过这些闸门才加入可关闭的 PBR 着色；其后继续 SSR 稳定与高低画质预算/性能验证。

**计划状态**：工单 01（静态/有限场景回归已完成，全面测试未完成）、02（初版实现）、03（CPU + 按需缓存完成，GPU 未完成）；工单 04–08 尚未开始。
