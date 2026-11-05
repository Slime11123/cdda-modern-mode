# CDDA Modern Mode — AI Agent 源码提交、游戏打包与 GitHub Release 手册

更新时间：2026-11-05。适用于有权访问用户 Windows 开发电脑和 Slime11123/cdda-modern-mode 的 Codex、GPT、Claude 等 AI Agent。

**执行前必须阅读根目录 [AGENTS.md](../AGENTS.md)。本文是可以逐步执行的操作规范，不意味着相关步骤已经执行或自动通过。**

## 1. 项目与固定位置

| 用途 | 地址 |
| --- | --- |
| **唯一正式开发源码** | C:\Users\StickySlime\Documents\Codex\Projects\cdda3dproject\CDDA-0I-FP3D |
| 外层工作区 | C:\Users\StickySlime\Documents\Codex\Projects\cdda3dproject |
| 示例构建目录 | 工作区\build\0i1（复用前检查缓存） |
| 运行包 / QA / 资产 | 工作区\runtime、qa、assets（不是 Git 根目录） |
| 打包位置 | 工作区\release-staging\<版本号独立文件夹> |
| **GitHub origin** | https://github.com/Slime11123/cdda-modern-mode.git |
| Git 默认分支 | main |
| 原版 upstream | CleverRaven/Cataclysm-DDA（只作参考，绝不向其推送） |

此项目源于 CDDA 官方 0.I-1，Windows SDL2 Tiles + 实验性 OpenGL 3D，构建条件 TILES=ON、ENABLE_FP3D=ON。**Documents\GitHub\cdda-modern-mode 可能是旧克隆，绝不把它当成用户实际修改的源码目录。**

任务分三种：**A 源码/README 提交**（只做第 2–3 节）、**B 本地生成 Windows ZIP**（第 2、4–7 节）、**C 源码+ZIP+GitHub 发布**（全部章节）。只有用户明确要求公开发布时才执行 C。

## 2. 操作前预检（PowerShell）

~~~powershell
$Repo = 'C:\Users\StickySlime\Documents\Codex\Projects\cdda3dproject\CDDA-0I-FP3D'
$Workspace = Split-Path $Repo -Parent
Set-Location -LiteralPath $Repo
git rev-parse --show-toplevel
git status -sb
git branch --show-current
git remote -v
git log -5 --format='%h %ci %s'
git ls-remote origin refs/heads/main
git diff --stat
git diff --check
git ls-files --others --exclude-standard
~~~

确认项目根路径正确，origin 是用户自己的仓库，当前工作树中是否有其他 Agent 的并发修改、已暂存/未跟踪文件。**绝不能覆盖、丢弃、误提交别人的工作**。禁止未审核的 git add .、git add -A，以及 git reset --hard、git clean -fdx、git push --force。远端发生变化时先 git fetch origin、比较分支，不盲目 pull 覆盖本地。

## 3. 更新 README、审查代码、提交和推送

1. 用最新源码和有证据的测试记录更新 README.md、README.zh-CN.md、相关 docs。保留文件顶部“本项目由 OpenAI GPT 生成并协助开发”的中英文声明、CDDA 原版署名、独立非官方项目声明。
2. 当前 F5/F6 打开**统一模式设置**（2D/3D 画面、经典/现代键位、原版侧栏/现代 HUD 三组设置），不能再照旧 README 写成“F5 循环三模式”。
3. 逐个审查 git diff，检查新增文档链接是否存在、声明是否准确；未进行完整人工验收则不得宣称“稳定可玩”，未亲自跑过的测试不得标为通过。
4. 仅暂存审核过的文件。以下只是**仅修改 README**的示例：

~~~powershell
git add -- README.md README.zh-CN.md
git diff --cached --name-status
git diff --cached --check
git diff --cached --stat
git diff --cached
git commit -m "docs: update bilingual Modern Mode README"
~~~

需要提交源代码时，用明确的 src/...、tests/...、docs/... 文件路径 git add。只有确认全部受跟踪修改均属于本次任务时才考虑 git add -u；仍须单独检查所有新文件和隐私。**不得把整个外层工作区、运行 EXE、个人存档、密钥或不明素材加入 Git**。

~~~powershell
git status -sb
git push origin main
git rev-parse HEAD
git ls-remote origin refs/heads/main
~~~

两个 SHA 必须一致，并从 GitHub 重新读取新版 README/源码确认。GitHub Desktop 的 Commit 不等于 Push。**上传源码不等于完成可玩 Release。**

## 4. 从当前源码构建 Windows 游戏

严格顺序：确认已提交的源码 SHA → 核对编译缓存和编译器 → **构建当前源码** → 获取新 EXE → 用它组包。绝不能从旧 runtime 直接复制 EXE 后冒称新版本。

~~~powershell
$Repo = 'C:\Users\StickySlime\Documents\Codex\Projects\cdda3dproject\CDDA-0I-FP3D'
$Workspace = Split-Path $Repo -Parent
$Build = Join-Path $Workspace 'build\0i1'
$CMake = 'C:\Users\StickySlime\Documents\Codex\Projects\cddaproject\.tools\msys64\ucrt64\bin\cmake.exe'
$Commit = (git -C $Repo rev-parse HEAD).Trim()
Test-Path $CMake
Test-Path (Join-Path $Build 'CMakeCache.txt')
Select-String (Join-Path $Build 'CMakeCache.txt') -Pattern 'CMAKE_HOME_DIRECTORY','TILES:BOOL','ENABLE_FP3D:BOOL','CMAKE_CXX_COMPILER'
& $CMake --build $Build --target cataclysm-tiles --parallel 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed; stop release' }
$Exe = Join-Path $Build 'src\cataclysm-tiles.exe'
if (!(Test-Path $Exe)) { throw 'EXE missing; stop release' }
Get-Item $Exe | Select-Object FullName,Length,LastWriteTime
Get-FileHash $Exe -Algorithm SHA256
~~~

上述 CMake 及 build 路径只代表上一轮已验证的本机路径；若工具链变化就重新定位。**CMAKE_HOME_DIRECTORY 必须等于此项目的源码路径；TILES 和 ENABLE_FP3D 均需 ON。** 若缓存指向其他项目，用 [原版 CMake 编译说明](../doc/c++/COMPILING-CMAKE.md)新建独立构建目录再编译。

“ninja: no work to do”并不自动证明 EXE 是当前提交：核对新增源文件进入构建、链接产物、依赖时间与提交；必要时从干净目录重建。有条件时进行 FP3D ON/OFF 构建及对应回归测试；目标名先查询当前构建系统，不要猜测。编译失败、实际运行依赖未知时停止发行。

## 5. 生成干净玩家目录

在源码仓库**外**的工作区 release-staging 下新建唯一版本文件夹，例：

~~~text
CDDA-Modern-Mode-v0.1.0-alpha.2-Windows-x64/
  cataclysm-tiles.exe               # 从第 4 节刚编译的新 EXE
  *.dll                             # 已审核来源、架构和许可证
  Play-CDDA-Modern-Mode.cmd
  BUILD-INFO.txt                    # 版本、提交 SHA、编译时间和选项
  READ-ME-FIRST.txt
  LICENSE.txt
  README.md
  README.zh-CN.md
  data/                             # 排除本地 data/cache 和测试截图
  gfx/                              # 仅可再分发的内置图块
  lang/mo/
  lang/json/
  docs/                             # Release notes、署名及许可证
  user/                             # 空目录，绝不复制开发者存档
~~~

从刚编译的 $Exe 复制到玩家目录；DLL 可以参照以前的验证运行包，但**不得带入旧 EXE**，还须核对所需 DLL 的许可证及 x64 架构。复制源码 data、gfx、编译后的 lang/mo 和运行需要的 lang/json；Robocopy 返回码 **0–7 通常非失败，8 及以上失败**，应核对复制日志与实际文件数量。

仅用相对路径的启动脚本，示例（若游戏启动参数变化，须按当前源码校正）：

~~~bat
@echo off
setlocal
cd /d "%~dp0"
if not exist "user" mkdir "user"
set "CDDA_FP3D_START_ACTIVE=1"
start "" /D "%~dp0" "%~dp0cataclysm-tiles.exe" --basepath "%~dp0" --userdir "%~dp0user"
~~~

**不要用 tools/run-fp3d.ps1 当玩家启动器**，它依赖外层工作区、开发者运行包、本机字体和第三方素材。

## 6. 版权与隐私门禁

- CDDA 主体内容主要采用 CC BY-SA 3.0；保留原版 LICENSE、上游作者署名、修改声明。字体、图块、DLL、依赖库等可能有单独许可证，必须逐项核对并附上要求的声明。
- 外层 assets/ 中的 **UndeadPeople** 图块整体再分发许可尚不能全面确认；**默认不随公开 ZIP 打包**，即便它能够从 GitHub 下载、供本地游玩。只有取得可验证的完整许可并完成来源审核才可改变。
- 不含 Photon 等外部光影、任何未确认授权的图块/音效、用户存档、账号凭据、开发者配置、qa/、logs、cache、测试截图或其他私人信息。
- 保留 OpenAI GPT 生成与协助开发声明，同时明确该项目不属于 OpenAI 或 CDDA 官方。详见 [ATTRIBUTION.md](ATTRIBUTION.md)。
- 如许可证或来源不清，应删除相关素材或停止公开发行，不得自行假定为开源。

## 7. 验证、ZIP、校验值

1. **从玩家目录运行玩家 EXE**，不要借用开发者源码目录作为 basepath。使用独立空白 user/，运行 --help、--jsonverify。Windows GUI EXE 通过 PowerShell 直接调用时有时不能可靠地取得退出码，使用 System.Diagnostics.Process 等待并检查 ExitCode=0，保存输出和必要的错误信息。
2. 尽可能人工验证进入主菜单、新建世界、F5/F6、现代 2D / 第一人称 3D、WASD、Tab、鼠标交互、保存与载入。不能测到的项目如实标记“未验证”，不能用 --help/jsonverify 冒充完整游玩验收。
3. 检查玩家目录 user/ 初始为空，data/gfx/DLL/许可证完整，没有个人资料和外部 UndeadPeople。
4. 创建包含**唯一顶层版本目录**的 ZIP，例如：

~~~powershell
$Tag = 'v0.1.0-alpha.2'    # 示例；用未使用的新 Tag
$Folder = "CDDA-Modern-Mode-$Tag-Windows-x64"
$Out = Join-Path $Workspace 'release-staging'
$Zip = Join-Path $Out "$Folder.zip"
if (Test-Path $Zip) { throw 'Version ZIP exists; do not overwrite' }
tar.exe -a -cf $Zip -C $Out $Folder
if ($LASTEXITCODE -ne 0) { throw 'ZIP failed' }
tar.exe -tf $Zip | Select-Object -First 20
Get-Item $Zip | Select-Object Name,Length
Get-FileHash $Zip -Algorithm SHA256
~~~

5. 在同目录生成 SHA256SUMS.txt，内容为“64 位十六进制 SHA-256 + 两个空格 + ZIP 的完整文件名”。把真实新增功能、已知问题、安装方法和测试情况写入 docs/RELEASE_NOTES_<Tag>.md。核对 Release 版本号、文件名称和 README 说明一致。

历史 v0.1.0-alpha.1 ZIP：**121,804,427 字节**，SHA-256：B740FE5887295248D009EA728C48C68830C4C06594ECD6C21857E9CD9FF31465。这只用来确认**旧版**，新版本不能复用其 EXE 或哈希。

## 8. GitHub Release 上传（仅当用户明确要求）

首先确保 main 已推送，远程 SHA 与本地一致，拟发布的 Tag 不存在。**使用新版本号，不覆盖之前公开的 Release**；若遇到同名草稿/部分上传附件，先查明状态再处理，禁止反复创建。

~~~powershell
gh auth status              # 未授权则由用户执行 gh auth login --web
$Commit = (git -C $Repo rev-parse HEAD).Trim()
$Tag = 'v0.1.0-alpha.2'    # 替换为实际未使用版本
$Folder = "CDDA-Modern-Mode-$Tag-Windows-x64"
$Zip = Join-Path $Workspace "release-staging\$Folder.zip"
$Sums = Join-Path $Workspace 'release-staging\SHA256SUMS.txt'
$Notes = Join-Path $Repo "docs\RELEASE_NOTES_$Tag.md"
gh release view $Tag -R Slime11123/cdda-modern-mode   # 已存在时停止并调查
# 确认 Tag 不存在、构建/测试/许可全部满足后才执行：
gh release create $Tag $Zip $Sums -R Slime11123/cdda-modern-mode --target $Commit --title "CDDA Modern Mode $Tag - Windows x64 Preview" --notes-file $Notes --prerelease --latest=false
~~~

网页手动上传：打开 https://github.com/Slime11123/cdda-modern-mode/releases/new ，填写新 Tag、Target=main、Title=CDDA Modern Mode <版本> - Windows x64 Preview、Release Notes=对应文档；上传同一版本的 Windows ZIP 与 SHA256SUMS.txt，勾选 **Pre-release**，确认两个附件真正上传完成后再 Publish。GitHub 自动提供的 Source code ZIP **不是可运行游戏包**。

**不要把 token、PAT、密码或一次性验证码写进命令日志、文档或聊天。**

## 9. 发布后远程验收与回报

~~~powershell
git -C $Repo rev-parse HEAD
git -C $Repo ls-remote origin refs/heads/main
gh release view $Tag -R Slime11123/cdda-modern-mode --json tagName,isDraft,isPrerelease,targetCommitish,url,assets
~~~

必须确认 GitHub 远端 main SHA 正确、Tag 对应目标提交、Release isDraft=false 且 isPrerelease=true。GitHub 中**两份附件**（Windows ZIP、SHA256SUMS.txt）应都存在且完全上传；远端 ZIP 大小与本地字节数相同。有条件时把公开 ZIP 重新下载并比对 SHA-256。

向用户明确报告：提交 SHA、Tag、Release URL、ZIP 文件名/大小、SHA-256、**实际已完成的测试与尚未完成的测试**、素材版权状态。失败时只陈述已验证的阶段，不得因为开始上传或出现 Draft 就声称已公开可玩。

## 10. 交接提示词

> 请在 C:\Users\StickySlime\Documents\Codex\Projects\cdda3dproject\CDDA-0I-FP3D 操作 CDDA Modern Mode，先阅读根目录 AGENTS.md 和 docs/AI_SOURCE_AND_RELEASE_WORKFLOW.md。核对真实 Git 状态和正在修改的文件，仅提交已审查的源码/README。如需玩家版，从当次提交重新编译 Windows TILES=ON + ENABLE_FP3D=ON，组装仓库外的完整 ZIP，排除存档/QA/未授权 UndeadPeople，执行独立验证、生成 SHA-256，再发布 GitHub Pre-release。最后核对远程 SHA、公开 ZIP 与校验文件的字节数、下载链接，报告测试与版权边界。
