# CDDA Modern Mode — Agent 操作入口

> 供 Codex、GPT、Claude 等 AI Agent 使用。提交代码、打包或上传前，**必须先阅读 [完整发布操作手册](docs/AI_SOURCE_AND_RELEASE_WORKFLOW.md)**。

## 固定项目位置

- GitHub：`https://github.com/Slime11123/cdda-modern-mode`，默认分支 `main`；本项目是**独立仓库，不是 Fork**。
- **唯一正式开发源码目录**：`C:\Users\StickySlime\Documents\Codex\Projects\cdda3dproject\CDDA-0I-FP3D`
- 外层工作区：`C:\Users\StickySlime\Documents\Codex\Projects\cdda3dproject`，其中的 `build/`、`runtime/`、`qa/`、`assets/`、`release-staging/` 不是 Git 源码目录。
- 可能另有 `Documents\GitHub\cdda-modern-mode` 旧克隆。**绝不能把它误当成正在编辑的源码目录。**
- 官方原版：CDDA 0.I-1，Windows SDL2 Tiles + 实验性 OpenGL 3D。`origin` 应指向此用户仓库，`upstream` 才指向官方 `CleverRaven/Cataclysm-DDA`。

## 必须遵守的行为规范

1. 操作前执行 `git rev-parse --show-toplevel`、`git status -sb`、`git remote -v`、`git log -1 --oneline`。确认没有其他 Agent 并发写文件；**不覆盖、不丢弃、不擅自提交别的任务的改动**。
2. 先审查差异和未跟踪文件，再明确暂存范围，运行 `git diff --cached --check`、`git diff --cached --stat`。**禁止盲目 `git add .`、`git add -A`**；禁用 `git reset --hard`、`git clean -fdx`、`git push --force`。
3. 所有源码提交仅推到 `origin/main`。**绝不向官方 CDDA 的 upstream 推送**。提交和远程推送都要确认 SHA；GitHub Desktop 的 Commit 不等于 Push。
4. README 保留顶部 **OpenAI GPT 生成与协助开发声明**、CDDA 原版署名及非官方声明。**F5 / F6 当前是统一模式设置窗口**，不再写成旧版 F5 直接循环模式。
5. **源码上传 ≠ 可游玩 ZIP**。必须重新编译当前提交，验证最新 EXE，单独复制运行 DLL、游戏 `data/`、获授权 `gfx/`、已编译翻译和许可证，放入仓库外发布目录；不得直接上传整个旧 `runtime`。
6. **不包含开发者存档、用户配置、QA 缓存、密钥、日志、外部下载的 UndeadPeople/光影素材**。除非确认相关第三方素材可依法再分发；参考 [版权说明](docs/ATTRIBUTION.md)。
7. 未经过实际测试，不宣称“稳定可玩”。Windows Alpha 版本标为 **GitHub Pre-release**；发布后检查远程附件 ZIP 与 SHA256SUMS 的文件名、大小和校验值。
8. **凭据不得写入日志、源码、文档或聊天**。使用 GitHub Desktop 或 `gh auth login` 获得用户授权。
9. 网络、编译、许可证、测试、上传失败时停止后续步骤，明确说明已成功及未完成的部分。**不要虚报成功**。

## 任务路由

- **更新代码或 README**：执行 [完整手册第 2–3 节](docs/AI_SOURCE_AND_RELEASE_WORKFLOW.md)，不需要制作 Release。
- **生成本地玩家 ZIP**：先提交并验证，再执行 [完整手册第 4–7 节](docs/AI_SOURCE_AND_RELEASE_WORKFLOW.md)。
- **上传 GitHub Release**：仅当用户明确要求对外发布，完整完成 [第 4–9 节](docs/AI_SOURCE_AND_RELEASE_WORKFLOW.md)。
- **历史 `v0.1.0-alpha.1`**：仅是 2026-11-04 的参考版本。后续版本必须重新核对源码 SHA 和构建输出，**不得复制旧 EXE 冒充最新版本**。
