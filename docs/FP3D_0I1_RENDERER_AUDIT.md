# 0.I-1 SDL2 / OpenGL 同窗口技术验证

日期：2026-10-26（更新）。范围：v2.0 的源码审查、P0/P1；不包含地图转换、第一人称行走或战斗适配。

## 版本和环境

- 唯一开发副本：`CDDA-0I-FP3D/`，分支 `feature/fp3d-0i`。
- 初始 `git describe --tags --exact-match`：`0.I-1`。
- 基线 `git rev-parse HEAD`：`7b2efa5cea38e4d4d97dd0e63b28b9148623da59`。
- 检出后初始工作树干净；新增原型前先构建原版，保留独立原版可执行文件。
- 旧方案的 `source/`、`build/stage0/` 和 SDL3 原型属于已停止的 v1.0 实验，**不用于本方案**。
- Windows x64；NVIDIA GeForce RTX 4080 SUPER，驱动 580.88。
- 实际工具链：MSYS2 UCRT64 GCC 16.2.0，CMake 4.4.4，Ninja。
- 实际 SDL2 头文件及运行库均为 2.32.10；实际 renderer 为 `opengl`，GL `4.6.0 NVIDIA 580.88`。
- 已核实 tag 中 `msvc-full-features/Cataclysm-vcpkg-static.sln` 和对应编译说明存在。本次使用已有 MSYS2 工具链；**MSVC/vcpkg 构建、SDK 版本及静态发行打包未验证**。

官方依据：[0.I-1 发布](https://github.com/CleverRaven/Cataclysm-DDA/releases/tag/0.I-1)、[固定 tag 的 Windows 编译说明](https://github.com/CleverRaven/Cataclysm-DDA/blob/0.I-1/doc/c%2B%2B/COMPILING-VS-VCPKG.md)。源码结论均以本地 tag 为准。

## 已确认的真实调用与生命周期

下表行号是 **未修改的 0.I-1 tag** 行号，可用 `git show 0.I-1:<path>` 复核。新增接入点另列于下一节，避免混用修改前后行号。

| 文件 / 函数 / tag 行号 | 实际行为 |
|---|---|
| `src/sdltiles.cpp:235` / `SetupRenderTarget` | 创建 `SDL_TEXTUREACCESS_TARGET` 的 `display_buffer`，绑定为 2D 绘制目标。 |
| `src/sdltiles.cpp:253` / `WinCreate` | 创建单一 SDL2 窗口，按保存的 renderer 选项寻找驱动。 |
| `src/sdltiles.cpp:317` / `SDL_CreateWindow` | 原版窗口归 SDL 所有。 |
| `src/sdltiles.cpp:376` / `SDL_CreateRenderer` | 加速 renderer 带 VSYNC、TARGETTEXTURE；失败或目标纹理失败则进入软件分支。 |
| `src/sdltiles.cpp:394` / 软件分支 | `SDL_RENDERER_SOFTWARE`，之后重新创建 display buffer。 |
| `src/sdltiles.cpp:441` / ImGui client 创建 | 共享原版窗口和 renderer。 |
| `src/sdltiles.cpp:539` / `refresh_display` | 切到窗口目标、清屏、复制 display buffer、唯一一次 Present、再绑定 display buffer。 |
| `src/sdltiles.cpp:567` / `SDL_RenderPresent` | 原型保留它作为唯一提交窗口画面的位置。 |
| `src/sdltiles.cpp:444` / `WinDestroy` | 释放 ImGui、图块、几何、display buffer，最后 renderer 和 window。 |
| `src/cata_imgui.cpp:251,274,275` / client 构造 | `ImGui_ImplSDL2_InitForSDLRenderer` + `ImGui_ImplSDLRenderer2_Init`。 |
| `src/cata_imgui.cpp:575,578` / `end_frame` | `ImGui::Render` 后，使用同一 SDL renderer 绘制 UI。 |
| `src/cata_imgui.cpp:464` / client 析构 | 关闭原版 ImGui 后端；原型不重建该后端。 |
| `src/cata_tiles.cpp:357` / `cata_tiles::load_tileset` | 通过 tileset cache 加载有效图块包。 |
| `src/cata_tiles.cpp:466,491,526` / loader | CPU surface 切片、上传 renderer 纹理、建立 sprite 范围。 |
| `src/cata_tiles.cpp:4467` / `tileset_cache::load_tileset` | 弱引用 cache；需要时重新建立或强制加载。 |

该版本没有独立的 `src/tileset_loader.cpp`。本轮不修改 tileset resolver，也不共享/猜测 atlas 的 GL 句柄。

## 两条合成路线比较

| 项目 | A：SDL2 opengl renderer + 有界 GL pass（本轮 PoC） | B：由自定义 GL 主合成器管理场景和 UI（备选，未实现） |
|---|---|---|
| Context | 借用 renderer 已创建的 compatibility context，不另建。 | 主合成器需要明确拥有 context，并重新接入原版绘制。 |
| Present | `refresh_display` 原来的 `SDL_RenderPresent`；没有额外 Swap。 | 必须将 SDL Present 和自定义 Swap 收敛到单个出口。 |
| 排队 | 自定义 GL 前后 `SDL_RenderFlush`；独立保存/恢复 GL 状态。 | 需要安排 SDL UI flush 和主合成器命令顺序。 |
| 纹理桥接 | 独立 RGBA8 color texture + depth24 renderbuffer + FBO；完全在 GPU 上合成。 | 将原版 UI 适配为 overlay 纹理/绘制列表；原版纹理生命周期需重新研究。 |
| UI | 原版 UI 先经 display buffer 绘到窗口，再叠加透明背景的测试立方体。 | 可精细安排 3D 后的 UI overlay，但侵入明显更大。 |
| Resize | 原版重建 display buffer；固定大小的立方体 FBO重用，按实际输出尺寸调整位置。 | 主合成器须管理所有输出尺寸、UI 缩放和上下文恢复。 |
| 失败回退 | 创建失败沿用原版软件回退；不满足 GL 能力则停用原型，继续原版 2D。 | 需要保留独立、可恢复的原版 renderer，切换代价更高。 |
| 限制 | 只支持经核验的 Windows `opengl` renderer 和兼容 GL。 | 需单独立项，不能据本 PoC 声称已可维护。 |

SDL 对混合底层 API 的规范：[SDL2 SDL_RenderFlush](https://wiki.libsdl.org/SDL2/SDL_RenderFlush)。Flush 不代替 GL 状态保存，也不让 SDL_Renderer 自身变成三维绘图接口。

## 实际实现和边界

- `CMakeLists.txt`：新增默认 OFF 的 `ENABLE_FP3D`；仅允许 Windows Tiles。
- `src/CMakeLists.txt`：关闭时从构建中排除 compositor；开启时只给 `sdltiles.cpp`、`input_context.cpp` 和 compositor 增加定义，并链接系统 OpenGL。
- `src/sdltiles.cpp`：窄接入点为 WinCreate 的 renderer 选择/attach、WinDestroy 的 release、Present 前的 compose、原版 SDL 事件循环中的 F5、空闲时请求表现层刷新。
- `src/input_context.cpp`：仅在启用 feature 时向 compositor 报告原版当前输入上下文；菜单收集输入时暂停测试图形，返回世界自动恢复，避免立方体遮住菜单标签。已构建并实机复验。
- `src/fp3d_compositor.h/.cpp`：全部 GL 资源、状态边界、立方体、鼠标角度、切换及诊断自测。
- `tools/build-fp3d.ps1`：构建并复制实际 DLL 导入依赖；可保留独立原版二进制。
- `tools/run-fp3d.ps1`：独立用户目录、字体/图块准备、明确选择 Original / Probe / SelfTest。

运行期开关 `CDDA_FP3D=1` 才请求 OpenGL 和接管 F5。未设置时不改原版 renderer 选择或快捷键。Probe 启动器是显式启用，直接显示测试立方体；F5 切换 overlay，不重新创建 game、地图或角色。该立方体**不是**真实地图或第一人称游戏。

颜色缓冲、depth24、FBO 各一个；切换重用它们。资源在原版 renderer/context 销毁前释放。Context 改变或 GL 错误时停用原型。若 context 已丢失，不对未知 context 删除旧名字；GPU 设备丢失后的原版恢复行为仍待专项验证。

状态边界包含 attribute 栈、投影/模型/纹理矩阵栈、framebuffer、renderbuffer、shader program、active texture。立方体不排序面且关闭背面剔除，依赖 depth24 深度测试解决遮挡。没有完整画面 GPU→CPU→GPU 回读。

```text
原版 2D Tiles / 文字 / ImGui -> display_buffer -> 原版窗口背景
                                                   |
                          SDL_RenderFlush -> GL 独立 FBO（颜色+深度）
                                                   |
                                     GL 透明纹理叠加到窗口
                                                   |
                                 恢复状态 -> SDL_RenderFlush
                                                   |
                                 原版 SDL_RenderPresent（一次）
```

没有修改存档格式、回合、地图生成、AI、碰撞和动作派发。

## 可复现命令

在项目根目录 PowerShell 执行。`$toolchain` 指现有 MSYS2 UCRT64 根目录，不是其 bin；本机位于相邻项目 `.tools/msys64/ucrt64`。工具脚本默认使用 Release 配置、`-O1`，以降低本次验证的编译成本，不是最终性能发行参数。

```powershell
$toolchain = '../cddaproject/.tools/msys64/ucrt64'
git -C CDDA-0I-FP3D describe --tags --exact-match 0.I-1
git -C CDDA-0I-FP3D rev-parse 0.I-1
git -C CDDA-0I-FP3D merge-base HEAD 0.I-1

# 关闭 feature，打包到独立基线目录
./CDDA-0I-FP3D/tools/build-fp3d.ps1 -ToolchainRoot $toolchain -Feature OFF -RuntimeDir ./runtime/p0-0i1

# 开启 feature，使用相同编译参数
./CDDA-0I-FP3D/tools/build-fp3d.ps1 -ToolchainRoot $toolchain -Feature ON

# 原型、原版、自测；所有 userdir 均独立于原版发布包
./CDDA-0I-FP3D/tools/run-fp3d.ps1 -Mode Probe
./CDDA-0I-FP3D/tools/run-fp3d.ps1 -Mode Original
./CDDA-0I-FP3D/tools/run-fp3d.ps1 -Mode SelfTest -UserDir ./qa/0i1/selftest
```

本机 GCC 16 对原版 `vehicle.h:818` 触发新的 `-Wsfinae-incomplete`，首轮因原版 `-Werror` 失败。重试只在构建参数添加 `-Wno-error`，没有为此编辑游戏源码。保留 `qa/0i1/p0-build-first-attempt.log`；后续警告不意味着“零警告构建”。

## 验证记录

报告更新期间的事实如下；最终结论见末尾“验收结论”。

| 验证项 | 状态 / 证据 |
|---|---|
| 干净 tag、固定 commit、独立分支 | 已验证；与用户发布包 VERSION.txt 一致。 |
| 无修改原版编译 | 已通过；`qa/0i1/p0-configure.log`、`p0-build.log`。 |
| 原版启动及真实新世界 | 已实机启动，生成真实角色及避难所场景；测试资料位于 `qa/0i1/p0/`。 |
| P1 编译 | 已通过；`qa/0i1/p1-build.log`。 |
| UI + 深度立方体同窗口 | 已实机看到；包含原版主菜单、文字及鼠标光标。 |
| F5 50 轮 | 最终菜单修复版自测通过真实 SDL_KEYDOWN 事件路径交替请求 100 次，200 帧，FBO 仅分配一次。 |
| 实体键盘 F5 | 用户在带 OpenGL 3D probe 标题的窗口实测，并明确回复“是，能正常切换”；与上述自动自测分开记录。 |
| 窗口最大化 / 恢复 | 已通过桌面实机检查，立方体与原版 UI 仍可显示。 |
| 全屏进入 / 退出 | 自测调用原版 `toggle_fullscreen_window()`，检查真实窗口 flags；结果见运行日志。 |
| 连续 10 分钟 | 已通过主菜单场景：最终日志 `elapsed_ms=660157, frames=25746, toggles=100, target_allocations=1, gl_error=0`。自动切换后持续开启超过 10 分钟。 |
| Alt+Tab | 已实机切出到原版测试窗口再切回，立方体、文字和菜单恢复正常。 |
| OpenGL 创建失败回退 | `SDL_VIDEODRIVER=dummy` 注入无 OpenGL 环境，加速创建失败后原版选择软件 renderer；原型记录 disabled，程序继续存活。该场景预期有一条原版初始化错误日志，不计作正常 GPU 场景的图形错误。 |
| Feature OFF | 重新编译通过；直接 OpenGL DLL 导入为 False；随后恢复 ON 构建通过。日志 `feature-off-build.log`、`feature-off-check.log`、`feature-on-restore.log`。 |
| 原版背包/移动 | 已验证真实新世界、角色移动并推进原版时间、打开背包及原版 ImGui 整理界面。 |
| 原版保存/退出/读取 | 已验证真实 Karns 世界、Mabel Dukes 角色；稳定版实际格式是 `.sav.zzip`。原版保存后可由原版和 Probe 分别读取；Probe 亦成功保存返回菜单。 |
| 保存状态对照 | 相同原版存档分别经原版、Probe 读入并保存；角色位置、游戏时间、行动点、身体生命值、耐力、背包一致。穿戴物仅一条内裤描述中的装饰颜色不同，未宣称全存档逐字节一致。详见回归补充。 |
| 门 | 原版及 Probe 均通过正常方向移动打开避难所北门、再通过原版关门命令关闭；消息和可见区域变化已实机观察并留存截图。原版门开关自动测试 10 项断言通过。 |
| 战斗 | 标准门与近战测试共 15 个用例、71,989 项断言通过；后续原版及立方体开启的 Probe 均实机拳击普通僵尸造成 1 点伤害，并成功保存。目标由独立 QA 内容脚本放置，原版规则不变。 |
| 图块包切换 | 原版及最终 Probe 均在同一已载入世界内将 UltimateCataclysm 热切换到 RetroDays，角色和游戏时间保持；Probe 退出菜单后立方体自动恢复。 |
| 1080p/60 FPS | 未做性能基准；沿用原版 25ms 刷新节流，立方体实验目标约 40 次/秒，不宣称达到最终目标。 |
| 设备丢失、显存泄漏分析 | 未验证；单次分配计数不等于 GPU 驱动泄漏检测。 |

注意：Windows 原版 DebugLog 的包装层会在后续消息/换行到来时才把上一条完整落盘，读取仍在运行的日志可能落后一条。桌面自动化中多个完全同名窗口也曾混淆目标，因此原型使用可区分标题；较早混淆窗口的截图不作为验收证据。

## 验收结论

**P0 原版基础回归及 P1 同窗口合成基础验收已通过，可以进入 P2。** 用户于 2026-10-26 要求持续按完整计划推进，后续按阶段顺序执行。

已证实：指定 tag 的源码可构建；原版真实世界可启动、移动、打开背包；单窗口 UI+深度立方体可运行超过 10 分钟；50 轮自动 F5、窗口尺寸/全屏往返、桌面最大化/恢复、Alt+Tab 和初始化失败回退已分别验证。程序的地图/行动/存档代码没有改动。

50 轮 F5 是由独立自测经 SDL 事件队列完成，并不是人工逐次按键的录像。实体键盘切换已由用户另行确认正常。

新补齐的证据包括：原版与 Probe 基础近战、战后保存、开关门、同一世界内热切换、菜单遮挡修复。最终代码自测日志记录 `elapsed_ms=660121, frames=25717, toggles=100, target_allocations=1, gl_error=0`，自动切换结束后持续渲染超过十分钟；已载入世界换包后也连续渲染超过十分钟，`elapsed_ms=795121, frames=29100, target_allocations=1, gl_error=0`（该时长包含前面的菜单停留，换包实际发生于 01:21:43，记录时间为 01:32:43）。

自然生成僵尸的击杀与搜刮闭环仍属 P5；无 feature 原版的 FPS/显存基准、设备丢失及驱动级泄漏分析尚未完成，作为后续专项保留，不宣称满足最终性能目标。此前文字裁切来自测试字体配置；新测试采用 Consolas 等宽字体后可正常显示，没有修改原版字体实现。

连续运行证据：项目根目录 `qa/0i1/p1-soak-final.log`；回退证据 `qa/0i1/fallback-final.log`；同窗截图 `qa/0i1/p1-evidence.png`。测试世界均在项目内的独立目录，未读写真实玩家存档。

本机二进制 SHA-256：

```text
Original: 58DCDDF21B30A0EAF22ECB60C656E04C0AD74474945C0D773C56122B914F62A5
Probe:    177C05B1575C02D659E688134FC413FAC4654545834EB324D783153F502AB44E
```

最终构建日志：`qa/0i1/p1-final-build.log`。最终版本删除临时按键追踪，并将立方体移至左侧地图区域，避免遮挡默认右侧生命值栏。最终布局在已载入 RetroDays 世界中也已连续运行超过十分钟：`elapsed_ms=960216, frames=39658, target_allocations=1, gl_error=0`。50 轮自测证据仍对应相同合成逻辑的较早布局版本。

最终菜单修复版证据另存于 `qa/0i1/p1-ui-final-selftest/`、`p1-ui-final/`；关闭 feature 检查见 `p1-ui-feature-off-check.json`，再次故障回退见 `p1-ui-fallback-final.log` 及 `p1-ui-fallback-check.json`。

下一步进入 P2 只读地图快照与可见性验证。当前 GL 固定管线仍只用于可行性实验，不预先锁定长期地图渲染实现。详细证据与异常见 [回归补充](FP3D_0I1_REGRESSION.md)。
