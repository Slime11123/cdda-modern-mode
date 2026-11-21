# CDDA Modern Mode — Agent 操作入口
 
## SDL3 性能优化任务路由（2026-11-19 用户最新指定）
- 2026-11-19 用户再次指定：最终完全使用 SDL3 GPU API，不再使用 OpenGL。优先执行外层《SDL3_GPU原生渲染迁移执行计划_2026-11-19.md》，此路线覆盖下面旧的先优化 OpenGL/双后端方案。原生 GPU 未完整接通前保留当前能运行的独立旧版，不以删减现有功能换迁移。
- 最新用户要求：优先迁移为纯 SDL3 GPU API（不再使用 OpenGL），再优化；以外层《SDL3_GPU原生渲染迁移_AI执行计划_2026-11-19.md》为执行路线。旧 OpenGL 仅留作对照及回退备份。
- 本工作树的渲染性能任务仅修改 CDDA-0I-FP3D-SDL3，执行外层工作区《3D渲染GPU优化_AI执行计划_SDL3版_2026-11-19.md》。本条覆盖下文历史 SDL2 路径及部署入口的冲突部分；其余保护规则继续有效。
- SDL3 构建目录 build/p6-sdl3；运行目录 runtime/p6-sdl3；入口 Launch-P6.cmd，独立 userdir。不得误编译、修改或部署旧 CDDA-0I-FP3D/、build/0i1 或 runtime/0i1 的正式 EXE。旧正式启动器不自动修改。
- SDL3 平台迁移已完成，当前为 SDL3 + OpenGL；按新计划参考 yuganxia 的 GPU 实例化、区块剔除、贴图缓存、Fence，再独立评估 SDL3 GPU 后端，保留全部现有内容和 Photon。不得擅自提交、推送或发布。


> 供 Codex、GPT、Claude 等 AI Agent 使用。提交代码、打包或上传前，**必须先阅读 [完整发布操作手册](docs/AI_SOURCE_AND_RELEASE_WORKFLOW.md)**。

## 固定项目位置

- GitHub：`https://github.com/Slime11123/cdda-modern-mode`，默认分支 `main`；本项目是**独立仓库，不是 Fork**。
- **唯一正式开发源码目录**：`C:\Users\StickySlime\Documents\Codex\Projects\cdda3dproject\CDDA-0I-FP3D`
- 外层工作区：`C:\Users\StickySlime\Documents\Codex\Projects\cdda3dproject`，其中的 `build/`、`runtime/`、`qa/`、`assets/`、`release-staging/` 不是 Git 源码目录。
- 可能另有 `Documents\GitHub\cdda-modern-mode` 旧克隆。**绝不能把它误当成正在编辑的源码目录。**
- 官方原版：CDDA 0.I-1，Windows SDL2 Tiles + 实验性 OpenGL 3D。`origin` 应指向此用户仓库，`upstream` 才指向官方 `CleverRaven/Cataclysm-DDA`。

## 本地游戏更新的优先入口

- **用户指定的首要启动器**：外层工作区的 `启动3D操作修复验证.cmd`。涉及本地游戏功能、修复或构建更新时，优先保证用户双击此启动器即可运行本次最新版本。
- 当前启动链：`启动3D操作修复验证.cmd` → `CDDA-0I-FP3D/tools/run-fp3d.ps1` → `runtime/0i1/cataclysm-tiles-models-latest.exe`。每次更新先核实启动器的实际目标；编译和相关验证完成后，将新产物部署到该目标，并同步 `runtime/0i1/.models-latest-build.json`、核对 SHA256。
- 若启动参数、路径或启动器内的版本/功能提示因本次修改而过时，应同步更新该 `.cmd`。不能只更新 `build/` 或另一个测试 EXE 就宣称用户常用入口已更新。
- 交付时明确此启动器是否已指向本次新产物及实际验证情况；保护已有存档和设置。此规则适用于本地开发入口，正式发布仍遵守下述打包规则。

## 必须遵守的行为规范

1. 操作前执行 `git rev-parse --show-toplevel`、`git status -sb`、`git remote -v`、`git log -1 --oneline`。确认没有其他 Agent 并发写文件；**不覆盖、不丢弃、不擅自提交别的任务的改动**。
2. 先审查差异和未跟踪文件，再明确暂存范围，运行 `git diff --cached --check`、`git diff --cached --stat`。**禁止盲目 `git add .`、`git add -A`**；禁用 `git reset --hard`、`git clean -fdx`、`git push --force`。
3. 所有源码提交仅推到 `origin/main`。**绝不向官方 CDDA 的 upstream 推送**。提交和远程推送都要确认 SHA；GitHub Desktop 的 Commit 不等于 Push。
4. README 保留顶部 **OpenAI GPT 生成与协助开发声明**、CDDA 原版署名及非官方声明。**F5 打开统一模式设置窗口（含光影包）；3D 世界内 F6 在第一人称与 3D 上帝视角（俯视）之间切换；2D 中 F6 仍打开设置窗口**。不要再写成旧版 F5 直接循环模式，也不要写成 F6 只开设置。
5. **源码上传 ≠ 可游玩 ZIP**。必须重新编译当前提交，验证最新 EXE，单独复制运行 DLL、游戏 `data/`、获授权 `gfx/`、已编译翻译和许可证，放入仓库外发布目录；不得直接上传整个旧 `runtime`。
6. **不包含开发者存档、用户配置、QA 缓存、密钥、日志、外部下载的 UndeadPeople/光影素材**。除非确认相关第三方素材可依法再分发；参考 [版权说明](docs/ATTRIBUTION.md)。
7. 未经过实际测试，不宣称“稳定可玩”。Windows Alpha 版本标为 **GitHub Pre-release**；发布后检查远程附件 ZIP 与 SHA256SUMS 的文件名、大小和校验值。
8. **凭据不得写入日志、源码、文档或聊天**。使用 GitHub Desktop 或 `gh auth login` 获得用户授权。
9. 网络、编译、许可证、测试、上传失败时停止后续步骤，明确说明已成功及未完成的部分。**不要虚报成功**。
10. **每次独立测试结束后，必须立即清理本次测试产生且不再需要的临时文件，避免占用磁盘空间。** 先提取并保留必要的测试结论、对照数据和问题证据，再删除临时用户目录/世界副本、测试专用 EXE 副本、重复日志、转储和缓存；失败或中断的测试也要清理。仅清理本次任务创建、已确认不再被进程使用且不再供后续步骤使用的文件，删除前核实绝对路径；不得删除正式存档、用户配置、源码、冻结基线或仍需用于定位问题的唯一证据。交付时说明清理情况，不把整批测试结束后统一清理代替逐次清理。

## 任务路由

- **更新代码或 README**：执行 [完整手册第 2–3 节](docs/AI_SOURCE_AND_RELEASE_WORKFLOW.md)，不需要制作 Release。
- **生成本地玩家 ZIP**：先提交并验证，再执行 [完整手册第 4–7 节](docs/AI_SOURCE_AND_RELEASE_WORKFLOW.md)。
- **上传 GitHub Release**：仅当用户明确要求对外发布，完整完成 [第 4–9 节](docs/AI_SOURCE_AND_RELEASE_WORKFLOW.md)。
- **历史 `v0.1.0-alpha.1`**：仅是 2026-11-04 的参考版本。后续版本必须重新核对源码 SHA 和构建输出，**不得复制旧 EXE 冒充最新版本**。
