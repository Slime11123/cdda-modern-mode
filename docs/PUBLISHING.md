# CDDA Modern Mode — GitHub Publishing Checklist / 独立仓库发布清单

**Chosen publishing model: an independent GitHub repository, NOT a GitHub Fork. / 已选择新建独立 GitHub 仓库，不使用 Fork。**

This is a publication checklist only. It does not create a GitHub repository, commit the source, or push code automatically.

## 1. Correct local source directory / 正确源码路径

`C:/Users/StickySlime/Documents/Codex/Projects/cdda3dproject/CDDA-0I-FP3D`

**Never upload the whole outer `cdda3dproject` directory.** The outer workspace also includes obsolete source, build output, runtime, QA, toolchains, archives, user data and separately downloaded tilesets.

The existing Git `origin` originally points to the official `https://github.com/CleverRaven/Cataclysm-DDA.git`. **Never push Modern Mode commits to the official CDDA repository.** Keep this URL only as `upstream` for historical reference and possible future pulls.

## 2. Prepublication review / 提交前审查

1. Run `git status --short` in the correct repository and review every uncommitted game-code change. This working tree is actively developed.
2. Verify `README.md`, `README.zh-CN.md`, `docs/UPSTREAM_README.md`, `docs/ATTRIBUTION.md`, `LICENSE.txt` and other relevant license files.
3. Audit staged content: `git diff --cached --stat`, `git diff --cached`; check for sensitive files, third-party art, logs, saves and binaries.
4. Run `git diff --check`; validate the Windows ON/OFF build and relevant regressions before presenting uncommitted code as tested.
5. Check whether the **existing committed Git history** already contains any files that must not be redistributed. `.gitignore` only prevents *new untracked additions*; it neither deletes committed files nor cleans past history.

## 3. Create an empty independent repository / 在 GitHub 新建空仓库

1. Open https://github.com/new and choose your own account as Owner.
2. Repository name: `cdda-modern-mode` (if available). Display title within README: **CDDA Modern Mode**.
3. Suggested description: `An unofficial modern controls and experimental first-person 3D mode for Cataclysm: Dark Days Ahead.`
4. Choose Public when ready; Private first is also possible for review.
5. **Do NOT** initialize the new repository with a README, `.gitignore`, or license because these already exist locally.
6. Click **Create repository**. This creates a new, standalone GitHub repository with no `forked from` label.

Do not use the **Fork** button or GitHub's fork creation URL.

## 4. Point local Git to your independent repository / 关联独立仓库

Only after the new repository exists and the URL is verified, run the following from the **CDDA-0I-FP3D** folder:

```powershell
git remote rename origin upstream
git remote add origin https://github.com/Slime11123/cdda-modern-mode.git
git remote -v
```

Expected: `origin` points to your personal standalone repo and `upstream` points to official CDDA. If a remote has already been renamed/added by another tool or developer, inspect it and adjust rather than rerunning commands blindly.

To work in GitHub Desktop, use File → Add Local Repository and choose exactly `CDDA-0I-FP3D`. Check Repository → Repository Settings → Remote before pushing.

## 5. Commit and push / 审查后提交并上传

The original development history lives on `feature/fp3d-0i` (a shallow derivative of official CDDA). After the independent initial import, develop and commit new public updates on `main`; do not push the shallow historical feature branch. Uncommitted gameplay changes must be reviewed and tested before being included. The first documentation-only commit was staged selectively:

```powershell
git add README.md README.zh-CN.md .gitignore docs/UPSTREAM_README.md docs/ATTRIBUTION.md docs/PUBLISHING.md
git diff --cached --stat
git diff --cached
git commit -m "docs: prepare CDDA Modern Mode independent repository"
```

**Documentation-only commits do not include uncommitted gameplay changes.** The standalone `main` branch is created from the audited committed tree as a new history root. Once it is published, use ordinary commits on `main` to send reviewed future updates:

```powershell
git status --short
git add -- <reviewed-file-paths>
git diff --cached --check
git commit -m "Describe the tested changes"
git push origin main
```

Do not run `git switch -c main` if `main` exists. Do not push the archived shallow development-history branch. Never force-push or blindly stage the entire outer workspace.

**Important:** The local development repository was shallow. For the standalone GitHub project, `main` is initialized using a **fresh root commit containing a complete audited snapshot of the committed source tree**, rather than pushing the truncated upstream commit ancestry. The original development branch and history stay available locally. Future changes should be committed directly on the new `main` branch after testing. This does not remove CDDA's licenses or attribution requirements.

## 6. Separate source and player downloads / 源码与下载包分开

Public source code is not a tested installer. A later `v0.1.0-alpha` Release requires a clean staging folder, reproducible Windows build, appropriate game data, executable and DLLs, license notices, and separate checks for third-party tileset redistribution permissions.

Do not package copied UndeadPeople assets or Photon shaders without verifying the actual redistribution rights. Verify startup, 2D/3D switching, inputs, UI, saves, and gameplay on a clean Windows environment. Mark incomplete or unverified systems honestly.

## 7. Naming and attribution / 命名与署名

- Project display name: **CDDA Modern Mode**.
- Independent GitHub repository slug: **cdda-modern-mode**.
- Local source folder stays **CDDA-0I-FP3D** for now to avoid breaking runtime/build paths.
- Keep CMake `project(CataclysmDDA)`, executable names and data identifiers until separately tested.
- Credit [Cataclysm: Dark Days Ahead](https://github.com/CleverRaven/Cataclysm-DDA) and preserve CC BY-SA 3.0 and separately licensed component notices.
- Project is unofficial and not endorsed by the CDDA developers.

**Current state:** Files prepared locally. No new remote repository created and no Git commit/push performed by this document edit.
