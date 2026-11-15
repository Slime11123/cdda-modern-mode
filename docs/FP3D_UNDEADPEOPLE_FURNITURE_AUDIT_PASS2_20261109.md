# 第二轮 · 不死人家具模型全量错配排查（2026-11-09）

## 实际执行的工作

针对用户要求的“继续把剩余家具全部再扫一遍，按预览自动排查同类错误”，重新核对所有 308 个原始非植被待分组 ID 的语义归属以及已有几何家族，并查看了含图片的高风险原始图块。

- 初始：295 个新增实体模型映射 / 75 种实际使用家族；其余包含 f_midden 及非实体。
- 第二轮结果：**292 个新增实体模型映射 / 141 种实际使用家族**。
- **16 个 ID 不生成实体几何**：包含 12 个气体/空物件或特效 ID 和 f_midden、f_alien_anemone、f_sea_anemone、f_sea_sponge 四类有机物（交叉贴原图）。
- 共有 **308** 个候选 ID 已重新走过 ID→家族分类检查。
- **162** 个高风险来源被选入 PNG 接触图集，能从本地 source 路径直接取出图片的 **135** 个；另外 **27** 个不能通过同名路径直接取出（部分是复合图集/状态图；不以此认定该图块不存在）。
- 代表性 **48** 个非植被实体 ID 分成四页对照预览，覆盖工业、医疗、娱乐场所、农村、地表、风力、水力、墓地、原料堆等类型。
- 若模型形状相同只因状态/损坏改变而共用几何，保留共用，不为每个 ID 制造独立结构。

## 修复的实际类别

1. 工业电气：f_air_conditioner / f_air_filter 从压缩机分离；f_electrical_conduit 从天线变成电气接线设施，f_ground_cable 变为地面电缆；f_still 的蒸馏罐和长颈管。
2. 娱乐、锻炼：f_roulette_table 与街机区分为圆桌、f_pinball_machine 独立倾斜台、f_ball_mach 小型发球设施；f_exercise 的举重器与 f_ergometer 的健身器分离。
3. 生活摆设：f_birdbath 鸟浴盆、f_winter_wreath 空心圆花环、f_mannequin 人形模型、墓碑/纪念碑和石块分开建模；f_gazing_ball 独立真实球面（若没有同名源 PNG 则为结构参考）。
4. 农村和户外：木桶横放、搅乳桶、风箱、车间风标、晾衣架和电视天线分类；沙堡、水槽和建筑碎料不再用托盘或桌子模型。
5. 特殊设施：弧炉从矩形锻炉分开；火圈、火盆、家用熔炉分开；保险柜开启/关闭形态和立式枪柜分开；石油/柴油横置储罐和站立罐分开。
6. 原料堆：泥沙/碎石等使用不规则椭球堆；砖堆是分层砖块；木头/钢管/钢筋/木棍横向圆柱；木板是平行条板，不再全用三层通用方块。
7. 异界/海洋生物：蛋簇、异界果荚改真实椭球组合；f_alien_anemone、f_sea_anemone、f_sea_sponge 不再是实心柱体，保留原始植被式交叉贴图。
8. 几何引擎增加椭球（分带网格 + 面法线），用于球体、蛋簇、自然石块、原料丘。原有圆柱、空心环和旋转叶片支持仍保留。

## 变更文件

- `qa/second_pass_fix_semantics.py`、`qa/second_pass_geometry.py`、`qa/second_pass_geometry_addendum.py`、`qa/second_pass_sphere_refinement.py`：生成器分类+新几何的可复现修改。
- `qa/second_pass_integrate_and_test.py`、`qa/second_pass_render_sphere.py`：C++ 结构接入和纹理参数升级。
- `qa/generate_undead_furniture_catalog.py`、`src/fp3d_furniture_catalog.inc`：最终映射和几何源数据。
- `src/fp3d_furniture.h`、`src/fp3d_furniture.cpp`、`src/fp3d_scene.cpp`、`src/fp3d_materials.cpp`、`src/fp3d_snapshot.cpp`：几何、UV、渲染、原生植被类处理。
- `qa/second_pass_source_contact_sheets.py`：五页高风险原始图块审阅图。
- `qa/render_second_pass_review.py`：四页新模型对应原图块预览，`qa/undead_furniture_second_pass_review.html` 浏览入口。
- `qa/undead_furniture_all_previews/index.html`：完整 25 页图集。
- 项目根目录 `查看第二轮家具修复.cmd`：双击查看新版四页结果。

## 复核标准与剩余限制

- 修的是**真实运行时 C++ 模型**和源贴图拆面规则，而不只是离线图形。
- 编译需以本地 Ninja `cataclysm-tiles` 完整成功和启动器 SHA256 部署为准。
- 图集是**由真实 C++ 几何原始数据生成的离线预览**，不是运行中的游戏截图；摄像机、阴影、动态光照、透明度仍需实机查验。
- 由于复合材质图块、缺失独立同名 PNG 等原因，依然有模型需要在实机逐个检查最终材质与精细度，不能把程序映射覆盖率当作 100% 美术合格率。
- 保留原项目全部正在开发的其他文件改动；没有提交 GitHub，也没有修改源贴图包。


## 最终编译与运行版本部署（已验证）

- Ninja 完整构建 `cataclysm-tiles`：**195/195 目标，退出码 0**，链接成功。
- 先前的启动器二次验证遇到 `.ninja_deps` 文件提前结束警告，重启大量不必要的编译任务；这次重复构建已经人工终止。它没有改变已经成功生成的游戏 EXE。
- 使用成功构建的 `build/0i1/src/cataclysm-tiles.exe` 直接部署至日常版 `runtime/0i1/cataclysm-tiles-models-latest.exe`。
- 运行版与构建版 SHA256 一致，均为 `73FC23FFB5EC6412723110108943531258D0E358E5C092EEBBD3A2565724A695`。
- 部署程序大小 `499376358` 字节；已验证部署成功，但本次没有实际打开游戏逐物体验收，所以不把离线模型预览当作实机截图。
- `qa/second_pass_semantic_model_checks.py`：308/308 ID、292 实体映射、16 例外、141 几何家族、55 条重点检查均 PASS。
- `qa/check_furniture_shape_corrections.py`：23 个上一轮问题 ID 与 945 个分件静态属性校验 PASS。
